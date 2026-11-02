// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "ui.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::UI.
 *
 * Provides a thin CPython object exposing the native UI API.
 * The UI pointer is non-owning and managed on the C++ side.
 */
typedef struct {
    PyObject_HEAD UI* ui;
} PyUIObject;

/**
 * @brief CPython type for `stageviz.UI`.
 */
extern PyTypeObject PyUIType;

/**
 * @brief Initialize the UI Python type.
 */
bool
initPyUIType();

/**
 * @brief Add the UI type to a Python module.
 */
int
addPyUIType(PyObject* module);

/**
 * @brief Create a Python wrapper for a UI.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPyUI(UI* ui);

}  // namespace stageviz::python
