// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyapplication.h"

#include <QTimer>
#include <vector>

namespace stageviz::python {

static std::vector<PyObject*> s_widgetRefs;

static bool
checkApplication(Application* application)
{
    if (application)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.Application");
    return false;
}

static PyObject*
callMethodNoArgs(PyObject* object, const char* name)
{
    PyObject* method = PyObject_GetAttrString(object, name);
    if (!method) {
        PyErr_Clear();
        Py_RETURN_NONE;
    }

    if (!PyCallable_Check(method)) {
        Py_DECREF(method);
        Py_RETURN_NONE;
    }

    PyObject* result = PyObject_CallObject(method, nullptr);
    Py_DECREF(method);

    return result;
}

static void
PyApplication_dealloc(PyApplicationObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject*
PyApplication_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PyApplicationObject* self = reinterpret_cast<PyApplicationObject*>(type->tp_alloc(type, 0));
    if (self)
        self->application = Application::instance();

    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PyApplication_invokeLater(PyApplicationObject* self, PyObject* args)
{
    if (!checkApplication(self->application))
        return nullptr;

    PyObject* callable = nullptr;
    int delay = 0;

    if (!PyArg_ParseTuple(args, "O|i", &callable, &delay))
        return nullptr;

    if (!PyCallable_Check(callable)) {
        PyErr_SetString(PyExc_TypeError, "invokeLater() expects a callable");
        return nullptr;
    }

    Py_INCREF(callable);

    QTimer::singleShot(delay, self->application, [callable]() {
        PyGILState_STATE gil = PyGILState_Ensure();

        PyObject* result = PyObject_CallObject(callable, nullptr);
        if (!result)
            PyErr_Print();
        else
            Py_DECREF(result);

        Py_DECREF(callable);
        PyGILState_Release(gil);
    });

    Py_RETURN_NONE;
}

static PyObject*
PyApplication_show(PyApplicationObject* self, PyObject* args)
{
    if (!checkApplication(self->application))
        return nullptr;

    PyObject* widget = nullptr;

    if (!PyArg_ParseTuple(args, "O", &widget))
        return nullptr;

    Py_INCREF(widget);
    s_widgetRefs.push_back(widget);

    QTimer::singleShot(0, self->application, [widget]() {
        PyGILState_STATE gil = PyGILState_Ensure();

        PyObject* result = callMethodNoArgs(widget, "show");
        if (!result) {
            PyErr_Print();
            PyGILState_Release(gil);
            return;
        }
        Py_DECREF(result);

        result = callMethodNoArgs(widget, "raise_");
        if (!result)
            PyErr_Print();
        else
            Py_DECREF(result);

        result = callMethodNoArgs(widget, "activateWindow");
        if (!result)
            PyErr_Print();
        else
            Py_DECREF(result);

        PyGILState_Release(gil);
    });

    Py_RETURN_NONE;
}

static PyMethodDef PyApplication_methods[]
    = { { "invokeLater", reinterpret_cast<PyCFunction>(PyApplication_invokeLater), METH_VARARGS,
          "Invoke a Python callable later on the Qt event loop. Optional delay in milliseconds." },
        { "show", reinterpret_cast<PyCFunction>(PyApplication_show), METH_VARARGS,
          "Show a Python Qt widget later on the Qt event loop." },
        { nullptr } };

PyTypeObject PyApplicationType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPyApplicationType()
{
    PyApplicationType.tp_name = "stageviz.Application";
    PyApplicationType.tp_basicsize = sizeof(PyApplicationObject);
    PyApplicationType.tp_itemsize = 0;
    PyApplicationType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyApplicationType.tp_new = PyApplication_new;
    PyApplicationType.tp_dealloc = reinterpret_cast<destructor>(PyApplication_dealloc);
    PyApplicationType.tp_methods = PyApplication_methods;
    PyApplicationType.tp_doc = "Stageviz application wrapper";

    return PyType_Ready(&PyApplicationType) >= 0;
}

int
addPyApplicationType(PyObject* module)
{
    Py_INCREF(&PyApplicationType);
    if (PyModule_AddObject(module, "Application", reinterpret_cast<PyObject*>(&PyApplicationType)) < 0) {
        Py_DECREF(&PyApplicationType);
        return -1;
    }

    return 0;
}

PyObject*
createPyApplication(Application* application)
{
    if (!application)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PyApplicationType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PyApplicationObject*>(object)->application = application;
    return object;
}

}  // namespace stageviz::python