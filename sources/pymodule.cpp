// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "application.h"
#include "pyapplication.h"
#include "pycommand.h"
#include "pyrenderengine.h"
#include "pyselectionlist.h"
#include "pysession.h"
#include "pystyle.h"
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
PyModule_style(PyObject*, PyObject*)
{
    Application* app = Application::instance();
    if (!app)
        Py_RETURN_NONE;

    return createPyStyle(app->style());
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
PyModule_getCurrentStage(PyObject*, PyObject*)
{
    Session* s = currentSession();
    if (!checkSession(s))
        return nullptr;

    return wrapUsdStage(s->stage());
}

static PyMethodDef Module_methods[] = { { "application", reinterpret_cast<PyCFunction>(PyModule_application),
                                          METH_NOARGS, "Get the current stageviz application wrapper" },
                                        { "session", reinterpret_cast<PyCFunction>(PyModule_session), METH_NOARGS,
                                          "Get the current stageviz session wrapper" },
                                        { "style", reinterpret_cast<PyCFunction>(PyModule_style), METH_NOARGS,
                                          "Get the current stageviz style wrapper" },
                                        { "selectionList", reinterpret_cast<PyCFunction>(PyModule_selectionList),
                                          METH_NOARGS, "Get the current stageviz selection list wrapper" },
                                        { "getCurrentStage", reinterpret_cast<PyCFunction>(PyModule_getCurrentStage),
                                          METH_NOARGS, "Get the current native USD stage" },
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

    if (!initPyStyleType())
        return nullptr;

    PyObject* module = PyModule_Create(&pyStagevizModule);
    if (!module)
        return nullptr;

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

    if (addPyStyleType(module) < 0) {
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

    PyModule_AddIntConstant(module, "ColorBase", static_cast<int>(Style::ColorRole::Base));
    PyModule_AddIntConstant(module, "ColorBaseAlt", static_cast<int>(Style::ColorRole::BaseAlt));
    PyModule_AddIntConstant(module, "ColorAccent", static_cast<int>(Style::ColorRole::Accent));
    PyModule_AddIntConstant(module, "ColorAccentAlt", static_cast<int>(Style::ColorRole::AccentAlt));
    PyModule_AddIntConstant(module, "ColorText", static_cast<int>(Style::ColorRole::Text));
    PyModule_AddIntConstant(module, "ColorHighlight", static_cast<int>(Style::ColorRole::Highlight));
    PyModule_AddIntConstant(module, "ColorHighlightAlt", static_cast<int>(Style::ColorRole::HighlightAlt));
    PyModule_AddIntConstant(module, "ColorBorder", static_cast<int>(Style::ColorRole::Border));
    PyModule_AddIntConstant(module, "ColorBorderAlt", static_cast<int>(Style::ColorRole::BorderAlt));
    PyModule_AddIntConstant(module, "ColorHandle", static_cast<int>(Style::ColorRole::Handle));
    PyModule_AddIntConstant(module, "ColorProgress", static_cast<int>(Style::ColorRole::Progress));
    PyModule_AddIntConstant(module, "ColorButton", static_cast<int>(Style::ColorRole::Button));
    PyModule_AddIntConstant(module, "ColorButtonAlt", static_cast<int>(Style::ColorRole::ButtonAlt));
    PyModule_AddIntConstant(module, "ColorItem", static_cast<int>(Style::ColorRole::Item));
    PyModule_AddIntConstant(module, "ColorItemAlt", static_cast<int>(Style::ColorRole::ItemAlt));
    PyModule_AddIntConstant(module, "ColorRender", static_cast<int>(Style::ColorRole::Render));
    PyModule_AddIntConstant(module, "ColorRenderAlt", static_cast<int>(Style::ColorRole::RenderAlt));
    PyModule_AddIntConstant(module, "ColorSelection", static_cast<int>(Style::ColorRole::Selection));
    PyModule_AddIntConstant(module, "ColorSelectionAlt", static_cast<int>(Style::ColorRole::SelectionAlt));
    PyModule_AddIntConstant(module, "ColorWarning", static_cast<int>(Style::ColorRole::Warning));
    PyModule_AddIntConstant(module, "ColorError", static_cast<int>(Style::ColorRole::Error));

    PyModule_AddIntConstant(module, "IconBranchClosed", static_cast<int>(Style::IconRole::BranchClosed));
    PyModule_AddIntConstant(module, "IconBranchOpen", static_cast<int>(Style::IconRole::BranchOpen));
    PyModule_AddIntConstant(module, "IconChecked", static_cast<int>(Style::IconRole::Checked));
    PyModule_AddIntConstant(module, "IconClear", static_cast<int>(Style::IconRole::Clear));
    PyModule_AddIntConstant(module, "IconCode", static_cast<int>(Style::IconRole::Code));
    PyModule_AddIntConstant(module, "IconCollapse", static_cast<int>(Style::IconRole::Collapse));
    PyModule_AddIntConstant(module, "IconDropdown", static_cast<int>(Style::IconRole::DropDown));
    PyModule_AddIntConstant(module, "IconExpand", static_cast<int>(Style::IconRole::Expand));
    PyModule_AddIntConstant(module, "IconExport", static_cast<int>(Style::IconRole::Export));
    PyModule_AddIntConstant(module, "IconExportImage", static_cast<int>(Style::IconRole::ExportImage));
    PyModule_AddIntConstant(module, "IconFollow", static_cast<int>(Style::IconRole::Follow));
    PyModule_AddIntConstant(module, "IconFrameAll", static_cast<int>(Style::IconRole::FrameAll));
    PyModule_AddIntConstant(module, "IconGeometry", static_cast<int>(Style::IconRole::Geometry));
    PyModule_AddIntConstant(module, "IconHidden", static_cast<int>(Style::IconRole::Hidden));
    PyModule_AddIntConstant(module, "IconLeft", static_cast<int>(Style::IconRole::Left));
    PyModule_AddIntConstant(module, "IconMaterial", static_cast<int>(Style::IconRole::Material));
    PyModule_AddIntConstant(module, "IconOpen", static_cast<int>(Style::IconRole::Open));
    PyModule_AddIntConstant(module, "IconPartiallyChecked", static_cast<int>(Style::IconRole::PartiallyChecked));
    PyModule_AddIntConstant(module, "IconPayload", static_cast<int>(Style::IconRole::Payload));
    PyModule_AddIntConstant(module, "IconPrim", static_cast<int>(Style::IconRole::Prim));
    PyModule_AddIntConstant(module, "IconRedo", static_cast<int>(Style::IconRole::Redo));
    PyModule_AddIntConstant(module, "IconRight", static_cast<int>(Style::IconRole::Right));
    PyModule_AddIntConstant(module, "IconRun", static_cast<int>(Style::IconRole::Run));
    PyModule_AddIntConstant(module, "IconShaded", static_cast<int>(Style::IconRole::Shaded));
    PyModule_AddIntConstant(module, "IconUndo", static_cast<int>(Style::IconRole::Undo));
    PyModule_AddIntConstant(module, "IconVisible", static_cast<int>(Style::IconRole::Visible));
    PyModule_AddIntConstant(module, "IconWireframe", static_cast<int>(Style::IconRole::Wireframe));

    PyModule_AddIntConstant(module, "UIScaleSmall", static_cast<int>(Style::UIScale::Small));
    PyModule_AddIntConstant(module, "UIScaleMedium", static_cast<int>(Style::UIScale::Medium));
    PyModule_AddIntConstant(module, "UIScaleLarge", static_cast<int>(Style::UIScale::Large));

    PyModule_AddIntConstant(module, "UIStateNormal", static_cast<int>(Style::UIState::Normal));
    PyModule_AddIntConstant(module, "UIStateDisabled", static_cast<int>(Style::UIState::Disabled));

    return module;
}

extern "C" PyObject*
PyInit_stageviz_wrapper()
{
    return PyInit_stageviz();
}
