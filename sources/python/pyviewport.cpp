// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "pyviewport.h"
#include "application.h"
#include "renderview.h"
#include "viewportcapture.h"
#include <QPointer>
#include <QThread>
#include <cmath>
#include <new>

namespace stageviz::python {
namespace {
    struct PyViewport {
        PyObject_HEAD QPointer<RenderView>* view;
    };
    struct PyCapture {
        PyObject_HEAD ViewportCapture* value;
    };
    PyTypeObject viewportType = { PyVarObject_HEAD_INIT(nullptr, 0) };
    PyTypeObject captureType = { PyVarObject_HEAD_INIT(nullptr, 0) };

    PyObject* pathsToPython(const QList<SdfPath>& paths)
    {
        PyObject* result = PyList_New(paths.size());
        if (!result)
            return nullptr;
        for (qsizetype i = 0; i < paths.size(); ++i) {
            PyObject* path = PyUnicode_FromString(paths.at(i).GetText());
            if (!path) {
                Py_DECREF(result);
                return nullptr;
            }
            PyList_SET_ITEM(result, i, path);
        }
        return result;
    }

    void viewportDealloc(PyViewport* self)
    {
        delete self->view;
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }
    void captureDealloc(PyCapture* self)
    {
        delete self->value;
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }
    PyObject* viewportCapture(PyViewport* self, PyObject*)
    {
        if (!self->view || !*self->view) {
            PyErr_SetString(PyExc_RuntimeError, "The viewport has closed");
            return nullptr;
        }
        QString error;
        ViewportCapture value;
        try {
            value = self->view->data()->captureViewport(error);
        } catch (const std::bad_alloc&) {
            return PyErr_NoMemory();
        }
        if (value.image.isNull()) {
            PyErr_SetString(PyExc_RuntimeError, error.toUtf8().constData());
            return nullptr;
        }
        auto* result = reinterpret_cast<PyCapture*>(captureType.tp_alloc(&captureType, 0));
        if (!result)
            return nullptr;
        result->value = new (std::nothrow) ViewportCapture(std::move(value));
        if (!result->value) {
            Py_DECREF(result);
            return PyErr_NoMemory();
        }
        return reinterpret_cast<PyObject*>(result);
    }
    PyObject* captureSize(PyCapture* self, PyObject*)
    {
        return Py_BuildValue("(ii)", self->value->image.width(), self->value->image.height());
    }
    PyObject* capturePaths(PyCapture* self, PyObject*) { return pathsToPython(self->value->objects.paths); }
    PyObject* capturePrims(PyCapture* self, PyObject*)
    {
        const auto counts = self->value->pixelCounts();
        const auto& paths = self->value->objects.paths;
        PyObject* result = PyList_New(paths.size());
        if (!result)
            return nullptr;
        for (qsizetype i = 0; i < paths.size(); ++i) {
            PyObject* row = Py_BuildValue("{s:s,s:n}", "path", paths.at(i).GetText(), "visiblePixels",
                                          Py_ssize_t(counts[size_t(i)]));
            if (!row) {
                Py_DECREF(result);
                return nullptr;
            }
            PyList_SET_ITEM(result, i, row);
        }
        return result;
    }
    PyObject* captureRgba(PyCapture* self, PyObject*)
    {
        const QImage image = self->value->image.convertToFormat(QImage::Format_RGBA8888);
        return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
    }
    PyObject* captureIds(PyCapture* self, PyObject*)
    {
        const auto& ids = self->value->objects.indices;
        return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(ids.data()),
                                         Py_ssize_t(ids.size() * sizeof(int32_t)));
    }
    PyObject* captureSave(PyCapture* self, PyObject* args)
    {
        const char* path = nullptr;
        if (!PyArg_ParseTuple(args, "s:save", &path))
            return nullptr;
        if (!self->value->image.save(QString::fromUtf8(path))) {
            PyErr_Format(PyExc_OSError, "Could not save viewport capture to '%s'", path);
            return nullptr;
        }
        Py_RETURN_NONE;
    }
    PyObject* capturePathAt(PyCapture* self, PyObject* args)
    {
        int x = 0, y = 0;
        if (!PyArg_ParseTuple(args, "ii:pathAt", &x, &y))
            return nullptr;
        const auto& image = self->value->image;
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
            PyErr_SetString(PyExc_IndexError, "Pixel is outside the viewport capture");
            return nullptr;
        }
        const int32_t id = self->value->objects.indices[size_t(y) * image.width() + x];
        if (id < 0)
            Py_RETURN_NONE;
        return PyUnicode_FromString(self->value->objects.paths.at(id).GetText());
    }
    bool checkCoverage(Py_ssize_t pixels, double coverage)
    {
        if (pixels < 1 || !std::isfinite(coverage) || coverage < 0 || coverage > 1) {
            PyErr_SetString(PyExc_ValueError, "minPixels must be positive and minCoverage must be in 0..1");
            return false;
        }
        return true;
    }
    PyObject* captureByColor(PyCapture* self, PyObject* args, PyObject* kwargs)
    {
        int r = 0, g = 0, b = 0;
        double tolerance = 30, coverage = 0;
        Py_ssize_t pixels = 8;
        static const char* keys[] = { "color", "tolerance", "minPixels", "minCoverage", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "(iii)|dnd:pathsByColor", const_cast<char**>(keys), &r, &g, &b,
                                         &tolerance, &pixels, &coverage))
            return nullptr;
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || !std::isfinite(tolerance) || tolerance < 0
            || tolerance > 255) {
            PyErr_SetString(PyExc_ValueError, "RGB channels and tolerance must be in 0..255");
            return nullptr;
        }
        if (!checkCoverage(pixels, coverage))
            return nullptr;
        return pathsToPython(self->value->pathsByColor(QColor(r, g, b), tolerance, size_t(pixels), coverage));
    }
    PyObject* captureByHue(PyCapture* self, PyObject* args, PyObject* kwargs)
    {
        double hue = 0, tolerance = 15, saturation = 0.35, value = 0.05, coverage = 0;
        Py_ssize_t pixels = 8;
        static const char* keys[] = { "hue",       "tolerance",   "minSaturation", "minValue",
                                      "minPixels", "minCoverage", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "d|dddnd:pathsByHue", const_cast<char**>(keys), &hue, &tolerance,
                                         &saturation, &value, &pixels, &coverage))
            return nullptr;
        if (!std::isfinite(hue) || !std::isfinite(tolerance) || tolerance < 0 || tolerance > 180
            || !std::isfinite(saturation) || saturation < 0 || saturation > 1 || !std::isfinite(value) || value < 0
            || value > 1) {
            PyErr_SetString(PyExc_ValueError, "Hue must be finite, tolerance in 0..180, saturation/value in 0..1");
            return nullptr;
        }
        if (!checkCoverage(pixels, coverage))
            return nullptr;
        hue = std::fmod(hue, 360.0);
        if (hue < 0)
            hue += 360;
        return pathsToPython(self->value->pathsByHue(hue, tolerance, saturation, value, size_t(pixels), coverage));
    }
    PyMethodDef viewportMethods[] = { { "capture", reinterpret_cast<PyCFunction>(viewportCapture), METH_NOARGS,
                                        "Capture visible document colors and object paths without changing selection." },
                                      { nullptr, nullptr, 0, nullptr } };
    PyMethodDef captureMethods[] = {
        { "size", reinterpret_cast<PyCFunction>(captureSize), METH_NOARGS, "Return (width, height) in device pixels." },
        { "paths", reinterpret_cast<PyCFunction>(capturePaths), METH_NOARGS, "Return unique visible USD object paths." },
        { "prims", reinterpret_cast<PyCFunction>(capturePrims), METH_NOARGS, "Return path and visiblePixels records." },
        { "rgba", reinterpret_cast<PyCFunction>(captureRgba), METH_NOARGS, "Return top-left row-major RGBA8 bytes." },
        { "objectIds", reinterpret_cast<PyCFunction>(captureIds), METH_NOARGS,
          "Return native-endian int32 path indices; -1 is background." },
        { "pathAt", reinterpret_cast<PyCFunction>(capturePathAt), METH_VARARGS,
          "Return the USD path at a device pixel, or None." },
        { "save", reinterpret_cast<PyCFunction>(captureSave), METH_VARARGS, "Save the captured color image." },
        { "pathsByColor", reinterpret_cast<PyCFunction>(captureByColor), METH_VARARGS | METH_KEYWORDS,
          "Find objects by displayed RGB8 color, channel tolerance, pixel count and coverage." },
        { "pathsByHue", reinterpret_cast<PyCFunction>(captureByHue), METH_VARARGS | METH_KEYWORDS,
          "Find objects by hue in degrees, saturation, value, pixel count and coverage." },
        { nullptr, nullptr, 0, nullptr }
    };
}  // namespace

bool
initPyViewportTypes()
{
    viewportType.tp_name = "stageviz.Viewport";
    viewportType.tp_basicsize = sizeof(PyViewport);
    viewportType.tp_flags = Py_TPFLAGS_DEFAULT;
    viewportType.tp_dealloc = reinterpret_cast<destructor>(viewportDealloc);
    viewportType.tp_methods = viewportMethods;
    viewportType.tp_doc = "Weak access to the active viewport; use stageviz.viewport().";
    captureType.tp_name = "stageviz.ViewportCapture";
    captureType.tp_basicsize = sizeof(PyCapture);
    captureType.tp_flags = Py_TPFLAGS_DEFAULT;
    captureType.tp_dealloc = reinterpret_cast<destructor>(captureDealloc);
    captureType.tp_methods = captureMethods;
    captureType.tp_doc = "Owned color and object-ID snapshot; use viewport.capture().";
    return PyType_Ready(&viewportType) >= 0 && PyType_Ready(&captureType) >= 0;
}

int
addPyViewportTypes(PyObject* module)
{
    for (auto pair : { std::make_pair("Viewport", &viewportType), std::make_pair("ViewportCapture", &captureType) }) {
        Py_INCREF(pair.second);
        if (PyModule_AddObject(module, pair.first, reinterpret_cast<PyObject*>(pair.second)) < 0) {
            Py_DECREF(pair.second);
            return -1;
        }
    }
    return 0;
}

PyObject*
createPyViewport()
{
    Application* app = Application::instance();
    if (!app || QThread::currentThread() != app->thread()) {
        PyErr_SetString(PyExc_RuntimeError, "Viewport access requires the application GUI thread");
        return nullptr;
    }
    QWidget* window = app->window();
    RenderView* view = window ? window->findChild<RenderView*>() : nullptr;
    if (!view) {
        PyErr_SetString(PyExc_RuntimeError, "No active Stageviz viewport is available");
        return nullptr;
    }
    auto* result = reinterpret_cast<PyViewport*>(viewportType.tp_alloc(&viewportType, 0));
    if (!result)
        return nullptr;
    result->view = new (std::nothrow) QPointer<RenderView>(view);
    if (!result->view) {
        Py_DECREF(result);
        return PyErr_NoMemory();
    }
    return reinterpret_cast<PyObject*>(result);
}
}  // namespace stageviz::python
