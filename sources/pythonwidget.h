// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QWidget>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PythonWidgetPrivate;

/**
 * @class PythonWidget
 * @brief Widget for editing and executing Python scripts.
 *
 * Provides a user interface for writing and running Python code
 * within the application. Typically consists of a code editor with
 * syntax highlighting and an execution interface connected to a
 * PythonInterpreter.
 *
 * The view is intended for scripting, automation, and development
 * of custom tools operating on the current session and USD stage.
 */
class PythonWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Python scripting view.
     *
     * @param parent Optional parent widget.
     */
    PythonWidget(QWidget* parent = nullptr);

    /**
     * @brief Destroys the PythonWidget instance.
     */
    virtual ~PythonWidget();

private:
    Q_DISABLE_COPY_MOVE(PythonWidget)
    QScopedPointer<PythonWidgetPrivate> p;
};

}  // namespace stageviz
