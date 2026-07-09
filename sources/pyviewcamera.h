// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "viewcamera.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::ViewCamera.
 *
 * Provides a thin CPython object exposing the native ViewCamera API.
 * The ViewCamera pointer is non-owning and managed on the C++ side.
 */
typedef struct PyViewCameraObject {
    PyObject_HEAD
    ViewCamera* camera;
} PyViewCameraObject;

/** CPython type for `stageviz.ViewCamera`. */
extern PyTypeObject PyViewCameraType;

/** Initialize the ViewCamera Python type. */
bool
initPyViewCameraType();

/** Add the ViewCamera type to a Python module. */
int
addPyViewCameraType(PyObject* module);

/**
 * @brief Create a Python wrapper for a ViewCamera.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPyViewCamera(ViewCamera* camera);

}  // namespace stageviz::python
