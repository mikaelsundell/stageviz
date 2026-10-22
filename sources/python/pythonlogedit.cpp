// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pythonlogedit.h"

namespace stageviz {

PythonLogEdit::PythonLogEdit(QWidget* parent)
    : QPlainTextEdit(parent)
{}

PythonLogEdit::~PythonLogEdit() = default;

QSize
PythonLogEdit::minimumSizeHint() const
{
    return QSize(0, 0);
}

}  // namespace stageviz
