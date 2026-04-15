// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyutils.h"
#include "application.h"
#include <pxr/external/boost/python.hpp>

namespace stageviz::python {

Session*
currentSession()
{
    return session();
}

bool
checkSession(Session* s)
{
    if (s)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "stageviz session is not available");
    return false;
}

bool
checkSelectionList(SelectionList* selection)
{
    if (selection)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "stageviz selection list is not available");
    return false;
}

Session::LoadPolicy
toLoadPolicy(long value)
{
    return value == static_cast<long>(Session::LoadPolicy::None) ? Session::LoadPolicy::None : Session::LoadPolicy::All;
}

Session::StageUp
toStageUp(long value)
{
    return value == static_cast<long>(Session::StageUp::Z) ? Session::StageUp::Z : Session::StageUp::Y;
}

Session::PrimsUpdate
toPrimsUpdate(long value)
{
    return value == static_cast<long>(Session::PrimsUpdate::Deferred) ? Session::PrimsUpdate::Deferred
                                                                      : Session::PrimsUpdate::Immediate;
}

Session::Notify::Status
toNotifyStatus(long value)
{
    switch (value) {
    case 1: return Session::Notify::Status::Progress;
    case 2: return Session::Notify::Status::Warning;
    case 3: return Session::Notify::Status::Error;
    case 0:
    default: return Session::Notify::Status::Info;
    }
}

PyObject*
sdfPathToPyString(const SdfPath& path)
{
    return PyUnicode_FromString(path.GetString().c_str());
}

PyObject*
pathListToPyList(const QList<SdfPath>& paths)
{
    PyObject* list = PyList_New(paths.size());
    if (!list)
        return nullptr;

    for (qsizetype i = 0; i < paths.size(); ++i) {
        PyObject* value = sdfPathToPyString(paths[i]);
        if (!value) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, value);
    }

    return list;
}

bool
pyToPathList(PyObject* object, QList<SdfPath>* paths)
{
    if (!paths) {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: null path output");
        return false;
    }

    paths->clear();

    if (!object || object == Py_None)
        return true;

    PyObject* seq = PySequence_Fast(object, "Expected a sequence of SdfPath strings");
    if (!seq)
        return false;

    const Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
    PyObject** items = PySequence_Fast_ITEMS(seq);

    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* item = items[i];
        if (!PyUnicode_Check(item)) {
            Py_DECREF(seq);
            PyErr_SetString(PyExc_TypeError, "Expected path strings");
            return false;
        }

        const char* utf8 = PyUnicode_AsUTF8(item);
        if (!utf8) {
            Py_DECREF(seq);
            return false;
        }

        const SdfPath path(utf8);
        if (!path.IsAbsolutePath()) {
            Py_DECREF(seq);
            PyErr_Format(PyExc_ValueError, "Expected absolute SdfPath, got '%s'", utf8);
            return false;
        }

        paths->append(path);
    }

    Py_DECREF(seq);
    return true;
}

bool
pyToPath(PyObject* object, SdfPath* path)
{
    if (!path) {
        PyErr_SetString(PyExc_RuntimeError, "Internal error: null path output");
        return false;
    }

    if (!object || object == Py_None) {
        PyErr_SetString(PyExc_TypeError, "Expected a path string");
        return false;
    }

    if (!PyUnicode_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "Expected a path string");
        return false;
    }

    const char* utf8 = PyUnicode_AsUTF8(object);
    if (!utf8)
        return false;

    const SdfPath value(utf8);
    if (!value.IsAbsolutePath()) {
        PyErr_Format(PyExc_ValueError, "Expected absolute SdfPath, got '%s'", utf8);
        return false;
    }

    *path = value;
    return true;
}

QVariant
pyToVariant(PyObject* object);

QVariantList
pyToVariantList(PyObject* object)
{
    QVariantList list;
    PyObject* seq = PySequence_Fast(object, "Expected a sequence");
    if (!seq)
        return list;

    const Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
    PyObject** items = PySequence_Fast_ITEMS(seq);

    for (Py_ssize_t i = 0; i < count; ++i)
        list.append(pyToVariant(items[i]));

    Py_DECREF(seq);
    return list;
}

QVariantMap
pyToVariantMap(PyObject* object)
{
    QVariantMap map;
    if (!object || object == Py_None)
        return map;

    if (!PyDict_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "Expected a dict for details");
        return QVariantMap();
    }

    PyObject* key = nullptr;
    PyObject* value = nullptr;
    Py_ssize_t pos = 0;

    while (PyDict_Next(object, &pos, &key, &value)) {
        if (!PyUnicode_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "Expected string keys in details dict");
            return QVariantMap();
        }

        const char* utf8 = PyUnicode_AsUTF8(key);
        if (!utf8)
            return QVariantMap();

        map.insert(QString::fromUtf8(utf8), pyToVariant(value));
    }

    return map;
}

QVariant
pyToVariant(PyObject* object)
{
    if (!object || object == Py_None)
        return QVariant();

    if (PyBool_Check(object))
        return QVariant(static_cast<bool>(object == Py_True));

    if (PyLong_Check(object))
        return QVariant::fromValue(PyLong_AsLongLong(object));

    if (PyFloat_Check(object))
        return QVariant(PyFloat_AsDouble(object));

    if (PyUnicode_Check(object))
        return QVariant(QString::fromUtf8(PyUnicode_AsUTF8(object)));

    if (PyDict_Check(object))
        return QVariant(pyToVariantMap(object));

    if (PyList_Check(object) || PyTuple_Check(object))
        return QVariant(pyToVariantList(object));

    return QVariant(QString::fromUtf8(Py_TYPE(object)->tp_name));
}

PyObject*
bboxToPyTuple(const GfBBox3d& bbox)
{
    const GfRange3d range = bbox.GetRange();
    const GfVec3d min = range.GetMin();
    const GfVec3d max = range.GetMax();

    return Py_BuildValue("((ddd)(ddd))", min[0], min[1], min[2], max[0], max[1], max[2]);
}

PyObject*
wrapUsdStage(const UsdStageRefPtr& stage)
{
    if (!stage)
        Py_RETURN_NONE;

    try {
        PXR_BOOST_PYTHON_NAMESPACE::object object(stage);
        return PXR_BOOST_PYTHON_NAMESPACE::incref(object.ptr());
    } catch (const PXR_BOOST_PYTHON_NAMESPACE::error_already_set&) {
        return nullptr;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to convert native UsdStageRefPtr to Python object");
        return nullptr;
    }
}

}  // namespace stageviz::python
