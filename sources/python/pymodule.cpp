// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "console.h"
#include "mcp/mcpserver.h"
#include "pyapplication.h"
#include "pycommand.h"
#include "pyrender.h"
#include "pyrenderengine.h"
#include "pyselectionlist.h"
#include "pysession.h"
#include "pyui.h"
#include "pyutils.h"
#include "pyviewcamera.h"
#include "pyviewstate.h"

using namespace stageviz;
using namespace stageviz::python;

static PyModuleDef pyStagevizModule = {
    PyModuleDef_HEAD_INIT, "stageviz", "Python interface for stageviz", -1, nullptr, nullptr, nullptr, nullptr, nullptr
};

static PyObject*
PyModule_application(PyObject*, PyObject*)
{
    Application* app = Application::instance();
    if (!app)
        Py_RETURN_NONE;

    return createPyApplication(app);
}

static PyObject*
PyModule_session(PyObject*, PyObject*)
{
    return createPySession(currentSession());
}

static PyObject*
PyModule_render(PyObject*, PyObject*)
{
    return createPyRender();
}

static PyObject*
PyModule_ui(PyObject*, PyObject*)
{
    Application* app = Application::instance();
    if (!app)
        Py_RETURN_NONE;

    return createPyUI(app->ui());
}

static PyObject*
PyModule_selectionList(PyObject*, PyObject*)
{
    Session* s = currentSession();
    if (!checkSession(s))
        return nullptr;

    return createPySelectionList(s->selectionList());
}

static PyObject*
PyModule_startMcpServer(PyObject*, PyObject*)
{
    McpServer* server = mcpServer();
    if (!server) {
        PyErr_SetString(PyExc_RuntimeError, "Stageviz MCP server is unavailable.");
        return nullptr;
    }
    const bool wasRunning = server->isRunning();
    if (!server->start()) {
        const QByteArray message = server->errorString().toUtf8();
        PyErr_SetString(PyExc_RuntimeError, message.constData());
        return nullptr;
    }
    const QByteArray url = server->url().toString().toUtf8();
    const QString message = QStringLiteral("MCP server %1 at %2")
                                .arg(wasRunning ? QStringLiteral("already running") : QStringLiteral("listening"),
                                     QString::fromUtf8(url));
    if (Session* session = currentSession())
        session->notifyStatus(Session::Notify::Status::Success, message);
    else
        PySys_WriteStdout("[MCP] %s\n", message.toUtf8().constData());
    return PyUnicode_FromString(url.constData());
}

static PyObject*
PyModule_stopMcpServer(PyObject*, PyObject*)
{
    McpServer* server = mcpServer();
    if (!server) {
        PyErr_SetString(PyExc_RuntimeError, "Stageviz MCP server is unavailable.");
        return nullptr;
    }
    server->stop();
    Py_RETURN_NONE;
}

static PyObject*
PyModule_isMcpServerRunning(PyObject*, PyObject*)
{
    McpServer* server = mcpServer();
    if (!server) {
        PyErr_SetString(PyExc_RuntimeError, "Stageviz MCP server is unavailable.");
        return nullptr;
    }
    return PyBool_FromLong(server->isRunning());
}

static PyObject*
PyModule_setConsoleOutputFile(PyObject*, PyObject* args, PyObject* kwargs)
{
    const char* filename = nullptr;
    int append = 1;
    static const char* keywords[] = { "path", "append", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "z|p:setConsoleOutputFile", const_cast<char**>(keywords), &filename,
                                     &append))
        return nullptr;
    Console* output = console();
    if (!output) {
        PyErr_SetString(PyExc_RuntimeError, "Stageviz console is unavailable.");
        return nullptr;
    }
    QString error;
    if (!output->setOutputFile(filename ? QString::fromUtf8(filename) : QString(), append != 0, error)) {
        PyErr_SetString(PyExc_OSError, error.toUtf8().constData());
        return nullptr;
    }
    Py_RETURN_NONE;
}

static PyObject*
PyModule_consoleOutputFile(PyObject*, PyObject*)
{
    Console* output = console();
    const QString filename = output ? output->outputFile() : QString();
    if (filename.isEmpty())
        Py_RETURN_NONE;
    return PyUnicode_FromString(filename.toUtf8().constData());
}

static PyMethodDef Module_methods[]
    = { { "application", reinterpret_cast<PyCFunction>(PyModule_application), METH_NOARGS,
          "Get the current stageviz application wrapper" },
        { "session", reinterpret_cast<PyCFunction>(PyModule_session), METH_NOARGS,
          "Get the current stageviz session wrapper" },
        { "ui", reinterpret_cast<PyCFunction>(PyModule_ui), METH_NOARGS, "Get the current stageviz UI wrapper" },
        { "render", reinterpret_cast<PyCFunction>(PyModule_render), METH_NOARGS,
          "Get read-only access to the active render view for capture and diagnostics" },
        { "selectionList", reinterpret_cast<PyCFunction>(PyModule_selectionList), METH_NOARGS,
          "Get the current stageviz selection list wrapper" },
        { "setConsoleOutputFile", reinterpret_cast<PyCFunction>(PyModule_setConsoleOutputFile),
          METH_VARARGS | METH_KEYWORDS, "Log captured stdout/stderr to path (append=True); pass None to stop" },
        { "consoleOutputFile", reinterpret_cast<PyCFunction>(PyModule_consoleOutputFile), METH_NOARGS,
          "Return the active console output filename, or None" },
        { "startMcpServer", reinterpret_cast<PyCFunction>(PyModule_startMcpServer), METH_NOARGS,
          "Start the local MCP server and return its URL" },
        { "stopMcpServer", reinterpret_cast<PyCFunction>(PyModule_stopMcpServer), METH_NOARGS,
          "Stop the local MCP server" },
        { "isMcpServerRunning", reinterpret_cast<PyCFunction>(PyModule_isMcpServerRunning), METH_NOARGS,
          "Return whether the local MCP server is running" },
        { nullptr, nullptr, 0, nullptr } };

PyMODINIT_FUNC
PyInit_stageviz(void)
{
    pyStagevizModule.m_methods = Module_methods;

    if (!initPyApplicationType())
        return nullptr;

    if (!initPySelectionListType())
        return nullptr;

    if (!initPyRenderEngineType())
        return nullptr;

    if (!initPyViewCameraType())
        return nullptr;

    if (!initPyViewStateType())
        return nullptr;

    if (!initPySessionType())
        return nullptr;

    if (!initPyUIType())
        return nullptr;
    if (!initPyRenderTypes())
        return nullptr;

    PyObject* module = PyModule_Create(&pyStagevizModule);
    if (!module)
        return nullptr;

    if (addPyRenderTypes(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPyApplicationType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPySelectionListType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPyRenderEngineType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPyViewCameraType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPyViewStateType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPySessionType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPyUIType(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    if (addPyCommandModule(module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    PyModule_AddIntConstant(module, "LoadAll", static_cast<int>(Session::LoadPolicy::All));
    PyModule_AddIntConstant(module, "LoadNone", static_cast<int>(Session::LoadPolicy::None));

    PyModule_AddIntConstant(module, "ProgressIdle", static_cast<int>(Session::ProgressMode::Idle));
    PyModule_AddIntConstant(module, "ProgressRunning", static_cast<int>(Session::ProgressMode::Running));

    PyModule_AddIntConstant(module, "PrimsImmediate", static_cast<int>(Session::PrimsUpdate::Immediate));
    PyModule_AddIntConstant(module, "PrimsDeferred", static_cast<int>(Session::PrimsUpdate::Deferred));

    PyModule_AddIntConstant(module, "StageLoaded", static_cast<int>(Session::StageStatus::Loaded));
    PyModule_AddIntConstant(module, "StageFailed", static_cast<int>(Session::StageStatus::Failed));
    PyModule_AddIntConstant(module, "StageClosed", static_cast<int>(Session::StageStatus::Closed));

    PyModule_AddIntConstant(module, "StageUpY", static_cast<int>(Session::StageUp::Y));
    PyModule_AddIntConstant(module, "StageUpZ", static_cast<int>(Session::StageUp::Z));

    PyModule_AddIntConstant(module, "NotifySuccess", static_cast<int>(Session::Notify::Status::Success));
    PyModule_AddIntConstant(module, "NotifyInfo", 0);
    PyModule_AddIntConstant(module, "NotifyProgress", 1);
    PyModule_AddIntConstant(module, "NotifyWarning", 2);
    PyModule_AddIntConstant(module, "NotifyError", 3);

    PyModule_AddIntConstant(module, "CameraUpX", static_cast<int>(ViewCamera::CameraUp::X));
    PyModule_AddIntConstant(module, "CameraUpY", static_cast<int>(ViewCamera::CameraUp::Y));
    PyModule_AddIntConstant(module, "CameraUpZ", static_cast<int>(ViewCamera::CameraUp::Z));

    PyModule_AddIntConstant(module, "CameraModeNone", static_cast<int>(ViewCamera::CameraMode::None));
    PyModule_AddIntConstant(module, "CameraModeTruck", static_cast<int>(ViewCamera::CameraMode::Truck));
    PyModule_AddIntConstant(module, "CameraModeTumble", static_cast<int>(ViewCamera::CameraMode::Tumble));
    PyModule_AddIntConstant(module, "CameraModeZoom", static_cast<int>(ViewCamera::CameraMode::Zoom));
    PyModule_AddIntConstant(module, "CameraModePick", static_cast<int>(ViewCamera::CameraMode::Pick));

    PyModule_AddIntConstant(module, "FovVertical", static_cast<int>(ViewCamera::FovDirection::Vertical));
    PyModule_AddIntConstant(module, "FovHorizontal", static_cast<int>(ViewCamera::FovDirection::Horizontal));
    PyModule_AddIntConstant(module, "ProjectionFieldOfView", static_cast<int>(ViewCamera::FieldOfView));
    PyModule_AddIntConstant(module, "ProjectionPhysical", static_cast<int>(ViewCamera::Physical));
    PyModule_AddIntConstant(module, "ProjectionOrthographic", static_cast<int>(ViewCamera::Orthographic));
    PyModule_AddIntConstant(module, "CameraViewPerspective", static_cast<int>(ViewCamera::PerspectiveView));
    PyModule_AddIntConstant(module, "CameraViewTop", static_cast<int>(ViewCamera::TopView));
    PyModule_AddIntConstant(module, "CameraViewFront", static_cast<int>(ViewCamera::FrontView));
    PyModule_AddIntConstant(module, "CameraViewSide", static_cast<int>(ViewCamera::SideView));
    PyModule_AddIntConstant(module, "RenderShaded", static_cast<int>(ViewState::Shaded));
    PyModule_AddIntConstant(module, "RenderWireframe", static_cast<int>(ViewState::Wireframe));
    PyModule_AddIntConstant(module, "ComplexityLow", static_cast<int>(ViewState::Low));
    PyModule_AddIntConstant(module, "ComplexityMedium", static_cast<int>(ViewState::Medium));
    PyModule_AddIntConstant(module, "ComplexityHigh", static_cast<int>(ViewState::High));
    PyModule_AddIntConstant(module, "ComplexityVeryHigh", static_cast<int>(ViewState::VeryHigh));
    PyModule_AddIntConstant(module, "MaterialAll", static_cast<int>(ViewState::All));
    PyModule_AddIntConstant(module, "MaterialClay", static_cast<int>(ViewState::Clay));
    PyModule_AddIntConstant(module, "MaterialOverride", static_cast<int>(ViewState::Override));
    PyModule_AddIntConstant(module, "SidednessPrimitive", static_cast<int>(ViewState::Primitive));
    PyModule_AddIntConstant(module, "SidednessDoubleSided", static_cast<int>(ViewState::DoubleSided));
    PyModule_AddIntConstant(module, "SidednessSingleSided", static_cast<int>(ViewState::SingleSided));
    PyModule_AddIntConstant(module, "AmbientOcclusionLow", static_cast<int>(ViewState::AmbientOcclusionLow));
    PyModule_AddIntConstant(module, "AmbientOcclusionMedium", static_cast<int>(ViewState::AmbientOcclusionMedium));
    PyModule_AddIntConstant(module, "AmbientOcclusionHigh", static_cast<int>(ViewState::AmbientOcclusionHigh));
    PyModule_AddIntConstant(module, "AmbientOcclusionUltra", static_cast<int>(ViewState::AmbientOcclusionUltra));
    PyModule_AddIntConstant(module, "AmbientOcclusionComposite",
                            static_cast<int>(ViewState::AmbientOcclusionComposite));
    PyModule_AddIntConstant(module, "AmbientOcclusionCombined", static_cast<int>(ViewState::AmbientOcclusionCombined));
    PyModule_AddIntConstant(module, "AmbientOcclusionContact", static_cast<int>(ViewState::AmbientOcclusionContact));
    PyModule_AddIntConstant(module, "AmbientOcclusionBroad", static_cast<int>(ViewState::AmbientOcclusionBroad));

    PyModule_AddIntConstant(module, "ColorBase", static_cast<int>(UI::ColorRole::Base));
    PyModule_AddIntConstant(module, "ColorBaseAlt", static_cast<int>(UI::ColorRole::BaseAlt));
    PyModule_AddIntConstant(module, "ColorAccent", static_cast<int>(UI::ColorRole::Accent));
    PyModule_AddIntConstant(module, "ColorAccentAlt", static_cast<int>(UI::ColorRole::AccentAlt));
    PyModule_AddIntConstant(module, "ColorText", static_cast<int>(UI::ColorRole::Text));
    PyModule_AddIntConstant(module, "ColorHighlight", static_cast<int>(UI::ColorRole::Highlight));
    PyModule_AddIntConstant(module, "ColorHighlightAlt", static_cast<int>(UI::ColorRole::HighlightAlt));
    PyModule_AddIntConstant(module, "ColorBorder", static_cast<int>(UI::ColorRole::Border));
    PyModule_AddIntConstant(module, "ColorBorderAlt", static_cast<int>(UI::ColorRole::BorderAlt));
    PyModule_AddIntConstant(module, "ColorHandle", static_cast<int>(UI::ColorRole::Handle));
    PyModule_AddIntConstant(module, "ColorProgress", static_cast<int>(UI::ColorRole::Progress));
    PyModule_AddIntConstant(module, "ColorButton", static_cast<int>(UI::ColorRole::Button));
    PyModule_AddIntConstant(module, "ColorButtonAlt", static_cast<int>(UI::ColorRole::ButtonAlt));
    PyModule_AddIntConstant(module, "ColorItem", static_cast<int>(UI::ColorRole::Item));
    PyModule_AddIntConstant(module, "ColorItemAlt", static_cast<int>(UI::ColorRole::ItemAlt));
    PyModule_AddIntConstant(module, "ColorRender", static_cast<int>(UI::ColorRole::Render));
    PyModule_AddIntConstant(module, "ColorRenderAlt", static_cast<int>(UI::ColorRole::RenderAlt));
    PyModule_AddIntConstant(module, "ColorSelection", static_cast<int>(UI::ColorRole::Selection));
    PyModule_AddIntConstant(module, "ColorSelectionAlt", static_cast<int>(UI::ColorRole::SelectionAlt));
    PyModule_AddIntConstant(module, "ColorWarning", static_cast<int>(UI::ColorRole::Warning));
    PyModule_AddIntConstant(module, "ColorReadonly", static_cast<int>(UI::ColorRole::Readonly));
    PyModule_AddIntConstant(module, "ColorError", static_cast<int>(UI::ColorRole::Error));

    PyModule_AddIntConstant(module, "IconBranchClosed", static_cast<int>(UI::IconRole::BranchClosed));
    PyModule_AddIntConstant(module, "IconBranchOpen", static_cast<int>(UI::IconRole::BranchOpen));
    PyModule_AddIntConstant(module, "IconChecked", static_cast<int>(UI::IconRole::Checked));
    PyModule_AddIntConstant(module, "IconClear", static_cast<int>(UI::IconRole::Clear));
    PyModule_AddIntConstant(module, "IconCode", static_cast<int>(UI::IconRole::Code));
    PyModule_AddIntConstant(module, "IconCollapse", static_cast<int>(UI::IconRole::Collapse));
    PyModule_AddIntConstant(module, "IconDropdown", static_cast<int>(UI::IconRole::DropDown));
    PyModule_AddIntConstant(module, "IconExpand", static_cast<int>(UI::IconRole::Expand));
    PyModule_AddIntConstant(module, "IconExport", static_cast<int>(UI::IconRole::Export));
    PyModule_AddIntConstant(module, "IconExportImage", static_cast<int>(UI::IconRole::ExportImage));
    PyModule_AddIntConstant(module, "IconFollow", static_cast<int>(UI::IconRole::Follow));
    PyModule_AddIntConstant(module, "IconFrameAll", static_cast<int>(UI::IconRole::FrameAll));
    PyModule_AddIntConstant(module, "IconGeometry", static_cast<int>(UI::IconRole::Geometry));
    PyModule_AddIntConstant(module, "IconHidden", static_cast<int>(UI::IconRole::Hidden));
    PyModule_AddIntConstant(module, "IconLeft", static_cast<int>(UI::IconRole::Left));
    PyModule_AddIntConstant(module, "IconLock", static_cast<int>(UI::IconRole::Lock));
    PyModule_AddIntConstant(module, "IconMaterial", static_cast<int>(UI::IconRole::Material));
    PyModule_AddIntConstant(module, "IconOpen", static_cast<int>(UI::IconRole::Open));
    PyModule_AddIntConstant(module, "IconPartiallyChecked", static_cast<int>(UI::IconRole::PartiallyChecked));
    PyModule_AddIntConstant(module, "IconPayload", static_cast<int>(UI::IconRole::Payload));
    PyModule_AddIntConstant(module, "IconPrim", static_cast<int>(UI::IconRole::Prim));
    PyModule_AddIntConstant(module, "IconRedo", static_cast<int>(UI::IconRole::Redo));
    PyModule_AddIntConstant(module, "IconRight", static_cast<int>(UI::IconRole::Right));
    PyModule_AddIntConstant(module, "IconRun", static_cast<int>(UI::IconRole::Run));
    PyModule_AddIntConstant(module, "IconShaded", static_cast<int>(UI::IconRole::Shaded));
    PyModule_AddIntConstant(module, "IconUndo", static_cast<int>(UI::IconRole::Undo));
    PyModule_AddIntConstant(module, "IconUnlock", static_cast<int>(UI::IconRole::Unlock));
    PyModule_AddIntConstant(module, "IconVisible", static_cast<int>(UI::IconRole::Visible));
    PyModule_AddIntConstant(module, "IconWireframe", static_cast<int>(UI::IconRole::Wireframe));

    PyModule_AddIntConstant(module, "UIScaleSmall", static_cast<int>(UI::UIScale::Small));
    PyModule_AddIntConstant(module, "UIScaleMedium", static_cast<int>(UI::UIScale::Medium));
    PyModule_AddIntConstant(module, "UIScaleLarge", static_cast<int>(UI::UIScale::Large));

    PyModule_AddIntConstant(module, "UIStateNormal", static_cast<int>(UI::UIState::Normal));
    PyModule_AddIntConstant(module, "UIStateDisabled", static_cast<int>(UI::UIState::Disabled));

    return module;
}

extern "C" PyObject*
PyInit_stageviz_wrapper()
{
    return PyInit_stageviz();
}
