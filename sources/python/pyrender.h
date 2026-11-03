// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {
/**
 * @brief Initializes the read-only render view and owned capture Python types.
 */
bool
initPyRenderTypes();

/**
 * @brief Adds render view and capture types to the Stageviz module.
 */
int
addPyRenderTypes(PyObject* module);

/**
 * @brief Returns a weak wrapper for the active render view, or raises RuntimeError.
 */
PyObject*
createPyRender();
}  // namespace stageviz::python
