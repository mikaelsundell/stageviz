// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyrenderengine.h"

#include <QColor>
#include <QFileInfo>
#include <QString>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace stageviz::python {
namespace {

    bool checkRenderer(RenderEngine* renderer)
    {
        if (renderer)
            return true;

        PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.RenderEngine");
        return false;
    }

    QString pyString(PyObject* object)
    {
        if (!object || !PyUnicode_Check(object))
            return {};

        const char* text = PyUnicode_AsUTF8(object);
        if (!text)
            return {};

        return QString::fromUtf8(text);
    }

    bool callNoArgs(PyObject* object, const char* methodName, PyObject** result)
    {
        if (!object || !result)
            return false;

        PyObject* method = PyObject_GetAttrString(object, methodName);
        if (!method)
            return false;

        if (!PyCallable_Check(method)) {
            Py_DECREF(method);
            PyErr_Format(PyExc_TypeError, "%s is not callable", methodName);
            return false;
        }

        *result = PyObject_CallObject(method, nullptr);
        Py_DECREF(method);
        return *result != nullptr;
    }

    bool usdStageFromPython(PyObject* object, UsdStageRefPtr* stage)
    {
        if (!stage) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid output USD stage pointer");
            return false;
        }

        *stage = nullptr;

        if (!object || object == Py_None) {
            PyErr_SetString(PyExc_TypeError, "stage must be a pxr.Usd.Stage");
            return false;
        }

        // Avoid depending on Boost.Python internals here. The pxr.Usd.Stage owns a
        // root SdfLayer that is registered in the same process. Read its identifier
        // through Python, find the native SdfLayer, then open a native UsdStage that
        // shares the same root layer. This also works for anonymous in-memory layers.
        PyObject* rootLayerObject = nullptr;
        if (!callNoArgs(object, "GetRootLayer", &rootLayerObject)) {
            PyErr_SetString(PyExc_TypeError, "stage must provide GetRootLayer()");
            return false;
        }

        PyObject* identifierObject = PyObject_GetAttrString(rootLayerObject, "identifier");
        Py_DECREF(rootLayerObject);
        if (!identifierObject) {
            PyErr_SetString(PyExc_TypeError, "stage root layer has no identifier");
            return false;
        }

        const QString identifier = pyString(identifierObject);
        Py_DECREF(identifierObject);
        if (identifier.isEmpty()) {
            PyErr_SetString(PyExc_ValueError, "stage root layer identifier is empty");
            return false;
        }

        SdfLayerHandle rootLayer = SdfLayer::Find(identifier.toStdString());
        if (!rootLayer)
            rootLayer = SdfLayer::FindOrOpen(identifier.toStdString());

        if (!rootLayer) {
            PyErr_Format(PyExc_RuntimeError, "Could not resolve USD root layer '%s'", identifier.toUtf8().constData());
            return false;
        }

        *stage = UsdStage::Open(rootLayer);
        if (!*stage) {
            PyErr_Format(PyExc_RuntimeError, "Could not open USD stage '%s'", identifier.toUtf8().constData());
            return false;
        }

        return true;
    }

    bool cameraFromPath(const UsdStageRefPtr& stage, const QString& pathText, GfCamera* camera)
    {
        if (!camera) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid output camera pointer");
            return false;
        }

        if (!stage) {
            PyErr_SetString(PyExc_RuntimeError, "Set a USD stage before setting the camera");
            return false;
        }

        const SdfPath path(pathText.toStdString());
        if (!path.IsAbsolutePath() || !path.IsPrimPath()) {
            PyErr_SetString(PyExc_ValueError, "camera path must be an absolute USD prim path");
            return false;
        }

        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim) {
            PyErr_Format(PyExc_ValueError, "Camera prim '%s' does not exist", pathText.toUtf8().constData());
            return false;
        }

        const UsdGeomCamera usdCamera(prim);
        if (!usdCamera) {
            PyErr_Format(PyExc_TypeError, "Prim '%s' is not a UsdGeomCamera", pathText.toUtf8().constData());
            return false;
        }

        *camera = usdCamera.GetCamera(UsdTimeCode::Default());
        return true;
    }

    void PyRenderEngine_dealloc(PyRenderEngineObject* self)
    {
        delete self->renderer;
        self->renderer = nullptr;
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }

    PyObject* PyRenderEngine_new(PyTypeObject* type, PyObject*, PyObject*)
    {
        PyRenderEngineObject* self = reinterpret_cast<PyRenderEngineObject*>(type->tp_alloc(type, 0));
        if (!self)
            return nullptr;

        self->renderer = nullptr;

        try {
            self->renderer = new RenderEngine(RenderEngine::ContextMode::Offscreen);
        } catch (const std::exception& e) {
            Py_DECREF(reinterpret_cast<PyObject*>(self));
            PyErr_SetString(PyExc_RuntimeError, e.what());
            return nullptr;
        } catch (...) {
            Py_DECREF(reinterpret_cast<PyObject*>(self));
            PyErr_SetString(PyExc_RuntimeError, "Could not create stageviz.RenderEngine");
            return nullptr;
        }

        return reinterpret_cast<PyObject*>(self);
    }

    PyObject* PyRenderEngine_setStage(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        PyObject* pyStage = nullptr;
        if (!PyArg_ParseTuple(args, "O", &pyStage))
            return nullptr;

        UsdStageRefPtr stage;
        if (!usdStageFromPython(pyStage, &stage))
            return nullptr;

        self->renderer->setStage(stage);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setSize(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        int width = 0;
        int height = 0;
        if (!PyArg_ParseTuple(args, "ii", &width, &height))
            return nullptr;

        if (width <= 0 || height <= 0) {
            PyErr_SetString(PyExc_ValueError, "width and height must be greater than zero");
            return nullptr;
        }

        self->renderer->setSize(GfVec2i(width, height));
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setCamera(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        const char* pathText = nullptr;
        if (!PyArg_ParseTuple(args, "s", &pathText))
            return nullptr;

        GfCamera camera;
        if (!cameraFromPath(self->renderer->stage(), QString::fromUtf8(pathText), &camera))
            return nullptr;

        self->renderer->setCamera(camera);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setBackground(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        double alpha = 1.0;
        if (!PyArg_ParseTuple(args, "ddd|d", &red, &green, &blue, &alpha))
            return nullptr;

        auto valid = [](double value) { return value >= 0.0 && value <= 1.0; };
        if (!valid(red) || !valid(green) || !valid(blue) || !valid(alpha)) {
            PyErr_SetString(PyExc_ValueError, "background components must be in the range 0.0 to 1.0");
            return nullptr;
        }

        RenderEngine::Settings settings = self->renderer->settings();
        settings.clearColor = QColor::fromRgbF(red, green, blue, alpha);
        self->renderer->setSettings(settings);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setSceneLightsEnabled(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        int enabled = 0;
        if (!PyArg_ParseTuple(args, "p", &enabled))
            return nullptr;

        RenderEngine::Settings settings = self->renderer->settings();
        settings.sceneLightsEnabled = enabled != 0;
        self->renderer->setSettings(settings);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setDefaultCameraLightEnabled(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        int enabled = 0;
        if (!PyArg_ParseTuple(args, "p", &enabled))
            return nullptr;

        RenderEngine::Settings settings = self->renderer->settings();
        settings.defaultCameraLightEnabled = enabled != 0;
        self->renderer->setSettings(settings);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setSceneMaterialsEnabled(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        int enabled = 0;
        if (!PyArg_ParseTuple(args, "p", &enabled))
            return nullptr;

        RenderEngine::Settings settings = self->renderer->settings();
        settings.sceneMaterialsEnabled = enabled != 0;
        self->renderer->setSettings(settings);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_setAov(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "s", &name))
            return nullptr;

        if (!name || !*name) {
            PyErr_SetString(PyExc_ValueError, "AOV name cannot be empty");
            return nullptr;
        }

        RenderEngine::Settings settings = self->renderer->settings();
        settings.aov = TfToken(name);
        self->renderer->setSettings(settings);
        Py_RETURN_NONE;
    }

    PyObject* PyRenderEngine_render(PyRenderEngineObject* self, PyObject* args)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        const char* filename = nullptr;
        if (!PyArg_ParseTuple(args, "s", &filename))
            return nullptr;

        if (!self->renderer->stage()) {
            PyErr_SetString(PyExc_RuntimeError, "Set a USD stage before rendering");
            return nullptr;
        }

        const QString outputPath = QFileInfo(QString::fromUtf8(filename)).absoluteFilePath();
        const QImage image = self->renderer->renderImage();
        if (image.isNull()) {
            PyErr_SetString(PyExc_RuntimeError, "Render failed");
            return nullptr;
        }

        if (!image.save(outputPath)) {
            PyErr_Format(PyExc_RuntimeError, "Could not save rendered image '%s'", outputPath.toUtf8().constData());
            return nullptr;
        }

        return PyUnicode_FromString(outputPath.toUtf8().constData());
    }

    PyObject* PyRenderEngine_aovs(PyRenderEngineObject* self, PyObject*)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        if (!self->renderer->isInitialized() && !self->renderer->initialize()) {
            PyErr_SetString(PyExc_RuntimeError, "Could not initialize render engine");
            return nullptr;
        }

        const QList<QString> aovs = self->renderer->rendererAovs();
        PyObject* result = PyList_New(aovs.size());
        if (!result)
            return nullptr;

        for (qsizetype i = 0; i < aovs.size(); ++i) {
            PyObject* value = PyUnicode_FromString(aovs.at(i).toUtf8().constData());
            if (!value) {
                Py_DECREF(result);
                return nullptr;
            }
            PyList_SET_ITEM(result, i, value);
        }

        return result;
    }

    PyObject* PyRenderEngine_hgiApi(PyRenderEngineObject* self, PyObject*)
    {
        if (!checkRenderer(self->renderer))
            return nullptr;

        if (!self->renderer->isInitialized() && !self->renderer->initialize()) {
            PyErr_SetString(PyExc_RuntimeError, "Could not initialize render engine");
            return nullptr;
        }

        return PyUnicode_FromString(self->renderer->hgiApiName().toUtf8().constData());
    }

    PyMethodDef PyRenderEngine_methods[]
        = { { "set_stage", reinterpret_cast<PyCFunction>(PyRenderEngine_setStage), METH_VARARGS,
              "Set the renderer stage from a pxr.Usd.Stage." },
            { "set_size", reinterpret_cast<PyCFunction>(PyRenderEngine_setSize), METH_VARARGS,
              "Set the render resolution in pixels." },
            { "set_camera", reinterpret_cast<PyCFunction>(PyRenderEngine_setCamera), METH_VARARGS,
              "Set the rendering camera from an absolute UsdGeomCamera prim path." },
            { "set_background", reinterpret_cast<PyCFunction>(PyRenderEngine_setBackground), METH_VARARGS,
              "Set background RGBA components in the range 0.0 to 1.0." },
            { "set_scene_lights_enabled", reinterpret_cast<PyCFunction>(PyRenderEngine_setSceneLightsEnabled),
              METH_VARARGS, "Enable or disable lights authored on the USD stage." },
            { "set_default_camera_light_enabled",
              reinterpret_cast<PyCFunction>(PyRenderEngine_setDefaultCameraLightEnabled), METH_VARARGS,
              "Enable or disable the Stageviz camera light." },
            { "set_scene_materials_enabled", reinterpret_cast<PyCFunction>(PyRenderEngine_setSceneMaterialsEnabled),
              METH_VARARGS, "Enable or disable authored scene material bindings." },
            { "set_aov", reinterpret_cast<PyCFunction>(PyRenderEngine_setAov), METH_VARARGS,
              "Set the renderer AOV by name." },
            { "render", reinterpret_cast<PyCFunction>(PyRenderEngine_render), METH_VARARGS,
              "Render the current stage offscreen and save it to an image file. Returns the absolute output path." },
            { "aovs", reinterpret_cast<PyCFunction>(PyRenderEngine_aovs), METH_NOARGS,
              "Return AOV names supported by the active renderer." },
            { "hgi_api", reinterpret_cast<PyCFunction>(PyRenderEngine_hgiApi), METH_NOARGS,
              "Return the active Hgi API name." },
            { nullptr, nullptr, 0, nullptr } };

}  // namespace

PyTypeObject PyRenderEngineType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPyRenderEngineType()
{
    PyRenderEngineType.tp_name = "stageviz.RenderEngine";
    PyRenderEngineType.tp_basicsize = sizeof(PyRenderEngineObject);
    PyRenderEngineType.tp_itemsize = 0;
    PyRenderEngineType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyRenderEngineType.tp_new = PyRenderEngine_new;
    PyRenderEngineType.tp_dealloc = reinterpret_cast<destructor>(PyRenderEngine_dealloc);
    PyRenderEngineType.tp_methods = PyRenderEngine_methods;
    PyRenderEngineType.tp_doc = "Stageviz offscreen Hydra render engine";

    return PyType_Ready(&PyRenderEngineType) >= 0;
}

int
addPyRenderEngineType(PyObject* module)
{
    Py_INCREF(&PyRenderEngineType);
    if (PyModule_AddObject(module, "RenderEngine", reinterpret_cast<PyObject*>(&PyRenderEngineType)) < 0) {
        Py_DECREF(&PyRenderEngineType);
        return -1;
    }

    return 0;
}

}  // namespace stageviz::python
