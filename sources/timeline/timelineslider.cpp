// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "timelineslider.h"
#include "application.h"
#include "style.h"
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stageviz {
class TimelineSliderPrivate {
public:
    TimelineSliderPrivate();
    ~TimelineSliderPrivate();
    struct Data {
        QList<int> tickValues;
        bool timelineMode = false;
    };
    Data d;
};

TimelineSliderPrivate::TimelineSliderPrivate() {}

TimelineSliderPrivate::~TimelineSliderPrivate() {}

TimelineSlider::TimelineSlider(QWidget* parent)
    : QSlider(parent)
    , p(new TimelineSliderPrivate())
{}

TimelineSlider::~TimelineSlider() = default;

void
TimelineSlider::setTickValues(const QList<int>& values)
{
    p->d.tickValues = values;
    update();
}

void
TimelineSlider::setTimelineMode(bool enabled)
{
    p->d.timelineMode = enabled;
    update();
}

void
TimelineSlider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

    const QColor grooveColor = app()->style()->color(Style::ColorRole::Base);
    const QColor outline = app()->style()->color(Style::ColorRole::BorderAlt);
    const bool timeline = p->d.timelineMode || !p->d.tickValues.isEmpty();
    QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    const int sliderMin = groove.left() + handle.width() / 2;
    const int sliderMax = groove.right() - handle.width() / 2;
    const int sliderLength = std::max(0, sliderMax - sliderMin);

    painter.setRenderHint(QPainter::Antialiasing, false);

    const int min = minimum();
    const int max = maximum();
    if (timeline) {
        painter.fillRect(rect(), app()->style()->color(Style::ColorRole::Item));
        const int baseline = height() - 3;
        const int frameCount = std::max(1, max - min + 1);
        const double pixelsPerFrame = static_cast<double>(sliderLength + 1) / frameCount;
        const double markerInset = std::min(2.0, pixelsPerFrame * 0.15);
        const double frameLeft = sliderMin + (value() - min) * pixelsPerFrame + markerInset;
        const double markerWidth = std::max(0.0, pixelsPerFrame - 2.0 * markerInset);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        QColor frameColor = app()->style()->color(Style::ColorRole::Handle);
        frameColor.setAlphaF(1.0);
        painter.setBrush(frameColor);
        if (isEnabled())
            painter.drawRoundedRect(QRectF(frameLeft, 3, markerWidth, height() - 6), 4, 4);
        painter.setRenderHint(QPainter::Antialiasing, false);

        painter.setPen(QPen(outline, 1));
        painter.drawLine(sliderMin, baseline, sliderMax, baseline);

        QSet<int> tickValues;
        const int targetTickSpacing = 56;
        const double rawStep = max > min && sliderLength > 0
                                   ? static_cast<double>(frameCount) * targetTickSpacing / sliderLength
                                   : 1.0;
        const double magnitude = std::pow(10.0, std::floor(std::log10(std::max(1.0, rawStep))));
        const double normalized = rawStep / magnitude;
        const double step = (normalized <= 1.0   ? 1.0
                             : normalized <= 2.0 ? 2.0
                             : normalized <= 5.0 ? 5.0
                                                 : 10.0)
                            * magnitude;
        const qint64 tickStep = std::max<qint64>(1, static_cast<qint64>(step));
        if (max > min) {
            const qint64 firstTick = static_cast<qint64>(std::ceil(min / step) * step);
            for (qint64 tick = firstTick; tick <= max; tick += tickStep) {
                tickValues.insert(static_cast<int>(tick));
            }
        }
        for (const int tick : p->d.tickValues) {
            if (tick >= min && tick <= max)
                tickValues.insert(tick);
        }

        painter.setPen(QPen(outline, 1));
        for (const int tick : tickValues) {
            const int x = static_cast<int>(std::round(sliderMin + (tick - min) * pixelsPerFrame));
            painter.drawLine(x, baseline - 12, x, baseline);
        }
    }
    else {
        painter.setPen(QPen(outline, 1));
        painter.setBrush(grooveColor);
        painter.drawRoundedRect(groove.adjusted(0, 0, -1, -1), 2, 2);
        if (max > min) {
            painter.setPen(QPen(app()->style()->color(Style::ColorRole::Base), 2));
            const int y1 = groove.center().y() - 2;
            const int y2 = groove.center().y() + 3;
            for (int value = min; value <= max; ++value) {
                const int pos = QStyle::sliderPositionFromValue(min, max, value, sliderLength);
                const int x = sliderMin + pos;
                painter.drawLine(x, y1, x, y2);
            }
        }
        opt.subControls = QStyle::SC_SliderHandle;
        style()->drawComplexControl(QStyle::CC_Slider, &opt, &painter, this);
    }
}

void
TimelineSlider::mousePressEvent(QMouseEvent* event)
{
    if ((!p->d.timelineMode && p->d.tickValues.isEmpty()) || event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    const int sliderMin = groove.left() + handle.width() / 2;
    const int sliderLength = std::max(0, groove.right() - handle.width() / 2 - sliderMin);

    setSliderDown(true);
    const int frameCount = std::max(1, maximum() - minimum() + 1);
    const double fraction = static_cast<double>(static_cast<int>(event->position().x()) - sliderMin)
                            / std::max(1, sliderLength + 1);
    setValue(minimum() + std::clamp(static_cast<int>(fraction * frameCount), 0, frameCount - 1));
    event->accept();
}

void
TimelineSlider::mouseMoveEvent(QMouseEvent* event)
{
    if ((p->d.timelineMode || !p->d.tickValues.isEmpty()) && isSliderDown() && (event->buttons() & Qt::LeftButton)) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        const int sliderMin = groove.left() + handle.width() / 2;
        const int sliderLength = std::max(0, groove.right() - handle.width() / 2 - sliderMin);
        const int frameCount = std::max(1, maximum() - minimum() + 1);
        const double fraction = static_cast<double>(static_cast<int>(event->position().x()) - sliderMin)
                                / std::max(1, sliderLength + 1);
        setValue(minimum() + std::clamp(static_cast<int>(fraction * frameCount), 0, frameCount - 1));
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void
TimelineSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if ((p->d.timelineMode || !p->d.tickValues.isEmpty()) && event->button() == Qt::LeftButton) {
        setSliderDown(false);
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

}  // namespace stageviz
