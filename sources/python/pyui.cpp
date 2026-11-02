// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pyui.h"
#include "application.h"

namespace stageviz::python {

struct EnumEntry {
    const char* name;
    long value;
};

static bool
checkUI(UI* ui)
{
    if (ui)
        return true;
    PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.UI");
    return false;
}

static PyObject*
colorToPyTuple(const QColor& color)
{
    return Py_BuildValue("(iiii)", color.red(), color.green(), color.blue(), color.alpha());
}

static PyObject*
createIntEnum(const char* name, const EnumEntry* entries, Py_ssize_t count)
{
    PyObject* enumModule = PyImport_ImportModule("enum");
    if (!enumModule)
        return nullptr;

    PyObject* intEnum = PyObject_GetAttrString(enumModule, "IntEnum");
    Py_DECREF(enumModule);
    if (!intEnum)
        return nullptr;

    PyObject* members = PyDict_New();
    if (!members) {
        Py_DECREF(intEnum);
        return nullptr;
    }

    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* value = PyLong_FromLong(entries[i].value);
        if (!value || PyDict_SetItemString(members, entries[i].name, value) < 0) {
            Py_XDECREF(value);
            Py_DECREF(members);
            Py_DECREF(intEnum);
            return nullptr;
        }
        Py_DECREF(value);
    }

    PyObject* enumName = PyUnicode_FromString(name);
    if (!enumName) {
        Py_DECREF(members);
        Py_DECREF(intEnum);
        return nullptr;
    }

    PyObject* enumType = PyObject_CallFunctionObjArgs(intEnum, enumName, members, nullptr);

    Py_DECREF(enumName);
    Py_DECREF(members);
    Py_DECREF(intEnum);

    if (!enumType)
        return nullptr;

    PyObject* moduleName = PyUnicode_FromString("stageviz");
    if (moduleName) {
        PyObject_SetAttrString(enumType, "__module__", moduleName);
        Py_DECREF(moduleName);
    }
    else {
        PyErr_Clear();
    }

    return enumType;
}

static bool
addEnumToUIType(const char* name, const EnumEntry* entries, Py_ssize_t count)
{
    PyObject* enumType = createIntEnum(name, entries, count);
    if (!enumType)
        return false;

    if (PyDict_SetItemString(PyUIType.tp_dict, name, enumType) < 0) {
        Py_DECREF(enumType);
        return false;
    }

    // Also expose the enum members directly on UI so both forms work:
    // stageviz.UI.ColorRole.AxisX and stageviz.UI.AxisX.
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* member = PyObject_GetAttrString(enumType, entries[i].name);
        if (!member) {
            Py_DECREF(enumType);
            return false;
        }

        const int result = PyDict_SetItemString(PyUIType.tp_dict, entries[i].name, member);
        Py_DECREF(member);

        if (result < 0) {
            Py_DECREF(enumType);
            return false;
        }
    }

    Py_DECREF(enumType);
    return true;
}

static bool
addUIEnums()
{
    static const EnumEntry colorRoles[] = { { "Accent", static_cast<long>(UI::ColorRole::Accent) },
                                            { "AccentAlt", static_cast<long>(UI::ColorRole::AccentAlt) },
                                            { "AxisX", static_cast<long>(UI::ColorRole::AxisX) },
                                            { "AxisY", static_cast<long>(UI::ColorRole::AxisY) },
                                            { "AxisZ", static_cast<long>(UI::ColorRole::AxisZ) },
                                            { "Base", static_cast<long>(UI::ColorRole::Base) },
                                            { "BaseAlt", static_cast<long>(UI::ColorRole::BaseAlt) },
                                            { "Border", static_cast<long>(UI::ColorRole::Border) },
                                            { "BorderAlt", static_cast<long>(UI::ColorRole::BorderAlt) },
                                            { "Button", static_cast<long>(UI::ColorRole::Button) },
                                            { "ButtonAlt", static_cast<long>(UI::ColorRole::ButtonAlt) },
                                            { "Error", static_cast<long>(UI::ColorRole::Error) },
                                            { "Graph", static_cast<long>(UI::ColorRole::Graph) },
                                            { "Grid", static_cast<long>(UI::ColorRole::Grid) },
                                            { "Guide", static_cast<long>(UI::ColorRole::Guide) },
                                            { "Handle", static_cast<long>(UI::ColorRole::Handle) },
                                            { "Highlight", static_cast<long>(UI::ColorRole::Highlight) },
                                            { "HighlightAlt", static_cast<long>(UI::ColorRole::HighlightAlt) },
                                            { "Item", static_cast<long>(UI::ColorRole::Item) },
                                            { "ItemAlt", static_cast<long>(UI::ColorRole::ItemAlt) },
                                            { "Progress", static_cast<long>(UI::ColorRole::Progress) },
                                            { "Render", static_cast<long>(UI::ColorRole::Render) },
                                            { "RenderAlt", static_cast<long>(UI::ColorRole::RenderAlt) },
                                            { "Selection", static_cast<long>(UI::ColorRole::Selection) },
                                            { "SelectionAlt", static_cast<long>(UI::ColorRole::SelectionAlt) },
                                            { "Text", static_cast<long>(UI::ColorRole::Text) },
                                            { "TextAlt", static_cast<long>(UI::ColorRole::TextAlt) },
                                            { "Warning", static_cast<long>(UI::ColorRole::Warning) },
                                            { "Readonly", static_cast<long>(UI::ColorRole::Readonly) } };

    static const EnumEntry iconRoles[] = { { "BranchClosed", static_cast<long>(UI::IconRole::BranchClosed) },
                                           { "BranchOpen", static_cast<long>(UI::IconRole::BranchOpen) },
                                           { "Checked", static_cast<long>(UI::IconRole::Checked) },
                                           { "Clear", static_cast<long>(UI::IconRole::Clear) },
                                           { "Code", static_cast<long>(UI::IconRole::Code) },
                                           { "Collapse", static_cast<long>(UI::IconRole::Collapse) },
                                           { "DefaultPrim", static_cast<long>(UI::IconRole::DefaultPrim) },
                                           { "Down", static_cast<long>(UI::IconRole::Down) },
                                           { "DropDown", static_cast<long>(UI::IconRole::DropDown) },
                                           { "Expand", static_cast<long>(UI::IconRole::Expand) },
                                           { "Export", static_cast<long>(UI::IconRole::Export) },
                                           { "ExportImage", static_cast<long>(UI::IconRole::ExportImage) },
                                           { "Follow", static_cast<long>(UI::IconRole::Follow) },
                                           { "FrameAll", static_cast<long>(UI::IconRole::FrameAll) },
                                           { "Geometry", static_cast<long>(UI::IconRole::Geometry) },
                                           { "Hidden", static_cast<long>(UI::IconRole::Hidden) },
                                           { "Instance", static_cast<long>(UI::IconRole::Instance) },
                                           { "Layer", static_cast<long>(UI::IconRole::Layer) },
                                           { "Left", static_cast<long>(UI::IconRole::Left) },
                                           { "List", static_cast<long>(UI::IconRole::List) },
                                           { "Lock", static_cast<long>(UI::IconRole::Lock) },
                                           { "Material", static_cast<long>(UI::IconRole::Material) },
                                           { "Move", static_cast<long>(UI::IconRole::Move) },
                                           { "Nested", static_cast<long>(UI::IconRole::Nested) },
                                           { "New", static_cast<long>(UI::IconRole::New) },
                                           { "Open", static_cast<long>(UI::IconRole::Open) },
                                           { "Over", static_cast<long>(UI::IconRole::Over) },
                                           { "Override", static_cast<long>(UI::IconRole::Override) },
                                           { "PartiallyChecked", static_cast<long>(UI::IconRole::PartiallyChecked) },
                                           { "Payload", static_cast<long>(UI::IconRole::Payload) },
                                           { "Pick", static_cast<long>(UI::IconRole::Pick) },
                                           { "Pivot", static_cast<long>(UI::IconRole::Pivot) },
                                           { "Prim", static_cast<long>(UI::IconRole::Prim) },
                                           { "Redo", static_cast<long>(UI::IconRole::Redo) },
                                           { "Right", static_cast<long>(UI::IconRole::Right) },
                                           { "Rotate", static_cast<long>(UI::IconRole::Rotate) },
                                           { "Run", static_cast<long>(UI::IconRole::Run) },
                                           { "Scale", static_cast<long>(UI::IconRole::Scale) },
                                           { "Select", static_cast<long>(UI::IconRole::Select) },
                                           { "Shaded", static_cast<long>(UI::IconRole::Shaded) },
                                           { "Transform", static_cast<long>(UI::IconRole::Transform) },
                                           { "Undo", static_cast<long>(UI::IconRole::Undo) },
                                           { "Unlock", static_cast<long>(UI::IconRole::Unlock) },
                                           { "Up", static_cast<long>(UI::IconRole::Up) },
                                           { "Visible", static_cast<long>(UI::IconRole::Visible) },
                                           { "Wireframe", static_cast<long>(UI::IconRole::Wireframe) } };

    static const EnumEntry uiScales[] = { { "Small", static_cast<long>(UI::UIScale::Small) },
                                          { "Medium", static_cast<long>(UI::UIScale::Medium) },
                                          { "Large", static_cast<long>(UI::UIScale::Large) } };

    static const EnumEntry uiStates[] = { { "Normal", static_cast<long>(UI::UIState::Normal) },
                                          { "Disabled", static_cast<long>(UI::UIState::Disabled) } };

    if (!addEnumToUIType("ColorRole", colorRoles, sizeof(colorRoles) / sizeof(colorRoles[0])))
        return false;

    if (!addEnumToUIType("IconRole", iconRoles, sizeof(iconRoles) / sizeof(iconRoles[0])))
        return false;

    if (!addEnumToUIType("UIScale", uiScales, sizeof(uiScales) / sizeof(uiScales[0])))
        return false;

    if (!addEnumToUIType("UIState", uiStates, sizeof(uiStates) / sizeof(uiStates[0])))
        return false;

    PyType_Modified(&PyUIType);
    return true;
}

static void
PyUI_dealloc(PyUIObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject*
PyUI_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PyUIObject* self = reinterpret_cast<PyUIObject*>(type->tp_alloc(type, 0));
    if (self) {
        self->ui = nullptr;
        if (auto* app = Application::instance())
            self->ui = app->ui();
    }
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PyUI_color(PyUIObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkUI(self->ui))
        return nullptr;

    long role = 0;
    long state = static_cast<long>(UI::UIState::Normal);

    static const char* keywords[] = { "role", "state", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "l|l", const_cast<char**>(keywords), &role, &state))
        return nullptr;

    const QColor color = self->ui->color(static_cast<UI::ColorRole>(role), static_cast<UI::UIState>(state));
    return colorToPyTuple(color);
}

static PyObject*
PyUI_setColor(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long role = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;

    if (!PyArg_ParseTuple(args, "liii|i", &role, &r, &g, &b, &a))
        return nullptr;

    self->ui->setColor(static_cast<UI::ColorRole>(role), QColor(r, g, b, a));
    Py_RETURN_NONE;
}

static PyObject*
PyUI_iconPath(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long role = 0;
    if (!PyArg_ParseTuple(args, "l", &role))
        return nullptr;

    const QString path = self->ui->iconPath(static_cast<UI::IconRole>(role));
    return PyUnicode_FromString(path.toUtf8().constData());
}

static PyObject*
PyUI_setIconPath(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long role = 0;
    const char* path = nullptr;

    if (!PyArg_ParseTuple(args, "ls", &role, &path))
        return nullptr;

    self->ui->setIconPath(static_cast<UI::IconRole>(role), QString::fromUtf8(path));
    Py_RETURN_NONE;
}

static PyObject*
PyUI_fontSize(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long scale = 0;
    if (!PyArg_ParseTuple(args, "l", &scale))
        return nullptr;

    return PyLong_FromLong(self->ui->fontSize(static_cast<UI::UIScale>(scale)));
}

static PyObject*
PyUI_setFontSize(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long scale = 0;
    long size = 0;

    if (!PyArg_ParseTuple(args, "ll", &scale, &size))
        return nullptr;

    self->ui->setFontSize(static_cast<UI::UIScale>(scale), static_cast<int>(size));
    Py_RETURN_NONE;
}

static PyObject*
PyUI_iconSize(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long scale = 0;
    if (!PyArg_ParseTuple(args, "l", &scale))
        return nullptr;

    return PyLong_FromLong(self->ui->iconSize(static_cast<UI::UIScale>(scale)));
}

static PyObject*
PyUI_setIconSize(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    long scale = 0;
    long size = 0;

    if (!PyArg_ParseTuple(args, "ll", &scale, &size))
        return nullptr;

    self->ui->setIconSize(static_cast<UI::UIScale>(scale), static_cast<int>(size));
    Py_RETURN_NONE;
}

static PyObject*
PyUI_gizmoSize(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;
    long scale = 0;
    if (!PyArg_ParseTuple(args, "l", &scale))
        return nullptr;
    return PyLong_FromLong(self->ui->gizmoSize(static_cast<UI::UIScale>(scale)));
}

static PyObject*
PyUI_setGizmoSize(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;
    long scale = 0;
    long size = 0;
    if (!PyArg_ParseTuple(args, "ll", &scale, &size))
        return nullptr;
    self->ui->setGizmoSize(static_cast<UI::UIScale>(scale), static_cast<int>(size));
    Py_RETURN_NONE;
}

static PyObject*
PyUI_transformMoveSnap(PyUIObject* self, PyObject*)
{
    if (!checkUI(self->ui))
        return nullptr;
    return PyFloat_FromDouble(self->ui->transformMoveSnap());
}

static PyObject*
PyUI_setTransformMoveSnap(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;
    double value = 0.0;
    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;
    self->ui->setTransformMoveSnap(value);
    Py_RETURN_NONE;
}

static PyObject*
PyUI_transformRotateSnap(PyUIObject* self, PyObject*)
{
    if (!checkUI(self->ui))
        return nullptr;
    return PyFloat_FromDouble(self->ui->transformRotateSnap());
}

static PyObject*
PyUI_setTransformRotateSnap(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;
    double value = 0.0;
    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;
    self->ui->setTransformRotateSnap(value);
    Py_RETURN_NONE;
}

static PyObject*
PyUI_transformScaleSnap(PyUIObject* self, PyObject*)
{
    if (!checkUI(self->ui))
        return nullptr;
    return PyFloat_FromDouble(self->ui->transformScaleSnap());
}

static PyObject*
PyUI_setTransformScaleSnap(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;
    double value = 0.0;
    if (!PyArg_ParseTuple(args, "d", &value))
        return nullptr;
    self->ui->setTransformScaleSnap(value);
    Py_RETURN_NONE;
}

static PyObject*
PyUI_styleSheet(PyUIObject* self, PyObject*)
{
    if (!checkUI(self->ui))
        return nullptr;

    const QString styleSheet = self->ui->styleSheet();
    return PyUnicode_FromString(styleSheet.toUtf8().constData());
}

static PyObject*
PyUI_setStyleSheet(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    const char* styleSheet = nullptr;
    if (!PyArg_ParseTuple(args, "s", &styleSheet))
        return nullptr;

    self->ui->setStyleSheet(QString::fromUtf8(styleSheet));
    Py_RETURN_NONE;
}

static PyObject*
PyUI_refresh(PyUIObject* self, PyObject*)
{
    if (!checkUI(self->ui))
        return nullptr;

    self->ui->refresh();
    Py_RETURN_NONE;
}

static PyObject*
PyUI_colorSpace(PyUIObject* self, PyObject*)
{
    if (!checkUI(self->ui))
        return nullptr;

    const QColorSpace colorSpace = self->ui->colorSpace();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (!colorSpace.isValid())
        Py_RETURN_NONE;

    const QString description = colorSpace.description();
    if (!description.isEmpty())
        return PyUnicode_FromString(description.toUtf8().constData());

    return PyLong_FromLong(static_cast<long>(colorSpace.primaries()));
#else
    Q_UNUSED(self)
    Py_RETURN_NONE;
#endif
}

static PyObject*
PyUI_setColorSpace(PyUIObject* self, PyObject* args)
{
    if (!checkUI(self->ui))
        return nullptr;

    const char* name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;

    const QString colorSpaceName = QString::fromUtf8(name);
    QColorSpace colorSpace;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (colorSpaceName.compare("srgb", Qt::CaseInsensitive) == 0) {
        colorSpace = QColorSpace::SRgb;
    }
    else if (colorSpaceName.compare("displayp3", Qt::CaseInsensitive) == 0
             || colorSpaceName.compare("display-p3", Qt::CaseInsensitive) == 0) {
        colorSpace = QColorSpace::DisplayP3;
    }
    else if (colorSpaceName.compare("adobergb", Qt::CaseInsensitive) == 0
             || colorSpaceName.compare("adobe-rgb", Qt::CaseInsensitive) == 0) {
        colorSpace = QColorSpace::AdobeRgb;
    }
    else if (colorSpaceName.compare("bt2020", Qt::CaseInsensitive) == 0
             || colorSpaceName.compare("rec2020", Qt::CaseInsensitive) == 0) {
        colorSpace = QColorSpace::Bt2020;
    }
    else {
        PyErr_SetString(PyExc_ValueError, "Unsupported color space name");
        return nullptr;
    }
#else
    PyErr_SetString(PyExc_RuntimeError, "QColorSpace not supported");
    return nullptr;
#endif

    self->ui->setColorSpace(colorSpace);
    Py_RETURN_NONE;
}

static PyMethodDef PyUI_methods[] = {
    { "color", reinterpret_cast<PyCFunction>(PyUI_color), METH_VARARGS | METH_KEYWORDS, "Get color as (r, g, b, a)" },
    { "setColor", reinterpret_cast<PyCFunction>(PyUI_setColor), METH_VARARGS, "Set color from role, r, g, b, a=255" },
    { "iconPath", reinterpret_cast<PyCFunction>(PyUI_iconPath), METH_VARARGS, "Get icon resource path for a role" },
    { "setIconPath", reinterpret_cast<PyCFunction>(PyUI_setIconPath), METH_VARARGS,
      "Set icon resource path for a role" },
    { "fontSize", reinterpret_cast<PyCFunction>(PyUI_fontSize), METH_VARARGS, "Get font size for a UI scale" },
    { "setFontSize", reinterpret_cast<PyCFunction>(PyUI_setFontSize), METH_VARARGS, "Set font size for a UI scale" },
    { "iconSize", reinterpret_cast<PyCFunction>(PyUI_iconSize), METH_VARARGS, "Get icon size for a UI scale" },
    { "setIconSize", reinterpret_cast<PyCFunction>(PyUI_setIconSize), METH_VARARGS, "Set icon size for a UI scale" },
    { "gizmoSize", reinterpret_cast<PyCFunction>(PyUI_gizmoSize), METH_VARARGS, "Get gizmo size for a UI scale" },
    { "setGizmoSize", reinterpret_cast<PyCFunction>(PyUI_setGizmoSize), METH_VARARGS, "Set gizmo size for a UI scale" },
    { "transformMoveSnap", reinterpret_cast<PyCFunction>(PyUI_transformMoveSnap), METH_NOARGS,
      "Get transform move snapping increment" },
    { "setTransformMoveSnap", reinterpret_cast<PyCFunction>(PyUI_setTransformMoveSnap), METH_VARARGS,
      "Set transform move snapping increment" },
    { "transformRotateSnap", reinterpret_cast<PyCFunction>(PyUI_transformRotateSnap), METH_NOARGS,
      "Get transform rotation snapping increment in degrees" },
    { "setTransformRotateSnap", reinterpret_cast<PyCFunction>(PyUI_setTransformRotateSnap), METH_VARARGS,
      "Set transform rotation snapping increment in degrees" },
    { "transformScaleSnap", reinterpret_cast<PyCFunction>(PyUI_transformScaleSnap), METH_NOARGS,
      "Get transform scale snapping increment" },
    { "setTransformScaleSnap", reinterpret_cast<PyCFunction>(PyUI_setTransformScaleSnap), METH_VARARGS,
      "Set transform scale snapping increment" },
    { "styleSheet", reinterpret_cast<PyCFunction>(PyUI_styleSheet), METH_NOARGS,
      "Get the unresolved application stylesheet template" },
    { "setStyleSheet", reinterpret_cast<PyCFunction>(PyUI_setStyleSheet), METH_VARARGS,
      "Set the unresolved application stylesheet template and refresh" },
    { "refresh", reinterpret_cast<PyCFunction>(PyUI_refresh), METH_NOARGS,
      "Refresh and reapply the application stylesheet" },
    { "colorSpace", reinterpret_cast<PyCFunction>(PyUI_colorSpace), METH_NOARGS, "Get the rendering color space" },
    { "setColorSpace", reinterpret_cast<PyCFunction>(PyUI_setColorSpace), METH_VARARGS,
      "Set the rendering color space" },
    { nullptr }
};

PyTypeObject PyUIType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPyUIType()
{
    PyUIType.tp_name = "stageviz.UI";
    PyUIType.tp_basicsize = sizeof(PyUIObject);
    PyUIType.tp_itemsize = 0;
    PyUIType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyUIType.tp_new = PyUI_new;
    PyUIType.tp_dealloc = reinterpret_cast<destructor>(PyUI_dealloc);
    PyUIType.tp_methods = PyUI_methods;

    if (PyType_Ready(&PyUIType) < 0)
        return false;

    return addUIEnums();
}

int
addPyUIType(PyObject* module)
{
    Py_INCREF(&PyUIType);
    if (PyModule_AddObject(module, "UI", reinterpret_cast<PyObject*>(&PyUIType)) < 0) {
        Py_DECREF(&PyUIType);
        return -1;
    }
    return 0;
}

PyObject*
createPyUI(UI* ui)
{
    if (!ui)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PyUIType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PyUIObject*>(object)->ui = ui;
    return object;
}

}  // namespace stageviz::python
