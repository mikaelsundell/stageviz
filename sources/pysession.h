// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "session.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::Session.
 *
 * Provides a thin CPython object exposing the native Session API.
 * The Session pointer is non-owning and managed on the C++ side.
 */
typedef struct {
    PyObject_HEAD Session* session;
} PySessionObject;

/** CPython type for `stageviz.Session`. */
extern PyTypeObject PySessionType;

/** Initialize the Session Python type. */
bool
initPySessionType();

/** Add the Session type to a Python module. */
int
addPySessionType(PyObject* module);

/**
 * @brief Create a Python wrapper for a Session.
 *
 * @return New reference or nullptr on failure.
 */
PyObject*
createPySession(Session* session);

}  // namespace stageviz::python
