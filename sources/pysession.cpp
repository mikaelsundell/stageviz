// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pysession.h"
#include "pyselectionlist.h"
#include "pyutils.h"
#include <QReadWriteLock>

namespace stageviz::python {

static void
PySession_dealloc(PySessionObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject*
PySession_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PySessionObject* self = reinterpret_cast<PySessionObject*>(type->tp_alloc(type, 0));
    if (self)
        self->session = currentSession();
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PySession_beginProgressBlock(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* name = nullptr;
    Py_ssize_t count = 0;

    static const char* keywords[] = { "name", "count", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|n", const_cast<char**>(keywords), &name, &count))
        return nullptr;

    self->session->beginProgressBlock(QString::fromUtf8(name), static_cast<size_t>(count));
    Py_RETURN_NONE;
}

static PyObject*
PySession_updateProgressNotify(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* message = nullptr;
    Py_ssize_t completed = 0;
    PyObject* pyPaths = Py_None;
    long status = 0;
    PyObject* pyDetails = Py_None;

    static const char* keywords[] = { "message", "completed", "paths", "status", "details", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sn|OlO", const_cast<char**>(keywords), &message, &completed,
                                     &pyPaths, &status, &pyDetails)) {
        return nullptr;
    }

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    QVariantMap details;
    if (pyDetails && pyDetails != Py_None) {
        details = pyToVariantMap(pyDetails);
        if (PyErr_Occurred())
            return nullptr;
    }

    Session::Notify notify(QString::fromUtf8(message), paths, toNotifyStatus(status), details);
    self->session->updateProgressNotify(notify, static_cast<size_t>(completed));
    Py_RETURN_NONE;
}

static PyObject*
PySession_cancelProgressBlock(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    self->session->cancelProgressBlock();
    Py_RETURN_NONE;
}

static PyObject*
PySession_endProgressBlock(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    self->session->endProgressBlock();
    Py_RETURN_NONE;
}

static PyObject*
PySession_isProgressBlockCancelled(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyBool_FromLong(self->session->isProgressBlockCancelled());
}

static PyObject*
PySession_newStage(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    long policy = static_cast<long>(Session::LoadPolicy::All);
    static const char* keywords[] = { "policy", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|l", const_cast<char**>(keywords), &policy))
        return nullptr;

    return PyBool_FromLong(self->session->newStage(toLoadPolicy(policy)));
}

static PyObject*
PySession_load(PySessionObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    long policy = static_cast<long>(Session::LoadPolicy::All);

    static const char* keywords[] = { "filename", "policy", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|l", const_cast<char**>(keywords), &filename, &policy))
        return nullptr;

    return PyBool_FromLong(self->session->loadFromFile(QString::fromUtf8(filename), toLoadPolicy(policy)));
}

static PyObject*
PySession_save(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return nullptr;

    return PyBool_FromLong(self->session->saveToFile(QString::fromUtf8(filename)));
}

static PyObject*
PySession_copy(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return nullptr;

    return PyBool_FromLong(self->session->copyToFile(QString::fromUtf8(filename)));
}

static PyObject*
PySession_flatten(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return nullptr;

    return PyBool_FromLong(self->session->flattenToFile(QString::fromUtf8(filename)));
}

static PyObject*
PySession_flattenPaths(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    PyObject* pyPaths = nullptr;
    const char* filename = nullptr;
    if (!PyArg_ParseTuple(args, "Os", &pyPaths, &filename))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    return PyBool_FromLong(self->session->flattenPathsToFile(paths, QString::fromUtf8(filename)));
}

static PyObject*
PySession_reload(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyBool_FromLong(self->session->reload());
}

static PyObject*
PySession_close(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyBool_FromLong(self->session->close());
}

static PyObject*
PySession_isLoaded(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyBool_FromLong(self->session->isLoaded());
}

static PyObject*
PySession_mask(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return pathListToPyList(self->session->mask());
}

static PyObject*
PySession_setMask(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    PyObject* pyPaths = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pyPaths))
        return nullptr;

    QList<SdfPath> paths;
    if (!pyToPathList(pyPaths, &paths))
        return nullptr;

    self->session->setMask(paths);
    Py_RETURN_NONE;
}

static PyObject*
PySession_stageUp(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->session->stageUp()));
}

static PyObject*
PySession_setStageUp(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    long value = 0;
    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->session->setStageUp(toStageUp(value));
    Py_RETURN_NONE;
}

static PyObject*
PySession_loadPolicy(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->session->loadPolicy()));
}

static PyObject*
PySession_boundingBox(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return bboxToPyTuple(self->session->boundingBox());
}

static PyObject*
PySession_filename(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    const QString name = self->session->filename();
    return PyUnicode_FromString(name.toUtf8().constData());
}

static PyObject*
PySession_stage(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return wrapUsdStage(self->session->stage());
}

static PyObject*
PySession_stageUnsafe(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return wrapUsdStage(self->session->stageUnsafe());
}

static PyObject*
PySession_stageLock(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    QReadWriteLock* lock = self->session->stageLock();
    if (!lock)
        Py_RETURN_NONE;

    return PyLong_FromVoidPtr(lock);
}

static PyObject*
PySession_selectionList(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return createPySelectionList(self->session->selectionList());
}

static PyObject*
PySession_selection(PySessionObject* self)
{
    return PySession_selectionList(self);
}

static PyObject*
PySession_paths(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    SelectionList* selection = self->session->selectionList();
    if (!checkSelectionList(selection))
        return nullptr;

    return pathListToPyList(selection->paths());
}

static PyObject*
PySession_primsUpdate(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    return PyLong_FromLong(static_cast<long>(self->session->primsUpdate()));
}

static PyObject*
PySession_setPrimsUpdate(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    long value = 0;
    if (!PyArg_ParseTuple(args, "l", &value))
        return nullptr;

    self->session->setPrimsUpdate(toPrimsUpdate(value));
    Py_RETURN_NONE;
}

static PyObject*
PySession_flushPrimsUpdates(PySessionObject* self)
{
    if (!checkSession(self->session))
        return nullptr;

    self->session->flushPrimsUpdates();
    Py_RETURN_NONE;
}

static PyObject*
PySession_setStatus(PySessionObject* self, PyObject* args)
{
    if (!checkSession(self->session))
        return nullptr;

    const char* status = nullptr;
    if (!PyArg_ParseTuple(args, "s", &status))
        return nullptr;

    self->session->notifyStatus(Session::Notify::Status::Info, QString::fromUtf8(status));
    Py_RETURN_NONE;
}

static PyMethodDef PySession_methods[] = {
    { "beginProgressBlock", reinterpret_cast<PyCFunction>(PySession_beginProgressBlock), METH_VARARGS | METH_KEYWORDS,
      "Begin a progress block" },
    { "updateProgressNotify", reinterpret_cast<PyCFunction>(PySession_updateProgressNotify),
      METH_VARARGS | METH_KEYWORDS, "Update progress with a notification" },
    { "cancelProgressBlock", reinterpret_cast<PyCFunction>(PySession_cancelProgressBlock), METH_NOARGS,
      "Cancel the current progress block" },
    { "endProgressBlock", reinterpret_cast<PyCFunction>(PySession_endProgressBlock), METH_NOARGS,
      "End the current progress block" },
    { "isProgressBlockCancelled", reinterpret_cast<PyCFunction>(PySession_isProgressBlockCancelled), METH_NOARGS,
      "Check whether the progress block was cancelled" },

    { "newStage", reinterpret_cast<PyCFunction>(PySession_newStage), METH_VARARGS | METH_KEYWORDS,
      "Create a new stage" },
    { "load", reinterpret_cast<PyCFunction>(PySession_load), METH_VARARGS | METH_KEYWORDS,
      "Load a USD stage from file" },
    { "save", reinterpret_cast<PyCFunction>(PySession_save), METH_VARARGS, "Save the current stage to file" },
    { "copy", reinterpret_cast<PyCFunction>(PySession_copy), METH_VARARGS, "Copy the current stage to file" },
    { "flatten", reinterpret_cast<PyCFunction>(PySession_flatten), METH_VARARGS, "Flatten the stage to file" },
    { "flattenPaths", reinterpret_cast<PyCFunction>(PySession_flattenPaths), METH_VARARGS,
      "Flatten specific paths to file" },
    { "reload", reinterpret_cast<PyCFunction>(PySession_reload), METH_NOARGS, "Reload the current stage" },
    { "close", reinterpret_cast<PyCFunction>(PySession_close), METH_NOARGS, "Close the current stage" },
    { "isLoaded", reinterpret_cast<PyCFunction>(PySession_isLoaded), METH_NOARGS, "Check if a stage is loaded" },

    { "mask", reinterpret_cast<PyCFunction>(PySession_mask), METH_NOARGS, "Get the current mask" },
    { "setMask", reinterpret_cast<PyCFunction>(PySession_setMask), METH_VARARGS, "Set the current mask" },
    { "stageUp", reinterpret_cast<PyCFunction>(PySession_stageUp), METH_NOARGS, "Get the stage up axis" },
    { "setStageUp", reinterpret_cast<PyCFunction>(PySession_setStageUp), METH_VARARGS, "Set the stage up axis" },
    { "loadPolicy", reinterpret_cast<PyCFunction>(PySession_loadPolicy), METH_NOARGS, "Get the current load policy" },
    { "boundingBox", reinterpret_cast<PyCFunction>(PySession_boundingBox), METH_NOARGS, "Get the current bounding box" },
    { "filename", reinterpret_cast<PyCFunction>(PySession_filename), METH_NOARGS, "Get the current filename" },
    { "stage", reinterpret_cast<PyCFunction>(PySession_stage), METH_NOARGS, "Get the native USD stage" },
    { "stageUnsafe", reinterpret_cast<PyCFunction>(PySession_stageUnsafe), METH_NOARGS,
      "Get the native USD stage without locking" },
    { "stageLock", reinterpret_cast<PyCFunction>(PySession_stageLock), METH_NOARGS,
      "Get the native stage lock address" },

    { "selectionList", reinterpret_cast<PyCFunction>(PySession_selectionList), METH_NOARGS,
      "Get the selection list wrapper" },
    { "selection", reinterpret_cast<PyCFunction>(PySession_selection), METH_NOARGS, "Get the selection list wrapper" },
    { "paths", reinterpret_cast<PyCFunction>(PySession_paths), METH_NOARGS, "Get selected paths" },

    { "primsUpdate", reinterpret_cast<PyCFunction>(PySession_primsUpdate), METH_NOARGS, "Get the prim update policy" },
    { "setPrimsUpdate", reinterpret_cast<PyCFunction>(PySession_setPrimsUpdate), METH_VARARGS,
      "Set the prim update policy" },
    { "flushPrimsUpdates", reinterpret_cast<PyCFunction>(PySession_flushPrimsUpdates), METH_NOARGS,
      "Flush buffered prim updates" },
    { "setStatus", reinterpret_cast<PyCFunction>(PySession_setStatus), METH_VARARGS, "Set the session status string" },

    { nullptr }
};

PyTypeObject PySessionType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPySessionType()
{
    PySessionType.tp_name = "stageviz.Session";
    PySessionType.tp_basicsize = sizeof(PySessionObject);
    PySessionType.tp_itemsize = 0;
    PySessionType.tp_flags = Py_TPFLAGS_DEFAULT;
    PySessionType.tp_new = PySession_new;
    PySessionType.tp_dealloc = reinterpret_cast<destructor>(PySession_dealloc);
    PySessionType.tp_methods = PySession_methods;

    return PyType_Ready(&PySessionType) >= 0;
}

int
addPySessionType(PyObject* module)
{
    Py_INCREF(&PySessionType);
    if (PyModule_AddObject(module, "Session", reinterpret_cast<PyObject*>(&PySessionType)) < 0) {
        Py_DECREF(&PySessionType);
        return -1;
    }

    return 0;
}

PyObject*
createPySession(Session* session)
{
    if (!session)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PySessionType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PySessionObject*>(object)->session = session;
    return object;
}

}  // namespace stageviz::python
