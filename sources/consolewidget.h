// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QHideEvent>
#include <QShowEvent>
#include <QWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class ConsoleWidgetPrivate;

/**
 * @class ConsoleWidget
 * @brief Widget for displaying console output and messages.
 *
 * Provides a UI component for presenting log output, status messages,
 * or other textual feedback within the application.
 */
class ConsoleWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the console widget.
     *
     * @param parent Optional parent widget.
     */
    ConsoleWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the ConsoleWidget instance.
     */
    virtual ~ConsoleWidget();

Q_SIGNALS:
    /**
     * @brief Emitted when the widget visibility changes.
     *
     * @param visible True when the widget is shown, false when hidden.
     */
    void visibilityChanged(bool visible);

private:
    QScopedPointer<ConsoleWidgetPrivate> p;
};

}  // namespace stageviz
