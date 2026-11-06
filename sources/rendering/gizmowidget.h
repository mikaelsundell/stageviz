// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QScopedPointer>
#include <QWidget>

namespace stageviz {

class GizmoWidgetPrivate;
class ViewportWidget;

/**
 * @class GizmoWidget
 * @brief Numeric controls for the active viewport transform tool.
 *
 * Displays coordinate-frame, translation, rotation, scale, and snapping
 * controls for a RenderView's viewport manipulator.
 */
class GizmoWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs transform controls for a viewport.
     *
     * @param viewport Viewport whose transform tool is controlled.
     * @param parent Optional parent widget.
     */
    explicit GizmoWidget(ViewportWidget* viewport, QWidget* parent = nullptr);

    /**
     * @brief Destroys the gizmo controls.
     */
    ~GizmoWidget() override;

    /**
     * @brief Returns whether the active transform tool needs this bar.
     */
    bool isActive() const;

    /**
     * @brief Refreshes the bar from the viewport's current transform state.
     */
    void refresh();

Q_SIGNALS:
    /**
     * @brief Emitted when this bar should replace timeline playback controls.
     */
    void activeChanged(bool active);

    /**
     * @brief Emitted when a numeric transform cannot be applied.
     */
    void statusMessage(const QString& message);

private:
    QScopedPointer<GizmoWidgetPrivate> p;
};

}  // namespace stageviz
