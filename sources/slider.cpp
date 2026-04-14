// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "slider.h"
#include "application.h"
#include "style.h"
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

namespace stageviz {
class SliderPrivate {
public:
    SliderPrivate();
    ~SliderPrivate();
    struct Data {};
    Data d;
};

SliderPrivate::SliderPrivate() {}

SliderPrivate::~SliderPrivate() {}

Slider::Slider(QWidget* parent)
    : QSlider(parent)
    , p(new SliderPrivate())
{}

Slider::~Slider() = default;

void
Slider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

    QColor grooveColor = app()->style()->color(Style::ColorRole::Base);
    QColor outline = app()->style()->color(Style::ColorRole::BorderAlt);

    painter.setPen(QPen(outline, 1));
    painter.setBrush(grooveColor);
    painter.drawRoundedRect(groove.adjusted(0, 0, -1, -1), 2, 2);
    QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    int sliderMin = groove.left() + handle.width() / 2;
    int sliderMax = groove.right() - handle.width() / 2;
    int sliderLength = sliderMax - sliderMin;

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(app()->style()->color(Style::ColorRole::Base), 2));

    int min = minimum();
    int max = maximum();
    if (max > min) {
        int y1 = groove.center().y() - 2;
        int y2 = groove.center().y() + 3;

        for (int v = min; v <= max; ++v) {
            int pos = QStyle::sliderPositionFromValue(min, max, v, sliderLength);
            int x = sliderMin + pos;
            painter.drawLine(x, y1, x, y2);
        }
    }
    opt.subControls = QStyle::SC_SliderHandle;
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &painter, this);
}

}  // namespace stageviz
