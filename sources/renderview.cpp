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
namespace {

    TfToken stageUpToken(Session::StageUp stageUp)
    {
        switch (stageUp) {
        case Session::StageUp::Z: return UsdGeomTokens->z;
        case Session::StageUp::Y:
        default: return UsdGeomTokens->y;
        }
    }

}  // namespace

class RenderViewPrivate : public QObject {
public:
    void init();
    ImagingGLWidget* imageGLWidget();

public Q_SLOTS:
    void updateBoundingBox(const GfBBox3d& bbox);
    void updateMask(const QList<SdfPath>& paths);
    void updatePrims(const NoticeBatch& batch);
    void updateSelection(const QList<SdfPath>& paths);
    void updateStage(UsdStageRefPtr stage, Session::LoadPolicy policy, Session::StageStatus status);
    void updateAuxiliary(UsdStageRefPtr auxiliary);
    void updateStageUp(Session::StageUp stageUp);
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
    connect(session(), &Session::stageUpChanged, this, &RenderViewPrivate::updateStageUp);
}

ImagingGLWidget*
RenderViewPrivate::imageGLWidget()
{
    return d.ui->imagingGLWidget;
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
    Q_UNUSED(stage);
    Q_UNUSED(policy);

    if (status == Session::StageStatus::Loaded) {
        // Resolve the application-level stage-up enum here at the RenderView
        // boundary. ImagingGLWidget only needs to know the USD/render-space token.
        imageGLWidget()->updateStageUp(stageUpToken(session()->stageUp()));
        imageGLWidget()->updateStage(session()->stage());
    }
    else {
        imageGLWidget()->close();
    }
}

void
RenderViewPrivate::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    imageGLWidget()->updateAuxiliary(auxiliary);
}

void
RenderViewPrivate::updateStageUp(Session::StageUp stageUp)
{
    imageGLWidget()->updateStageUp(stageUpToken(stageUp));
}

void
RenderViewPrivate::captureReady(qint64 elapsed)
{
    const QString msg = QStringLiteral("Capture finished in %1 ms").arg(elapsed);
    session()->notifyStatus(Session::Notify::Status::Success, msg);
}

void
RenderViewPrivate::renderReady(qint64 elapsed)
{
    const qint64 thresholdMs = 500;
    if (elapsed > thresholdMs) {
        const QString msg = QStringLiteral("Render finished in %1 ms").arg(elapsed);
        session()->notifyStatus(Session::Notify::Status::Success, msg);
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

void
RenderView::updateAuxiliary(UsdStageRefPtr auxiliary)
{
    p->updateAuxiliary(auxiliary);
}

}  // namespace stageviz
