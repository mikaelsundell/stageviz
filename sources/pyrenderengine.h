// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "renderengine.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

namespace stageviz::python {

/**
 * @brief Python wrapper for @ref stageviz::RenderEngine.
 *
 * Owns an offscreen RenderEngine instance intended for scripted rendering.
 * USD stages are supplied as normal `pxr.Usd.Stage` Python objects and camera
 * state is read from a UsdGeomCamera prim on that stage.
 */
typedef struct {
    PyObject_HEAD RenderEngine* renderer;
} PyRenderEngineObject;

/** CPython type for `stageviz.RenderEngine`. */
extern PyTypeObject PyRenderEngineType;

/**
 * @brief Initializes the RenderEngine Python type.
 * @return True on success.
 */
bool
initPyRenderEngineType();

/**
 * @brief Adds the RenderEngine type to a Python module.
 * @param module Python module receiving the type.
 * @return Zero on success, otherwise -1.
 */
int
addPyRenderEngineType(PyObject* module);

}  // namespace stageviz::python
