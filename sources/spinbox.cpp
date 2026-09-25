// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "spinbox.h"

#include <QApplication>
#include <QLineEdit>
#include <algorithm>
#include <cmath>

namespace stageviz {

SpinBox::SpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    setKeyboardTracking(true);
    setAccelerated(false);
    setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    setScrubSensitivity(0.1);
    lineEdit()->setReadOnly(true);
    lineEdit()->setMouseTracking(true);
    lineEdit()->installEventFilter(this);
    // connect
    connect(this, &QDoubleSpinBox::editingFinished, this, [this]() { finishEditing(); });
}

SpinBox::~SpinBox() = default;

void
SpinBox::setScrubSensitivity(double sensitivity)
{
    m_scrubSensitivity = std::max(0.0, sensitivity);
}

double
SpinBox::scrubSensitivity() const
{
    return m_scrubSensitivity;
}

bool
SpinBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == lineEdit()) {
        switch (event->type()) {
        case QEvent::Enter: updateValueCursor(); break;

        case QEvent::Leave:
            if (!m_scrubbing && !m_editing)
                lineEdit()->unsetCursor();
            break;

        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!m_editing && mouse->button() == Qt::LeftButton) {
                beginPress(mouse);
                return false;
            }
            break;
        }

        case QEvent::MouseMove: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!m_editing && updateScrub(mouse))
                return true;
            break;
        }

        case QEvent::MouseButtonRelease: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!m_editing && finishPress(mouse))
                return true;
            break;
        }

        case QEvent::MouseButtonDblClick: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_pressed = false;
                m_scrubbing = false;
                beginEditing();
                mouse->accept();
                return true;
            }
            break;
        }

        default: break;
        }
    }
    return QDoubleSpinBox::eventFilter(watched, event);
}

void
SpinBox::mousePressEvent(QMouseEvent* event)
{
    if (!m_editing && event->button() == Qt::LeftButton)
        beginPress(event);

    QDoubleSpinBox::mousePressEvent(event);
}

void
SpinBox::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_editing && updateScrub(event))
        return;

    QDoubleSpinBox::mouseMoveEvent(event);
}

void
SpinBox::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_editing && finishPress(event))
        return;

    QDoubleSpinBox::mouseReleaseEvent(event);
}

void
SpinBox::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = false;
        m_scrubbing = false;
        beginEditing();
        event->accept();
        return;
    }

    QDoubleSpinBox::mouseDoubleClickEvent(event);
}

void
SpinBox::beginPress(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton)
        return;

    m_pressed = true;
    m_scrubbing = false;
    m_pressGlobalPosition = event->globalPosition().toPoint();
    m_pressValue = value();
}

bool
SpinBox::updateScrub(QMouseEvent* event)
{
    if (!event || !m_pressed || !(event->buttons() & Qt::LeftButton))
        return false;

    const QPoint current = event->globalPosition().toPoint();
    const QPoint delta = current - m_pressGlobalPosition;

    if (!m_scrubbing) {
        if (delta.manhattanLength() < QApplication::startDragDistance())
            return false;

        m_scrubbing = true;
        Q_EMIT scrubStarted();
        lineEdit()->setCursor(Qt::SizeHorCursor);
    }
    const int pixels = delta.x() - delta.y();

    double modifier = 1.0;
    if (event->modifiers() & Qt::ShiftModifier)
        modifier *= 0.1;

    const double increment = singleStep() * m_scrubSensitivity * modifier;
    double next = m_pressValue + static_cast<double>(pixels) * increment;

    // Control can be pressed or released at any point during a scrub. Since
    // the value is always derived from the original press value, toggling snap
    // does not accumulate rounding errors or introduce drift.
    if (event->modifiers() & Qt::ControlModifier)
        next = std::round(next);

    next = std::clamp(next, minimum(), maximum());

    if (!qFuzzyCompare(1.0 + next, 1.0 + value())) {
        setValue(next);
        Q_EMIT scrubbed(next);
    }

    event->accept();
    return true;
}

bool
SpinBox::finishPress(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton || !m_pressed)
        return false;

    const bool wasScrubbing = m_scrubbing;
    m_pressed = false;
    m_scrubbing = false;

    if (wasScrubbing)
        Q_EMIT scrubFinished(value());

    updateValueCursor();

    if (wasScrubbing) {
        event->accept();
        return true;
    }
    return false;
}

void
SpinBox::beginEditing()
{
    if (m_editing)
        return;

    m_editing = true;
    lineEdit()->setReadOnly(false);
    lineEdit()->setCursor(Qt::IBeamCursor);
    lineEdit()->setFocus(Qt::MouseFocusReason);
    lineEdit()->selectAll();
}

void
SpinBox::finishEditing()
{
    if (!m_editing)
        return;

    m_editing = false;
    lineEdit()->setReadOnly(true);
    lineEdit()->deselect();
    updateValueCursor();
}

void
SpinBox::updateValueCursor()
{
    if (!lineEdit())
        return;

    if (m_editing) {
        lineEdit()->setCursor(Qt::IBeamCursor);
        return;
    }

    if (lineEdit()->underMouse())
        lineEdit()->setCursor(Qt::SizeHorCursor);
    else
        lineEdit()->unsetCursor();
}

}  // namespace stageviz
