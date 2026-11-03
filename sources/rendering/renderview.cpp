// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "renderview.h"
#include "application.h"
#include "notice.h"
#include "viewcontext.h"
#include <QPointer>
#include <pxr/usd/usdGeom/tokens.h>

// generated files
#include "ui_renderview.h"

namespace stageviz {

class RenderViewPrivate : public QObject {
public:
    void init();
    ViewportWidget* viewportWidget();

public Q_SLOTS:
    void updateBoundingBox(const GfBBox3d& bbox);
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void updateAuxiliary(UsdStageRefPtr auxiliary);
    void updateStageUp(Session::StageUp stageUp);

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
    viewportWidget()->setContext(d.context.data());
    // connect
    connect(viewportWidget(), &ViewportWidget::captureReady, d.view, &RenderView::captureReady);
    connect(viewportWidget(), &ViewportWidget::renderReady, d.view, &RenderView::renderReady);
    connect(viewportWidget(), &ViewportWidget::statusMessage, d.view, &RenderView::statusMessage);
    connect(viewportWidget(), &ViewportWidget::statusReady, d.view, &RenderView::statusReady);
    connect(session(), &Session::boundingBoxChanged, this, &RenderViewPrivate::updateBoundingBox);
    connect(session(), &Session::maskChanged, this, &RenderViewPrivate::updateMask);
    connect(session(), &Session::primsChanged, this, &RenderViewPrivate::updatePrims);
    connect(session(), &Session::redrawRequested, viewportWidget(), QOverload<>::of(&QWidget::update));
    connect(session(), &Session::stageChanged, this, &RenderViewPrivate::updateStage);
    connect(session(), &Session::stageUpChanged, this, &RenderViewPrivate::updateStageUp);
}

ViewportWidget*
RenderViewPrivate::viewportWidget()
{
    return d.ui->imagingGLWidget;
}

void
RenderViewPrivate::updateBoundingBox(const GfBBox3d& bbox)
{
    viewportWidget()->updateBoundingBox(bbox);
}

void
RenderViewPrivate::updateMask(const QList<SdfPath>& paths)
{
    viewportWidget()->updateMask(paths);
}

void
RenderViewPrivate::updatePrims(const NoticeBatch& batch)
{
    viewportWidget()->updatePrims(batch);
}

void
RenderViewPrivate::updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status)
{
    Q_UNUSED(stage);
    Q_UNUSED(policy);

    if (status == Session::StageStatus::Loaded) {
        const TfToken stageUp = session()->stageUp() == Session::StageUp::Z ? UsdGeomTokens->z : UsdGeomTokens->y;

        viewportWidget()->updateStageUp(stageUp);
        viewportWidget()->updateStage(session()->stage());
    }
    else {
        viewportWidget()->close();
    }
}

void
RenderViewPrivate::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    viewportWidget()->updateAuxiliary(auxiliary);
}

void
RenderViewPrivate::updateStageUp(Session::StageUp stageUp)
{
    const TfToken token = stageUp == Session::StageUp::Z ? UsdGeomTokens->z : UsdGeomTokens->y;

    viewportWidget()->updateStageUp(token);
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
    return p->viewportWidget()->captureImage();
}

ViewportCapture
RenderView::captureViewport(QString& error)
{
    return p->viewportWidget()->captureViewport(error);
}

QJsonObject
RenderView::inspectPrim(const SdfPath& path, QString& error)
{
    return p->viewportWidget()->inspectPrim(path, error);
}

void
RenderView::captureVisible()
{
    p->viewportWidget()->captureVisible();
}

void
RenderView::clearVisibleCapture()
{
    p->viewportWidget()->clearVisibleCapture();
}

QList<SdfPath>
RenderView::visibleCapturePaths() const
{
    return p->viewportWidget()->visibleCapturePaths();
}

bool
RenderView::transformEnabled() const
{
    return p->viewportWidget()->transformEnabled();
}

void
RenderView::setTransformEnabled(bool enabled)
{
    p->viewportWidget()->setTransformEnabled(enabled);
}

bool
RenderView::pivotEnabled() const
{
    return p->viewportWidget()->pivotEnabled();
}

void
RenderView::setPivotEnabled(bool enabled)
{
    p->viewportWidget()->setPivotEnabled(enabled);
}

bool
RenderView::moveEnabled() const
{
    return p->viewportWidget()->moveEnabled();
}

void
RenderView::setMoveEnabled(bool enabled)
{
    p->viewportWidget()->setMoveEnabled(enabled);
}

bool
RenderView::rotateEnabled() const
{
    return p->viewportWidget()->rotateEnabled();
}

void
RenderView::setRotateEnabled(bool enabled)
{
    p->viewportWidget()->setRotateEnabled(enabled);
}

bool
RenderView::scaleEnabled() const
{
    return p->viewportWidget()->scaleEnabled();
}

void
RenderView::setScaleEnabled(bool enabled)
{
    p->viewportWidget()->setScaleEnabled(enabled);
}

void
RenderView::setTimeCode(double timeCode)
{
    p->viewportWidget()->setTimeCode(timeCode);
}

void
RenderView::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    p->updateAuxiliary(auxiliary);
}

}  // namespace stageviz
