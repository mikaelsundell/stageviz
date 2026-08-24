// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderengine.h"
#include "materialoverridesceneindex.h"
#include "qtutils.h"
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
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {
namespace {

    /**
     * @brief Restores the OpenGL context that was current on entry.
     *
     * Offscreen material previews temporarily make their own context current.
     * QOpenGLContext::doneCurrent() does not restore the context that was
     * active before that switch, which can leave a QOpenGLWidget without its
     * expected context during the next paint. This guard scopes that switch
     * and restores the previous context and surface on every return path.
     */
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
        TfRefPtr<MaterialOverrideSceneIndex> materialOverride;
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

                HdMergingSceneIndexRefPtr mergingSceneIndex = HdMergingSceneIndex::New();
                mergingSceneIndex->AddInputScene(inputScene, SdfPath::AbsoluteRootPath());

                TfRefPtr<MaterialOverrideSceneIndex> materialOverrideSceneIndex = MaterialOverrideSceneIndex::New(
                    mergingSceneIndex);

                if (SceneIndices* sceneIndices = constructingSceneIndices()) {
                    sceneIndices->merging = mergingSceneIndex;
                    sceneIndices->materialOverride = materialOverrideSceneIndex;
                }

                return materialOverrideSceneIndex;
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
    void updateMaterialOverrideSceneIndex();
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
    if (engine)
        return true;

    if (!ensureCurrentContext())
        return false;

    UsdImagingGLEngine::Parameters engineParams {};
    engineParams.displayUnloadedPrimsWithBounds = false;
    // Offscreen preview rendering shares GPU resources with the interactive
    // viewport. Keep its Hydra scene processing synchronous so background
    // work cannot overlap a context hand-off back to QOpenGLWidget.
    engineParams.allowAsynchronousSceneProcessing = contextMode == ContextMode::Current;

    engine = std::make_unique<ImagingGLEngine>(engineParams);
    if (!engine->GetHgi()) {
        engine.reset();
        return false;
    }

    updateMaterialOverrideSceneIndex();
    ensureAuxiliarySceneIndex();

    SdfPathVector selectedPaths;
    selectedPaths.reserve(selected.size());
    for (const SdfPath& path : selected)
        selectedPaths.push_back(path);
    engine->SetSelected(selectedPaths);
    engine->SetSelectionColor(qt::QColorToGfVec4f(selectionColor));
    return true;
}

void
RenderEngine::Private::resetEngine()
{
    if (!engine)
        return;

    if (contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;
        if (!ensureCurrentContext()) {
            qWarning() << "could not make offscreen context current while resetting render engine";
            return;
        }
        engine.reset();
        return;
    }

    engine.reset();
}

void
RenderEngine::Private::reset()
{
    if (contextMode == ContextMode::Offscreen) {
        {
            OpenGLContextRestore contextRestore;
            if (engine
                && (!offscreenContext || !offscreenSurface || !offscreenContext->makeCurrent(offscreenSurface.get()))) {
                qWarning() << "could not make offscreen context current while releasing render engine";
                return;
            }
            engine.reset();
        }

        offscreenContext.reset();
        offscreenSurface.reset();
        return;
    }

    engine.reset();
}

void
RenderEngine::Private::ensureAuxiliarySceneIndex()
{
    if (!engine || !auxiliary)
        return;

    SceneIndices& sceneIndices = engine->sceneIndices();
    if (sceneIndices.auxiliaryInserted)
        return;
    if (!sceneIndices.merging)
        return;

    UsdImagingCreateSceneIndicesInfo createInfo;
    createInfo.stage = auxiliary;
    createInfo.displayUnloadedPrimsWithBounds = false;
    createInfo.addDrawModeSceneIndex = true;
    sceneIndices.auxiliary = UsdImagingCreateSceneIndices(createInfo);

    if (!sceneIndices.auxiliary.stageSceneIndex || !sceneIndices.auxiliary.finalSceneIndex) {
        sceneIndices.clearAuxiliary();
        return;
    }

    sceneIndices.merging->AddInputScene(sceneIndices.auxiliary.finalSceneIndex, SdfPath::AbsoluteRootPath());
    sceneIndices.auxiliaryInserted = true;
}

void
RenderEngine::Private::refreshAuxiliarySceneIndex()
{
    if (!engine)
        return;

    SceneIndices& sceneIndices = engine->sceneIndices();
    if (sceneIndices.auxiliary.stageSceneIndex)
        sceneIndices.auxiliary.stageSceneIndex->ApplyPendingUpdates();
}

void
RenderEngine::Private::updateMaterialOverrideSceneIndex()
{
    if (!engine)
        return;

    SceneIndices& sceneIndices = engine->sceneIndices();
    if (!sceneIndices.materialOverride)
        return;

    sceneIndices.materialOverride->setSceneMaterialsEnabled(settings.sceneMaterialsEnabled);
    sceneIndices.materialOverride->setMaterialPath(settings.overrideMaterial);

    MaterialOverrideSceneIndex::Mode mode = MaterialOverrideSceneIndex::None;
    switch (settings.materialMode) {
    case MaterialMode::Clay: mode = MaterialOverrideSceneIndex::Clay; break;
    case MaterialMode::Override: mode = MaterialOverrideSceneIndex::Custom; break;
    case MaterialMode::Scene:
    default: mode = MaterialOverrideSceneIndex::None; break;
    }
    sceneIndices.materialOverride->setMode(mode);

    const bool overrideDoubleSided = settings.doubleSidedMode == DoubleSidedMode::DoubleSided;
    sceneIndices.materialOverride->setDoubleSidedOverride(false);
    sceneIndices.materialOverride->setDoubleSidedOverrideEnabled(overrideDoubleSided);
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
    params.highlight = true;
    params.bboxes = selectionBBoxes;
    params.bboxLineColor = qt::QColorToGfVec4f(selectionColor);
    params.bboxLineDashSize = 3.0f;
}

void
RenderEngine::Private::updateLighting()
{
    if (!engine)
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

        // Glf/Hdx dome environments use Y as their vertical axis. Rotate the
        // dome into the stage coordinate system when rendering a Z-up stage.
        // Keep this correction separate from any future user-controlled HDRI
        // rotation, which should rotate around the corrected stage-up axis.
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
    engine->SetLightingState(lights, material, ambient);
}

bool
RenderEngine::Private::render()
{
    if (!stage || size[0] <= 0 || size[1] <= 0)
        return false;
    if (!engine && !initialize())
        return false;

    ensureAuxiliarySceneIndex();
    refreshAuxiliarySceneIndex();
    updateMaterialOverrideSceneIndex();
    updateRenderParams();
    updateLighting();

    engine->SetRendererSetting(TfToken("domeLightCameraVisibility"), VtValue(settings.domeLightCameraVisibility));

    engine->SetRendererAov(settings.aov);
    engine->SetRenderBufferSize(size);
    engine->SetFraming(CameraUtilFraming(GfRange2f(GfVec2i(), size), GfRect2i(GfVec2i(), size)));
    engine->SetWindowPolicy(CameraUtilMatchVertically);

    GfVec4d renderViewport = viewport;
    if (renderViewport[2] <= 0.0 || renderViewport[3] <= 0.0)
        renderViewport = GfVec4d(0, 0, size[0], size[1]);
    engine->SetRenderViewport(renderViewport);

    const GfFrustum frustum = camera.GetFrustum();
    engine->SetCameraState(frustum.ComputeViewMatrix(), frustum.ComputeProjectionMatrix());
    engine->SetSelectionColor(qt::QColorToGfVec4f(selectionColor));

    Hgi* hgi = engine->GetHgi();
    if (!hgi)
        return false;

    hgi->StartFrame();
    const UsdPrim root = stage->GetPseudoRoot();
    if (mask.isEmpty()) {
        engine->Render(root, params);
    }
    else {
        SdfPathVector paths;
        paths.reserve(mask.size());
        for (const SdfPath& path : mask)
            paths.push_back(path);
        engine->PrepareBatch(root, params);
        engine->RenderBatch(paths, params);
    }
    hgi->EndFrame();
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
    return p->engine != nullptr;
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
    p->updateMaterialOverrideSceneIndex();
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
    p->selected = paths;
    if (!p->engine)
        return;
    SdfPathVector selected;
    selected.reserve(paths.size());
    for (const SdfPath& path : paths)
        selected.push_back(path);
    p->engine->SetSelected(selected);
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
    if (p->engine)
        p->engine->SetSelectionColor(qt::QColorToGfVec4f(color));
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
#endif

    return p->render();
}

QImage
RenderEngine::renderImage()
{
    if (p->contextMode != ContextMode::Offscreen)
        return {};

    // Rendering a swatch temporarily switches away from the context owned by
    // ImagingGLWidget. Always restore that previous context before returning;
    // simply calling doneCurrent() would otherwise leave the viewport with no
    // current context and can produce black or corrupted frames.
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
    if (!p->engine && !p->initialize())
        return false;
    p->updateRenderParams();
    return p->engine->TestIntersection(pickParams, viewMatrix, projectionMatrix, root, p->params, results);
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
