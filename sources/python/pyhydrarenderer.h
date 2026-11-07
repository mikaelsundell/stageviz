// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Initializes the metadata-only HydraRenderer Python type.
 *
 * @return True on success.
 */
bool
initPyHydraRendererType();

/**
 * @brief Adds HydraRenderer to the embedded module.
 *
 * @param module Module receiving the type.
 * @return Zero on success, otherwise -1.
 */
int
addPyHydraRendererType(PyObject* module);

}  // namespace stageviz::python
