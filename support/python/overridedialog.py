# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from pxr import Gf, Sdf, UsdGeom, UsdShade
import stageviz

try:
    from PySide6 import QtCore, QtWidgets
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtCore, QtWidgets
    qt_name = "PyQt6"


DIALOG_OBJECT_NAME = "stagevizOverrideMaterialsDialog"
DIALOG_GLOBAL_NAME = "_stageviz_override_materials_dialog"

MATERIAL_ROOT = Sdf.Path("/Materials")


MATERIALS = {
    "Neutral Clay": {
        "name": "NeutralClay",
        "base_color": Gf.Vec3f(0.55, 0.52, 0.48),
        "metalness": 0.0,
        "roughness": 0.65,
        "specular": 0.25,
    },
    "Matte Grey": {
        "name": "MatteGrey",
        "base_color": Gf.Vec3f(0.45, 0.45, 0.45),
        "metalness": 0.0,
        "roughness": 0.85,
        "specular": 0.15,
    },
    "White": {
        "name": "White",
        "base_color": Gf.Vec3f(0.9, 0.9, 0.9),
        "metalness": 0.0,
        "roughness": 0.55,
        "specular": 0.3,
    },
    "Black": {
        "name": "Black",
        "base_color": Gf.Vec3f(0.015, 0.015, 0.015),
        "metalness": 0.0,
        "roughness": 0.55,
        "specular": 0.3,
    },
    "Red": {
        "name": "Red",
        "base_color": Gf.Vec3f(0.8, 0.025, 0.015),
        "metalness": 0.0,
        "roughness": 0.3,
        "specular": 0.6,
    },
    "Green": {
        "name": "Green",
        "base_color": Gf.Vec3f(0.03, 0.65, 0.08),
        "metalness": 0.0,
        "roughness": 0.35,
        "specular": 0.5,
    },
    "Blue": {
        "name": "Blue",
        "base_color": Gf.Vec3f(0.025, 0.08, 0.8),
        "metalness": 0.0,
        "roughness": 0.3,
        "specular": 0.6,
    },
    "Chrome": {
        "name": "Chrome",
        "base_color": Gf.Vec3f(0.8, 0.8, 0.78),
        "metalness": 1.0,
        "roughness": 0.12,
        "specular": 1.0,
    },
    "UV Checker": {
        "name": "UVChecker",
        "type": "uv_checker",
        "uv_name": "st",
    },
}


def get_session():
    try:
        return stageviz.session()
    except Exception:
        return stageviz.Session()


def get_auxiliary_and_view_state():
    session = get_session()

    stage = session.auxiliary()
    if not stage:
        raise RuntimeError("No auxiliary stage available.")

    view_state = session.viewState()
    if view_state is None:
        raise RuntimeError("No ViewState available.")

    return stage, view_state


def find_stageviz_main_window():
    try:
        app = stageviz.application()
        if hasattr(app, "window"):
            window = app.window()
            if window is not None:
                return window
    except Exception:
        traceback.print_exc()

    qt_app = QtWidgets.QApplication.instance()
    if qt_app is None:
        return None

    return qt_app.activeWindow()


def material_path(material_name):
    return MATERIAL_ROOT.AppendChild(MATERIALS[material_name]["name"])


def connect_input(shader_input, source_shader, source_output_name):
    shader_input.ConnectToSource(source_shader.ConnectableAPI(), source_output_name)


def ensure_scopes(stage):
    UsdGeom.Scope.Define(stage, MATERIAL_ROOT)


def ensure_uv_checker_material(stage, material_name):
    spec = MATERIALS[material_name]
    path = material_path(material_name)
    uv_name = spec.get("uv_name", "st")

    ensure_scopes(stage)

    material = UsdShade.Material.Define(stage, path)

    uv = UsdShade.Shader.Define(stage, path.AppendChild("UV"))
    uv.CreateIdAttr("ND_geompropvalue_vector2")
    uv.CreateInput("geomprop", Sdf.ValueTypeNames.String).Set(uv_name)
    uv.CreateOutput("out", Sdf.ValueTypeNames.Float2)

    checker = UsdShade.Shader.Define(stage, path.AppendChild("Checker"))
    checker.CreateIdAttr("ND_checkerboard_color3")
    connect_input(
        checker.CreateInput("texcoord", Sdf.ValueTypeNames.Float2),
        uv,
        "out",
    )
    checker.CreateInput("color1", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(1.0))
    checker.CreateInput("color2", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0.0))
    checker.CreateOutput("out", Sdf.ValueTypeNames.Color3f)

    surface = UsdShade.Shader.Define(stage, path.AppendChild("Surface"))
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
    spec = MATERIALS[material_name]

    if spec.get("type") == "uv_checker":
        return ensure_uv_checker_material(stage, material_name)

    path = material_path(material_name)
    ensure_scopes(stage)

    material = UsdShade.Material.Define(stage, path)

    surface = UsdShade.Shader.Define(stage, path.AppendChild("Surface"))
    surface.CreateIdAttr("ND_standard_surface_surfaceshader")
    surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
    surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f).Set(
        spec["base_color"]
    )
    surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(
        spec.get("metalness", 0.0)
    )
    surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(
        spec.get("specular", 0.5)
    )
    surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(
        spec.get("roughness", 0.4)
    )

    surface.CreateOutput("out", Sdf.ValueTypeNames.Token)
    material.CreateSurfaceOutput().ConnectToSource(surface.ConnectableAPI(), "out")

    return material


def ensure_override_material(stage, material_name):
    return ensure_standard_surface_material(stage, material_name)


def set_override_material(material_name):
    stage, view_state = get_auxiliary_and_view_state()

    if not material_name:
        view_state.setOverrideMaterial("")
        return Sdf.Path.emptyPath

    material = ensure_override_material(stage, material_name)
    path = material.GetPath()

    print(
        f"[Stageviz Override Materials] "
        f"auxiliary has {path}: {bool(stage.GetPrimAtPath(path))}"
    )

    view_state.setOverrideMaterial(str(path))
    return path


def current_override_name():
    try:
        _, view_state = get_auxiliary_and_view_state()
        current = str(view_state.overrideMaterial() or "")
    except Exception:
        return None

    if not current:
        return ""

    for name in MATERIALS:
        if str(material_path(name)) == current:
            return name

    return None


class OverrideMaterialsDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(f"Stageviz Override Materials - {qt_name}")
        self.resize(360, 420)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        layout = QtWidgets.QVBoxLayout(self)

        label = QtWidgets.QLabel("Viewport override material:")
        layout.addWidget(label)

        self.material_list = QtWidgets.QListWidget()
        self.material_list.setSelectionMode(
            QtWidgets.QAbstractItemView.SelectionMode.SingleSelection
        )
        layout.addWidget(self.material_list, 1)

        scene_item = QtWidgets.QListWidgetItem("Scene Materials")
        scene_item.setData(QtCore.Qt.ItemDataRole.UserRole, "")
        self.material_list.addItem(scene_item)

        for name in MATERIALS:
            item = QtWidgets.QListWidgetItem(name)
            item.setData(QtCore.Qt.ItemDataRole.UserRole, name)
            self.material_list.addItem(item)

        self.status_label = QtWidgets.QLabel("")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)
        button_row.addStretch(1)

        self.close_button = QtWidgets.QPushButton("Close")
        button_row.addWidget(self.close_button)

        self.material_list.itemClicked.connect(self.apply_item)
        self.material_list.itemActivated.connect(self.apply_item)
        self.close_button.clicked.connect(self.close)

        self.sync_selection()

    def sync_selection(self):
        current = current_override_name()

        for index in range(self.material_list.count()):
            item = self.material_list.item(index)
            if current == item.data(QtCore.Qt.ItemDataRole.UserRole):
                self.material_list.setCurrentItem(item)
                break

    def apply_item(self, item):
        material_name = item.data(QtCore.Qt.ItemDataRole.UserRole)

        try:
            path = set_override_material(material_name)

            if material_name:
                self.status_label.setText(
                    f"Override: {material_name}\nMaterial: {path}"
                )
                print(
                    f"[Stageviz Override Materials] "
                    f"Override set to {material_name}: {path}"
                )
            else:
                self.status_label.setText("Using authored scene materials.")
                print("[Stageviz Override Materials] Override cleared.")

        except Exception as exc:
            self.status_label.setText(
                f"Error: {type(exc).__name__}: {exc}"
            )
            traceback.print_exc()


def show_override_materials_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old_win = globals().get(DIALOG_GLOBAL_NAME)
    if old_win is not None:
        try:
            old_win.close()
            old_win.deleteLater()
        except Exception:
            pass
        globals()[DIALOG_GLOBAL_NAME] = None

    parent = find_stageviz_main_window()

    win = OverrideMaterialsDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


override_materials_dialog = show_override_materials_dialog()
