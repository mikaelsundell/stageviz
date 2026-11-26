// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "spinbox.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionSpinBox>
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
    setFocusPolicy(Qt::NoFocus);
    lineEdit()->setFocusPolicy(Qt::NoFocus);
    lineEdit()->setReadOnly(true);
    lineEdit()->setMouseTracking(true);
    lineEdit()->installEventFilter(this);
    updateValueCursor();
    // connect
    connect(this, &QDoubleSpinBox::editingFinished, this, [this]() { finishEditing(); });
    connect(this, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double next) {
        if (m_stepping && !m_scrubbing)
            Q_EMIT scrubbed(next);
    });
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
SpinBox::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        if (m_editing) {
            event->accept();
            return true;
        }
        event->ignore();
        return true;
    }
    return QDoubleSpinBox::event(event);
}

bool
SpinBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == lineEdit()) {
        switch (event->type()) {
        case QEvent::ShortcutOverride:
            if (m_editing) {
                event->accept();
                return true;
            }
            event->ignore();
            return true;
        case QEvent::KeyPress:
            if (!m_editing) {
                event->ignore();
                return true;
            }
            break;
        case QEvent::Enter: updateValueCursor(); break;

        case QEvent::FocusIn:
            if (!m_editing) {
                // QAbstractSpinBox may select the complete line-edit contents
                // when a persistent editor receives focus. Queue the deselect so
                // it runs after the built-in focus handling. Direct edit mode
                // still selects all explicitly in beginEditing().
                QMetaObject::invokeMethod(
                    lineEdit(),
                    [this]() {
                        if (!m_editing && lineEdit())
                            lineEdit()->deselect();
                    },
                    Qt::QueuedConnection);
            }
            break;

        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!m_editing && mouse->button() == Qt::LeftButton) {
                beginPress(mouse);
                mouse->accept();
                return true;
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
            if (!m_editing && mouse->button() == Qt::LeftButton) {
                finishPress(mouse);
                mouse->accept();
                releaseKeyboardFocus();
                return true;
            }
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

bool
SpinBox::isTextEditing() const
{
    return m_editing;
}

bool
SpinBox::isScrubbing() const
{
    return m_scrubbing;
}

void
SpinBox::keyPressEvent(QKeyEvent* event)
{
    if (m_editing)
        QDoubleSpinBox::keyPressEvent(event);
    else
        event->ignore();
}

void
SpinBox::mousePressEvent(QMouseEvent* event)
{
    if (!m_editing && event->button() == Qt::LeftButton) {
        beginPress(event);
        QStyleOptionSpinBox option;
        initStyleOption(&option);
        const auto control = style()->hitTestComplexControl(QStyle::CC_SpinBox, &option, event->position().toPoint(),
                                                            this);
        m_stepping = (control == QStyle::SC_SpinBoxUp && stepEnabled().testFlag(StepUpEnabled))
                     || (control == QStyle::SC_SpinBoxDown && stepEnabled().testFlag(StepDownEnabled));
        if (m_stepping)
            Q_EMIT scrubStarted();
    }

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
    if (!m_editing && event->button() == Qt::LeftButton) {
        if (!m_scrubbing)
            QDoubleSpinBox::mouseReleaseEvent(event);
        finishPress(event);
        releaseKeyboardFocus();
        return;
    }
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
    m_stepping = false;
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

        // A press on an arrow starts Qt's step-repeat timer. End that native
        // interaction before taking over the drag so repeats cannot compete
        // with scrubbing or continue after our release handler consumes it.
        QMouseEvent release(QEvent::MouseButtonRelease, mapFromGlobal(current), event->globalPosition(), Qt::LeftButton,
                            Qt::NoButton, event->modifiers());
        QDoubleSpinBox::mouseReleaseEvent(&release);

        m_scrubbing = true;
        setCursor(Qt::SizeHorCursor);
        updateValueCursor();
        if (!m_stepping)
            Q_EMIT scrubStarted();
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

    const bool wasScrubbing = m_scrubbing || m_stepping;
    m_pressed = false;
    m_scrubbing = false;
    m_stepping = false;
    unsetCursor();

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
    setFocusPolicy(Qt::StrongFocus);
    lineEdit()->setFocusPolicy(Qt::StrongFocus);
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
    setFocusPolicy(Qt::NoFocus);
    lineEdit()->setFocusPolicy(Qt::NoFocus);
    releaseKeyboardFocus();
}

void
SpinBox::releaseKeyboardFocus()
{
    // Enter already emits editingFinished. Clearing focus must not emit a
    // second commit, and a mouse scrub commits through scrubFinished instead.
    const QSignalBlocker blocker(this);
    lineEdit()->clearFocus();
    clearFocus();
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

    lineEdit()->setCursor(Qt::SizeHorCursor);
}

}  // namespace stageviz
