// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderengine.h"
#include "paths.h"
#include "qtutils.h"
#include "rendersceneindex.h"
#include "rendertask.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QSurfaceFormat>
#include <QtGui/qopengl.h>
#include <algorithm>
#include <memory>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hdx/colorizeSelectionTask.h>
#include <pxr/imaging/hdx/presentTask.h>
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

    QString renderResourcePath(const QString& filename)
    {
        const QString applicationDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_MAC
        QDir contentsDir(applicationDir);
        if (contentsDir.cdUp()) {
            const QString candidate = contentsDir.filePath(QStringLiteral("Resources/") + filename);
            if (QFileInfo::exists(candidate))
                return QFileInfo(candidate).absoluteFilePath();
        }
#endif

        const QString candidate = QDir(applicationDir).filePath(QStringLiteral("resources/") + filename);
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();

        return {};
    }

    QString renderShaderPath() { return renderResourcePath(QStringLiteral("Render.glslfx")); }

    QString sceneIdShaderPath() { return renderResourcePath(QStringLiteral("SceneId.glslfx")); }

    class RenderTaskDelegate final : public HdSceneDelegate {
    public:
        RenderTaskDelegate(HdRenderIndex* renderIndex, const SdfPath& delegateId)
            : HdSceneDelegate(renderIndex, delegateId)
        {}

        VtValue Get(const SdfPath& id, const TfToken& key) override
        {
            if (id == m_taskPath && key == HdTokens->params)
                return VtValue(m_params);

            return {};
        }

        void setParams(const SdfPath& taskPath, const RenderTaskParams& params)
        {
            const bool pathChanged = taskPath != m_taskPath;
            const bool paramsChanged = params != m_params;
            if (!pathChanged && !paramsChanged)
                return;

            m_taskPath = taskPath;
            m_params = params;

            if (HdRenderIndex& renderIndex = GetRenderIndex(); renderIndex.HasTask(taskPath))
                renderIndex.GetChangeTracker().MarkTaskDirty(taskPath, HdChangeTracker::DirtyParams);
        }

    private:
        SdfPath m_taskPath;
        RenderTaskParams m_params;
    };

    class AuxiliaryMaterialSceneIndex final : public HdSingleInputFilteringSceneIndexBase {
    public:
        static TfRefPtr<AuxiliaryMaterialSceneIndex> New(const HdSceneIndexBaseRefPtr& input)
        {
            return TfCreateRefPtr(new AuxiliaryMaterialSceneIndex(input));
        }

        HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override
        {
            // Keep the root visible for scene-index traversal, but expose only
            // the canonical auxiliary material branch below it.
            if (primPath == SdfPath::AbsoluteRootPath() || isMaterialPath(primPath))
                return _GetInputSceneIndex()->GetPrim(primPath);
            return {};
        }

        SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override
        {
            if (primPath == SdfPath::AbsoluteRootPath()) {
                const SdfPathVector children = _GetInputSceneIndex()->GetChildPrimPaths(primPath);
                return std::find(children.begin(), children.end(), paths::auxiliary::materials) != children.end()
                           ? SdfPathVector { paths::auxiliary::materials }
                           : SdfPathVector {};
            }

            if (!isMaterialPath(primPath))
                return {};

            SdfPathVector result;
            for (const SdfPath& child : _GetInputSceneIndex()->GetChildPrimPaths(primPath)) {
                if (isMaterialPath(child))
                    result.push_back(child);
            }
            return result;
        }

    protected:
        void _PrimsAdded(const HdSceneIndexBase&, const HdSceneIndexObserver::AddedPrimEntries& entries) override
        {
            HdSceneIndexObserver::AddedPrimEntries filtered;
            for (const auto& entry : entries) {
                if (isMaterialPath(entry.primPath))
                    filtered.push_back(entry);
            }
            if (!filtered.empty())
                _SendPrimsAdded(filtered);
        }

        void _PrimsRemoved(const HdSceneIndexBase&, const HdSceneIndexObserver::RemovedPrimEntries& entries) override
        {
            HdSceneIndexObserver::RemovedPrimEntries filtered;
            for (const auto& entry : entries) {
                if (isMaterialPath(entry.primPath))
                    filtered.push_back(entry);
            }
            if (!filtered.empty())
                _SendPrimsRemoved(filtered);
        }

        void _PrimsDirtied(const HdSceneIndexBase&, const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
        {
            HdSceneIndexObserver::DirtiedPrimEntries filtered;
            for (const auto& entry : entries) {
                if (isMaterialPath(entry.primPath))
                    filtered.push_back(entry);
            }
            if (!filtered.empty())
                _SendPrimsDirtied(filtered);
        }

        void _PrimsRenamed(const HdSceneIndexBase&, const HdSceneIndexObserver::RenamedPrimEntries& entries) override
        {
            // Stageviz owns the auxiliary namespace and does not rename its
            // canonical roots. Forward child renames so material updates remain
            // compatible across Hydra versions with different rename entry APIs.
            _SendPrimsRenamed(entries);
        }

    private:
        explicit AuxiliaryMaterialSceneIndex(const HdSceneIndexBaseRefPtr& input)
            : HdSingleInputFilteringSceneIndexBase(input)
        {}

        static bool isMaterialPath(const SdfPath& path)
        {
            return path == paths::auxiliary::materials || path.HasPrefix(paths::auxiliary::materials);
        }
    };

    struct SceneIndices {
        HdMergingSceneIndexRefPtr merging;
        TfRefPtr<RenderSceneIndex> renderSceneIndex;
        UsdImagingSceneIndices auxiliary;
        HdSceneIndexBaseRefPtr auxiliaryMaterials;
        bool auxiliaryInserted = false;

        void clearAuxiliary()
        {
            auxiliary = {};
            auxiliaryMaterials = {};
            auxiliaryInserted = false;
        }
    };

    class ImagingGLEngine final : public UsdImagingGLEngine {
    public:
        explicit ImagingGLEngine(const UsdImagingGLEngine::Parameters& params)
            : ImagingGLEngine(params, std::make_shared<SceneIndices>())
        {}

        ~ImagingGLEngine() { removeRenderTask(); }

        SceneIndices& sceneIndices() { return *m_sceneIndices; }
        const SceneIndices& sceneIndices() const { return *m_sceneIndices; }

        void renderBatch(const SdfPathVector& paths, const UsdImagingGLRenderParams& params)
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            _UpdateHydraCollection(&_renderCollection, paths, params);
            _taskControllerSceneIndex->SetCollection(_renderCollection);
            _PrepareRender(params);
            _SetBBoxParams(params.bboxes, params.bboxLineColor, params.bboxLineDashSize);
            _taskControllerSceneIndex->SetEnableSelection(false);
            _taskControllerSceneIndex->SetEnablePresentation(true);

            const VtValue selectionValue(_selTracker);
            if (HdEngine* hdEngine = _GetHdEngine())
                hdEngine->SetTaskContextData(HdxTokens->selectionState, selectionValue);

            _Execute(params, _taskControllerSceneIndex->GetRenderingTaskPaths());
        }

        void renderBatchWithRenderTask(const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                                       const RenderTaskParams& renderTaskParams)
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            _UpdateHydraCollection(&_renderCollection, paths, params);
            _taskControllerSceneIndex->SetCollection(_renderCollection);
            _PrepareRender(params);

            _SetBBoxParams(params.bboxes, params.bboxLineColor, params.bboxLineDashSize);
            _taskControllerSceneIndex->SetEnableSelection(false);
            _taskControllerSceneIndex->SetEnablePresentation(true);

            const VtValue selectionValue(_selTracker);
            if (HdEngine* hdEngine = _GetHdEngine())
                hdEngine->SetTaskContextData(HdxTokens->selectionState, selectionValue);

            SdfPathVector taskPaths = _taskControllerSceneIndex->GetRenderingTaskPaths();

            if (renderTaskParams.enabled() && ensureRenderTask()) {
                // The custom scene-ID pass must use the exact same Hydra repr
                // selector as the beauty pass. UsdImagingGLEngine folds draw
                // mode and complexity/refinement into _renderCollection in
                // _UpdateHydraCollection(). Reusing that selector keeps the
                // primId silhouette identical to the viewport, including
                // subdiv/refined geometry at Medium/High/VeryHigh complexity.
                RenderTaskParams taskParams = renderTaskParams;
                taskParams.sceneIds.reprSelector = _renderCollection.GetReprSelector();
                m_renderTaskDelegate->setParams(m_renderTaskPath, taskParams);

                // The custom task must run after Storm has populated color/depth,
                // but before display transforms or presentation. Prefer identifying
                // the controller tasks by type and fall back to their conventional
                // path names for Hydra scene-index emulation builds.
                HdRenderIndex* renderIndex = _GetRenderIndex();
                auto insertionPoint = taskPaths.end();

                for (auto it = taskPaths.begin(); it != taskPaths.end(); ++it) {
                    bool insertBefore = false;

                    if (renderIndex && renderIndex->HasTask(*it)) {
                        const HdTaskSharedPtr& task = renderIndex->GetTask(*it);
                        insertBefore = dynamic_cast<HdxColorizeSelectionTask*>(task.get()) != nullptr
                                       || dynamic_cast<HdxColorCorrectionTask*>(task.get()) != nullptr
                                       || dynamic_cast<HdxPresentTask*>(task.get()) != nullptr;
                    }

                    if (!insertBefore) {
                        const std::string name = it->GetName();
                        insertBefore = name.find("colorizeSelection") != std::string::npos
                                       || name.find("selectionColorize") != std::string::npos
                                       || name.find("colorCorrection") != std::string::npos
                                       || name.find("present") != std::string::npos;
                    }

                    if (insertBefore) {
                        insertionPoint = it;
                        break;
                    }
                }

                taskPaths.insert(insertionPoint, m_renderTaskPath);
            }

            _Execute(params, taskPaths);
        }



        SdfPathVector takeCapturedVisiblePaths()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (!renderIndex || !renderIndex->HasTask(m_renderTaskPath))
                return {};

            const HdTaskSharedPtr& task = renderIndex->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeCapturedVisiblePaths() : SdfPathVector {};
        }

        void renderBatchWithDepthBias(const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                                      float constantFactor, float slopeFactor, const GfVec4f& wireframeColor)
        {
            if (!_taskControllerSceneIndex)
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
        bool ensureRenderTask()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (!renderIndex)
                return false;

            if (!m_renderTaskDelegate)
                m_renderTaskDelegate = std::make_unique<RenderTaskDelegate>(renderIndex, SdfPath("/__Stageviz"));

            if (!renderIndex->HasTask(m_renderTaskPath))
                renderIndex->InsertTask<RenderTask>(m_renderTaskDelegate.get(), m_renderTaskPath);

            return renderIndex->HasTask(m_renderTaskPath);
        }

        void removeRenderTask()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (renderIndex && renderIndex->HasTask(m_renderTaskPath))
                renderIndex->RemoveTask(m_renderTaskPath);

            m_renderTaskDelegate.reset();
        }

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

                // Apply document presentation overrides before merging the
                // auxiliary scene. Viewport support content such as the grid
                // remains outside document-specific material overrides.
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
        std::unique_ptr<RenderTaskDelegate> m_renderTaskDelegate;
        const SdfPath m_renderTaskPath = SdfPath("/__Stageviz/Tasks/Render");
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
    void updateDocumentRenderSceneIndex();
    void updateAuxiliaryRenderSceneIndex();
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
    QColor selectionColor = QColor(255, 210, 0);
    bool captureVisibleRequested = false;
    int captureVisibleGridSize = 4;
    SdfPathVector capturedVisiblePaths;
    UsdImagingGLRenderParams params;
    std::unique_ptr<ImagingGLEngine> engine;
    std::unique_ptr<ImagingGLEngine> auxiliaryEngine;
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
    if (engine && (contextMode == ContextMode::Offscreen || auxiliaryEngine))
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

    // Auxiliary display content remains a separate presentation pass.
    // Selection is handled by the Stageviz RenderTask as a screen-space outline.
    // Offscreen rendering intentionally omits viewport-only auxiliary content.
    if (contextMode == ContextMode::Current) {
        UsdImagingGLEngine::Parameters overlayParams = documentParams;
        overlayParams.allowAsynchronousSceneProcessing = false;

        auxiliaryEngine = std::make_unique<ImagingGLEngine>(overlayParams);
        if (!auxiliaryEngine->GetHgi()) {
            auxiliaryEngine.reset();
            engine.reset();
            return false;
        }
    }

    updateDocumentRenderSceneIndex();
    updateAuxiliaryRenderSceneIndex();
    ensureAuxiliarySceneIndex();

    return true;
}

void
RenderEngine::Private::resetEngine()
{
    if (!engine && !auxiliaryEngine)
        return;

    if (contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;

        if (!ensureCurrentContext()) {
            qWarning() << "could not make offscreen context current while resetting render engine";
            return;
        }

        auxiliaryEngine.reset();
        engine.reset();
        return;
    }

    auxiliaryEngine.reset();
    engine.reset();
}

void
RenderEngine::Private::reset()
{
    if (contextMode == ContextMode::Offscreen) {
        {
            OpenGLContextRestore contextRestore;

            if ((engine || auxiliaryEngine)
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

    auxiliaryEngine.reset();
    engine.reset();
}

void
RenderEngine::Private::ensureAuxiliarySceneIndex()
{
    if (!auxiliary)
        return;

    for (ImagingGLEngine* target : { engine.get() }) {
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

        // Keep auxiliary materials available for document/selection overrides,
        // but prune /Display so grid and helpers never enter their color/depth AOVs.
        sceneIndices.auxiliaryMaterials = AuxiliaryMaterialSceneIndex::New(sceneIndices.auxiliary.finalSceneIndex);
        sceneIndices.merging->AddInputScene(sceneIndices.auxiliaryMaterials, SdfPath::AbsoluteRootPath());
        sceneIndices.auxiliaryInserted = true;
    }
}

void
RenderEngine::Private::refreshAuxiliarySceneIndex()
{
    for (ImagingGLEngine* target : { engine.get() }) {
        if (!target)
            continue;

        SceneIndices& sceneIndices = target->sceneIndices();
        if (sceneIndices.auxiliary.stageSceneIndex)
            sceneIndices.auxiliary.stageSceneIndex->ApplyPendingUpdates();
    }
}

void
RenderEngine::Private::updateDocumentRenderSceneIndex()
{
    if (!engine)
        return;

    SceneIndices& sceneIndices = engine->sceneIndices();
    if (!sceneIndices.renderSceneIndex)
        return;

    TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;

    renderSceneIndex->setSceneMaterialsEnabled(settings.sceneMaterialsEnabled);
    renderSceneIndex->setMaterialPath(settings.overrideMaterial);

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

void
RenderEngine::Private::updateAuxiliaryRenderSceneIndex()
{
    if (!auxiliaryEngine)
        return;

    SceneIndices& sceneIndices = auxiliaryEngine->sceneIndices();
    if (!sceneIndices.renderSceneIndex)
        return;

    TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;
    renderSceneIndex->setSceneMaterialsEnabled(true);
    renderSceneIndex->setMaterialPath({});
    renderSceneIndex->setMode(RenderSceneIndex::None);
    renderSceneIndex->setDoubleSidedOverrideEnabled(false);
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
}

void
RenderEngine::Private::updateLighting()
{
    if (!engine && !auxiliaryEngine)
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
    if (auxiliaryEngine)
        auxiliaryEngine->SetLightingState(lights, material, ambient);
}

bool
RenderEngine::Private::render()
{
    if (!stage || size[0] <= 0 || size[1] <= 0)
        return false;

    if ((!engine || (contextMode == ContextMode::Current && !auxiliaryEngine)) && !initialize())
        return false;

    ensureAuxiliarySceneIndex();
    refreshAuxiliarySceneIndex();
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
    if (auxiliaryEngine)
        configureEngine(auxiliaryEngine.get());

    const UsdPrim root = stage->GetPseudoRoot();
    UsdImagingGLRenderParams documentParams = params;
    documentParams.highlight = false;

    SdfPathVector selectedPaths;
    selectedPaths.reserve(selected.size());
    for (const SdfPath& path : selected)
        selectedPaths.push_back(path);

    Hgi* documentHgi = engine->GetHgi();
    if (!documentHgi)
        return false;

    documentHgi->StartFrame();
    engine->PrepareBatch(root, documentParams);
    updateDocumentRenderSceneIndex();

    SdfPathVector renderPaths;
    if (mask.isEmpty()) {
        renderPaths.push_back(root.GetPath());
    }
    else {
        renderPaths.reserve(mask.size());
        for (const SdfPath& path : mask)
            renderPaths.push_back(path);
    }

    RenderTaskParams renderTaskParams;
    renderTaskParams.ambientOcclusion = settings.ambientOcclusion;
    renderTaskParams.ambientOcclusion.enabled = settings.ambientOcclusion.enabled && settings.aov == HdAovTokens->color
                                                && settings.drawMode != UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE;
    renderTaskParams.projectionMatrix = GfMatrix4f(projectionMatrix);

    renderTaskParams.selectionOutline.enabled = contextMode == ContextMode::Current && !selectedPaths.empty()
                                                && settings.aov == HdAovTokens->color;
    renderTaskParams.selectionOutline.paths = selectedPaths;
    renderTaskParams.selectionOutline.color = qt::QColorToGfVec4f(selectionColor);
    renderTaskParams.selectionOutline.radius = 3;

    renderTaskParams.captureVisible = captureVisibleRequested;
    renderTaskParams.captureProjectionMatrices.clear();
    if (renderTaskParams.captureVisible) {
        const int gridSize = std::clamp(captureVisibleGridSize, 1, 8);
        if (gridSize > 1) {
            renderTaskParams.captureProjectionMatrices.reserve(static_cast<size_t>(gridSize * gridSize));

            const GfVec2d tileSize(1.0 / static_cast<double>(gridSize), 1.0 / static_cast<double>(gridSize));
            for (int ty = 0; ty < gridSize; ++ty) {
                for (int tx = 0; tx < gridSize; ++tx) {
                    // ComputeNarrowedFrustum expects the center in NDC and the
                    // size as a fraction of the original frustum. Rendering
                    // each narrow tile at the full viewport resolution gives
                    // gridSize-times finer linear coverage while keeping the
                    // ID buffer allocation unchanged.
                    const GfVec2d center(-1.0 + (2.0 * (static_cast<double>(tx) + 0.5) / static_cast<double>(gridSize)),
                                         1.0 - (2.0 * (static_cast<double>(ty) + 0.5) / static_cast<double>(gridSize)));
                    const GfFrustum tileFrustum = frustum.ComputeNarrowedFrustum(center, tileSize);
                    renderTaskParams.captureProjectionMatrices.push_back(tileFrustum.ComputeProjectionMatrix());
                }
            }
        }
    }
    renderTaskParams.sceneIds.enabled = renderTaskParams.selectionOutline.enabled || renderTaskParams.captureVisible;
    renderTaskParams.sceneIds.roots = renderPaths;
    renderTaskParams.sceneIds.renderTags = { HdRenderTagTokens->geometry };
    if (settings.showGuides)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->guide);
    if (settings.showProxy)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->proxy);
    if (settings.showRender)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->render);
    renderTaskParams.sceneIds.size = size;
    renderTaskParams.sceneIds.viewport = renderViewport;
    renderTaskParams.sceneIds.viewMatrix = viewMatrix;
    renderTaskParams.sceneIds.projectionMatrix = projectionMatrix;

    const GfRange1d nearFar = frustum.GetNearFar();
    renderTaskParams.nearClip = static_cast<float>(nearFar.GetMin());
    renderTaskParams.farClip = static_cast<float>(nearFar.GetMax());
    renderTaskParams.orthographic = camera.GetProjection() == GfCamera::Orthographic;

    if (renderTaskParams.ambientOcclusion.enabled || renderTaskParams.selectionOutline.enabled) {
        const QString shader = renderShaderPath();
        if (!shader.isEmpty()) {
            renderTaskParams.shaderPath = TfToken(shader.toStdString());
        }
        else {
            static bool warnedMissingShader = false;
            if (!warnedMissingShader) {
                warnedMissingShader = true;
                qWarning() << "render effects disabled: could not locate resources/Render.glslfx";
            }
            renderTaskParams.ambientOcclusion.enabled = false;
            renderTaskParams.selectionOutline.enabled = false;
            renderTaskParams.sceneIds.enabled = renderTaskParams.captureVisible;
        }
    }

    if (renderTaskParams.sceneIds.enabled) {
        const QString sceneIdShader = sceneIdShaderPath();
        if (!sceneIdShader.isEmpty()) {
            renderTaskParams.sceneIdShaderPath = TfToken(sceneIdShader.toStdString());
        }
        else {
            static bool warnedMissingSceneIdShader = false;
            if (!warnedMissingSceneIdShader) {
                warnedMissingSceneIdShader = true;
                qWarning() << "scene ID pass disabled: could not locate resources/SceneId.glslfx";
            }
            renderTaskParams.sceneIds.enabled = false;
            renderTaskParams.selectionOutline.enabled = false;
            renderTaskParams.captureVisible = false;
        }
    }

    engine->renderBatchWithRenderTask(renderPaths, documentParams, renderTaskParams);

    if (captureVisibleRequested) {
        capturedVisiblePaths = renderTaskParams.captureVisible ? engine->takeCapturedVisiblePaths() : SdfPathVector {};
        captureVisibleRequested = false;
    }

    documentHgi->EndFrame();

    // Render viewport support geometry after Look processing. The dedicated
    // pass keeps /Display out of document AO/depth while preserving Stageviz
    // grid and helper presentation in the final viewport.
    if (auxiliaryEngine && auxiliary && auxiliary->GetPrimAtPath(paths::auxiliary::display)) {
        Hgi* auxiliaryHgi = auxiliaryEngine->GetHgi();
        if (!auxiliaryHgi)
            return false;

        UsdImagingGLRenderParams auxiliaryParams = documentParams;
        auxiliaryParams.highlight = false;
        auxiliaryParams.clearColor = GfVec4f(0.0f);
        // Viewport support geometry must remain independent of document
        // refinement. The grid and helpers use fixed authored geometry and
        // should not change when scene complexity is adjusted for NURBS or
        // other refinable document primitives.
        auxiliaryParams.complexity = 1.0;

        const UsdPrim auxiliaryRoot = auxiliary->GetPseudoRoot();
        auxiliaryHgi->StartFrame();
        auxiliaryEngine->PrepareBatch(auxiliaryRoot, auxiliaryParams);
        updateAuxiliaryRenderSceneIndex();
        auxiliaryEngine->renderBatch({ paths::auxiliary::display }, auxiliaryParams);
        auxiliaryHgi->EndFrame();
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
    return p->engine != nullptr && (p->contextMode == ContextMode::Offscreen || p->auxiliaryEngine != nullptr);
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

        if (!p->selected.contains(primPath)) {
            p->selected.append(primPath);
        }
    }
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

QList<SdfPath>
RenderEngine::captureVisiblePaths(int gridSize)
{
    QList<SdfPath> result;

    if (p->contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return result;

    p->captureVisibleRequested = true;
    p->captureVisibleGridSize = std::clamp(gridSize, 1, 8);
    p->capturedVisiblePaths.clear();

    if (!renderToCurrentFramebuffer()) {
        p->captureVisibleRequested = false;
        return result;
    }

    result.reserve(static_cast<qsizetype>(p->capturedVisiblePaths.size()));
    for (const SdfPath& path : p->capturedVisiblePaths)
        result.append(path);

    p->capturedVisiblePaths.clear();
    return result;
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
