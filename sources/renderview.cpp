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
    void frameAll();
    void frameSelected();
    void resetView();

public Q_SLOTS:
    void updateBoundingBox(const GfBBox3d& bbox);
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
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
    d.context->setSelectionList(session()->selectionList());
    d.context->setViewState(session()->viewState());
    imageGLWidget()->setContext(d.context.data());
    // connect
    connect(imageGLWidget(), &ImagingGLWidget::captureReady, this, &RenderViewPrivate::captureReady);
    connect(imageGLWidget(), &ImagingGLWidget::renderReady, this, &RenderViewPrivate::renderReady);
    connect(session(), &Session::boundingBoxChanged, this, &RenderViewPrivate::updateBoundingBox);
    connect(session(), &Session::maskChanged, this, &RenderViewPrivate::updateMask);
    connect(session(), &Session::primsChanged, this, &RenderViewPrivate::updatePrims);
    connect(session(), &Session::stageChanged, this, &RenderViewPrivate::updateStage);
}

ImagingGLWidget*
RenderViewPrivate::imageGLWidget()
{
    return d.ui->imagingGLWidget;
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
RenderViewPrivate::updateBoundingBox(const GfBBox3d& bbox)
{
    imageGLWidget()->updateBoundingBox(bbox);
}

void
RenderViewPrivate::updateMask(const QList<SdfPath>& paths)
{
    imageGLWidget()->updateMask(paths);
}

void
RenderViewPrivate::updatePrims(const NoticeBatch& batch)
{
    imageGLWidget()->updatePrims(batch);
}

void
RenderViewPrivate::updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
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

RenderView::ComplexityLevel
RenderView::complexityLevel() const
{
    ImagingGLWidget::ComplexityLevel complexityLevel = p->imageGLWidget()->complexityLevel();
    switch (complexityLevel) {
    case ImagingGLWidget::ComplexityLevel::Low: return ComplexityLevel::Low;
    case ImagingGLWidget::ComplexityLevel::Medium: return ComplexityLevel::Medium;
    case ImagingGLWidget::ComplexityLevel::High: return ComplexityLevel::High;
    case ImagingGLWidget::ComplexityLevel::VeryHigh: return ComplexityLevel::VeryHigh;
    default: return ComplexityLevel::Medium;
    }
}

void
RenderView::setComplexityLevel(ComplexityLevel complexityLevel)
{
    switch (complexityLevel) {
    case ComplexityLevel::Low: p->imageGLWidget()->setComplexityLevel(ImagingGLWidget::ComplexityLevel::Low); break;
    case ComplexityLevel::Medium:
        p->imageGLWidget()->setComplexityLevel(ImagingGLWidget::ComplexityLevel::Medium);
        break;
    case ComplexityLevel::High: p->imageGLWidget()->setComplexityLevel(ImagingGLWidget::ComplexityLevel::High); break;
    case ComplexityLevel::VeryHigh:
        p->imageGLWidget()->setComplexityLevel(ImagingGLWidget::ComplexityLevel::VeryHigh);
        break;
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
    p->imageGLWidget()->setDefaultCameraLightEnabled(enabled);
}

bool
RenderView::sceneLightsEnabled() const
{
    return p->imageGLWidget()->sceneLightsEnabled();
}

void
RenderView::setSceneLightsEnabled(bool enabled)
{
    p->imageGLWidget()->setSceneLightsEnabled(enabled);
}

bool
RenderView::sceneMaterialsEnabled() const
{
    return p->imageGLWidget()->sceneShadersEnabled();
}

void
RenderView::setSceneMaterialsEnabled(bool enabled)
{
    p->imageGLWidget()->setSceneShadersEnabled(enabled);
}

bool
RenderView::sceneStatsEnabled() const
{
    return p->imageGLWidget()->sceneStatsEnabled();
}

void
RenderView::setSceneStatsEnabled(bool enabled)
{
    p->imageGLWidget()->setSceneStatsEnabled(enabled);
}

bool
RenderView::performanceStatsEnabled() const
{
    return p->imageGLWidget()->performanceStatsEnabled();
}

void
RenderView::setPerformanceStatsEnabled(bool enabled)
{
    p->imageGLWidget()->setPerformanceStatsEnabled(enabled);
}

bool
RenderView::cameraAxisEnabled() const
{
    return p->imageGLWidget()->cameraAxisEnabled();
}

void
RenderView::setCameraAxisEnabled(bool enabled)
{
    p->imageGLWidget()->setCameraAxisEnabled(enabled);
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
