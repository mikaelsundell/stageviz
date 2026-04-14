// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "commandstack.h"
#include "selectionlist.h"
#include "session.h"

#undef slots
#include <Python.h>
#define slots Q_SLOTS

#include <pxr/external/boost/python.hpp>
#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>

#include <QStringList>
#include <QVariant>
#include <QVariantMap>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace stageviz;

namespace {

static Session*
currentSession()
{
    return session();
}

static bool
checkSession(Session* s)
{
    if (s)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "stageviz session is not available");
    return false;
}

static bool
checkSelectionList(SelectionList* selection)
{
    if (selection)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "stageviz selection list is not available");
    return false;
}

static bool
checkCommandStack(CommandStack* stack)
{
    if (stack)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "stageviz command stack is not available");
    return false;
}

static Session::LoadPolicy
toLoadPolicy(long value)
{
    return value == static_cast<long>(Session::LoadPolicy::None) ? Session::LoadPolicy::None : Session::LoadPolicy::All;
}

static Session::StageUp
toStageUp(long value)
{
    return value == static_cast<long>(Session::StageUp::Z) ? Session::StageUp::Z : Session::StageUp::Y;
}

static Session::PrimsUpdate
toPrimsUpdate(long value)
{
    return value == static_cast<long>(Session::PrimsUpdate::Deferred) ? Session::PrimsUpdate::Deferred
                                                                      : Session::PrimsUpdate::Immediate;
}

static Session::Notify::Status
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

static PyObject*
sdfPathToPyString(const SdfPath& path)
{
    return PyUnicode_FromString(path.GetString().c_str());
}

static PyObject*
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

static bool
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

static bool
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

static QVariant
pyToVariant(PyObject* object);

static QVariantList
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

static QVariantMap
pyToVariantMap(PyObject* object)
{
    QVariantMap map;
    if (!object || object == Py_None)
        return map;

    if (!PyDict_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "Expected a dict for details");
        return map;
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

static QVariant
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

static PyObject*
bboxToPyTuple(const GfBBox3d& bbox)
{
    const GfRange3d range = bbox.GetRange();
    const GfVec3d min = range.GetMin();
    const GfVec3d max = range.GetMax();

    return Py_BuildValue("((ddd)(ddd))", min[0], min[1], min[2], max[0], max[1], max[2]);
}

static PyObject*
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

}  // namespace

// ----------------------------------------------------------------------------
// Python module definition
// ----------------------------------------------------------------------------

static PyModuleDef pysessionModule = { PyModuleDef_HEAD_INIT,
                                       "stageviz",
                                       "Python interface for stageviz Session",
                                       -1,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr };

// ----------------------------------------------------------------------------
// PySelectionListObject
// ----------------------------------------------------------------------------

typedef struct {
    PyObject_HEAD SelectionList* selection;
} PySelectionListObject;

static void
PySelectionList_dealloc(PySelectionListObject* self)
{
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
PySelectionList_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PySelectionListObject* self = reinterpret_cast<PySelectionListObject*>(type->tp_alloc(type, 0));
    if (self)
        self->selection = currentSession() ? currentSession()->selectionList() : nullptr;
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PySelectionList_isSelected(PySelectionListObject* self, PyObject* args)
{
    if (!checkSelectionList(self->selection))
        return nullptr;

    const char* pathString = nullptr;
    if (!PyArg_ParseTuple(args, "s", &pathString))
        return nullptr;

    const SdfPath path(pathString);
    if (!path.IsAbsolutePath()) {
        PyErr_Format(PyExc_ValueError, "Expected absolute SdfPath, got '%s'", pathString);
        return nullptr;
    }

    return PyBool_FromLong(self->selection->isSelected(path));
}

static PyObject*
PySelectionList_paths(PySelectionListObject* self)
{
    if (!checkSelectionList(self->selection))
        return nullptr;
    return pathListToPyList(self->selection->paths());
}

static PyObject*
PySelectionList_isEmpty(PySelectionListObject* self)
{
    if (!checkSelectionList(self->selection))
        return nullptr;
    return PyBool_FromLong(self->selection->isEmpty());
}

static PyObject*
PySelectionList_isValid(PySelectionListObject* self)
{
    if (!checkSelectionList(self->selection))
        return nullptr;
    return PyBool_FromLong(self->selection->isValid());
}

static PyObject*
PySelectionList_addPaths(PySelectionListObject* self, PyObject* args)
{
    if (!checkSelectionList(self->selection))
        return nullptr;

    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    self->selection->addPaths(paths);
    Py_RETURN_NONE;
}

static PyObject*
PySelectionList_removePaths(PySelectionListObject* self, PyObject* args)
{
    if (!checkSelectionList(self->selection))
        return nullptr;

    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    self->selection->removePaths(paths);
    Py_RETURN_NONE;
}

static PyObject*
PySelectionList_togglePaths(PySelectionListObject* self, PyObject* args)
{
    if (!checkSelectionList(self->selection))
        return nullptr;

    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    self->selection->togglePaths(paths);
    Py_RETURN_NONE;
}

static PyObject*
PySelectionList_updatePaths(PySelectionListObject* self, PyObject* args)
{
    if (!checkSelectionList(self->selection))
        return nullptr;

    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    self->selection->updatePaths(paths);
    Py_RETURN_NONE;
}

static PyObject*
PySelectionList_clear(PySelectionListObject* self)
{
    if (!checkSelectionList(self->selection))
        return nullptr;

    self->selection->clear();
    Py_RETURN_NONE;
}

static PyMethodDef PySelectionList_methods[]
    = { { "isSelected", (PyCFunction)PySelectionList_isSelected, METH_VARARGS, "Check if a path is selected" },
        { "paths", (PyCFunction)PySelectionList_paths, METH_NOARGS, "Get selected paths" },
        { "isEmpty", (PyCFunction)PySelectionList_isEmpty, METH_NOARGS, "Check whether selection is empty" },
        { "isValid", (PyCFunction)PySelectionList_isValid, METH_NOARGS, "Check whether selection is valid" },
        { "addPaths", (PyCFunction)PySelectionList_addPaths, METH_VARARGS, "Add paths to the selection" },
        { "removePaths", (PyCFunction)PySelectionList_removePaths, METH_VARARGS, "Remove paths from the selection" },
        { "togglePaths", (PyCFunction)PySelectionList_togglePaths, METH_VARARGS, "Toggle selection state for paths" },
        { "updatePaths", (PyCFunction)PySelectionList_updatePaths, METH_VARARGS, "Replace the current selection" },
        { "clear", (PyCFunction)PySelectionList_clear, METH_NOARGS, "Clear the current selection" },
        { nullptr } };

static PyTypeObject PySelectionListType = { PyVarObject_HEAD_INIT(nullptr, 0) };

// ----------------------------------------------------------------------------
// PyCommandStackObject
// ----------------------------------------------------------------------------

typedef struct {
    PyObject_HEAD CommandStack* stack;
} PyCommandStackObject;

static void
PyCommandStack_dealloc(PyCommandStackObject* self)
{
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
PyCommandStack_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PyCommandStackObject* self = reinterpret_cast<PyCommandStackObject*>(type->tp_alloc(type, 0));
    if (self)
        self->stack = currentSession() ? currentSession()->commandStack() : nullptr;
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PyCommandStack_canUndo(PyCommandStackObject* self)
{
    if (!checkCommandStack(self->stack))
        return nullptr;
    return PyBool_FromLong(self->stack->canUndo());
}

static PyObject*
PyCommandStack_canRedo(PyCommandStackObject* self)
{
    if (!checkCommandStack(self->stack))
        return nullptr;
    return PyBool_FromLong(self->stack->canRedo());
}

static PyObject*
PyCommandStack_canClear(PyCommandStackObject* self)
{
    if (!checkCommandStack(self->stack))
        return nullptr;
    return PyBool_FromLong(self->stack->canClear());
}

static PyObject*
PyCommandStack_undo(PyCommandStackObject* self)
{
    if (!checkCommandStack(self->stack))
        return nullptr;
    self->stack->undo();
    Py_RETURN_NONE;
}

static PyObject*
PyCommandStack_redo(PyCommandStackObject* self)
{
    if (!checkCommandStack(self->stack))
        return nullptr;
    self->stack->redo();
    Py_RETURN_NONE;
}

static PyObject*
PyCommandStack_clear(PyCommandStackObject* self)
{
    if (!checkCommandStack(self->stack))
        return nullptr;
    self->stack->clear();
    Py_RETURN_NONE;
}

static PyMethodDef PyCommandStack_methods[]
    = { { "canUndo", (PyCFunction)PyCommandStack_canUndo, METH_NOARGS, "Check whether undo is available" },
        { "canRedo", (PyCFunction)PyCommandStack_canRedo, METH_NOARGS, "Check whether redo is available" },
        { "canClear", (PyCFunction)PyCommandStack_canClear, METH_NOARGS, "Check whether clear is available" },
        { "undo", (PyCFunction)PyCommandStack_undo, METH_NOARGS, "Undo the current command" },
        { "redo", (PyCFunction)PyCommandStack_redo, METH_NOARGS, "Redo the next command" },
        { "clear", (PyCFunction)PyCommandStack_clear, METH_NOARGS, "Clear the command stack" },
        { nullptr } };

static PyTypeObject PyCommandStackType = { PyVarObject_HEAD_INIT(nullptr, 0) };

// ----------------------------------------------------------------------------
// PySessionObject
// ----------------------------------------------------------------------------

typedef struct {
    PyObject_HEAD Session* session;
} PySessionObject;

static void
PySession_dealloc(PySessionObject* self)
{
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
PySession_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PySessionObject* self = reinterpret_cast<PySessionObject*>(type->tp_alloc(type, 0));
    if (self)
        self->session = currentSession();
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PySession_beginProgressBlock(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* name = nullptr;
    Py_ssize_t count = 0;

    static const char* keywords[] = { "name", "count", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|n", const_cast<char**>(keywords), &name, &count))
        return nullptr;

    self->session->beginProgressBlock(QString::fromUtf8(name), static_cast<size_t>(count));
    Py_RETURN_NONE;
}

static PyObject*
PySession_updateProgressNotify(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* message = nullptr;
    Py_ssize_t completed = 0;
    PyObject* pyPaths = Py_None;
    long status = 0;
    PyObject* pyDetails = Py_None;

    static const char* keywords[] = { "message", "completed", "paths", "status", "details", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sn|OlO", const_cast<char**>(keywords), &message, &completed,
                                     &pyPaths, &status, &pyDetails)) {
        return nullptr;
    }

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    QVariantMap details;
    if (pyDetails && pyDetails != Py_None) {
        details = pyToVariantMap(pyDetails);
        if (PyErr_Occurred())
            return nullptr;
    }

    Session::Notify notify(QString::fromUtf8(message), paths, toNotifyStatus(status), details);
    self->session->updateProgressNotify(notify, static_cast<size_t>(completed));
    Py_RETURN_NONE;
}

static PyObject*
PySession_cancelProgressBlock(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    self->session->cancelProgressBlock();
    Py_RETURN_NONE;
}

static PyObject*
PySession_endProgressBlock(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    self->session->endProgressBlock();
    Py_RETURN_NONE;
}

static PyObject*
PySession_isProgressBlockCancelled(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyBool_FromLong(self->session->isProgressBlockCancelled());
}

static PyObject*
PySession_newStage(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    long policy = static_cast<long>(Session::LoadPolicy::All);
    static const char* keywords[] = { "policy", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|l", const_cast<char**>(keywords), &policy))
        return nullptr;

    const bool ok = self->session->newStage(toLoadPolicy(policy));
    return PyBool_FromLong(ok);
}

static PyObject*
PySession_load(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    long policy = static_cast<long>(Session::LoadPolicy::All);

    static const char* keywords[] = { "filename", "policy", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|l", const_cast<char**>(keywords), &filename, &policy))
        return nullptr;

    const bool ok = self->session->loadFromFile(QString::fromUtf8(filename), toLoadPolicy(policy));
    return PyBool_FromLong(ok);
}

static PyObject*
PySession_save(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return nullptr;

    return PyBool_FromLong(self->session->saveToFile(QString::fromUtf8(filename)));
}

static PyObject*
PySession_copy(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return nullptr;

    return PyBool_FromLong(self->session->copyToFile(QString::fromUtf8(filename)));
}

static PyObject*
PySession_flatten(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return nullptr;

    return PyBool_FromLong(self->session->flattenToFile(QString::fromUtf8(filename)));
}

static PyObject*
PySession_flattenPaths(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    PyObject* pyPaths = nullptr;
    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "Os", &pyPaths, &filename))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    return PyBool_FromLong(self->session->flattenPathsToFile(paths, QString::fromUtf8(filename)));
}

static PyObject*
PySession_reload(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyBool_FromLong(self->session->reload());
}

static PyObject*
PySession_close(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyBool_FromLong(self->session->close());
}

static PyObject*
PySession_isLoaded(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyBool_FromLong(self->session->isLoaded());
}

static PyObject*
PySession_mask(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return pathListToPyList(self->session->mask());
}

static PyObject*
PySession_setMask(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    self->session->setMask(paths);
    Py_RETURN_NONE;
}

static PyObject*
PySession_stageUp(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyLong_FromLong(static_cast<long>(self->session->stageUp()));
}

static PyObject*
PySession_setStageUp(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    long value = 0;
    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->session->setStageUp(toStageUp(value));
    Py_RETURN_NONE;
}

static PyObject*
PySession_loadPolicy(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyLong_FromLong(static_cast<long>(self->session->loadPolicy()));
}

static PyObject*
PySession_boundingBox(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return bboxToPyTuple(self->session->boundingBox());
}

static PyObject*
PySession_filename(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    const QString name = self->session->filename();
    return PyUnicode_FromString(name.toUtf8().constData());
}

static PyObject*
PySession_stage(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return wrapUsdStage(self->session->stage());
}

static PyObject*
PySession_stageUnsafe(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return wrapUsdStage(self->session->stageUnsafe());
}

static PyObject*
PySession_stageLock(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    QReadWriteLock* lock = self->session->stageLock();
    if (!lock)
        Py_RETURN_NONE;

    return PyLong_FromVoidPtr(lock);
}

static PyObject*
PySession_commandStack(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    CommandStack* stack = self->session->commandStack();
    if (!stack)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PyCommandStackType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PyCommandStackObject*>(object)->stack = stack;
    return object;
}

static PyObject*
PySession_selectionList(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    SelectionList* selection = self->session->selectionList();
    if (!selection)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PySelectionListType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PySelectionListObject*>(object)->selection = selection;
    return object;
}

static PyObject*
PySession_selection(PySessionObject* self)
{
    return PySession_selectionList(self);
}

static PyObject*
PySession_paths(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    SelectionList* selection = self->session->selectionList();
    if (!checkSelectionList(selection))
        return nullptr;

    return pathListToPyList(selection->paths());
}

static PyObject*
PySession_primsUpdate(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    return PyLong_FromLong(static_cast<long>(self->session->primsUpdate()));
}

static PyObject*
PySession_setPrimsUpdate(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    long value = 0;
    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->session->setPrimsUpdate(toPrimsUpdate(value));
    Py_RETURN_NONE;
}

static PyObject*
PySession_flushPrimsUpdates(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;
    self->session->flushPrimsUpdates();
    Py_RETURN_NONE;
}

static PyObject*
PySession_setStatus(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* status = nullptr;
    if (!PyArg_ParseTuple(args, "s", &status))
        return nullptr;

    self->session->notifyStatus(Session::Notify::Status::Info, QString::fromUtf8(status));
    Py_RETURN_NONE;
}

static PyMethodDef PySession_methods[]
    = { { "beginProgressBlock", (PyCFunction)PySession_beginProgressBlock, METH_VARARGS | METH_KEYWORDS,
          "Begin a progress block" },
        { "updateProgressNotify", (PyCFunction)PySession_updateProgressNotify, METH_VARARGS | METH_KEYWORDS,
          "Update progress with a notification" },
        { "cancelProgressBlock", (PyCFunction)PySession_cancelProgressBlock, METH_NOARGS,
          "Cancel the current progress block" },
        { "endProgressBlock", (PyCFunction)PySession_endProgressBlock, METH_NOARGS, "End the current progress block" },
        { "isProgressBlockCancelled", (PyCFunction)PySession_isProgressBlockCancelled, METH_NOARGS,
          "Check whether the progress block was cancelled" },

        { "newStage", (PyCFunction)PySession_newStage, METH_VARARGS | METH_KEYWORDS, "Create a new stage" },
        { "load", (PyCFunction)PySession_load, METH_VARARGS | METH_KEYWORDS, "Load a USD stage from file" },
        { "save", (PyCFunction)PySession_save, METH_VARARGS, "Save the current stage to file" },
        { "copy", (PyCFunction)PySession_copy, METH_VARARGS, "Copy the current stage to file" },
        { "flatten", (PyCFunction)PySession_flatten, METH_VARARGS, "Flatten the stage to file" },
        { "flattenPaths", (PyCFunction)PySession_flattenPaths, METH_VARARGS, "Flatten specific paths to file" },
        { "reload", (PyCFunction)PySession_reload, METH_NOARGS, "Reload the current stage" },
        { "close", (PyCFunction)PySession_close, METH_NOARGS, "Close the current stage" },
        { "isLoaded", (PyCFunction)PySession_isLoaded, METH_NOARGS, "Check if a stage is loaded" },

        { "mask", (PyCFunction)PySession_mask, METH_NOARGS, "Get the current mask" },
        { "setMask", (PyCFunction)PySession_setMask, METH_VARARGS, "Set the current mask" },
        { "stageUp", (PyCFunction)PySession_stageUp, METH_NOARGS, "Get the stage up axis" },
        { "setStageUp", (PyCFunction)PySession_setStageUp, METH_VARARGS, "Set the stage up axis" },
        { "loadPolicy", (PyCFunction)PySession_loadPolicy, METH_NOARGS, "Get the current load policy" },
        { "boundingBox", (PyCFunction)PySession_boundingBox, METH_NOARGS, "Get the current bounding box" },
        { "filename", (PyCFunction)PySession_filename, METH_NOARGS, "Get the current filename" },
        { "stage", (PyCFunction)PySession_stage, METH_NOARGS, "Get the native USD stage" },
        { "stageUnsafe", (PyCFunction)PySession_stageUnsafe, METH_NOARGS, "Get the native USD stage without locking" },
        { "stageLock", (PyCFunction)PySession_stageLock, METH_NOARGS, "Get the native stage lock address" },

        { "commandStack", (PyCFunction)PySession_commandStack, METH_NOARGS, "Get the command stack wrapper" },
        { "selectionList", (PyCFunction)PySession_selectionList, METH_NOARGS, "Get the selection list wrapper" },
        { "selection", (PyCFunction)PySession_selection, METH_NOARGS, "Get the selection list wrapper" },
        { "paths", (PyCFunction)PySession_paths, METH_NOARGS, "Get selected paths" },

        { "primsUpdate", (PyCFunction)PySession_primsUpdate, METH_NOARGS, "Get the prim update policy" },
        { "setPrimsUpdate", (PyCFunction)PySession_setPrimsUpdate, METH_VARARGS, "Set the prim update policy" },
        { "flushPrimsUpdates", (PyCFunction)PySession_flushPrimsUpdates, METH_NOARGS, "Flush buffered prim updates" },
        { "setStatus", (PyCFunction)PySession_setStatus, METH_VARARGS, "Set the session status string" },

        { nullptr } };

static PyTypeObject PySessionType = { PyVarObject_HEAD_INIT(nullptr, 0) };

static PyObject*
PyModule_session(PyObject*, PyObject*)
{
    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PySessionType), args);
    Py_DECREF(args);
    return object;
}

static PyObject*
PyModule_selectionList(PyObject*, PyObject*)
{
    Session* s = currentSession();
    if (!checkSession(s))
        return nullptr;

    SelectionList* selection = s->selectionList();
    if (!selection)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PySelectionListType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PySelectionListObject*>(object)->selection = selection;
    return object;
}

static PyObject*
PyModule_getCurrentStage(PyObject*, PyObject*)
{
    Session* s = currentSession();
    if (!checkSession(s))
        return nullptr;
    return wrapUsdStage(s->stage());
}

static PyMethodDef Module_methods[]
    = { { "session", (PyCFunction)PyModule_session, METH_NOARGS, "Get the current stageviz session wrapper" },
        { "selectionList", (PyCFunction)PyModule_selectionList, METH_NOARGS,
          "Get the current stageviz selection list wrapper" },
        { "getCurrentStage", (PyCFunction)PyModule_getCurrentStage, METH_NOARGS, "Get the current native USD stage" },
        { nullptr, nullptr, 0, nullptr } };

PyMODINIT_FUNC
PyInit_stageviz(void)
{
    pysessionModule.m_methods = Module_methods;

    PySessionType.tp_name = "stageviz.Session";
    PySessionType.tp_basicsize = sizeof(PySessionObject);
    PySessionType.tp_itemsize = 0;
    PySessionType.tp_flags = Py_TPFLAGS_DEFAULT;
    PySessionType.tp_new = PySession_new;
    PySessionType.tp_dealloc = reinterpret_cast<destructor>(PySession_dealloc);
    PySessionType.tp_methods = PySession_methods;

    PySelectionListType.tp_name = "stageviz.SelectionList";
    PySelectionListType.tp_basicsize = sizeof(PySelectionListObject);
    PySelectionListType.tp_itemsize = 0;
    PySelectionListType.tp_flags = Py_TPFLAGS_DEFAULT;
    PySelectionListType.tp_new = PySelectionList_new;
    PySelectionListType.tp_dealloc = reinterpret_cast<destructor>(PySelectionList_dealloc);
    PySelectionListType.tp_methods = PySelectionList_methods;

    PyCommandStackType.tp_name = "stageviz.CommandStack";
    PyCommandStackType.tp_basicsize = sizeof(PyCommandStackObject);
    PyCommandStackType.tp_itemsize = 0;
    PyCommandStackType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyCommandStackType.tp_new = PyCommandStack_new;
    PyCommandStackType.tp_dealloc = reinterpret_cast<destructor>(PyCommandStack_dealloc);
    PyCommandStackType.tp_methods = PyCommandStack_methods;

    if (PyType_Ready(&PySessionType) < 0)
        return nullptr;
    if (PyType_Ready(&PySelectionListType) < 0)
        return nullptr;
    if (PyType_Ready(&PyCommandStackType) < 0)
        return nullptr;

    PyObject* module = PyModule_Create(&pysessionModule);
    if (!module)
        return nullptr;

    Py_INCREF(&PySessionType);
    if (PyModule_AddObject(module, "Session", reinterpret_cast<PyObject*>(&PySessionType)) < 0) {
        Py_DECREF(&PySessionType);
        Py_DECREF(module);
        return nullptr;
    }

    Py_INCREF(&PySelectionListType);
    if (PyModule_AddObject(module, "SelectionList", reinterpret_cast<PyObject*>(&PySelectionListType)) < 0) {
        Py_DECREF(&PySelectionListType);
        Py_DECREF(module);
        return nullptr;
    }

    Py_INCREF(&PyCommandStackType);
    if (PyModule_AddObject(module, "CommandStack", reinterpret_cast<PyObject*>(&PyCommandStackType)) < 0) {
        Py_DECREF(&PyCommandStackType);
        Py_DECREF(module);
        return nullptr;
    }

    PyModule_AddIntConstant(module, "LoadAll", static_cast<int>(Session::LoadPolicy::All));
    PyModule_AddIntConstant(module, "LoadNone", static_cast<int>(Session::LoadPolicy::None));

    PyModule_AddIntConstant(module, "ProgressIdle", static_cast<int>(Session::ProgressMode::Idle));
    PyModule_AddIntConstant(module, "ProgressRunning", static_cast<int>(Session::ProgressMode::Running));

    PyModule_AddIntConstant(module, "PrimsImmediate", static_cast<int>(Session::PrimsUpdate::Immediate));
    PyModule_AddIntConstant(module, "PrimsDeferred", static_cast<int>(Session::PrimsUpdate::Deferred));

    PyModule_AddIntConstant(module, "StageLoaded", static_cast<int>(Session::StageStatus::Loaded));
    PyModule_AddIntConstant(module, "StageFailed", static_cast<int>(Session::StageStatus::Failed));
    PyModule_AddIntConstant(module, "StageClosed", static_cast<int>(Session::StageStatus::Closed));

    PyModule_AddIntConstant(module, "StageUpY", static_cast<int>(Session::StageUp::Y));
    PyModule_AddIntConstant(module, "StageUpZ", static_cast<int>(Session::StageUp::Z));

    PyModule_AddIntConstant(module, "NotifyInfo", 0);
    PyModule_AddIntConstant(module, "NotifyProgress", 1);
    PyModule_AddIntConstant(module, "NotifyWarning", 2);
    PyModule_AddIntConstant(module, "NotifyError", 3);

    return module;
}

extern "C" PyObject*
PyInit_stageviz_wrapper()
{
    return PyInit_stageviz();
}
