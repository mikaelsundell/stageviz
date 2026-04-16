// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QDialog>

namespace stageviz {

class ConsoleDialogPrivate;

/**
 * @class ConsoleDialog
 * @brief Dialog for displaying console output and messages.
 *
 * Provides a UI component for presenting log output, status messages,
 * or other textual feedback within the application.
 */
class ConsoleDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Constructs the console widget.
     *
     * @param parent Optional parent widget.
     */
    ConsoleDialog(QWidget* parent = nullptr);

    /**
     * @brief Destroys the ConsoleDialog instance.
     */
    virtual ~ConsoleDialog();

Q_SIGNALS:
    /**
     * @brief Emitted when the widget visibility changes.
     *
     * @param visible True when the widget is shown, false when hidden.
     */
    void visibilityChanged(bool visible);

private:
    QScopedPointer<ConsoleDialogPrivate> p;
};

}  // namespace stageviz
