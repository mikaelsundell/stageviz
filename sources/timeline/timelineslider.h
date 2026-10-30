// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QList>
#include <QScopedPointer>
#include <QSlider>

class QMouseEvent;

namespace stageviz {

class TimelineSliderPrivate;

/**
 * @class TimelineSlider
 * @brief Custom slider with overridden tick rendering.
 *
 * Provides a QSlider implementation with manually rendered tick marks. When
 * custom tick values are set, the slider draws a frame ruler and a scrub
 * indicator for the current value.
 */
class TimelineSlider : public QSlider {
    Q_OBJECT
public:
    /**
     * @brief Constructs the slider.
     *
     * @param parent Optional parent widget.
     */
    explicit TimelineSlider(QWidget* parent = nullptr);

    /**
     * @brief Sets specific values to highlight as custom tick marks.
     *
     * When values are provided, they are added to adaptive ruler ticks. An
     * empty list preserves the standard slider appearance.
     *
     * @param values Slider values to mark.
     */
    void setTickValues(const QList<int>& values);

    /**
     * @brief Enables timeline ruler rendering and frame-based scrubbing.
     *
     * Timeline mode keeps the ruler appearance when disabled and paints the
     * selected frame as a one-frame-wide block.
     *
     * @param enabled Whether timeline mode is enabled.
     */
    void setTimelineMode(bool enabled);

    /**
     * @brief Destroys the slider.
     */
    ~TimelineSlider() override;

protected:
    /**
     * @brief Paints the slider and custom tick marks.
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief Starts timeline scrubbing at the clicked frame.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Updates the current frame while dragging the timeline slider.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Ends timeline scrubbing when the mouse button is released.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QScopedPointer<TimelineSliderPrivate> p;
};

}  // namespace stageviz
