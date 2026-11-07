// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#include "pyhydrarenderer.h"
#include "hydrarenderer.h"
#include <exception>
#include <new>

namespace stageviz::python {
namespace {
    PyTypeObject hydraRendererType = { PyVarObject_HEAD_INIT(nullptr, 0) };

    PyObject* availableRenderers(PyObject*, PyObject*)
    {
        try {
            const auto plugins = HydraRenderer::availableRenderers();
            PyObject* result = PyList_New(plugins.size());
            if (!result)
                return nullptr;
            for (qsizetype i = 0; i < plugins.size(); ++i) {
                const auto& plugin = plugins.at(i);
                PyObject* item = Py_BuildValue("{s:s,s:s,s:s,s:i,s:O}", "id", plugin.id.toUtf8().constData(),
                                               "display_name", plugin.displayName.toUtf8().constData(), "library_path",
                                               plugin.libraryPath.toUtf8().constData(), "priority", plugin.priority,
                                               "loaded", plugin.loaded ? Py_True : Py_False);
                if (!item) {
                    Py_DECREF(result);
                    return nullptr;
                }
                PyList_SET_ITEM(result, i, item);
            }
            return result;
        } catch (const std::bad_alloc&) {
            return PyErr_NoMemory();
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_RuntimeError, error.what());
        } catch (...) {
            PyErr_SetString(PyExc_RuntimeError, "Hydra renderer discovery failed");
        }
        return nullptr;
    }

    PyMethodDef methods[] = { { "available_renderers", availableRenderers, METH_NOARGS | METH_STATIC,
                                "Return registered renderer metadata without loading plugin libraries. "
                                "Registration does not guarantee compatibility or runtime availability." },
                              { nullptr, nullptr, 0, nullptr } };
}  // namespace

bool
initPyHydraRendererType()
{
    hydraRendererType.tp_name = "stageviz.HydraRenderer";
    hydraRendererType.tp_basicsize = sizeof(PyObject);
    hydraRendererType.tp_flags = Py_TPFLAGS_DEFAULT;
    hydraRendererType.tp_methods = methods;
    hydraRendererType.tp_doc = "Hydra renderer discovery (phase 1; rendering sessions are not implemented).";
    return PyType_Ready(&hydraRendererType) >= 0;
}

int
addPyHydraRendererType(PyObject* module)
{
    Py_INCREF(&hydraRendererType);
    if (PyModule_AddObject(module, "HydraRenderer", reinterpret_cast<PyObject*>(&hydraRendererType)) < 0) {
        Py_DECREF(&hydraRendererType);
        return -1;
    }
    return 0;
}

}  // namespace stageviz::python
