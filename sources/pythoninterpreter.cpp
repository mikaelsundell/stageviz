// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell

#include "pythoninterpreter.h"
#include "application.h"
#include "commandstack.h"
#include "os.h"
#include "session.h"
#include <QDir>
#include <QPointer>

#undef slots
#include <Python.h>
#define slots Q_SLOTS

extern "C" PyObject*
PyInit_stageviz_wrapper();

extern "C" PyObject*
PyInit__stageviz_native_wrapper();

namespace stageviz {

class PythonInterpreterPrivate {
public:
    PythonInterpreterPrivate();
    ~PythonInterpreterPrivate();
    void init();
    void release();
    void configure();
    QString executeScript(const QString& script);
    QString pythonError() const;

public:
    struct Data {
        QPointer<PythonInterpreter> interpreter;
        PyObject* globals = nullptr;
        PyObject* locals = nullptr;
        bool initialized = false;
    };
    Data d;
};

PythonInterpreterPrivate::PythonInterpreterPrivate() {}

PythonInterpreterPrivate::~PythonInterpreterPrivate() { release(); }

void
PythonInterpreterPrivate::init()
{
    if (d.initialized)
        return;

    if (PyImport_AppendInittab("stageviz", PyInit_stageviz_wrapper) == -1) {
        qWarning("Failed to add stageviz module");
        return;
    }

    Py_Initialize();

    QStringList pythonDirs;
#if DEPLOY_BUILD
#    ifdef __APPLE__
    pythonDirs << QDir::cleanPath(os::getApplicationPath() + "/Frameworks/site-packages");
#    else
    pythonDirs << QDir::cleanPath(os::getApplicationPath() + "/site-packages");
#    endif
#else
    pythonDirs = QString::fromUtf8(PYTHON_SEARCH_DIRS).split(';', Qt::SkipEmptyParts);
#endif


    qInfo() << "pythonDirs: " << pythonDirs;

    PyObject* sysPath = PySys_GetObject("path");  // borrowed reference
    if (sysPath && PyList_Check(sysPath)) {
        for (auto it = pythonDirs.crbegin(); it != pythonDirs.crend(); ++it) {
            const QString cleanDir = QDir::cleanPath(*it);
            if (!QDir(cleanDir).exists())
                continue;

            PyObject* path = PyUnicode_FromString(cleanDir.toUtf8().constData());
            if (path) {
                if (PySequence_Contains(sysPath, path) == 0)
                    PyList_Insert(sysPath, 0, path);
                Py_DECREF(path);
            }
        }
    }

    configure();

    PyObject* mainModule = PyImport_AddModule("__main__");
    PyObject* mainDict = PyModule_GetDict(mainModule);

    d.globals = mainDict;
    Py_INCREF(d.globals);

    d.locals = d.globals;
    Py_INCREF(d.locals);

    PyObject* module = PyImport_ImportModule("stageviz");
    if (!module) {
        PyErr_Print();
        qWarning() << "[Python] Failed to import stageviz module";
        return;
    }

    PyObject* cls = PyObject_GetAttrString(module, "Session");
    if (!cls || !PyCallable_Check(cls)) {
        PyErr_Print();
        qWarning() << "[Python] Failed to get Session class";
        Py_XDECREF(cls);
        Py_DECREF(module);
        return;
    }

    PyObject* instance = PyObject_CallObject(cls, nullptr);
    if (!instance) {
        PyErr_Print();
        qWarning() << "[Python] Failed to create Session instance";
        Py_DECREF(cls);
        Py_DECREF(module);
        return;
    }

    PyDict_SetItemString(d.globals, "session", instance);

    Py_DECREF(instance);
    Py_DECREF(cls);
    Py_DECREF(module);

    PyObject* key = PyUnicode_FromString("session");
    int has = PyDict_Contains(d.globals, key);
    Q_UNUSED(has);
    Py_DECREF(key);

    d.initialized = true;
}

void
PythonInterpreterPrivate::configure()
{
    const char* code = "import sys\n"
                       "for _stream_name in ('stdout', 'stderr'):\n"
                       "    _stream = getattr(sys, _stream_name, None)\n"
                       "    if _stream is None:\n"
                       "        continue\n"
                       "    _reconfigure = getattr(_stream, 'reconfigure', None)\n"
                       "    if callable(_reconfigure):\n"
                       "        try:\n"
                       "            _reconfigure(line_buffering=True, write_through=True)\n"
                       "        except TypeError:\n"
                       "            try:\n"
                       "                _reconfigure(line_buffering=True)\n"
                       "            except Exception:\n"
                       "                pass\n"
                       "        except Exception:\n"
                       "            pass\n"
                       "    try:\n"
                       "        _stream.flush()\n"
                       "    except Exception:\n"
                       "        pass\n";

    if (PyRun_SimpleString(code) != 0) {
        PyErr_Clear();
        qWarning() << "[Python] Failed to configure stdout/stderr buffering";
    }
}

void
PythonInterpreterPrivate::release()
{
    if (!d.initialized)
        return;

    PyGILState_STATE gil = PyGILState_Ensure();

    Py_XDECREF(d.globals);
    Py_XDECREF(d.locals);

    d.globals = nullptr;
    d.locals = nullptr;

    PyGILState_Release(gil);

    Py_Finalize();

    d.initialized = false;
}

QString
PythonInterpreterPrivate::executeScript(const QString& script)
{
    if (!d.initialized) {
        const QString error = QStringLiteral("Python not initialized");
        session()->notifyStatus(Session::Notify::Status::Error,
                                QStringLiteral("Python script failed. Check the Python log for details."));
        return error;
    }

    QString output;
    PyGILState_STATE gil = PyGILState_Ensure();

    try {
        QByteArray scriptBytes = script.toUtf8();
        const char* code = scriptBytes.constData();

        session()->setPrimsUpdate(Session::PrimsUpdate::Deferred);
        session()->commandStack()->clear();

        PyObject* result = PyRun_String(code, Py_file_input, d.globals, d.locals);

        session()->setPrimsUpdate(Session::PrimsUpdate::Immediate);

        if (result) {
            if (result != Py_None) {
                PyObject* str = PyObject_Str(result);
                if (str) {
                    output = QString::fromUtf8(PyUnicode_AsUTF8(str));
                    Py_DECREF(str);
                }
            }
            Py_DECREF(result);
        }
        else {
            output = pythonError();
            qWarning().noquote() << "[Python]" << output;
            session()->notifyStatus(Session::Notify::Status::Error,
                                    QStringLiteral("Python script failed. Check the Python log for details"));
        }
    } catch (const std::exception& e) {
        session()->setPrimsUpdate(Session::PrimsUpdate::Immediate);
        output = QStringLiteral("[C++ exception] ") + QString::fromUtf8(e.what());
        qWarning().noquote() << output;
        session()->notifyStatus(Session::Notify::Status::Error,
                                QStringLiteral("Python script failed. Check the Python log for details."));
    } catch (...) {
        session()->setPrimsUpdate(Session::PrimsUpdate::Immediate);
        output = QStringLiteral("[C++ exception] Unknown error");
        qWarning().noquote() << output;
        session()->notifyStatus(Session::Notify::Status::Error,
                                QStringLiteral("Python script failed. Check the Python log for details."));
    }

    PyGILState_Release(gil);
    return output;
}

QString
PythonInterpreterPrivate::pythonError() const
{
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;

    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);

    QString msg;
    if (type) {
        PyObject* s = PyObject_Str(type);
        if (s) {
            msg += QString::fromUtf8(PyUnicode_AsUTF8(s));
            Py_DECREF(s);
        }
        Py_DECREF(type);
    }

    if (value) {
        msg += ": ";
        PyObject* s = PyObject_Str(value);
        if (s) {
            msg += QString::fromUtf8(PyUnicode_AsUTF8(s));
            Py_DECREF(s);
        }
        Py_DECREF(value);
    }

    if (traceback) {
        msg += "\nTraceback:\n";

        PyObject* tbModule = PyImport_ImportModule("traceback");
        if (tbModule) {
            PyObject* fmt = PyObject_GetAttrString(tbModule, "format_tb");
            if (fmt && PyCallable_Check(fmt)) {
                PyObject* list = PyObject_CallFunctionObjArgs(fmt, traceback, nullptr);
                if (list) {
                    PyObject* sep = PyUnicode_FromString("");
                    PyObject* str = PyUnicode_Join(sep, list);
                    Py_DECREF(sep);
                    if (str) {
                        msg += QString::fromUtf8(PyUnicode_AsUTF8(str));
                        Py_DECREF(str);
                    }
                    Py_DECREF(list);
                }
            }
            Py_XDECREF(fmt);
            Py_DECREF(tbModule);
        }

        Py_DECREF(traceback);
    }

    return msg;
}

PythonInterpreter::PythonInterpreter(QObject* parent)
    : QObject(parent)
    , p(new PythonInterpreterPrivate())
{
    p->d.interpreter = this;
    p->init();
}

PythonInterpreter::~PythonInterpreter() = default;

QString
PythonInterpreter::executeScript(const QString& script)
{
    return p->executeScript(script);
}

}  // namespace stageviz
