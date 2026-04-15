// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QApplication>
#include <QScopedPointer>

namespace stageviz {
class PythonInterpreter;
class Console;
class Session;
class Style;
class Settings;
class ApplicationPrivate;

/**
 * @class Application
 * @brief Main application object for stageviz.
 *
 * Extends QApplication and provides access to global subsystems
 * used throughout the viewer such as settings and style.
 */
class Application : public QApplication {
    Q_OBJECT
public:
    /**
     * @brief Constructs the stageviz application.
     *
     * @param argc Argument count.
     * @param argv Argument vector.
     */
    Application(int& argc, char** argv);

    /**
     * @brief Destroys the application.
     */
    ~Application() override;

    /** @name Subsystems */
    ///@{

    /**
     * @brief Returns the console subsystem.
     */
    Console* console() const;

    /**
     * @brief Returns the python interpreter subsystem.
     */
    PythonInterpreter* pythonInterpreter() const;

    /**
     * @brief Returns the session subsystem.
     */
    Session* session() const;

    /**
     * @brief Returns the settings subsystem.
     */
    Settings* settings() const;

    /**
     * @brief Returns the style subsystem.
     */
    Style* style() const;

    ///@}

    /**
     * @brief Returns the global Application instance.
     *
     * Asserts if the active QCoreApplication is not an
     * stageviz::Application instance.
     */
    static Application* instance();

private:
    Q_DISABLE_COPY_MOVE(Application)
    QScopedPointer<ApplicationPrivate> p;
};

/**
 * @brief Returns the global Application instance.
 */
inline Application*
app()
{
    return Application::instance();
}

/**
 * @brief Returns the global console subsystem.
 */
inline Console*
console()
{
    auto* a = app();
    return a ? a->console() : nullptr;
}

/**
 * @brief Returns the global python interpreter subsystem.
 */
inline PythonInterpreter*
pythonInterpreter()
{
    auto* a = app();
    return a ? a->pythonInterpreter() : nullptr;
}

/**
 * @brief Returns the global session subsystem.
 */
inline Session*
session()
{
    auto* a = app();
    return a ? a->session() : nullptr;
}

/**
 * @brief Returns the global style subsystem.
 */
inline Style*
style()
{
    auto* a = app();
    return a ? a->style() : nullptr;
}

/**
 * @brief Returns the global settings subsystem.
 */
inline Settings*
settings()
{
    auto* a = app();
    return a ? a->settings() : nullptr;
}

}  // namespace stageviz
