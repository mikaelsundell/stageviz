// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "gizmowidget.h"
#include "application.h"
#include "responsewidget.h"
#include "session.h"
#include "settings.h"
#include "spinbox.h"
#include "ui.h"
#include "viewportgizmo.h"
#include "viewportwidget.h"
#include <QComboBox>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>

// generated files
#include "ui_gizmowidget.h"

namespace stageviz {

class GizmoWidgetPrivate : public QObject {
public:
    void init();
    void update();

    struct Data {
        QScopedPointer<Ui_GizmoWidget> ui;
        QPointer<GizmoWidget> widget;
        QPointer<ViewportWidget> viewport;
        bool active = false;
    };
    Data d;
};

void
GizmoWidgetPrivate::init()
{
    d.ui.reset(new Ui_GizmoWidget());
    d.ui->setupUi(d.widget.data());
    auto* responseWidget = new ResponseWidget(d.widget.data());
    responseWidget->addGroup(600, { d.ui->mode, d.ui->referenceFrame });
    d.ui->referenceFrame->setItemData(0, static_cast<int>(ViewportGizmo::ReferenceFrame::World));
    d.ui->referenceFrame->setItemData(1, static_cast<int>(ViewportGizmo::ReferenceFrame::Object));
    d.ui->referenceFrame->setItemData(2, static_cast<int>(ViewportGizmo::ReferenceFrame::Parent));
    d.ui->referenceFrame->setItemData(3, static_cast<int>(ViewportGizmo::ReferenceFrame::Camera));
    const int defaultFrame = static_cast<int>(ViewportGizmo::ReferenceFrame::Object);
    int frame = settings() ? settings()->value("transformReferenceFrame", defaultFrame).toInt() : defaultFrame;
    if (d.ui->referenceFrame->findData(frame) < 0)
        frame = defaultFrame;
    const int frameIndex = d.ui->referenceFrame->findData(frame);
    if (frameIndex >= 0)
        d.ui->referenceFrame->setCurrentIndex(frameIndex);
    if (d.viewport)
        d.viewport->setTransformReferenceFrame(static_cast<ViewportGizmo::ReferenceFrame>(frame));
    // connect
    connect(d.ui->referenceFrame, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const int frame = d.ui->referenceFrame->itemData(index).toInt();
        if (d.viewport)
            d.viewport->setTransformReferenceFrame(static_cast<ViewportGizmo::ReferenceFrame>(frame));
        if (settings())
            settings()->setValue("transformReferenceFrame", frame);
    });
    const auto values = [this]() {
        return GfVec3d(d.ui->valueX->value(), d.ui->valueY->value(), d.ui->valueZ->value());
    };
    const auto updateValues = [this, values]() {
        if (!d.viewport)
            return;
        QString error;
        if (!d.viewport->updateNumericTransform(values(), error) && !error.isEmpty())
            Q_EMIT d.widget->statusMessage(error);
    };
    for (SpinBox* field : { d.ui->valueX, d.ui->valueY, d.ui->valueZ }) {
        connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [updateValues](double) { updateValues(); });
        connect(field, &QDoubleSpinBox::editingFinished, this, [this]() {
            if (d.viewport)
                d.viewport->finishNumericTransform();
        });
        connect(field, &SpinBox::scrubFinished, this, [this](double) {
            if (d.viewport)
                d.viewport->finishNumericTransform();
        });
        connect(field, &SpinBox::scrubStarted, this, [this]() {
            if (!d.viewport)
                return;
            QString error;
            if (!d.viewport->beginNumericTransform(error) && !error.isEmpty())
                Q_EMIT d.widget->statusMessage(error);
        });
    }
    connect(d.viewport, &ViewportWidget::transformStateChanged, this, &GizmoWidgetPrivate::update);
    connect(d.viewport, &ViewportWidget::transformSelectionChanged, this, [this]() { update(); });
    connect(d.viewport, &ViewportWidget::transformInputValuesChanged, this, [this](const GfVec3d& values) {
        const QSignalBlocker blockerX(d.ui->valueX);
        const QSignalBlocker blockerY(d.ui->valueY);
        const QSignalBlocker blockerZ(d.ui->valueZ);
        d.ui->valueX->setValue(values[0]);
        d.ui->valueY->setValue(values[1]);
        d.ui->valueZ->setValue(values[2]);
    });
    connect(session(), &Session::stageChanged, this, [this]() { update(); });
    update();
}

void
GizmoWidgetPrivate::update()
{
    if (!d.viewport)
        return;
    const bool toolActive = d.viewport->transformEnabled() && !d.viewport->pivotEnabled();
    const bool active = toolActive && d.viewport->transformHandleAvailable();
    d.ui->frame->setVisible(active);
    if (d.active != active) {
        d.active = active;
        if (!active)
            d.viewport->finishNumericTransform();
        Q_EMIT d.widget->activeChanged(active);
    }
    if (!toolActive)
        return;

    QString mode;
    QString suffix;
    int decimals = 3;
    if (d.viewport->rotateEnabled()) {
        mode = tr("Rotate");
        suffix = QStringLiteral("°");
        decimals = 2;
    }
    else if (d.viewport->scaleEnabled()) {
        mode = tr("Scale");
        suffix = QStringLiteral("×");
    }
    else {
        mode = tr("Move");
    }

    d.ui->mode->setText(mode);
    auto setField = [decimals, suffix](SpinBox* field) {
        field->setDecimals(decimals);
        field->setSuffix(suffix);
    };

    const QSignalBlocker blockerX(d.ui->valueX);
    const QSignalBlocker blockerY(d.ui->valueY);
    const QSignalBlocker blockerZ(d.ui->valueZ);
    setField(d.ui->valueX);
    setField(d.ui->valueY);
    setField(d.ui->valueZ);
    const int frameIndex = d.ui->referenceFrame->findData(static_cast<int>(d.viewport->transformReferenceFrame()));
    if (frameIndex >= 0 && d.ui->referenceFrame->currentIndex() != frameIndex) {
        const QSignalBlocker blocker(d.ui->referenceFrame);
        d.ui->referenceFrame->setCurrentIndex(frameIndex);
    }
}

GizmoWidget::GizmoWidget(ViewportWidget* viewport, QWidget* parent)
    : QWidget(parent)
    , p(new GizmoWidgetPrivate())
{
    p->d.widget = this;
    p->d.viewport = viewport;
    p->init();
}

GizmoWidget::~GizmoWidget() = default;

bool
GizmoWidget::isActive() const
{
    return p->d.active;
}

void
GizmoWidget::refresh()
{
    p->update();
}

}  // namespace stageviz
