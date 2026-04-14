// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderview.h"
#include "application.h"
#include "notice.h"
#include "usdutils.h"
#include "viewcontext.h"
#include <QPointer>

// generated files
#include "ui_renderview.h"

namespace stageviz {
class RenderViewPrivate : public QObject {
public:
    void init();
    ImagingGLWidget* imageGLWidget();
    ViewCamera camera();
    void frameAll();
    void frameSelected();
    void resetView();

public Q_SLOTS:
    void boundingBoxChanged(const GfBBox3d& bbox);
    void maskChanged(const QList<SdfPath>& paths);
    void primsChanged(const NoticeBatch& batch);
    void selectionChanged(const QList<SdfPath>& paths);
    void stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void captureReady(qint64 elapsed);
    void renderReady(qint64 elapsed);

public:
    struct Data {
        QScopedPointer<ViewContext> context;
        QScopedPointer<Ui_RenderView> ui;
        QPointer<RenderView> view;
    };
    Data d;
};

void
RenderViewPrivate::init()
{
    d.ui.reset(new Ui_RenderView());
    d.ui->setupUi(d.view.data());
    d.context.reset(new ViewContext(d.view.data()));
    d.context->setStageLock(session()->stageLock());
    d.context->setCommandStack(session()->commandStack());
    imageGLWidget()->setContext(d.context.data());
    // connect
    connect(imageGLWidget(), &ImagingGLWidget::captureReady, this, &RenderViewPrivate::captureReady);
    connect(imageGLWidget(), &ImagingGLWidget::renderReady, this, &RenderViewPrivate::renderReady);
    connect(session(), &Session::boundingBoxChanged, this, &RenderViewPrivate::boundingBoxChanged);
    connect(session(), &Session::maskChanged, this, &RenderViewPrivate::maskChanged);
    connect(session(), &Session::primsChanged, this, &RenderViewPrivate::primsChanged);
    connect(session(), &Session::stageChanged, this, &RenderViewPrivate::stageChanged);
    connect(session()->selectionList(), &SelectionList::selectionChanged, this, &RenderViewPrivate::selectionChanged);
}

ImagingGLWidget*
RenderViewPrivate::imageGLWidget()
{
    return d.ui->imagingGLWidget;
}

ViewCamera
RenderViewPrivate::camera()
{
    return imageGLWidget()->viewCamera();
}

void
RenderViewPrivate::frameAll()
{
    if (session()->isLoaded()) {
        imageGLWidget()->frame(session()->boundingBox());
    }
}

void
RenderViewPrivate::frameSelected()
{
    if (session()->selectionList()->paths().size()) {
        imageGLWidget()->frame(stage::boundingBox(session()->stage(), session()->selectionList()->paths()));
    }
}

void
RenderViewPrivate::resetView()
{
    if (session()->isLoaded()) {
        imageGLWidget()->resetView();
    }
}

void
RenderViewPrivate::boundingBoxChanged(const GfBBox3d& bbox)
{
    imageGLWidget()->updateBoundingBox(bbox);
}

void
RenderViewPrivate::maskChanged(const QList<SdfPath>& paths)
{
    imageGLWidget()->updateMask(paths);
}

void
RenderViewPrivate::primsChanged(const NoticeBatch& batch)
{
    imageGLWidget()->updatePrims(batch);
}

void
RenderViewPrivate::selectionChanged(const QList<SdfPath>& paths)
{
    imageGLWidget()->updateSelection(paths);
}

void
RenderViewPrivate::stageChanged(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    if (status == Session::StageStatus::Loaded) {
        imageGLWidget()->updateStage(session()->stage());
    }
    else {
        imageGLWidget()->close();
    }
}

void
RenderViewPrivate::captureReady(qint64 elapsed)
{
    const QString msg = QStringLiteral("Capture finished in %1 ms").arg(elapsed);
    session()->notifyStatus(Session::Notify::Status::Info, msg);
}

void
RenderViewPrivate::renderReady(qint64 elapsed)
{
    const qint64 thresholdMs = 500;
    if (elapsed > thresholdMs) {
        const QString msg = QStringLiteral("Render finished in %1 ms").arg(elapsed);
        session()->notifyStatus(Session::Notify::Status::Info, msg);
    }
}

RenderView::RenderView(QWidget* parent)
    : QWidget(parent)
    , p(new RenderViewPrivate())
{
    p->d.view = this;
    p->init();
}

RenderView::~RenderView() = default;

QImage
RenderView::captureImage()
{
    return p->imageGLWidget()->captureImage();
}

void
RenderView::frameAll()
{
    p->frameAll();
}

void
RenderView::frameSelected()
{
    p->frameSelected();
}

void
RenderView::resetView()
{
    p->resetView();
}

QColor
RenderView::backgroundColor() const
{
    return p->imageGLWidget()->clearColor();
}

void
RenderView::setBackgroundColor(const QColor& color)
{
    p->imageGLWidget()->setClearColor(color);
}

RenderView::RenderMode
RenderView::renderMode() const
{
    ImagingGLWidget::DrawMode drawMode = p->imageGLWidget()->drawMode();
    switch (drawMode) {
    case ImagingGLWidget::DrawMode::Wireframe:
    case ImagingGLWidget::DrawMode::WireframeOnSurface: return RenderMode::Wireframe;

    default: return RenderMode::Shaded;
    }
}

void
RenderView::setRenderMode(RenderMode renderMode)
{
    switch (renderMode) {
    case RenderMode::Shaded: p->imageGLWidget()->setDrawMode(ImagingGLWidget::DrawMode::ShadedSmooth); break;

    case RenderMode::Wireframe: p->imageGLWidget()->setDrawMode(ImagingGLWidget::DrawMode::WireframeOnSurface); break;
    }
    p->imageGLWidget()->update();
}

bool
RenderView::defaultCameraLightEnabled() const
{
    return p->imageGLWidget()->defaultCameraLightEnabled();
}
void
RenderView::setDefaultCameraLightEnabled(bool enabled)
{
    p->imageGLWidget()->enableDefaultCameraLight(enabled);
}

bool
RenderView::sceneLightsEnabled() const
{
    return p->imageGLWidget()->sceneLightsEnabled();
}

void
RenderView::setSceneLightsEnabled(bool enabled)
{
    p->imageGLWidget()->enableSceneLights(enabled);
}

bool
RenderView::sceneMaterialsEnabled() const
{
    return p->imageGLWidget()->sceneShadersEnabled();
}

void
RenderView::setSceneMaterialsEnabled(bool enabled)
{
    p->imageGLWidget()->enableSceneShaders(enabled);
}

bool
RenderView::sceneTreeEnabled() const
{
    return p->imageGLWidget()->sceneTreeEnabled();
}

void
RenderView::setSceneTreeEnabled(bool enabled)
{
    p->imageGLWidget()->enableSceneTree(enabled);
}

bool
RenderView::gpuPerformanceEnabled() const
{
    return p->imageGLWidget()->gpuPerformanceEnabled();
}

void
RenderView::setGpuPerformanceEnabled(bool enabled)
{
    p->imageGLWidget()->enableGpuPerformance(enabled);
}

bool
RenderView::cameraAxisEnabled() const
{
    return p->imageGLWidget()->cameraAxisEnabled();
}

void
RenderView::setCameraAxisEnabled(bool enabled)
{
    p->imageGLWidget()->enableCameraAxis(enabled);
}

void
RenderView::captureVisible()
{
    p->imageGLWidget()->captureVisible();
}

void
RenderView::clearVisibleCapture()
{
    p->imageGLWidget()->clearVisibleCapture();
}

QList<SdfPath>
RenderView::visibleCapturePaths() const
{
    return p->imageGLWidget()->visibleCapturePaths();
}

}  // namespace stageviz
