// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderengine.h"
#include "qtutils.h"
#include "rendersceneindex.h"
#include <QColorSpace>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QSurfaceFormat>
#include <QtGui/qopengl.h>
#include <algorithm>
#include <memory>
#include <pxr/base/gf/rotation.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/taskControllerSceneIndex.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

namespace {

    class OpenGLContextRestore final {
    public:
        OpenGLContextRestore()
            : m_context(QOpenGLContext::currentContext())
            , m_surface(m_context ? m_context->surface() : nullptr)
        {}

        ~OpenGLContextRestore()
        {
            QOpenGLContext* current = QOpenGLContext::currentContext();
            if (current && current != m_context)
                current->doneCurrent();

            if (m_context && m_surface && QOpenGLContext::currentContext() != m_context)
                m_context->makeCurrent(m_surface);
        }

        OpenGLContextRestore(const OpenGLContextRestore&) = delete;
        OpenGLContextRestore& operator=(const OpenGLContextRestore&) = delete;

    private:
        QOpenGLContext* m_context = nullptr;
        QSurface* m_surface = nullptr;
    };

    struct SceneIndices {
        HdMergingSceneIndexRefPtr merging;
        TfRefPtr<RenderSceneIndex> renderSceneIndex;
        UsdImagingSceneIndices auxiliary;
        bool auxiliaryInserted = false;

        void clearAuxiliary()
        {
            auxiliary = {};
            auxiliaryInserted = false;
        }
    };

    class ImagingGLEngine final : public UsdImagingGLEngine {
    public:
        explicit ImagingGLEngine(const UsdImagingGLEngine::Parameters& params)
            : ImagingGLEngine(params, std::make_shared<SceneIndices>())
        {}

        SceneIndices& sceneIndices() { return *m_sceneIndices; }
        const SceneIndices& sceneIndices() const { return *m_sceneIndices; }

        void setSelectionOutline(bool enabled, unsigned int radius)
        {
            if (!_taskControllerSceneIndex)
                return;

            _taskControllerSceneIndex->SetSelectionEnableOutline(enabled);
            _taskControllerSceneIndex->SetSelectionOutlineRadius(radius);
        }

        void renderBatchWithDepthBias(const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                                      float constantFactor, float slopeFactor, const GfVec4f& wireframeColor)
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            // Match UsdImagingGLEngine::RenderBatch, but override the Hydra
            // render-pass depth state after _PrepareRender() and before task
            // execution. Storm/Hgi translates this to the active backend.
            _UpdateHydraCollection(&_renderCollection, paths, params);
            _taskControllerSceneIndex->SetCollection(_renderCollection);
            _PrepareRender(params);

            HdxRenderTaskParams renderParams = _MakeHydraUsdImagingGLRenderParams(params);
            renderParams.depthBiasUseDefault = false;
            renderParams.depthBiasEnable = true;
            renderParams.depthBiasConstantFactor = constantFactor;
            renderParams.depthBiasSlopeFactor = slopeFactor;
            renderParams.depthFunc = HdCmpFuncLEqual;
            renderParams.depthMaskEnable = true;
            renderParams.wireframeColor = wireframeColor;

            _taskControllerSceneIndex->SetRenderParams(renderParams);
            _SetBBoxParams(params.bboxes, params.bboxLineColor, params.bboxLineDashSize);
            _taskControllerSceneIndex->SetEnableSelection(params.highlight);

            // Match UsdImagingGLEngine::RenderBatch: Hdx render tasks expect
            // selectionState to exist in the task context even when selection
            // highlighting itself is disabled.
            const VtValue selectionValue(_selTracker);

            if (HdEngine* hdEngine = _GetHdEngine())
                hdEngine->SetTaskContextData(HdxTokens->selectionState, selectionValue);

            _Execute(params, _taskControllerSceneIndex->GetRenderingTaskPaths());
        }

    private:
        using SceneIndicesPtr = std::shared_ptr<SceneIndices>;

        ImagingGLEngine(const UsdImagingGLEngine::Parameters& params, const SceneIndicesPtr& sceneIndices)
            : UsdImagingGLEngine(prepareParameters(params, sceneIndices))
            , m_sceneIndices(sceneIndices)
        {
            constructingSceneIndices() = nullptr;
        }

        static SceneIndices*& constructingSceneIndices()
        {
            static thread_local SceneIndices* sceneIndices = nullptr;
            return sceneIndices;
        }

        static void registerSceneIndexFilters()
        {
            static bool registered = false;
            if (registered)
                return;

            HdSceneIndexPluginRegistry::SceneIndexAppendCallback callback =
                [](const std::string& renderInstanceId, const HdSceneIndexBaseRefPtr& inputScene,
                   const HdContainerDataSourceHandle& inputArgs) -> HdSceneIndexBaseRefPtr {
                Q_UNUSED(renderInstanceId);
                Q_UNUSED(inputArgs);

                // Apply document material overrides before merging the
                // auxiliary scene. This keeps viewport support content such as
                // the grid outside document-specific material overrides.
                TfRefPtr<RenderSceneIndex> renderSceneIndex = RenderSceneIndex::New(inputScene);
                HdMergingSceneIndexRefPtr mergingSceneIndex = HdMergingSceneIndex::New();

                mergingSceneIndex->AddInputScene(renderSceneIndex, SdfPath::AbsoluteRootPath());

                if (SceneIndices* sceneIndices = constructingSceneIndices()) {
                    sceneIndices->merging = mergingSceneIndex;
                    sceneIndices->renderSceneIndex = renderSceneIndex;
                }

                return mergingSceneIndex;
            };

            HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
                std::string(), callback, nullptr, 0, HdSceneIndexPluginRegistry::InsertionOrderAtStart);

            registered = true;
        }

        static UsdImagingGLEngine::Parameters prepareParameters(UsdImagingGLEngine::Parameters params,
                                                                const SceneIndicesPtr& sceneIndices)
        {
            registerSceneIndexFilters();
            constructingSceneIndices() = sceneIndices.get();
            return params;
        }

        SceneIndicesPtr m_sceneIndices;
    };

    UsdImagingGLCullStyle cullStyle(RenderEngine::DoubleSidedMode mode)
    {
        switch (mode) {
        case RenderEngine::DoubleSidedMode::Primitive:
            return UsdImagingGLCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED;

        case RenderEngine::DoubleSidedMode::SingleSided: return UsdImagingGLCullStyle::CULL_STYLE_BACK;

        case RenderEngine::DoubleSidedMode::DoubleSided:
        default: return UsdImagingGLCullStyle::CULL_STYLE_NOTHING;
        }
    }

}  // namespace

class RenderEngine::Private {
public:
    explicit Private(ContextMode mode)
        : contextMode(mode)
    {}

    bool initialize();
    bool ensureCurrentContext();
    void resetEngine();
    void reset();
    void ensureAuxiliarySceneIndex();
    void refreshAuxiliarySceneIndex();
    void updateRenderSceneIndex();
    void updateRenderParams();
    void updateLighting();
    bool render();

    ContextMode contextMode = ContextMode::Current;
    Settings settings;
    UsdStageRefPtr stage;
    UsdStageRefPtr auxiliary;
    GfCamera camera;
    GfVec2i size = GfVec2i(512, 512);
    GfVec4d viewport = GfVec4d(0.0);
    QList<SdfPath> mask;
    QList<SdfPath> selected;
    std::vector<GfBBox3d> selectionBBoxes;
    QColor selectionColor = QColor(255, 210, 0);
    UsdImagingGLRenderParams params;
    std::unique_ptr<ImagingGLEngine> engine;
    std::unique_ptr<ImagingGLEngine> selectionEngine;
    std::unique_ptr<QOpenGLContext> offscreenContext;
    std::unique_ptr<QOffscreenSurface> offscreenSurface;
};

bool
RenderEngine::Private::ensureCurrentContext()
{
    if (contextMode == ContextMode::Current)
        return QOpenGLContext::currentContext() != nullptr;

    if (!offscreenContext) {
        QSurfaceFormat format;
        format.setSamples(4);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setAlphaBufferSize(8);
        format.setColorSpace(QColorSpace::SRgb);

        offscreenSurface = std::make_unique<QOffscreenSurface>();
        offscreenSurface->setFormat(format);
        offscreenSurface->create();

        if (!offscreenSurface->isValid())
            return false;

        offscreenContext = std::make_unique<QOpenGLContext>();
        offscreenContext->setFormat(format);

        if (QOpenGLContext::globalShareContext())
            offscreenContext->setShareContext(QOpenGLContext::globalShareContext());

        if (!offscreenContext->create())
            return false;
    }

    return offscreenContext->makeCurrent(offscreenSurface.get());
}

bool
RenderEngine::Private::initialize()
{
    if (engine && selectionEngine)
        return true;

    if (!ensureCurrentContext())
        return false;

    UsdImagingGLEngine::Parameters documentParams {};
    documentParams.displayUnloadedPrimsWithBounds = false;
    documentParams.allowAsynchronousSceneProcessing = contextMode == ContextMode::Current;

    engine = std::make_unique<ImagingGLEngine>(documentParams);
    if (!engine->GetHgi()) {
        engine.reset();
        return false;
    }

    // Keep selection in a completely separate Hydra render index. Its scene
    // presentation is stable while a frame is rendered; only user selection
    // changes dirty this index between frames.
    UsdImagingGLEngine::Parameters selectionParams = documentParams;
    selectionParams.allowAsynchronousSceneProcessing = false;

    selectionEngine = std::make_unique<ImagingGLEngine>(selectionParams);
    if (!selectionEngine->GetHgi()) {
        selectionEngine.reset();
        engine.reset();
        return false;
    }

    updateRenderSceneIndex();
    ensureAuxiliarySceneIndex();

    engine->SetSelected(SdfPathVector());
    selectionEngine->SetSelected(SdfPathVector());
    return true;
}

void
RenderEngine::Private::resetEngine()
{
    if (!engine && !selectionEngine)
        return;

    if (contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;

        if (!ensureCurrentContext()) {
            qWarning() << "could not make offscreen context current while resetting render engine";
            return;
        }

        selectionEngine.reset();
        engine.reset();
        return;
    }

    selectionEngine.reset();
    engine.reset();
}

void
RenderEngine::Private::reset()
{
    if (contextMode == ContextMode::Offscreen) {
        {
            OpenGLContextRestore contextRestore;

            if ((engine || selectionEngine)
                && (!offscreenContext || !offscreenSurface || !offscreenContext->makeCurrent(offscreenSurface.get()))) {
                qWarning() << "could not make offscreen context current while releasing render engine";
                return;
            }

            selectionEngine.reset();
            engine.reset();
        }

        offscreenContext.reset();
        offscreenSurface.reset();
        return;
    }

    selectionEngine.reset();
    engine.reset();
}

void
RenderEngine::Private::ensureAuxiliarySceneIndex()
{
    if (!auxiliary)
        return;

    for (ImagingGLEngine* target : { engine.get(), selectionEngine.get() }) {
        if (!target)
            continue;

        SceneIndices& sceneIndices = target->sceneIndices();

        if (sceneIndices.auxiliaryInserted || !sceneIndices.merging)
            continue;

        UsdImagingCreateSceneIndicesInfo createInfo;
        createInfo.stage = auxiliary;
        createInfo.displayUnloadedPrimsWithBounds = false;
        createInfo.addDrawModeSceneIndex = true;

        sceneIndices.auxiliary = UsdImagingCreateSceneIndices(createInfo);

        if (!sceneIndices.auxiliary.stageSceneIndex || !sceneIndices.auxiliary.finalSceneIndex) {
            sceneIndices.clearAuxiliary();
            continue;
        }

        sceneIndices.merging->AddInputScene(sceneIndices.auxiliary.finalSceneIndex, SdfPath::AbsoluteRootPath());
        sceneIndices.auxiliaryInserted = true;
    }
}

void
RenderEngine::Private::refreshAuxiliarySceneIndex()
{
    for (ImagingGLEngine* target : { engine.get(), selectionEngine.get() }) {
        if (!target)
            continue;

        SceneIndices& sceneIndices = target->sceneIndices();
        if (sceneIndices.auxiliary.stageSceneIndex)
            sceneIndices.auxiliary.stageSceneIndex->ApplyPendingUpdates();
    }
}

void
RenderEngine::Private::updateRenderSceneIndex()
{
    if (engine) {
        SceneIndices& sceneIndices = engine->sceneIndices();

        if (sceneIndices.renderSceneIndex) {
            TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;

            renderSceneIndex->setSceneMaterialsEnabled(settings.sceneMaterialsEnabled);
            renderSceneIndex->setMaterialPath(settings.overrideMaterial);
            renderSceneIndex->setSelectionPaths({});
            renderSceneIndex->setSelectionPresentationEnabled(false);

            RenderSceneIndex::Mode mode = RenderSceneIndex::None;
            switch (settings.materialMode) {
            case MaterialMode::Clay: mode = RenderSceneIndex::Clay; break;
            case MaterialMode::Override: mode = RenderSceneIndex::Custom; break;
            case MaterialMode::Scene:
            default: mode = RenderSceneIndex::None; break;
            }

            renderSceneIndex->setMode(mode);

            const bool overrideDoubleSided = settings.doubleSidedMode == DoubleSidedMode::DoubleSided;
            renderSceneIndex->setDoubleSidedOverride(false);
            renderSceneIndex->setDoubleSidedOverrideEnabled(overrideDoubleSided);
        }
    }

    if (selectionEngine) {
        SceneIndices& sceneIndices = selectionEngine->sceneIndices();

        if (sceneIndices.renderSceneIndex) {
            TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;

            SdfPathVector selectionPaths;
            selectionPaths.reserve(selected.size());
            for (const SdfPath& path : selected)
                selectionPaths.push_back(path);

            // The selection render index never changes presentation mode during
            // rendering. It permanently uses the selection material/repr and is
            // only rendered for the selected root collection.
            renderSceneIndex->setSceneMaterialsEnabled(true);
            renderSceneIndex->setMaterialPath({});
            renderSceneIndex->setMode(RenderSceneIndex::None);
            renderSceneIndex->setSelectionPaths(selectionPaths);
            renderSceneIndex->setSelectionPresentationEnabled(true);

            const bool overrideDoubleSided = settings.doubleSidedMode == DoubleSidedMode::DoubleSided;
            renderSceneIndex->setDoubleSidedOverride(false);
            renderSceneIndex->setDoubleSidedOverrideEnabled(overrideDoubleSided);
        }
    }
}

void
RenderEngine::Private::updateRenderParams()
{
    params.clearColor = qt::QColorToGfVec4f(settings.clearColor);
    params.drawMode = settings.drawMode;
    params.complexity = settings.complexity;
    params.cullStyle = cullStyle(settings.doubleSidedMode);
    params.enableLighting = true;
    params.gammaCorrectColors = settings.gammaCorrectColors;
    params.enableSampleAlphaToCoverage = settings.sampleAlphaToCoverageEnabled;
    params.enableSceneLights = settings.sceneLightsEnabled;
    params.enableSceneMaterials = true;
    params.flipFrontFacing = settings.flipFrontFacing;
    params.showGuides = settings.showGuides;
    params.showProxy = settings.showProxy;
    params.showRender = settings.showRender;
    params.highlight = false;
    params.bboxLineColor = qt::QColorToGfVec4f(selectionColor);
    params.bboxLineDashSize = 3.0f;
}

void
RenderEngine::Private::updateLighting()
{
    if (!engine && !selectionEngine)
        return;

    std::vector<GlfSimpleLight> lights;

    if (settings.defaultCameraLightEnabled) {
        const GfMatrix4d cameraTransform = camera.GetTransform();
        const GfVec3d cameraPosition = cameraTransform.ExtractTranslation();

        GlfSimpleLight light;
        light.SetAmbient(GfVec4f(0, 0, 0, 0));
        light.SetPosition(GfVec4f(cameraPosition[0], cameraPosition[1], cameraPosition[2], 1.0f));
        light.SetTransform(cameraTransform);

        lights.push_back(light);
    }

    if (settings.defaultDomeLightEnabled) {
        GlfSimpleLight light;
        light.SetAmbient(GfVec4f(0, 0, 0, 0));
        light.SetPosition(GfVec4f(0, 0, 1, 0));
        light.SetIsDomeLight(true);

        if (stage && UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z) {
            GfMatrix4d domeTransform(1.0);
            domeTransform.SetRotate(GfRotation(GfVec3d(1.0, 0.0, 0.0), 90.0));
            light.SetTransform(domeTransform);
        }

        if (!settings.domeLightTexture.isEmpty()) {
            const std::string filename = settings.domeLightTexture.toStdString();
            light.SetDomeLightTextureFile(SdfAssetPath(filename, filename));
        }

        lights.push_back(light);
    }

    const GfVec4f ambient(settings.defaultAmbient, settings.defaultAmbient, settings.defaultAmbient, 1.0f);

    GlfSimpleMaterial material;
    material.SetAmbient(ambient);
    material.SetSpecular(GfVec4f(settings.defaultSpecular, settings.defaultSpecular, settings.defaultSpecular, 1.0f));
    material.SetShininess(settings.defaultShininess);

    if (engine)
        engine->SetLightingState(lights, material, ambient);
    if (selectionEngine)
        selectionEngine->SetLightingState(lights, material, ambient);
}

bool
RenderEngine::Private::render()
{
    if (!stage || size[0] <= 0 || size[1] <= 0)
        return false;

    if ((!engine || !selectionEngine) && !initialize())
        return false;

    ensureAuxiliarySceneIndex();
    refreshAuxiliarySceneIndex();
    updateRenderSceneIndex();
    updateRenderParams();
    updateLighting();

    GfVec4d renderViewport = viewport;
    if (renderViewport[2] <= 0.0 || renderViewport[3] <= 0.0)
        renderViewport = GfVec4d(0, 0, size[0], size[1]);

    const GfFrustum frustum = camera.GetFrustum();
    const GfMatrix4d viewMatrix = frustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = frustum.ComputeProjectionMatrix();

    auto configureEngine = [&](ImagingGLEngine* target) {
        target->SetRendererSetting(TfToken("domeLightCameraVisibility"), VtValue(settings.domeLightCameraVisibility));
        target->SetRendererAov(settings.aov);
        target->SetRenderBufferSize(size);
        target->SetFraming(CameraUtilFraming(GfRange2f(GfVec2i(), size), GfRect2i(GfVec2i(), size)));
        target->SetWindowPolicy(CameraUtilMatchVertically);
        target->SetRenderViewport(renderViewport);
        target->SetCameraState(viewMatrix, projectionMatrix);
    };

    configureEngine(engine.get());
    configureEngine(selectionEngine.get());

    const UsdPrim root = stage->GetPseudoRoot();
    UsdImagingGLRenderParams documentParams = params;

    Hgi* documentHgi = engine->GetHgi();
    if (!documentHgi)
        return false;

    documentHgi->StartFrame();
    engine->PrepareBatch(root, documentParams);

    if (mask.isEmpty()) {
        engine->Render(root, documentParams);
    }
    else {
        SdfPathVector renderPaths;
        renderPaths.reserve(mask.size());

        for (const SdfPath& path : mask)
            renderPaths.push_back(path);

        engine->RenderBatch(renderPaths, documentParams);
    }

    documentHgi->EndFrame();

    SdfPathVector selectionPaths;
    selectionPaths.reserve(selected.size());

    for (const SdfPath& path : selected) {
        if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
            continue;
        selectionPaths.push_back(path);
    }

    if (!selectionPaths.empty()) {
        Hgi* selectionHgi = selectionEngine->GetHgi();
        if (!selectionHgi)
            return false;

        UsdImagingGLRenderParams selectionParams = documentParams;
        selectionParams.highlight = false;
        selectionParams.enableSceneMaterials = true;

        constexpr float selectionDepthBiasConstant = -2.0f;
        constexpr float selectionDepthBiasSlope = 0.0f;

        const GfVec4f fillColor = qt::QColorToGfVec4f(selectionColor);
        const GfVec4f wireframeColor(fillColor[0] * 0.8f, fillColor[1] * 0.8f, fillColor[2] * 0.8f, fillColor[3]);

        selectionHgi->StartFrame();
        selectionEngine->PrepareBatch(root, selectionParams);
        selectionEngine->renderBatchWithDepthBias(selectionPaths, selectionParams, selectionDepthBiasConstant,
                                                  selectionDepthBiasSlope, wireframeColor);
        selectionHgi->EndFrame();
    }

    return true;
}

RenderEngine::RenderEngine(ContextMode mode)
    : p(std::make_unique<Private>(mode))
{}

RenderEngine::~RenderEngine()
{
    if (p)
        p->reset();
}

bool
RenderEngine::initialize()
{
    if (p->contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;
        return p->initialize();
    }

    return p->initialize();
}

void
RenderEngine::reset()
{
    p->reset();
}

bool
RenderEngine::isInitialized() const
{
    return p->engine != nullptr && p->selectionEngine != nullptr;
}

void
RenderEngine::setStage(UsdStageRefPtr stage)
{
    if (p->stage == stage)
        return;

    p->stage = stage;
    p->resetEngine();
}

UsdStageRefPtr
RenderEngine::stage() const
{
    return p->stage;
}

void
RenderEngine::setAuxiliaryStage(UsdStageRefPtr stage)
{
    if (p->auxiliary == stage)
        return;

    p->auxiliary = stage;
    p->resetEngine();
}

UsdStageRefPtr
RenderEngine::auxiliaryStage() const
{
    return p->auxiliary;
}

void
RenderEngine::refreshAuxiliaryStage()
{
    p->refreshAuxiliarySceneIndex();
}

void
RenderEngine::setCamera(const GfCamera& camera)
{
    p->camera = camera;
}

const GfCamera&
RenderEngine::camera() const
{
    return p->camera;
}

void
RenderEngine::setSize(const GfVec2i& size)
{
    p->size = size;
}

const GfVec2i&
RenderEngine::size() const
{
    return p->size;
}

void
RenderEngine::setViewport(const GfVec4d& viewport)
{
    p->viewport = viewport;
}

const GfVec4d&
RenderEngine::viewport() const
{
    return p->viewport;
}

void
RenderEngine::setSettings(const Settings& settings)
{
    p->settings = settings;
    p->updateRenderSceneIndex();
}

const RenderEngine::Settings&
RenderEngine::settings() const
{
    return p->settings;
}

void
RenderEngine::setMask(const QList<SdfPath>& paths)
{
    p->mask = paths;
}

void
RenderEngine::setSelected(const QList<SdfPath>& paths)
{
    p->selected.clear();
    p->selected.reserve(paths.size());

    for (const SdfPath& path : paths) {
        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;

        if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath())
            continue;

        if (p->stage && !p->stage->GetPrimAtPath(primPath))
            continue;

        if (!p->selected.contains(primPath))
            p->selected.append(primPath);
    }

    p->updateRenderSceneIndex();
}

void
RenderEngine::setSelectionBBoxes(const std::vector<GfBBox3d>& bboxes)
{
    p->selectionBBoxes = bboxes;
}

void
RenderEngine::setSelectionColor(const QColor& color)
{
    p->selectionColor = color;
}

bool
RenderEngine::renderToCurrentFramebuffer()
{
    if (!QOpenGLContext::currentContext())
        return false;

    if (!p->engine && !p->initialize())
        return false;

    glClearColor(p->settings.clearColor.redF(), p->settings.clearColor.greenF(), p->settings.clearColor.blueF(),
                 p->settings.clearColor.alphaF());

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!p->engine->IsColorCorrectionCapable())
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);

#ifdef WIN32
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    GfVec4d viewport = p->viewport;
    if (viewport[2] <= 0.0 || viewport[3] <= 0.0)
        viewport = GfVec4d(0, 0, p->size[0], p->size[1]);

    glViewport(static_cast<GLint>(viewport[0]), static_cast<GLint>(viewport[1]), static_cast<GLsizei>(viewport[2]),
               static_cast<GLsizei>(viewport[3]));
#endif  // WIN32

    return p->render();
}

QImage
RenderEngine::renderImage()
{
    if (p->contextMode != ContextMode::Offscreen)
        return {};

    OpenGLContextRestore contextRestore;

    if (!p->ensureCurrentContext())
        return {};

    if (!p->engine && !p->initialize())
        return {};

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(4);

    QOpenGLFramebufferObject framebuffer(p->size[0], p->size[1], format);

    if (!framebuffer.isValid())
        return {};

    if (!framebuffer.bind())
        return {};

    const bool rendered = renderToCurrentFramebuffer();
    const QImage image = rendered ? framebuffer.toImage() : QImage();

    framebuffer.release();
    return image;
}

bool
RenderEngine::testIntersection(const GfMatrix4d& viewMatrix, const GfMatrix4d& projectionMatrix, const UsdPrim& root,
                               GfVec3d* hitPoint, GfVec3d* hitNormal, SdfPath* hitPrimPath, SdfPath* hitInstancerPath)
{
    if (!p->engine && !p->initialize())
        return false;

    p->updateRenderParams();

    return p->engine->TestIntersection(viewMatrix, projectionMatrix, root, p->params, hitPoint, hitNormal, hitPrimPath,
                                       hitInstancerPath);
}

bool
RenderEngine::testIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfMatrix4d& viewMatrix,
                               const GfMatrix4d& projectionMatrix, const UsdPrim& root,
                               UsdImagingGLEngine::IntersectionResultVector* results)
{
    if (!results)
        return false;

    if (!p->engine && !p->initialize())
        return false;

    p->updateRenderParams();

    UsdImagingGLEngine::IntersectionResultVector rawResults;

    if (!p->engine->TestIntersection(pickParams, viewMatrix, projectionMatrix, root, p->params, &rawResults)) {
        results->clear();
        return false;
    }

    results->clear();
    results->reserve(rawResults.size());

    for (const auto& result : rawResults) {
        if (result.hitPrimPath.IsEmpty())
            continue;

        if (p->stage && !p->stage->GetPrimAtPath(result.hitPrimPath))
            continue;

        results->push_back(result);
    }

    return !results->empty();
}

QList<QString>
RenderEngine::rendererAovs() const
{
    QList<QString> result;

    if (!p->engine)
        return result;

    for (const TfToken& token : p->engine->GetRendererAovs())
        result.append(QString::fromStdString(token.GetString()));

    return result;
}

VtDictionary
RenderEngine::renderStats() const
{
    return p->engine ? p->engine->GetRenderStats() : VtDictionary();
}

QString
RenderEngine::hgiApiName() const
{
    if (!p->engine)
        return {};

    Hgi* hgi = p->engine->GetHgi();
    return hgi ? QString::fromStdString(hgi->GetAPIName().GetString()) : QString();
}

bool
RenderEngine::isColorCorrectionCapable() const
{
    return p->engine && p->engine->IsColorCorrectionCapable();
}

}  // namespace stageviz
