// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "pyrender.h"
#include "application.h"
#include "renderview.h"
#include "viewportcapture.h"
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <cmath>
#include <new>

namespace stageviz::python {
namespace {
    struct PyRender {
        PyObject_HEAD QPointer<RenderView>* view;
    };
    struct PyCapture {
        PyObject_HEAD ViewportCapture* value;
    };
    PyTypeObject renderType = { PyVarObject_HEAD_INIT(nullptr, 0) };
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

    void renderDealloc(PyRender* self)
    {
        delete self->view;
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }
    void captureDealloc(PyCapture* self)
    {
        delete self->value;
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }
    RenderView* checkedView(PyRender* self)
    {
        if (!self->view || !*self->view) {
            PyErr_SetString(PyExc_RuntimeError, "The viewport has closed");
            return nullptr;
        }
        RenderView* view = self->view->data();
        if (QThread::currentThread() != view->thread()) {
            PyErr_SetString(PyExc_RuntimeError, "Viewport access requires the application GUI thread");
            return nullptr;
        }
        return view;
    }
    PyObject* renderCapturePaths(PyRender* self, PyObject* args, PyObject* kwargs)
    {
        int accumulate = 1;
        static const char* keys[] = { "accumulate", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p:capturePaths", const_cast<char**>(keys), &accumulate))
            return nullptr;
        RenderView* view = checkedView(self);
        if (!view)
            return nullptr;
        try {
            return pathsToPython(view->capturePaths(accumulate != 0));
        } catch (const std::bad_alloc&) {
            return PyErr_NoMemory();
        }
    }
    PyObject* renderVisibleCapturePaths(PyRender* self, PyObject*)
    {
        RenderView* view = checkedView(self);
        if (!view)
            return nullptr;
        return pathsToPython(view->visibleCapturePaths());
    }
    PyObject* renderClearVisibleCapture(PyRender* self, PyObject*)
    {
        RenderView* view = checkedView(self);
        if (!view)
            return nullptr;
        view->clearVisibleCapture();
        Py_RETURN_NONE;
    }
    PyObject* renderCapture(PyRender* self, PyObject*)
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
    PyObject* renderInspectPrim(PyRender* self, PyObject* args)
    {
        const char* value = nullptr;
        if (!PyArg_ParseTuple(args, "s:inspectPrim", &value))
            return nullptr;
        if (!SdfPath::IsValidPathString(value)) {
            PyErr_SetString(PyExc_ValueError, "Expected an absolute USD geometry prim path");
            return nullptr;
        }
        const SdfPath path(value);
        if (!path.IsAbsolutePath() || !path.IsPrimPath() || path == SdfPath::AbsoluteRootPath()) {
            PyErr_SetString(PyExc_ValueError, "Expected an absolute USD geometry prim path");
            return nullptr;
        }
        if (!self->view || !*self->view) {
            PyErr_SetString(PyExc_RuntimeError, "The viewport has closed");
            return nullptr;
        }
        if (QThread::currentThread() != self->view->data()->thread()) {
            PyErr_SetString(PyExc_RuntimeError, "Viewport diagnostics require the application GUI thread");
            return nullptr;
        }
        QString error;
        QJsonObject report;
        try {
            report = self->view->data()->inspectPrim(path, error);
        } catch (const std::bad_alloc&) {
            return PyErr_NoMemory();
        }
        if (report.isEmpty()) {
            PyErr_SetString(PyExc_RuntimeError, error.toUtf8().constData());
            return nullptr;
        }
        const QByteArray data = QJsonDocument(report).toJson(QJsonDocument::Compact);
        PyObject* json = PyImport_ImportModule("json");
        if (!json)
            return nullptr;
        PyObject* result = PyObject_CallMethod(json, "loads", "s", data.constData());
        Py_DECREF(json);
        return result;
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
    PyMethodDef renderMethods[] = { { "capture", reinterpret_cast<PyCFunction>(renderCapture), METH_NOARGS,
                                      "Capture visible document colors and object paths without changing selection." },
                                    { "capturePaths", reinterpret_cast<PyCFunction>(renderCapturePaths),
                                      METH_VARARGS | METH_KEYWORDS,
                                      "Scan visible paths with the menu's tiled pass; accumulate=True shares its collection." },
                                    { "visibleCapturePaths", reinterpret_cast<PyCFunction>(renderVisibleCapturePaths),
                                      METH_NOARGS, "Return a copy of the menu's accumulated visible path collection." },
                                    { "clearVisibleCapture", reinterpret_cast<PyCFunction>(renderClearVisibleCapture),
                                      METH_NOARGS, "Clear the shared visible path collection without changing selection." },
                                    { "inspectPrim", reinterpret_cast<PyCFunction>(renderInspectPrim), METH_VARARGS,
                                      "Inspect USD visibility and last-synchronized Hydra items and buffer metadata." },
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
initPyRenderTypes()
{
    renderType.tp_name = "stageviz.Render";
    renderType.tp_basicsize = sizeof(PyRender);
    renderType.tp_flags = Py_TPFLAGS_DEFAULT;
    renderType.tp_dealloc = reinterpret_cast<destructor>(renderDealloc);
    renderType.tp_methods = renderMethods;
    renderType.tp_doc = "Viewport capture, diagnostics and visible path collection; use stageviz.render().";
    captureType.tp_name = "stageviz.ViewportCapture";
    captureType.tp_basicsize = sizeof(PyCapture);
    captureType.tp_flags = Py_TPFLAGS_DEFAULT;
    captureType.tp_dealloc = reinterpret_cast<destructor>(captureDealloc);
    captureType.tp_methods = captureMethods;
    captureType.tp_doc = "Owned color and object-ID snapshot; use render.capture().";
    return PyType_Ready(&renderType) >= 0 && PyType_Ready(&captureType) >= 0;
}

int
addPyRenderTypes(PyObject* module)
{
    for (auto pair : { std::make_pair("Render", &renderType), std::make_pair("ViewportCapture", &captureType) }) {
        Py_INCREF(pair.second);
        if (PyModule_AddObject(module, pair.first, reinterpret_cast<PyObject*>(pair.second)) < 0) {
            Py_DECREF(pair.second);
            return -1;
        }
    }
    return 0;
}

PyObject*
createPyRender()
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
    auto* result = reinterpret_cast<PyRender*>(renderType.tp_alloc(&renderType, 0));
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
