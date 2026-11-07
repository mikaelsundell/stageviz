// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "viewportwidget.h"
#include "application.h"
#include "bboxutils.h"
#include "commandmaterial.h"
#include "contextmenu.h"
#include "mime.h"
#include "notice.h"
#include "os.h"
#include "paths.h"
#include "perfscope.h"
#include "qtutils.h"
#include "renderengine.h"
#include "session.h"
#include "signalguard.h"
#include "tracelocks.h"
#include "ui.h"
#include "usdutils.h"
#include "viewcamera.h"
#include "viewcontext.h"
#include "viewportauxiliary.h"
#include "viewporthost.h"
#include "viewportlights.h"
#include "viewportselection.h"
#include "viewporttransform.h"
#include "viewstate.h"
#include <QApplication>
#include <QColorSpace>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QOpenGLContext>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QPolygonF>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <pxr/base/tf/error.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {


struct ViewportSceneCounts {
    size_t prims = 0;
    size_t meshes = 0;
    size_t xforms = 0;
    size_t payloads = 0;
    size_t instances = 0;
    size_t vertices = 0;
    size_t normals = 0;
    size_t faces = 0;
};

class ViewportWidgetPrivate : public QObject, public SignalGuard, public ViewportHost {
public:
    void init();
    void initGL();
    void initContext();
    SelectionList* selectionList();
    ViewCamera* viewCamera() override;
    ViewState* viewState() override;
    void close();
    void paintGL();
    void paintOverlay(QPainter& painter);
    void updateOverlay() override;
    void focusEvent(QMouseEvent* event);
    void contextMenuEvent(QContextMenuEvent* event);
    void dragEnterEvent(QDragEnterEvent* event);
    void dragMoveEvent(QDragMoveEvent* event);
    void dropEvent(QDropEvent* event);
    void mouseDoubleClickEvent(QMouseEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void wheelEvent(QWheelEvent* event);
    bool eventFilter(QObject* object, QEvent* event) override;
    void updateStage(UsdStageRefPtr stage);
    void updateAuxiliary(UsdStageRefPtr auxiliary);
    void updateStageUp(const TfToken& upAxis);
    void updateBoundingBox(const GfBBox3d& bbox);
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    QList<SdfPath> capturePaths(bool accumulate);
    void clearVisibleCapture();

public Q_SLOTS:
    void updateCamera(const GfCamera& camera);
    void updateSelection(const QList<SdfPath>& paths);

public:
    void updateRenderEngineSettings();
    void navigationIdle();

    bool projectWorldToScreen(const GfVec3d& world, QPointF& screen) override;
    QPoint deviceRatio(QPoint value) const;
    double deviceRatio(double value) const;
    double widgetAspectRatio() const;
    GfVec2i widgetSize() const override;
    QRectF cameraGateRect() override;
    GfVec4d widgetViewport() override;
    GfVec4d renderViewport();
    void drawLetterbox(QPainter& painter);
    void drawBorder(QPainter& painter);
    void updateSceneStats();
    void updatePerformanceStats();

public:
    class OverlayWidget : public QWidget {
    public:
        explicit OverlayWidget(ViewportWidgetPrivate* owner, QWidget* parent)
            : QWidget(parent)
            , owner(owner)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_TranslucentBackground);
            setAttribute(Qt::WA_NoSystemBackground);
            setFocusPolicy(Qt::NoFocus);
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            owner->paintOverlay(painter);
        }

    private:
        ViewportWidgetPrivate* owner;
    };
    ViewportWidget* widget() const override { return d.glwidget; }
    ViewContext* context() const override { return d.context; }
    UsdStageRefPtr stage() const override { return d.stage; }
    UsdStageRefPtr auxiliaryStage() const override { return d.auxiliary->stage(); }
    RenderEngine* renderEngine() const override { return d.renderEngine.data(); }
    const QList<SdfPath>& selectedPaths() const override { return d.selection; }
    const QList<SdfPath>& maskPaths() const override { return d.mask; }
    bool pathMaskedIn(const SdfPath& path) const override { return d.selectionTool->isPathMaskedIn(path); }
    void syncSelection(const QList<SdfPath>& paths) override
    {
        d.selection = paths;
        d.selectedTotalsDirty = true;
        d.selectionTool->invalidateVisibleSelection();
    }
    const QList<SdfPath>& cachedVisibleSelection() { return d.selectionTool->visiblePaths(); }
    void updateAuxiliaryGrid() { d.auxiliary->updateGrid(viewState()); }
    void ensureAuxiliaryMaterials() { d.auxiliary->ensureMaterials(); }
    void updateAxis()
    {
        d.auxiliary->updateAxis(viewCamera()->camera(), bool(d.stage), d.glwidget->devicePixelRatioF());
    }

    struct Data {
        size_t count;
        qint64 frame;
        double gpuPerformanceMs;
        double timeCode;
        float defaultAmbient;
        float defaultSpecular;
        float defaultShininess;
        bool drag;
        bool navigating;
        bool suppressContextMenu;
        QPoint mousepos;
        QTimer navigationTimer;
        ViewportSceneCounts cachedSceneTotals;
        ViewportSceneCounts cachedSelectedTotals;
        bool selectedTotalsDirty = true;
        bool sceneTotalsDirty = true;
        QImage sceneStats;
        QImage performanceStats;
        GfBBox3d selectionBBox;
        UsdStageRefPtr stage;
        QScopedPointer<ViewportAuxiliary> auxiliary;
        QScopedPointer<ViewportSelection> selectionTool;
        QScopedPointer<ViewportLights> lights;
        QScopedPointer<ViewportTransform> transform;
        GfBBox3d bbox;
        QList<SdfPath> mask;
        QList<SdfPath> selection;
        QList<SdfPath> visibleCapture;
        QSet<SdfPath> visibleCaptureSet;
        QScopedPointer<RenderEngine> renderEngine;
        QPointer<ViewContext> context;
        QPointer<ViewportWidget> glwidget;
        QPointer<OverlayWidget> overlay;
    };
    Data d;
};

void
ViewportWidgetPrivate::init()
{
    d.auxiliary.reset(new ViewportAuxiliary());
    d.selectionTool.reset(new ViewportSelection(*this));
    d.lights.reset(new ViewportLights(*this));
    d.transform.reset(new ViewportTransform(*this));
    attach(d.glwidget);
    d.navigationTimer.setSingleShot(true);
    d.navigationTimer.setInterval(150);
    connect(&d.navigationTimer, &QTimer::timeout, this, &ViewportWidgetPrivate::navigationIdle);
    QSurfaceFormat format;
    format.setSamples(4);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setColorSpace(QColorSpace::SRgb);
    d.glwidget->setFormat(format);
    d.glwidget->setAcceptDrops(true);
    d.glwidget->setFocusPolicy(Qt::StrongFocus);
    d.overlay = new OverlayWidget(this, d.glwidget);
    d.overlay->setGeometry(d.glwidget->rect());
    d.overlay->show();
    d.overlay->raise();
    d.glwidget->installEventFilter(this);
    connect(d.glwidget, &QOpenGLWidget::frameSwapped, d.overlay, qOverload<>(&QWidget::update));
    if (QApplication* application = qApp)
        application->installEventFilter(this);
    d.count = 0;
    d.frame = 0;
    d.gpuPerformanceMs = 0.0;
    d.timeCode = 0.0;
    d.defaultAmbient = 0.4f;
    d.defaultSpecular = 0.5f;
    d.defaultShininess = 32.0f;
    d.drag = false;
    d.selectionTool->cancel();
    d.navigating = false;
    d.suppressContextMenu = false;
    d.context = nullptr;
}

void
ViewportWidgetPrivate::initGL()
{
    if (d.renderEngine && d.renderEngine->isInitialized())
        return;

    if (!QOpenGLContext::currentContext()) {
        return;
    }

    if (!d.renderEngine)
        d.renderEngine.reset(new RenderEngine(RenderEngine::ContextMode::Current));

    d.transform->updateGeometry();
    d.renderEngine->setStage(d.stage);
    d.renderEngine->setAuxiliaryStage(d.auxiliary->stage());


    d.renderEngine->setSelected(cachedVisibleSelection());
    d.renderEngine->setSelectionColor(ui()->color(UI::ColorRole::Selection));
    d.renderEngine->setSecondarySelectionColor(ui()->color(UI::ColorRole::SelectionAlt));
    updateRenderEngineSettings();

    if (!d.renderEngine->initialize()) {
        d.sceneStats = QImage();
        d.performanceStats = QImage();
        d.auxiliary->clearAxis();
    }
}

void
ViewportWidgetPrivate::initContext()
{
    connect(selectionList(), &SelectionList::selectionChanged, this, &ViewportWidgetPrivate::updateSelection);
    connect(viewCamera(), &ViewCamera::cameraChanged, this, &ViewportWidgetPrivate::updateCamera);
    connect(viewState(), &ViewState::backgroundColorChanged, this, [this](const QColor&) { d.glwidget->update(); });
    connect(viewState(), &ViewState::gridColorChanged, this, [this](const QColor&) {
        updateAuxiliaryGrid();
        if (d.renderEngine)
            d.renderEngine->refreshAuxiliaryStage();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::gridEnabledChanged, this, [this](bool) {
        updateAuxiliaryGrid();
        if (d.renderEngine)
            d.renderEngine->refreshAuxiliaryStage();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::materialModeChanged, this, [this](ViewState::MaterialMode) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::overrideMaterialChanged, this, [this](const SdfPath&) {
        if (d.renderEngine)
            d.renderEngine->refreshAuxiliaryStage();
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::sceneMaterialsEnabledChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::doubleSidedModeChanged, this, [this](ViewState::DoubleSidedMode) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::defaultCameraLightEnabledChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::defaultDomeLightEnabledChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::domeLightTextureChanged, this, [this](const QString&) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::domeLightCameraVisibilityChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionEnabledChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionContactAmountChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionContactRadiusChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionBroadAmountChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionBroadRadiusChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionNormalBiasChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionFalloffChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionContrastChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionEdgeSharpnessChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionBlurEnabledChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionBlurRadiusChanged, this, [this](float) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionQualityChanged, this, [this](ViewState::AmbientOcclusionQuality) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::ambientOcclusionDebugModeChanged, this,
            [this](ViewState::AmbientOcclusionDebugMode) {
                updateRenderEngineSettings();
                d.glwidget->update();
            });
    connect(viewState(), &ViewState::sceneLightsEnabledChanged, this, [this](bool) {
        updateRenderEngineSettings();
        d.glwidget->update();
    });
    connect(viewState(), &ViewState::renderModeChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::complexityLevelChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::rendererAovChanged, d.glwidget, qOverload<>(&QWidget::update));
    connect(viewState(), &ViewState::sceneStatsEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            updateSceneStats();
        else
            d.sceneStats = QImage();
        updateOverlay();
    });
    connect(viewState(), &ViewState::performanceStatsEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            updatePerformanceStats();
        else
            d.performanceStats = QImage();
        updateOverlay();
    });
    connect(ui(), &UI::axisSizeChanged, this, [this](UI::UIScale scale) {
        if (scale == UI::UIScale::Medium && viewState()->cameraAxisEnabled()) {
            updateAxis();
            updateOverlay();
        }
    });
    connect(viewState(), &ViewState::cameraAxisEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            updateAxis();
        else
            d.auxiliary->clearAxis();
        updateOverlay();
    });
    ensureAuxiliaryMaterials();
    updateRenderEngineSettings();
}

SelectionList*
ViewportWidgetPrivate::selectionList()
{
    return d.context->selectionList();
}

ViewCamera*
ViewportWidgetPrivate::viewCamera()
{
    return d.context->viewState()->camera();
}

ViewState*
ViewportWidgetPrivate::viewState()
{
    return d.context ? d.context->viewState() : nullptr;
}

void
ViewportWidgetPrivate::close()
{
    d.selectionTool->reset();

    d.mask.clear();
    d.selectionTool->invalidateVisibleSelection();
    d.selection.clear();
    d.selectionTool->invalidateVisibleSelection();
    d.visibleCapture.clear();
    d.visibleCaptureSet.clear();
    d.stage = nullptr;
    d.lights->invalidate();
    d.cachedSceneTotals = {};
    d.cachedSelectedTotals = {};
    d.selectedTotalsDirty = true;
    d.sceneTotalsDirty = true;
    d.sceneStats = QImage();
    d.performanceStats = QImage();
    d.bbox = GfBBox3d();
    d.selectionBBox = GfBBox3d();
    d.drag = false;
    d.selectionTool->cancel();
    d.suppressContextMenu = false;

    if (d.renderEngine)
        d.renderEngine->reset();

    if (viewState() && viewState()->sceneStatsEnabled())
        updateSceneStats();

    if (viewState() && viewState()->performanceStatsEnabled())
        updatePerformanceStats();

    d.glwidget->update();
}

void
ViewportWidgetPrivate::paintGL()
{
    PerfScope perfScope("viewport.paint");
    QElapsedTimer timer;
    timer.start();
    if (!d.renderEngine || !d.renderEngine->isInitialized())
        initGL();

    if (!d.stage || !d.renderEngine || !d.renderEngine->isInitialized())
        return;

    if (!viewCamera()->aspectRatioLocked())
        viewCamera()->setAspectRatio(widgetAspectRatio());

    d.transform->updateGeometry();
    d.renderEngine->setStage(d.stage);
    d.renderEngine->setAuxiliaryStage(d.auxiliary->stage());
    d.renderEngine->setCamera(viewCamera()->camera());
    d.renderEngine->setInteractiveNavigation(d.navigating);
    d.renderEngine->setSize(widgetSize());
    d.renderEngine->setDevicePixelRatio(d.glwidget->devicePixelRatioF());
    d.renderEngine->setViewport(renderViewport());
    d.renderEngine->setMask(d.mask);


    d.renderEngine->setSelected(cachedVisibleSelection());
    d.renderEngine->setSelectionColor(ui()->color(UI::ColorRole::Selection));
    d.renderEngine->setSecondarySelectionColor(ui()->color(UI::ColorRole::SelectionAlt));
    updateRenderEngineSettings();

    QElapsedTimer gpuTimer;
    gpuTimer.start();
    TfErrorMark mark;
    bool rendered = false;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.stage)
            rendered = d.renderEngine->renderToCurrentFramebuffer();
    }
    if (!mark.IsClean())
        mark.Clear();
    if (!rendered)
        return;

    d.gpuPerformanceMs = gpuTimer.nsecsElapsed() / 1e6;
    const bool converged = d.renderEngine->isConverged();
    d.glwidget->setProperty("renderConverged", converged);
    if (!converged)
        d.glwidget->update();
    d.count++;

    Q_EMIT d.glwidget->renderReady(timer.elapsed());
    if (viewState() && viewState()->performanceStatsEnabled())
        updatePerformanceStats();
}

void
ViewportWidgetPrivate::updateOverlay()
{
    if (d.overlay)
        d.overlay->update();
}

void
ViewportWidgetPrivate::paintOverlay(QPainter& painter)
{
    if (!d.stage) {
        QColor background = Qt::black;
        if (viewState() && viewState()->backgroundColor().isValid())
            background = viewState()->backgroundColor();

        painter.fillRect(d.glwidget->rect(), background);
    }

    drawLetterbox(painter);

    d.selectionTool->draw(painter);

    d.lights->draw(painter);

    d.transform->draw(painter);

    if (viewState() && viewState()->sceneStatsEnabled()) {
        painter.drawImage(QPoint(0, 0), d.sceneStats);
    }

    if (viewState() && viewState()->performanceStatsEnabled()) {
        QPoint pos(d.glwidget->width() - d.performanceStats.width() / d.performanceStats.devicePixelRatio(), 0);
        painter.drawImage(pos, d.performanceStats);
    }

    if (viewState() && viewState()->cameraAxisEnabled()) {
        const int margin = qRound(ui()->axisSize(UI::UIScale::Medium) / 9.0);
        const int axisHeight = qRound(d.auxiliary->axisImage().height() / d.auxiliary->axisImage().devicePixelRatio());
        painter.drawImage(QPoint(margin, d.glwidget->height() - margin - axisHeight), d.auxiliary->axisImage());
    }

    drawBorder(painter);
}

void
ViewportWidgetPrivate::focusEvent(QMouseEvent* event)
{
    if (!d.stage || !d.renderEngine || !viewCamera())
        return;

    d.glwidget->makeCurrent();
    const QPoint devicePos = deviceRatio(event->pos());
    const GfVec2i framebufferSize = widgetSize();
    const GfVec2i pixel(devicePos.x(), framebufferSize[1] - 1 - devicePos.y());
    if (pixel[0] < 0 || pixel[1] < 0 || pixel[0] >= framebufferSize[0] || pixel[1] >= framebufferSize[1])
        return;

    GpuPickResult pick;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;
        pick = d.renderEngine->gpuPickAt(pixel, {});
    }
    if (pick.path.IsEmpty() || !d.selectionTool->isPathMaskedIn(pick.path) || !std::isfinite(pick.depth)
        || pick.depth < 0.0f || pick.depth >= 1.0f)
        return;

    const GfFrustum frustum = viewCamera()->camera().GetFrustum();
    const GfMatrix4d inverseViewProjection
        = (frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix()).GetInverse();
    const GfVec4d viewport = widgetViewport();
    if (viewport[2] <= 0.0 || viewport[3] <= 0.0)
        return;

    // Pixel centers in framebuffer coordinates (origin at bottom left).
    const double ndcX = 2.0 * (double(pixel[0]) + 0.5 - viewport[0]) / viewport[2] - 1.0;
    const double ndcY = 2.0 * (double(pixel[1]) + 0.5 - viewport[1]) / viewport[3] - 1.0;
    const double ndcZ = 2.0 * double(pick.depth) - 1.0;
    const GfVec4d world = GfVec4d(ndcX, ndcY, ndcZ, 1.0) * inverseViewProjection;
    if (std::abs(world[3]) <= 1e-12)
        return;
    const GfVec3d hit(world[0] / world[3], world[1] / world[3], world[2] / world[3]);
    viewCamera()->setFocusPoint(hit);
    d.glwidget->update();
}

void
ViewportWidgetPrivate::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(mime::material)) {
        if (event)
            event->ignore();
        return;
    }

    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void
ViewportWidgetPrivate::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(mime::material)) {
        if (event)
            event->ignore();
        return;
    }

    const SdfPath path = d.selectionTool->pickNearestPath(event->position().toPoint());
    if (path.IsEmpty()) {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void
ViewportWidgetPrivate::dropEvent(QDropEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(mime::material) || !d.context || !d.stage) {
        if (event)
            event->ignore();
        return;
    }

    const QString materialText = QString::fromUtf8(event->mimeData()->data(mime::material)).trimmed();
    const SdfPath materialPath(materialText.toStdString());
    if (materialPath.IsEmpty() || !materialPath.IsAbsolutePath() || !materialPath.IsPrimPath()) {
        event->ignore();
        return;
    }
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage) {
            event->ignore();
            return;
        }

        const UsdPrim materialPrim = d.stage->GetPrimAtPath(materialPath);
        if (!materialPrim || !materialPrim.IsA<UsdShadeMaterial>()) {
            event->ignore();
            return;
        }
    }

    const QPoint dropPosition = event->position().toPoint();
    const SdfPath targetPath = d.selectionTool->pickNearestPath(dropPosition);
    if (targetPath.IsEmpty()) {
        event->ignore();
        return;
    }

    d.context->execute(new Command(bindMaterial({ targetPath }, materialPath)));

    event->setDropAction(Qt::CopyAction);
    event->accept();
    d.glwidget->update();
}

void
ViewportWidgetPrivate::contextMenuEvent(QContextMenuEvent* event)
{
    if (!event || !d.stage || !d.context)
        return;

    if (d.suppressContextMenu) {
        d.suppressContextMenu = false;
        event->accept();
        return;
    }

    const QList<SdfPath> paths = d.selection;

    SdfPath createParentPath = SdfPath::AbsoluteRootPath();
    if (paths.size() == 1)
        createParentPath = paths.first();
    else if (paths.size() > 1)
        createParentPath = paths.first().GetParentPath();

    ContextMenu::exec(d.glwidget, d.context, d.stage, event->globalPos(), paths, d.mask, createParentPath);
    event->accept();
}

void
ViewportWidgetPrivate::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
        focusEvent(event);
    }
}

void
ViewportWidgetPrivate::mousePressEvent(QMouseEvent* event)
{
    if (!d.stage)
        return;

    if (event->button() == Qt::LeftButton && viewState() && viewState()->cameraAxisEnabled()
        && !d.auxiliary->axisImage().isNull()) {
        const int margin = qRound(ui()->axisSize(UI::UIScale::Medium) / 9.0);
        const int axisHeight = qRound(d.auxiliary->axisImage().height() / d.auxiliary->axisImage().devicePixelRatio());
        const QPointF local = event->position() - QPointF(margin, d.glwidget->height() - margin - axisHeight);
        const int axis = d.auxiliary->hitTestAxis(local, viewCamera()->camera());
        if (axis >= 0) {
            d.drag = false;
            d.selectionTool->cancel();
            d.mousepos = event->pos();
            // Use the shared camera directly; the stage-up convention is
            // already represented by ViewCamera::cameraUp().
            ViewCamera* camera = viewCamera();
            if (camera) {
                const bool zUp = camera->cameraUp() == ViewCamera::Z;
                ViewCamera::CameraView view;
                switch (axis) {
                case 0: view = ViewCamera::SideView; break;
                case 1: view = zUp ? ViewCamera::FrontView : ViewCamera::TopView; break;
                case 2: view = zUp ? ViewCamera::TopView : ViewCamera::FrontView; break;
                default: return;
                }
                camera->setCameraView(view);
            }
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::MiddleButton) {
        d.drag = true;
        d.selectionTool->cancel();
        d.transform->endDrag();
        viewCamera()->setCameraMode(ViewCamera::Truck);
    }
    else if (event->button() == Qt::LeftButton) {
        // Shift at press starts a Move copy gesture. Other tools keep Shift snapping.
        d.transform->setSnap(bool(event->modifiers() & Qt::ShiftModifier));
        if (d.transform->beginDrag(event->position(), bool(event->modifiers() & Qt::ShiftModifier))) {
            d.drag = false;
            d.selectionTool->cancel();
            d.glwidget->setFocus(Qt::MouseFocusReason);
        }
        else if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
            d.drag = true;
            d.selectionTool->cancel();
            d.transform->endDrag();
            viewCamera()->setCameraMode(ViewCamera::Tumble);
        }
        else {
            d.drag = false;
            d.selectionTool->begin(event->pos());
        }
    }
    else if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
        d.drag = true;
        d.selectionTool->cancel();
        d.transform->endDrag();
        if (event->button() == Qt::RightButton)
            viewCamera()->setCameraMode(ViewCamera::Zoom);
    }
    else if (event->button() == Qt::RightButton) {
        // Plain right mouse is reserved for the context menu. It must not
        // behave like a viewport click, start a sweep, or alter selection.
        d.drag = false;
        d.selectionTool->cancel();
        d.transform->endDrag();
    }
    d.mousepos = event->pos();
}

void
ViewportWidgetPrivate::mouseMoveEvent(QMouseEvent* event)
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
            if (!delta.isNull())
                viewCamera()->tumble(0.25 * delta.x(), 0.25 * delta.y());
        }
        else if (viewCamera()->cameraMode() == ViewCamera::Zoom) {
            double factor = -.002 * (delta.x() + delta.y());
            viewCamera()->distance(1 + factor);
        }
        d.glwidget->update();
    }
    else if (d.transform->dragging()) {
        const Qt::KeyboardModifiers mouseMods = event->modifiers();
        d.transform->setSnap(bool(mouseMods & Qt::ShiftModifier));
        d.transform->updateDrag(event->position());
    }
    else if (d.selectionTool->active()) {
        d.selectionTool->update(event->pos());
    }
    else {
        d.transform->updateHover(event->position());
    }
    d.mousepos = event->pos();
}

void
ViewportWidgetPrivate::mouseReleaseEvent(QMouseEvent* event)
{
    if (!d.stage)
        return;

    if (d.drag) {
        d.suppressContextMenu = event->button() == Qt::RightButton;
        d.drag = false;
        viewCamera()->setCameraMode(ViewCamera::None);
        d.glwidget->update();
    }
    else if (d.transform->dragging()) {
        d.transform->setSnap(bool(event->modifiers() & Qt::ShiftModifier));
        d.transform->updateDrag(event->position());
        d.transform->endDrag();
    }
    else if (d.selectionTool->active()) {
        d.selectionTool->end(event);
    }
}

bool
ViewportWidgetPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (object == d.glwidget && event && event->type() == QEvent::Resize && d.overlay) {
        d.overlay->setGeometry(d.glwidget->rect());
        d.overlay->raise();
    }

    if (event && d.transform->dragging()
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::ShortcutOverride)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && keyEvent->key() == Qt::Key_Shift && !keyEvent->isAutoRepeat()) {
            d.transform->setSnap(event->type() == QEvent::KeyPress);
            d.transform->updateDrag(QPointF(d.mousepos));
        }
    }
#ifdef Q_OS_MAC
    if (object == d.glwidget && event && event->type() == QEvent::NativeGesture) {
        auto* gesture = static_cast<QNativeGestureEvent*>(event);

        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            if (!viewCamera())
                return false;

            // macOS trackpad pinch:
            // pinch inward  -> zoom out
            // pinch outward -> zoom in
            const double delta = std::clamp(gesture->value(), -0.5, 0.5);
            viewCamera()->distance(1.0 - delta);

            event->accept();
            d.glwidget->update();
            return true;
        }
    }
#else
    Q_UNUSED(object);
    Q_UNUSED(event);
#endif

    return QObject::eventFilter(object, event);
}

void
ViewportWidgetPrivate::wheelEvent(QWheelEvent* event)
{
    if (!event || !viewCamera())
        return;

    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();

    const QPointingDevice* device = event->pointingDevice();
    const bool isTrackpad = device && device->type() == QInputDevice::DeviceType::TouchPad;

    if (isTrackpad && !pixelDelta.isNull()) {
        if (event->modifiers() & Qt::ShiftModifier) {
            const double delta = static_cast<double>(pixelDelta.y()) / 300.0;
            const double clamped = std::clamp(delta, -0.5, 0.5);
            viewCamera()->distance(1.0 - clamped);
        }
        else {
            const double height = std::max(1, widgetSize()[1]);
            const double factor = viewCamera()->mapToFrustumHeight(height);

            viewCamera()->truck(-static_cast<double>(pixelDelta.x()) * factor,
                                static_cast<double>(pixelDelta.y()) * factor);
        }

        event->accept();
        d.glwidget->update();
        return;
    }

    QPoint zoomDelta = angleDelta;
    double scale = 1000.0;

    if (zoomDelta.isNull() && !pixelDelta.isNull()) {
        zoomDelta = pixelDelta;
        scale = 300.0;
    }

    if (zoomDelta.isNull()) {
        event->ignore();
        return;
    }

    const double delta = static_cast<double>(zoomDelta.y()) / scale;
    const double clamped = std::clamp(delta, -0.5, 0.5);
    viewCamera()->distance(1.0 - clamped);

    event->accept();
    d.glwidget->update();
}

void
ViewportWidgetPrivate::updateStage(UsdStageRefPtr stage)
{
    SignalGuard::Scope guard(this);
    d.stage = stage;
    d.lights->invalidate();
    d.sceneTotalsDirty = true;
    d.cachedSceneTotals = {};
    d.cachedSelectedTotals = {};
    d.selectedTotalsDirty = true;
    d.selectionTool->reset();
    d.selectionTool->invalidateVisibleSelection();
    d.transform->invalidate();
    d.visibleCapture.clear();
    d.visibleCaptureSet.clear();
    if (d.renderEngine)
        d.renderEngine->setStage(stage);

    ensureAuxiliaryMaterials();
    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }

    updateAuxiliaryGrid();
    d.glwidget->update();
    updateAxis();
}

void
ViewportWidgetPrivate::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    SignalGuard::Scope guard(this);
    if (auxiliary == d.auxiliary->stage())
        return;

    d.auxiliary->setStage(auxiliary);
    d.transform->auxiliaryChanged();
    d.transform->invalidate();
    ensureAuxiliaryMaterials();
    updateAuxiliaryGrid();
    if (d.renderEngine)
        d.renderEngine->setAuxiliaryStage(auxiliary);

    d.glwidget->update();
}

void
ViewportWidgetPrivate::updateStageUp(const TfToken& upAxis)
{
    const TfToken normalizedAxis = (upAxis == UsdGeomTokens->z) ? UsdGeomTokens->z : UsdGeomTokens->y;
    if (d.auxiliary->upAxis() == normalizedAxis)
        return;

    d.auxiliary->setUpAxis(normalizedAxis);
    updateAuxiliaryGrid();
    if (d.renderEngine)
        d.renderEngine->refreshAuxiliaryStage();

    d.glwidget->update();
}

void
ViewportWidgetPrivate::updateBoundingBox(const GfBBox3d& bbox)
{
    SignalGuard::Scope guard(this);
    d.bbox = bbox;
    d.glwidget->update();
}

void
ViewportWidgetPrivate::updateMask(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    if (d.transform->dragging())
        d.transform->endDrag();
    d.transform->invalidate();
    d.transform->clearHover();
    d.mask = paths;
    d.selectionTool->invalidateVisibleSelection();

    if (d.renderEngine) {
        d.renderEngine->setMask(d.mask);
        d.renderEngine->setSelected(cachedVisibleSelection());
    }

    d.glwidget->update();
}

void
ViewportWidgetPrivate::updatePrims(const NoticeBatch& batch)
{
    if (batch.entries.isEmpty())
        d.lights->invalidate();
    for (const auto& entry : batch.entries) {
        if ((!entry.changedInfoOnly && !entry.path.IsPropertyPath())
            || std::find(entry.changedFields.begin(), entry.changedFields.end(), TfToken("typeName"))
                   != entry.changedFields.end()
            || std::find(entry.changedFields.begin(), entry.changedFields.end(), TfToken("apiSchemas"))
                   != entry.changedFields.end()) {
            d.lights->invalidate();
            break;
        }
    }
    // Counts depend on topology/composition, not the transform stack. Preserve
    // totals for ordinary drag updates; any unknown change remains conservative.
    const bool countsChanged = !notice::onlyTransformProperties(batch);
    d.sceneTotalsDirty = d.sceneTotalsDirty || countsChanged;
    d.selectedTotalsDirty = d.selectedTotalsDirty || countsChanged;
    d.selectionTool->invalidateVisibleSelection();
    d.selectionTool->invalidateStage();
    {
        SignalGuard::Scope guard(this);
        if (d.renderEngine)
            d.renderEngine->invalidateSelectionCache();
    }

    // A transform changed outside the active gizmo operation (for example Undo
    // or Redo), so the cached manipulation pivot may no longer match the stage.
    // Keep it stable while flushing this gizmo's own deferred notices to avoid
    // recomputing the lead object's pivot midway through an interactive edit.
    const bool invalidatePivot = !d.transform->dragging() && !d.transform->deferringUpdates();
    if (invalidatePivot) {
        d.transform->invalidate();
    }

    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }
    d.glwidget->update();
}

QList<SdfPath>
ViewportWidgetPrivate::capturePaths(bool accumulate)
{
    QElapsedTimer timer;
    timer.start();

    if (!d.stage || !d.renderEngine)
        return {};

#ifdef WIN32
    glDepthMask(GL_TRUE);
#endif

    // Visible capture shares the Stageviz scene-ID pass used by selection
    // outlines. RenderEngine performs a tiled high-coverage primId scan and
    // returns the unique front-most document prim paths from the current view.
    QList<SdfPath> captured;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        captured = d.renderEngine->captureVisiblePaths(4);
    }

    bool changed = false;
    if (accumulate) {
        for (const SdfPath& path : captured) {
            if (!d.visibleCaptureSet.contains(path)) {
                d.visibleCaptureSet.insert(path);
                d.visibleCapture.append(path);
                changed = true;
            }
        }
    }

    if (changed && viewState() && viewState()->sceneStatsEnabled())
        updateSceneStats();

    if (changed)
        d.glwidget->update();

    Q_EMIT d.glwidget->captureReady(timer.elapsed());
    return accumulate ? d.visibleCapture : captured;
}

void
ViewportWidgetPrivate::clearVisibleCapture()
{
    if (d.visibleCapture.isEmpty())
        return;

    d.visibleCapture.clear();
    d.visibleCaptureSet.clear();
    if (viewState() && viewState()->sceneStatsEnabled())
        updateSceneStats();
    d.glwidget->update();
}

void
ViewportWidgetPrivate::navigationIdle()
{
    d.navigating = false;
    if (d.glwidget)
        d.glwidget->update();
}

void
ViewportWidgetPrivate::updateCamera(const GfCamera& camera)
{
    Q_UNUSED(camera);
    d.navigating = true;
    d.navigationTimer.start();
    d.selectionTool->resetPickCycle();
    d.glwidget->update();
    updateAxis();
}

void
ViewportWidgetPrivate::updateSelection(const QList<SdfPath>& paths)
{
    PerfScope perfScope("ui.viewport_selection");
    {
        SignalGuard::Scope guard(this);
        d.selection = paths;
        d.selectedTotalsDirty = true;
        d.selectionTool->invalidateVisibleSelection();
    }
    // The guard blocks signals on the GL widget. Notify the transform after
    // it has been released so the gizmo panel receives the refreshed TRS for
    // the new selection as well as the selection-state notifications below.
    d.transform->selectionChanged(paths.isEmpty());
    Q_EMIT d.glwidget->transformStateChanged();
    Q_EMIT d.glwidget->transformSelectionChanged();

    if (d.renderEngine) {
        const QList<SdfPath>& visible = cachedVisibleSelection();
        d.renderEngine->setSelected(visible);
    }
    d.glwidget->update();
    if (viewState() && viewState()->sceneStatsEnabled()) {
        QTimer::singleShot(16, this, [this]() {
            if (!viewState() || !viewState()->sceneStatsEnabled())
                return;
            updateSceneStats();
            d.glwidget->update();
        });
    }
}

void
ViewportWidgetPrivate::updateRenderEngineSettings()
{
    if (!d.renderEngine)
        return;

    ViewState* state = viewState();
    if (!state)
        return;

    RenderEngine::Settings settings = d.renderEngine->settings();
    settings.timeCode = d.timeCode;
    settings.clearColor = state->backgroundColor().isValid() ? state->backgroundColor() : QColor(Qt::black);
    settings.aov = QStringToTfToken(state->rendererAov().isEmpty() ? QStringLiteral("color") : state->rendererAov());
    settings.drawMode = state->renderMode() == ViewState::Wireframe ? UsdImagingGLDrawMode::DRAW_WIREFRAME_ON_SURFACE
                                                                    : UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
    switch (state->complexityLevel()) {
    case ViewState::Low: settings.complexity = 1.0; break;
    case ViewState::Medium: settings.complexity = 1.1; break;
    case ViewState::High: settings.complexity = 1.2; break;
    case ViewState::VeryHigh: settings.complexity = 1.3; break;
    }

    switch (state->doubleSidedMode()) {
    case ViewState::Primitive: settings.doubleSidedMode = RenderEngine::DoubleSidedMode::Primitive; break;
    case ViewState::SingleSided: settings.doubleSidedMode = RenderEngine::DoubleSidedMode::SingleSided; break;
    case ViewState::DoubleSided:
    default: settings.doubleSidedMode = RenderEngine::DoubleSidedMode::DoubleSided; break;
    }

    switch (state->materialMode()) {
    case ViewState::Clay: settings.materialMode = RenderEngine::MaterialMode::Clay; break;
    case ViewState::Override: settings.materialMode = RenderEngine::MaterialMode::Override; break;
    case ViewState::All:
    default: settings.materialMode = RenderEngine::MaterialMode::Scene; break;
    }

    settings.overrideMaterial = state->overrideMaterial();
    settings.sceneLightsEnabled = state->sceneLightsEnabled();
    settings.sceneMaterialsEnabled = state->sceneMaterialsEnabled();
    settings.defaultCameraLightEnabled = state->defaultCameraLightEnabled();
    settings.defaultDomeLightEnabled = state->defaultDomeLightEnabled();
    settings.domeLightTexture = state->domeLightTexture();
    settings.domeLightCameraVisibility = state->domeLightCameraVisibility();
    // Copy the canonical AO state as one unit so renderer defaults and viewport
    // controls cannot drift apart as new look controls are added.
    settings.ambientOcclusion = state->ambientOcclusionSettings();
    settings.defaultAmbient = d.defaultAmbient;
    settings.defaultSpecular = d.defaultSpecular;
    settings.defaultShininess = d.defaultShininess;
    d.renderEngine->setSettings(settings);
}

bool
ViewportWidgetPrivate::projectWorldToScreen(const GfVec3d& world, QPointF& screen)
{
    if (!d.context || !viewCamera())
        return false;

    const GfCamera camera = viewCamera()->camera();
    const GfFrustum frustum = camera.GetFrustum();
    const GfMatrix4d view = frustum.ComputeViewMatrix();
    const GfMatrix4d projection = frustum.ComputeProjectionMatrix();
    const GfVec3d cameraPoint = view.Transform(world);

    // GfCamera looks down -Z in camera space. Reject points behind the eye or
    // outside the camera clipping range before projecting them into the 2D
    // painter overlay. Otherwise perspective projection may turn points behind
    // or extremely close to the camera into very large screen-space values.
    const double depth = -cameraPoint[2];
    const double nearClip = std::max(1e-8, viewCamera()->nearClipping());
    const double farClip = viewCamera()->farClipping();

    if (!std::isfinite(depth) || depth <= nearClip)
        return false;

    if (std::isfinite(farClip) && farClip > nearClip && depth >= farClip)
        return false;

    const GfVec3d ndc = projection.Transform(cameraPoint);
    if (!std::isfinite(ndc[0]) || !std::isfinite(ndc[1]) || !std::isfinite(ndc[2]))
        return false;

    const QRectF gate = cameraGateRect();
    screen.setX(gate.left() + (ndc[0] * 0.5 + 0.5) * gate.width());
    screen.setY(gate.top() + (1.0 - (ndc[1] * 0.5 + 0.5)) * gate.height());

    return std::isfinite(screen.x()) && std::isfinite(screen.y());
}

QPoint
ViewportWidgetPrivate::deviceRatio(QPoint value) const
{
    return QPoint(deviceRatio(value.x()), deviceRatio(value.y()));
}

double
ViewportWidgetPrivate::deviceRatio(double value) const
{
    return value * d.glwidget->devicePixelRatio();
}

double
ViewportWidgetPrivate::widgetAspectRatio() const
{
    GfVec2i size = widgetSize();
    double width = static_cast<double>(size[0]);
    double height = static_cast<double>(size[1]);
    return width / std::max(1.0, height);
}

GfVec2i
ViewportWidgetPrivate::widgetSize() const
{
    int w = deviceRatio(d.glwidget->width());
    int h = deviceRatio(d.glwidget->height());
    return GfVec2i(w, h);
}

QRectF
ViewportWidgetPrivate::cameraGateRect()
{
    const QRectF widgetRect = d.glwidget->rect();
    if (!viewCamera() || !viewCamera()->aspectRatioLocked())
        return widgetRect;

    const double aspect = viewCamera()->aspectRatio();
    if (!std::isfinite(aspect) || aspect <= 0.0 || widgetRect.width() <= 0.0 || widgetRect.height() <= 0.0)
        return widgetRect;

    const double widgetAspect = widgetRect.width() / widgetRect.height();
    if (std::abs(widgetAspect - aspect) < 1e-8)
        return widgetRect;

    if (widgetAspect > aspect) {
        const double width = widgetRect.height() * aspect;
        const double left = widgetRect.left() + (widgetRect.width() - width) * 0.5;
        return QRectF(left, widgetRect.top(), width, widgetRect.height());
    }

    const double height = widgetRect.width() / aspect;
    const double top = widgetRect.top() + (widgetRect.height() - height) * 0.5;
    return QRectF(widgetRect.left(), top, widgetRect.width(), height);
}

GfVec4d
ViewportWidgetPrivate::widgetViewport()
{
    const QRectF gate = cameraGateRect();
    const qreal dpr = d.glwidget->devicePixelRatioF();
    return GfVec4d(gate.left() * dpr, gate.top() * dpr, gate.width() * dpr, gate.height() * dpr);
}

GfVec4d
ViewportWidgetPrivate::renderViewport()
{
    const QRectF gate = cameraGateRect();
    const qreal dpr = d.glwidget->devicePixelRatioF();

    // Hydra render viewport coordinates originate at the lower-left while Qt
    // widget coordinates originate at the upper-left.
    const double x = gate.left() * dpr;
    const double y = (d.glwidget->height() - gate.top() - gate.height()) * dpr;
    return GfVec4d(x, y, gate.width() * dpr, gate.height() * dpr);
}

void
ViewportWidgetPrivate::drawLetterbox(QPainter& painter)
{
    if (!viewCamera() || !viewCamera()->aspectRatioLocked())
        return;

    const QRectF gate = cameraGateRect();
    const QRectF widgetRect = d.glwidget->rect();
    if (gate == widgetRect)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);

    if (viewCamera()->letterboxEnabled()) {
        const int alpha = qRound(std::clamp(viewCamera()->letterboxOpacity(), 0.0, 1.0) * 255.0);
        painter.setBrush(QColor(0, 0, 0, alpha));

        if (gate.left() > widgetRect.left())
            painter.drawRect(
                QRectF(widgetRect.left(), widgetRect.top(), gate.left() - widgetRect.left(), widgetRect.height()));
        if (gate.right() < widgetRect.right())
            painter.drawRect(
                QRectF(gate.right(), widgetRect.top(), widgetRect.right() - gate.right(), widgetRect.height()));
        if (gate.top() > widgetRect.top())
            painter.drawRect(QRectF(gate.left(), widgetRect.top(), gate.width(), gate.top() - widgetRect.top()));
        if (gate.bottom() < widgetRect.bottom())
            painter.drawRect(QRectF(gate.left(), gate.bottom(), gate.width(), widgetRect.bottom() - gate.bottom()));
    }

    // inset the guide slightly from the exact render gate so all four edges
    // remain visible even when the gate touches the widget boundary.
    constexpr qreal guideInset = 3.0;
    constexpr qreal guideWidth = 1.5;
    const QRectF guideRect = gate.adjusted(guideInset, guideInset, -guideInset, -guideInset);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ui()->color(UI::ColorRole::Guide), guideWidth));
    painter.drawRect(guideRect);
    painter.restore();
}

void
ViewportWidgetPrivate::drawBorder(QPainter& painter)
{
    const int w = 1;
    painter.setPen(QPen(ui()->color(UI::ColorRole::Border), w));
    painter.setBrush(Qt::NoBrush);
    QRect r = d.glwidget->rect().adjusted(w / 1, w / 1, -w / 1, -w / 1);
    painter.drawRect(r);
}

void
ViewportWidgetPrivate::updateSceneStats()
{
    PerfScope perfScope("ui.scene_statistics");
    auto accumulate = [&](const UsdPrim& prim, ViewportSceneCounts& s) {
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
    ViewportSceneCounts selected = d.cachedSelectedTotals;
    if (d.stage) {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.sceneTotalsDirty) {
            ViewportSceneCounts totals;
            for (const UsdPrim& prim : d.stage->Traverse(UsdTraverseInstanceProxies())) {
                accumulate(prim, totals);
            }
            d.cachedSceneTotals = totals;
            d.sceneTotalsDirty = false;
        }
        if (d.selectedTotalsDirty) {
            selected = {};
            const QList<SdfPath> roots = path::sortedRootPaths(d.selection);
            for (const SdfPath& path : roots) {
                UsdPrim root = d.stage->GetPrimAtPath(path);
                if (!root)
                    continue;

                for (const UsdPrim& prim : UsdPrimRange(root, UsdTraverseInstanceProxies())) {
                    accumulate(prim, selected);
                }
            }
        }
    }
    d.cachedSelectedTotals = selected;
    d.selectedTotalsDirty = false;
    const ViewportSceneCounts& total = d.cachedSceneTotals;
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
    font.setPixelSize(ui()->fontSize(UI::UIScale::Small));
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
        textColor = ui()->color(UI::ColorRole::Text, UI::UIState::Normal);
    }
    else {
        textColor = ui()->color(UI::ColorRole::Text, UI::UIState::Disabled);
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
ViewportWidgetPrivate::updatePerformanceStats()
{
    const bool hasEngine = d.renderEngine && d.renderEngine->isInitialized() && d.stage;
    VtDictionary stats;

    if (d.renderEngine)
        stats = d.renderEngine->renderStats();

    auto fmtMB = [](uint64_t bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 2) + " MB";
    };
    auto statBytes = [&](const TfToken& key, uint64_t& bytes) -> bool {
        bytes = 0;
        const auto it = stats.find(key);
        if (it == stats.end())
            return false;
        const VtValue& value = it->second;
        if (value.IsHolding<unsigned long>()) {
            bytes = static_cast<uint64_t>(value.UncheckedGet<unsigned long>());
            return true;
        }
        if (value.IsHolding<unsigned long long>()) {
            bytes = static_cast<uint64_t>(value.UncheckedGet<unsigned long long>());
            return true;
        }
        if (value.IsHolding<unsigned int>()) {
            bytes = static_cast<uint64_t>(value.UncheckedGet<unsigned int>());
            return true;
        }
        if (value.IsHolding<unsigned short>()) {
            bytes = static_cast<uint64_t>(value.UncheckedGet<unsigned short>());
            return true;
        }
        if (value.IsHolding<long>()) {
            const long v = value.UncheckedGet<long>();
            if (v >= 0) {
                bytes = static_cast<uint64_t>(v);
                return true;
            }
            return false;
        }
        if (value.IsHolding<long long>()) {
            const long long v = value.UncheckedGet<long long>();
            if (v >= 0) {
                bytes = static_cast<uint64_t>(v);
                return true;
            }
            return false;
        }
        if (value.IsHolding<int>()) {
            const int v = value.UncheckedGet<int>();
            if (v >= 0) {
                bytes = static_cast<uint64_t>(v);
                return true;
            }
            return false;
        }
        if (value.IsHolding<short>()) {
            const short v = value.UncheckedGet<short>();
            if (v >= 0) {
                bytes = static_cast<uint64_t>(v);
                return true;
            }
            return false;
        }
        return false;
    };
    struct Row {
        QString label;
        QString value;
    };
    QVector<Row> rows;
    if (d.renderEngine && !d.renderEngine->hgiApiName().isEmpty())
        rows.append({ "Hgi", d.renderEngine->hgiApiName() });
    else
        rows.append({ "Hgi", QStringLiteral("-") });
    rows.append({ "GPU time", hasEngine ? QString::number(d.gpuPerformanceMs, 'f', 2) + " ms" : "-" });
    if (hasEngine) {
        uint64_t bytes = 0;
        if (statBytes(TfToken("gpuMemoryUsed"), bytes)) {
            rows.append({ "GPU mem", fmtMB(bytes) });
        }
        if (statBytes(TfToken("primvar"), bytes)) {
            rows.append({ "primvar", fmtMB(bytes) });
        }
        if (statBytes(TfToken("topology"), bytes)) {
            rows.append({ "topology", fmtMB(bytes) });
        }
        if (statBytes(TfToken("drawingShader"), bytes)) {
            rows.append({ "shader", fmtMB(bytes) });
        }
        if (statBytes(TfToken("textureMemory"), bytes)) {
            rows.append({ "texture", fmtMB(bytes) });
        }
    }
    const double dpr = d.glwidget->devicePixelRatioF();
    QFont font = d.glwidget->font();
    font.setPixelSize(ui()->fontSize(UI::UIScale::Small));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    QFontMetrics fm(font);
    const int rowHeight = fm.lineSpacing() + 2;
    const int marginLeft = 18;
    const int marginRight = 18;
    const int marginTop = 16;
    const int columnSpacing = 24;
    int labelWidth = 0;
    int valueWidth = 0;
    for (const Row& row : rows) {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(row.label));
        valueWidth = std::max(valueWidth, fm.horizontalAdvance(row.value));
    }
    const int width = marginLeft + labelWidth + columnSpacing + valueWidth + marginRight;
    const int height = static_cast<int>(rows.size()) * rowHeight + marginTop;
    d.performanceStats = QImage(qRound(width * dpr), qRound(height * dpr), QImage::Format_ARGB32_Premultiplied);
    d.performanceStats.setDevicePixelRatio(dpr);
    d.performanceStats.fill(Qt::transparent);
    QPainter painter(&d.performanceStats);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    const QColor textColor = d.stage ? ui()->color(UI::ColorRole::Text, UI::UIState::Normal)
                                     : ui()->color(UI::ColorRole::Text, UI::UIState::Disabled);
    const QColor shadowColor(0, 0, 0, 160);
    int y = marginTop + fm.ascent();
    const int labelX = marginLeft;
    const int valueRight = width - marginRight;
    for (const Row& row : rows) {
        const QRect labelRect(labelX, y - fm.ascent(), labelWidth, rowHeight);
        const QRect valueRect(valueRight - valueWidth, y - fm.ascent(), valueWidth, rowHeight);
        painter.setPen(shadowColor);
        painter.drawText(labelRect.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, row.label);
        painter.drawText(valueRect.translated(1, 1), Qt::AlignRight | Qt::AlignVCenter, row.value);
        painter.setPen(textColor);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, row.label);
        painter.drawText(valueRect, Qt::AlignRight | Qt::AlignVCenter, row.value);
        y += rowHeight;
    }
}

ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , p(new ViewportWidgetPrivate())
{
    p->d.glwidget = this;
    p->init();
}

ViewportWidget::~ViewportWidget() = default;

ViewContext*
ViewportWidget::context() const
{
    return p->d.context;
}

void
ViewportWidget::setContext(ViewContext* context)
{
    if (p->d.context != context) {
        p->d.context = context;
        p->initContext();
    }
}

QImage
ViewportWidget::captureImage()
{
    return QOpenGLWidget::grabFramebuffer();
}

QJsonObject
ViewportWidget::inspectPrim(const SdfPath& path, QString& error)
{
    if (QThread::currentThread() != thread()) {
        error = QStringLiteral("Viewport diagnostics must run on the GUI thread");
        return {};
    }
    if (!p->d.stage || !p->d.renderEngine) {
        error = QStringLiteral("No rendered USD stage is available in the viewport");
        return {};
    }
    READ_LOCKER(locker, p->d.context->stageLock(), "stageLock");
    return p->d.renderEngine->inspectPrim(path, error);
}

ViewportCapture
ViewportWidget::captureViewport(QString& error)
{
    if (QThread::currentThread() != thread()) {
        error = QStringLiteral("Viewport capture must run on the GUI thread");
        return {};
    }
    if (!p->d.stage || !isValid()) {
        error = QStringLiteral("No rendered USD stage is available in the viewport");
        return {};
    }
    QOpenGLContext* previous = QOpenGLContext::currentContext();
    QSurface* surface = previous ? previous->surface() : nullptr;
    makeCurrent();
    auto restore = qScopeGuard([&]() {
        doneCurrent();
        if (previous && surface)
            previous->makeCurrent(surface);
    });
    if (QOpenGLContext::currentContext() != QOpenGLWidget::context()) {
        error = QStringLiteral("Could not make the viewport GL context current");
        return {};
    }
    // Synchronize camera, gate, masks, materials and display settings exactly
    // as a regular frame before taking the clean document-only capture.
    p->paintGL();
    if (!p->d.stage || !p->d.renderEngine) {
        error = QStringLiteral("The viewport stage closed during capture");
        return {};
    }
    READ_LOCKER(locker, p->d.context->stageLock(), "stageLock");
    return p->d.renderEngine->captureViewport(error);
}

bool
ViewportWidget::transformEnabled() const
{
    return p->d.transform->enabled();
}

bool
ViewportWidget::transformHandleAvailable()
{
    return p->d.transform->hasActiveHandle();
}

void
ViewportWidget::setTransformEnabled(bool enabled)
{
    p->d.transform->setEnabled(enabled);
    Q_EMIT transformStateChanged();
}

bool
ViewportWidget::pivotEnabled() const
{
    return p->d.transform->pivotEnabled();
}

void
ViewportWidget::setPivotEnabled(bool enabled)
{
    p->d.transform->setPivotEnabled(enabled);
    Q_EMIT transformStateChanged();
}

ViewportGizmo::ReferenceFrame
ViewportWidget::transformReferenceFrame() const
{
    return p->d.transform->referenceFrame();
}

void
ViewportWidget::setTransformReferenceFrame(ViewportGizmo::ReferenceFrame frame)
{
    p->d.transform->setReferenceFrame(frame);
    Q_EMIT transformStateChanged();
}

bool
ViewportWidget::applyNumericTransform(const GfVec3d& values, QString& error)
{
    return p->d.transform->applyNumeric(values, error);
}

bool
ViewportWidget::beginNumericTransform(QString& error)
{
    return p->d.transform->beginNumericEdit(error);
}

bool
ViewportWidget::updateNumericTransform(const GfVec3d& values, QString& error)
{
    return p->d.transform->updateNumericEdit(values, error);
}

void
ViewportWidget::finishNumericTransform()
{
    p->d.transform->finishNumericEdit();
}

bool
ViewportWidget::moveEnabled() const
{
    return p->d.transform->mode() == ViewportTransform::Mode::Move;
}

void
ViewportWidget::setMoveEnabled(bool enabled)
{
    p->d.transform->setMode(ViewportTransform::Mode::Move, enabled);
    Q_EMIT transformStateChanged();
}

bool
ViewportWidget::rotateEnabled() const
{
    return p->d.transform->mode() == ViewportTransform::Mode::Rotate;
}

void
ViewportWidget::setRotateEnabled(bool enabled)
{
    p->d.transform->setMode(ViewportTransform::Mode::Rotate, enabled);
    Q_EMIT transformStateChanged();
}

bool
ViewportWidget::scaleEnabled() const
{
    return p->d.transform->mode() == ViewportTransform::Mode::Scale;
}

void
ViewportWidget::setScaleEnabled(bool enabled)
{
    p->d.transform->setMode(ViewportTransform::Mode::Scale, enabled);
    Q_EMIT transformStateChanged();
}

void
ViewportWidget::close()
{
    p->close();
}

QList<QString>
ViewportWidget::rendererAovs() const
{
    if (!p->d.renderEngine)
        return {};
    return p->d.renderEngine->rendererAovs();
}

QList<SdfPath>
ViewportWidget::capturePaths(bool accumulate)
{
    if (QThread::currentThread() != thread() || !p->d.stage || !isValid())
        return {};

    QOpenGLContext* previous = QOpenGLContext::currentContext();
    QSurface* surface = previous ? previous->surface() : nullptr;
    makeCurrent();
    auto restore = qScopeGuard([&]() {
        doneCurrent();
        if (previous && surface)
            previous->makeCurrent(surface);
    });
    if (QOpenGLContext::currentContext() != QOpenGLWidget::context())
        return {};

    // Menu and Python captures synchronize the same camera, mask and display
    // settings before scanning, including immediately after Python setters.
    p->paintGL();
    return p->capturePaths(accumulate);
}

void
ViewportWidget::captureVisible()
{
    capturePaths();
}

void
ViewportWidget::clearVisibleCapture()
{
    p->clearVisibleCapture();
}

QList<SdfPath>
ViewportWidget::visibleCapturePaths() const
{
    return p->d.visibleCapture;
}

void
ViewportWidget::updateStage(UsdStageRefPtr stage)
{
    p->updateStage(stage);
}

SdfPath
ViewportWidget::pickLight(const QPointF& pos)
{
    return p->d.lights->pick(pos);
}

QList<SdfPath>
ViewportWidget::lightPathsInRect(const QRectF& rect)
{
    return p->d.lights->pathsInRect(rect);
}

void
ViewportWidget::setTimeCode(double timeCode)
{
    if (qFuzzyCompare(p->d.timeCode + 1.0, timeCode + 1.0))
        return;

    p->d.timeCode = timeCode;
    p->d.lights->setTimeCode(timeCode);
    p->updateRenderEngineSettings();
    update();
}

void
ViewportWidget::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    p->updateAuxiliary(auxiliary);
}

void
ViewportWidget::updateStageUp(const TfToken& upAxis)
{
    p->updateStageUp(upAxis);
}

void
ViewportWidget::updateBoundingBox(const GfBBox3d& bbox)
{
    p->updateBoundingBox(bbox);
}

void
ViewportWidget::updateMask(const QList<SdfPath>& paths)
{
    p->updateMask(paths);
}

void
ViewportWidget::updatePrims(const NoticeBatch& batch)
{
    p->updatePrims(batch);
}

void
ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    p->initGL();
}

void
ViewportWidget::paintGL()
{
    p->paintGL();
}

void
ViewportWidget::paintEvent(QPaintEvent* event)
{
    QOpenGLWidget::paintEvent(event);
}

void
ViewportWidget::dragEnterEvent(QDragEnterEvent* event)
{
    p->dragEnterEvent(event);
}

void
ViewportWidget::dragMoveEvent(QDragMoveEvent* event)
{
    p->dragMoveEvent(event);
}

void
ViewportWidget::dropEvent(QDropEvent* event)
{
    p->dropEvent(event);
}

void
ViewportWidget::contextMenuEvent(QContextMenuEvent* event)
{
    p->contextMenuEvent(event);
}

void
ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    p->mouseDoubleClickEvent(event);
}

void
ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    p->mousePressEvent(event);
}

void
ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    p->mouseMoveEvent(event);
}

void
ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    p->mouseReleaseEvent(event);
}

void
ViewportWidget::wheelEvent(QWheelEvent* event)
{
    p->wheelEvent(event);
}
}  // namespace stageviz
