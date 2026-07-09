// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyviewcamera.h"
#include "pyutils.h"

namespace stageviz::python {

static bool
checkViewCamera(ViewCamera* camera)
{
    if (camera)
        return true;

    PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.ViewCamera");
    return false;
}

static ViewCamera::CameraUp
toCameraUp(long value)
{
    switch (value) {
    case ViewCamera::X: return ViewCamera::X;
    case ViewCamera::Y: return ViewCamera::Y;
    case ViewCamera::Z: return ViewCamera::Z;
    default: return ViewCamera::Y;
    }
}

static ViewCamera::CameraMode
toCameraMode(long value)
{
    switch (value) {
    case ViewCamera::None: return ViewCamera::None;
    case ViewCamera::Truck: return ViewCamera::Truck;
    case ViewCamera::Tumble: return ViewCamera::Tumble;
    case ViewCamera::Zoom: return ViewCamera::Zoom;
    case ViewCamera::Pick: return ViewCamera::Pick;
    default: return ViewCamera::None;
    }
}

static ViewCamera::FovDirection
toFovDirection(long value)
{
    switch (value) {
    case ViewCamera::Vertical: return ViewCamera::Vertical;
    case ViewCamera::Horizontal: return ViewCamera::Horizontal;
    default: return ViewCamera::Vertical;
    }
}

static void
PyGfCameraCapsule_destructor(PyObject* capsule)
{
    auto* camera = static_cast<GfCamera*>(PyCapsule_GetPointer(capsule, "stageviz.GfCamera"));
    delete camera;
}

static void
PyViewCamera_dealloc(PyViewCameraObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject*
PyViewCamera_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PyViewCameraObject* self = reinterpret_cast<PyViewCameraObject*>(type->tp_alloc(type, 0));
    if (self)
        self->camera = nullptr;
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PyViewCamera_isIdentity(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyBool_FromLong(self->camera->isIdentity());
}

static PyObject*
PyViewCamera_frameAll(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    self->camera->frameAll();
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_resetView(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    self->camera->resetView();
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_tumble(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double x = 0.0;
    double y = 0.0;

    if (!PyArg_ParseTuple(args, "dd", &x, &y))
        return nullptr;

    self->camera->tumble(x, y);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_truck(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double right = 0.0;
    double up = 0.0;

    if (!PyArg_ParseTuple(args, "dd", &right, &up))
        return nullptr;

    self->camera->truck(right, up);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_distance(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double factor = 0.0;

    if (!PyArg_ParseTuple(args, "d", &factor))
        return nullptr;

    self->camera->distance(factor);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_mapToFrustumHeight(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    int height = 0;

    if (!PyArg_ParseTuple(args, "i", &height))
        return nullptr;

    return PyFloat_FromDouble(self->camera->mapToFrustumHeight(height));
}

static PyObject*
PyViewCamera_camera(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    auto* camera = new GfCamera(self->camera->camera());
    return PyCapsule_New(camera, "stageviz.GfCamera", PyGfCameraCapsule_destructor);
}

static PyObject*
PyViewCamera_aspectRatio(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->aspectRatio());
}

static PyObject*
PyViewCamera_setAspectRatio(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setAspectRatio(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_fov(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->fov());
}

static PyObject*
PyViewCamera_setFov(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setFov(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_fovDirection(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->camera->fovDirection()));
}

static PyObject*
PyViewCamera_setFovDirection(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    long value = 0;

    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->camera->setFovDirection(toFovDirection(value));
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_focusPoint(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return vec3dToPyTuple(self->camera->focusPoint());
}

static PyObject*
PyViewCamera_setFocusPoint(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    if (!PyArg_ParseTuple(args, "ddd", &x, &y, &z))
        return nullptr;

    self->camera->setFocusPoint(GfVec3d(x, y, z));
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_boundingBox(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return bboxToPyTuple(self->camera->boundingBox());
}

static PyObject*
PyViewCamera_setBoundingBox(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    PyObject* object = nullptr;

    if (!PyArg_ParseTuple(args, "O", &object))
        return nullptr;

    GfBBox3d bbox;
    if (!pyToBBox(object, &bbox))
        return nullptr;

    self->camera->setBoundingBox(bbox);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_fit(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->fit());
}

static PyObject*
PyViewCamera_setFit(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setFit(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_cameraUp(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->camera->cameraUp()));
}

static PyObject*
PyViewCamera_setCameraUp(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    long value = 0;

    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->camera->setCameraUp(toCameraUp(value));
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_axisYaw(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->axisYaw());
}

static PyObject*
PyViewCamera_setAxisYaw(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setAxisYaw(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_axisPitch(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->axisPitch());
}

static PyObject*
PyViewCamera_setAxisPitch(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setAxisPitch(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_axisRoll(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->axisRoll());
}

static PyObject*
PyViewCamera_setAxisRoll(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setAxisRoll(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_cameraMode(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->camera->cameraMode()));
}

static PyObject*
PyViewCamera_setCameraMode(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    long value = 0;

    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->camera->setCameraMode(toCameraMode(value));
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_nearClipping(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->nearClipping());
}

static PyObject*
PyViewCamera_setNearClipping(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setNearClipping(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_farClipping(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->farClipping());
}

static PyObject*
PyViewCamera_setFarClipping(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setFarClipping(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_cameraDistance(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    return PyFloat_FromDouble(self->camera->cameraDistance());
}

static PyObject*
PyViewCamera_setCameraDistance(PyViewCameraObject* self, PyObject* args)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    double value = 0.0;

    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;

    self->camera->setCameraDistance(value);
    Py_RETURN_NONE;
}

static PyObject*
PyViewCamera_reset(PyViewCameraObject* self)
{
    if (!checkViewCamera(self->camera))
        return nullptr;

    self->camera->reset();
    Py_RETURN_NONE;
}

static PyMethodDef PyViewCamera_methods[] = {
    { "isIdentity", reinterpret_cast<PyCFunction>(PyViewCamera_isIdentity), METH_NOARGS,
      "Return whether the camera is in its identity state" },

    { "frameAll", reinterpret_cast<PyCFunction>(PyViewCamera_frameAll), METH_NOARGS,
      "Frame the whole scene bounding box" },
    { "resetView", reinterpret_cast<PyCFunction>(PyViewCamera_resetView), METH_NOARGS,
      "Reset the camera view orientation" },
    { "tumble", reinterpret_cast<PyCFunction>(PyViewCamera_tumble), METH_VARARGS,
      "Rotate the camera around the focus point" },
    { "truck", reinterpret_cast<PyCFunction>(PyViewCamera_truck), METH_VARARGS,
      "Translate the camera parallel to the view plane" },
    { "distance", reinterpret_cast<PyCFunction>(PyViewCamera_distance), METH_VARARGS,
      "Adjust camera distance by a zoom factor" },
    { "mapToFrustumHeight", reinterpret_cast<PyCFunction>(PyViewCamera_mapToFrustumHeight), METH_VARARGS,
      "Map a pixel height to frustum height" },

    { "camera", reinterpret_cast<PyCFunction>(PyViewCamera_camera), METH_NOARGS,
      "Get a capsule containing a copy of the GfCamera" },

    { "aspectRatio", reinterpret_cast<PyCFunction>(PyViewCamera_aspectRatio), METH_NOARGS,
      "Get the aspect ratio" },
    { "setAspectRatio", reinterpret_cast<PyCFunction>(PyViewCamera_setAspectRatio), METH_VARARGS,
      "Set the aspect ratio" },
    { "fov", reinterpret_cast<PyCFunction>(PyViewCamera_fov), METH_NOARGS,
      "Get the field of view" },
    { "setFov", reinterpret_cast<PyCFunction>(PyViewCamera_setFov), METH_VARARGS,
      "Set the field of view" },
    { "fovDirection", reinterpret_cast<PyCFunction>(PyViewCamera_fovDirection), METH_NOARGS,
      "Get the field-of-view direction" },
    { "setFovDirection", reinterpret_cast<PyCFunction>(PyViewCamera_setFovDirection), METH_VARARGS,
      "Set the field-of-view direction" },

    { "focusPoint", reinterpret_cast<PyCFunction>(PyViewCamera_focusPoint), METH_NOARGS,
      "Get the focus point as a tuple" },
    { "setFocusPoint", reinterpret_cast<PyCFunction>(PyViewCamera_setFocusPoint), METH_VARARGS,
      "Set the focus point from x, y, z" },
    { "boundingBox", reinterpret_cast<PyCFunction>(PyViewCamera_boundingBox), METH_NOARGS,
      "Get the bounding box as ((min), (max))" },
    { "setBoundingBox", reinterpret_cast<PyCFunction>(PyViewCamera_setBoundingBox), METH_VARARGS,
      "Set the bounding box from ((min), (max))" },
    { "fit", reinterpret_cast<PyCFunction>(PyViewCamera_fit), METH_NOARGS,
      "Get the framing fit multiplier" },
    { "setFit", reinterpret_cast<PyCFunction>(PyViewCamera_setFit), METH_VARARGS,
      "Set the framing fit multiplier" },

    { "cameraUp", reinterpret_cast<PyCFunction>(PyViewCamera_cameraUp), METH_NOARGS,
      "Get the camera up axis" },
    { "setCameraUp", reinterpret_cast<PyCFunction>(PyViewCamera_setCameraUp), METH_VARARGS,
      "Set the camera up axis" },
    { "axisYaw", reinterpret_cast<PyCFunction>(PyViewCamera_axisYaw), METH_NOARGS,
      "Get the camera yaw angle" },
    { "setAxisYaw", reinterpret_cast<PyCFunction>(PyViewCamera_setAxisYaw), METH_VARARGS,
      "Set the camera yaw angle" },
    { "axisPitch", reinterpret_cast<PyCFunction>(PyViewCamera_axisPitch), METH_NOARGS,
      "Get the camera pitch angle" },
    { "setAxisPitch", reinterpret_cast<PyCFunction>(PyViewCamera_setAxisPitch), METH_VARARGS,
      "Set the camera pitch angle" },
    { "axisRoll", reinterpret_cast<PyCFunction>(PyViewCamera_axisRoll), METH_NOARGS,
      "Get the camera roll angle" },
    { "setAxisRoll", reinterpret_cast<PyCFunction>(PyViewCamera_setAxisRoll), METH_VARARGS,
      "Set the camera roll angle" },

    { "cameraMode", reinterpret_cast<PyCFunction>(PyViewCamera_cameraMode), METH_NOARGS,
      "Get the camera interaction mode" },
    { "setCameraMode", reinterpret_cast<PyCFunction>(PyViewCamera_setCameraMode), METH_VARARGS,
      "Set the camera interaction mode" },

    { "nearClipping", reinterpret_cast<PyCFunction>(PyViewCamera_nearClipping), METH_NOARGS,
      "Get the near clipping plane" },
    { "setNearClipping", reinterpret_cast<PyCFunction>(PyViewCamera_setNearClipping), METH_VARARGS,
      "Set the near clipping plane" },
    { "farClipping", reinterpret_cast<PyCFunction>(PyViewCamera_farClipping), METH_NOARGS,
      "Get the far clipping plane" },
    { "setFarClipping", reinterpret_cast<PyCFunction>(PyViewCamera_setFarClipping), METH_VARARGS,
      "Set the far clipping plane" },

    { "cameraDistance", reinterpret_cast<PyCFunction>(PyViewCamera_cameraDistance), METH_NOARGS,
      "Get the camera distance" },
    { "setCameraDistance", reinterpret_cast<PyCFunction>(PyViewCamera_setCameraDistance), METH_VARARGS,
      "Set the camera distance" },

    { "reset", reinterpret_cast<PyCFunction>(PyViewCamera_reset), METH_NOARGS,
      "Reset the camera to default state" },

    { nullptr }
};

PyTypeObject PyViewCameraType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPyViewCameraType()
{
    PyViewCameraType.tp_name = "stageviz.ViewCamera";
    PyViewCameraType.tp_basicsize = sizeof(PyViewCameraObject);
    PyViewCameraType.tp_itemsize = 0;
    PyViewCameraType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyViewCameraType.tp_new = PyViewCamera_new;
    PyViewCameraType.tp_dealloc = reinterpret_cast<destructor>(PyViewCamera_dealloc);
    PyViewCameraType.tp_methods = PyViewCamera_methods;

    return PyType_Ready(&PyViewCameraType) >= 0;
}

int
addPyViewCameraType(PyObject* module)
{
    Py_INCREF(&PyViewCameraType);
    if (PyModule_AddObject(module, "ViewCamera", reinterpret_cast<PyObject*>(&PyViewCameraType)) < 0) {
        Py_DECREF(&PyViewCameraType);
        return -1;
    }

    return 0;
}

PyObject*
createPyViewCamera(ViewCamera* camera)
{
    if (!camera)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PyViewCameraType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PyViewCameraObject*>(object)->camera = camera;
    return object;
}

}  // namespace stageviz::python
