// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderengine.h"
#include "application.h"
#include "gizmoprojection.h"
#include "notice.h"
#include "paths.h"
#include "perfscope.h"
#include "qtutils.h"
#include "renderdiagnostics.h"
#include "renderisolation.h"
#include "renderresources.h"
#include "rendersceneindex.h"
#include "renderselection.h"
#include "rendertask.h"
#include "ui.h"
#include <QColorSpace>
#include <QJsonArray>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions>
#include <QScopeGuard>
#include <QSurfaceFormat>
#include <QtGui/qopengl.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/work/loops.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/instanceSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hdx/colorizeSelectionTask.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/taskControllerSceneIndex.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <pxr/usdImaging/usdImaging/usdPrimInfoSchema.h>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

    QString renderShaderPath()
    {
        static const QString path = renderresources::path(QStringLiteral("Render.glslfx"));
        return path;
    }

    QString sceneIdShaderPath()
    {
        static const QString path = renderresources::path(QStringLiteral("SceneId.glslfx"));
        return path;
    }

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
            HdSceneIndexObserver::RenamedPrimEntries renamed;
            HdSceneIndexObserver::RemovedPrimEntries removed;
            HdSceneIndexObserver::AddedPrimEntries added;
            const HdSceneIndexBaseRefPtr input = _GetInputSceneIndex();
            for (const auto& entry : entries) {
                const bool oldIsMaterial = isMaterialPath(entry.oldPrimPath);
                const bool newIsMaterial = isMaterialPath(entry.newPrimPath);
                if (oldIsMaterial && newIsMaterial)
                    renamed.push_back(entry);
                else if (oldIsMaterial)
                    removed.emplace_back(entry.oldPrimPath);
                else if (newIsMaterial && input) {
                    const HdSceneIndexPrim prim = input->GetPrim(entry.newPrimPath);
                    added.emplace_back(entry.newPrimPath, prim.primType);
                }
            }
            if (!renamed.empty())
                _SendPrimsRenamed(renamed);
            if (!removed.empty())
                _SendPrimsRemoved(removed);
            if (!added.empty())
                _SendPrimsAdded(added);
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
        RenderSelection selection;
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

        HdRenderIndex* renderIndex() { return _GetRenderIndex(); }

        Hgi* hgi()
        {
            if (!_GetRenderIndex())
                return nullptr;
            // GetHgi() only returns UsdImagingGLEngine's owned Hgi. A shared
            // driver is supplied externally and remains in _hgiDriver.
            return _hgiDriver.driver.IsHolding<Hgi*>() ? _hgiDriver.driver.UncheckedGet<Hgi*>() : GetHgi();
        }

        const HdRprimCollection& renderCollection() const { return _renderCollection; }

        SceneIndices& sceneIndices() { return *m_sceneIndices; }
        const SceneIndices& sceneIndices() const { return *m_sceneIndices; }

        bool resolveSelectionRprims(const UsdStageRefPtr& stage, const SdfPathVector& paths, SdfPathVector* directPaths,
                                    std::vector<HydraSelectionRprim>* rprims)
        {
            return m_sceneIndices->selection.resolve(stage, paths, directPaths, rprims);
        }

        uint64_t selectionMappingGeneration() const { return m_sceneIndices->selection.generation(); }

        void renderBatch(const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                         const SdfPathVector& excludes = {})
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            prepareSubmission(paths, params, true, excludes);

            _Execute(params, _taskControllerSceneIndex->GetRenderingTaskPaths());
        }

        void renderBatchWithRenderTask(const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                                       const RenderTaskParams& renderTaskParams, bool present = true)
        {
            if (!_taskControllerSceneIndex || paths.empty())
                return;

            prepareSubmission(paths, params, present, {});

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



        bool displayModesPrepared(const SdfPathVector& paths, const UsdImagingGLRenderParams& params)
        {
            HdRenderIndex* index = renderIndex();
            if (!index)
                return false;
            const auto& tracker = index->GetChangeTracker();
            const auto& previous = m_displayWarmupParams;
            return m_displayWarmupValid && m_displayWarmupRprimVersion == tracker.GetRprimIndexVersion()
                   && m_displayWarmupInstancerVersion == tracker.GetInstancerIndexVersion()
                   && m_displayWarmupSprimVersion == tracker.GetSprimIndexVersion() && m_displayWarmupPaths == paths
                   && previous.complexity == params.complexity && previous.cullStyle == params.cullStyle
                   && previous.flipFrontFacing == params.flipFrontFacing
                   && previous.enableSampleAlphaToCoverage == params.enableSampleAlphaToCoverage
                   && previous.enableLighting == params.enableLighting
                   && previous.enableSceneLights == params.enableSceneLights
                   && previous.enableSceneMaterials == params.enableSceneMaterials
                   && previous.showGuides == params.showGuides && previous.showProxy == params.showProxy
                   && previous.showRender == params.showRender;
        }

        void markDisplayModesPrepared(const SdfPathVector& paths, const UsdImagingGLRenderParams& params)
        {
            const auto& tracker = renderIndex()->GetChangeTracker();
            m_displayWarmupParams = params;
            m_displayWarmupPaths = paths;
            m_displayWarmupRprimVersion = tracker.GetRprimIndexVersion();
            m_displayWarmupInstancerVersion = tracker.GetInstancerIndexVersion();
            m_displayWarmupSprimVersion = tracker.GetSprimIndexVersion();
            m_displayWarmupValid = true;
        }

        void warmDisplayModes(const UsdStageRefPtr& stage, const SdfPathVector& paths,
                              const UsdImagingGLRenderParams& params, const RenderTaskParams& taskParams)
        {
            if (!renderIndex() || displayModesPrepared(paths, params))
                return;

            PerfScope scope("viewport.display_modes.warmup");
            for (const auto mode :
                 { UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH, UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE }) {
                if (mode == params.drawMode)
                    continue;
                auto warmParams = params;
                warmParams.drawMode = mode;
                auto warmTaskParams = taskParams;
                warmTaskParams.selectionOutline.enabled = false;
                warmTaskParams.ambientOcclusion.enabled = false;
                warmTaskParams.captureVisible = false;
                warmTaskParams.captureObjects = false;
                warmTaskParams.captureGpuPick = false;
                warmTaskParams.captureElementId = false;
                warmTaskParams.captureProjectionMatrices.clear();
                // Use the same engine and task resources, but do not present
                // the alternate mode to the widget's current framebuffer.
                PrepareBatch(stage->GetPseudoRoot(), warmParams);
                renderBatchWithRenderTask(paths, warmParams, warmTaskParams, false);
            }
            markDisplayModesPrepared(paths, params);
            // The visible submission below restores the requested collection,
            // task parameters and presentation after this hidden preparation.
            PrepareBatch(stage->GetPseudoRoot(), params);
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

        GpuPickResult takeGpuPickResult()
        {
            HdRenderIndex* index = _GetRenderIndex();
            if (!index || !index->HasTask(m_renderTaskPath))
                return {};
            const HdTaskSharedPtr& task = index->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeGpuPickResult() : GpuPickResult {};
        }

        ViewportObjectIds takeCapturedObjects()
        {
            HdRenderIndex* index = _GetRenderIndex();
            if (!index || !index->HasTask(m_renderTaskPath))
                return {};
            const auto task = index->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeCapturedObjects() : ViewportObjectIds {};
        }

        int32_t takeCapturedElementId()
        {
            HdRenderIndex* renderIndex = _GetRenderIndex();
            if (!renderIndex || !renderIndex->HasTask(m_renderTaskPath))
                return -1;

            const HdTaskSharedPtr& task = renderIndex->GetTask(m_renderTaskPath);
            auto* renderTask = dynamic_cast<RenderTask*>(task.get());
            return renderTask ? renderTask->takeCapturedElementId() : -1;
        }

    private:
        void prepareSubmission(const SdfPathVector& paths, const UsdImagingGLRenderParams& params, bool present,
                               const SdfPathVector& excludes)
        {
            _UpdateHydraCollection(&_renderCollection, paths, params);
            // Reset exclusions on every submission so collection state cannot
            // retain paths excluded by an earlier submission.
            _renderCollection.SetExcludePaths(excludes);
            _taskControllerSceneIndex->SetCollection(_renderCollection);
            _PrepareRender(params);
            _SetBBoxParams(params.bboxes, params.bboxLineColor, params.bboxLineDashSize);
            _taskControllerSceneIndex->SetEnableSelection(false);
            _taskControllerSceneIndex->SetEnablePresentation(present);

            const VtValue selectionValue(_selTracker);
            if (HdEngine* hdEngine = _GetHdEngine())
                hdEngine->SetTaskContextData(HdxTokens->selectionState, selectionValue);
        }

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
            static std::once_flag registered;
            std::call_once(registered, [] {
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
                        sceneIndices->selection.setSceneIndex(inputScene);
                    }

                    return mergingSceneIndex;
                };

                HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
                    std::string(), callback, nullptr, 0, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
            });
        }

        static UsdImagingGLEngine::Parameters prepareParameters(UsdImagingGLEngine::Parameters params,
                                                                const SceneIndicesPtr& sceneIndices)
        {
            registerSceneIndexFilters();
            constructingSceneIndices() = sceneIndices.get();
            return params;
        }

        bool m_displayWarmupValid = false;
        unsigned m_displayWarmupRprimVersion = 0;
        unsigned m_displayWarmupInstancerVersion = 0;
        unsigned m_displayWarmupSprimVersion = 0;
        UsdImagingGLRenderParams m_displayWarmupParams;
        SdfPathVector m_displayWarmupPaths;
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
    struct DisplayModeWarmup {
        UsdStageRefPtr stage;
        SdfPathVector paths;
        UsdImagingGLRenderParams params;
        RenderTaskParams taskParams;
    };

    explicit Private(ContextMode mode) { d.contextMode = mode; }

    bool ensureCurrentContext();
    bool initialize();
    void resetEngine(bool resetAuxiliary = true);
    void reset();
    UsdStageRefPtr documentStage();
    void ensureAuxiliarySceneIndex();
    void refreshAuxiliarySceneIndex();
    void updateDocumentRenderSceneIndex();
    void updateAuxiliaryRenderSceneIndex();
    void updateRenderParams();
    void updateLighting();
    void configureEngine(ImagingGLEngine* target, const GfVec4d& renderViewport);
    void refreshMaterials();
    void resolveSelection();
    RenderTaskParams makeRenderTaskParams(const SdfPathVector& renderPaths, const GfVec4d& renderViewport);
    void warmDocument(const UsdStageRefPtr& renderStage, const SdfPathVector& renderPaths,
                      const UsdImagingGLRenderParams& documentParams, const RenderTaskParams& renderTaskParams,
                      bool deferDisplayModeWarmup);
    void prepareDisplayModes(ImagingGLEngine& target, const UsdStageRefPtr& stage, const SdfPathVector& paths,
                             const UsdImagingGLRenderParams& params, const RenderTaskParams& taskParams,
                             std::optional<DisplayModeWarmup>* deferred);
    void runDisplayModeWarmup(ImagingGLEngine& target, const DisplayModeWarmup& warmup);
    void warmDeferredDisplayModes();
    bool warmGizmo(const UsdImagingGLRenderParams& gizmoParams, const GfMatrix4d& gizmoTransform);
    void collectCaptures(const RenderTaskParams& renderTaskParams);
    bool renderOverlays(const UsdImagingGLRenderParams& documentParams, const GfMatrix4d& viewMatrix,
                        bool deferDisplayModeWarmup);
    bool render(bool deferDisplayModeWarmup);

    struct Data {
        ContextMode contextMode = ContextMode::Current;
        Settings settings;
        UsdStageRefPtr stage;
        RenderIsolation isolation;
        std::set<SdfPath> pendingMaterialChanges;
        UsdStageWeakPtr imagingStage;
        UsdStageWeakPtr cachedImagingStage;
        uint64_t documentWarmupSourceVersion = 0;
        uint64_t deferredWarmupSourceVersion = 0;
        std::optional<DisplayModeWarmup> deferredDocumentWarmup;
        std::optional<DisplayModeWarmup> deferredAuxiliaryWarmup;
        UsdStageRefPtr auxiliary;
        GfCamera camera;
        GfVec2i size = GfVec2i(512, 512);
        double devicePixelRatio = 1.0;
        GfVec4d viewport = GfVec4d(0.0);
        QList<SdfPath> mask;
        QList<SdfPath> selected;
        SdfPathVector cachedSelectedPaths;
        SdfPathVector cachedDirectSelectionPaths;
        SdfPathVector cachedLeadSelectionPaths;
        SdfPathVector cachedLeadDirectSelectionPaths;
        std::vector<HydraSelectionRprim> cachedSelectedHydraRprims;
        std::vector<HydraSelectionRprim> cachedLeadHydraRprims;
        bool cachedHydraSelectionResolved = false;
        bool cachedLeadHydraSelectionResolved = false;
        std::vector<SelectionProxyTarget> cachedSelectedProxyTargets;
        std::vector<SelectionProxyTarget> cachedLeadProxyTargets;
        bool selectionCacheValid = false;
        uint64_t cachedSelectionMappingGeneration = 0;
        bool interactiveNavigation = false;
        QColor selectionColor = ui()->color(UI::ColorRole::Selection);
        QColor secondarySelectionColor = ui()->color(UI::ColorRole::SelectionAlt);
        bool linearColorCapture = false;
        bool captureVisibleRequested = false;
        bool captureObjectsRequested = false;
        ViewportObjectIds capturedObjects;
        bool captureElementIdRequested = false;
        bool captureGpuPickRequested = false;
        GfVec2i gpuPickPixel = GfVec2i(-1, -1);
        SdfPathVector gpuPickExcludes;
        GpuPickResult capturedGpuPick;
        GfVec2i captureElementPixel = GfVec2i(-1, -1);
        int32_t capturedElementId = -1;
        int captureVisibleGridSize = 4;
        SdfPathVector capturedVisiblePaths;
        UsdImagingGLRenderParams params;
        HgiUniquePtr sharedDocumentHgi;
        std::unique_ptr<ImagingGLEngine> engine;
        std::unique_ptr<ImagingGLEngine> cachedEngine;
        std::unique_ptr<ImagingGLEngine> auxiliaryEngine;
        std::unique_ptr<ImagingGLEngine> gizmoEngine;
        bool gizmoRendered = false;
        bool gizmoWarmed = false;
        std::unique_ptr<QOpenGLContext> offscreenContext;
        std::unique_ptr<QOffscreenSurface> offscreenSurface;
    } d;
};

bool
RenderEngine::Private::ensureCurrentContext()
{
    if (d.contextMode == ContextMode::Current)
        return QOpenGLContext::currentContext() != nullptr;

    if (!d.offscreenContext) {
        QSurfaceFormat format;
        format.setSamples(4);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setAlphaBufferSize(8);
        format.setColorSpace(QColorSpace::SRgb);

        d.offscreenSurface = std::make_unique<QOffscreenSurface>();
        d.offscreenSurface->setFormat(format);
        d.offscreenSurface->create();

        if (!d.offscreenSurface->isValid())
            return false;

        d.offscreenContext = std::make_unique<QOpenGLContext>();
        d.offscreenContext->setFormat(format);

        if (QOpenGLContext::globalShareContext())
            d.offscreenContext->setShareContext(QOpenGLContext::globalShareContext());

        if (!d.offscreenContext->create()) {
            d.offscreenContext.reset();
            return false;
        }
    }

    return d.offscreenContext->makeCurrent(d.offscreenSurface.get());
}

bool
RenderEngine::Private::initialize()
{
    if (d.engine && (d.contextMode == ContextMode::Offscreen || (d.auxiliaryEngine && d.gizmoEngine)))
        return true;

    if (!ensureCurrentContext())
        return false;

    UsdImagingGLEngine::Parameters documentParams {};
    documentParams.displayUnloadedPrimsWithBounds = false;
    // Stageviz has no independent polling loop for Hydra's asynchronous scene
    // processing. Keep updates synchronous so scene-index changes are fully
    // applied during PrepareBatch before dependent work such as selection mapping.
    documentParams.allowAsynchronousSceneProcessing = false;

    // Full and isolated documents keep independent scene indices but share
    // their Hgi so Storm can reuse shader and geometry resource registries.
    if (!d.sharedDocumentHgi)
        d.sharedDocumentHgi = Hgi::CreatePlatformDefaultHgi();
    if (!d.sharedDocumentHgi)
        return false;
    documentParams.driver.name = HgiTokens->renderDriver;
    documentParams.driver.driver = VtValue(d.sharedDocumentHgi.get());
    if (!d.engine)
        d.engine = std::make_unique<ImagingGLEngine>(documentParams);
    if (!d.engine->hgi()) {
        d.engine.reset();
        return false;
    }

    // Transform handles remain a separate final presentation pass. Selection
    // is handled by the Stageviz RenderTask as a screen-space outline.
    // Offscreen rendering intentionally omits viewport-only auxiliary content.
    if (d.contextMode == ContextMode::Current) {
        UsdImagingGLEngine::Parameters overlayParams = documentParams;
        overlayParams.allowAsynchronousSceneProcessing = false;
        overlayParams.driver = HdDriver();

        if (!d.auxiliaryEngine)
            d.auxiliaryEngine = std::make_unique<ImagingGLEngine>(overlayParams);
        if (!d.auxiliaryEngine->hgi()) {
            d.auxiliaryEngine.reset();
            d.engine.reset();
            return false;
        }
        // Keep the grid and gizmo in independent imaging engines so their
        // camera, collection and GPU resources are not reused between passes.
        if (!d.gizmoEngine)
            d.gizmoEngine = std::make_unique<ImagingGLEngine>(overlayParams);
        if (!d.gizmoEngine->hgi()) {
            d.gizmoEngine.reset();
            d.auxiliaryEngine.reset();
            d.engine.reset();
            return false;
        }
    }

    updateDocumentRenderSceneIndex();
    updateAuxiliaryRenderSceneIndex();
    ensureAuxiliarySceneIndex();

    return true;
}

void
RenderEngine::Private::resetEngine(bool resetAuxiliary)
{
    d.deferredDocumentWarmup.reset();
    d.deferredAuxiliaryWarmup.reset();
    if (!d.engine && !d.cachedEngine && !d.auxiliaryEngine && !d.gizmoEngine)
        return;

    if (resetAuxiliary) {
        d.gizmoRendered = false;
        d.gizmoWarmed = false;
    }
    d.selectionCacheValid = false;

    if (d.contextMode == ContextMode::Offscreen) {
        OpenGLContextRestore contextRestore;

        if (!ensureCurrentContext()) {
            qWarning() << "could not make offscreen context current while resetting render engine";
            return;
        }

        if (resetAuxiliary) {
            d.gizmoEngine.reset();
            d.auxiliaryEngine.reset();
        }
        d.cachedEngine.reset();
        d.engine.reset();
        return;
    }

    if (resetAuxiliary) {
        d.gizmoEngine.reset();
        d.auxiliaryEngine.reset();
    }
    d.cachedEngine.reset();
    d.engine.reset();
}

void
RenderEngine::Private::reset()
{
    d.deferredDocumentWarmup.reset();
    d.deferredAuxiliaryWarmup.reset();
    d.gizmoRendered = false;
    d.gizmoWarmed = false;
    if (d.contextMode == ContextMode::Offscreen) {
        {
            OpenGLContextRestore contextRestore;

            if ((d.engine || d.cachedEngine || d.auxiliaryEngine || d.gizmoEngine)
                && (!d.offscreenContext || !d.offscreenSurface
                    || !d.offscreenContext->makeCurrent(d.offscreenSurface.get()))) {
                qWarning() << "could not make offscreen context current while releasing render engine";
                return;
            }

            d.gizmoEngine.reset();
            d.auxiliaryEngine.reset();
            d.cachedEngine.reset();
            d.engine.reset();
            d.sharedDocumentHgi.reset();
        }

        d.offscreenContext.reset();
        d.offscreenSurface.reset();
        return;
    }

    d.gizmoEngine.reset();
    d.auxiliaryEngine.reset();
    d.cachedEngine.reset();
    d.engine.reset();
    d.sharedDocumentHgi.reset();
}

UsdStageRefPtr
RenderEngine::Private::documentStage()
{
    const UsdStageRefPtr rendered = d.isolation.stage();
    if (d.imagingStage != rendered) {
        if (!d.imagingStage) {
            d.imagingStage = rendered;
        }
        else if (d.cachedEngine && d.cachedImagingStage == rendered) {
            d.engine.swap(d.cachedEngine);
            std::swap(d.imagingStage, d.cachedImagingStage);
        }
        else {
            d.cachedEngine = std::move(d.engine);
            d.cachedImagingStage = d.imagingStage;
            d.imagingStage = rendered;
        }
        d.selectionCacheValid = false;
        d.gizmoRendered = false;
    }
    return rendered;
}

void
RenderEngine::Private::ensureAuxiliarySceneIndex()
{
    if (!d.auxiliary)
        return;

    for (ImagingGLEngine* target : { d.engine.get() }) {
        if (!target)
            continue;

        SceneIndices& sceneIndices = target->sceneIndices();

        if (sceneIndices.auxiliaryInserted || !sceneIndices.merging)
            continue;

        UsdImagingCreateSceneIndicesInfo createInfo;
        createInfo.stage = d.auxiliary;
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
    for (ImagingGLEngine* target : { d.engine.get() }) {
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
    if (!d.engine)
        return;

    SceneIndices& sceneIndices = d.engine->sceneIndices();
    if (!sceneIndices.renderSceneIndex)
        return;

    TfRefPtr<RenderSceneIndex> renderSceneIndex = sceneIndices.renderSceneIndex;

    renderSceneIndex->setSceneMaterialsEnabled(d.settings.sceneMaterialsEnabled);
    renderSceneIndex->setMaterialPath(d.settings.overrideMaterial);

    RenderSceneIndex::Mode mode = RenderSceneIndex::None;
    switch (d.settings.materialMode) {
    case MaterialMode::Clay: mode = RenderSceneIndex::Clay; break;
    case MaterialMode::Override: mode = RenderSceneIndex::Custom; break;
    case MaterialMode::Scene:
    default: mode = RenderSceneIndex::None; break;
    }

    renderSceneIndex->setMode(mode);

    const bool overrideDoubleSided = d.settings.doubleSidedMode == DoubleSidedMode::DoubleSided;
    renderSceneIndex->setDoubleSidedOverride(false);
    renderSceneIndex->setDoubleSidedOverrideEnabled(overrideDoubleSided);
}

void
RenderEngine::Private::updateAuxiliaryRenderSceneIndex()
{
    for (auto* target : { d.auxiliaryEngine.get(), d.gizmoEngine.get() }) {
        if (!target)
            continue;
        SceneIndices& sceneIndices = target->sceneIndices();
        if (!sceneIndices.renderSceneIndex)
            continue;
        auto renderSceneIndex = sceneIndices.renderSceneIndex;
        renderSceneIndex->setSceneMaterialsEnabled(true);
        renderSceneIndex->setMaterialPath({});
        renderSceneIndex->setMode(RenderSceneIndex::None);
        renderSceneIndex->setDoubleSidedOverrideEnabled(false);
    }
}

void
RenderEngine::Private::updateRenderParams()
{
    d.params.clearColor = qt::QColorToGfVec4f(d.settings.clearColor);
    d.params.drawMode = d.settings.drawMode;
    d.params.complexity = d.settings.complexity;
    d.params.frame = d.settings.timeCode;
    d.params.cullStyle = cullStyle(d.settings.doubleSidedMode);
    d.params.enableLighting = true;
    d.params.gammaCorrectColors = d.settings.gammaCorrectColors;
    d.params.enableSampleAlphaToCoverage = d.settings.sampleAlphaToCoverageEnabled;
    d.params.enableSceneLights = d.settings.sceneLightsEnabled;
    d.params.enableSceneMaterials = true;
    d.params.flipFrontFacing = d.settings.flipFrontFacing;
    d.params.showGuides = d.settings.showGuides;
    d.params.showProxy = d.settings.showProxy;
    d.params.showRender = d.settings.showRender;
    d.params.highlight = false;
}

void
RenderEngine::Private::updateLighting()
{
    if (!d.engine && !d.auxiliaryEngine && !d.gizmoEngine)
        return;

    std::vector<GlfSimpleLight> lights;

    if (d.settings.defaultCameraLightEnabled) {
        const GfMatrix4d cameraTransform = d.camera.GetTransform();
        const GfVec3d cameraPosition = cameraTransform.ExtractTranslation();

        GlfSimpleLight light;
        light.SetAmbient(GfVec4f(0, 0, 0, 0));
        light.SetPosition(GfVec4f(cameraPosition[0], cameraPosition[1], cameraPosition[2], 1.0f));
        light.SetTransform(cameraTransform);

        lights.push_back(light);
    }

    if (d.settings.defaultDomeLightEnabled) {
        GlfSimpleLight light;
        light.SetAmbient(GfVec4f(0, 0, 0, 0));
        light.SetPosition(GfVec4f(0, 0, 1, 0));
        light.SetIsDomeLight(true);

        if (d.stage && UsdGeomGetStageUpAxis(d.stage) == UsdGeomTokens->z) {
            GfMatrix4d domeTransform(1.0);
            domeTransform.SetRotate(GfRotation(GfVec3d(1.0, 0.0, 0.0), 90.0));
            light.SetTransform(domeTransform);
        }

        const QString texture = d.settings.domeLightTexture.isEmpty() ? renderresources::defaultDomeTexture()
                                                                      : d.settings.domeLightTexture;
        if (!texture.isEmpty()) {
            const std::string filename = texture.toStdString();
            light.SetDomeLightTextureFile(SdfAssetPath(filename, filename));
        }

        lights.push_back(light);
    }

    const GfVec4f ambient(d.settings.defaultAmbient, d.settings.defaultAmbient, d.settings.defaultAmbient, 1.0f);

    GlfSimpleMaterial material;
    material.SetAmbient(ambient);
    material.SetSpecular(
        GfVec4f(d.settings.defaultSpecular, d.settings.defaultSpecular, d.settings.defaultSpecular, 1.0f));
    material.SetShininess(d.settings.defaultShininess);

    if (d.engine)
        d.engine->SetLightingState(lights, material, ambient);
    if (d.auxiliaryEngine)
        d.auxiliaryEngine->SetLightingState(lights, material, ambient);
    if (d.gizmoEngine)
        d.gizmoEngine->SetLightingState(lights, material, ambient);
}

void
RenderEngine::Private::configureEngine(ImagingGLEngine* target, const GfVec4d& renderViewport)
{
    const GfFrustum frustum = d.camera.GetFrustum();
    const auto viewMatrix = frustum.ComputeViewMatrix();
    const auto projectionMatrix = frustum.ComputeProjectionMatrix();

    // Only the document pass owns the background. A second opaque dome
    // in the helper pass covers translucent document pixels, which do not
    // write depth, even when their color was already presented correctly.
    target->SetRendererSetting(TfToken("domeLightCameraVisibility"),
                               VtValue(target == d.engine.get() && d.settings.domeLightCameraVisibility));
    target->SetRendererAov(d.settings.aov);
    target->SetRenderBufferSize(d.size);
    // CameraUtilFraming uses image coordinates (origin at the upper left),
    // while the widget's render viewport uses GL coordinates (origin at
    // the lower left).
    const double viewportTop = d.size[1] - (renderViewport[1] + renderViewport[3]);
    const double viewportBottom = d.size[1] - renderViewport[1];
    const GfVec2i viewportMin(static_cast<int>(std::floor(renderViewport[0])),
                              static_cast<int>(std::floor(viewportTop)));
    const GfVec2i viewportMax(static_cast<int>(std::ceil(renderViewport[0] + renderViewport[2])),
                              static_cast<int>(std::ceil(viewportBottom)));
    const GfVec2i framingMin(std::clamp(viewportMin[0], 0, d.size[0]), std::clamp(viewportMin[1], 0, d.size[1]));
    const GfVec2i framingMax(std::clamp(viewportMax[0], framingMin[0], d.size[0]),
                             std::clamp(viewportMax[1], framingMin[1], d.size[1]));
    const GfRect2i dataWindow(framingMin, framingMax);
    const GfRange2f displayWindow { GfVec2f(framingMin), GfVec2f(framingMax) };
    target->SetFraming(CameraUtilFraming(displayWindow, dataWindow));
    target->SetWindowPolicy(CameraUtilMatchVertically);
    target->SetCameraState(viewMatrix, projectionMatrix);
}

void
RenderEngine::Private::refreshMaterials()
{
    if (!d.pendingMaterialChanges.empty()) {
        std::set<SdfPath> materials;
        for (auto path : d.pendingMaterialChanges) {
            for (; !path.IsEmpty(); path = path.GetParentPath()) {
                if (UsdShadeMaterial(d.stage->GetPrimAtPath(path))) {
                    materials.insert(path);
                    break;
                }
            }
        }
        d.pendingMaterialChanges.clear();
        if (const auto filter = d.engine->sceneIndices().renderSceneIndex)
            filter->refreshMaterials(materials);
        if (d.cachedEngine) {
            if (const auto filter = d.cachedEngine->sceneIndices().renderSceneIndex)
                filter->refreshMaterials(materials);
        }
    }
}

void
RenderEngine::Private::resolveSelection()
{
    // PrepareBatch applies pending USD stage-scene-index updates. Resolve the
    // selection afterward so the mapper and its cache observe the same current
    // Hydra hierarchy and instance topology that the render tasks will consume.
    const uint64_t mappingGeneration = d.engine->selectionMappingGeneration();
    if (!d.selectionCacheValid || d.cachedSelectionMappingGeneration != mappingGeneration) {
        PerfScope perfScope("selection.hydra_mapping");
        d.cachedSelectedPaths.clear();
        d.cachedSelectedPaths.reserve(d.selected.size());
        d.cachedSelectedProxyTargets.clear();
        d.cachedLeadSelectionPaths.clear();
        d.cachedLeadDirectSelectionPaths.clear();
        d.cachedLeadHydraRprims.clear();
        d.cachedLeadProxyTargets.clear();
        d.cachedLeadHydraSelectionResolved = false;
        std::unordered_set<SdfPath, SdfPath::Hash> viewportPaths;
        viewportPaths.reserve(d.selected.size());

        for (const SdfPath& selectedPath : d.selected) {
            const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
            const UsdPrim prim = d.stage->GetPrimAtPath(path);
            // Project subset selection only for drawing. The selection model
            // retains the exact subset path for inspection and authoring.
            const UsdPrim viewportPrim = prim && prim.IsA<UsdGeomSubset>() ? prim.GetParent() : prim;
            const SdfPath viewportPath = viewportPrim ? viewportPrim.GetPath() : path;
            if (viewportPath.IsEmpty() || !viewportPaths.insert(viewportPath).second)
                continue;

            if (viewportPrim && viewportPrim.IsInstanceProxy()) {
                UsdPrim prototypePrim = viewportPrim.GetPrimInPrototype();
                UsdPrim prototypeRoot = prototypePrim;
                while (prototypeRoot && !prototypeRoot.IsPrototype())
                    prototypeRoot = prototypeRoot.GetParent();
                if (prototypePrim && prototypeRoot) {
                    d.cachedSelectedProxyTargets.push_back(
                        { viewportPath, prototypeRoot.GetPath(), prototypePrim.GetPath() });
                }
            }

            d.cachedSelectedPaths.push_back(viewportPath);
        }

        d.cachedSelectedHydraRprims.clear();
        d.cachedDirectSelectionPaths.clear();
        d.cachedHydraSelectionResolved = d.engine
                                         && d.engine->resolveSelectionRprims(d.stage, d.cachedSelectedPaths,
                                                                             &d.cachedDirectSelectionPaths,
                                                                             &d.cachedSelectedHydraRprims);
        if (!d.cachedSelectedPaths.empty()) {
            // The most recently selected path is the active object. Keep its
            // outline in the primary selection color, matching the Outliner
            // current item and the gizmo's lead transform target.
            d.cachedLeadSelectionPaths.push_back(d.cachedSelectedPaths.back());
            for (const SelectionProxyTarget& target : d.cachedSelectedProxyTargets) {
                if (target.proxyPath == d.cachedLeadSelectionPaths.front())
                    d.cachedLeadProxyTargets.push_back(target);
            }
            d.cachedLeadHydraSelectionResolved = d.engine->resolveSelectionRprims(d.stage, d.cachedLeadSelectionPaths,
                                                                                  &d.cachedLeadDirectSelectionPaths,
                                                                                  &d.cachedLeadHydraRprims);
        }
        d.cachedSelectionMappingGeneration = d.engine->selectionMappingGeneration();
        d.selectionCacheValid = true;
    }
}

RenderTaskParams
RenderEngine::Private::makeRenderTaskParams(const SdfPathVector& renderPaths, const GfVec4d& renderViewport)
{
    const auto& selectedPaths = d.cachedSelectedPaths;
    const auto& documentParams = d.params;
    const GfFrustum frustum = d.camera.GetFrustum();
    const GfMatrix4d viewMatrix = frustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = frustum.ComputeProjectionMatrix();
    RenderTaskParams renderTaskParams;
    renderTaskParams.applyColorManagement = !d.linearColorCapture && d.settings.aov == HdAovTokens->color;
    renderTaskParams.colorManagement = d.settings.colorManagement;
    renderTaskParams.ambientOcclusion = d.settings.ambientOcclusion;
    renderTaskParams.ambientOcclusion.enabled = d.settings.ambientOcclusion.enabled && !d.interactiveNavigation
                                                && d.settings.aov == HdAovTokens->color
                                                && d.settings.drawMode
                                                       != UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE;
    renderTaskParams.projectionMatrix = GfMatrix4f(projectionMatrix);

    renderTaskParams.selectionOutline.enabled = !d.captureObjectsRequested && d.contextMode == ContextMode::Current
                                                && !selectedPaths.empty() && d.settings.aov == HdAovTokens->color;
    {
        renderTaskParams.selectionOutline.paths = selectedPaths;
        renderTaskParams.selectionOutline.leadPaths = d.cachedLeadSelectionPaths;
        renderTaskParams.selectionOutline.directPaths = d.cachedDirectSelectionPaths;
        renderTaskParams.selectionOutline.leadDirectPaths = d.cachedLeadDirectSelectionPaths;
        renderTaskParams.selectionOutline.hydraRprims = d.cachedSelectedHydraRprims;
        renderTaskParams.selectionOutline.leadHydraRprims = d.cachedLeadHydraRprims;
        renderTaskParams.selectionOutline.hydraMappingResolved = d.cachedHydraSelectionResolved;
        renderTaskParams.selectionOutline.leadHydraMappingResolved = d.cachedLeadHydraSelectionResolved;
        renderTaskParams.selectionOutline.proxyTargets = d.cachedSelectedProxyTargets;
        renderTaskParams.selectionOutline.leadProxyTargets = d.cachedLeadProxyTargets;
    }
    renderTaskParams.selectionOutline.color = qt::QColorToGfVec4f(d.selectionColor);
    renderTaskParams.selectionOutline.secondaryColor = qt::QColorToGfVec4f(d.secondarySelectionColor);
    renderTaskParams.selectionOutline.radius = static_cast<unsigned int>(
        std::clamp(std::lround(d.devicePixelRatio), 1l, 4l));

    renderTaskParams.captureVisible = d.captureVisibleRequested;
    renderTaskParams.captureObjects = d.captureObjectsRequested;
    renderTaskParams.captureElementId = d.captureElementIdRequested;
    renderTaskParams.captureGpuPick = d.captureGpuPickRequested;
    renderTaskParams.gpuPickPixel = d.gpuPickPixel;
    renderTaskParams.gpuPickExcludes = d.gpuPickExcludes;
    renderTaskParams.captureElementPixel = d.captureElementPixel;
    renderTaskParams.captureProjectionMatrices.clear();
    if (renderTaskParams.captureVisible) {
        const int gridSize = std::clamp(d.captureVisibleGridSize, 1, 8);
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
    renderTaskParams.prepareSelection = !d.captureObjectsRequested && d.contextMode == ContextMode::Current
                                        && d.settings.aov == HdAovTokens->color;
    renderTaskParams.sceneIds.enabled = renderTaskParams.prepareSelection || renderTaskParams.selectionOutline.enabled
                                        || renderTaskParams.captureVisible || renderTaskParams.captureElementId
                                        || renderTaskParams.captureGpuPick || renderTaskParams.captureObjects;
    if (d.captureObjectsRequested) {
        switch (documentParams.cullStyle) {
        case UsdImagingGLCullStyle::CULL_STYLE_BACK: renderTaskParams.sceneIds.cullStyle = HdCullStyleBack; break;
        case UsdImagingGLCullStyle::CULL_STYLE_FRONT: renderTaskParams.sceneIds.cullStyle = HdCullStyleFront; break;
        case UsdImagingGLCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED:
            renderTaskParams.sceneIds.cullStyle = HdCullStyleBackUnlessDoubleSided;
            break;
        default: renderTaskParams.sceneIds.cullStyle = HdCullStyleNothing; break;
        }
    }
    renderTaskParams.sceneIds.roots = renderPaths;
    renderTaskParams.sceneIds.renderTags = { HdRenderTagTokens->geometry };
    if (d.settings.showGuides)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->guide);
    if (d.settings.showProxy)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->proxy);
    if (d.settings.showRender)
        renderTaskParams.sceneIds.renderTags.push_back(HdRenderTagTokens->render);
    renderTaskParams.sceneIds.size = d.size;
    renderTaskParams.sceneIds.viewport = renderViewport;
    renderTaskParams.sceneIds.viewMatrix = viewMatrix;
    renderTaskParams.sceneIds.projectionMatrix = projectionMatrix;

    const GfRange1d nearFar = frustum.GetNearFar();
    renderTaskParams.nearClip = static_cast<float>(nearFar.GetMin());
    renderTaskParams.farClip = static_cast<float>(nearFar.GetMax());
    renderTaskParams.orthographic = d.camera.GetProjection() == GfCamera::Orthographic;

    if (renderTaskParams.ambientOcclusion.enabled || renderTaskParams.selectionOutline.enabled
        || renderTaskParams.prepareSelection) {
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
            renderTaskParams.prepareSelection = false;
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
            renderTaskParams.prepareSelection = false;
            renderTaskParams.captureVisible = false;
            renderTaskParams.captureObjects = false;
        }
    }

    return renderTaskParams;
}

void
RenderEngine::Private::prepareDisplayModes(ImagingGLEngine& target, const UsdStageRefPtr& stage,
                                           const SdfPathVector& paths, const UsdImagingGLRenderParams& params,
                                           const RenderTaskParams& taskParams,
                                           std::optional<DisplayModeWarmup>* deferred)
{
    if (target.displayModesPrepared(paths, params))
        return;
    if (deferred)
        deferred->emplace(DisplayModeWarmup { stage, paths, params, taskParams });
    else
        target.warmDisplayModes(stage, paths, params, taskParams);
}

void
RenderEngine::Private::runDisplayModeWarmup(ImagingGLEngine& target, const DisplayModeWarmup& warmup)
{
    if (Hgi* hgi = target.hgi()) {
        hgi->StartFrame();
        target.warmDisplayModes(warmup.stage, warmup.paths, warmup.params, warmup.taskParams);
        hgi->EndFrame();
    }
}

void
RenderEngine::Private::warmDocument(const UsdStageRefPtr& renderStage, const SdfPathVector& renderPaths,
                                    const UsdImagingGLRenderParams& documentParams,
                                    const RenderTaskParams& renderTaskParams, bool deferDisplayModeWarmup)
{
    if (renderTaskParams.prepareSelection
        && (documentParams.drawMode == UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH
            || documentParams.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE)) {
        // Population changes do not introduce new source geometry. Reuse the
        // full document's shared GPU preparation when its topology, shader
        // dependencies and render settings have not changed while inactive.
        if (d.isolation.active() && d.cachedEngine && d.cachedImagingStage == d.stage
            && d.documentWarmupSourceVersion == d.isolation.sourceVersion()
            && d.cachedEngine->displayModesPrepared(renderPaths, documentParams))
            d.engine->markDisplayModesPrepared(renderPaths, documentParams);
        else
            prepareDisplayModes(*d.engine, renderStage, renderPaths, documentParams, renderTaskParams,
                                deferDisplayModeWarmup ? &d.deferredDocumentWarmup : nullptr);
        if (!d.isolation.active() && !d.deferredDocumentWarmup)
            d.documentWarmupSourceVersion = d.isolation.sourceVersion();
    }
}

bool
RenderEngine::Private::warmGizmo(const UsdImagingGLRenderParams& gizmoParams, const GfMatrix4d& gizmoTransform)
{
    const SdfPath gizmoPath = paths::auxiliary::transform;
    auto* gizmoTarget = d.gizmoEngine.get();
    Hgi* gizmoHgi = gizmoTarget->hgi();
    const UsdPrim auxiliaryRoot = d.auxiliary->GetPseudoRoot();
    const bool warmPresentation = !d.gizmoWarmed && d.contextMode == ContextMode::Current;
    if (warmPresentation) {
        PerfScope warmupScope("gizmo.renderer.warmup");
        // Prepare every mode in the dedicated engine without presenting
        // its color or depth. Visibility changes touch only the private
        // auxiliary stage and are restored before the visible pass.
        std::vector<std::pair<UsdAttribute, VtValue>> visibility;
        for (const SdfPath& path :
             { gizmoPath, gizmoPath.AppendChild(TfToken("Move")), gizmoPath.AppendChild(TfToken("Rotate")),
               gizmoPath.AppendChild(TfToken("Scale")) }) {
            UsdGeomImageable imageable(d.auxiliary->GetPrimAtPath(path));
            if (!imageable)
                continue;
            UsdAttribute attr = imageable.GetVisibilityAttr();
            VtValue value;
            if (attr.HasAuthoredValueOpinion())
                attr.Get(&value);
            visibility.emplace_back(attr, value);
            attr.Set(UsdGeomTokens->inherited);
        }
        const auto restoreVisibility = qScopeGuard([&]() {
            for (const auto& item : visibility) {
                if (item.second.IsEmpty())
                    item.first.Clear();
                else
                    item.first.Set(item.second);
            }
        });
        GfMatrix4d warmView(1.0);
        warmView.SetLookAt(GfVec3d(0, 0, 5), GfVec3d(0), GfVec3d::YAxis());
        GfFrustum warmFrustum;
        warmFrustum.SetPerspective(45, 1, .01, 100);
        gizmoTarget->SetCameraState(gizmoTransform.GetInverse() * warmView, warmFrustum.ComputeProjectionMatrix());
        gizmoHgi->StartFrame();
        gizmoTarget->PrepareBatch(auxiliaryRoot, gizmoParams);
        gizmoTarget->renderBatchWithRenderTask({ gizmoPath }, gizmoParams, {}, false);
        gizmoHgi->EndFrame();
        d.gizmoWarmed = true;
    }

    return warmPresentation;
}

void
RenderEngine::Private::collectCaptures(const RenderTaskParams& renderTaskParams)
{
    if (d.captureObjectsRequested)
        d.capturedObjects = d.engine->takeCapturedObjects();

    if (d.captureVisibleRequested) {
        d.capturedVisiblePaths = renderTaskParams.captureVisible ? d.engine->takeCapturedVisiblePaths()
                                                                 : SdfPathVector {};
        d.captureVisibleRequested = false;
    }

    if (d.captureGpuPickRequested) {
        d.capturedGpuPick = renderTaskParams.captureGpuPick ? d.engine->takeGpuPickResult() : GpuPickResult {};
        d.captureGpuPickRequested = false;
    }

    if (d.captureElementIdRequested) {
        d.capturedElementId = renderTaskParams.captureElementId ? d.engine->takeCapturedElementId() : -1;
        d.captureElementIdRequested = false;
    }
}

bool
RenderEngine::Private::renderOverlays(const UsdImagingGLRenderParams& documentParams, const GfMatrix4d& viewMatrix,
                                      bool deferDisplayModeWarmup)
{
    // Render viewport support geometry after Look processing. The dedicated
    // pass keeps /Display out of document AO/depth while preserving Stageviz
    // grid and helper presentation in the final viewport.
    if (!d.captureObjectsRequested && d.auxiliaryEngine && d.auxiliary
        && d.auxiliary->GetPrimAtPath(paths::auxiliary::display)) {
        Hgi* auxiliaryHgi = d.auxiliaryEngine->hgi();
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

        const UsdPrim auxiliaryRoot = d.auxiliary->GetPseudoRoot();
        auxiliaryHgi->StartFrame();
        d.auxiliaryEngine->PrepareBatch(auxiliaryRoot, auxiliaryParams);
        updateAuxiliaryRenderSceneIndex();
        const bool prepareAuxiliaryModes = d.settings.aov == HdAovTokens->color
                                           && (auxiliaryParams.drawMode == UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH
                                               || auxiliaryParams.drawMode
                                                      == UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE);
        if (prepareAuxiliaryModes && !deferDisplayModeWarmup)
            prepareDisplayModes(*d.auxiliaryEngine, d.auxiliary, { paths::auxiliary::display }, auxiliaryParams, {},
                                nullptr);
        const SdfPath gizmoPath = paths::auxiliary::transform;
        // Draw the gizmo only in its dedicated smooth-shaded, depth-cleared
        // pass. The broader /Display collection otherwise draws it twice.
        d.auxiliaryEngine->renderBatch({ paths::auxiliary::display }, auxiliaryParams, { gizmoPath });
        if (prepareAuxiliaryModes && deferDisplayModeWarmup)
            prepareDisplayModes(*d.auxiliaryEngine, d.auxiliary, { paths::auxiliary::display }, auxiliaryParams, {},
                                &d.deferredAuxiliaryWarmup);

        if (d.gizmoEngine && d.auxiliary->GetPrimAtPath(gizmoPath)) {
            PerfScope gizmoScope("gizmo.render_cpu");
            auto* gizmoTarget = d.gizmoEngine.get();
            Hgi* gizmoHgi = gizmoTarget->hgi();
            // Gizmo shading must not inherit the document's wireframe mode.
            UsdImagingGLRenderParams gizmoParams = auxiliaryParams;
            gizmoParams.drawMode = UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
            gizmoParams.clipPlanes.clear();
            GfMatrix4d gizmoTransform(1.0);
            bool resetGizmoStack = false;
            UsdGeomXformable(d.auxiliary->GetPrimAtPath(gizmoPath))
                .GetLocalTransformation(&gizmoTransform, &resetGizmoStack);
            const auto gizmoCamera = gizmoprojection::camera(d.camera, gizmoTransform);
            const bool warmPresentation = warmGizmo(gizmoParams, gizmoTransform);
            // Initialize the presentation pipeline once too. With the restored
            // invisible root this presents an empty transparent overlay, never
            // the warmup meshes. Later hidden frames skip the pass entirely.
            if (warmPresentation
                || UsdGeomImageable(d.auxiliary->GetPrimAtPath(gizmoPath)).ComputeVisibility()
                       != UsdGeomTokens->invisible) {
                gizmoTarget->SetCameraState(viewMatrix, gizmoCamera.GetFrustum().ComputeProjectionMatrix());
                gizmoHgi->StartFrame();
                gizmoTarget->PrepareBatch(auxiliaryRoot, gizmoParams);
                updateAuxiliaryRenderSceneIndex();
                // This pass intentionally clears depth but preserves the color
                // attachment. OpenGL state operations require a current GL context.
                glDepthMask(GL_TRUE);
                glClear(GL_DEPTH_BUFFER_BIT);
                gizmoTarget->renderBatch({ gizmoPath }, gizmoParams);
                d.gizmoRendered = true;
                gizmoHgi->EndFrame();
            }
        }
        auxiliaryHgi->EndFrame();
    }

    return true;
}

bool
RenderEngine::Private::render(bool deferDisplayModeWarmup)
{
    PerfScope perfScope("hydra.render_cpu");
    d.deferredDocumentWarmup.reset();
    d.deferredAuxiliaryWarmup.reset();
    if (!d.stage || d.size[0] <= 0 || d.size[1] <= 0)
        return false;

    const UsdStageRefPtr renderStage = documentStage();
    if (!renderStage)
        return false;
    if ((!d.engine || (d.contextMode == ContextMode::Current && (!d.auxiliaryEngine || !d.gizmoEngine)))
        && !initialize())
        return false;

    d.gizmoRendered = false;
    ensureAuxiliarySceneIndex();
    refreshAuxiliarySceneIndex();
    updateRenderParams();
    updateLighting();

    GfVec4d renderViewport = d.viewport;
    if (renderViewport[2] <= 0.0 || renderViewport[3] <= 0.0)
        renderViewport = GfVec4d(0, 0, d.size[0], d.size[1]);

    const GfFrustum frustum = d.camera.GetFrustum();
    const GfMatrix4d viewMatrix = frustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = frustum.ComputeProjectionMatrix();

    configureEngine(d.engine.get(), renderViewport);
    if (d.auxiliaryEngine)
        configureEngine(d.auxiliaryEngine.get(), renderViewport);
    if (d.gizmoEngine)
        configureEngine(d.gizmoEngine.get(), renderViewport);

    const UsdPrim root = renderStage->GetPseudoRoot();
    UsdImagingGLRenderParams documentParams = d.params;
    documentParams.highlight = false;
    Hgi* documentHgi = d.engine->hgi();
    if (!documentHgi)
        return false;

    documentHgi->StartFrame();
    d.engine->PrepareBatch(root, documentParams);
    updateDocumentRenderSceneIndex();
    refreshMaterials();

    resolveSelection();

    // Isolation is represented by the rendering stage's population for every
    // prim type. Both beauty and scene-ID passes render that complete input.
    const SdfPathVector renderPaths { root.GetPath() };
    const RenderTaskParams renderTaskParams = makeRenderTaskParams(renderPaths, renderViewport);
    if (!deferDisplayModeWarmup)
        warmDocument(renderStage, renderPaths, documentParams, renderTaskParams, false);
    d.engine->renderBatchWithRenderTask(renderPaths, documentParams, renderTaskParams);
    if (deferDisplayModeWarmup)
        warmDocument(renderStage, renderPaths, documentParams, renderTaskParams, true);

    collectCaptures(renderTaskParams);

    documentHgi->EndFrame();

    d.deferredWarmupSourceVersion = d.isolation.sourceVersion();
    return renderOverlays(documentParams, viewMatrix, deferDisplayModeWarmup);
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
    if (p->d.contextMode == ContextMode::Offscreen) {
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
    return p->d.engine != nullptr
           && (p->d.contextMode == ContextMode::Offscreen
               || (p->d.auxiliaryEngine != nullptr && p->d.gizmoEngine != nullptr));
}

void
RenderEngine::setStage(UsdStageRefPtr stage)
{
    if (p->d.stage == stage)
        return;

    p->d.stage = stage;
    p->d.pendingMaterialChanges.clear();
    p->d.isolation.setStage(stage);
    p->d.selectionCacheValid = false;
    // Grid and transform handles belong to the unchanged auxiliary stage.
    // Retain their GPU resources and warmup across document replacements.
    p->resetEngine(false);
}

UsdStageRefPtr
RenderEngine::stage() const
{
    return p->d.stage;
}

void
RenderEngine::setAuxiliaryStage(UsdStageRefPtr stage)
{
    if (p->d.auxiliary == stage)
        return;

    p->d.gizmoWarmed = false;
    p->d.auxiliary = stage;
    p->resetEngine();
}

UsdStageRefPtr
RenderEngine::auxiliaryStage() const
{
    return p->d.auxiliary;
}

void
RenderEngine::refreshAuxiliaryStage()
{
    p->refreshAuxiliarySceneIndex();
}

void
RenderEngine::setCamera(const GfCamera& camera)
{
    p->d.camera = camera;
}

const GfCamera&
RenderEngine::camera() const
{
    return p->d.camera;
}

void
RenderEngine::setSize(const GfVec2i& size)
{
    p->d.size = size;
}

void
RenderEngine::setDevicePixelRatio(double ratio)
{
    if (std::isfinite(ratio) && ratio > 0.0)
        p->d.devicePixelRatio = ratio;
}

const GfVec2i&
RenderEngine::size() const
{
    return p->d.size;
}

void
RenderEngine::setViewport(const GfVec4d& viewport)
{
    p->d.viewport = viewport;
}

const GfVec4d&
RenderEngine::viewport() const
{
    return p->d.viewport;
}

void
RenderEngine::setSettings(const Settings& settings)
{
    p->d.settings = settings;
}

const RenderEngine::Settings&
RenderEngine::settings() const
{
    return p->d.settings;
}

void
RenderEngine::setMask(const QList<SdfPath>& paths)
{
    p->d.mask = paths;
    p->d.isolation.setMask(paths);
}

void
RenderEngine::setSelected(const QList<SdfPath>& paths)
{
    if (paths == p->d.selected)
        return;

    std::set<SdfPath> seen;
    QList<SdfPath> normalized;
    normalized.reserve(paths.size());
    for (const SdfPath& path : paths) {
        const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (primPath.IsEmpty() || primPath == SdfPath::AbsoluteRootPath())
            continue;
        if (seen.insert(primPath).second)
            normalized.append(primPath);
    }
    if (normalized == p->d.selected)
        return;
    p->d.selected = std::move(normalized);
    p->d.selectionCacheValid = false;
}

void
RenderEngine::updatePrims(const NoticeBatch& batch)
{
    p->d.selectionCacheValid = false;
    for (const auto& entry : batch.entries) {
        if (!entry.changedInfoOnly || entry.path.GetNameToken() == TfToken("info:id")
            || std::find(entry.changedFields.begin(), entry.changedFields.end(), TfToken("connectionPaths"))
                   != entry.changedFields.end())
            p->d.pendingMaterialChanges.insert(entry.path.GetPrimPath());
    }
}

void
RenderEngine::invalidateSelectionCache()
{
    p->d.selectionCacheValid = false;
}

void
RenderEngine::setInteractiveNavigation(bool enabled)
{
    p->d.interactiveNavigation = enabled;
}

void
RenderEngine::setSelectionColor(const QColor& color)
{
    p->d.selectionColor = color;
}

void
RenderEngine::setSecondarySelectionColor(const QColor& color)
{
    p->d.secondarySelectionColor = color;
}

bool
RenderEngine::renderToCurrentFramebuffer(bool deferDisplayModeWarmup)
{
    if (!QOpenGLContext::currentContext())
        return false;

    if (!p->d.engine && !p->initialize())
        return false;

    glClearColor(p->d.settings.clearColor.redF(), p->d.settings.clearColor.greenF(), p->d.settings.clearColor.blueF(),
                 p->d.settings.clearColor.alphaF());

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_FRAMEBUFFER_SRGB);

#ifdef WIN32
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    GfVec4d viewport = p->d.viewport;
    if (viewport[2] <= 0.0 || viewport[3] <= 0.0)
        viewport = GfVec4d(0, 0, p->d.size[0], p->d.size[1]);

    glViewport(static_cast<GLint>(viewport[0]), static_cast<GLint>(viewport[1]), static_cast<GLsizei>(viewport[2]),
               static_cast<GLsizei>(viewport[3]));
#endif  // WIN32

    return p->render(deferDisplayModeWarmup);
}

bool
RenderEngine::hasDeferredDisplayModeWarmup() const
{
    return p->d.deferredDocumentWarmup.has_value() || p->d.deferredAuxiliaryWarmup.has_value();
}

void
RenderEngine::Private::warmDeferredDisplayModes()
{
    auto document = std::exchange(d.deferredDocumentWarmup, std::nullopt);
    auto auxiliary = std::exchange(d.deferredAuxiliaryWarmup, std::nullopt);
    if (!QOpenGLContext::currentContext() || d.deferredWarmupSourceVersion != d.isolation.sourceVersion())
        return;

    PerfScope scope("viewport.display_modes.deferred");
    if (document && d.engine && document->stage == d.imagingStage) {
        runDisplayModeWarmup(*d.engine, *document);
        if (!d.isolation.active() && d.engine->displayModesPrepared(document->paths, document->params))
            d.documentWarmupSourceVersion = d.deferredWarmupSourceVersion;
    }
    if (auxiliary && d.auxiliaryEngine && auxiliary->stage == d.auxiliary)
        runDisplayModeWarmup(*d.auxiliaryEngine, *auxiliary);
}

void
RenderEngine::warmDeferredDisplayModes()
{
    p->warmDeferredDisplayModes();
}

bool
RenderEngine::isConverged() const
{
    return (!p->d.engine || p->d.engine->IsConverged())
           && (!p->d.auxiliaryEngine || p->d.auxiliaryEngine->IsConverged())
           && (!p->d.gizmoEngine || !p->d.gizmoRendered || p->d.gizmoEngine->IsConverged());
}

QJsonObject
RenderEngine::inspectPrim(const SdfPath& path, QString& error)
{
    if (!p->d.stage || !p->d.engine || !p->d.engine->renderIndex()) {
        error = QStringLiteral("No synchronized viewport renderer is available");
        return {};
    }
    const auto prim = p->d.stage->GetPrimAtPath(path);
    if (!prim || !UsdGeomGprim(prim)) {
        error = QStringLiteral("inspectPrim requires an existing USD geometry prim path");
        return {};
    }
    SdfPathVector mapped;
    std::vector<HydraSelectionRprim> instances;
    p->d.engine->resolveSelectionRprims(p->d.stage, { path }, &mapped, &instances);
    for (const auto& instance : instances) {
        if (std::find(mapped.begin(), mapped.end(), instance.path) == mapped.end())
            mapped.push_back(instance.path);
    }
    if (mapped.empty())
        mapped.push_back(path);
    auto result = inspectRenderPrim(p->d.stage, p->d.engine->renderIndex(),
                                    p->d.engine->sceneIndices().selection.sceneIndex(), path, mapped, p->d.camera,
                                    p->d.settings.timeCode, p->d.engine->renderCollection());
    QJsonArray mask;
    bool included = p->d.mask.isEmpty();
    for (const auto& root : p->d.mask) {
        mask.append(QString::fromStdString(root.GetString()));
        included = included || path.HasPrefix(root);
    }
    result.insert("mask", mask);
    result.insert("includedByMask", included);
    result.insert("drawMode", int(p->d.settings.drawMode));
    result.insert("aov", QString::fromStdString(p->d.settings.aov.GetString()));
    result.insert("doubleSidedMode", int(p->d.settings.doubleSidedMode));
    return result;
}

ViewportCapture
RenderEngine::captureViewport(QString& error)
{
    ViewportCapture result;
    if (p->d.contextMode != ContextMode::Current || !QOpenGLContext::currentContext() || !p->d.stage) {
        error = QStringLiteral("An active viewport GL context and USD stage are required");
        return result;
    }
    if (p->d.settings.aov != HdAovTokens->color) {
        error = QStringLiteral("Viewport color capture requires the color AOV");
        return result;
    }
    if (p->d.settings.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME
        || p->d.settings.drawMode == UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE
        || p->d.settings.drawMode == UsdImagingGLDrawMode::DRAW_POINTS) {
        error = QStringLiteral("Viewport color capture requires a filled surface display mode");
        return result;
    }
    auto* functions = QOpenGLContext::currentContext()->functions();
    GLint drawFramebuffer = 0, readFramebuffer = 0;
    functions->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    functions->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    auto restore = qScopeGuard([&]() {
        p->d.captureObjectsRequested = false;
        functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFramebuffer));
        functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFramebuffer));
    });
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(0);
    QOpenGLFramebufferObject framebuffer(p->d.size[0], p->d.size[1], format);
    if (!framebuffer.isValid() || !framebuffer.bind()) {
        error = QStringLiteral("Could not allocate viewport capture framebuffer");
        return result;
    }
    p->d.captureObjectsRequested = true;
    p->d.capturedObjects = {};
    if (!renderToCurrentFramebuffer()) {
        error = QStringLiteral("Viewport capture rendering failed");
        return result;
    }
    result.objects = std::move(p->d.capturedObjects);
    result.image = framebuffer.toImage().convertToFormat(QImage::Format_ARGB32);
    if (result.image.isNull() || result.objects.size != result.image.size()) {
        error = QStringLiteral("Viewport object-ID capture is unavailable for this renderer");
        return {};
    }
    for (const auto& path : result.objects.paths) {
        const auto prim = p->d.stage->GetPrimAtPath(path);
        if (!prim || !prim.IsA<UsdGeomGprim>()) {
            error = QStringLiteral("Could not resolve a captured object to USD geometry: %1")
                        .arg(QString::fromStdString(path.GetString()));
            return {};
        }
    }
    return result;
}

QList<SdfPath>
RenderEngine::captureVisiblePaths(int gridSize)
{
    QList<SdfPath> result;

    if (p->d.contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return result;

    p->d.captureVisibleRequested = true;
    p->d.captureVisibleGridSize = std::clamp(gridSize, 1, 8);
    p->d.capturedVisiblePaths.clear();

    if (!renderToCurrentFramebuffer()) {
        p->d.captureVisibleRequested = false;
        return result;
    }

    result.reserve(static_cast<qsizetype>(p->d.capturedVisiblePaths.size()));
    for (const SdfPath& path : p->d.capturedVisiblePaths)
        result.append(path);

    p->d.capturedVisiblePaths.clear();
    return result;
}

int
RenderEngine::captureElementIdAt(const GfVec2i& pixel)
{
    if (p->d.contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return -1;

    p->d.captureElementIdRequested = true;
    p->d.captureElementPixel = pixel;
    p->d.capturedElementId = -1;

    if (!renderToCurrentFramebuffer()) {
        p->d.captureElementIdRequested = false;
        return -1;
    }

    const int result = static_cast<int>(p->d.capturedElementId);
    p->d.capturedElementId = -1;
    return result;
}

GpuPickResult
RenderEngine::gpuPickAt(const GfVec2i& pixel, const QList<SdfPath>& excludedPaths)
{
    if (p->d.contextMode != ContextMode::Current || !QOpenGLContext::currentContext())
        return {};

    p->d.captureGpuPickRequested = true;
    p->d.gpuPickPixel = pixel;
    p->d.gpuPickExcludes.assign(excludedPaths.begin(), excludedPaths.end());
    p->d.capturedGpuPick = GpuPickResult {};

    if (!renderToCurrentFramebuffer()) {
        p->d.captureGpuPickRequested = false;
        return {};
    }
    return p->d.capturedGpuPick;
}

SdfPath
RenderEngine::pickNextIdAt(const GfVec2i& pixel, const QList<SdfPath>& excludedPaths)
{
    return gpuPickAt(pixel, excludedPaths).path;
}

QImage
RenderEngine::renderImage()
{
    return renderImageInternal(false);
}

QImage
RenderEngine::renderLinearImage()
{
    return renderImageInternal(true);
}

QImage
RenderEngine::renderImageInternal(bool linear)
{
    if (p->d.contextMode != ContextMode::Offscreen)
        return {};

    OpenGLContextRestore contextRestore;

    if (!p->ensureCurrentContext())
        return {};

    if (!p->d.engine && !p->initialize())
        return {};

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(linear ? 0 : 4);
    if (linear)
        format.setInternalTextureFormat(GL_RGBA32F);

    QOpenGLFramebufferObject framebuffer(p->d.size[0], p->d.size[1], format);

    if (!framebuffer.isValid())
        return {};

    if (!framebuffer.bind())
        return {};

    const bool previousCapture = p->d.linearColorCapture;
    p->d.linearColorCapture = linear;
    auto restoreCapture = qScopeGuard([&]() { p->d.linearColorCapture = previousCapture; });
    const bool rendered = renderToCurrentFramebuffer();

    QImage image;
    if (rendered && linear && framebuffer.bind()) {
        image = QImage(p->d.size[0], p->d.size[1], QImage::Format_RGBA32FPx4_Premultiplied);
        if (!image.isNull()) {
            auto* functions = QOpenGLContext::currentContext()->functions();
            functions->glPixelStorei(GL_PACK_ALIGNMENT, 4);
            functions->glReadPixels(0, 0, p->d.size[0], p->d.size[1], GL_RGBA, GL_FLOAT, image.bits());
            image = image.flipped(Qt::Vertical);
        }
    }
    else if (rendered && !linear) {
        image = framebuffer.toImage();
    }

    framebuffer.release();
    return image;
}

bool
RenderEngine::testIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfMatrix4d& viewMatrix,
                               const GfMatrix4d& projectionMatrix, const UsdPrim& root,
                               UsdImagingGLEngine::IntersectionResultVector* results)
{
    if (!results)
        return false;

    OpenGLContextRestore contextRestore;

    const UsdStageRefPtr documentStage = p->documentStage();
    if (!documentStage || !root)
        return false;
    if (!p->d.engine && !p->initialize())
        return false;

    p->updateRenderParams();

    UsdImagingGLEngine::IntersectionResultVector rawResults;

    const UsdPrim pickRoot = p->d.isolation.active() ? documentStage->GetPseudoRoot()
                                                     : documentStage->GetPrimAtPath(root.GetPath());
    if (!p->d.engine->TestIntersection(pickParams, viewMatrix, projectionMatrix, pickRoot, p->d.params, &rawResults)) {
        results->clear();
        return false;
    }

    results->clear();
    results->reserve(rawResults.size());

    for (const auto& result : rawResults) {
        if (result.hitPrimPath.IsEmpty())
            continue;

        if (p->d.stage && !p->d.stage->GetPrimAtPath(result.hitPrimPath))
            continue;
        if (!result.hitPrimPath.HasPrefix(root.GetPath()))
            continue;
        if (!p->d.mask.isEmpty() && std::none_of(p->d.mask.begin(), p->d.mask.end(), [&](const SdfPath& path) {
                return result.hitPrimPath.HasPrefix(path);
            }))
            continue;

        results->push_back(result);
    }

    return !results->empty();
}

QList<QString>
RenderEngine::rendererAovs() const
{
    QList<QString> result;

    if (!p->d.engine)
        return result;

    for (const TfToken& token : p->d.engine->GetRendererAovs())
        result.append(QString::fromStdString(token.GetString()));

    return result;
}

VtDictionary
RenderEngine::renderStats() const
{
    return p->d.engine ? p->d.engine->GetRenderStats() : VtDictionary();
}

QString
RenderEngine::hgiApiName() const
{
    if (!p->d.engine)
        return {};

    Hgi* hgi = p->d.engine->hgi();
    return hgi ? QString::fromStdString(hgi->GetAPIName().GetString()) : QString();
}

bool
RenderEngine::isColorCorrectionCapable() const
{
    return p->d.engine && p->d.engine->IsColorCorrectionCapable();
}

}  // namespace stageviz
