// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "imagingglwidget.h"
#include "application.h"
#include "command.h"
#include "materialoverridesceneindex.h"
#include "notice.h"
#include "os.h"
#include "qtutils.h"
#include "signalguard.h"
#include "style.h"
#include "tracelocks.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewstate.h"
#include <QApplication>
#include <QColor>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLocale>
#include <QMouseEvent>
#include <QObject>
#include <QOpenGLContext>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointer>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/error.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/glf/diagnostic.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
#include <pxr/usdImaging/usdImagingGL/engine.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ImagingGLEngine final : public UsdImagingGLEngine {
public:
    explicit ImagingGLEngine(const UsdImagingGLEngine::Parameters& params)
        : UsdImagingGLEngine(prepareParameters(params))
    {}
    TfRefPtr<MaterialOverrideSceneIndex> materialOverrideSceneIndex() const
    {
        return materialOverrideSceneIndexInstance();
    }
    HdMergingSceneIndexRefPtr mergingSceneIndex() const { return mergingSceneIndexInstance(); }

private:
    static HdMergingSceneIndexRefPtr& mergingSceneIndexInstance()
    {
        static HdMergingSceneIndexRefPtr instance;
        return instance;
    }
    static TfRefPtr<MaterialOverrideSceneIndex>& materialOverrideSceneIndexInstance()
    {
        static TfRefPtr<MaterialOverrideSceneIndex> instance;
        return instance;
    }
    static void registerStagevizSceneIndexFilters()
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
            TfRefPtr<MaterialOverrideSceneIndex> sceneIndex = MaterialOverrideSceneIndex::New(mergingSceneIndex);
            mergingSceneIndexInstance() = mergingSceneIndex;
            materialOverrideSceneIndexInstance() = sceneIndex;
            return sceneIndex;
        };
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            std::string(), callback, nullptr, 0, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
        registered = true;
    }
    static UsdImagingGLEngine::Parameters prepareParameters(UsdImagingGLEngine::Parameters params)
    {
        registerStagevizSceneIndexFilters();
        return params;
    }
};

class ImagingGLWidgetPrivate : public QObject, public SignalGuard {
public:
    void init();
    void initGL();
    void initContext();
    SelectionList* selectionList();
    ViewCamera* viewCamera();
    ViewState* viewState();
    void close();
    void paintGL();
    void paintEvent(QPaintEvent* event);
    void focusEvent(QMouseEvent* event);
    void mouseDoubleClickEvent(QMouseEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void sweepEvent(const QRect& rect, QMouseEvent* event);
    void wheelEvent(QWheelEvent* event);
    void updateStage(UsdStageRefPtr stage);
    void updateAuxiliary(UsdStageRefPtr auxiliary);
    void updateStageUp(const TfToken& upAxis);
    void updateBoundingBox(const GfBBox3d& bbox);
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    void updateTransform(bool enabled);
    void captureVisible();
    void clearVisibleCapture();
public Q_SLOTS:
    void updateCamera(const GfCamera& camera);
    void updateSelection(const QList<SdfPath>& paths);

public:
    void rebuildSelectionBBoxes();
    void ensureAuxiliarySceneIndex();
    void refreshAuxiliarySceneIndex();
    void updateAuxiliaryGrid();
    void ensureAuxiliaryMaterials();
    void authorAuxiliaryGridMaterial(const SdfPath& materialPath, const GfVec3f& color);
    void authorAuxiliaryStandardSurface(const SdfPath& materialPath, const GfVec3f& baseColor, float metalness,
                                        float roughness, float specular);
    void ensureMaterialOverrideSceneIndex();
    void updateMaterialOverrideSceneIndex();
    bool projectWorldToScreen(const GfVec3d& world, QPointF& screen);
    QPointF transformAxisDirection(int axis);
    GfVec3d transformAxisVector(int axis);
    double transformWorldPerPixel();
    double transformWorldPerPixel(const GfVec3d& pivot);
    bool transformRotationPoint(int axis, double angle, QPointF& screen);
    bool transformRotationAngle(int axis, const QPointF& pos, double& angle, double* distance = nullptr);
    int hitTestTransform(const QPointF& pos);
    bool beginTransformDrag(const QPointF& pos);
    void updateTransformDrag(const QPointF& pos);
    void endTransformDrag();
    void updateTransformHover(const QPointF& pos);
    void drawTransformTransform(QPainter& painter);
    QPoint deviceRatio(QPoint value) const;
    double deviceRatio(double value) const;
    double widgetAspectRatio() const;
    GfVec2i widgetSize() const;
    GfVec4d widgetViewport() const;
    void drawBorder(QPainter& painter);
    void updateAxis();
    void updateSceneStats();
    void updatePerformanceStats();
    bool isPathMaskedIn(const SdfPath& path) const;
    bool pickMaskedIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfFrustum& pickFrustum,
                                UsdImagingGLEngine::IntersectionResultVector* results);
    struct Data {
        size_t count;
        qint64 frame;
        float defaultAmbient;
        float defaultSpecular;
        float defaultShininess;
        double gpuPerformanceMs;
        bool auxiliarySceneIndexInserted;
        bool drag;
        bool sweep;
        bool transformEnabled;
        bool transformDragging;
        int transformHoverAxis;
        int transformActiveAxis;
        QPointF transformStart;
        double transformRotationStartAngle;
        GfVec3d transformPivot;
        GfVec3d transformStartPivot;
        QList<SdfPath> transformPaths;
        QList<GfMatrix4d> transformBefore;
        QList<GfMatrix4d> transformAfter;
        QPoint start;
        QPoint end;
        QPoint mousepos;
        QImage sceneStats;
        QImage performanceStats;
        QImage axis;
        GfBBox3d selectionBBox;
        UsdStageRefPtr stage;
        UsdStageRefPtr auxiliary;
        TfToken stageUpAxis;
        UsdImagingGLRenderParams params;
        GfBBox3d bbox;
        QList<SdfPath> mask;
        QList<SdfPath> selection;
        QList<SdfPath> visibleCapture;
        std::vector<GfBBox3d> selectionBBoxes;
        HgiUniquePtr hgi;
        UsdImagingSceneIndices auxiliarySceneIndices;
        TfRefPtr<MaterialOverrideSceneIndex> materialOverrideSceneIndex;
        QScopedPointer<ImagingGLEngine> glEngine;
        QPointer<ViewContext> context;
        QPointer<ImagingGLWidget> glwidget;
    };
    Data d;
};

void
ImagingGLWidgetPrivate::init()
{
    attach(d.glwidget);
    QSurfaceFormat format;
    format.setSamples(4);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setColorSpace(QColorSpace::SRgb);
    d.glwidget->setFormat(format);
    d.count = 0;
    d.frame = 0;
    d.defaultAmbient = 0.4f;
    d.defaultSpecular = 0.5f;
    d.defaultShininess = 32.0f;
    d.gpuPerformanceMs = 0.0;
    d.auxiliarySceneIndexInserted = false;
    d.drag = false;
    d.sweep = false;
    d.transformEnabled = false;
    d.transformDragging = false;
    d.transformHoverAxis = 0;
    d.transformActiveAxis = 0;
    d.transformRotationStartAngle = 0.0;
    d.transformPivot = GfVec3d(0.0);
    d.transformStartPivot = GfVec3d(0.0);
    d.context = nullptr;
    d.stageUpAxis = UsdGeomTokens->y;
}

void
ImagingGLWidgetPrivate::initGL()
{
    if (d.glEngine)
        return;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning() << "could not initialize gl engine, no current OpenGL context";
        return;
    }
    UsdImagingGLEngine::Parameters params {};
    params.displayUnloadedPrimsWithBounds = false;
    params.allowAsynchronousSceneProcessing = true;
    d.glEngine.reset(new ImagingGLEngine(params));
    Hgi* hgi = d.glEngine->GetHgi();
    if (!hgi) {
        qWarning() << "could not initialize gl engine, no hydra driver found.";
        d.glEngine.reset();
        d.sceneStats = QImage();
        d.performanceStats = QImage();
        d.axis = QImage();
        return;
    }
    ensureMaterialOverrideSceneIndex();
    ensureAuxiliarySceneIndex();
}

void
ImagingGLWidgetPrivate::initContext()
{
    connect(selectionList(), &SelectionList::selectionChanged, this, &ImagingGLWidgetPrivate::updateSelection);
    connect(viewCamera(), &ViewCamera::cameraChanged, this, &ImagingGLWidgetPrivate::updateCamera);
    connect(viewState(), &ViewState::backgroundColorChanged, this, [this](const QColor&) { d.glwidget->update(); });
    connect(viewState(), &ViewState::gridColorChanged, this, [this](const QColor&) {
        updateAuxiliaryGrid();
        refreshAuxiliarySceneIndex();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::gridEnabledChanged, this, [this](bool) {
        updateAuxiliaryGrid();
        refreshAuxiliarySceneIndex();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::materialModeChanged, this, [this](ViewState::MaterialMode) {
        updateMaterialOverrideSceneIndex();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::overrideMaterialChanged, this, [this](const SdfPath&) {
        refreshAuxiliarySceneIndex();
        updateMaterialOverrideSceneIndex();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::sceneMaterialsEnabledChanged, this, [this](bool) {
        updateMaterialOverrideSceneIndex();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::doubleSidedModeChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::defaultCameraLightEnabledChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::sceneLightsEnabledChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::renderModeChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::complexityLevelChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::rendererAovChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::sceneStatsEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            updateSceneStats();
        else
            d.sceneStats = QImage();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::performanceStatsEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            updatePerformanceStats();
        else
            d.performanceStats = QImage();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::cameraAxisEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            updateAxis();
        else
            d.axis = QImage();
        d.glwidget->update();
    });
    ensureAuxiliaryMaterials();
    updateMaterialOverrideSceneIndex();
}

SelectionList*
ImagingGLWidgetPrivate::selectionList()
{
    return d.context->selectionList();
}

ViewCamera*
ImagingGLWidgetPrivate::viewCamera()
{
    return d.context->viewState()->camera();
}

ViewState*
ImagingGLWidgetPrivate::viewState()
{
    return d.context ? d.context->viewState() : nullptr;
}

void
ImagingGLWidgetPrivate::close()
{
    d.mask.clear();
    d.selection.clear();
    d.visibleCapture.clear();
    d.selectionBBoxes.clear();
    d.stage = nullptr;
    d.bbox = GfBBox3d();
    d.selectionBBox = GfBBox3d();
    d.drag = false;
    d.sweep = false;
    d.transformDragging = false;
    d.transformHoverAxis = 0;
    d.transformActiveAxis = 0;
    d.transformRotationStartAngle = 0.0;
    d.transformPivot = GfVec3d(0.0);
    d.transformStartPivot = GfVec3d(0.0);
    d.transformPaths.clear();
    d.transformBefore.clear();
    d.transformAfter.clear();
    d.auxiliarySceneIndexInserted = false;
    d.auxiliarySceneIndices = {};
    d.materialOverrideSceneIndex = nullptr;
    d.glEngine.reset();
    d.hgi.reset();
    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }
    if (viewState() && viewState()->performanceStatsEnabled()) {
        updatePerformanceStats();
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::paintGL()
{
    if (!d.glEngine)
        initGL();
    const QColor clearColor = viewState() && viewState()->backgroundColor().isValid() ? viewState()->backgroundColor()
                                                                                      : QColor(Qt::black);
    glClearColor(clearColor.redF(), clearColor.greenF(), clearColor.blueF(), clearColor.alphaF());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (d.stage) {
        if (d.glEngine) {
            QElapsedTimer timer;
            timer.start();
            if (!d.glEngine->IsColorCorrectionCapable()) {
                glEnable(GL_FRAMEBUFFER_SRGB);
            }
            else {
                glDisable(GL_FRAMEBUFFER_SRGB);
            }
            const QString aov = viewState() ? viewState()->rendererAov() : QStringLiteral("color");
            Q_ASSERT("aov is not set and is required" && !aov.isEmpty());
            TfToken aovtoken(QStringToTfToken(aov));
            d.glEngine->SetRendererAov(aovtoken);
            GfVec4d viewport = widgetViewport();
            d.glEngine->SetRenderBufferSize(widgetSize());
            d.glEngine->SetFraming(
                CameraUtilFraming(GfRange2f(GfVec2i(), widgetSize()), GfRect2i(GfVec2i(), widgetSize())));
            d.glEngine->SetWindowPolicy(CameraUtilMatchVertically);
            d.glEngine->SetRenderViewport(viewport);
#ifdef WIN32
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
#endif  // WIN32
            viewCamera()->setAspectRatio(widgetAspectRatio());
            GfCamera camera = viewCamera()->camera();
            GfFrustum frustum = camera.GetFrustum();
            GfMatrix4d viewModel = frustum.ComputeViewMatrix();
            GfMatrix4d projectionMatrix = frustum.ComputeProjectionMatrix();
            d.glEngine->SetCameraState(viewModel, projectionMatrix);
            d.params.clearColor = QColorToGfVec4f(clearColor);
            {
                const ViewState::RenderMode renderMode = viewState() ? viewState()->renderMode() : ViewState::Shaded;
                d.params.drawMode = renderMode == ViewState::Wireframe ? UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE
                                                                       : UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
            }
            {
                double complexity = 1.0;
                const ViewState::ComplexityLevel level = viewState() ? viewState()->complexityLevel() : ViewState::Low;
                switch (level) {
                case ViewState::Low: complexity = 1.0; break;
                case ViewState::Medium: complexity = 1.1; break;
                case ViewState::High: complexity = 1.2; break;
                case ViewState::VeryHigh: complexity = 1.3; break;
                }
                d.params.complexity = complexity;
            }
            {
                const ViewState::DoubleSidedMode mode = viewState() ? viewState()->doubleSidedMode()
                                                                    : ViewState::DoubleSided;
                switch (mode) {
                case ViewState::Primitive:
                    d.params.cullStyle = UsdImagingGLCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED;
                    break;
                case ViewState::SingleSided: d.params.cullStyle = UsdImagingGLCullStyle::CULL_STYLE_BACK; break;
                case ViewState::DoubleSided:
                default: d.params.cullStyle = UsdImagingGLCullStyle::CULL_STYLE_NOTHING; break;
                }
            }
            d.params.enableLighting = true;
            {
                std::vector<GlfSimpleLight> lights;
                if (!viewState() || viewState()->defaultCameraLightEnabled()) {
                    GfCamera lightCamera = viewCamera()->camera();
                    GfMatrix4d viewInverse = lightCamera.GetTransform();
                    GfVec3d camPos = viewInverse.ExtractTranslation();
                    GlfSimpleLight light;
                    light.SetAmbient(GfVec4f(0, 0, 0, 0));
                    light.SetPosition(GfVec4f(camPos[0], camPos[1], camPos[2], 1.0f));
                    light.SetTransform(viewInverse);
                    lights.push_back(light);
                }
                GfVec4f defaultAmbient(d.defaultAmbient, d.defaultAmbient, d.defaultAmbient, 1.0f);
                GlfSimpleMaterial material;
                material.SetAmbient(defaultAmbient);
                material.SetSpecular(GfVec4f(d.defaultSpecular, d.defaultSpecular, d.defaultSpecular, 1.0f));
                material.SetShininess(d.defaultShininess);
                d.glEngine->SetLightingState(lights, material, defaultAmbient);
            }
            d.params.enableSampleAlphaToCoverage = true;
            d.params.enableSceneLights = !viewState() || viewState()->sceneLightsEnabled();
            // material evaluation remains enabled so viewport materials, such as the grid and
            // clay override, always render. MaterialOverrideSceneIndex controls authored USD material bindings.
            d.params.enableSceneMaterials = true;
            d.params.flipFrontFacing = true;
            d.params.showGuides = false;
            d.params.showProxy = true;
            d.params.showRender = true;
            d.glEngine->SetSelectionColor(qt::QColorToGfVec4f(style()->color(Style::ColorRole::SelectionAlt)));
            d.params.highlight = true;
            d.params.bboxes = d.selectionBBoxes;
            d.params.bboxLineColor = qt::QColorToGfVec4f(style()->color(Style::ColorRole::Selection));
            d.params.bboxLineDashSize = 3.0f;
            QElapsedTimer gpuTimer;
            gpuTimer.start();
            TfErrorMark mark;
            {
                READ_LOCKER(locker, d.context->stageLock(), "stageLock");
                if (!d.stage) {
                    qWarning() << "stage is not set, render pass will be skipped";
                }
                else {
                    Hgi* hgi = d.glEngine->GetHgi();
                    if (!hgi) {
                        qWarning() << "gl engine has no Hgi, render pass will be skipped";
                    }
                    else {
                        ensureMaterialOverrideSceneIndex();
                        ensureAuxiliarySceneIndex();
                        hgi->StartFrame();
                        UsdPrim root = d.stage->GetPseudoRoot();
                        if (!d.mask.isEmpty()) {
                            SdfPathVector paths;
                            for (const SdfPath& path : d.mask)
                                paths.push_back(path);
                            d.glEngine->PrepareBatch(root, d.params);
                            d.glEngine->RenderBatch(paths, d.params);
                        }
                        else {
                            d.glEngine->Render(root, d.params);
                        }
                        hgi->EndFrame();
                    }
                }
            }
            if (!mark.IsClean()) {
                qWarning() << "gl engine errors occured during rendering";
            }
            qint64 gpuTimeNSecs = gpuTimer.nsecsElapsed();
            d.gpuPerformanceMs = gpuTimeNSecs / 1e6;
            d.count++;
            Q_EMIT d.glwidget->renderReady(timer.elapsed());
            if (viewState() && viewState()->performanceStatsEnabled()) {
                updatePerformanceStats();
            }
        }
        else {
            qWarning() << "gl engine is not inititialized, render pass will be skipped";
        }
    }
}

void
ImagingGLWidgetPrivate::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!d.glEngine) {
        return;
    }
    QPainter painter(d.glwidget);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (d.sweep) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        QRect rect(d.start, d.end);
        rect = rect.normalized();
        painter.setPen(QPen(QColor(0, 150, 255, 200), 1));
        painter.setBrush(QColor(0, 150, 255, 50));
        painter.drawRect(rect);
        painter.restore();
    }
    drawTransformTransform(painter);
    if (viewState() && viewState()->sceneStatsEnabled()) {
        painter.drawImage(QPoint(0, 0), d.sceneStats);
    }
    if (viewState() && viewState()->performanceStatsEnabled()) {
        QPoint pos(d.glwidget->width() - d.performanceStats.width() / d.performanceStats.devicePixelRatio(), 0);
        painter.drawImage(pos, d.performanceStats);
    }
    if (viewState() && viewState()->cameraAxisEnabled()) {
        const int margin = 8;
        const int axisHeight = qRound(d.axis.height() / d.axis.devicePixelRatio());
        painter.drawImage(QPoint(margin, d.glwidget->height() - margin - axisHeight), d.axis);
    }
    drawBorder(painter);
}

void
ImagingGLWidgetPrivate::focusEvent(QMouseEvent* event)
{
    d.glwidget->makeCurrent();
    if (!d.stage || !d.glEngine)
        return;
#ifdef WIN32
    glDepthMask(GL_TRUE);
#endif
    const qreal deviceRatio = d.glwidget->devicePixelRatioF();
    QPointF mousePosDevice = event->pos() * deviceRatio;
    GfVec4d viewport = widgetViewport();
    GfVec2d pos((mousePosDevice.x() - viewport[0]) / static_cast<double>(viewport[2]),
                (mousePosDevice.y() - viewport[1]) / static_cast<double>(viewport[3]));
    pos[0] = pos[0] * 2.0 - 1.0;
    pos[1] = -1.0 * (pos[1] * 2.0 - 1.0);
    GfVec2d size(1.0 / static_cast<double>(viewport[2]), 1.0 / static_cast<double>(viewport[3]));
    GfCamera camera = viewCamera()->camera();
    GfFrustum frustum = camera.GetFrustum();
    GfFrustum pickFrustum = frustum.ComputeNarrowedFrustum(pos, size);
    const GfMatrix4d viewMatrix = pickFrustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = pickFrustum.ComputeProjectionMatrix();
    const GfVec3d cameraPos = camera.GetTransform().ExtractTranslation();
    GfVec3d bestHitPoint;
    double bestDistance = std::numeric_limits<double>::max();
    bool found = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        if (d.mask.isEmpty()) {
            GfVec3d hitPoint, hitNormal;
            SdfPath hitPrimPath, hitInstancerPath;
            const bool hit = d.glEngine->TestIntersection(viewMatrix, projectionMatrix, d.stage->GetPseudoRoot(),
                                                          d.params, &hitPoint, &hitNormal, &hitPrimPath,
                                                          &hitInstancerPath);
            if (hit && !hitPrimPath.IsEmpty()) {
                viewCamera()->setFocusPoint(hitPoint);
                d.glwidget->update();
            }
            return;
        }
        for (const SdfPath& maskPath : d.mask) {
            UsdPrim root = d.stage->GetPrimAtPath(maskPath);
            if (!root)
                continue;
            GfVec3d hitPoint, hitNormal;
            SdfPath hitPrimPath, hitInstancerPath;
            const bool hit = d.glEngine->TestIntersection(viewMatrix, projectionMatrix, root, d.params, &hitPoint,
                                                          &hitNormal, &hitPrimPath, &hitInstancerPath);
            if (!hit || hitPrimPath.IsEmpty())
                continue;
            if (!isPathMaskedIn(hitPrimPath))
                continue;
            const double distance = (hitPoint - cameraPos).GetLength();
            if (!found || distance < bestDistance) {
                bestDistance = distance;
                bestHitPoint = hitPoint;
                found = true;
            }
        }
    }
    if (found) {
        viewCamera()->setFocusPoint(bestHitPoint);
    }
}

void
ImagingGLWidgetPrivate::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
        focusEvent(event);
    }
}

void
ImagingGLWidgetPrivate::mousePressEvent(QMouseEvent* event)
{
    if (!d.stage)
        return;
    if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
        d.drag = true;
        d.sweep = false;
        d.transformDragging = false;
        if (event->button() == Qt::LeftButton)
            viewCamera()->setCameraMode(ViewCamera::Tumble);
        else if (event->button() == Qt::MiddleButton)
            viewCamera()->setCameraMode(ViewCamera::Truck);
        else if (event->button() == Qt::RightButton)
            viewCamera()->setCameraMode(ViewCamera::Zoom);
    }
    else if (event->button() == Qt::LeftButton && beginTransformDrag(event->position())) {
        d.drag = false;
        d.sweep = false;
    }
    else if (event->button() == Qt::LeftButton) {
        d.drag = false;
        d.sweep = true;
        d.start = event->pos();
        d.end = event->pos();
        d.glwidget->update();
    }
    d.mousepos = event->pos();
}

void
ImagingGLWidgetPrivate::mouseMoveEvent(QMouseEvent* event)
{
    if (!d.stage)
        return;
    const QPoint pos = event->pos();
    if (d.drag) {
        QPoint delta = deviceRatio(pos) - deviceRatio(d.mousepos);
        if (viewCamera()->cameraMode() == ViewCamera::Truck) {
            double height = widgetSize()[1];
            double factor = viewCamera()->mapToFrustumHeight(height);
            viewCamera()->truck(-delta.x() * factor, delta.y() * factor);
        }
        else if (viewCamera()->cameraMode() == ViewCamera::Tumble) {
            viewCamera()->tumble(0.25 * delta.x(), 0.25 * delta.y());
        }
        else if (viewCamera()->cameraMode() == ViewCamera::Zoom) {
            double factor = -.002 * (delta.x() + delta.y());
            viewCamera()->distance(1 + factor);
        }
        d.glwidget->update();
    }
    else if (d.transformDragging) {
        updateTransformDrag(event->position());
    }
    else if (d.sweep) {
        d.end = event->pos();
        d.glwidget->update();
    }
    else {
        updateTransformHover(event->position());
    }
    d.mousepos = event->pos();
}

void
ImagingGLWidgetPrivate::mouseReleaseEvent(QMouseEvent* event)
{
    if (!d.stage)
        return;
    
    if (d.drag) {
        d.drag = false;
        viewCamera()->setCameraMode(ViewCamera::None);
        d.glwidget->update();
    }
    else if (d.transformDragging) {
        updateTransformDrag(event->position());
        endTransformDrag();
    }
    else if (d.sweep) {
        d.end = event->pos();
        QRect rect(d.start, d.end);
        sweepEvent(rect, event);
        d.sweep = false;
        d.glwidget->update();
    }
}

void
ImagingGLWidgetPrivate::sweepEvent(const QRect& rect, QMouseEvent* event)
{
    d.glwidget->makeCurrent();
    if (!d.stage || !d.glEngine)
        return;
    
#ifdef WIN32
    glDepthMask(GL_TRUE);
#endif
    QRect r = rect.normalized();
    QPoint tl = deviceRatio(r.topLeft());
    QPoint br = deviceRatio(r.bottomRight() - QPoint(1, 1));
    r = QRect(tl, br);
    const int minSize = 10;
    const bool isClick = (r.width() < 3 && r.height() < 3);
    if (isClick) {
        int cx = r.center().x();
        int cy = r.center().y();
        int halfW = minSize / 2;
        int halfH = minSize / 2;
        r = QRect(QPoint(cx - halfW, cy - halfH), QPoint(cx + halfW, cy + halfH));
    }
    GfVec4d viewport = widgetViewport();
    GfVec2d center(((r.left() + r.right()) * 0.5 - viewport[0]) / viewport[2],
                   ((r.top() + r.bottom()) * 0.5 - viewport[1]) / viewport[3]);
    center[0] = center[0] * 2.0 - 1.0;
    center[1] = -1.0 * (center[1] * 2.0 - 1.0);
    GfVec2d size(double(r.width()) / viewport[2], double(r.height()) / viewport[3]);
    UsdImagingGLEngine::PickParams pickParams;
    pickParams.resolveMode = isClick ? TfToken("resolveNearestToCenter") : TfToken("resolveDeep");
    GfCamera cam = viewCamera()->camera();
    GfFrustum fr = cam.GetFrustum();
    GfFrustum pickFr = fr.ComputeNarrowedFrustum(center, size);
    UsdImagingGLEngine::IntersectionResultVector results;
    const bool hit = pickMaskedIntersection(pickParams, pickFr, &results);
    QList<SdfPath> selectedPaths;
    if (hit) {
        for (const auto& rItem : results) {
            if (!rItem.hitPrimPath.IsEmpty())
                selectedPaths.append(rItem.hitPrimPath);
        }
    }
    selectedPaths = path::uniquePaths(selectedPaths);
    bool update = false;
    if (!selectedPaths.isEmpty()) {
        if (event->modifiers() & Qt::ShiftModifier) {
            for (const SdfPath& path : selectedPaths) {
                qsizetype idx = d.selection.indexOf(path);
                if (idx >= 0)
                    d.selection.removeAt(idx);
                else
                    d.selection.append(path);
                update = true;
            }
        }
        else {
            if (d.selection != selectedPaths) {
                d.selection = selectedPaths;
                update = true;
            }
        }
    }
    else {
        if (!d.selection.isEmpty()) {
            d.selection.clear();
            update = true;
        }
    }
    if (update) {
        d.context->run(new Command(selectPaths(d.selection)));
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::wheelEvent(QWheelEvent* event)
{
    double delta = static_cast<double>(event->angleDelta().y()) / 1000.0;
    double clamped = std::max(-0.5, std::min(0.5, delta));
    double factor = 1.0 - clamped;
    viewCamera()->distance(factor);
}

void
ImagingGLWidgetPrivate::updateStage(UsdStageRefPtr stage)
{
    SignalGuard::Scope guard(this);
    d.stage = stage;
    d.visibleCapture.clear();
    d.selectionBBoxes.clear();
    d.auxiliarySceneIndexInserted = false;
    d.auxiliarySceneIndices = {};
    d.materialOverrideSceneIndex = nullptr;
    d.glEngine.reset();
    d.hgi.reset();
    rebuildSelectionBBoxes();
    ensureAuxiliaryMaterials();
    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }
    updateAuxiliaryGrid();
    d.glwidget->update();
    updateAxis();
}

void
ImagingGLWidgetPrivate::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    SignalGuard::Scope guard(this);
    if (auxiliary == d.auxiliary)
        return;
    d.auxiliary = auxiliary;
    ensureAuxiliaryMaterials();
    updateAuxiliaryGrid();
    d.auxiliarySceneIndices = {};
    d.auxiliarySceneIndexInserted = false;
    // recreate the render engine so the render index no longer retains an
    // auxiliary scene index backed by the previous stage.
    d.materialOverrideSceneIndex = nullptr;
    d.glEngine.reset();
    d.hgi.reset();
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateStageUp(const TfToken& upAxis)
{
    const TfToken normalizedAxis = (upAxis == UsdGeomTokens->z) ? UsdGeomTokens->z : UsdGeomTokens->y;
    if (d.stageUpAxis == normalizedAxis)
        return;
    d.stageUpAxis = normalizedAxis;
    updateAuxiliaryGrid();
    refreshAuxiliarySceneIndex();
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateBoundingBox(const GfBBox3d& bbox)
{
    SignalGuard::Scope guard(this);
    d.bbox = bbox;
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateMask(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    d.mask = paths;
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updatePrims(const NoticeBatch& batch)
{
    Q_UNUSED(batch);
    SignalGuard::Scope guard(this);
    rebuildSelectionBBoxes();
    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateTransform(bool enabled)
{
    if (d.transformEnabled == enabled)
        return;
    d.transformEnabled = enabled;
    if (!enabled) {
        if (d.transformDragging)
            endTransformDrag();
        d.transformHoverAxis = 0;
        d.transformActiveAxis = 0;
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::captureVisible()
{
    QElapsedTimer timer;
    timer.start();
    d.glwidget->makeCurrent();
#ifdef WIN32
    glDepthMask(GL_TRUE);
#endif
    const GfVec2i size = widgetSize();
    const GfVec4d viewport = widgetViewport();
    GfCamera camera = viewCamera()->camera();
    GfFrustum frustum = camera.GetFrustum();
    UsdImagingGLEngine::PickParams pickParams;
    pickParams.resolveMode = TfToken("resolveUnique");
    constexpr int tilesX = 6;
    constexpr int tilesY = 6;
    constexpr double overlap = 0.20;
    auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    QList<SdfPath> captured;
    int totalRawHits = 0;
    int tilesWithHits = 0;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            const double tileW = 1.0 / static_cast<double>(tilesX);
            const double tileH = 1.0 / static_cast<double>(tilesY);
            double u0 = tx * tileW;
            double v0 = ty * tileH;
            double u1 = (tx + 1) * tileW;
            double v1 = (ty + 1) * tileH;
            const double padX = tileW * overlap * 0.5;
            const double padY = tileH * overlap * 0.5;
            u0 = clamp01(u0 - padX);
            v0 = clamp01(v0 - padY);
            u1 = clamp01(u1 + padX);
            v1 = clamp01(v1 + padY);
            const double centerU = (u0 + u1) * 0.5;
            const double centerV = (v0 + v1) * 0.5;
            const double sizeU = (u1 - u0);
            const double sizeV = (v1 - v0);
            GfVec2d center(centerU * 2.0 - 1.0, -1.0 * (centerV * 2.0 - 1.0));
            GfVec2d pickSize(sizeU, sizeV);
            GfFrustum tileFrustum = frustum.ComputeNarrowedFrustum(center, pickSize);
            UsdImagingGLEngine::IntersectionResultVector results;
            const bool hit = pickMaskedIntersection(pickParams, tileFrustum, &results);
            if (!hit)
                continue;
            tilesWithHits++;
            totalRawHits += static_cast<int>(results.size());
            for (const auto& result : results) {
                if (!result.hitPrimPath.IsEmpty()) {
                    captured.append(result.hitPrimPath);
                }
            }
        }
    }
    captured = path::uniquePaths(captured);
    bool changed = false;
    int addedCount = 0;
    for (const SdfPath& path : captured) {
        if (!d.visibleCapture.contains(path)) {
            d.visibleCapture.append(path);
            changed = true;
            addedCount++;
        }
    }
    if (changed && viewState() && viewState()->sceneStatsEnabled())
        updateSceneStats();
    if (changed)
        d.glwidget->update();
    Q_EMIT d.glwidget->captureReady(timer.elapsed());
}

void
ImagingGLWidgetPrivate::clearVisibleCapture()
{
    if (d.visibleCapture.isEmpty())
        return;
    d.visibleCapture.clear();
    if (viewState() && viewState()->sceneStatsEnabled())
        updateSceneStats();
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateCamera(const GfCamera& camera)
{
    Q_UNUSED(camera);
    d.glwidget->update();
    updateAxis();
}

void
ImagingGLWidgetPrivate::updateSelection(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    d.selection = paths;
    if (d.glEngine) {
        d.glEngine->SetSelected(QListToSdfPathVector(paths));
    }
    rebuildSelectionBBoxes();
    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::rebuildSelectionBBoxes()
{
    d.selectionBBoxes.clear();
    if (!d.context || !d.stage || d.selection.isEmpty())
        return;
    READ_LOCKER(locker, d.context->stageLock(), "stageLock");
    if (!d.stage)
        return;
    UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(),
                               { UsdGeomTokens->default_, UsdGeomTokens->proxy, UsdGeomTokens->render }, true);
    d.selectionBBoxes.reserve(d.selection.size());
    for (const SdfPath& path : d.selection) {
        UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            continue;
        GfBBox3d bbox = bboxCache.ComputeWorldBound(prim);
        if (!bbox.GetRange().IsEmpty())
            d.selectionBBoxes.push_back(bbox);
    }
}

void
ImagingGLWidgetPrivate::ensureAuxiliarySceneIndex()
{
    if (!d.glEngine || d.auxiliarySceneIndexInserted || !d.auxiliary)
        return;
    HdMergingSceneIndexRefPtr mergingSceneIndex = d.glEngine->mergingSceneIndex();
    if (!mergingSceneIndex) {
        qWarning() << "could not find Stageviz merging scene index";
        return;
    }
    UsdImagingCreateSceneIndicesInfo createInfo;
    createInfo.stage = d.auxiliary;
    createInfo.displayUnloadedPrimsWithBounds = false;
    createInfo.addDrawModeSceneIndex = true;
    d.auxiliarySceneIndices = UsdImagingCreateSceneIndices(createInfo);
    if (!d.auxiliarySceneIndices.stageSceneIndex || !d.auxiliarySceneIndices.finalSceneIndex) {
        qWarning() << "could not create auxiliary USD Imaging scene-index chain";
        d.auxiliarySceneIndices = {};
        return;
    }
    // Merge the fully processed USD Imaging output, not the raw
    // UsdImagingStageSceneIndex. The processing chain resolves USD semantics
    // such as material bindings into the Hydra schemas expected downstream.
    mergingSceneIndex->AddInputScene(d.auxiliarySceneIndices.finalSceneIndex, SdfPath::AbsoluteRootPath());
    d.auxiliarySceneIndexInserted = true;
}

void
ImagingGLWidgetPrivate::refreshAuxiliarySceneIndex()
{
    if (!d.auxiliarySceneIndices.stageSceneIndex)
        return;
    // UsdImagingStageSceneIndex queues USD edits while it is being queried.
    // Flush those edits so they propagate through the complete auxiliary
    // USD Imaging chain and into the Stageviz merge.
    d.auxiliarySceneIndices.stageSceneIndex->ApplyPendingUpdates();
}

void
ImagingGLWidgetPrivate::updateAuxiliaryGrid()
{
    if (!d.auxiliary)
        return;
    ViewState* state = viewState();
    if (!state)
        return;
    WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
    const SdfPath displayPath("/Display");
    const SdfPath gridRootPath("/Display/Grid");
    const SdfPath gridPath("/Display/Grid/Lines");
    const SdfPath centerPath("/Display/Grid/Center");
    const SdfPath materialsPath("/Materials");
    const SdfPath gridMaterialPath("/Materials/Grid");
    const SdfPath centerMaterialPath("/Materials/GridCenter");
    if (!state->gridEnabled()) {
        d.auxiliary->RemovePrim(gridRootPath);
        return;
    }
    UsdGeomScope::Define(d.auxiliary, displayPath);
    UsdGeomScope::Define(d.auxiliary, gridRootPath);
    UsdGeomScope::Define(d.auxiliary, materialsPath);
    const TfToken& upAxis = d.stageUpAxis;
    constexpr int lines = 12;
    constexpr float spacing = 1.0f;
    constexpr float extent = lines * spacing;
    auto point = [&](float a, float b) {
        return upAxis == UsdGeomTokens->z ? GfVec3f(a, b, 0.0f) : GfVec3f(a, 0.0f, b);
    };
    VtVec3fArray points;
    VtIntArray counts;
    for (int i = 1; i <= lines; ++i) {
        const float offset = spacing * static_cast<float>(i);
        points.push_back(point(-offset, -extent));
        points.push_back(point(-offset, extent));
        points.push_back(point(offset, -extent));
        points.push_back(point(offset, extent));
        points.push_back(point(-extent, -offset));
        points.push_back(point(extent, -offset));
        points.push_back(point(-extent, offset));
        points.push_back(point(extent, offset));
        for (int j = 0; j < 4; ++j)
            counts.push_back(2);
    }
    UsdGeomBasisCurves grid = UsdGeomBasisCurves::Define(d.auxiliary, gridPath);
    grid.CreateTypeAttr(VtValue(UsdGeomTokens->linear));
    grid.CreateBasisAttr(VtValue(UsdGeomTokens->bezier));
    grid.CreateWrapAttr(VtValue(UsdGeomTokens->nonperiodic));
    grid.CreatePointsAttr(VtValue(points));
    grid.CreateCurveVertexCountsAttr(VtValue(counts));
    UsdAttribute gridWidths = grid.CreateWidthsAttr(VtValue(VtFloatArray { 1.0f }));
    gridWidths.SetMetadata(TfToken("interpolation"), VtValue(UsdGeomTokens->constant));
    VtVec3fArray centerPoints;
    VtIntArray centerCounts;
    centerPoints.push_back(point(0.0f, -extent));
    centerPoints.push_back(point(0.0f, extent));
    centerCounts.push_back(2);
    centerPoints.push_back(point(-extent, 0.0f));
    centerPoints.push_back(point(extent, 0.0f));
    centerCounts.push_back(2);
    UsdGeomBasisCurves center = UsdGeomBasisCurves::Define(d.auxiliary, centerPath);
    center.CreateTypeAttr(VtValue(UsdGeomTokens->linear));
    center.CreateBasisAttr(VtValue(UsdGeomTokens->bezier));
    center.CreateWrapAttr(VtValue(UsdGeomTokens->nonperiodic));
    center.CreatePointsAttr(VtValue(centerPoints));
    center.CreateCurveVertexCountsAttr(VtValue(centerCounts));
    UsdAttribute centerWidths = center.CreateWidthsAttr(VtValue(VtFloatArray { 1.0f }));
    centerWidths.SetMetadata(TfToken("interpolation"), VtValue(UsdGeomTokens->constant));
    const QColor gridColor = state->gridColor();
    const GfVec3f color = gridColor.isValid() ? GfVec3f(gridColor.redF(), gridColor.greenF(), gridColor.blueF())
                                              : GfVec3f(0.34f);
    authorAuxiliaryGridMaterial(gridMaterialPath, color);
    authorAuxiliaryGridMaterial(centerMaterialPath, GfVec3f(0.0f));
    UsdShadeMaterial gridMaterial(d.auxiliary->GetPrimAtPath(gridMaterialPath));
    UsdShadeMaterial centerMaterial(d.auxiliary->GetPrimAtPath(centerMaterialPath));
    UsdShadeMaterialBindingAPI::Apply(grid.GetPrim()).Bind(gridMaterial);
    UsdShadeMaterialBindingAPI::Apply(center.GetPrim()).Bind(centerMaterial);
}

void
ImagingGLWidgetPrivate::ensureAuxiliaryMaterials()
{
    if (!d.auxiliary)
        return;
    WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
    const SdfPath materialsPath("/Materials");
    const SdfPath clayPath("/Materials/Clay");
    UsdGeomScope::Define(d.auxiliary, materialsPath);
    // Built-in Stageviz clay lives on the shared auxiliary stage just like any
    // other render-support material.
    authorAuxiliaryStandardSurface(clayPath, GfVec3f(0.55f, 0.18f, 0.16f), 0.0f, 0.68f, 0.35f);
}

void
ImagingGLWidgetPrivate::authorAuxiliaryGridMaterial(const SdfPath& materialPath, const GfVec3f& color)
{
    if (!d.auxiliary)
        return;
    UsdShadeMaterial material = UsdShadeMaterial::Define(d.auxiliary, materialPath);
    UsdShadeShader surface = UsdShadeShader::Define(d.auxiliary, materialPath.AppendChild(TfToken("Surface")));
    // Use the same MaterialX Standard Surface path as the working viewport
    // overrides. The grid is intentionally emission-only so its appearance is
    // stable and independent of viewport lighting, matching the old direct
    // Hydra grid material.
    surface.CreateIdAttr(VtValue(TfToken("ND_standard_surface_surfaceshader")));
    surface.CreateInput(TfToken("base"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.0f));
    surface.CreateInput(TfToken("emission"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("emission_color"), SdfValueTypeNames->Color3f).Set(color);
    surface.CreateInput(TfToken("metalness"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(0.0f);
    UsdShadeOutput output = surface.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(output);
}

void
ImagingGLWidgetPrivate::authorAuxiliaryStandardSurface(const SdfPath& materialPath, const GfVec3f& baseColor,
                                                       float metalness, float roughness, float specular)
{
    if (!d.auxiliary)
        return;
    UsdShadeMaterial material = UsdShadeMaterial::Define(d.auxiliary, materialPath);
    UsdShadeShader surface = UsdShadeShader::Define(d.auxiliary, materialPath.AppendChild(TfToken("Surface")));
    surface.CreateIdAttr(VtValue(TfToken("ND_standard_surface_surfaceshader")));
    surface.CreateInput(TfToken("base"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(baseColor);
    surface.CreateInput(TfToken("metalness"), SdfValueTypeNames->Float).Set(metalness);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(roughness);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(specular);
    UsdShadeOutput output = surface.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(output);
}

void
ImagingGLWidgetPrivate::ensureMaterialOverrideSceneIndex()
{
    if (!d.glEngine || d.materialOverrideSceneIndex)
        return;
    d.materialOverrideSceneIndex = d.glEngine->materialOverrideSceneIndex();
    if (!d.materialOverrideSceneIndex) {
        qWarning() << "could not find material override scene index";
        return;
    }
    updateMaterialOverrideSceneIndex();
    updateAuxiliaryGrid();
}

void
ImagingGLWidgetPrivate::updateMaterialOverrideSceneIndex()
{
    if (!d.materialOverrideSceneIndex || !d.context)
        return;
    ViewState* state = viewState();
    if (!state)
        return;
    d.materialOverrideSceneIndex->setSceneMaterialsEnabled(state->sceneMaterialsEnabled());
    d.materialOverrideSceneIndex->setMaterialPath(state->overrideMaterial());
    MaterialOverrideSceneIndex::Mode mode = MaterialOverrideSceneIndex::None;
    switch (state->materialMode()) {
    case ViewState::Clay: mode = MaterialOverrideSceneIndex::Clay; break;
    case ViewState::Override: mode = MaterialOverrideSceneIndex::Custom; break;
    case ViewState::All:
    default: mode = MaterialOverrideSceneIndex::None; break;
    }
    d.materialOverrideSceneIndex->setMode(mode);
}

bool
ImagingGLWidgetPrivate::projectWorldToScreen(const GfVec3d& world, QPointF& screen)
{
    if (!d.context || !viewCamera())
        return false;
    const GfCamera camera = viewCamera()->camera();
    const GfFrustum frustum = camera.GetFrustum();
    const GfMatrix4d view = frustum.ComputeViewMatrix();
    const GfMatrix4d projection = frustum.ComputeProjectionMatrix();
    const GfVec3d cameraPoint = view.Transform(world);
    const GfVec3d ndc = projection.Transform(cameraPoint);
    if (!std::isfinite(ndc[0]) || !std::isfinite(ndc[1]) || !std::isfinite(ndc[2]))
        return false;
    screen.setX((ndc[0] * 0.5 + 0.5) * d.glwidget->width());
    screen.setY((1.0 - (ndc[1] * 0.5 + 0.5)) * d.glwidget->height());
    return true;
}

QPointF
ImagingGLWidgetPrivate::transformAxisDirection(int axis)
{
    const GfMatrix4d view = viewCamera()->camera().GetFrustum().ComputeViewMatrix();
    const GfVec3d worldAxis = transformAxisVector(axis);
    if (worldAxis.GetLengthSq() < 1e-12)
        return {};
    const GfVec3d cameraAxis = view.TransformDir(worldAxis);
    QPointF direction(cameraAxis[0], -cameraAxis[1]);
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-4)
        return {};
    return direction / length;
}

GfVec3d
ImagingGLWidgetPrivate::transformAxisVector(int axis)
{
    const int component = ((axis - 1) % 3) + 1;
    if (component == 1)
        return GfVec3d::XAxis();
    if (component == 2)
        return GfVec3d::YAxis();
    if (component == 3)
        return GfVec3d::ZAxis();
    return GfVec3d(0.0);
}

double
ImagingGLWidgetPrivate::transformWorldPerPixel()
{
    return transformWorldPerPixel(d.transformPivot);
}

double
ImagingGLWidgetPrivate::transformWorldPerPixel(const GfVec3d& pivot)
{
    if (!viewCamera())
        return 0.0;
    double worldPerPixel = viewCamera()->mapToFrustumHeight(std::max(1, widgetSize()[1]));
    const GfCamera camera = viewCamera()->camera();
    const double pivotDistance = (camera.GetTransform().ExtractTranslation() - pivot).GetLength();
    const double focusDistance = std::max(1e-8, static_cast<double>(camera.GetFocusDistance()));
    worldPerPixel *= pivotDistance / focusDistance;
    return worldPerPixel;
}

bool
ImagingGLWidgetPrivate::transformRotationPoint(int axis, double angle, QPointF& screen)
{
    const GfVec3d normal = transformAxisVector(axis);
    if (normal.GetLengthSq() < 1e-12)
        return false;
    GfVec3d basisA;
    GfVec3d basisB;
    if (normal == GfVec3d::XAxis()) {
        basisA = GfVec3d::YAxis();
        basisB = GfVec3d::ZAxis();
    }
    else if (normal == GfVec3d::YAxis()) {
        basisA = GfVec3d::ZAxis();
        basisB = GfVec3d::XAxis();
    }
    else {
        basisA = GfVec3d::XAxis();
        basisB = GfVec3d::YAxis();
    }
    constexpr double radiusPixels = 62.0;
    const double radius = transformWorldPerPixel() * radiusPixels;
    if (radius <= 0.0)
        return false;
    const GfVec3d world = d.transformPivot + basisA * (std::cos(angle) * radius) + basisB * (std::sin(angle) * radius);
    return const_cast<ImagingGLWidgetPrivate*>(this)->projectWorldToScreen(world, screen);
}

bool
ImagingGLWidgetPrivate::transformRotationAngle(int axis, const QPointF& pos, double& angle, double* distance)
{
    constexpr int segments = 96;
    double bestDistance = std::numeric_limits<double>::max();
    double bestAngle = 0.0;
    bool found = false;
    for (int i = 0; i < segments; ++i) {
        const double a = (2.0 * 3.14159265358979323846 * static_cast<double>(i)) / static_cast<double>(segments);
        QPointF p;
        if (!transformRotationPoint(axis, a, p))
            continue;
        const double d = std::hypot(pos.x() - p.x(), pos.y() - p.y());
        if (d < bestDistance) {
            bestDistance = d;
            bestAngle = a;
            found = true;
        }
    }
    if (!found)
        return false;
    angle = bestAngle;
    if (distance)
        *distance = bestDistance;
    return true;
}

int
ImagingGLWidgetPrivate::hitTestTransform(const QPointF& pos)
{
    if (!d.transformEnabled || d.selection.isEmpty() || !d.stage)
        return 0;
    QPointF center;
    if (!projectWorldToScreen(d.transformPivot, center))
        return 0;
    // handles are encoded as:
    // 1..3 = translate X/Y/Z
    // 4..6 = rotate X/Y/Z
    // 7..9 = scale X/Y/Z
    constexpr double scaleDistance = 52.0;
    constexpr double scaleHitRadius = 8.0;
    for (int axis = 1; axis <= 3; ++axis) {
        const QPointF dir = transformAxisDirection(axis);
        if (dir.isNull())
            continue;
        const QPointF handle = center + dir * scaleDistance;
        if (std::hypot(pos.x() - handle.x(), pos.y() - handle.y()) <= scaleHitRadius)
            return axis + 6;
    }
    constexpr double rotateHitWidth = 7.0;
    int rotateHandle = 0;
    double rotateDistance = rotateHitWidth;
    for (int axis = 1; axis <= 3; ++axis) {
        double angle = 0.0;
        double distance = 0.0;
        if (transformRotationAngle(axis, pos, angle, &distance) && distance < rotateDistance) {
            rotateDistance = distance;
            rotateHandle = axis + 3;
        }
    }
    if (rotateHandle != 0)
        return rotateHandle;
    constexpr double axisLength = 82.0;
    constexpr double hitWidth = 8.0;
    int bestAxis = 0;
    double bestDistance = hitWidth;
    for (int axis = 1; axis <= 3; ++axis) {
        const QPointF dir = transformAxisDirection(axis);
        if (dir.isNull())
            continue;
        const QPointF end = center + dir * axisLength;
        const QPointF segment = end - center;
        const double length2 = QPointF::dotProduct(segment, segment);
        if (length2 <= 0.0)
            continue;
        const double t = std::clamp(QPointF::dotProduct(pos - center, segment) / length2, 0.0, 1.0);
        const QPointF nearest = center + segment * t;
        const double distance = std::hypot(pos.x() - nearest.x(), pos.y() - nearest.y());
        if (distance < bestDistance) {
            bestDistance = distance;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

bool
ImagingGLWidgetPrivate::beginTransformDrag(const QPointF& pos)
{
    if (!d.transformEnabled)
        return false;
    const int handle = hitTestTransform(pos);
    if (handle == 0 || !d.context || !d.stage)
        return false;
    QList<SdfPath> paths;
    QList<GfMatrix4d> before;
    GfVec3d pivot(0.0);
    int count = 0;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return false;
        for (const SdfPath& selectedPath : d.selection) {
            const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
            if (!stage::isTransformEditable(d.stage, path))
                continue;
            GfMatrix4d matrix(1.0);
            QString error;
            if (!stage::worldTransform(d.stage, path, matrix, error))
                continue;
            paths.append(path);
            before.append(matrix);
            pivot += matrix.ExtractTranslation();
            ++count;
        }
    }
    if (before.isEmpty() || count == 0)
        return false;
    d.transformPivot = pivot / static_cast<double>(count);
    d.transformStartPivot = d.transformPivot;
    d.transformPaths = paths;
    d.transformBefore = before;
    d.transformAfter = before;
    d.transformStart = pos;
    d.transformActiveAxis = handle;
    d.transformHoverAxis = handle;
    d.transformRotationStartAngle = 0.0;
    if (handle >= 4 && handle <= 6) {
        double angle = 0.0;
        if (!transformRotationAngle(handle - 3, pos, angle))
            return false;
        d.transformRotationStartAngle = angle;
    }
    d.transformDragging = true;
    d.glwidget->update();
    return true;
}

void
ImagingGLWidgetPrivate::updateTransformDrag(const QPointF& pos)
{
    if (!d.transformDragging || d.transformActiveAxis == 0 || d.transformBefore.isEmpty())
        return;
    d.transformAfter = d.transformBefore;
    if (d.transformActiveAxis >= 1 && d.transformActiveAxis <= 3) {
        const GfVec3d axis = transformAxisVector(d.transformActiveAxis);
        if (axis.GetLengthSq() < 1e-12)
            return;
        QPointF pivotScreen;
        QPointF axisScreen;
        if (!projectWorldToScreen(d.transformStartPivot, pivotScreen))
            return;
        const double probeDistance = std::max(1e-6, transformWorldPerPixel(d.transformStartPivot) * 100.0);
        if (!projectWorldToScreen(d.transformStartPivot + axis * probeDistance, axisScreen)) {
            return;
        }
        const QPointF projectedAxis = axisScreen - pivotScreen;
        const double projectedLength = std::hypot(projectedAxis.x(), projectedAxis.y());
        if (projectedLength < 1e-6)
            return;
        const QPointF screenAxis = projectedAxis / projectedLength;
        const double pixels = QPointF::dotProduct(pos - d.transformStart, screenAxis);
        const double worldPerScreenPixel = probeDistance / projectedLength;
        const GfVec3d delta = axis * (pixels * worldPerScreenPixel);
        for (qsizetype i = 0; i < d.transformAfter.size(); ++i) {
            GfMatrix4d matrix = d.transformBefore.at(i);
            matrix.SetTranslateOnly(d.transformBefore.at(i).ExtractTranslation() + delta);
            d.transformAfter[i] = matrix;
        }
        d.transformPivot = d.transformStartPivot + delta;
    }
    else if (d.transformActiveAxis >= 4 && d.transformActiveAxis <= 6) {
        const int axisIndex = d.transformActiveAxis - 3;
        double currentAngle = 0.0;
        if (!transformRotationAngle(axisIndex, pos, currentAngle)) {
            return;
        }
        double deltaAngle = currentAngle - d.transformRotationStartAngle;
        while (deltaAngle > 3.14159265358979323846)
            deltaAngle -= 2.0 * 3.14159265358979323846;
        while (deltaAngle < -3.14159265358979323846)
            deltaAngle += 2.0 * 3.14159265358979323846;
        const GfVec3d axis = transformAxisVector(axisIndex);
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d rotation(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        rotation.SetRotate(GfRotation(axis, deltaAngle * 180.0 / 3.14159265358979323846));
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d delta = toOrigin * rotation * fromOrigin;
        for (qsizetype i = 0; i < d.transformAfter.size(); ++i) {
            d.transformAfter[i] = d.transformBefore.at(i) * delta;
        }
        d.transformPivot = d.transformStartPivot;
    }
    else if (d.transformActiveAxis >= 7 && d.transformActiveAxis <= 9) {
        const int axisIndex = d.transformActiveAxis - 6;
        const QPointF screenAxis = transformAxisDirection(axisIndex);
        if (screenAxis.isNull())
            return;
        const double pixels = QPointF::dotProduct(pos - d.transformStart, screenAxis);
        const double factor = std::clamp(std::exp(pixels * 0.01), 0.01, 100.0);
        GfVec3d scale(1.0);
        scale[axisIndex - 1] = factor;
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d scaleMatrix(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        scaleMatrix.SetScale(scale);
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d delta = toOrigin * scaleMatrix * fromOrigin;
        for (qsizetype i = 0; i < d.transformAfter.size(); ++i) {
            d.transformAfter[i] = d.transformBefore.at(i) * delta;
        }
        d.transformPivot = d.transformStartPivot;
    }
    {
        WRITE_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        for (qsizetype i = 0; i < d.transformPaths.size() && i < d.transformAfter.size(); ++i) {
            QString error;
            stage::setWorldTransform(d.stage, d.transformPaths.at(i), d.transformAfter.at(i), error);
        }
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::endTransformDrag()
{
    if (!d.transformDragging)
        return;
    const QList<SdfPath> paths = d.transformPaths;
    const QList<GfMatrix4d> before = d.transformBefore;
    const QList<GfMatrix4d> after = d.transformAfter;
    d.transformDragging = false;
    d.transformActiveAxis = 0;
    d.transformPaths.clear();
    d.transformBefore.clear();
    d.transformAfter.clear();
    bool changed = !paths.isEmpty() && paths.size() == before.size() && before.size() == after.size();
    if (changed) {
        changed = false;
        for (qsizetype i = 0; i < before.size(); ++i) {
            if (before.at(i) != after.at(i)) {
                changed = true;
                break;
            }
        }
    }
    if (changed)
        d.context->run(new Command(setTransforms(paths, before, after)));
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateTransformHover(const QPointF& pos)
{
    if (!d.transformEnabled) {
        if (d.transformHoverAxis != 0) {
            d.transformHoverAxis = 0;
            d.glwidget->update();
        }
        return;
    }
    const int handle = hitTestTransform(pos);
    if (handle == d.transformHoverAxis)
        return;
    d.transformHoverAxis = handle;
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::drawTransformTransform(QPainter& painter)
{
    if (!d.transformEnabled || !d.stage || d.selection.isEmpty())
        return;
    if (!d.transformDragging) {
        GfVec3d pivot(0.0);
        int count = 0;
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage)
                return;
            for (const SdfPath& selectedPath : d.selection) {
                const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
                if (!stage::isTransformEditable(d.stage, path))
                    continue;
                GfMatrix4d matrix(1.0);
                QString error;
                if (stage::worldTransform(d.stage, path, matrix, error)) {
                    pivot += matrix.ExtractTranslation();
                    ++count;
                }
            }
        }
        if (count == 0)
            return;
        d.transformPivot = pivot / static_cast<double>(count);
    }
    QPointF center;
    if (!projectWorldToScreen(d.transformPivot, center))
        return;
    constexpr double axisLength = 82.0;
    constexpr double scaleDistance = 52.0;
    constexpr double arrowLength = 13.0;
    constexpr double arrowWidth = 7.0;
    constexpr int ringSegments = 96;
    const QColor colors[4] = { QColor(), QColor(230, 70, 80), QColor(90, 200, 90), QColor(70, 125, 235) };
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // rotation rings are drawn first so translation/scale handles stay crisp
    // and easy to identify in front of them.
    for (int axis = 1; axis <= 3; ++axis) {
        QColor color = colors[axis];
        const int handle = axis + 3;
        if (handle == d.transformHoverAxis || handle == d.transformActiveAxis)
            color = QColor(255, 205, 70);
        QPolygonF ring;
        ring.reserve(ringSegments + 1);
        for (int i = 0; i <= ringSegments; ++i) {
            const double angle = (2.0 * 3.14159265358979323846 * static_cast<double>(i % ringSegments))
                                 / static_cast<double>(ringSegments);
            QPointF p;
            if (transformRotationPoint(axis, angle, p))
                ring << p;
        }
        if (ring.size() > 1) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, handle == d.transformActiveAxis ? 3.0 : 1.8, Qt::SolidLine, Qt::RoundCap));
            painter.drawPolyline(ring);
        }
    }
    // translation arrows.
    for (int axis = 1; axis <= 3; ++axis) {
        const QPointF dir = transformAxisDirection(axis);
        if (dir.isNull())
            continue;
        QColor color = colors[axis];
        if (axis == d.transformHoverAxis || axis == d.transformActiveAxis)
            color = QColor(255, 205, 70);
        const QPointF end = center + dir * axisLength;
        const QPointF normal(-dir.y(), dir.x());
        painter.setPen(QPen(color, axis == d.transformActiveAxis ? 4.0 : 3.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center, end - dir * 3.0);
        QPolygonF arrow;
        arrow << end << end - dir * arrowLength + normal * arrowWidth << end - dir * arrowLength - normal * arrowWidth;
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(arrow);
    }
    // axis scale handles. The cube sits on the same projected axis but closer
    // to the pivot than the translation arrow head.
    for (int axis = 1; axis <= 3; ++axis) {
        const QPointF dir = transformAxisDirection(axis);
        if (dir.isNull())
            continue;
        QColor color = colors[axis];
        const int handle = axis + 6;
        if (handle == d.transformHoverAxis || handle == d.transformActiveAxis)
            color = QColor(255, 205, 70);
        const QPointF p = center + dir * scaleDistance;
        painter.setPen(QPen(QColor(20, 20, 20, 190), 1.0));
        painter.setBrush(color);
        painter.drawRect(QRectF(p.x() - 5.0, p.y() - 5.0, 10.0, 10.0));
    }
    painter.setPen(QPen(QColor(245, 245, 245, 220), 1.5));
    painter.setBrush(QColor(35, 35, 35, 220));
    painter.drawEllipse(center, 5.0, 5.0);
    painter.restore();
}

QPoint
ImagingGLWidgetPrivate::deviceRatio(QPoint value) const
{
    return QPoint(deviceRatio(value.x()), deviceRatio(value.y()));
}

double
ImagingGLWidgetPrivate::deviceRatio(double value) const
{
    return value * d.glwidget->devicePixelRatio();
}

double
ImagingGLWidgetPrivate::widgetAspectRatio() const
{
    GfVec2i size = widgetSize();
    double width = static_cast<double>(size[0]);
    double height = static_cast<double>(size[1]);
    return width / std::max(1.0, height);
}

GfVec2i
ImagingGLWidgetPrivate::widgetSize() const
{
    int w = deviceRatio(d.glwidget->width());
    int h = deviceRatio(d.glwidget->height());
    return GfVec2i(w, h);
}

GfVec4d
ImagingGLWidgetPrivate::widgetViewport() const
{
    GfVec2i size = widgetSize();
    return GfVec4d(0, 0, size[0], size[1]);
}

void
ImagingGLWidgetPrivate::drawBorder(QPainter& painter)
{
    const int w = 1;
    painter.setPen(QPen(style()->color(Style::ColorRole::Border), w));
    painter.setBrush(Qt::NoBrush);
    QRect r = d.glwidget->rect().adjusted(w / 1, w / 1, -w / 1, -w / 1);
    painter.drawRect(r);
}

void
ImagingGLWidgetPrivate::updateAxis()
{
    GfCamera camera = viewCamera()->camera();
    GfFrustum frustum = camera.GetFrustum();
    GfMatrix4d viewMatrix = frustum.ComputeViewMatrix();
    const GfVec3d xCam = viewMatrix.TransformDir(GfVec3d(1.0, 0.0, 0.0));
    const GfVec3d yCam = viewMatrix.TransformDir(GfVec3d(0.0, 1.0, 0.0));
    const GfVec3d zCam = viewMatrix.TransformDir(GfVec3d(0.0, 0.0, 1.0));
    const int margin = 18;
    const int radius = 30;
    const int bubbleRadius = 10;
    const int width = margin + radius * 2 + margin;
    const int height = margin + radius * 2 + margin;
    const QPoint center(margin + radius, margin + radius);
    auto toPoint = [&](const GfVec3d& dir) -> QPoint {
        return QPoint(qRound(center.x() + dir[0] * radius), qRound(center.y() - dir[1] * radius));
    };
    struct AxisLine {
        QString label;
        QColor color;
        GfVec3d dir;
    };
    QVector<AxisLine> axes { { "X", QColor(200, 50, 50), xCam },
                             { "Y", QColor(50, 170, 70), yCam },
                             { "Z", QColor(70, 110, 220), zCam } };
    std::sort(axes.begin(), axes.end(), [](const AxisLine& a, const AxisLine& b) { return a.dir[2] < b.dir[2]; });
    const bool hasStage = static_cast<bool>(d.stage);
    const qreal opacity = hasStage ? 1.0 : 0.35;
    const qreal dpr = d.glwidget->devicePixelRatioF();
    d.axis = QImage(qRound(width * dpr), qRound(height * dpr), QImage::Format_ARGB32_Premultiplied);
    d.axis.setDevicePixelRatio(dpr);
    d.axis.fill(Qt::transparent);
    QPainter painter(&d.axis);
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font = app()->font();
    font.setPixelSize(style()->fontSize(Style::UIScale::Small));
    font.setBold(true);
    painter.setFont(font);
    QFontMetrics fm(font);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 20));
    painter.drawEllipse(center, radius - 10, radius - 10);
    for (const AxisLine& axis : axes) {
        const QPoint end = toPoint(axis.dir);
        painter.setPen(QPen(axis.color, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center, end);
        painter.setPen(Qt::NoPen);
        painter.setBrush(axis.color);
        painter.drawEllipse(end, bubbleRadius, bubbleRadius);
        const int textWidth = fm.horizontalAdvance(axis.label);
        const int textX = end.x() - textWidth / 2;
        const int textY = end.y() + (fm.ascent() - fm.descent()) / 2;
        painter.setPen(QColor(0, 0, 0, 160));
        painter.drawText(textX + 1, textY + 1, axis.label);
        painter.setPen(Qt::white);
        painter.drawText(textX, textY, axis.label);
    }
}

void
ImagingGLWidgetPrivate::updateSceneStats()
{
    struct SceneStats {
        size_t prims = 0;
        size_t meshes = 0;
        size_t xforms = 0;
        size_t payloads = 0;
        size_t instances = 0;
        size_t vertices = 0;
        size_t normals = 0;
        size_t faces = 0;
    };
    auto accumulate = [&](const UsdPrim& prim, SceneStats& s) {
        if (!prim.IsActive() || !prim.IsLoaded())
            return;
        s.prims++;
        if (prim.IsA<UsdGeomXform>())
            s.xforms++;
        if (prim.IsA<UsdGeomMesh>()) {
            s.meshes++;
            UsdGeomMesh mesh(prim);
            VtArray<GfVec3f> points;
            mesh.GetPointsAttr().Get(&points);
            s.vertices += points.size();
            VtArray<int> faceCounts;
            mesh.GetFaceVertexCountsAttr().Get(&faceCounts);
            s.faces += faceCounts.size();
            VtArray<GfVec3f> meshNormals;
            UsdGeomPrimvarsAPI pvAPI(prim);
            UsdGeomPrimvar normalsPv = pvAPI.GetPrimvar(TfToken("normals"));
            bool hasNormals = false;
            if (normalsPv && normalsPv.HasValue()) {
                normalsPv.Get(&meshNormals);
                hasNormals = true;
            }
            if (!hasNormals) {
                mesh.GetNormalsAttr().Get(&meshNormals);
            }
            s.normals += meshNormals.size();
        }
        if (prim.HasPayload())
            s.payloads++;
        if (prim.IsInstanceable())
            s.instances++;
    };
    auto filterRootPaths = [](const QList<SdfPath>& paths) {
        QList<SdfPath> result;
        for (const SdfPath& p : paths) {
            bool isChild = false;
            for (const SdfPath& other : paths) {
                if (p == other)
                    continue;
                if (p.HasPrefix(other)) {
                    isChild = true;
                    break;
                }
            }
            if (!isChild)
                result.append(p);
        }
        return result;
    };
    SceneStats total;
    SceneStats selected;
    if (d.stage) {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        for (const UsdPrim& prim : d.stage->Traverse()) {
            accumulate(prim, total);
        }
        if (!d.selection.isEmpty()) {
            const QList<SdfPath> roots = filterRootPaths(d.selection);
            for (const SdfPath& path : roots) {
                UsdPrim root = d.stage->GetPrimAtPath(path);
                if (!root)
                    continue;
                for (const UsdPrim& prim : UsdPrimRange(root)) {
                    accumulate(prim, selected);
                }
            }
        }
    }
    QLocale locale = QLocale::system();
    auto fmt = [&](size_t v) { return locale.toString((qlonglong)v); };
    const bool hasSelection = !d.selection.isEmpty();
    auto fmtPair = [&](size_t totalValue, size_t selectedValue) {
        if (hasSelection && selectedValue > 0)
            return QString("%1 (%2)").arg(fmt(totalValue), fmt(selectedValue));
        return fmt(totalValue);
    };
    struct Row {
        QString label;
        QString value;
    };
    QVector<Row> rows { { "Prims", fmtPair(total.prims, selected.prims) },
                        { "Meshes", fmtPair(total.meshes, selected.meshes) },
                        { "Xforms", fmtPair(total.xforms, selected.xforms) },
                        { "Payloads", fmtPair(total.payloads, selected.payloads) },
                        { "Instances", fmtPair(total.instances, selected.instances) },
                        { "Vertices", fmtPair(total.vertices, selected.vertices) },
                        { "Normals", fmtPair(total.normals, selected.normals) },
                        { "Faces", fmtPair(total.faces, selected.faces) } };
    if (!d.visibleCapture.isEmpty()) {
        rows.append({ "Captures", fmt(d.visibleCapture.size()) });
    }
    double dpr = d.glwidget->devicePixelRatioF();
    QFont font = d.glwidget->font();
    font.setPixelSize(style()->fontSize(Style::UIScale::Small));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    QFontMetrics fm(font);
    int rowHeight = fm.lineSpacing() + 2;
    int marginLeft = 18;
    int marginTop = 16;
    int columnSpacing = 20;
    int labelWidth = 0;
    int valueWidth = 0;
    for (const auto& r : rows) {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(r.label));
        valueWidth = std::max(valueWidth, fm.horizontalAdvance(r.value));
    }
    qsizetype width = labelWidth + columnSpacing + valueWidth + marginLeft;
    qsizetype height = rows.size() * rowHeight + marginTop;
    d.sceneStats = QImage(width * dpr, height * dpr, QImage::Format_ARGB32_Premultiplied);
    d.sceneStats.setDevicePixelRatio(dpr);
    d.sceneStats.fill(Qt::transparent);
    QPainter p(&d.sceneStats);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    QColor textColor;
    if (d.stage) {
        textColor = style()->color(Style::ColorRole::Text, Style::UIState::Normal);
    }
    else {
        textColor = style()->color(Style::ColorRole::Text, Style::UIState::Disabled);
    }
    const QColor shadowColor(0, 0, 0, 160);
    int y = marginTop + fm.ascent();
    int labelX = marginLeft;
    int valueX = marginLeft + labelWidth + columnSpacing;
    for (const auto& r : rows) {
        p.setPen(shadowColor);
        p.drawText(labelX + 1, y + 1, r.label);
        p.drawText(valueX + 1, y + 1, r.value);
        p.setPen(textColor);
        p.drawText(labelX, y, r.label);
        p.drawText(valueX, y, r.value);
        y += rowHeight;
    }
}

void
ImagingGLWidgetPrivate::updatePerformanceStats()
{
    const bool hasEngine = d.glEngine && d.stage;
    VtDictionary stats;
    if (d.glEngine)
        stats = d.glEngine->GetRenderStats();
    auto fmtMB = [&](uint64_t bytes) { return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 2) + " MB"; };
    struct Row {
        QString label;
        QString value;
    };
    QVector<Row> rows;
    if (d.glEngine) {
        if (Hgi* hgi = d.glEngine->GetHgi())
            rows.append({ "Hgi", TfTokenToQString(hgi->GetAPIName()) });
    }
    else {
        rows.append({ "Hgi", QStringLiteral("-") });
    }
    rows.append({ "GPU time", hasEngine ? QString::number(d.gpuPerformanceMs, 'f', 2) + " ms" : "-" });
    if (hasEngine) {
        if (stats.count("gpuMemoryUsed"))
            rows.append({ "GPU mem", fmtMB(VtDictionaryGet<uint64_t>(stats, "gpuMemoryUsed")) });
        if (stats.count("primvar"))
            rows.append({ "primvar", fmtMB(VtDictionaryGet<uint64_t>(stats, "primvar")) });
        if (stats.count("topology"))
            rows.append({ "topology", fmtMB(VtDictionaryGet<uint64_t>(stats, "topology")) });
        if (stats.count("drawingShader"))
            rows.append({ "shader", fmtMB(VtDictionaryGet<uint64_t>(stats, "drawingShader")) });
        if (stats.count("textureMemory"))
            rows.append({ "texture", fmtMB(VtDictionaryGet<uint64_t>(stats, "textureMemory")) });
    }
    const double dpr = d.glwidget->devicePixelRatioF();
    QFont font = d.glwidget->font();
    font.setPixelSize(style()->fontSize(Style::UIScale::Small));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    QFontMetrics fm(font);
    const int rowHeight = fm.lineSpacing() + 2;
    const int marginLeft = 18;
    const int marginRight = 18;
    const int marginTop = 16;
    const int columnSpacing = 24;
    int labelWidth = 0;
    int valueWidth = 0;
    for (const Row& r : rows) {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(r.label));
        valueWidth = std::max(valueWidth, fm.horizontalAdvance(r.value));
    }
    const int width = marginLeft + labelWidth + columnSpacing + valueWidth + marginRight;
    const int height = static_cast<int>(rows.size()) * rowHeight + marginTop;
    d.performanceStats = QImage(qRound(width * dpr), qRound(height * dpr), QImage::Format_ARGB32_Premultiplied);
    d.performanceStats.setDevicePixelRatio(dpr);
    d.performanceStats.fill(Qt::transparent);
    QPainter p(&d.performanceStats);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    const QColor textColor = d.stage ? style()->color(Style::ColorRole::Text, Style::UIState::Normal)
                                     : style()->color(Style::ColorRole::Text, Style::UIState::Disabled);
    const QColor shadowColor(0, 0, 0, 160);
    int y = marginTop + fm.ascent();
    const int labelX = marginLeft;
    const int valueRight = width - marginRight;
    for (const Row& r : rows) {
        const QRect labelRect(labelX, y - fm.ascent(), labelWidth, rowHeight);
        const QRect valueRect(valueRight - valueWidth, y - fm.ascent(), valueWidth, rowHeight);
        p.setPen(shadowColor);
        p.drawText(labelRect.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, r.label);
        p.drawText(valueRect.translated(1, 1), Qt::AlignRight | Qt::AlignVCenter, r.value);
        p.setPen(textColor);
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, r.label);
        p.drawText(valueRect, Qt::AlignRight | Qt::AlignVCenter, r.value);
        y += rowHeight;
    }
}

bool
ImagingGLWidgetPrivate::isPathMaskedIn(const SdfPath& path) const
{
    if (path.IsEmpty())
        return false;
    if (d.mask.isEmpty())
        return true;
    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    for (const SdfPath& maskedPath : d.mask) {
        const SdfPath maskedPrimPath = maskedPath.IsPropertyPath() ? maskedPath.GetPrimPath() : maskedPath;
        if (primPath == maskedPrimPath || primPath.HasPrefix(maskedPrimPath))
            return true;
    }
    return false;
}

bool
ImagingGLWidgetPrivate::pickMaskedIntersection(const UsdImagingGLEngine::PickParams& pickParams,
                                               const GfFrustum& pickFrustum,
                                               UsdImagingGLEngine::IntersectionResultVector* results)
{
    if (!results)
        return false;
    results->clear();
    if (!d.stage || !d.glEngine)
        return false;
    const GfMatrix4d viewMatrix = pickFrustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = pickFrustum.ComputeProjectionMatrix();
    READ_LOCKER(locker, d.context->stageLock(), "stageLock");
    if (!d.stage)
        return false;
    if (d.mask.isEmpty()) {
        return d.glEngine->TestIntersection(pickParams, viewMatrix, projectionMatrix, d.stage->GetPseudoRoot(),
                                            d.params, results);
    }
    bool hitAny = false;
    for (const SdfPath& maskPath : d.mask) {
        UsdPrim root = d.stage->GetPrimAtPath(maskPath);
        if (!root)
            continue;
        UsdImagingGLEngine::IntersectionResultVector localResults;
        const bool hit = d.glEngine->TestIntersection(pickParams, viewMatrix, projectionMatrix, root, d.params,
                                                      &localResults);
        if (!hit)
            continue;
        for (const auto& item : localResults) {
            if (!item.hitPrimPath.IsEmpty() && isPathMaskedIn(item.hitPrimPath))
                results->push_back(item);
        }
        if (!localResults.empty())
            hitAny = true;
    }
    return hitAny && !results->empty();
}

ImagingGLWidget::ImagingGLWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , p(new ImagingGLWidgetPrivate())
{
    p->d.glwidget = this;
    p->init();
}

ImagingGLWidget::~ImagingGLWidget() = default;

ViewContext*
ImagingGLWidget::context() const
{
    return p->d.context;
}
void
ImagingGLWidget::setContext(ViewContext* context)
{
    if (p->d.context != context) {
        p->d.context = context;
        p->initContext();
    }
}
QImage
ImagingGLWidget::captureImage()
{
    return QOpenGLWidget::grabFramebuffer();
}
bool
ImagingGLWidget::transformEnabled() const
{
    return p->d.transformEnabled;
}
void
ImagingGLWidget::setTransformEnabled(bool enabled)
{
    p->updateTransform(enabled);
}
void
ImagingGLWidget::close()
{
    p->close();
}
QList<QString>
ImagingGLWidget::rendererAovs() const
{
    if (!p->d.glEngine)
        return {};
    return TfTokenVectorToQList(p->d.glEngine->GetRendererAovs());
}
void
ImagingGLWidget::captureVisible()
{
    p->captureVisible();
}
void
ImagingGLWidget::clearVisibleCapture()
{
    p->clearVisibleCapture();
}
QList<SdfPath>
ImagingGLWidget::visibleCapturePaths() const
{
    return p->d.visibleCapture;
}
void
ImagingGLWidget::updateStage(UsdStageRefPtr stage)
{
    p->updateStage(stage);
}
void
ImagingGLWidget::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    p->updateAuxiliary(auxiliary);
}
void
ImagingGLWidget::updateStageUp(const TfToken& upAxis)
{
    p->updateStageUp(upAxis);
}
void
ImagingGLWidget::updateBoundingBox(const GfBBox3d& bbox)
{
    p->updateBoundingBox(bbox);
}
void
ImagingGLWidget::updateMask(const QList<SdfPath>& paths)
{
    p->updateMask(paths);
}
void
ImagingGLWidget::updatePrims(const NoticeBatch& batch)
{
    p->updatePrims(batch);
}
void
ImagingGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    p->initGL();
}
void
ImagingGLWidget::paintGL()
{
    p->paintGL();
}
void
ImagingGLWidget::paintEvent(QPaintEvent* event)
{
    QOpenGLWidget::paintEvent(event);
    p->paintEvent(event);
}
void
ImagingGLWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    p->mouseDoubleClickEvent(event);
}
void
ImagingGLWidget::mousePressEvent(QMouseEvent* event)
{
    p->mousePressEvent(event);
}
void
ImagingGLWidget::mouseMoveEvent(QMouseEvent* event)
{
    p->mouseMoveEvent(event);
}
void
ImagingGLWidget::mouseReleaseEvent(QMouseEvent* event)
{
    p->mouseReleaseEvent(event);
}
void
ImagingGLWidget::wheelEvent(QWheelEvent* event)
{
    p->wheelEvent(event);
}
}  // namespace stageviz
