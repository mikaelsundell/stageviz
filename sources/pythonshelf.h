// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"
#include <QScopedPointer>
#include <QWidget>

namespace stageviz {

class PythonShelfPrivate;

/**
 * @class PythonShelf
 * @brief Persistent shelf bar for reusable Python scripts.
 *
 * PythonShelf owns the named shelf tabs shown in the main viewer. Scripts can
 * be dropped onto a shelf, dragged back out, reordered, renamed, exported, and
 * executed directly through the application Python interpreter.
 *
 * The shelf is intentionally independent from PythonDialog. The dialog is a
 * script editor, while PythonShelf is an application-level launcher.
 */
class PythonShelf : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Python shelf bar.
     * @param parent Optional parent widget.
     */
    PythonShelf(QWidget* parent = nullptr);

    /**
     * @brief Destroys the Python shelf bar and persists its current contents.
     */
    ~PythonShelf() override;

    /**
     * @brief Return the preferred compact shelf-bar size.
     */
    QSize sizeHint() const override;

    /**
     * @brief Return the minimum useful shelf-bar size.
     */
    QSize minimumSizeHint() const override;

private:
    Q_DISABLE_COPY_MOVE(PythonShelf)
    QScopedPointer<PythonShelfPrivate> p;
};

}  // namespace stageviz
