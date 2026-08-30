// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "stageviz.h"

#include <QPlainTextEdit>

namespace stageviz {

/**
 * @class PythonLogEdit
 * @brief Plain text log view with a zero minimum size hint.
 *
 * Provides a plain text editor suitable for use inside a splitter
 * where the log view should be freely resizable down to zero.
 */
class PythonLogEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Python log editor widget.
     *
     * @param parent Optional parent widget.
     */
    PythonLogEdit(QWidget* parent = nullptr);

    /**
     * @brief Destroys the PythonLogEdit instance.
     */
    virtual ~PythonLogEdit();

    QSize minimumSizeHint() const override;
};

}  // namespace stageviz
