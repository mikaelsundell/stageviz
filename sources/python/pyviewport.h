// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {
/**
 * @brief Initializes the read-only viewport and owned capture Python types.
 */
bool
initPyViewportTypes();

/**
 * @brief Adds viewport types to the Stageviz module.
 */
int
addPyViewportTypes(PyObject* module);

/**
 * @brief Returns a weak wrapper for the active viewport, or raises RuntimeError.
 */
PyObject*
createPyViewport();
}  // namespace stageviz::python
