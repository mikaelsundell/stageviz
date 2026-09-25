// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QPoint>

namespace stageviz {

/**
 * @class SpinBox
 * @brief Compact floating-point editor with mouse scrubbing and direct value entry.
 *
 * Extends QDoubleSpinBox with interactive mouse scrubbing so numeric values can
 * be adjusted without requiring a separate slider.
 *
 * Hovering the value shows a horizontal scrub cursor. A normal left-button drag
 * adjusts the value; dragging right or upward increases it, while dragging left
 * or downward decreases it. Shift enables fine adjustment and Control snaps
 * the dragged value to whole-number increments.
 *
 * The value editor is read-only during normal interaction. Double-clicking the
 * value enables direct text entry. Pressing Enter or moving focus away commits
 * the value and returns the control to scrub mode. The standard spin buttons
 * remain available for step-wise adjustment.
 */
class SpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    /**
     * @brief Creates a scrubbable floating-point spin box.
     * @param parent Parent widget.
     */
    explicit SpinBox(QWidget* parent = nullptr);

    /**
     * @brief Releases the spin box.
     */
    ~SpinBox() override;

    /**
     * @brief Sets the multiplier applied to mouse scrubbing.
     *
     * The effective adjustment also depends on the configured single step and
     * active keyboard modifiers.
     *
     * @param sensitivity Scrubbing sensitivity multiplier.
     */
    void setScrubSensitivity(double sensitivity);

    /**
     * @brief Returns the current mouse scrubbing sensitivity.
     * @return Scrubbing sensitivity multiplier.
     */
    double scrubSensitivity() const;

Q_SIGNALS:
    /**
     * @brief Emitted when an interactive mouse scrub begins.
     */
    void scrubStarted();

    /**
     * @brief Emitted continuously while the value is being scrubbed.
     * @param value Current value.
     */
    void scrubbed(double value);

    /**
     * @brief Emitted when an interactive mouse scrub ends.
     * @param value Final value.
     */
    void scrubFinished(double value);

protected:
    /**
     * @brief Filters value-editor events for hover, scrubbing, and direct editing.
     * @param watched Object receiving the event.
     * @param event Incoming event.
     * @return True when the event has been handled.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    /**
     * @brief Starts tracking a possible scrub when the spin box itself is pressed.
     * @param event Mouse press event.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Updates the scrub when the spin box itself receives mouse movement.
     * @param event Mouse move event.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Finishes the current scrub when the spin box itself is released.
     * @param event Mouse release event.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Enables direct text entry when the value is double-clicked.
     * @param event Mouse double-click event.
     */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void beginPress(QMouseEvent* event);
    bool updateScrub(QMouseEvent* event);
    bool finishPress(QMouseEvent* event);
    void beginEditing();
    void finishEditing();
    void updateValueCursor();

    QPoint m_pressGlobalPosition;
    double m_pressValue = 0.0;
    double m_scrubSensitivity = 0.1;
    bool m_pressed = false;
    bool m_scrubbing = false;
    bool m_editing = false;
};

}  // namespace stageviz
