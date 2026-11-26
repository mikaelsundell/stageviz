// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#undef slots
#include <Python.h>
#define slots Q_SLOTS
namespace stageviz::python {
/**
 * @brief Attaches Python bindings backed by the core file-format registry.
 */
int
addPyPluginsModule(PyObject* module);
/**
 * @brief Releases Python-owned core registrations before interpreter shutdown.
 */
void
releasePyPlugins();
}  // namespace stageviz::python
