// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QDockWidget>

namespace stageviz {

class DockWidgetPrivate;

/**
 * @class DockWidget
 * @brief Dockable panel used by the viewer interface.
 *
 * Extends QDockWidget with custom mouse interaction behavior
 * used by the viewer layout. The widget is typically used to
 * host tools such as the stage tree, property inspector, or
 * statistics panels.
 */
class DockWidget : public QDockWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the dock widget.
     *
     * @param parent Optional parent widget.
     */
    DockWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the DockWidget instance.
     */
    virtual ~DockWidget();

protected:
    /** @name Mouse Interaction */
    ///@{

    /**
     * @brief Handles mouse press events.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse move events.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    ///@}

private:
    QScopedPointer<DockWidgetPrivate> p;
};

}  // namespace stageviz
