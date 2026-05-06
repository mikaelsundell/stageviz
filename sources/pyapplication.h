// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "application.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::Application.
 *
 * Provides a thin CPython object exposing application-level UI services,
 * such as queued invocation and safe widget display from Python scripts.
 * The Application pointer is non-owning and managed on the C++ side.
 */
typedef struct {
    PyObject_HEAD Application* application;
} PyApplicationObject;

/** CPython type for `stageviz.Application`. */
extern PyTypeObject PyApplicationType;

/** Initialize the Application Python type. */
bool
initPyApplicationType();

/** Add the Application type to a Python module. */
int
addPyApplicationType(PyObject* module);

/**
 * @brief Create a Python wrapper for an Application.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPyApplication(Application* application);

}  // namespace stageviz::python