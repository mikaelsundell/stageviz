// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyselectionlist.h"
#include "pyutils.h"

namespace stageviz::python {

static void
PySelectionList_dealloc(PySelectionListObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
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

static PyMethodDef PySelectionList_methods[] = {
    { "isSelected", reinterpret_cast<PyCFunction>(PySelectionList_isSelected), METH_VARARGS,
      "Check if a path is selected" },
    { "paths", reinterpret_cast<PyCFunction>(PySelectionList_paths), METH_NOARGS, "Get selected paths" },
    { "isEmpty", reinterpret_cast<PyCFunction>(PySelectionList_isEmpty), METH_NOARGS,
      "Check whether selection is empty" },
    { "isValid", reinterpret_cast<PyCFunction>(PySelectionList_isValid), METH_NOARGS,
      "Check whether selection is valid" },
    { "addPaths", reinterpret_cast<PyCFunction>(PySelectionList_addPaths), METH_VARARGS, "Add paths to the selection" },
    { "removePaths", reinterpret_cast<PyCFunction>(PySelectionList_removePaths), METH_VARARGS,
      "Remove paths from the selection" },
    { "togglePaths", reinterpret_cast<PyCFunction>(PySelectionList_togglePaths), METH_VARARGS,
      "Toggle selection state for paths" },
    { "updatePaths", reinterpret_cast<PyCFunction>(PySelectionList_updatePaths), METH_VARARGS,
      "Replace the current selection" },
    { "clear", reinterpret_cast<PyCFunction>(PySelectionList_clear), METH_NOARGS, "Clear the current selection" },
    { nullptr }
};

PyTypeObject PySelectionListType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPySelectionListType()
{
    PySelectionListType.tp_name = "stageviz.SelectionList";
    PySelectionListType.tp_basicsize = sizeof(PySelectionListObject);
    PySelectionListType.tp_itemsize = 0;
    PySelectionListType.tp_flags = Py_TPFLAGS_DEFAULT;
    PySelectionListType.tp_new = PySelectionList_new;
    PySelectionListType.tp_dealloc = reinterpret_cast<destructor>(PySelectionList_dealloc);
    PySelectionListType.tp_methods = PySelectionList_methods;

    return PyType_Ready(&PySelectionListType) >= 0;
}

int
addPySelectionListType(PyObject* module)
{
    Py_INCREF(&PySelectionListType);
    if (PyModule_AddObject(module, "SelectionList", reinterpret_cast<PyObject*>(&PySelectionListType)) < 0) {
        Py_DECREF(&PySelectionListType);
        return -1;
    }

    return 0;
}

PyObject*
createPySelectionList(SelectionList* selection)
{
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

}  // namespace stageviz::python
