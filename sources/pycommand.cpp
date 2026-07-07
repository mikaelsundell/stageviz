// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pycommand.h"

#include "application.h"
#include "command.h"
#include "commandstack.h"
#include "pyutils.h"
#include "qtutils.h"
#include "session.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::python {
namespace {

    static bool checkApplication(Application* application)
    {
        if (application)
            return true;

        PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.Application");
        return false;
    }

    static bool checkPath(const SdfPath& path, const char* argumentName)
    {
        if (!path.IsEmpty())
            return true;

        PyErr_Format(PyExc_ValueError, "%s must be a valid non-empty SdfPath", argumentName);
        return false;
    }

    static bool parsePathArg(PyObject* object, const char* argumentName, SdfPath* out)
    {
        if (!out) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid output path pointer");
            return false;
        }

        if (!PyUnicode_Check(object)) {
            PyErr_Format(PyExc_TypeError, "%s must be a string", argumentName);
            return false;
        }

        const char* text = PyUnicode_AsUTF8(object);
        if (!text)
            return false;

        *out = SdfPath(text);
        return checkPath(*out, argumentName);
    }

    static bool parsePathListArg(PyObject* object, const char* argumentName, QList<SdfPath>* out)
    {
        if (!out) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid output path list pointer");
            return false;
        }

        if (!pyToPathList(object, out))
            return false;

        for (const SdfPath& path : *out) {
            if (!checkPath(path, argumentName))
                return false;
        }

        return true;
    }

    static PyObject* runCommand(const Command& command)
    {
        Session* s = stageviz::session();
        if (!s) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.Session");
            return nullptr;
        }

        CommandStack* stack = s->commandStack();
        if (!stack) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.CommandStack");
            return nullptr;
        }

        stack->run(new Command(command));
        Py_RETURN_NONE;
    }

}  // namespace

static PyObject*
PyCommand_selectPaths(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(selectPaths(paths));
}

static PyObject*
PyCommand_selectAll(PyObject*, PyObject*)
{
    return runCommand(selectAll());
}

static PyObject*
PyCommand_selectInvert(PyObject*, PyObject*)
{
    return runCommand(selectInvert());
}

static PyObject*
PyCommand_selectInvertPayload(PyObject*, PyObject*)
{
    return runCommand(selectInvertPayload());
}

static PyObject*
PyCommand_isolatePaths(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(isolatePaths(paths));
}

static PyObject*
PyCommand_showPaths(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyPaths = nullptr;
    int recursive = 0;

    static const char* keywords[] = { "paths", "recursive", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p", const_cast<char**>(keywords), &pyPaths, &recursive))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(showPaths(paths, recursive != 0));
}

static PyObject*
PyCommand_hidePaths(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyPaths = nullptr;
    int recursive = 0;

    static const char* keywords[] = { "paths", "recursive", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p", const_cast<char**>(keywords), &pyPaths, &recursive))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(hidePaths(paths, recursive != 0));
}

static PyObject*
PyCommand_loadPayloads(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyPaths = nullptr;
    const char* variantSet = "";
    const char* variantValue = "";

    static const char* keywords[] = { "paths", "variant_set", "variant_value", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|ss", const_cast<char**>(keywords), &pyPaths, &variantSet,
                                     &variantValue)) {
        return nullptr;
    }

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(loadPayloads(paths, QString::fromUtf8(variantSet), QString::fromUtf8(variantValue)));
}

static PyObject*
PyCommand_unloadPayloads(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(unloadPayloads(paths));
}

static PyObject*
PyCommand_setStageUp(PyObject*, PyObject* args)
{
    long value = 0;
    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    return runCommand(stageUp(toStageUp(value)));
}

static PyObject*
PyCommand_setDefaultPrim(PyObject*, PyObject* args)
{
    PyObject* pyPath = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPath))
        return nullptr;

    SdfPath path;
    if (!parsePathArg(pyPath, "path", &path))
        return nullptr;

    return runCommand(defaultPrimPath(path));
}

static PyObject*
PyCommand_deletePaths(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(deletePaths(paths));
}

static PyObject*
PyCommand_renamePath(PyObject*, PyObject* args)
{
    PyObject* pyPath = nullptr;
    const char* name = nullptr;

    if (!PyArg_ParseTuple(args, "Os", &pyPath, &name))
        return nullptr;

    SdfPath path;
    if (!parsePathArg(pyPath, "path", &path))
        return nullptr;

    return runCommand(renamePath(path, QString::fromUtf8(name)));
}

static PyObject*
PyCommand_newXform(PyObject*, PyObject* args)
{
    PyObject* pyParentPath = nullptr;
    const char* name = nullptr;

    if (!PyArg_ParseTuple(args, "Os", &pyParentPath, &name))
        return nullptr;

    SdfPath parentPath;
    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    return runCommand(newXformPath(parentPath, QString::fromUtf8(name)));
}

static PyObject*
PyCommand_movePath(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyPath = nullptr;
    PyObject* pyParentPath = nullptr;
    int insertIndex = -1;

    static const char* keywords[] = { "path", "parent_path", "insert_index", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|i", const_cast<char**>(keywords), &pyPath, &pyParentPath,
                                     &insertIndex)) {
        return nullptr;
    }

    SdfPath path;
    SdfPath parentPath;

    if (!parsePathArg(pyPath, "path", &path))
        return nullptr;

    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    return runCommand(movePath(path, parentPath, insertIndex));
}

static PyMethodDef PyCommand_methods[] = {
    { "select_paths", reinterpret_cast<PyCFunction>(PyCommand_selectPaths), METH_VARARGS, "Select paths." },
    { "select_all", reinterpret_cast<PyCFunction>(PyCommand_selectAll), METH_NOARGS,
      "Select all selectable leaf paths." },
    { "select_invert", reinterpret_cast<PyCFunction>(PyCommand_selectInvert), METH_NOARGS,
      "Invert the current selection." },
    { "select_invert_payload", reinterpret_cast<PyCFunction>(PyCommand_selectInvertPayload), METH_NOARGS,
      "Invert the current payload selection." },

    { "isolate_paths", reinterpret_cast<PyCFunction>(PyCommand_isolatePaths), METH_VARARGS, "Isolate paths." },
    { "show_paths", reinterpret_cast<PyCFunction>(PyCommand_showPaths), METH_VARARGS | METH_KEYWORDS, "Show paths." },
    { "hide_paths", reinterpret_cast<PyCFunction>(PyCommand_hidePaths), METH_VARARGS | METH_KEYWORDS, "Hide paths." },

    { "load_payloads", reinterpret_cast<PyCFunction>(PyCommand_loadPayloads), METH_VARARGS | METH_KEYWORDS,
      "Load payloads. Optional keyword arguments: variant_set, variant_value." },
    { "unload_payloads", reinterpret_cast<PyCFunction>(PyCommand_unloadPayloads), METH_VARARGS, "Unload payloads." },

    { "set_stage_up", reinterpret_cast<PyCFunction>(PyCommand_setStageUp), METH_VARARGS, "Set the stage up axis." },
    { "set_default_prim", reinterpret_cast<PyCFunction>(PyCommand_setDefaultPrim), METH_VARARGS,
      "Set the default prim." },

    { "delete_paths", reinterpret_cast<PyCFunction>(PyCommand_deletePaths), METH_VARARGS, "Delete paths." },
    { "rename_path", reinterpret_cast<PyCFunction>(PyCommand_renamePath), METH_VARARGS, "Rename a path." },
    { "new_xform", reinterpret_cast<PyCFunction>(PyCommand_newXform), METH_VARARGS, "Create a new Xform path." },
    { "move_path", reinterpret_cast<PyCFunction>(PyCommand_movePath), METH_VARARGS | METH_KEYWORDS, "Move a path." },

    { nullptr, nullptr, 0, nullptr }
};

static PyModuleDef PyCommandModule = { PyModuleDef_HEAD_INIT,
                                       "stageviz.command",
                                       "Stageviz command API.",
                                       -1,
                                       PyCommand_methods,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr };

int
addPyCommandModule(PyObject* module)
{
    PyObject* commandModule = PyModule_Create(&PyCommandModule);
    if (!commandModule)
        return -1;

    if (PyModule_AddObject(module, "command", commandModule) < 0) {
        Py_DECREF(commandModule);
        return -1;
    }

    return 0;
}

}  // namespace stageviz::python
