// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 - present Mikael Sundell
// https://github.com/mikaelsundell/stageviz

#include "pystyle.h"
#include "application.h"

namespace stageviz::python {

struct EnumEntry {
    const char* name;
    long value;
};

static bool
checkStyle(Style* style)
{
    if (style)
        return true;
    PyErr_SetString(PyExc_RuntimeError, "Invalid stageviz.Style");
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
addEnumToStyleType(const char* name, const EnumEntry* entries, Py_ssize_t count)
{
    PyObject* enumType = createIntEnum(name, entries, count);
    if (!enumType)
        return false;

    if (PyDict_SetItemString(PyStyleType.tp_dict, name, enumType) < 0) {
        Py_DECREF(enumType);
        return false;
    }

    // Also expose the enum members directly on Style so both forms work:
    // stageviz.Style.ColorRole.AxisX and stageviz.Style.AxisX.
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* member = PyObject_GetAttrString(enumType, entries[i].name);
        if (!member) {
            Py_DECREF(enumType);
            return false;
        }

        const int result = PyDict_SetItemString(PyStyleType.tp_dict, entries[i].name, member);
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
addStyleEnums()
{
    static const EnumEntry colorRoles[] = {
        { "Accent", static_cast<long>(Style::ColorRole::Accent) },
        { "AccentAlt", static_cast<long>(Style::ColorRole::AccentAlt) },
        { "AxisX", static_cast<long>(Style::ColorRole::AxisX) },
        { "AxisY", static_cast<long>(Style::ColorRole::AxisY) },
        { "AxisZ", static_cast<long>(Style::ColorRole::AxisZ) },
        { "Base", static_cast<long>(Style::ColorRole::Base) },
        { "BaseAlt", static_cast<long>(Style::ColorRole::BaseAlt) },
        { "Border", static_cast<long>(Style::ColorRole::Border) },
        { "BorderAlt", static_cast<long>(Style::ColorRole::BorderAlt) },
        { "Button", static_cast<long>(Style::ColorRole::Button) },
        { "ButtonAlt", static_cast<long>(Style::ColorRole::ButtonAlt) },
        { "Error", static_cast<long>(Style::ColorRole::Error) },
        { "Grid", static_cast<long>(Style::ColorRole::Grid) },
        { "Handle", static_cast<long>(Style::ColorRole::Handle) },
        { "Highlight", static_cast<long>(Style::ColorRole::Highlight) },
        { "HighlightAlt", static_cast<long>(Style::ColorRole::HighlightAlt) },
        { "Item", static_cast<long>(Style::ColorRole::Item) },
        { "ItemAlt", static_cast<long>(Style::ColorRole::ItemAlt) },
        { "Progress", static_cast<long>(Style::ColorRole::Progress) },
        { "Render", static_cast<long>(Style::ColorRole::Render) },
        { "RenderAlt", static_cast<long>(Style::ColorRole::RenderAlt) },
        { "Selection", static_cast<long>(Style::ColorRole::Selection) },
        { "SelectionAlt", static_cast<long>(Style::ColorRole::SelectionAlt) },
        { "Text", static_cast<long>(Style::ColorRole::Text) },
        { "Warning", static_cast<long>(Style::ColorRole::Warning) }
    };

    static const EnumEntry iconRoles[] = {
        { "BranchClosed", static_cast<long>(Style::IconRole::BranchClosed) },
        { "BranchOpen", static_cast<long>(Style::IconRole::BranchOpen) },
        { "Checked", static_cast<long>(Style::IconRole::Checked) },
        { "Clear", static_cast<long>(Style::IconRole::Clear) },
        { "Code", static_cast<long>(Style::IconRole::Code) },
        { "Collapse", static_cast<long>(Style::IconRole::Collapse) },
        { "Down", static_cast<long>(Style::IconRole::Down) },
        { "DropDown", static_cast<long>(Style::IconRole::DropDown) },
        { "Expand", static_cast<long>(Style::IconRole::Expand) },
        { "Export", static_cast<long>(Style::IconRole::Export) },
        { "ExportImage", static_cast<long>(Style::IconRole::ExportImage) },
        { "Follow", static_cast<long>(Style::IconRole::Follow) },
        { "FrameAll", static_cast<long>(Style::IconRole::FrameAll) },
        { "Geometry", static_cast<long>(Style::IconRole::Geometry) },
        { "Hidden", static_cast<long>(Style::IconRole::Hidden) },
        { "Left", static_cast<long>(Style::IconRole::Left) },
        { "Material", static_cast<long>(Style::IconRole::Material) },
        { "Open", static_cast<long>(Style::IconRole::Open) },
        { "PartiallyChecked", static_cast<long>(Style::IconRole::PartiallyChecked) },
        { "Payload", static_cast<long>(Style::IconRole::Payload) },
        { "Prim", static_cast<long>(Style::IconRole::Prim) },
        { "Redo", static_cast<long>(Style::IconRole::Redo) },
        { "Right", static_cast<long>(Style::IconRole::Right) },
        { "Run", static_cast<long>(Style::IconRole::Run) },
        { "Shaded", static_cast<long>(Style::IconRole::Shaded) },
        { "Transform", static_cast<long>(Style::IconRole::Transform) },
        { "Undo", static_cast<long>(Style::IconRole::Undo) },
        { "Up", static_cast<long>(Style::IconRole::Up) },
        { "Visible", static_cast<long>(Style::IconRole::Visible) },
        { "Wireframe", static_cast<long>(Style::IconRole::Wireframe) }
    };

    static const EnumEntry uiScales[] = {
        { "Small", static_cast<long>(Style::UIScale::Small) },
        { "Medium", static_cast<long>(Style::UIScale::Medium) },
        { "Large", static_cast<long>(Style::UIScale::Large) }
    };

    static const EnumEntry uiStates[] = {
        { "Normal", static_cast<long>(Style::UIState::Normal) },
        { "Disabled", static_cast<long>(Style::UIState::Disabled) }
    };

    if (!addEnumToStyleType("ColorRole", colorRoles, sizeof(colorRoles) / sizeof(colorRoles[0])))
        return false;

    if (!addEnumToStyleType("IconRole", iconRoles, sizeof(iconRoles) / sizeof(iconRoles[0])))
        return false;

    if (!addEnumToStyleType("UIScale", uiScales, sizeof(uiScales) / sizeof(uiScales[0])))
        return false;

    if (!addEnumToStyleType("UIState", uiStates, sizeof(uiStates) / sizeof(uiStates[0])))
        return false;

    PyType_Modified(&PyStyleType);
    return true;
}

static void
PyStyle_dealloc(PyStyleObject* self)
{
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject*
PyStyle_new(PyTypeObject* type, PyObject*, PyObject*)
{
    PyStyleObject* self = reinterpret_cast<PyStyleObject*>(type->tp_alloc(type, 0));
    if (self) {
        self->style = nullptr;
        if (auto* app = Application::instance())
            self->style = app->style();
    }
    return reinterpret_cast<PyObject*>(self);
}

static PyObject*
PyStyle_color(PyStyleObject* self, PyObject* args, PyObject* kwargs)
{
    if (!checkStyle(self->style))
        return nullptr;

    long role = 0;
    long state = static_cast<long>(Style::UIState::Normal);

    static const char* keywords[] = { "role", "state", nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "l|l", const_cast<char**>(keywords), &role, &state))
        return nullptr;

    const QColor color = self->style->color(static_cast<Style::ColorRole>(role), static_cast<Style::UIState>(state));
    return colorToPyTuple(color);
}

static PyObject*
PyStyle_setColor(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long role = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;

    if (!PyArg_ParseTuple(args, "liii|i", &role, &r, &g, &b, &a))
        return nullptr;

    self->style->setColor(static_cast<Style::ColorRole>(role), QColor(r, g, b, a));
    Py_RETURN_NONE;
}

static PyObject*
PyStyle_iconPath(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long role = 0;
    if (!PyArg_ParseTuple(args, "l", &role))
        return nullptr;

    const QString path = self->style->iconPath(static_cast<Style::IconRole>(role));
    return PyUnicode_FromString(path.toUtf8().constData());
}

static PyObject*
PyStyle_setIconPath(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long role = 0;
    const char* path = nullptr;

    if (!PyArg_ParseTuple(args, "ls", &role, &path))
        return nullptr;

    self->style->setIconPath(static_cast<Style::IconRole>(role), QString::fromUtf8(path));
    Py_RETURN_NONE;
}

static PyObject*
PyStyle_fontSize(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long scale = 0;
    if (!PyArg_ParseTuple(args, "l", &scale))
        return nullptr;

    return PyLong_FromLong(self->style->fontSize(static_cast<Style::UIScale>(scale)));
}

static PyObject*
PyStyle_setFontSize(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long scale = 0;
    long size = 0;

    if (!PyArg_ParseTuple(args, "ll", &scale, &size))
        return nullptr;

    self->style->setFontSize(static_cast<Style::UIScale>(scale), static_cast<int>(size));
    Py_RETURN_NONE;
}

static PyObject*
PyStyle_iconSize(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long scale = 0;
    if (!PyArg_ParseTuple(args, "l", &scale))
        return nullptr;

    return PyLong_FromLong(self->style->iconSize(static_cast<Style::UIScale>(scale)));
}

static PyObject*
PyStyle_setIconSize(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    long scale = 0;
    long size = 0;

    if (!PyArg_ParseTuple(args, "ll", &scale, &size))
        return nullptr;

    self->style->setIconSize(static_cast<Style::UIScale>(scale), static_cast<int>(size));
    Py_RETURN_NONE;
}

static PyObject*
PyStyle_styleSheet(PyStyleObject* self)
{
    if (!checkStyle(self->style))
        return nullptr;

    const QString styleSheet = self->style->styleSheet();
    return PyUnicode_FromString(styleSheet.toUtf8().constData());
}

static PyObject*
PyStyle_setStyleSheet(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
        return nullptr;

    const char* styleSheet = nullptr;
    if (!PyArg_ParseTuple(args, "s", &styleSheet))
        return nullptr;

    self->style->setStyleSheet(QString::fromUtf8(styleSheet));
    Py_RETURN_NONE;
}

static PyObject*
PyStyle_refresh(PyStyleObject* self)
{
    if (!checkStyle(self->style))
        return nullptr;

    self->style->refresh();
    Py_RETURN_NONE;
}

static PyObject*
PyStyle_colorSpace(PyStyleObject* self)
{
    if (!checkStyle(self->style))
        return nullptr;

    const QColorSpace colorSpace = self->style->colorSpace();
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
PyStyle_setColorSpace(PyStyleObject* self, PyObject* args)
{
    if (!checkStyle(self->style))
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

    self->style->setColorSpace(colorSpace);
    Py_RETURN_NONE;
}

static PyMethodDef PyStyle_methods[] = {
    { "color", reinterpret_cast<PyCFunction>(PyStyle_color), METH_VARARGS | METH_KEYWORDS,
      "Get color as (r, g, b, a)" },
    { "setColor", reinterpret_cast<PyCFunction>(PyStyle_setColor), METH_VARARGS,
      "Set color from role, r, g, b, a=255" },
    { "iconPath", reinterpret_cast<PyCFunction>(PyStyle_iconPath), METH_VARARGS,
      "Get icon resource path for a role" },
    { "setIconPath", reinterpret_cast<PyCFunction>(PyStyle_setIconPath), METH_VARARGS,
      "Set icon resource path for a role" },
    { "fontSize", reinterpret_cast<PyCFunction>(PyStyle_fontSize), METH_VARARGS,
      "Get font size for a UI scale" },
    { "setFontSize", reinterpret_cast<PyCFunction>(PyStyle_setFontSize), METH_VARARGS,
      "Set font size for a UI scale" },
    { "iconSize", reinterpret_cast<PyCFunction>(PyStyle_iconSize), METH_VARARGS,
      "Get icon size for a UI scale" },
    { "setIconSize", reinterpret_cast<PyCFunction>(PyStyle_setIconSize), METH_VARARGS,
      "Set icon size for a UI scale" },
    { "styleSheet", reinterpret_cast<PyCFunction>(PyStyle_styleSheet), METH_NOARGS,
      "Get the unresolved application stylesheet template" },
    { "setStyleSheet", reinterpret_cast<PyCFunction>(PyStyle_setStyleSheet), METH_VARARGS,
      "Set the unresolved application stylesheet template and refresh" },
    { "refresh", reinterpret_cast<PyCFunction>(PyStyle_refresh), METH_NOARGS,
      "Refresh and reapply the application stylesheet" },
    { "colorSpace", reinterpret_cast<PyCFunction>(PyStyle_colorSpace), METH_NOARGS,
      "Get the rendering color space" },
    { "setColorSpace", reinterpret_cast<PyCFunction>(PyStyle_setColorSpace), METH_VARARGS,
      "Set the rendering color space" },
    { nullptr }
};

PyTypeObject PyStyleType = { PyVarObject_HEAD_INIT(nullptr, 0) };

bool
initPyStyleType()
{
    PyStyleType.tp_name = "stageviz.Style";
    PyStyleType.tp_basicsize = sizeof(PyStyleObject);
    PyStyleType.tp_itemsize = 0;
    PyStyleType.tp_flags = Py_TPFLAGS_DEFAULT;
    PyStyleType.tp_new = PyStyle_new;
    PyStyleType.tp_dealloc = reinterpret_cast<destructor>(PyStyle_dealloc);
    PyStyleType.tp_methods = PyStyle_methods;

    if (PyType_Ready(&PyStyleType) < 0)
        return false;

    return addStyleEnums();
}

int
addPyStyleType(PyObject* module)
{
    Py_INCREF(&PyStyleType);
    if (PyModule_AddObject(module, "Style", reinterpret_cast<PyObject*>(&PyStyleType)) < 0) {
        Py_DECREF(&PyStyleType);
        return -1;
    }
    return 0;
}

PyObject*
createPyStyle(Style* style)
{
    if (!style)
        Py_RETURN_NONE;

    PyObject* args = PyTuple_New(0);
    if (!args)
        return nullptr;

    PyObject* object = PyObject_CallObject(reinterpret_cast<PyObject*>(&PyStyleType), args);
    Py_DECREF(args);

    if (!object)
        return nullptr;

    reinterpret_cast<PyStyleObject*>(object)->style = style;
    return object;
}

}  // namespace stageviz::python
