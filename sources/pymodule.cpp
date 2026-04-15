// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyselectionlist.h"
#include "pysession.h"
#include "pyutils.h"

using namespace stageviz;
using namespace stageviz::python;

static PyModuleDef pyStagevizModule = {
    PyModuleDef_HEAD_INIT, "stageviz", "Python interface for stageviz", -1, nullptr, nullptr, nullptr, nullptr, nullptr
};

static PyObject*
PyModule_session(PyObject*, PyObject*)
{
    return createPySession(currentSession());
}

static PyObject*
PyModule_selectionList(PyObject*, PyObject*)
{
    Session* s = currentSession();
    if (!checkSession(s))
        return nullptr;

    return createPySelectionList(s->selectionList());
}

static PyObject*
PyModule_getCurrentStage(PyObject*, PyObject*)
{
    Session* s = currentSession();
    if (!checkSession(s))
        return nullptr;

    return wrapUsdStage(s->stage());
}

static PyMethodDef Module_methods[] = { { "session", reinterpret_cast<PyCFunction>(PyModule_session), METH_NOARGS,
                                          "Get the current stageviz session wrapper" },
                                        { "selectionList", reinterpret_cast<PyCFunction>(PyModule_selectionList),
                                          METH_NOARGS, "Get the current stageviz selection list wrapper" },
                                        { "getCurrentStage", reinterpret_cast<PyCFunction>(PyModule_getCurrentStage),
                                          METH_NOARGS, "Get the current native USD stage" },
                                        { nullptr, nullptr, 0, nullptr } };

PyMODINIT_FUNC
PyInit_stageviz(void)
{
    pyStagevizModule.m_methods = Module_methods;

    if (!initPySelectionListType())
        return nullptr;

    if (!initPySessionType())
        return nullptr;

    PyObject* module = PyModule_Create(&pyStagevizModule);
    if (!module)
        return nullptr;

    if (addPySelectionListType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPySessionType(module) < 0) {
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
