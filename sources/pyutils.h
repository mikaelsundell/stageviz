// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "selectionlist.h"
#include "session.h"
#include "viewstate.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

#include <QColor>
#include <QVariant>
#include <QVariantMap>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::python {

/**
 * @brief Return the current application session.
 */
Session*
currentSession();

/**
 * @brief Validate a Session pointer for Python calls.
 */
bool
checkSession(Session* session);

/**
 * @brief Validate a SelectionList pointer for Python calls.
 */
bool
checkSelectionList(SelectionList* selection);

/**
 * @brief Validate a ViewState pointer for Python calls.
 */
bool
checkViewState(ViewState* viewState);

/**
 * @brief Convert an integer to Session::LoadPolicy.
 */
Session::LoadPolicy
toLoadPolicy(long value);

/**
 * @brief Convert an integer to Session::StageUp.
 */
Session::StageUp
toStageUp(long value);

/**
 * @brief Convert an integer to Session::PrimsUpdate.
 */
Session::PrimsUpdate
toPrimsUpdate(long value);

/**
 * @brief Convert an integer to Session::Notify::Status.
 */
Session::Notify::Status
toNotifyStatus(long value);

/**
 * @brief Convert an SdfPath to a Python string.
 */
PyObject*
sdfPathToPyString(const SdfPath& path);

/**
 * @brief Convert a list of SdfPath values to a Python list of strings.
 */
PyObject*
pathListToPyList(const QList<SdfPath>& paths);

/**
 * @brief Convert a Python sequence of path strings to QList<SdfPath>.
 */
bool
pyToPathList(PyObject* object, QList<SdfPath>* paths);

/**
 * @brief Convert a Python path string to SdfPath.
 */
bool
pyToPath(PyObject* object, SdfPath* path);

/**
 * @brief Convert a QColor to a Python RGBA tuple.
 */
PyObject*
colorToPyTuple(const QColor& color);

/**
 * @brief Convert a Python RGB/RGBA sequence to QColor.
 */
bool
pyToColor(PyObject* object, QColor* color);

/**
 * @brief Convert a Python object to QVariant.
 */
QVariant
pyToVariant(PyObject* object);

/**
 * @brief Convert a Python sequence to QVariantList.
 */
QVariantList
pyToVariantList(PyObject* object);

/**
 * @brief Convert a Python dict to QVariantMap.
 */
QVariantMap
pyToVariantMap(PyObject* object);

/**
 * @brief Convert a GfVec3d to a Python tuple.
 */
PyObject*
vec3dToPyTuple(const GfVec3d& value);

/**
 * @brief Convert a Python tuple to GfVec3d.
 */
bool
pyToVec3d(PyObject* object, GfVec3d* value);

/**
 * @brief Convert a bounding box to a Python tuple.
 */
PyObject*
bboxToPyTuple(const GfBBox3d& bbox);

/**
 * @brief Convert a Python tuple pair to GfBBox3d.
 */
bool
pyToBBox(PyObject* object, GfBBox3d* bbox);

/**
 * @brief Wrap a native USD stage as a Python USD object.
 */
PyObject*
wrapUsdStage(const UsdStageRefPtr& stage);

}  // namespace stageviz::python
