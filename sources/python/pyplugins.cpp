// SPDX-License-Identifier: BSD-3-Clause
#include "pyplugins.h"
#include "fileformats.h"
#include "pyutils.h"
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <stdexcept>

namespace stageviz::python {
namespace {
    const char providerTag = 0;
    struct Gil {
        PyGILState_STATE state = PyGILState_Ensure();
        ~Gil() { PyGILState_Release(state); }
    };
    struct Ref {
        PyObject* value;
        explicit Ref(PyObject* object)
            : value(object)
        {}
        Ref(const Ref&) = delete;
        ~Ref()
        {
            if (value && Py_IsInitialized()) {
                Gil gil;
                Py_DECREF(value);
            }
        }
        PyObject* release()
        {
            PyObject* result = value;
            value = nullptr;
            return result;
        }
    };
    struct ExceptionState {
        PyObject* type = nullptr;
        PyObject* value = nullptr;
        PyObject* traceback = nullptr;
        QString message;
        ~ExceptionState()
        {
            if (Py_IsInitialized()) {
                Gil gil;
                Py_XDECREF(type);
                Py_XDECREF(value);
                Py_XDECREF(traceback);
            }
        }
    };
    std::shared_ptr<ExceptionState> captureError()
    {
        auto state = std::make_shared<ExceptionState>();
        PyErr_Fetch(&state->type, &state->value, &state->traceback);
        PyErr_NormalizeException(&state->type, &state->value, &state->traceback);
        Ref text(state->value ? PyObject_Str(state->value) : PyUnicode_FromString("Python callback failed"));
        if (text.value)
            state->message = QString::fromUtf8(PyUnicode_AsUTF8(text.value));
        Ref module(PyImport_ImportModule("traceback"));
        Ref lines(module.value
                      ? PyObject_CallMethod(module.value, "format_exception", "OOO",
                                            state->type ? state->type : Py_None, state->value ? state->value : Py_None,
                                            state->traceback ? state->traceback : Py_None)
                      : nullptr);
        Ref separator(PyUnicode_FromString(""));
        Ref details(lines.value ? PyUnicode_Join(separator.value, lines.value) : nullptr);
        if (details.value)
            state->message = QString::fromUtf8(PyUnicode_AsUTF8(details.value));
        PyErr_Clear();
        return state;
    }
    class PythonError : public std::runtime_error {
    public:
        PythonError()
            : PythonError(captureError())
        {}
        void restore() const
        {
            Py_XINCREF(state->type);
            Py_XINCREF(state->value);
            Py_XINCREF(state->traceback);
            PyErr_Restore(state->type, state->value, state->traceback);
        }

    private:
        explicit PythonError(std::shared_ptr<ExceptionState> value)
            : std::runtime_error(value->message.toStdString())
            , state(std::move(value))
        {}
        std::shared_ptr<ExceptionState> state;
    };
    PyObject* translateError()
    {
        try {
            throw;
        } catch (const PythonError& error) {
            error.restore();
        } catch (const std::invalid_argument& error) {
            PyErr_SetString(PyExc_ValueError, error.what());
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_RuntimeError, error.what());
        }
        return nullptr;
    }
    std::shared_ptr<Ref> retain(PyObject* object)
    {
        Py_INCREF(object);
        return std::make_shared<Ref>(object);
    }
    std::shared_ptr<Ref> settingsObject(const std::any& settings)
    {
        auto result = settings.has_value() ? std::any_cast<std::shared_ptr<Ref>>(settings)
                                           : std::make_shared<Ref>(PyDict_New());
        if (!result->value)
            throw PythonError();
        return result;
    }
    fileformats::Format::Load loaderAdapter(const std::shared_ptr<Ref>& callback)
    {
        return [callback](const QString& source, const std::any& settings) {
            Gil gil;
            const auto options = settingsObject(settings);
            Ref result(PyObject_CallFunction(callback->value, "sO", source.toUtf8().constData(), options->value));
            if (!result.value)
                throw PythonError();
            UsdStageRefPtr stage;
            if (!pyToUsdStage(result.value, &stage))
                throw PythonError();
            return stage;
        };
    }
    fileformats::Format::Save saverAdapter(const std::shared_ptr<Ref>& callback)
    {
        return [callback](const UsdStageRefPtr& stage, const QString& destination, const std::any& settings) {
            Gil gil;
            const auto options = settingsObject(settings);
            Ref source(wrapUsdStage(stage));
            if (!source.value)
                throw PythonError();
            Ref result(PyObject_CallFunction(callback->value, "OsO", source.value, destination.toUtf8().constData(),
                                             options->value));
            if (!result.value)
                throw PythonError();
        };
    }
    bool pathFromPython(PyObject* object, QString* result)
    {
        Ref path(PyOS_FSPath(object));
        if (!path.value)
            return false;
        if (!PyUnicode_Check(path.value)) {
            PyErr_SetString(PyExc_TypeError, "File paths must be strings or text PathLike objects");
            return false;
        }
        const char* text = PyUnicode_AsUTF8(path.value);
        if (!text)
            return false;
        *result = QString::fromUtf8(text);
        if (result->startsWith("~/"))
            *result = QDir::home().filePath(result->mid(2));
        return true;
    }
    PyObject* registerFormat(PyObject*, PyObject* args, PyObject* kwargs)
    {
        const char* name = nullptr;
        PyObject* extensions = nullptr;
        PyObject* loader = Py_None;
        PyObject* saver = Py_None;
        PyObject* options = Py_None;
        static const char* keywords[] = { "name", "extensions", "loader", "saver", "options", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO|OOO", const_cast<char**>(keywords), &name, &extensions,
                                         &loader, &saver, &options))
            return nullptr;
        if (PyUnicode_Check(extensions)) {
            PyErr_SetString(PyExc_TypeError, "extensions must be a sequence, not a string");
            return nullptr;
        }
        for (PyObject* callback : { loader, saver, options })
            if (callback != Py_None && !PyCallable_Check(callback)) {
                PyErr_SetString(PyExc_TypeError, "loader, saver and options must be callable or None");
                return nullptr;
            }
        Ref sequence(PySequence_Fast(extensions, "extensions must be a sequence"));
        if (!sequence.value)
            return nullptr;
        fileformats::Format format;
        format.name = QString::fromUtf8(name);
        format.provider = &providerTag;
        for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(sequence.value); ++i) {
            Ref text(PyObject_Str(PySequence_Fast_GET_ITEM(sequence.value, i)));
            if (!text.value)
                return nullptr;
            format.extensions.append(QString::fromUtf8(PyUnicode_AsUTF8(text.value)));
        }
        if (loader != Py_None)
            format.loader = loaderAdapter(retain(loader));
        if (saver != Py_None)
            format.saver = saverAdapter(retain(saver));
        if (options != Py_None) {
            const auto callback = retain(options);
            format.settings = [callback](const QString& path, const QString& operation) -> std::optional<std::any> {
                Gil gil;
                Ref module(PyImport_ImportModule("stageviz"));
                if (!module.value)
                    throw PythonError();
                Ref app(PyObject_CallMethod(module.value, "application", nullptr));
                if (!app.value)
                    throw PythonError();
                Ref parent(PyObject_CallMethod(app.value, "window", nullptr));
                if (!parent.value)
                    throw PythonError();
                Ref result(PyObject_CallFunction(callback->value, "ssO", path.toUtf8().constData(),
                                                 operation.toUtf8().constData(), parent.value));
                if (!result.value)
                    throw PythonError();
                if (result.value == Py_None)
                    return std::nullopt;
                if (!PyDict_Check(result.value)) {
                    PyErr_SetString(PyExc_TypeError, "options must return a dict, or None to cancel");
                    throw PythonError();
                }
                return std::any(std::make_shared<Ref>(result.release()));
            };
        }
        try {
            fileformats::registerFormat(std::move(format));
            Py_RETURN_NONE;
        } catch (...) {
            return translateError();
        }
    }
    PyObject* unregisterFormat(PyObject*, PyObject* args, PyObject* kwargs)
    {
        const char* name = nullptr;
        static const char* keywords[] = { "name", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", const_cast<char**>(keywords), &name))
            return nullptr;
        try {
            return PyBool_FromLong(fileformats::unregisterFormat(QString::fromUtf8(name)));
        } catch (...) {
            return translateError();
        }
    }
    PyObject* formats(PyObject*, PyObject*)
    {
        try {
            Ref result(PyList_New(0));
            for (const auto& format : fileformats::formats()) {
                Ref extensions(PyTuple_New(format.extensions.size()));
                for (qsizetype i = 0; i < format.extensions.size(); ++i)
                    PyTuple_SET_ITEM(extensions.value, i,
                                     PyUnicode_FromString(format.extensions[i].toUtf8().constData()));
                Ref info(Py_BuildValue("{s:s,s:O,s:O,s:O}", "name", format.name.toUtf8().constData(), "extensions",
                                       extensions.value, "readable", format.readable ? Py_True : Py_False, "writable",
                                       format.writable ? Py_True : Py_False));
                if (!info.value || PyList_Append(result.value, info.value) != 0)
                    return nullptr;
            }
            return result.release();
        } catch (...) {
            return translateError();
        }
    }
    PyObject* read(PyObject*, PyObject* args, PyObject* kwargs)
    {
        PyObject* path = nullptr;
        const char* operation = "open";
        int interactive = 0;
        static const char* keywords[] = { "path", "interactive", "operation", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|ps", const_cast<char**>(keywords), &path, &interactive,
                                         &operation))
            return nullptr;
        QString filename;
        if (!pathFromPython(path, &filename))
            return nullptr;
        if (fileformats::isNative(filename)) {
            PyErr_SetString(PyExc_ValueError, "Use Session.load for native USD files");
            return nullptr;
        }
        if (!QFileInfo(filename).isFile()) {
            PyErr_SetString(PyExc_FileNotFoundError, filename.toUtf8().constData());
            return nullptr;
        }
        try {
            return wrapUsdStage(fileformats::read(filename, interactive != 0, QString::fromUtf8(operation)));
        } catch (...) {
            return translateError();
        }
    }
    PyObject* write(PyObject*, PyObject* args, PyObject* kwargs)
    {
        PyObject* source = nullptr;
        PyObject* path = nullptr;
        int interactive = 0;
        static const char* keywords[] = { "stage", "path", "interactive", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|p", const_cast<char**>(keywords), &source, &path,
                                         &interactive))
            return nullptr;
        UsdStageRefPtr stage;
        QString filename;
        if (!pyToUsdStage(source, &stage) || !pathFromPython(path, &filename))
            return nullptr;
        try {
            return PyBool_FromLong(fileformats::write(stage, filename, interactive != 0));
        } catch (...) {
            return translateError();
        }
    }
    PyObject* filters(PyObject*, PyObject* args)
    {
        int writing = 0;
        if (!PyArg_ParseTuple(args, "|p", &writing))
            return nullptr;
        try {
            return PyUnicode_FromString(fileformats::filters(writing != 0).toUtf8().constData());
        } catch (...) {
            return translateError();
        }
    }
    PyMethodDef methods[] = { { "registerFileFormat", reinterpret_cast<PyCFunction>(registerFormat),
                                METH_VARARGS | METH_KEYWORDS,
                                "Register Python callbacks with the core file-format registry" },
                              { "unregisterFileFormat", reinterpret_cast<PyCFunction>(unregisterFormat),
                                METH_VARARGS | METH_KEYWORDS, "Remove a core registration" },
                              { "fileFormats", formats, METH_NOARGS, "Return core registration metadata" },
                              { "readFile", reinterpret_cast<PyCFunction>(read), METH_VARARGS | METH_KEYWORDS,
                                "Load an external file as an in-memory USD stage" },
                              { "writeFile", reinterpret_cast<PyCFunction>(write), METH_VARARGS | METH_KEYWORDS,
                                "Export a USD stage through the core registry" },
                              { "_filters", filters, METH_VARARGS, "Return core dialog filters for diagnostics" },
                              { nullptr, nullptr, 0, nullptr } };
    PyModuleDef nativeModule = { PyModuleDef_HEAD_INIT, "stageviz.plugins", nullptr, -1, methods };
}  // namespace
void
releasePyPlugins()
{
    fileformats::unregisterProvider(&providerTag);
}
int
addPyPluginsModule(PyObject* module)
{
    Ref formats(PyModule_Create(&nativeModule));
    if (!formats.value)
        return -1;
    if (PyDict_SetItemString(PyImport_GetModuleDict(), "stageviz.plugins", formats.value) != 0)
        return -1;
    return PyModule_AddObject(module, "plugins", formats.release());
}
}  // namespace stageviz::python
