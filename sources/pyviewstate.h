// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "viewstate.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::ViewState.
 *
 * The ViewState pointer is non-owning and managed by Session.
 */
typedef struct PyViewStateObject {
    PyObject_HEAD ViewState* viewState;
} PyViewStateObject;

/** CPython type for `stageviz.ViewState`. */
extern PyTypeObject PyViewStateType;

/** Initialize the ViewState Python type. */
bool
initPyViewStateType();

/** Add the ViewState type to a Python module. */
int
addPyViewStateType(PyObject* module);

/**
 * @brief Create a Python wrapper for a ViewState.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPyViewState(ViewState* viewState);

}  // namespace stageviz::python
