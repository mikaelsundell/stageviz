// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QObject>

/**
 * @class MouseEvent
 * @brief Event filter emitting signals for mouse interactions.
 *
 * Installs an event filter on a QObject and emits signals when
 * mouse events are detected. This provides a lightweight way to
 * observe mouse activity without subclassing the target widget.
 */
class MouseEvent : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs the mouse event filter.
     *
     * @param object Optional parent object.
     */
    MouseEvent(QObject* object = nullptr);

    /**
     * @brief Destroys the MouseEvent instance.
     */
    virtual ~MouseEvent();

Q_SIGNALS:

    /**
     * @brief Emitted when a mouse press event is detected.
     */
    void pressed();

protected:
    /**
     * @brief Filters events from the monitored object.
     *
     * Intercepts events and emits signals when relevant
     * mouse events occur.
     */
    bool eventFilter(QObject* obj, QEvent* event) override;
};
