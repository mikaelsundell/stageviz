// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "mouseevent.h"
#include <QMouseEvent>

MouseEvent::MouseEvent(QObject* parent)
    : QObject(parent)
{}

MouseEvent::~MouseEvent() = default;

bool
MouseEvent::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            pressed();
        }
    }
    return QObject::eventFilter(obj, event);
}
