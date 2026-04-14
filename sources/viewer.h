// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include <QMainWindow>
#include <QScopedPointer>

namespace stageviz {

class ViewerPrivate;

/**
 * @class Viewer
 * @brief Main application window for the USD viewer.
 *
 * Provides the primary user interface and integrates the core
 * viewing components of the application. The viewer manages
 * window-level behavior such as drag-and-drop file loading and
 * argument handling from the application entry point.
 */
class Viewer : public QMainWindow {
public:
    /**
     * @brief Constructs the Viewer window.
     *
     * Initializes the main window and internal viewer components.
     *
     * @param parent Optional parent widget.
     */
    Viewer(QWidget* parent = nullptr);

    /**
     * @brief Destroys the Viewer instance.
     */
    virtual ~Viewer();

    /** @name Application Arguments */
    ///@{

    /**
     * @brief Sets command line arguments for the viewer.
     *
     * Typically called during application startup to pass
     * file paths or configuration parameters to the viewer.
     *
     * @param arguments Command line arguments.
     */
    void setArguments(const QStringList& arguments);

    ///@}

protected:
    void closeEvent(QCloseEvent* event) override;

    /** @name Drag and Drop */
    ///@{

    /**
     * @brief Handles drag enter events.
     *
     * Accepts drag operations when supported data types
     * (such as files) are detected.
     *
     * @param event Drag enter event.
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Handles drop events.
     *
     * Processes dropped data, typically used to load
     * files directly into the viewer.
     *
     * @param event Drop event.
     */
    void dropEvent(QDropEvent* event) override;

    ///@}


private:
    QScopedPointer<ViewerPrivate> p;
};

}  // namespace stageviz
