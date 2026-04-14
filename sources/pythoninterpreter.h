
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QList>
#include <QObject>
#include <QScopedPointer>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz {

class PythonInterpreterPrivate;

/**
 * @class PythonInterpreter
 * @brief Provides a Python execution environment for the session.
 *
 * Wraps an embedded Python interpreter used to execute scripts
 * against the current application session and USD stage. This
 * enables scripting, automation, and tool development directly
 * within the viewer.
 *
 * The interpreter is typically owned by the Session and may expose
 * session APIs (e.g. stage, selection, commands) to Python.
 */
class PythonInterpreter : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a Python interpreter instance.
     *
     * @param parent Optional parent object.
     */
    PythonInterpreter(QObject* parent = nullptr);

    /**
     * @brief Destroys the Python interpreter instance.
     */
    ~PythonInterpreter();

    /** @name Script Execution */
    ///@{

    /**
     * @brief Executes a Python script.
     *
     * Runs the provided Python source code within the interpreter
     * context and returns any output or result as a string.
     *
     * @param script Python source code to execute.
     * @return Output or result of the execution.
     */
    QString executeScript(const QString& script);

    ///@}

private:
    QScopedPointer<PythonInterpreterPrivate> p;
};

}  // namespace stageviz
