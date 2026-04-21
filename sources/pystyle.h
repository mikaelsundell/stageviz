// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "style.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::Style.
 *
 * Provides a thin CPython object exposing the native Style API.
 * The Style pointer is non-owning and managed on the C++ side.
 */
typedef struct {
    PyObject_HEAD Style* style;
} PyStyleObject;

/** CPython type for `stageviz.Style`. */
extern PyTypeObject PyStyleType;

/** Initialize the Style Python type. */
bool
initPyStyleType();

/** Add the Style type to a Python module. */
int
addPyStyleType(PyObject* module);

/**
 * @brief Create a Python wrapper for a Style.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPyStyle(Style* style);

}  // namespace stageviz::python