// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "imagingglwidget.h"
#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "contextmenu.h"
#include "mime.h"
#include "notice.h"
#include "os.h"
#include "paths.h"
#include "qtutils.h"
#include "renderengine.h"
#include "session.h"
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
#include <QObject>
#include <QOpenGLContext>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QPointer>
#include <QPolygonF>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/error.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

constexpr double Pi = 3.14159265358979323846;
constexpr double TransformGizmoSize = 61.5;
constexpr double TransformMoveSnap = 1.0;
constexpr double TransformRotateSnapDegrees = 1.0;
constexpr double TransformScaleSnap = 1.0;

enum class TransformMode { None, Move, Rotate, Scale };


static GfRotation
transformRotationWithoutScale(const GfMatrix4d& matrix)
{
    GfMatrix4d rotationMatrix(matrix);
    for (int row = 0; row < 3; ++row) {
        const double length = std::sqrt(rotationMatrix[row][0] * rotationMatrix[row][0]
                                        + rotationMatrix[row][1] * rotationMatrix[row][1]
                                        + rotationMatrix[row][2] * rotationMatrix[row][2]);
        if (length > 1e-12) {
            rotationMatrix[row][0] /= length;
            rotationMatrix[row][1] /= length;
            rotationMatrix[row][2] /= length;
        }
    }
    return rotationMatrix.ExtractRotation();
}

static GfVec3d
transformEulerWithoutScale(const GfMatrix4d& matrix)
{
    return transformRotationWithoutScale(matrix).Decompose(GfVec3d::XAxis(), GfVec3d::YAxis(), GfVec3d::ZAxis());
}


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
    void contextMenuEvent(QContextMenuEvent* event);
    void dragEnterEvent(QDragEnterEvent* event);
    void dragMoveEvent(QDragMoveEvent* event);
    void dropEvent(QDropEvent* event);
    void mouseDoubleClickEvent(QMouseEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);
    void sweepEvent(const QRect& rect, QMouseEvent* event);
    void wheelEvent(QWheelEvent* event);
    bool eventFilter(QObject* object, QEvent* event) override;
    void updateStage(UsdStageRefPtr stage);
    void updateAuxiliary(UsdStageRefPtr auxiliary);
    void updateStageUp(const TfToken& upAxis);
    void updateBoundingBox(const GfBBox3d& bbox);
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    void updateTransform(bool enabled);
    void updateTransformMode(TransformMode mode, bool enabled);
    void captureVisible();
    void clearVisibleCapture();

public Q_SLOTS:
    void updateCamera(const GfCamera& camera);
    void updateSelection(const QList<SdfPath>& paths);

public:
    void updateAuxiliaryGrid();
    void updateRenderEngineSettings();
    void ensureAuxiliaryMaterials();
    void authorAuxiliaryGridMaterial(const SdfPath& materialPath, const GfVec3f& color);
    void authorAuxiliaryStandardSurface(const SdfPath& materialPath, const GfVec3f& baseColor, float metalness,
                                        float roughness, float specular);

    bool projectWorldToScreen(const GfVec3d& world, QPointF& screen);
    QList<SdfPath> transformVisiblePaths() const;
    bool transformSelectionPivot(GfVec3d& pivot);
    QPointF transformAxisDirection(int axis);
    GfVec3d transformAxisVector(int axis);
    double transformWorldPerPixel();
    double transformWorldPerPixel(const GfVec3d& pivot);
    bool transformRotationScreenBasis(int axis, QPointF& center, QPointF& basisA, QPointF& basisB);
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
    QRectF cameraGateRect();
    GfVec4d widgetViewport();
    GfVec4d renderViewport();
    void drawLetterbox(QPainter& painter);
    void drawBorder(QPainter& painter);
    void updateAxis();
    void updateSceneStats();
    void updatePerformanceStats();
    bool isPathMaskedIn(const SdfPath& path) const;
    bool isSelectionVisible(const SdfPath& path) const;
    SdfPath pickNearestPath(const QPoint& pos);
    SdfPath resolveGeomSubsetTarget(const SdfPath& hitPath, const QPoint& pos);
    bool pickMaskedIntersection(const UsdImagingGLEngine::PickParams& pickParams, const GfFrustum& pickFrustum,
                                UsdImagingGLEngine::IntersectionResultVector* results);
    struct Data {
        size_t count;
        qint64 frame;
        float defaultAmbient;
        float defaultSpecular;
        float defaultShininess;
        double gpuPerformanceMs;
        bool drag;
        bool sweep;
        bool transformEnabled;
        TransformMode transformMode;
        bool transformDragging;
        bool transformSnap;
        bool transformPivotValid;
        bool suppressContextMenu;
        int transformHoverAxis;
        int transformActiveAxis;
        QPointF transformStart;
        double transformRotationStartAngle;
        GfVec3d transformPivot;
        GfVec3d transformStartPivot;
        double transformMetersPerUnit;
        QList<SdfPath> transformPaths;
        XformEdit transformEdit;
        QList<PreparedTransform> transformPrepared;
        bool transformDefersPrimsUpdate;
        Session::PrimsUpdate transformPreviousPrimsUpdate;
        QPoint start;
        QPoint end;
        QPoint mousepos;
        QPoint lastPickPosition;
        QList<SdfPath> lastPickPaths;
        int lastPickIndex;
        QImage sceneStats;
        QImage performanceStats;
        QImage axis;
        GfBBox3d selectionBBox;
        UsdStageRefPtr stage;
        UsdStageRefPtr auxiliary;
        TfToken stageUpAxis;
        GfBBox3d bbox;
        QList<SdfPath> mask;
        QList<SdfPath> selection;
        QList<SdfPath> visibleCapture;
        QScopedPointer<RenderEngine> renderEngine;
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
    d.glwidget->setAcceptDrops(true);
    d.glwidget->setFocusPolicy(Qt::StrongFocus);
    if (QApplication* application = qApp)
        application->installEventFilter(this);
    d.count = 0;
    d.frame = 0;
    d.defaultAmbient = 0.4f;
    d.defaultSpecular = 0.5f;
    d.defaultShininess = 32.0f;
    d.gpuPerformanceMs = 0.0;
    d.drag = false;
    d.sweep = false;
    d.transformEnabled = false;
    d.transformMode = TransformMode::None;
    d.transformDragging = false;
    d.transformSnap = false;
    d.transformPivotValid = false;
    d.suppressContextMenu = false;
    d.transformHoverAxis = 0;
    d.transformActiveAxis = 0;
    d.transformRotationStartAngle = 0.0;
    d.transformDefersPrimsUpdate = false;
    d.transformPreviousPrimsUpdate = Session::PrimsUpdate::Immediate;
    d.lastPickIndex = -1;
    d.transformPivot = GfVec3d(0.0);
    d.transformStartPivot = GfVec3d(0.0);
    d.transformMetersPerUnit = 1.0;
    d.context = nullptr;
    d.stageUpAxis = UsdGeomTokens->y;
}

void
ImagingGLWidgetPrivate::initGL()
{
    if (d.renderEngine && d.renderEngine->isInitialized())
        return;

    if (!QOpenGLContext::currentContext()) {
        qWarning() << "could not initialize render engine, no current OpenGL context";
        return;
    }

    if (!d.renderEngine)
        d.renderEngine.reset(new RenderEngine(RenderEngine::ContextMode::Current));

    d.renderEngine->setStage(d.stage);
    d.renderEngine->setAuxiliaryStage(d.auxiliary);

    QList<SdfPath> visibleSelection;
    visibleSelection.reserve(d.selection.size());
    for (const SdfPath& path : d.selection) {
        if (isSelectionVisible(path))
            visibleSelection.append(path);
    }

    d.renderEngine->setSelected(visibleSelection);
    d.renderEngine->setSelectionColor(style()->color(Style::ColorRole::Selection));
    updateRenderEngineSettings();

    if (!d.renderEngine->initialize()) {
        qWarning() << "could not initialize render engine";
        d.sceneStats = QImage();
        d.performanceStats = QImage();
        d.axis = QImage();
    }
}

void
ImagingGLWidgetPrivate::initContext()
{
    connect(selectionList(), &SelectionList::selectionChanged, this, &ImagingGLWidgetPrivate::updateSelection);
    connect(viewCamera(), &ViewCamera::cameraChanged, this, &ImagingGLWidgetPrivate::updateCamera);
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
    updateRenderEngineSettings();
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
    if (d.transformDefersPrimsUpdate) {
        // Deliver the accumulated transform notices now, but do not rebuild
        // the complete scene bbox synchronously. On large CAD stages that
        // pseudo-root bbox calculation dominated release time by several
        // seconds. Session::boundingBox() still evaluates an exact bound on
        // demand (Frame All), while normal editing stays responsive.
        session()->setPrimsUpdate(d.transformPreviousPrimsUpdate);
        d.transformDefersPrimsUpdate = false;
    }

    d.mask.clear();
    d.selection.clear();
    d.visibleCapture.clear();
    d.stage = nullptr;
    d.bbox = GfBBox3d();
    d.selectionBBox = GfBBox3d();
    d.drag = false;
    d.sweep = false;
    d.transformDragging = false;
    d.transformSnap = false;
    d.transformPivotValid = false;
    d.transformMode = TransformMode::None;
    d.transformEnabled = false;
    d.suppressContextMenu = false;
    d.transformHoverAxis = 0;
    d.transformActiveAxis = 0;
    d.transformRotationStartAngle = 0.0;
    d.transformPivot = GfVec3d(0.0);
    d.transformStartPivot = GfVec3d(0.0);
    d.transformMetersPerUnit = 1.0;
    d.transformEdit = XformEdit();
    d.lastPickPosition = QPoint();
    d.lastPickPaths.clear();
    d.lastPickIndex = -1;

    if (d.renderEngine)
        d.renderEngine->reset();

    if (viewState() && viewState()->sceneStatsEnabled())
        updateSceneStats();

    if (viewState() && viewState()->performanceStatsEnabled())
        updatePerformanceStats();

    d.glwidget->update();
}
void
ImagingGLWidgetPrivate::paintGL()
{
    if (!d.renderEngine || !d.renderEngine->isInitialized())
        initGL();

    if (!d.stage || !d.renderEngine || !d.renderEngine->isInitialized())
        return;

    QElapsedTimer timer;
    timer.start();
    if (!viewCamera()->aspectRatioLocked())
        viewCamera()->setAspectRatio(widgetAspectRatio());

    d.renderEngine->setStage(d.stage);
    d.renderEngine->setAuxiliaryStage(d.auxiliary);
    d.renderEngine->setCamera(viewCamera()->camera());
    d.renderEngine->setSize(widgetSize());
    d.renderEngine->setViewport(renderViewport());
    d.renderEngine->setMask(d.mask);

    QList<SdfPath> visibleSelection;
    visibleSelection.reserve(d.selection.size());
    for (const SdfPath& path : d.selection) {
        if (isSelectionVisible(path))
            visibleSelection.append(path);
    }

    d.renderEngine->setSelected(visibleSelection);
    d.renderEngine->setSelectionColor(style()->color(Style::ColorRole::Selection));
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
        qWarning() << "render engine errors occurred during rendering";
    if (!rendered) {
        qWarning() << "render pass was skipped";
        return;
    }

    d.gpuPerformanceMs = gpuTimer.nsecsElapsed() / 1e6;
    d.count++;

    Q_EMIT d.glwidget->renderReady(timer.elapsed());
    if (viewState() && viewState()->performanceStatsEnabled())
        updatePerformanceStats();
}

void
ImagingGLWidgetPrivate::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(d.glwidget);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!d.stage) {
        QColor background = Qt::black;
        if (viewState() && viewState()->backgroundColor().isValid())
            background = viewState()->backgroundColor();

        painter.fillRect(d.glwidget->rect(), background);
    }

    drawLetterbox(painter);

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
    if (!d.stage || !d.renderEngine)
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
            const bool hit = d.renderEngine->testIntersection(viewMatrix, projectionMatrix, d.stage->GetPseudoRoot(),
                                                              &hitPoint, &hitNormal, &hitPrimPath, &hitInstancerPath);
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
            const bool hit = d.renderEngine->testIntersection(viewMatrix, projectionMatrix, root, &hitPoint, &hitNormal,
                                                              &hitPrimPath, &hitInstancerPath);
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

SdfPath
ImagingGLWidgetPrivate::pickNearestPath(const QPoint& pos)
{
    d.glwidget->makeCurrent();
    if (!d.stage || !d.renderEngine)
        return {};
#ifdef WIN32
    glDepthMask(GL_TRUE);
#endif
    const QPoint devicePos = deviceRatio(pos);
    const GfVec4d viewport = widgetViewport();
    GfVec2d center((devicePos.x() - viewport[0]) / viewport[2], (devicePos.y() - viewport[1]) / viewport[3]);
    center[0] = center[0] * 2.0 - 1.0;
    center[1] = -1.0 * (center[1] * 2.0 - 1.0);
    const GfVec2d size(1.0 / viewport[2], 1.0 / viewport[3]);
    const GfCamera camera = viewCamera()->camera();
    const GfFrustum pickFrustum = camera.GetFrustum().ComputeNarrowedFrustum(center, size);
    const GfMatrix4d viewMatrix = pickFrustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = pickFrustum.ComputeProjectionMatrix();
    const GfVec3d cameraPosition = camera.GetTransform().ExtractTranslation();
    SdfPath nearestPath;
    double nearestDistance = std::numeric_limits<double>::max();
    READ_LOCKER(locker, d.context->stageLock(), "stageLock");
    if (!d.stage)
        return {};

    auto testRoot = [&](const UsdPrim& root) {
        if (!root)
            return;

        GfVec3d hitPoint;
        GfVec3d hitNormal;
        SdfPath hitPrimPath;
        SdfPath hitInstancerPath;
        const bool hit = d.renderEngine->testIntersection(viewMatrix, projectionMatrix, root, &hitPoint, &hitNormal,
                                                          &hitPrimPath, &hitInstancerPath);
        if (!hit || hitPrimPath.IsEmpty() || !isPathMaskedIn(hitPrimPath))
            return;
        const double distance = (hitPoint - cameraPosition).GetLength();
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestPath = hitPrimPath;
        }
    };
    if (d.mask.isEmpty()) {
        testRoot(d.stage->GetPseudoRoot());
    }
    else {
        for (const SdfPath& maskPath : d.mask)
            testRoot(d.stage->GetPrimAtPath(maskPath));
    }
    return nearestPath;
}

SdfPath
ImagingGLWidgetPrivate::resolveGeomSubsetTarget(const SdfPath& hitPath, const QPoint& pos)
{
    if (hitPath.IsEmpty() || !d.stage || !d.renderEngine)
        return hitPath;

    const QPoint devicePos = deviceRatio(pos);
    const GfVec2i framebufferSize = widgetSize();
    if (devicePos.x() < 0 || devicePos.y() < 0 || devicePos.x() >= framebufferSize[0]
        || devicePos.y() >= framebufferSize[1]) {
        return hitPath;
    }

    const GfVec2i pixel(devicePos.x(), framebufferSize[1] - 1 - devicePos.y());

    int elementId = -1;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (d.stage)
            elementId = d.renderEngine->captureElementIdAt(pixel);
    }

    if (elementId < 0)
        return hitPath;

    READ_LOCKER(locker, d.context->stageLock(), "stageLock");
    if (!d.stage)
        return hitPath;

    const UsdPrim hitPrim = d.stage->GetPrimAtPath(hitPath);
    if (!hitPrim)
        return hitPath;

    SdfPath firstMatch;
    SdfPath materialMatch;

    for (const UsdPrim& child : hitPrim.GetChildren()) {
        const UsdGeomSubset subset(child);
        if (!subset)
            continue;

        TfToken elementType;
        VtIntArray indices;
        if (!subset.GetElementTypeAttr().Get(&elementType) || elementType != UsdGeomTokens->face
            || !subset.GetIndicesAttr().Get(&indices)) {
            continue;
        }

        if (std::find(indices.begin(), indices.end(), elementId) == indices.end())
            continue;

        if (firstMatch.IsEmpty())
            firstMatch = child.GetPath();

        TfToken familyName;
        if (subset.GetFamilyNameAttr().Get(&familyName) && familyName == TfToken("materialBind")) {
            materialMatch = child.GetPath();
            break;
        }
    }

    if (!materialMatch.IsEmpty())
        return materialMatch;
    if (!firstMatch.IsEmpty())
        return firstMatch;
    return hitPath;
}

void
ImagingGLWidgetPrivate::dragEnterEvent(QDragEnterEvent* event)
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
ImagingGLWidgetPrivate::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasFormat(mime::material)) {
        if (event)
            event->ignore();
        return;
    }

    const SdfPath path = pickNearestPath(event->position().toPoint());
    if (path.IsEmpty()) {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void
ImagingGLWidgetPrivate::dropEvent(QDropEvent* event)
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
    const SdfPath hitPath = pickNearestPath(dropPosition);
    const SdfPath targetPath = resolveGeomSubsetTarget(hitPath, dropPosition);
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
ImagingGLWidgetPrivate::contextMenuEvent(QContextMenuEvent* event)
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

    if (event->button() == Qt::MiddleButton) {
        d.drag = true;
        d.sweep = false;
        d.transformDragging = false;
        viewCamera()->setCameraMode(ViewCamera::Truck);
    }
    else if (event->button() == Qt::LeftButton) {
        // Give transform handles priority over viewport navigation so Shift can
        // be held before the initial click to enable snapping.
        d.transformSnap = bool(event->modifiers() & Qt::ShiftModifier);
        if (beginTransformDrag(event->position())) {
            d.drag = false;
            d.sweep = false;
            d.glwidget->setFocus(Qt::MouseFocusReason);
        }
        else if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
            d.drag = true;
            d.sweep = false;
            d.transformDragging = false;
            viewCamera()->setCameraMode(ViewCamera::Tumble);
        }
        else {
            d.drag = false;
            d.sweep = true;
            d.start = event->pos();
            d.end = event->pos();
            d.glwidget->update();
        }
    }
    else if (event->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) {
        d.drag = true;
        d.sweep = false;
        d.transformDragging = false;
        if (event->button() == Qt::RightButton)
            viewCamera()->setCameraMode(ViewCamera::Zoom);
    }
    else if (event->button() == Qt::RightButton) {
        // Plain right mouse is reserved for the context menu. It must not
        // behave like a viewport click, start a sweep, or alter selection.
        d.drag = false;
        d.sweep = false;
        d.transformDragging = false;
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
        const Qt::KeyboardModifiers mouseMods = event->modifiers();
        d.transformSnap = bool(mouseMods & Qt::ShiftModifier);
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
        d.suppressContextMenu = event->button() == Qt::RightButton;
        d.drag = false;
        viewCamera()->setCameraMode(ViewCamera::None);
        d.glwidget->update();
    }
    else if (d.transformDragging) {
        d.transformSnap = bool(event->modifiers() & Qt::ShiftModifier);
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
    if (!d.stage || !d.renderEngine)
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
        const int cx = r.center().x();
        const int cy = r.center().y();
        const int halfW = minSize / 2;
        const int halfH = minSize / 2;
        r = QRect(QPoint(cx - halfW, cy - halfH), QPoint(cx + halfW, cy + halfH));
    }

    const GfVec4d viewport = widgetViewport();
    GfVec2d center(((r.left() + r.right()) * 0.5 - viewport[0]) / viewport[2],
                   ((r.top() + r.bottom()) * 0.5 - viewport[1]) / viewport[3]);
    center[0] = center[0] * 2.0 - 1.0;
    center[1] = -1.0 * (center[1] * 2.0 - 1.0);
    const GfVec2d size(double(r.width()) / viewport[2], double(r.height()) / viewport[3]);

    const GfCamera camera = viewCamera()->camera();
    const GfFrustum frustum = camera.GetFrustum();
    const GfFrustum pickFrustum = frustum.ComputeNarrowedFrustum(center, size);

    QList<SdfPath> selectedPaths;

    if (isClick) {
        constexpr int pickCycleTolerance = 6;
        const QPoint clickPosition = rect.normalized().center();
        const bool samePosition = d.lastPickIndex >= 0
                                  && std::abs(clickPosition.x() - d.lastPickPosition.x()) <= pickCycleTolerance
                                  && std::abs(clickPosition.y() - d.lastPickPosition.y()) <= pickCycleTolerance;
        if (!samePosition) {
            d.lastPickPaths.clear();
            d.lastPickIndex = -1;
        }

        const QPoint devicePos = deviceRatio(clickPosition);
        const GfVec2i framebufferSize = widgetSize();
        const GfVec2i pixel(devicePos.x(), framebufferSize[1] - 1 - devicePos.y());
        if (pixel[0] >= 0 && pixel[1] >= 0 && pixel[0] < framebufferSize[0] && pixel[1] < framebufferSize[1]) {
            SdfPath hitPath;
            {
                READ_LOCKER(locker, d.context->stageLock(), "stageLock");
                if (d.stage)
                    hitPath = d.renderEngine->pickNextIdAt(pixel, d.lastPickPaths);
            }
            if (hitPath.IsEmpty() && !d.lastPickPaths.isEmpty()) {
                d.lastPickPaths.clear();
                READ_LOCKER(locker, d.context->stageLock(), "stageLock");
                if (d.stage)
                    hitPath = d.renderEngine->pickNextIdAt(pixel, {});
            }
            if (!hitPath.IsEmpty() && isPathMaskedIn(hitPath)) {
                d.lastPickPaths.append(hitPath);
                d.lastPickIndex = static_cast<int>(d.lastPickPaths.size()) - 1;
                d.lastPickPosition = clickPosition;
                SdfPath selectedPath = hitPath;
                if (d.lastPickIndex == 0)
                    selectedPath = resolveGeomSubsetTarget(hitPath, clickPosition);
                qInfo().noquote() << "[GpuPick] path=" << QString::fromStdString(selectedPath.GetString())
                                  << "layer=" << d.lastPickIndex;
                selectedPaths.append(selectedPath);
            }
            else {
                d.lastPickPaths.clear();
                d.lastPickIndex = -1;
            }
        }
    }
    else {
        // Sweep selection is geometric instead of raster based. Testing each
        // renderable prim's world-space bound against the actual 3D sweep
        // frustum makes tiny/distant prims selectable regardless of pixel
        // coverage, and occluded prims are naturally included as well.
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage)
                return;

            UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(),
                                       { UsdGeomTokens->default_, UsdGeomTokens->proxy, UsdGeomTokens->render }, true);

            for (const UsdPrim& prim : d.stage->Traverse()) {
                if (!prim || !prim.IsActive() || !prim.IsDefined())
                    continue;

                const SdfPath path = prim.GetPath();
                if (!isPathMaskedIn(path))
                    continue;

                // Limit sweep results to actual drawable geometry. Selecting
                // aggregate Xform/container bounds would otherwise pull whole
                // assemblies into a small box selection.
                const UsdGeomBoundable boundable(prim);
                if (!boundable)
                    continue;

                const UsdGeomImageable imageable(prim);
                if (imageable && imageable.ComputeVisibility() == UsdGeomTokens->invisible)
                    continue;

                const GfBBox3d worldBounds = bboxCache.ComputeWorldBound(prim);
                if (worldBounds.ComputeAlignedRange().IsEmpty())
                    continue;

                if (pickFrustum.Intersects(worldBounds))
                    selectedPaths.append(path);
            }
        }

        selectedPaths = path::uniquePaths(selectedPaths);
        d.lastPickPosition = QPoint();
        d.lastPickPaths.clear();
        d.lastPickIndex = -1;
    }

    bool update = false;
    if (!selectedPaths.isEmpty()) {
        if (event->modifiers() & Qt::ShiftModifier) {
            for (const SdfPath& path : selectedPaths) {
                const qsizetype index = d.selection.indexOf(path);
                if (index >= 0)
                    d.selection.removeAt(index);
                else
                    d.selection.append(path);
                update = true;
            }
        }
        else if (d.selection != selectedPaths) {
            d.selection = selectedPaths;
            update = true;
        }
    }
    else if (!d.selection.isEmpty()) {
        d.selection.clear();
        update = true;
    }

    if (update)
        d.context->execute(new Command(selectPaths(d.selection)));

    d.glwidget->update();
}

bool
ImagingGLWidgetPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event && d.transformDragging
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::ShortcutOverride)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && keyEvent->key() == Qt::Key_Shift && !keyEvent->isAutoRepeat()) {
            d.transformSnap = event->type() == QEvent::KeyPress;
            updateTransformDrag(QPointF(d.mousepos));
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
ImagingGLWidgetPrivate::wheelEvent(QWheelEvent* event)
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
ImagingGLWidgetPrivate::updateStage(UsdStageRefPtr stage)
{
    SignalGuard::Scope guard(this);
    d.stage = stage;
    d.transformPivotValid = false;
    d.visibleCapture.clear();
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
ImagingGLWidgetPrivate::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    SignalGuard::Scope guard(this);
    if (auxiliary == d.auxiliary)
        return;

    d.auxiliary = auxiliary;
    ensureAuxiliaryMaterials();
    updateAuxiliaryGrid();
    if (d.renderEngine)
        d.renderEngine->setAuxiliaryStage(auxiliary);

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
    if (d.renderEngine)
        d.renderEngine->refreshAuxiliaryStage();

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
    if (d.transformDragging)
        endTransformDrag();
    d.transformPivotValid = false;
    d.transformHoverAxis = 0;
    // GeomSubsets are face collections, not Hydra drawables. Resolve their
    // render mask to the owning mesh while retaining the original selection.
    d.mask.clear();
    d.mask.reserve(paths.size());
    for (const SdfPath& path : paths) {
        SdfPath renderPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const UsdPrim prim = d.stage ? d.stage->GetPrimAtPath(renderPath) : UsdPrim();
        if (prim && UsdGeomSubset(prim)) {
            renderPath = prim.GetParent().GetPath();
            qInfo().noquote() << "[MaskDebug] subset mask resolved:" << QString::fromStdString(path.GetString()) << "->"
                              << QString::fromStdString(renderPath.GetString());
        }
        if (!renderPath.IsEmpty() && !d.mask.contains(renderPath))
            d.mask.append(renderPath);
    }

    if (d.renderEngine) {
        QList<SdfPath> visibleSelection;
        visibleSelection.reserve(d.selection.size());
        for (const SdfPath& path : d.selection) {
            if (isSelectionVisible(path))
                visibleSelection.append(path);
        }

        d.renderEngine->setMask(d.mask);
        d.renderEngine->setSelected(visibleSelection);
    }

    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updatePrims(const NoticeBatch& batch)
{
    Q_UNUSED(batch);
    SignalGuard::Scope guard(this);

    // A transform changed outside the active gizmo operation (for example Undo
    // or Redo), so the cached manipulation pivot may no longer match the stage.
    // Keep it stable while flushing this gizmo's own deferred notices; otherwise
    // rotate/scale would recompute the pivot from a new world-space bound and jump.
    const bool invalidatePivot = !d.transformDragging && !d.transformDefersPrimsUpdate;
    if (invalidatePivot)
        d.transformPivotValid = false;

    if (viewState() && viewState()->sceneStatsEnabled()) {
        updateSceneStats();
    }
    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::updateTransform(bool enabled)
{
    if (!enabled) {
        updateTransformMode(TransformMode::None, false);
        return;
    }

    if (d.transformMode == TransformMode::None)
        updateTransformMode(TransformMode::Move, true);
}

void
ImagingGLWidgetPrivate::updateTransformMode(TransformMode mode, bool enabled)
{
    const TransformMode nextMode = enabled ? mode : (d.transformMode == mode ? TransformMode::None : d.transformMode);


    if (d.transformMode == nextMode) {
        d.glwidget->update();
        return;
    }

    if (d.transformDragging)
        endTransformDrag();

    d.transformMode = nextMode;
    d.transformEnabled = d.transformMode != TransformMode::None;
    d.transformSnap = false;
    d.transformHoverAxis = 0;
    d.transformActiveAxis = 0;

    d.glwidget->update();
}

void
ImagingGLWidgetPrivate::captureVisible()
{
    QElapsedTimer timer;
    timer.start();

    d.glwidget->makeCurrent();
    if (!d.stage || !d.renderEngine)
        return;

#ifdef WIN32
    glDepthMask(GL_TRUE);
#endif

    // Visible capture shares the Stageviz scene-ID pass used by selection
    // outlines. RenderEngine performs a tiled high-coverage primId scan and
    // returns the unique front-most document prim paths from the current view.
    const QList<SdfPath> captured = d.renderEngine->captureVisiblePaths(4);

    bool changed = false;
    for (const SdfPath& path : captured) {
        if (!d.visibleCapture.contains(path)) {
            d.visibleCapture.append(path);
            changed = true;
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
    d.lastPickPosition = QPoint();
    d.lastPickPaths.clear();
    d.lastPickIndex = -1;
    d.glwidget->update();
    updateAxis();
}
void
ImagingGLWidgetPrivate::updateSelection(const QList<SdfPath>& paths)
{
    SignalGuard::Scope guard(this);
    d.selection = paths;

    if (!d.transformDragging) {
        d.transformPivotValid = false;
    }

    if (d.renderEngine) {
        QList<SdfPath> visibleSelection;
        visibleSelection.reserve(d.selection.size());
        for (const SdfPath& path : d.selection) {
            if (isSelectionVisible(path))
                visibleSelection.append(path);
        }

        d.renderEngine->setSelected(visibleSelection);
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
ImagingGLWidgetPrivate::updateAuxiliaryGrid()
{
    if (!d.auxiliary)
        return;

    ViewState* state = viewState();
    WRITE_LOCKER(locker, session()->auxiliaryLock(), "auxiliaryLock");
    // Keep implementation-specific children below the shared auxiliary roots.
    const SdfPath gridRootPath = paths::auxiliary::display.AppendChild(TfToken("Grid"));
    const SdfPath gridPath = gridRootPath.AppendChild(TfToken("Lines"));
    const SdfPath centerPath = gridRootPath.AppendChild(TfToken("Center"));
    const SdfPath gridMaterialPath = paths::auxiliary::materials.AppendChild(TfToken("Grid"));
    const SdfPath centerMaterialPath = paths::auxiliary::materials.AppendChild(TfToken("GridCenter"));
    if (!state->gridEnabled()) {
        d.auxiliary->RemovePrim(gridRootPath);
        return;
    }

    UsdGeomScope::Define(d.auxiliary, paths::auxiliary::display);
    UsdGeomScope::Define(d.auxiliary, gridRootPath);
    UsdGeomScope::Define(d.auxiliary, paths::auxiliary::materials);
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
    // Derive implementation-specific materials from the shared auxiliary root.
    const SdfPath clayPath = paths::auxiliary::materials.AppendChild(TfToken("Clay"));
    const SdfPath chromePath = paths::auxiliary::materials.AppendChild(TfToken("Chrome"));
    const SdfPath glossyPath = paths::auxiliary::materials.AppendChild(TfToken("Glossy"));
    const SdfPath reflectionPath = paths::auxiliary::materials.AppendChild(TfToken("Reflection"));
    const SdfPath selectionPath = paths::auxiliary::materials.AppendChild(TfToken("Selection"));
    UsdGeomScope::Define(d.auxiliary, paths::auxiliary::materials);

    // Built-in Stageviz inspection materials live on the shared auxiliary
    // stage and never modify the document stage.
    authorAuxiliaryStandardSurface(clayPath, GfVec3f(0.55f, 0.18f, 0.16f), 0.0f, 0.68f, 0.35f);
    authorAuxiliaryStandardSurface(chromePath, GfVec3f(0.62f), 1.0f, 0.06f, 1.0f);
    authorAuxiliaryStandardSurface(glossyPath, GfVec3f(0.18f), 0.0f, 0.12f, 1.0f);
    authorAuxiliaryStandardSurface(reflectionPath, GfVec3f(0.16f), 1.0f, 0.025f, 1.0f);

    const QColor selectionColor = style()->color(Style::ColorRole::Selection);
    const GfVec3f color(selectionColor.redF(), selectionColor.greenF(), selectionColor.blueF());
    authorAuxiliaryStandardSurface(selectionPath, color, 0.0f, 0.72f, 0.20f);
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
    // stable and independent of viewport lighting.
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
ImagingGLWidgetPrivate::updateRenderEngineSettings()
{
    if (!d.renderEngine)
        return;

    ViewState* state = viewState();
    if (!state)
        return;

    RenderEngine::Settings settings = d.renderEngine->settings();
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
ImagingGLWidgetPrivate::projectWorldToScreen(const GfVec3d& world, QPointF& screen)
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
QList<SdfPath>
ImagingGLWidgetPrivate::transformVisiblePaths() const
{
    QList<SdfPath> paths;
    if (!d.stage)
        return paths;

    paths.reserve(d.selection.size());
    for (const SdfPath& selectedPath : d.selection) {
        const SdfPath path = selectedPath.IsPropertyPath() ? selectedPath.GetPrimPath() : selectedPath;
        if (!isPathMaskedIn(path) || !stage::isTransformEditable(d.stage, path))
            continue;

        const UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            continue;

        const UsdGeomImageable imageable(prim);
        if (imageable && imageable.ComputeVisibility() == UsdGeomTokens->invisible)
            continue;

        if (!paths.contains(path))
            paths.append(path);
    }
    return paths;
}

bool
ImagingGLWidgetPrivate::transformSelectionPivot(GfVec3d& pivot)
{
    const QList<SdfPath> paths = transformVisiblePaths();

    if (paths.isEmpty())
        return false;

    if (paths.size() == 1) {
        const SdfPath& path = paths.first();

        GfMatrix4d matrix(1.0);
        QString error;
        if (!stage::worldTransform(d.stage, path, matrix, error))
            return false;

        pivot = matrix.ExtractTranslation();

        QString pivotError;
        stage::worldPivot(d.stage, path, pivot, pivotError);
        return true;
    }

    UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(),
                               { UsdGeomTokens->default_, UsdGeomTokens->proxy, UsdGeomTokens->render }, true);

    GfRange3d range;
    bool hasBounds = false;

    for (const SdfPath& path : paths) {
        const UsdPrim prim = d.stage->GetPrimAtPath(path);
        if (!prim)
            continue;

        const GfBBox3d bbox = bboxCache.ComputeWorldBound(prim);
        const GfRange3d aligned = bbox.ComputeAlignedRange();
        if (aligned.IsEmpty())
            continue;

        if (!hasBounds) {
            range = aligned;
            hasBounds = true;
        }
        else {
            range.UnionWith(aligned);
        }
    }

    if (!hasBounds)
        return false;

    pivot = range.GetMidpoint();
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
ImagingGLWidgetPrivate::transformRotationScreenBasis(int axis, QPointF& center, QPointF& basisA, QPointF& basisB)
{
    const GfVec3d normal = transformAxisVector(axis);
    if (normal.GetLengthSq() < 1e-12)
        return false;

    GfVec3d worldA;
    GfVec3d worldB;
    if (normal == GfVec3d::XAxis()) {
        worldA = GfVec3d::YAxis();
        worldB = GfVec3d::ZAxis();
    }
    else if (normal == GfVec3d::YAxis()) {
        worldA = GfVec3d::ZAxis();
        worldB = GfVec3d::XAxis();
    }
    else {
        worldA = GfVec3d::XAxis();
        worldB = GfVec3d::YAxis();
    }

    if (!projectWorldToScreen(d.transformPivot, center))
        return false;

    // Build the rotation overlay in screen space from the projected world axes.
    // The largest projected semi-axis is normalized to the exact same screen-space
    // size used by the Move and Scale gizmos. The other semi-axis is allowed to
    // foreshorten naturally with the camera angle.
    const double probeRadius = transformWorldPerPixel(d.transformPivot) * TransformGizmoSize;
    if (!std::isfinite(probeRadius) || probeRadius <= 0.0)
        return false;

    QPointF projectedA;
    QPointF projectedB;
    if (!projectWorldToScreen(d.transformPivot + worldA * probeRadius, projectedA)
        || !projectWorldToScreen(d.transformPivot + worldB * probeRadius, projectedB))
        return false;

    basisA = projectedA - center;
    basisB = projectedB - center;

    const double lengthA = std::hypot(basisA.x(), basisA.y());
    const double lengthB = std::hypot(basisB.x(), basisB.y());
    const double maxLength = std::max(lengthA, lengthB);
    if (!std::isfinite(maxLength) || maxLength < 1e-6)
        return false;

    const double scale = TransformGizmoSize / maxLength;
    basisA *= scale;
    basisB *= scale;
    return true;
}

bool
ImagingGLWidgetPrivate::transformRotationPoint(int axis, double angle, QPointF& screen)
{
    QPointF center;
    QPointF basisA;
    QPointF basisB;
    if (!transformRotationScreenBasis(axis, center, basisA, basisB))
        return false;

    screen = center + basisA * std::cos(angle) + basisB * std::sin(angle);
    return std::isfinite(screen.x()) && std::isfinite(screen.y());
}

bool
ImagingGLWidgetPrivate::transformRotationAngle(int axis, const QPointF& pos, double& angle, double* distance)
{
    QPointF center;
    QPointF basisA;
    QPointF basisB;
    if (!transformRotationScreenBasis(axis, center, basisA, basisB))
        return false;

    // Solve mouse = center + basisA*cos(a) + basisB*sin(a) in screen space.
    // The old sampled-ring implementation only resolved 96 positions (3.75 deg
    // steps), which masked the intended 1-degree Shift snapping. This continuous
    // solution gives the rotation drag full precision before snapping is applied.
    const QPointF delta = pos - center;
    const double determinant = basisA.x() * basisB.y() - basisA.y() * basisB.x();
    if (std::abs(determinant) < 1e-8)
        return false;

    const double cosComponent = (delta.x() * basisB.y() - delta.y() * basisB.x()) / determinant;
    const double sinComponent = (basisA.x() * delta.y() - basisA.y() * delta.x()) / determinant;
    if (!std::isfinite(cosComponent) || !std::isfinite(sinComponent))
        return false;

    angle = std::atan2(sinComponent, cosComponent);

    if (distance) {
        const QPointF nearest = center + basisA * std::cos(angle) + basisB * std::sin(angle);
        *distance = std::hypot(pos.x() - nearest.x(), pos.y() - nearest.y());
    }

    return true;
}
int
ImagingGLWidgetPrivate::hitTestTransform(const QPointF& pos)
{
    if (!d.transformEnabled || d.transformMode == TransformMode::None || transformVisiblePaths().isEmpty())
        return 0;

    QPointF center;
    if (!projectWorldToScreen(d.transformPivot, center))
        return 0;

    // Handles are encoded as:
    // 1..3  = translate X/Y/Z
    // 4..6  = rotate X/Y/Z
    // 7..9  = scale X/Y/Z
    // 10    = free rotation
    // 11    = free translation in the camera plane
    // 12    = uniform scale
    constexpr double centerHitRadius = 8.0;
    if (std::hypot(pos.x() - center.x(), pos.y() - center.y()) <= centerHitRadius) {
        if (d.transformMode == TransformMode::Move)
            return 11;
        if (d.transformMode == TransformMode::Rotate)
            return 10;
        if (d.transformMode == TransformMode::Scale)
            return 12;
    }

    if (d.transformMode == TransformMode::Rotate) {
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
        return rotateHandle;
    }

    constexpr double hitWidth = 8.0;

    if (d.transformMode == TransformMode::Scale) {
        for (int axis = 1; axis <= 3; ++axis) {
            const QPointF dir = transformAxisDirection(axis);
            if (dir.isNull())
                continue;
            const QPointF handle = center + dir * TransformGizmoSize;
            if (std::hypot(pos.x() - handle.x(), pos.y() - handle.y()) <= hitWidth)
                return axis + 6;
        }
        return 0;
    }

    int bestAxis = 0;
    double bestDistance = hitWidth;
    for (int axis = 1; axis <= 3; ++axis) {
        const QPointF dir = transformAxisDirection(axis);
        if (dir.isNull())
            continue;
        const QPointF end = center + dir * TransformGizmoSize;
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
    XformEdit edit;
    d.transformPaths.clear();
    GfVec3d pivot = d.transformPivot;
    {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return false;

        if (!d.transformPivotValid && !transformSelectionPivot(pivot))
            return false;

        const SdfLayerHandle editLayer = d.stage->GetEditTarget().GetLayer();
        if (!editLayer)
            return false;

        d.transformMetersPerUnit = UsdGeomGetStageMetersPerUnit(d.stage);
        if (!std::isfinite(d.transformMetersPerUnit) || d.transformMetersPerUnit <= 0.0)
            d.transformMetersPerUnit = 1.0;

        for (const SdfPath& path : transformVisiblePaths()) {
            GfMatrix4d matrix(1.0);
            QString error;
            if (!stage::worldTransform(d.stage, path, matrix, error))
                continue;
            d.transformPaths.append(path);
            edit.before.append(matrix);

            XformState state;
            state.hadPrimSpec = bool(editLayer->GetPrimAtPath(path));
            const SdfPath orderPath = path.AppendProperty(TfToken("xformOpOrder"));
            const SdfPath matrixPath = path.AppendProperty(TfToken("xformOp:transform"));
            state.hadXformOpOrderSpec = bool(editLayer->GetPropertyAtPath(orderPath));
            state.hadXformOpOrderDefault = editLayer->HasField(orderPath, SdfFieldKeys->Default);
            if (state.hadXformOpOrderDefault)
                state.xformOpOrderDefault = editLayer->GetField(orderPath, SdfFieldKeys->Default);
            state.hadMatrixOpSpec = bool(editLayer->GetPropertyAtPath(matrixPath));
            state.hadMatrixOpDefault = editLayer->HasField(matrixPath, SdfFieldKeys->Default);
            if (state.hadMatrixOpDefault)
                state.matrixOpDefault = editLayer->GetField(matrixPath, SdfFieldKeys->Default);
            edit.beforeState.append(state);
        }
    }
    if (edit.before.isEmpty())
        return false;
    edit.after = edit.before;
    d.transformPivot = pivot;
    d.transformPivotValid = true;
    d.transformStartPivot = d.transformPivot;
    d.transformEdit = edit;
    d.transformStart = pos;
    d.transformActiveAxis = handle;
    d.transformHoverAxis = d.transformActiveAxis;
    d.transformRotationStartAngle = 0.0;
    if (handle >= 4 && handle <= 6) {
        double angle = 0.0;
        if (!transformRotationAngle(handle - 3, pos, angle))
            return false;
        d.transformRotationStartAngle = angle;
    }

    // Interactive transforms author real USD values so Hydra can update live.
    // Buffer Stageviz-side prim notifications during the drag; otherwise every
    // mouse move can make PropertyTree, StageTree and other listeners process
    // one notice per selected prim. Restoring the previous policy at release
    // flushes the accumulated notice batch once.
    d.transformPreviousPrimsUpdate = session()->primsUpdate();
    if (d.transformPreviousPrimsUpdate != Session::PrimsUpdate::Deferred) {
        session()->setPrimsUpdate(Session::PrimsUpdate::Deferred);
        d.transformDefersPrimsUpdate = true;
    }
    else {
        d.transformDefersPrimsUpdate = false;
    }

    // Normalize transform authoring once at drag start, then cache the matrix
    // ops used by the common case. This avoids re-running the generic
    // stage::setWorldTransform() setup for every selected prim on every
    // mouse-move event.
    d.transformPrepared.clear();
    {
        WRITE_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return false;

        QStringList errors;
        d.transformPrepared = prepareTransforms(d.stage, d.transformPaths, d.transformEdit.before, &errors);
        for (const QString& error : errors)
            qWarning().noquote() << QStringLiteral("Could not prepare transform preview: %1").arg(error);
    }

    d.transformDragging = true;
    d.glwidget->update();
    return true;
}

void
ImagingGLWidgetPrivate::updateTransformDrag(const QPointF& pos)
{
    if (!d.transformDragging || d.transformActiveAxis == 0 || d.transformEdit.before.isEmpty())
        return;

    d.transformEdit.after = d.transformEdit.before;
    GfMatrix4d dragDelta(1.0);
    QString transformMessage;

    auto axisName = [](int axis) -> QString {
        switch (axis) {
        case 1: return QStringLiteral("X");
        case 2: return QStringLiteral("Y");
        case 3: return QStringLiteral("Z");
        default: return QStringLiteral("?");
        }
    };

    auto signedNumber = [](double value, int precision) {
        return QStringLiteral("%1%2").arg(value >= 0.0 ? QStringLiteral("+") : QString()).arg(value, 0, 'f', precision);
    };

    auto snapValue = [](double value, double unit) { return std::round(value / unit) * unit; };


    auto matrixScale = [](const GfMatrix4d& matrix) {
        return GfVec3d(
            std::sqrt(matrix[0][0] * matrix[0][0] + matrix[0][1] * matrix[0][1] + matrix[0][2] * matrix[0][2]),
            std::sqrt(matrix[1][0] * matrix[1][0] + matrix[1][1] * matrix[1][1] + matrix[1][2] * matrix[1][2]),
            std::sqrt(matrix[2][0] * matrix[2][0] + matrix[2][1] * matrix[2][1] + matrix[2][2] * matrix[2][2]));
    };


    auto snappedRotationDelta = [&](const GfMatrix4d& inputDelta, int snapAxis) {
        if (!d.transformSnap || d.transformEdit.before.isEmpty())
            return inputDelta;

        // First calculate the unsnapped absolute world orientation produced by
        // the drag.  Snapping is defined in that absolute orientation, not in
        // the incremental mouse-delta matrix.
        const GfMatrix4d& before = d.transformEdit.before.first();
        const GfMatrix4d candidate = before * inputDelta;
        const GfRotation candidateRotation = transformRotationWithoutScale(candidate);
        GfVec3d angles = candidateRotation.Decompose(GfVec3d::XAxis(), GfVec3d::YAxis(), GfVec3d::ZAxis());

        if (snapAxis >= 1 && snapAxis <= 3) {
            angles[snapAxis - 1] = snapValue(angles[snapAxis - 1], TransformRotateSnapDegrees);
        }
        else {
            for (int axis = 0; axis < 3; ++axis)
                angles[axis] = snapValue(angles[axis], TransformRotateSnapDegrees);
        }

        // Rebuild the exact desired snapped absolute orientation.
        GfMatrix4d rotateX(1.0);
        GfMatrix4d rotateY(1.0);
        GfMatrix4d rotateZ(1.0);
        rotateX.SetRotate(GfRotation(GfVec3d::XAxis(), angles[0]));
        rotateY.SetRotate(GfRotation(GfVec3d::YAxis(), angles[1]));
        rotateZ.SetRotate(GfRotation(GfVec3d::ZAxis(), angles[2]));
        // GfRotation::Decompose(X, Y, Z) decomposes the matrix using the
        // opposite matrix-composition order from UsdGeom rotateXYZ.  To rebuild
        // the exact orientation represented by those decomposed angles we must
        // compose Z * Y * X here.  Using X * Y * Z only appears correct when
        // one/two components are near zero; with free rotation or an already
        // compound orientation it produces a completely different rotation.
        const GfMatrix4d snappedRotation = rotateZ * rotateY * rotateX;

        // Build ONE rotation delta from the drag-start orientation directly to
        // the desired snapped orientation.  The previous implementation first
        // applied inputDelta and then appended a candidate-space correction;
        // with an already-rotated prim that correction was composed in the wrong
        // space and changed the other Euler components as well.
        GfMatrix4d beforeRotation(1.0);
        beforeRotation.SetRotate(transformRotationWithoutScale(before));
        const GfMatrix4d deltaRotation = beforeRotation.GetInverse() * snappedRotation;

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d snappedDelta = toOrigin * deltaRotation * fromOrigin;


        return snappedDelta;
    };

    if (d.transformActiveAxis == 11) {
        // Free move is solved directly in screen space on a camera-facing plane
        // through the starting pivot. Using the projected camera right/up basis
        // keeps the gizmo center exactly under the mouse instead of relying on
        // an approximate world-units-per-pixel conversion.
        const GfCamera camera = viewCamera()->camera();
        const GfMatrix4d cameraTransform = camera.GetTransform();
        const GfVec3d right = cameraTransform.TransformDir(GfVec3d::XAxis()).GetNormalized();
        const GfVec3d up = cameraTransform.TransformDir(GfVec3d::YAxis()).GetNormalized();

        QPointF pivotScreen;
        if (!projectWorldToScreen(d.transformStartPivot, pivotScreen))
            return;

        const double probeDistance = std::max(1e-6, transformWorldPerPixel(d.transformStartPivot) * 100.0);
        QPointF rightScreen;
        QPointF upScreen;
        if (!projectWorldToScreen(d.transformStartPivot + right * probeDistance, rightScreen)
            || !projectWorldToScreen(d.transformStartPivot + up * probeDistance, upScreen))
            return;

        const QPointF screenRight = rightScreen - pivotScreen;
        const QPointF screenUp = upScreen - pivotScreen;
        const double determinant = screenRight.x() * screenUp.y() - screenRight.y() * screenUp.x();
        if (std::abs(determinant) < 1e-8)
            return;

        // The entire center circle is one logical free-move handle.  Do not
        // make the world-space pivot jump to the exact pixel that was clicked
        // inside that circle.  Measure only the mouse movement since drag start
        // so grabbing the left/right/top/bottom side of the center handle gives
        // exactly the same translation behavior and zero delta at mouse-down.
        const QPointF screenDelta = pos - d.transformStart;
        const double rightAmount = (screenDelta.x() * screenUp.y() - screenDelta.y() * screenUp.x()) / determinant;
        const double upAmount = (screenRight.x() * screenDelta.y() - screenRight.y() * screenDelta.x()) / determinant;
        GfVec3d translation = right * (rightAmount * probeDistance) + up * (upAmount * probeDistance);

        if (d.transformSnap) {
            GfVec3d target = d.transformStartPivot + translation;
            for (int axis = 0; axis < 3; ++axis)
                target[axis] = snapValue(target[axis], TransformMoveSnap);
            translation = target - d.transformStartPivot;
        }

        dragDelta.SetTranslate(translation);
        const GfVec3d translationMm = translation * (d.transformMetersPerUnit * 1000.0);
        transformMessage = QStringLiteral("Translate  X %1 mm  Y %2 mm  Z %3 mm")
                               .arg(signedNumber(translationMm[0], 3), signedNumber(translationMm[1], 3),
                                    signedNumber(translationMm[2], 3));

        for (qsizetype i = 0; i < d.transformEdit.after.size(); ++i) {
            GfMatrix4d matrix = d.transformEdit.before.at(i);
            matrix.SetTranslateOnly(d.transformEdit.before.at(i).ExtractTranslation() + translation);
            d.transformEdit.after[i] = matrix;
        }
        d.transformPivot = d.transformStartPivot + translation;
    }
    else if (d.transformActiveAxis == 10) {
        const QPointF mouseDelta = pos - d.transformStart;
        const GfCamera camera = viewCamera()->camera();
        const GfMatrix4d cameraTransform = camera.GetTransform();
        const GfVec3d right = cameraTransform.TransformDir(GfVec3d::XAxis()).GetNormalized();
        const GfVec3d up = cameraTransform.TransformDir(GfVec3d::YAxis()).GetNormalized();
        constexpr double degreesPerPixel = 0.35;
        double horizontalAngle = mouseDelta.x() * degreesPerPixel;
        double verticalAngle = mouseDelta.y() * degreesPerPixel;

        // Keep the free gesture continuous here. Shift snapping is applied to
        // the resulting absolute XYZ orientation below, so an object that starts
        // on fractional angles is pulled onto whole-degree values as well.
        GfMatrix4d horizontalRotation(1.0);
        GfMatrix4d verticalRotation(1.0);
        horizontalRotation.SetRotate(GfRotation(up, horizontalAngle));
        verticalRotation.SetRotate(GfRotation(right, verticalAngle));
        const GfMatrix4d rotationMatrix = verticalRotation * horizontalRotation;

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d delta = toOrigin * rotationMatrix * fromOrigin;


        dragDelta = snappedRotationDelta(delta, 0);

        const GfMatrix4d snappedCandidate = d.transformEdit.before.first() * dragDelta;
        const GfVec3d xyz = transformEulerWithoutScale(snappedCandidate);

        transformMessage = QStringLiteral("Rotate  X %1°  Y %2°  Z %3°")
                               .arg(signedNumber(xyz[0], 2), signedNumber(xyz[1], 2), signedNumber(xyz[2], 2));

        for (qsizetype i = 0; i < d.transformEdit.after.size(); ++i)
            d.transformEdit.after[i] = d.transformEdit.before.at(i) * dragDelta;
        d.transformPivot = d.transformStartPivot;
    }
    else if (d.transformActiveAxis == 12) {
        const QPointF mouseDelta = pos - d.transformStart;
        const double rawFactor = std::clamp(std::exp((mouseDelta.x() - mouseDelta.y()) * 0.01), 0.01, 100.0);
        double factor = rawFactor;
        if (d.transformSnap) {
            const GfVec3d startScale = matrixScale(d.transformEdit.before.first());
            const double minScale = std::min({ startScale[0], startScale[1], startScale[2] });
            const double maxScale = std::max({ startScale[0], startScale[1], startScale[2] });

            // If the starting scale is uniform, snap the resulting absolute scale
            // to whole units. For an intentionally non-uniform object there is no
            // single uniform factor that can make all three axes integral, so snap
            // the uniform factor itself to whole units instead.
            if (maxScale > 1e-8 && (maxScale - minScale) <= maxScale * 1e-6) {
                const double baseScale = (startScale[0] + startScale[1] + startScale[2]) / 3.0;
                const double targetScale = std::max(TransformScaleSnap,
                                                    snapValue(baseScale * factor, TransformScaleSnap));
                factor = targetScale / baseScale;
            }
            else {
                factor = std::max(TransformScaleSnap, snapValue(factor, TransformScaleSnap));
            }
            factor = std::clamp(factor, 0.01, 100.0);
        }

        GfMatrix4d toOrigin(1.0);
        GfMatrix4d scaleMatrix(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        scaleMatrix.SetScale(GfVec3d(factor));
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d delta = toOrigin * scaleMatrix * fromOrigin;
        dragDelta = delta;
        transformMessage = QStringLiteral("Scale XYZ  %1x").arg(factor, 0, 'f', 3);

        for (qsizetype i = 0; i < d.transformEdit.after.size(); ++i)
            d.transformEdit.after[i] = d.transformEdit.before.at(i) * delta;
        d.transformPivot = d.transformStartPivot;
    }
    else if (d.transformActiveAxis >= 1 && d.transformActiveAxis <= 3) {
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
        double distance = pixels * worldPerScreenPixel;
        if (d.transformSnap) {
            const int axisIndex = d.transformActiveAxis - 1;
            const double target = d.transformStartPivot[axisIndex] + distance;
            distance = snapValue(target, TransformMoveSnap) - d.transformStartPivot[axisIndex];
        }
        const GfVec3d delta = axis * distance;
        dragDelta.SetTranslate(delta);
        const double distanceMm = distance * d.transformMetersPerUnit * 1000.0;
        transformMessage
            = QStringLiteral("Translate %1  %2 mm").arg(axisName(d.transformActiveAxis), signedNumber(distanceMm, 3));
        for (qsizetype i = 0; i < d.transformEdit.after.size(); ++i) {
            GfMatrix4d matrix = d.transformEdit.before.at(i);
            matrix.SetTranslateOnly(d.transformEdit.before.at(i).ExtractTranslation() + delta);
            d.transformEdit.after[i] = matrix;
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
        while (deltaAngle > Pi)
            deltaAngle -= 2.0 * Pi;

        while (deltaAngle < -Pi)
            deltaAngle += 2.0 * Pi;

        const double degrees = deltaAngle * 180.0 / Pi;

        const GfVec3d axis = transformAxisVector(axisIndex);
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d rotation(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        rotation.SetRotate(GfRotation(axis, degrees));
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d delta = toOrigin * rotation * fromOrigin;
        dragDelta = snappedRotationDelta(delta, axisIndex);
        const GfMatrix4d snappedCandidate = d.transformEdit.before.first() * dragDelta;
        const GfVec3d snappedAngles = transformEulerWithoutScale(snappedCandidate);
        const GfVec3d beforeAngles = transformEulerWithoutScale(d.transformEdit.before.first());
        double displayedDelta = snappedAngles[axisIndex - 1] - beforeAngles[axisIndex - 1];
        while (displayedDelta > 180.0)
            displayedDelta -= 360.0;
        while (displayedDelta < -180.0)
            displayedDelta += 360.0;
        transformMessage = QStringLiteral("Rotate %1  %2°").arg(axisName(axisIndex), signedNumber(displayedDelta, 2));
        for (qsizetype i = 0; i < d.transformEdit.after.size(); ++i) {
            d.transformEdit.after[i] = d.transformEdit.before.at(i) * dragDelta;
        }
        d.transformPivot = d.transformStartPivot;
    }
    else if (d.transformActiveAxis >= 7 && d.transformActiveAxis <= 9) {
        const int axisIndex = d.transformActiveAxis - 6;
        const QPointF screenAxis = transformAxisDirection(axisIndex);
        if (screenAxis.isNull())
            return;

        const double pixels = QPointF::dotProduct(pos - d.transformStart, screenAxis);
        const double rawFactor = std::clamp(std::exp(pixels * 0.01), 0.01, 100.0);
        double factor = rawFactor;
        if (d.transformSnap) {
            const GfVec3d startScale = matrixScale(d.transformEdit.before.first());
            const double baseScale = startScale[axisIndex - 1];
            if (baseScale > 1e-8) {
                const double targetScale = std::max(TransformScaleSnap,
                                                    snapValue(baseScale * factor, TransformScaleSnap));
                factor = targetScale / baseScale;
            }
            else {
                factor = std::max(TransformScaleSnap, snapValue(factor, TransformScaleSnap));
            }
            factor = std::clamp(factor, 0.01, 100.0);
        }
        GfVec3d scale(1.0);
        scale[axisIndex - 1] = factor;
        GfMatrix4d toOrigin(1.0);
        GfMatrix4d scaleMatrix(1.0);
        GfMatrix4d fromOrigin(1.0);
        toOrigin.SetTranslate(-d.transformStartPivot);
        scaleMatrix.SetScale(scale);
        fromOrigin.SetTranslate(d.transformStartPivot);
        const GfMatrix4d delta = toOrigin * scaleMatrix * fromOrigin;
        dragDelta = delta;
        transformMessage = QStringLiteral("Scale %1  %2x").arg(axisName(axisIndex)).arg(factor, 0, 'f', 3);
        for (qsizetype i = 0; i < d.transformEdit.after.size(); ++i) {
            d.transformEdit.after[i] = d.transformEdit.before.at(i) * delta;
        }
        d.transformPivot = d.transformStartPivot;
    }
    {
        WRITE_LOCKER(locker, d.context->stageLock(), "stageLock");
        if (!d.stage)
            return;

        d.transformEdit.delta = dragDelta;
        d.transformEdit.hasDelta = true;
        QStringList errors;
        applyPreparedTransforms(d.stage, d.transformPrepared, d.transformEdit.after, dragDelta, true, &errors);
        for (const QString& error : errors)
            qWarning().noquote() << QStringLiteral("Could not apply transform preview: %1").arg(error);


        // Keep the displayed gizmo center identical to the pivot produced by
        // the authored USD transform. A translated pivot derived only from the
        // mouse delta can differ slightly from stage::worldPivot() for prims
        // with existing pivot/xform-op structure, which otherwise makes the
        // gizmo jump when the drag is released and the pivot is recomputed.
        const bool translating = d.transformActiveAxis == 11
                                 || (d.transformActiveAxis >= 1 && d.transformActiveAxis <= 3);
        if (translating && d.transformPaths.size() == 1) {
            GfVec3d authoredPivot(0.0);
            if (transformSelectionPivot(authoredPivot))
                d.transformPivot = authoredPivot;
        }
    }
    if (!transformMessage.isEmpty())
        Q_EMIT d.glwidget->statusMessage(transformMessage);

    d.glwidget->update();
}
void
ImagingGLWidgetPrivate::endTransformDrag()
{
    if (!d.transformDragging)
        return;

    const QList<SdfPath> paths = d.transformPaths;
    const XformEdit edit = d.transformEdit;
    d.transformDragging = false;
    d.transformSnap = false;
    d.transformActiveAxis = 0;
    d.transformHoverAxis = 0;
    d.transformPaths.clear();
    d.transformEdit = XformEdit();
    d.transformPrepared.clear();

    bool changed = !paths.isEmpty() && paths.size() == edit.before.size() && edit.before.size() == edit.after.size();
    if (changed) {
        changed = false;
        for (qsizetype i = 0; i < edit.before.size(); ++i) {
            if (edit.before.at(i) != edit.after.at(i)) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        if (CommandStack* stack = d.context ? d.context->commandStack() : nullptr)
            stack->execute(new Command(setTransforms(paths, edit)), CommandStack::ExecutionMode::Applied);
    }
    if (d.transformDefersPrimsUpdate) {
        session()->setPrimsUpdate(d.transformPreviousPrimsUpdate);
        d.transformDefersPrimsUpdate = false;
    }

    if (d.stage && paths.size() == 1) {
        READ_LOCKER(locker, d.context->stageLock(), "stageLock");
        GfVec3d releasedPivot(0.0);
        if (d.stage && transformSelectionPivot(releasedPivot)) {}
    }

    Q_EMIT d.glwidget->statusReady();

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
    if (!d.transformEnabled || d.transformMode == TransformMode::None || transformVisiblePaths().isEmpty())
        return;

    if (!d.transformDragging && !d.transformPivotValid) {
        GfVec3d pivot(0.0);
        {
            READ_LOCKER(locker, d.context->stageLock(), "stageLock");
            if (!d.stage || !transformSelectionPivot(pivot))
                return;
        }
        d.transformPivot = pivot;
        d.transformPivotValid = true;
    }

    QPointF center;
    if (!projectWorldToScreen(d.transformPivot, center))
        return;

    constexpr double arrowLength = 13.0;
    constexpr double arrowWidth = 7.0;
    constexpr int ringSegments = 96;

    const QColor colors[4] = { QColor(), style()->color(Style::ColorRole::AxisX),
                               style()->color(Style::ColorRole::AxisY), style()->color(Style::ColorRole::AxisZ) };
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (d.transformMode == TransformMode::Rotate) {
        for (int axis = 1; axis <= 3; ++axis) {
            QColor color = colors[axis];
            const int handle = axis + 3;
            if (handle == d.transformHoverAxis || handle == d.transformActiveAxis)
                color = style()->color(Style::ColorRole::Selection);

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, handle == d.transformActiveAxis ? 3.0 : 1.8, Qt::SolidLine, Qt::RoundCap));

            QPointF previous;
            bool previousValid = false;
            constexpr double maxRingSegmentPixels = 24.0;
            for (int i = 0; i <= ringSegments; ++i) {
                const double angle = (2.0 * Pi * static_cast<double>(i % ringSegments))
                                     / static_cast<double>(ringSegments);
                QPointF current;
                const bool currentValid = transformRotationPoint(axis, angle, current);
                if (previousValid && currentValid) {
                    const double segmentLength = std::hypot(current.x() - previous.x(), current.y() - previous.y());
                    if (std::isfinite(segmentLength) && segmentLength <= maxRingSegmentPixels)
                        painter.drawLine(previous, current);
                }
                previous = current;
                previousValid = currentValid;
            }
        }
    }
    else if (d.transformMode == TransformMode::Move) {
        for (int axis = 1; axis <= 3; ++axis) {
            const QPointF dir = transformAxisDirection(axis);
            if (dir.isNull())
                continue;
            QColor color = colors[axis];
            if (axis == d.transformHoverAxis || axis == d.transformActiveAxis)
                color = style()->color(Style::ColorRole::Selection);
            const QPointF end = center + dir * TransformGizmoSize;
            const QPointF normal(-dir.y(), dir.x());
            painter.setPen(QPen(color, axis == d.transformActiveAxis ? 4.0 : 3.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(center, end - dir * 3.0);
            QPolygonF arrow;
            arrow << end << end - dir * arrowLength + normal * arrowWidth
                  << end - dir * arrowLength - normal * arrowWidth;
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(arrow);
        }
    }
    else if (d.transformMode == TransformMode::Scale) {
        for (int axis = 1; axis <= 3; ++axis) {
            const QPointF dir = transformAxisDirection(axis);
            if (dir.isNull())
                continue;
            QColor color = colors[axis];
            const int handle = axis + 6;
            if (handle == d.transformHoverAxis || handle == d.transformActiveAxis)
                color = style()->color(Style::ColorRole::Selection);
            const QPointF p = center + dir * TransformGizmoSize;
            painter.setPen(QPen(color, handle == d.transformActiveAxis ? 4.0 : 3.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(center, p);
            painter.setPen(QPen(QColor(20, 20, 20, 190), 1.0));
            painter.setBrush(color);
            painter.drawRect(QRectF(p.x() - 5.0, p.y() - 5.0, 10.0, 10.0));
        }
    }

    const int centerHandle = d.transformMode == TransformMode::Move
                                 ? 11
                                 : (d.transformMode == TransformMode::Rotate ? 10 : 12);
    QColor centerColor(35, 35, 35, 220);
    if (d.transformHoverAxis == centerHandle || d.transformActiveAxis == centerHandle)
        centerColor = style()->color(Style::ColorRole::Selection);
    painter.setPen(QPen(QColor(245, 245, 245, 220), 1.5));
    painter.setBrush(centerColor);
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

QRectF
ImagingGLWidgetPrivate::cameraGateRect()
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
ImagingGLWidgetPrivate::widgetViewport()
{
    const QRectF gate = cameraGateRect();
    const qreal dpr = d.glwidget->devicePixelRatioF();
    return GfVec4d(gate.left() * dpr, gate.top() * dpr, gate.width() * dpr, gate.height() * dpr);
}

GfVec4d
ImagingGLWidgetPrivate::renderViewport()
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
ImagingGLWidgetPrivate::drawLetterbox(QPainter& painter)
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
    painter.setPen(QPen(style()->color(Style::ColorRole::Guide), guideWidth));
    painter.drawRect(guideRect);
    painter.restore();
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
    QVector<AxisLine> axes { { "X", style()->color(Style::ColorRole::AxisX), xCam },
                             { "Y", style()->color(Style::ColorRole::AxisY), yCam },
                             { "Z", style()->color(Style::ColorRole::AxisZ), zCam } };
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
    const QColor textColor = d.stage ? style()->color(Style::ColorRole::Text, Style::UIState::Normal)
                                     : style()->color(Style::ColorRole::Text, Style::UIState::Disabled);
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
ImagingGLWidgetPrivate::isSelectionVisible(const SdfPath& path) const
{
    if (path.IsEmpty())
        return false;

    if (d.mask.isEmpty())
        return true;

    const SdfPath primPath = path.IsPropertyPath() ? path.GetPrimPath() : path;
    for (const SdfPath& maskedPath : d.mask) {
        const SdfPath maskedPrimPath = maskedPath.IsPropertyPath() ? maskedPath.GetPrimPath() : maskedPath;
        if (primPath == maskedPrimPath || primPath.HasPrefix(maskedPrimPath) || maskedPrimPath.HasPrefix(primPath))
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
    if (!d.stage || !d.renderEngine)
        return false;
    const GfMatrix4d viewMatrix = pickFrustum.ComputeViewMatrix();
    const GfMatrix4d projectionMatrix = pickFrustum.ComputeProjectionMatrix();
    READ_LOCKER(locker, d.context->stageLock(), "stageLock");
    if (!d.stage)
        return false;

    if (d.mask.isEmpty()) {
        const bool hit = d.renderEngine->testIntersection(pickParams, viewMatrix, projectionMatrix,
                                                          d.stage->GetPseudoRoot(), results);
        return hit;
    }

    bool hitAny = false;
    for (const SdfPath& maskPath : d.mask) {
        UsdPrim root = d.stage->GetPrimAtPath(maskPath);
        if (!root) {
            qWarning().noquote() << "[MaskDebug] invalid pick root=" << QString::fromStdString(maskPath.GetString());
            continue;
        }

        UsdImagingGLEngine::IntersectionResultVector localResults;
        const bool hit = d.renderEngine->testIntersection(pickParams, viewMatrix, projectionMatrix, root,
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

bool
ImagingGLWidget::moveEnabled() const
{
    return p->d.transformMode == TransformMode::Move;
}

void
ImagingGLWidget::setMoveEnabled(bool enabled)
{
    p->updateTransformMode(TransformMode::Move, enabled);
}

bool
ImagingGLWidget::rotateEnabled() const
{
    return p->d.transformMode == TransformMode::Rotate;
}

void
ImagingGLWidget::setRotateEnabled(bool enabled)
{
    p->updateTransformMode(TransformMode::Rotate, enabled);
}

bool
ImagingGLWidget::scaleEnabled() const
{
    return p->d.transformMode == TransformMode::Scale;
}

void
ImagingGLWidget::setScaleEnabled(bool enabled)
{
    p->updateTransformMode(TransformMode::Scale, enabled);
}

void
ImagingGLWidget::close()
{
    p->close();
}

QList<QString>
ImagingGLWidget::rendererAovs() const
{
    if (!p->d.renderEngine)
        return {};
    return p->d.renderEngine->rendererAovs();
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
ImagingGLWidget::dragEnterEvent(QDragEnterEvent* event)
{
    p->dragEnterEvent(event);
}

void
ImagingGLWidget::dragMoveEvent(QDragMoveEvent* event)
{
    p->dragMoveEvent(event);
}

void
ImagingGLWidget::dropEvent(QDropEvent* event)
{
    p->dropEvent(event);
}

void
ImagingGLWidget::contextMenuEvent(QContextMenuEvent* event)
{
    p->contextMenuEvent(event);
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
