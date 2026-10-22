// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QPlainTextEdit>

namespace stageviz {

class PythonEditorPrivate;

/**
 * @class PythonEditor
 * @brief Code editor for writing Python scripts.
 *
 * Provides a plain text editor optimized for Python scripting,
 * typically used within a PythonDialog. The editor may include
 * features such as syntax highlighting, indentation handling,
 * and script editing utilities.
 *
 * The editor is intended for authoring scripts that operate on
 * the current session and USD stage.
 */
class PythonEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Python editor widget.
     *
     * @param parent Optional parent widget.
     */
    PythonEditor(QWidget* parent = nullptr);

    /**
     * @brief Destroys the PythonEditor instance.
     */
    virtual ~PythonEditor();

    /**
     * @brief Returns the gutter width required for the current line count.
     */
    int lineNumberAreaWidth() const;

protected:
    /**
     * @brief Repositions the line-number gutter when the editor is resized.
     */
    void resizeEvent(QResizeEvent* event) override;
    /**
     * @brief Paints line numbers alongside the visible text blocks.
     */
    void lineNumberAreaPaintEvent(QPaintEvent* event);

private:
    friend class PythonEditorPrivate;
    QScopedPointer<PythonEditorPrivate> p;
};

}  // namespace stageviz
