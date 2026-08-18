// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyviewstate.h"
#include "pyutils.h"
#include "pyviewcamera.h"

namespace stageviz::python {

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

static PyObject*
PyViewState_backgroundColor(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return colorToPyTuple(self->viewState->backgroundColor());
}

static PyObject*
PyViewState_setBackgroundColor(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    PyObject* object = nullptr;
    if (!PyArg_ParseTuple(args, "O", &object))
        return nullptr;

    QColor color;
    if (!pyToColor(object, &color))
        return nullptr;

    self->viewState->setBackgroundColor(color);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_gridColor(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return colorToPyTuple(self->viewState->gridColor());
}

static PyObject*
PyViewState_setGridColor(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    PyObject* object = nullptr;
    if (!PyArg_ParseTuple(args, "O", &object))
        return nullptr;

    QColor color;
    if (!pyToColor(object, &color))
        return nullptr;

    self->viewState->setGridColor(color);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_gridEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->gridEnabled());
}

static PyObject*
PyViewState_setGridEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setGridEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_materialMode(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->viewState->materialMode()));
}

static PyObject*
PyViewState_setMaterialMode(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    long mode = 0;
    if (!PyArg_ParseTuple(args, "l", &mode))
        return nullptr;

    if (mode < static_cast<long>(ViewState::All) || mode > static_cast<long>(ViewState::Override)) {
        PyErr_SetString(PyExc_ValueError, "Invalid material mode");
        return nullptr;
    }

    self->viewState->setMaterialMode(static_cast<ViewState::MaterialMode>(mode));
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_overrideMaterial(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return sdfPathToPyString(self->viewState->overrideMaterial());
}

static PyObject*
PyViewState_setOverrideMaterial(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    PyObject* object = nullptr;
    if (!PyArg_ParseTuple(args, "O", &object))
        return nullptr;

    SdfPath path;
    if (!pyToPath(object, &path))
        return nullptr;

    if (!path.IsEmpty() && !path.IsPrimPath()) {
        PyErr_SetString(PyExc_ValueError, "Override material must be an absolute prim path on the auxiliary stage");
        return nullptr;
    }

    self->viewState->setOverrideMaterial(path);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_defaultCameraLightEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->defaultCameraLightEnabled());
}

static PyObject*
PyViewState_setDefaultCameraLightEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setDefaultCameraLightEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_sceneLightsEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->sceneLightsEnabled());
}

static PyObject*
PyViewState_setSceneLightsEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setSceneLightsEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_sceneMaterialsEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->sceneMaterialsEnabled());
}

static PyObject*
PyViewState_setSceneMaterialsEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setSceneMaterialsEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_doubleSidedMode(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->viewState->doubleSidedMode()));
}

static PyObject*
PyViewState_setDoubleSidedMode(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    long mode = 0;
    if (!PyArg_ParseTuple(args, "l", &mode))
        return nullptr;

    if (mode < static_cast<long>(ViewState::Primitive) || mode > static_cast<long>(ViewState::SingleSided)) {
        PyErr_SetString(PyExc_ValueError,
                        "Invalid double-sided mode. Expected Primitive, DoubleSided, or SingleSided.");
        return nullptr;
    }

    self->viewState->setDoubleSidedMode(static_cast<ViewState::DoubleSidedMode>(mode));
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_renderMode(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->viewState->renderMode()));
}

static PyObject*
PyViewState_setRenderMode(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    long mode = 0;
    if (!PyArg_ParseTuple(args, "l", &mode))
        return nullptr;

    if (mode < static_cast<long>(ViewState::Shaded) || mode > static_cast<long>(ViewState::Wireframe)) {
        PyErr_SetString(PyExc_ValueError, "Invalid render mode");
        return nullptr;
    }

    self->viewState->setRenderMode(static_cast<ViewState::RenderMode>(mode));
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_complexityLevel(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->viewState->complexityLevel()));
}

static PyObject*
PyViewState_setComplexityLevel(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    long level = 0;
    if (!PyArg_ParseTuple(args, "l", &level))
        return nullptr;

    if (level < static_cast<long>(ViewState::Low) || level > static_cast<long>(ViewState::VeryHigh)) {
        PyErr_SetString(PyExc_ValueError, "Invalid complexity level");
        return nullptr;
    }

    self->viewState->setComplexityLevel(static_cast<ViewState::ComplexityLevel>(level));
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_rendererAov(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    const QByteArray value = self->viewState->rendererAov().toUtf8();
    return PyUnicode_FromString(value.constData());
}

static PyObject*
PyViewState_setRendererAov(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    const char* value = nullptr;
    if (!PyArg_ParseTuple(args, "s", &value))
        return nullptr;

    self->viewState->setRendererAov(QString::fromUtf8(value));
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_sceneStatsEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->sceneStatsEnabled());
}

static PyObject*
PyViewState_setSceneStatsEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setSceneStatsEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_performanceStatsEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->performanceStatsEnabled());
}

static PyObject*
PyViewState_setPerformanceStatsEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setPerformanceStatsEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyObject*
PyViewState_cameraAxisEnabled(PyViewStateObject* self)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    return PyBool_FromLong(self->viewState->cameraAxisEnabled());
}

static PyObject*
PyViewState_setCameraAxisEnabled(PyViewStateObject* self, PyObject* args)
{
    if (!checkViewState(self->viewState))
        return nullptr;

    int enabled = 0;
    if (!PyArg_ParseTuple(args, "p", &enabled))
        return nullptr;

    self->viewState->setCameraAxisEnabled(enabled != 0);
    Py_RETURN_NONE;
}

static PyMethodDef PyViewState_methods[]
    = { { "camera", reinterpret_cast<PyCFunction>(PyViewState_camera), METH_NOARGS, "Get the view camera wrapper" },

        { "backgroundColor", reinterpret_cast<PyCFunction>(PyViewState_backgroundColor), METH_NOARGS,
          "Get the viewport background color as (r, g, b, a)" },
        { "setBackgroundColor", reinterpret_cast<PyCFunction>(PyViewState_setBackgroundColor), METH_VARARGS,
          "Set the viewport background color from an RGB or RGBA sequence" },

        { "gridColor", reinterpret_cast<PyCFunction>(PyViewState_gridColor), METH_NOARGS,
          "Get the viewport grid color as (r, g, b, a)" },
        { "setGridColor", reinterpret_cast<PyCFunction>(PyViewState_setGridColor), METH_VARARGS,
          "Set the viewport grid color from an RGB or RGBA sequence" },

        { "gridEnabled", reinterpret_cast<PyCFunction>(PyViewState_gridEnabled), METH_NOARGS,
          "Get the viewport grid display state" },
        { "setGridEnabled", reinterpret_cast<PyCFunction>(PyViewState_setGridEnabled), METH_VARARGS,
          "Set the viewport grid display state" },

        { "materialMode", reinterpret_cast<PyCFunction>(PyViewState_materialMode), METH_NOARGS,
          "Get the viewport material mode" },
        { "setMaterialMode", reinterpret_cast<PyCFunction>(PyViewState_setMaterialMode), METH_VARARGS,
          "Set the viewport material mode" },

        { "overrideMaterial", reinterpret_cast<PyCFunction>(PyViewState_overrideMaterial), METH_NOARGS,
          "Get the viewport override material prim path" },
        { "setOverrideMaterial", reinterpret_cast<PyCFunction>(PyViewState_setOverrideMaterial), METH_VARARGS,
          "Set the viewport override material prim path" },

        { "defaultCameraLightEnabled", reinterpret_cast<PyCFunction>(PyViewState_defaultCameraLightEnabled),
          METH_NOARGS, "Get the default camera light state" },
        { "setDefaultCameraLightEnabled", reinterpret_cast<PyCFunction>(PyViewState_setDefaultCameraLightEnabled),
          METH_VARARGS, "Set the default camera light state" },

        { "sceneLightsEnabled", reinterpret_cast<PyCFunction>(PyViewState_sceneLightsEnabled), METH_NOARGS,
          "Get the scene light state" },
        { "setSceneLightsEnabled", reinterpret_cast<PyCFunction>(PyViewState_setSceneLightsEnabled), METH_VARARGS,
          "Set the scene light state" },

        { "sceneMaterialsEnabled", reinterpret_cast<PyCFunction>(PyViewState_sceneMaterialsEnabled), METH_NOARGS,
          "Get the scene material state" },
        { "setSceneMaterialsEnabled", reinterpret_cast<PyCFunction>(PyViewState_setSceneMaterialsEnabled), METH_VARARGS,
          "Set the scene material state" },

        { "doubleSidedMode", reinterpret_cast<PyCFunction>(PyViewState_doubleSidedMode), METH_NOARGS,
          "Get the viewport double-sided rendering mode: 0=Primitive, 1=DoubleSided, 2=SingleSided" },
        { "setDoubleSidedMode", reinterpret_cast<PyCFunction>(PyViewState_setDoubleSidedMode), METH_VARARGS,
          "Set the viewport double-sided rendering mode: 0=Primitive, 1=DoubleSided, 2=SingleSided" },

        { "renderMode", reinterpret_cast<PyCFunction>(PyViewState_renderMode), METH_NOARGS,
          "Get the viewport render mode" },
        { "setRenderMode", reinterpret_cast<PyCFunction>(PyViewState_setRenderMode), METH_VARARGS,
          "Set the viewport render mode" },

        { "complexityLevel", reinterpret_cast<PyCFunction>(PyViewState_complexityLevel), METH_NOARGS,
          "Get the viewport complexity level" },
        { "setComplexityLevel", reinterpret_cast<PyCFunction>(PyViewState_setComplexityLevel), METH_VARARGS,
          "Set the viewport complexity level" },

        { "rendererAov", reinterpret_cast<PyCFunction>(PyViewState_rendererAov), METH_NOARGS, "Get the renderer AOV" },
        { "setRendererAov", reinterpret_cast<PyCFunction>(PyViewState_setRendererAov), METH_VARARGS,
          "Set the renderer AOV" },

        { "sceneStatsEnabled", reinterpret_cast<PyCFunction>(PyViewState_sceneStatsEnabled), METH_NOARGS,
          "Get the scene statistics display state" },
        { "setSceneStatsEnabled", reinterpret_cast<PyCFunction>(PyViewState_setSceneStatsEnabled), METH_VARARGS,
          "Set the scene statistics display state" },

        { "performanceStatsEnabled", reinterpret_cast<PyCFunction>(PyViewState_performanceStatsEnabled), METH_NOARGS,
          "Get the performance statistics display state" },
        { "setPerformanceStatsEnabled", reinterpret_cast<PyCFunction>(PyViewState_setPerformanceStatsEnabled),
          METH_VARARGS, "Set the performance statistics display state" },

        { "cameraAxisEnabled", reinterpret_cast<PyCFunction>(PyViewState_cameraAxisEnabled), METH_NOARGS,
          "Get the camera axis display state" },
        { "setCameraAxisEnabled", reinterpret_cast<PyCFunction>(PyViewState_setCameraAxisEnabled), METH_VARARGS,
          "Set the camera axis display state" },

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
