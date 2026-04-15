// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "selectionlist.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::SelectionList.
 *
 * Provides a thin CPython object exposing the native SelectionList API.
 * The SelectionList pointer is non-owning and managed on the C++ side.
 */
typedef struct {
    PyObject_HEAD SelectionList* selection;
} PySelectionListObject;

/** CPython type for `stageviz.SelectionList`. */
extern PyTypeObject PySelectionListType;

/** Initialize the SelectionList Python type. */
bool
initPySelectionListType();

/** Add the SelectionList type to a Python module. */
int
addPySelectionListType(PyObject* module);

/**
 * @brief Create a Python wrapper for a SelectionList.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPySelectionList(SelectionList* selection);

}  // namespace stageviz::python
