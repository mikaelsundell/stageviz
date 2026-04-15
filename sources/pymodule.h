// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/** Initialize the stageviz Python module. */
PyMODINIT_FUNC
PyInit_stageviz(void);

}  // namespace stageviz::python

/** Python entry point used when registering the embedded stageviz module. */
extern "C" PyObject*
PyInit_stageviz_wrapper();
