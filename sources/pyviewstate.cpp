// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyviewstate.h"
#include "pyviewcamera.h"

namespace stageviz::python {

static bool
checkViewState(ViewState* viewState)
{
    if (viewState)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.ViewState");
    return false;
}

static void
PyViewState_dealloc(PyViewStateObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject*
PyViewState_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PyViewStateObject* self = reinterpret_cast<PyViewStateObject*>(type->tp_alloc(type, 0));
    if (self)
        self->viewState = nullptr;
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PyViewState_camera(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return createPyViewCamera(self->viewState->camera());
}

static PyMethodDef PyViewState_methods[]
    = { { "camera", reinterpret_cast<PyCFunction>(PyViewState_camera), METH_NOARGS, "Get the view camera wrapper" },
        { nullptr } };

PyTypeObject PyViewStateType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPyViewStateType()
{
    PyViewStateType.tp_name = "stageviz.ViewState";
    PyViewStateType.tp_basicsize = sizeof(PyViewStateObject);
    PyViewStateType.tp_itemsize = 0;
    PyViewStateType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyViewStateType.tp_new = PyViewState_new;
    PyViewStateType.tp_dealloc = reinterpret_cast<destructor>(PyViewState_dealloc);
    PyViewStateType.tp_methods = PyViewState_methods;
    return PyType_Ready(&PyViewStateType) >= 0;
}

int
addPyViewStateType(PyObject* module)
{
    Py_INCREF(&PyViewStateType);
    if (PyModule_AddObject(module, "ViewState", reinterpret_cast<PyObject*>(&PyViewStateType)) < 0) {
        Py_DECREF(&PyViewStateType);
        return -1;
    }
    return 0;
}

PyObject*
createPyViewState(ViewState* viewState)
{
    if (!viewState)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PyViewStateType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PyViewStateObject*>(object)->viewState = viewState;
    return object;
}

}  // namespace stageviz::python
