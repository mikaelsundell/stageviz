# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022 - present Mikael Sundell.
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from pxr import UsdShade, UsdGeom, Sdf, Gf
import stageviz

try:
    from PySide6 import QtWidgets, QtCore
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtWidgets, QtCore
    qt_name = "PyQt6"


MATERIALS = {
    "Car Paint - Red": {
        "path": "/World/Looks/CarPaint_Red",
        "base_color": Gf.Vec3f(0.85, 0.02, 0.01),
        "metalness": 0.0,
        "roughness": 0.28,
        "specular": 0.8,
    },
    "Car Paint - Blue": {
        "path": "/World/Looks/CarPaint_Blue",
        "base_color": Gf.Vec3f(0.02, 0.08, 0.8),
        "metalness": 0.0,
        "roughness": 0.24,
        "specular": 0.8,
    },
    "Neutral Clay": {
        "path": "/World/Looks/Clay",
        "base_color": Gf.Vec3f(0.55, 0.52, 0.48),
        "metalness": 0.0,
        "roughness": 0.65,
        "specular": 0.25,
    },
    "Matte Grey": {
        "path": "/World/Looks/MatteGrey",
        "base_color": Gf.Vec3f(0.45, 0.45, 0.45),
        "metalness": 0.0,
        "roughness": 0.85,
        "specular": 0.15,
    },
    "Black Rubber": {
        "path": "/World/Looks/BlackRubber",
        "base_color": Gf.Vec3f(0.01, 0.01, 0.01),
        "metalness": 0.0,
        "roughness": 0.72,
        "specular": 0.2,
    },
    "Chrome": {
        "path": "/World/Looks/Chrome",
        "base_color": Gf.Vec3f(0.8, 0.8, 0.78),
        "metalness": 1.0,
        "roughness": 0.12,
        "specular": 1.0,
    },
    "Glass": {
        "path": "/World/Looks/Glass",
        "base_color": Gf.Vec3f(0.7, 0.9, 1.0),
        "metalness": 0.0,
        "roughness": 0.02,
        "specular": 1.0,
        "transmission": 0.75,
        "opacity": 0.35,
    },
    "UV Checker": {
        "path": "/World/Looks/UVChecker",
        "type": "uv_checker",
        "uv_name": "st",
    },
}


def get_session():
    try:
        return stageviz.session()
    except Exception:
        return stageviz.Session()


def get_stage():
    session = get_session()
    stage = session.stage()
    if not stage:
        raise RuntimeError("No stage loaded.")
    return stage, session


def ensure_xform(stage, path):
    prim = stage.GetPrimAtPath(path)
    if prim and prim.IsValid():
        return UsdGeom.Xform(prim)
    return UsdGeom.Xform.Define(stage, path)


def ensure_looks_scope(stage):
    ensure_xform(stage, "/World")
    ensure_xform(stage, "/World/Looks")


def selected_paths(session):
    try:
        return [str(path) for path in session.paths()]
    except Exception:
        return []


def bind_material(prim, material):
    UsdShade.MaterialBindingAPI.Apply(prim).Bind(material)


def iter_bindable_prims(prim, recursive=True):
    if not prim or not prim.IsValid():
        return

    if UsdGeom.Imageable(prim):
        yield prim

    if recursive:
        for child in prim.GetChildren():
            yield from iter_bindable_prims(child, recursive=True)


def connect_input(shader_input, source_shader, source_output_name):
    shader_input.ConnectToSource(source_shader.ConnectableAPI(), source_output_name)


def ensure_uv_checker_material(stage, material_name):
    spec = MATERIALS[material_name]
    material_path = Sdf.Path(spec["path"])
    uv_name = spec.get("uv_name", "st")

    ensure_looks_scope(stage)

    material = UsdShade.Material.Define(stage, material_path)

    uv = UsdShade.Shader.Define(stage, material_path.AppendChild("UV"))
    uv.CreateIdAttr("ND_geompropvalue_vector2")
    uv.CreateInput("geomprop", Sdf.ValueTypeNames.String).Set(uv_name)
    uv.CreateOutput("out", Sdf.ValueTypeNames.Float2)

    checker = UsdShade.Shader.Define(stage, material_path.AppendChild("Checker"))
    checker.CreateIdAttr("ND_checkerboard_color3")

    connect_input(
        checker.CreateInput("texcoord", Sdf.ValueTypeNames.Float2),
        uv,
        "out",
    )

    checker.CreateInput("color1", Sdf.ValueTypeNames.Color3f).Set(
        Gf.Vec3f(1.0, 1.0, 1.0)
    )
    checker.CreateInput("color2", Sdf.ValueTypeNames.Color3f).Set(
        Gf.Vec3f(0.0, 0.0, 0.0)
    )
    checker.CreateOutput("out", Sdf.ValueTypeNames.Color3f)

    surface = UsdShade.Shader.Define(stage, material_path.AppendChild("Surface"))
    surface.CreateIdAttr("ND_standard_surface_surfaceshader")

    surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
    connect_input(
        surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f),
        checker,
        "out",
    )
    surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(0.0)
    surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.45)
    surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(0.35)

    surface.CreateOutput("out", Sdf.ValueTypeNames.Token)
    material.CreateSurfaceOutput().ConnectToSource(surface.ConnectableAPI(), "out")

    return material


def ensure_standard_surface_material(stage, material_name):
    if material_name not in MATERIALS:
        raise RuntimeError(f"Unknown material: {material_name}")

    spec = MATERIALS[material_name]

    if spec.get("type") == "uv_checker":
        return ensure_uv_checker_material(stage, material_name)

    material_path = Sdf.Path(spec["path"])

    ensure_looks_scope(stage)

    material = UsdShade.Material.Define(stage, material_path)

    surface = UsdShade.Shader.Define(stage, material_path.AppendChild("Surface"))
    surface.CreateIdAttr("ND_standard_surface_surfaceshader")

    surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
    surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f).Set(spec["base_color"])
    surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(
        spec.get("metalness", 0.0)
    )
    surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(
        spec.get("specular", 0.5)
    )
    surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(
        spec.get("roughness", 0.4)
    )

    if "transmission" in spec:
        surface.CreateInput("transmission", Sdf.ValueTypeNames.Float).Set(
            spec["transmission"]
        )

    if "opacity" in spec:
        surface.CreateInput("opacity", Sdf.ValueTypeNames.Float).Set(spec["opacity"])

    surface.CreateOutput("out", Sdf.ValueTypeNames.Token)
    material.CreateSurfaceOutput().ConnectToSource(surface.ConnectableAPI(), "out")

    return material


def apply_look(material_name, recursive=True):
    stage, session = get_stage()

    paths = selected_paths(session)
    if not paths:
        raise RuntimeError("No prims selected.")

    material = ensure_standard_surface_material(stage, material_name)

    bound_count = 0

    for path in paths:
        prim = stage.GetPrimAtPath(path)
        if not prim or not prim.IsValid():
            continue

        for target in iter_bindable_prims(prim, recursive=recursive):
            bind_material(target, material)
            bound_count += 1

    return bound_count, material.GetPath()


def find_stageviz_main_window():
    app = QtWidgets.QApplication.instance()
    if app is None:
        return None

    active = app.activeWindow()
    if active and active.objectName() != "stagevizLooksDialog":
        return active

    for widget in app.topLevelWidgets():
        if not widget.isWindow():
            continue
        if not widget.isVisible():
            continue
        if widget.objectName() == "stagevizLooksDialog":
            continue
        if isinstance(widget, QtWidgets.QDialog):
            continue

        return widget

    return active


class LooksDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName("stagevizLooksDialog")
        self.setWindowTitle(f"Stageviz Looks - {qt_name}")
        self.resize(420, 180)

        self.activated = False
        self.last_pos = None

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        app = QtWidgets.QApplication.instance()
        if app is not None:
            app.applicationStateChanged.connect(self._application_state_changed)

        layout = QtWidgets.QVBoxLayout(self)

        layout.addWidget(QtWidgets.QLabel("Apply look to selected prims:"))

        self.material_combo = QtWidgets.QComboBox()
        for name in MATERIALS.keys():
            self.material_combo.addItem(name)
        layout.addWidget(self.material_combo)

        self.recursive_check = QtWidgets.QCheckBox("Apply recursively to children")
        self.recursive_check.setChecked(True)
        layout.addWidget(self.recursive_check)

        self.status_label = QtWidgets.QLabel("")
        layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.apply_button = QtWidgets.QPushButton("Apply")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addStretch(1)
        button_row.addWidget(self.apply_button)
        button_row.addWidget(self.close_button)

        self.apply_button.clicked.connect(self.apply_material)
        self.close_button.clicked.connect(self.close)

    def _application_state_changed(self, state):
        if state == QtCore.Qt.ApplicationState.ApplicationInactive:
            self.activated = self.isVisible()
            self.last_pos = self.pos()
            self.hide()
            return

        if state == QtCore.Qt.ApplicationState.ApplicationActive:
            if self.activated:
                if self.last_pos is not None:
                    self.move(self.last_pos)

                self.show()

                if self.last_pos is not None:
                    self.move(self.last_pos)

                self.raise_()

    def apply_material(self):
        material_name = self.material_combo.currentText()
        recursive = self.recursive_check.isChecked()

        try:
            count, material_path = apply_look(material_name, recursive=recursive)
            self.status_label.setText(
                f"Applied {material_name} to {count} prim(s).\nMaterial: {material_path}"
            )
            print(f"[Stageviz Looks] Applied {material_name} to {count} prim(s).")
        except Exception as e:
            self.status_label.setText(f"Error: {type(e).__name__}: {e}")
            traceback.print_exc()


def show_looks_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old_win = globals().get("_stageviz_looks_dialog")
    if old_win is not None:
        try:
            old_win.close()
            old_win.deleteLater()
        except Exception:
            pass

        globals()["_stageviz_looks_dialog"] = None

    parent = find_stageviz_main_window()

    win = LooksDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()["_stageviz_looks_dialog"] = win

    if owns_app:
        app.exec()


show_looks_dialog()