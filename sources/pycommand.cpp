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
#include <cstdint>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/token.h>
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

    static CommandStack* commandStack()
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

        return stack;
    }

    static bool pyToVtValueSimple(PyObject* object, VtValue* out)
    {
        if (!out) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid output value pointer");
            return false;
        }

        if (PyBool_Check(object)) {
            *out = VtValue(object == Py_True);
            return true;
        }

        if (PyLong_Check(object)) {
            const long long value = PyLong_AsLongLong(object);
            if (PyErr_Occurred())
                return false;

            *out = VtValue(static_cast<int64_t>(value));
            return true;
        }

        if (PyFloat_Check(object)) {
            const double value = PyFloat_AsDouble(object);
            if (PyErr_Occurred())
                return false;

            *out = VtValue(value);
            return true;
        }

        if (PyUnicode_Check(object)) {
            const char* text = PyUnicode_AsUTF8(object);
            if (!text)
                return false;

            *out = VtValue(std::string(text));
            return true;
        }

        if (PyTuple_Check(object) || PyList_Check(object)) {
            PyObject* sequence = PySequence_Fast(object, "value must be a sequence");
            if (!sequence)
                return false;

            const Py_ssize_t size = PySequence_Fast_GET_SIZE(sequence);

            if (size == 2 || size == 3 || size == 4) {
                double values[4] = { 0.0, 0.0, 0.0, 0.0 };
                bool numeric = true;

                for (Py_ssize_t i = 0; i < size; ++i) {
                    PyObject* item = PySequence_Fast_GET_ITEM(sequence, i);
                    if (!PyFloat_Check(item) && !PyLong_Check(item)) {
                        numeric = false;
                        break;
                    }

                    values[i] = PyFloat_AsDouble(item);
                    if (PyErr_Occurred()) {
                        Py_DECREF(sequence);
                        return false;
                    }
                }

                if (numeric) {
                    if (size == 2)
                        *out = VtValue(GfVec2d(values[0], values[1]));
                    else if (size == 3)
                        *out = VtValue(GfVec3d(values[0], values[1], values[2]));
                    else
                        *out = VtValue(GfVec4d(values[0], values[1], values[2], values[3]));

                    Py_DECREF(sequence);
                    return true;
                }
            }

            Py_DECREF(sequence);
        }

        PyErr_SetString(PyExc_TypeError,
                        "value must be bool, int, float, string, or a numeric sequence of length 2, 3, or 4");
        return false;
    }
}  // namespace

static PyObject*
PyCommand_undo(PyObject*, PyObject*)
{
    CommandStack* stack = commandStack();
    if (!stack)
        return nullptr;

    stack->undo();
    Py_RETURN_NONE;
}

static PyObject*
PyCommand_redo(PyObject*, PyObject*)
{
    CommandStack* stack = commandStack();
    if (!stack)
        return nullptr;

    stack->redo();
    Py_RETURN_NONE;
}

static PyObject*
PyCommand_clear(PyObject*, PyObject*)
{
    CommandStack* stack = commandStack();
    if (!stack)
        return nullptr;

    stack->clear();
    Py_RETURN_NONE;
}

static PyObject*
PyCommand_canUndo(PyObject*, PyObject*)
{
    CommandStack* stack = commandStack();
    if (!stack)
        return nullptr;

    return PyBool_FromLong(stack->canUndo() ? 1 : 0);
}

static PyObject*
PyCommand_canRedo(PyObject*, PyObject*)
{
    CommandStack* stack = commandStack();
    if (!stack)
        return nullptr;

    return PyBool_FromLong(stack->canRedo() ? 1 : 0);
}

static PyObject*
PyCommand_resetTransforms(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(resetTransforms(paths));
}

static PyObject*
PyCommand_resetOverrides(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(resetOverrides(paths));
}

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
PyCommand_selectAll(PyObject*, PyObject* args, PyObject* kwargs)
{
    int recursive = 0;
    static const char* keywords[] = { "recursive", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", const_cast<char**>(keywords), &recursive))
        return nullptr;

    return runCommand(selectAll(recursive != 0));
}

static PyObject*
PyCommand_selectInvert(PyObject*, PyObject*)
{
    return runCommand(selectInvert());
}

static PyObject*
PyCommand_selectPayload(PyObject*, PyObject*)
{
    return runCommand(selectPayload());
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
PyCommand_loadNeighborPayloads(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(loadNeighborPayloads(paths));
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
PyCommand_duplicatePaths(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(duplicatePaths(paths));
}

static PyObject*
PyCommand_newPrim(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyParentPath = nullptr;
    const char* name = nullptr;
    const char* typeName = "";

    static const char* keywords[] = { "parent_path", "name", "type_name", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Os|s", const_cast<char**>(keywords), &pyParentPath, &name,
                                     &typeName)) {
        return nullptr;
    }

    SdfPath parentPath;
    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    return runCommand(newPrimPath(parentPath, QString::fromUtf8(name), TfToken(typeName)));
}

static PyObject*
PyCommand_newScope(PyObject*, PyObject* args)
{
    PyObject* pyParentPath = nullptr;
    const char* name = nullptr;
    if (!PyArg_ParseTuple(args, "Os", &pyParentPath, &name))
        return nullptr;

    SdfPath parentPath;
    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    return runCommand(newScopePath(parentPath, QString::fromUtf8(name)));
}

static PyObject*
PyCommand_newMaterial(PyObject*, PyObject* args)
{
    PyObject* pyParentPath = nullptr;
    const char* name = nullptr;
    if (!PyArg_ParseTuple(args, "Os", &pyParentPath, &name))
        return nullptr;

    SdfPath parentPath;
    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    return runCommand(newMaterialPath(parentPath, QString::fromUtf8(name)));
}

static PyObject*
PyCommand_newReference(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyParentPath = nullptr;
    const char* name = nullptr;
    const char* assetPath = nullptr;
    const char* primPath = "";

    static const char* keywords[] = { "parent_path", "name", "asset_path", "prim_path", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|s", const_cast<char**>(keywords), &pyParentPath, &name,
                                     &assetPath, &primPath)) {
        return nullptr;
    }

    SdfPath parentPath;
    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    SdfPath targetPrimPath;
    if (primPath && primPath[0] != '\0') {
        targetPrimPath = SdfPath(primPath);
        if (!checkPath(targetPrimPath, "prim_path"))
            return nullptr;
    }

    return runCommand(
        newReferencePath(parentPath, QString::fromUtf8(name), QString::fromUtf8(assetPath), targetPrimPath));
}

static PyObject*
PyCommand_newPayload(PyObject*, PyObject* args, PyObject* kwargs)
{
    PyObject* pyParentPath = nullptr;
    const char* name = nullptr;
    const char* assetPath = nullptr;
    const char* primPath = "";

    static const char* keywords[] = { "parent_path", "name", "asset_path", "prim_path", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|s", const_cast<char**>(keywords), &pyParentPath, &name,
                                     &assetPath, &primPath)) {
        return nullptr;
    }

    SdfPath parentPath;
    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    SdfPath targetPrimPath;
    if (primPath && primPath[0] != '\0') {
        targetPrimPath = SdfPath(primPath);
        if (!checkPath(targetPrimPath, "prim_path"))
            return nullptr;
    }

    return runCommand(
        newPayloadPath(parentPath, QString::fromUtf8(name), QString::fromUtf8(assetPath), targetPrimPath));
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
    PyObject* pyPaths = nullptr;
    PyObject* pyParentPath = nullptr;
    int insertIndex = -1;
    int preserveWorldTransform = 1;
    static const char* keywords[] = { "paths", "parent_path", "insert_index", "preserve_world_transform", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|ip", const_cast<char**>(keywords), &pyPaths, &pyParentPath,
                                     &insertIndex, &preserveWorldTransform)) {
        return nullptr;
    }

    QList<SdfPath> paths;
    SdfPath parentPath;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    if (!parsePathArg(pyParentPath, "parent_path", &parentPath))
        return nullptr;

    return runCommand(movePath(paths, parentPath, insertIndex, preserveWorldTransform != 0));
}

static PyObject*
PyCommand_resetAttributeOverride(PyObject*, PyObject* args)
{
    PyObject* pyAttributePath = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyAttributePath))
        return nullptr;

    SdfPath attributePath;
    if (!parsePathArg(pyAttributePath, "attribute_path", &attributePath))
        return nullptr;

    if (!attributePath.IsPropertyPath()) {
        PyErr_SetString(PyExc_ValueError, "attribute_path must be a USD property path");
        return nullptr;
    }

    return runCommand(resetAttributeOverride(attributePath));
}

static PyObject*
PyCommand_resetAttributeOverrides(PyObject*, PyObject* args)
{
    PyObject* pyAttributePaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyAttributePaths))
        return nullptr;

    QList<SdfPath> attributePaths;
    if (!parsePathListArg(pyAttributePaths, "attribute_paths", &attributePaths))
        return nullptr;

    for (const SdfPath& path : attributePaths) {
        if (!path.IsPropertyPath()) {
            PyErr_SetString(PyExc_ValueError, "attribute_paths must contain USD property paths");
            return nullptr;
        }
    }

    return runCommand(resetAttributeOverrides(attributePaths));
}

static PyObject*
PyCommand_setAttributeValue(PyObject*, PyObject* args)
{
    PyObject* pyAttributePath = nullptr;
    PyObject* pyValue = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &pyAttributePath, &pyValue))
        return nullptr;

    SdfPath attributePath;
    if (!parsePathArg(pyAttributePath, "attribute_path", &attributePath))
        return nullptr;

    if (!attributePath.IsPropertyPath()) {
        PyErr_SetString(PyExc_ValueError, "attribute_path must be a USD property path");
        return nullptr;
    }

    VtValue value;
    if (!pyToVtValueSimple(pyValue, &value))
        return nullptr;

    return runCommand(setAttributeValue(attributePath, value));
}

static PyObject*
PyCommand_setAttributeValues(PyObject*, PyObject* args)
{
    PyObject* pyAttributePaths = nullptr;
    PyObject* pyValue = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &pyAttributePaths, &pyValue))
        return nullptr;

    QList<SdfPath> attributePaths;
    if (!parsePathListArg(pyAttributePaths, "attribute_paths", &attributePaths))
        return nullptr;

    for (const SdfPath& path : attributePaths) {
        if (!path.IsPropertyPath()) {
            PyErr_SetString(PyExc_ValueError, "attribute_paths must contain USD property paths");
            return nullptr;
        }
    }

    VtValue value;
    if (!pyToVtValueSimple(pyValue, &value))
        return nullptr;

    return runCommand(setAttributeValues(attributePaths, value));
}

static PyObject*
PyCommand_resetAttributeValues(PyObject*, PyObject* args)
{
    PyObject* pyAttributePaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyAttributePaths))
        return nullptr;

    QList<SdfPath> attributePaths;
    if (!parsePathListArg(pyAttributePaths, "attribute_paths", &attributePaths))
        return nullptr;

    for (const SdfPath& path : attributePaths) {
        if (!path.IsPropertyPath()) {
            PyErr_SetString(PyExc_ValueError, "attribute_paths must contain USD property paths");
            return nullptr;
        }
    }

    return runCommand(resetAttributeValues(attributePaths));
}

static PyObject*
PyCommand_centerPivots(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(centerPivots(paths));
}

static PyObject*
PyCommand_resetPivots(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(resetPivots(paths));
}

static PyObject*
PyCommand_identityTransforms(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    return runCommand(identityTransforms(paths));
}

static PyObject*
PyCommand_bindMaterial(PyObject*, PyObject* args)
{
    PyObject* pyPaths = nullptr;
    PyObject* pyMaterialPath = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &pyPaths, &pyMaterialPath))
        return nullptr;

    QList<SdfPath> paths;
    SdfPath materialPath;
    if (!parsePathListArg(pyPaths, "paths", &paths))
        return nullptr;

    if (!parsePathArg(pyMaterialPath, "material_path", &materialPath))
        return nullptr;

    return runCommand(bindMaterial(paths, materialPath));
}

static PyMethodDef PyCommand_methods[] = {
    { "undo", reinterpret_cast<PyCFunction>(PyCommand_undo), METH_NOARGS, "Undo the most recent command." },
    { "redo", reinterpret_cast<PyCFunction>(PyCommand_redo), METH_NOARGS, "Redo the most recently undone command." },
    { "clear", reinterpret_cast<PyCFunction>(PyCommand_clear), METH_NOARGS, "Clear command history." },
    { "can_undo", reinterpret_cast<PyCFunction>(PyCommand_canUndo), METH_NOARGS, "Return whether undo is available." },
    { "can_redo", reinterpret_cast<PyCFunction>(PyCommand_canRedo), METH_NOARGS, "Return whether redo is available." },
    { "select_paths", reinterpret_cast<PyCFunction>(PyCommand_selectPaths), METH_VARARGS, "Select paths." },
    { "select_all", reinterpret_cast<PyCFunction>(PyCommand_selectAll), METH_VARARGS | METH_KEYWORDS,
      "Select prims. Optional keyword argument: recursive." },
    { "select_invert", reinterpret_cast<PyCFunction>(PyCommand_selectInvert), METH_NOARGS,
      "Invert the current selection." },
    { "select_payload", reinterpret_cast<PyCFunction>(PyCommand_selectPayload), METH_NOARGS,
      "Select the nearest owning payload for the current selection." },
    { "select_invert_payload", reinterpret_cast<PyCFunction>(PyCommand_selectInvertPayload), METH_NOARGS,
      "Invert the current payload selection." },
    { "isolate_paths", reinterpret_cast<PyCFunction>(PyCommand_isolatePaths), METH_VARARGS, "Isolate paths." },
    { "show_paths", reinterpret_cast<PyCFunction>(PyCommand_showPaths), METH_VARARGS | METH_KEYWORDS, "Show paths." },
    { "hide_paths", reinterpret_cast<PyCFunction>(PyCommand_hidePaths), METH_VARARGS | METH_KEYWORDS, "Hide paths." },
    { "load_payloads", reinterpret_cast<PyCFunction>(PyCommand_loadPayloads), METH_VARARGS | METH_KEYWORDS,
      "Load payloads. Optional keyword arguments: variant_set, variant_value." },
    { "load_neighbor_payloads", reinterpret_cast<PyCFunction>(PyCommand_loadNeighborPayloads), METH_VARARGS,
      "Load spatially neighboring payloads using extentsHint." },
    { "unload_payloads", reinterpret_cast<PyCFunction>(PyCommand_unloadPayloads), METH_VARARGS, "Unload payloads." },
    { "set_stage_up", reinterpret_cast<PyCFunction>(PyCommand_setStageUp), METH_VARARGS, "Set the stage up axis." },
    { "set_default_prim", reinterpret_cast<PyCFunction>(PyCommand_setDefaultPrim), METH_VARARGS,
      "Set the default prim." },
    { "delete_paths", reinterpret_cast<PyCFunction>(PyCommand_deletePaths), METH_VARARGS, "Delete paths." },
    { "duplicate_paths", reinterpret_cast<PyCFunction>(PyCommand_duplicatePaths), METH_VARARGS,
      "Duplicate one or more prim paths." },
    { "new_prim", reinterpret_cast<PyCFunction>(PyCommand_newPrim), METH_VARARGS | METH_KEYWORDS,
      "Create a generic prim. Optional keyword argument: type_name." },
    { "new_scope", reinterpret_cast<PyCFunction>(PyCommand_newScope), METH_VARARGS, "Create a new Scope prim." },
    { "new_material", reinterpret_cast<PyCFunction>(PyCommand_newMaterial), METH_VARARGS,
      "Create a new material with a UsdPreviewSurface shader." },
    { "new_reference", reinterpret_cast<PyCFunction>(PyCommand_newReference), METH_VARARGS | METH_KEYWORDS,
      "Create a prim with a reference arc. Optional keyword argument: prim_path." },
    { "new_payload", reinterpret_cast<PyCFunction>(PyCommand_newPayload), METH_VARARGS | METH_KEYWORDS,
      "Create a prim with a payload arc. Optional keyword argument: prim_path." },
    { "rename_path", reinterpret_cast<PyCFunction>(PyCommand_renamePath), METH_VARARGS, "Rename a path." },
    { "new_xform", reinterpret_cast<PyCFunction>(PyCommand_newXform), METH_VARARGS, "Create a new Xform path." },
    { "move_path", reinterpret_cast<PyCFunction>(PyCommand_movePath), METH_VARARGS | METH_KEYWORDS,
      "Move paths. Optional keyword arguments: insert_index, preserve_world_transform." },
    { "set_attribute_value", reinterpret_cast<PyCFunction>(PyCommand_setAttributeValue), METH_VARARGS,
      "Set one USD attribute value in the active edit layer." },
    { "set_attribute_values", reinterpret_cast<PyCFunction>(PyCommand_setAttributeValues), METH_VARARGS,
      "Set one value on several USD attributes in the active edit layer." },
    { "reset_attribute_values", reinterpret_cast<PyCFunction>(PyCommand_resetAttributeValues), METH_VARARGS,
      "Clear authored default values from USD attributes in the active edit layer." },
    { "reset_attribute_override", reinterpret_cast<PyCFunction>(PyCommand_resetAttributeOverride), METH_VARARGS,
      "Reset one USD attribute override in the active edit layer." },
    { "reset_attribute_overrides", reinterpret_cast<PyCFunction>(PyCommand_resetAttributeOverrides), METH_VARARGS,
      "Reset several USD attribute overrides in the active edit layer." },
    { "center_pivots", reinterpret_cast<PyCFunction>(PyCommand_centerPivots), METH_VARARGS,
      "Center transform pivots on prim bounds." },
    { "reset_pivots", reinterpret_cast<PyCFunction>(PyCommand_resetPivots), METH_VARARGS,
      "Reset Stageviz-authored pivots while preserving local transforms." },
    { "reset_transforms", reinterpret_cast<PyCFunction>(PyCommand_resetTransforms), METH_VARARGS,
      "Reset local transforms in the active edit layer." },
    { "identity_transforms", reinterpret_cast<PyCFunction>(PyCommand_identityTransforms), METH_VARARGS,
      "Set local transforms to identity using edit-layer-aware transform reset behavior." },
    { "reset_overrides", reinterpret_cast<PyCFunction>(PyCommand_resetOverrides), METH_VARARGS,
      "Reset direct root-layer overrides." },
    { "bind_material", reinterpret_cast<PyCFunction>(PyCommand_bindMaterial), METH_VARARGS,
      "Bind an existing USD material to one or more prim paths." },
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
