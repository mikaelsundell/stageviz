# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022 - present Mikael Sundell.
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from pxr import UsdShade, UsdGeom, Sdf, Gf
import stageviz

try:
    from PySide6 import QtWidgets, QtCore, QtGui
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtWidgets, QtCore, QtGui
    qt_name = "PyQt6"


DIALOG_OBJECT_NAME = "stagevizWoodDialog"
DIALOG_GLOBAL_NAME = "_stageviz_wood_dialog"

WOOD_MATERIAL_PATH = "/World/Looks/ProceduralWood"


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
    shader_input.ConnectToSource(
        source_shader.ConnectableAPI(),
        source_output_name,
    )


def vec3_from_qcolor(color):
    color = QtGui.QColor(color)

    return Gf.Vec3f(
        color.redF(),
        color.greenF(),
        color.blueF(),
    )


def create_wood_material(
    stage,
    light_color,
    dark_color,
    grain_scale=5.0,
    grain_frequency=18.0,
    distortion=0.35,
    roughness=0.45,
):
    ensure_looks_scope(stage)

    material_path = Sdf.Path(WOOD_MATERIAL_PATH)

    material = UsdShade.Material.Define(
        stage,
        material_path,
    )

    position = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Position"),
    )
    position.CreateIdAttr("ND_position_vector3")
    position.CreateInput("space", Sdf.ValueTypeNames.String).Set("object")
    position.CreateOutput("out", Sdf.ValueTypeNames.Float3)

    scale = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("ScalePosition"),
    )
    scale.CreateIdAttr("ND_multiply_vector3")
    connect_input(
        scale.CreateInput("in1", Sdf.ValueTypeNames.Float3),
        position,
        "out",
    )
    scale.CreateInput("in2", Sdf.ValueTypeNames.Float3).Set(
        Gf.Vec3f(
            grain_scale,
            grain_scale,
            grain_scale,
        )
    )
    scale.CreateOutput("out", Sdf.ValueTypeNames.Float3)

    noise = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Noise"),
    )
    noise.CreateIdAttr("ND_noise3d_float")
    connect_input(
        noise.CreateInput("position", Sdf.ValueTypeNames.Float3),
        scale,
        "out",
    )
    noise.CreateInput("amplitude", Sdf.ValueTypeNames.Float).Set(1.0)
    noise.CreateInput("pivot", Sdf.ValueTypeNames.Float).Set(0.0)
    noise.CreateOutput("out", Sdf.ValueTypeNames.Float)

    position_x = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("PositionX"),
    )
    position_x.CreateIdAttr("ND_extract_vector3")
    connect_input(
        position_x.CreateInput("in", Sdf.ValueTypeNames.Float3),
        scale,
        "out",
    )
    position_x.CreateInput(
        "index",
        Sdf.ValueTypeNames.Int,
    ).Set(0)
    position_x.CreateOutput("out", Sdf.ValueTypeNames.Float)

    frequency = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Frequency"),
    )
    frequency.CreateIdAttr("ND_multiply_float")
    connect_input(
        frequency.CreateInput("in1", Sdf.ValueTypeNames.Float),
        position_x,
        "out",
    )
    frequency.CreateInput("in2", Sdf.ValueTypeNames.Float).Set(grain_frequency)
    frequency.CreateOutput("out", Sdf.ValueTypeNames.Float)

    distort = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Distortion"),
    )
    distort.CreateIdAttr("ND_multiply_float")
    connect_input(
        distort.CreateInput("in1", Sdf.ValueTypeNames.Float),
        noise,
        "out",
    )
    distort.CreateInput("in2", Sdf.ValueTypeNames.Float).Set(
        distortion * grain_frequency
    )
    distort.CreateOutput("out", Sdf.ValueTypeNames.Float)

    grain_coord = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("GrainCoordinate"),
    )
    grain_coord.CreateIdAttr("ND_add_float")
    connect_input(
        grain_coord.CreateInput("in1", Sdf.ValueTypeNames.Float),
        frequency,
        "out",
    )
    connect_input(
        grain_coord.CreateInput("in2", Sdf.ValueTypeNames.Float),
        distort,
        "out",
    )
    grain_coord.CreateOutput("out", Sdf.ValueTypeNames.Float)

    sine = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("GrainSine"),
    )
    sine.CreateIdAttr("ND_sin_float")
    connect_input(
        sine.CreateInput("in", Sdf.ValueTypeNames.Float),
        grain_coord,
        "out",
    )
    sine.CreateOutput("out", Sdf.ValueTypeNames.Float)

    add_one = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("AddOne"),
    )
    add_one.CreateIdAttr("ND_add_float")
    connect_input(
        add_one.CreateInput("in1", Sdf.ValueTypeNames.Float),
        sine,
        "out",
    )
    add_one.CreateInput("in2", Sdf.ValueTypeNames.Float).Set(1.0)
    add_one.CreateOutput("out", Sdf.ValueTypeNames.Float)

    normalize = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Normalize"),
    )
    normalize.CreateIdAttr("ND_multiply_float")
    connect_input(
        normalize.CreateInput("in1", Sdf.ValueTypeNames.Float),
        add_one,
        "out",
    )
    normalize.CreateInput("in2", Sdf.ValueTypeNames.Float).Set(0.5)
    normalize.CreateOutput("out", Sdf.ValueTypeNames.Float)

    wood_color = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("WoodColor"),
    )
    wood_color.CreateIdAttr("ND_mix_color3")
    wood_color.CreateInput("fg", Sdf.ValueTypeNames.Color3f).Set(light_color)
    wood_color.CreateInput("bg", Sdf.ValueTypeNames.Color3f).Set(dark_color)
    connect_input(
        wood_color.CreateInput("mix", Sdf.ValueTypeNames.Float),
        normalize,
        "out",
    )
    wood_color.CreateOutput("out", Sdf.ValueTypeNames.Color3f)

    surface = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Surface"),
    )
    surface.CreateIdAttr("ND_standard_surface_surfaceshader")
    surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
    connect_input(
        surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f),
        wood_color,
        "out",
    )
    surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(0.0)
    surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(0.35)
    surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(roughness)
    surface.CreateOutput("out", Sdf.ValueTypeNames.Token)

    material.CreateSurfaceOutput().ConnectToSource(
        surface.ConnectableAPI(),
        "out",
    )

    return material


def apply_wood(
    light_color,
    dark_color,
    grain_scale,
    grain_frequency,
    distortion,
    roughness,
    recursive=True,
):
    stage, session = get_stage()

    paths = selected_paths(session)

    if not paths:
        raise RuntimeError("No prims selected.")

    material = create_wood_material(
        stage,
        light_color=light_color,
        dark_color=dark_color,
        grain_scale=grain_scale,
        grain_frequency=grain_frequency,
        distortion=distortion,
        roughness=roughness,
    )

    bound_count = 0

    for path in paths:
        prim = stage.GetPrimAtPath(path)

        if not prim or not prim.IsValid():
            continue

        for target in iter_bindable_prims(
            prim,
            recursive=recursive,
        ):
            bind_material(target, material)
            bound_count += 1

    return bound_count, material.GetPath()


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


class ColorButton(QtWidgets.QPushButton):
    colorChanged = QtCore.Signal(object)

    def __init__(self, color, parent=None):
        super().__init__(parent)

        self._color = QtGui.QColor(color)

        self.clicked.connect(self.choose_color)

        self.update_style()

    def color(self):
        return QtGui.QColor(self._color)

    def set_color(self, color):
        self._color = QtGui.QColor(color)

        self.update_style()
        self.colorChanged.emit(self._color)

    def update_style(self):
        color_name = self._color.name()

        self.setText(color_name.upper())

        self.setStyleSheet(
            "QPushButton {"
            f"background-color: {color_name};"
            "padding: 5px;"
            "}"
        )

    def choose_color(self):
        color = QtWidgets.QColorDialog.getColor(
            self._color,
            self,
            "Choose Wood Color",
        )

        if color.isValid():
            self.set_color(color)


class WoodDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(
            f"Stageviz Procedural Wood - {qt_name}"
        )
        self.resize(460, 370)

        self.setWindowModality(
            QtCore.Qt.WindowModality.NonModal
        )
        self.setWindowFlag(
            QtCore.Qt.WindowType.Tool,
            True,
        )
        self.setAttribute(
            QtCore.Qt.WidgetAttribute.WA_DeleteOnClose,
            True,
        )

        main_layout = QtWidgets.QVBoxLayout(self)

        description = QtWidgets.QLabel(
            "Create and apply a procedural MaterialX "
            "wood appearance to the selected prims."
        )
        description.setWordWrap(True)
        main_layout.addWidget(description)

        form = QtWidgets.QFormLayout()
        main_layout.addLayout(form)

        self.light_color_button = ColorButton(
            self.make_color(
                0.55,
                0.22,
                0.065,
            ),
            self,
        )

        self.dark_color_button = ColorButton(
            self.make_color(
                0.12,
                0.025,
                0.008,
            ),
            self,
        )

        form.addRow(
            "Light wood:",
            self.light_color_button,
        )
        form.addRow(
            "Dark wood:",
            self.dark_color_button,
        )

        self.scale_spin = QtWidgets.QDoubleSpinBox()
        self.scale_spin.setRange(0.01, 100.0)
        self.scale_spin.setDecimals(2)
        self.scale_spin.setSingleStep(0.25)
        self.scale_spin.setValue(5.0)
        form.addRow(
            "Grain scale:",
            self.scale_spin,
        )

        self.frequency_spin = QtWidgets.QDoubleSpinBox()
        self.frequency_spin.setRange(0.1, 200.0)
        self.frequency_spin.setDecimals(1)
        self.frequency_spin.setSingleStep(1.0)
        self.frequency_spin.setValue(18.0)
        form.addRow(
            "Grain frequency:",
            self.frequency_spin,
        )

        self.distortion_spin = QtWidgets.QDoubleSpinBox()
        self.distortion_spin.setRange(0.0, 3.0)
        self.distortion_spin.setDecimals(2)
        self.distortion_spin.setSingleStep(0.05)
        self.distortion_spin.setValue(0.35)
        form.addRow(
            "Distortion:",
            self.distortion_spin,
        )

        self.roughness_spin = QtWidgets.QDoubleSpinBox()
        self.roughness_spin.setRange(0.0, 1.0)
        self.roughness_spin.setDecimals(2)
        self.roughness_spin.setSingleStep(0.05)
        self.roughness_spin.setValue(0.45)
        form.addRow(
            "Roughness:",
            self.roughness_spin,
        )

        self.recursive_check = QtWidgets.QCheckBox(
            "Apply recursively to children"
        )
        self.recursive_check.setChecked(True)
        main_layout.addWidget(
            self.recursive_check
        )

        preset_group = QtWidgets.QGroupBox("Presets")
        preset_layout = QtWidgets.QHBoxLayout(
            preset_group
        )

        oak_button = QtWidgets.QPushButton("Oak")
        walnut_button = QtWidgets.QPushButton("Walnut")
        mahogany_button = QtWidgets.QPushButton("Mahogany")

        preset_layout.addWidget(oak_button)
        preset_layout.addWidget(walnut_button)
        preset_layout.addWidget(mahogany_button)

        main_layout.addWidget(preset_group)

        oak_button.clicked.connect(self.set_oak)
        walnut_button.clicked.connect(self.set_walnut)
        mahogany_button.clicked.connect(self.set_mahogany)

        self.status_label = QtWidgets.QLabel("")
        self.status_label.setWordWrap(True)
        main_layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        main_layout.addLayout(button_row)

        self.apply_button = QtWidgets.QPushButton(
            "Apply Wood"
        )
        self.close_button = QtWidgets.QPushButton(
            "Close"
        )

        button_row.addStretch(1)
        button_row.addWidget(self.apply_button)
        button_row.addWidget(self.close_button)

        self.apply_button.clicked.connect(
            self.apply_material
        )
        self.close_button.clicked.connect(
            self.close
        )

    @staticmethod
    def make_color(r, g, b):
        return QtGui.QColor.fromRgbF(
            r,
            g,
            b,
            1.0,
        )

    def set_oak(self):
        self.light_color_button.set_color(
            self.make_color(
                0.65,
                0.36,
                0.12,
            )
        )
        self.dark_color_button.set_color(
            self.make_color(
                0.20,
                0.07,
                0.015,
            )
        )
        self.scale_spin.setValue(4.0)
        self.frequency_spin.setValue(20.0)
        self.distortion_spin.setValue(0.28)
        self.roughness_spin.setValue(0.48)

    def set_walnut(self):
        self.light_color_button.set_color(
            self.make_color(
                0.30,
                0.10,
                0.035,
            )
        )
        self.dark_color_button.set_color(
            self.make_color(
                0.055,
                0.012,
                0.006,
            )
        )
        self.scale_spin.setValue(4.5)
        self.frequency_spin.setValue(16.0)
        self.distortion_spin.setValue(0.45)
        self.roughness_spin.setValue(0.42)

    def set_mahogany(self):
        self.light_color_button.set_color(
            self.make_color(
                0.48,
                0.095,
                0.035,
            )
        )
        self.dark_color_button.set_color(
            self.make_color(
                0.12,
                0.012,
                0.006,
            )
        )
        self.scale_spin.setValue(5.5)
        self.frequency_spin.setValue(22.0)
        self.distortion_spin.setValue(0.32)
        self.roughness_spin.setValue(0.38)

    def apply_material(self):
        try:
            light_color = vec3_from_qcolor(
                self.light_color_button.color()
            )

            dark_color = vec3_from_qcolor(
                self.dark_color_button.color()
            )

            count, material_path = apply_wood(
                light_color=light_color,
                dark_color=dark_color,
                grain_scale=self.scale_spin.value(),
                grain_frequency=self.frequency_spin.value(),
                distortion=self.distortion_spin.value(),
                roughness=self.roughness_spin.value(),
                recursive=self.recursive_check.isChecked(),
            )

            self.status_label.setText(
                f"Applied procedural wood to {count} prim(s).\n"
                f"Material: {material_path}"
            )

            print(
                "[Stageviz Wood] "
                f"Applied procedural wood to {count} prim(s)."
            )

        except Exception as e:
            self.status_label.setText(
                f"Error: {type(e).__name__}: {e}"
            )
            traceback.print_exc()


def show_wood_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old_win = globals().get(
        DIALOG_GLOBAL_NAME
    )

    if old_win is not None:
        try:
            old_win.close()
            old_win.deleteLater()
        except Exception:
            pass

        globals()[DIALOG_GLOBAL_NAME] = None

    parent = find_stageviz_main_window()

    win = WoodDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


wood_dialog = show_wood_dialog()
