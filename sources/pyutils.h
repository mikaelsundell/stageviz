// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#pragma once

#include "selectionlist.h"
#include "session.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

#include <pxr/base/gf/bbox3d.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

#include <QVariant>
#include <QVariantMap>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::python {

/** Return the current application session. */
Session*
currentSession();

/** Validate a Session pointer for Python calls. */
bool
checkSession(Session* session);

/** Validate a SelectionList pointer for Python calls. */
bool
checkSelectionList(SelectionList* selection);

/** Convert an integer to Session::LoadPolicy. */
Session::LoadPolicy
toLoadPolicy(long value);

/** Convert an integer to Session::StageUp. */
Session::StageUp
toStageUp(long value);

/** Convert an integer to Session::PrimsUpdate. */
Session::PrimsUpdate
toPrimsUpdate(long value);

/** Convert an integer to Session::Notify::Status. */
Session::Notify::Status
toNotifyStatus(long value);

/** Convert an SdfPath to a Python string. */
PyObject*
sdfPathToPyString(const SdfPath& path);

/** Convert a list of SdfPath values to a Python list of strings. */
PyObject*
pathListToPyList(const QList<SdfPath>& paths);

/** Convert a Python sequence of path strings to QList<SdfPath>. */
bool
pyToPathList(PyObject* object, QList<SdfPath>* paths);

/** Convert a Python path string to SdfPath. */
bool
pyToPath(PyObject* object, SdfPath* path);

/** Convert a Python object to QVariant. */
QVariant
pyToVariant(PyObject* object);

/** Convert a Python sequence to QVariantList. */
QVariantList
pyToVariantList(PyObject* object);

/** Convert a Python dict to QVariantMap. */
QVariantMap
pyToVariantMap(PyObject* object);

/** Convert a bounding box to a Python tuple. */
PyObject*
bboxToPyTuple(const GfBBox3d& bbox);

/** Wrap a native USD stage as a Python USD object. */
PyObject*
wrapUsdStage(const UsdStageRefPtr& stage);

}  // namespace stageviz::python
