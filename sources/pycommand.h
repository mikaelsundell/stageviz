// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/** Add the stageviz.command module to the root stageviz Python module. */
int
addPyCommandModule(PyObject* module);

}  // namespace stageviz::python
