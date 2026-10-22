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


DIALOG_OBJECT_NAME = "stagevizCarPaintDialog"
DIALOG_GLOBAL_NAME = "_stageviz_car_paint_dialog"

CAR_PAINT_MATERIAL_PATH = "/World/Looks/CarPaint"


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


def vec3_from_qcolor(color):
    color = QtGui.QColor(color)

    return Gf.Vec3f(
        color.redF(),
        color.greenF(),
        color.blueF(),
    )


def create_car_paint_material(
    stage,
    base_color,
    roughness=0.22,
    specular=0.8,
    metalness=0.0,
    coat=1.0,
    coat_roughness=0.08,
):
    """
    Create a MaterialX Standard Surface car-paint material.

    The appearance is based on a colored base layer with a strong,
    glossy clear coat. No UVs or external textures are required.
    """

    ensure_looks_scope(stage)

    material_path = Sdf.Path(CAR_PAINT_MATERIAL_PATH)

    material = UsdShade.Material.Define(
        stage,
        material_path,
    )

    surface = UsdShade.Shader.Define(
        stage,
        material_path.AppendChild("Surface"),
    )

    surface.CreateIdAttr(
        "ND_standard_surface_surfaceshader"
    )

    surface.CreateInput(
        "base",
        Sdf.ValueTypeNames.Float,
    ).Set(1.0)

    surface.CreateInput(
        "base_color",
        Sdf.ValueTypeNames.Color3f,
    ).Set(base_color)

    surface.CreateInput(
        "metalness",
        Sdf.ValueTypeNames.Float,
    ).Set(metalness)

    surface.CreateInput(
        "specular",
        Sdf.ValueTypeNames.Float,
    ).Set(specular)

    surface.CreateInput(
        "roughness",
        Sdf.ValueTypeNames.Float,
    ).Set(roughness)

    surface.CreateInput(
        "coat",
        Sdf.ValueTypeNames.Float,
    ).Set(coat)

    surface.CreateInput(
        "coat_roughness",
        Sdf.ValueTypeNames.Float,
    ).Set(coat_roughness)

    surface.CreateOutput(
        "out",
        Sdf.ValueTypeNames.Token,
    )

    material.CreateSurfaceOutput().ConnectToSource(
        surface.ConnectableAPI(),
        "out",
    )

    return material


def apply_car_paint(
    base_color,
    roughness,
    specular,
    metalness,
    coat,
    coat_roughness,
    recursive=True,
):
    stage, session = get_stage()

    paths = selected_paths(session)

    if not paths:
        raise RuntimeError("No prims selected.")

    material = create_car_paint_material(
        stage,
        base_color=base_color,
        roughness=roughness,
        specular=specular,
        metalness=metalness,
        coat=coat,
        coat_roughness=coat_roughness,
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

        text_color = "#ffffff"

        if self._color.lightnessF() > 0.6:
            text_color = "#000000"

        self.setStyleSheet(
            "QPushButton {"
            f"background-color: {color_name};"
            f"color: {text_color};"
            "padding: 5px;"
            "}"
        )

    def choose_color(self):
        color = QtWidgets.QColorDialog.getColor(
            self._color,
            self,
            "Choose Car Paint Color",
        )

        if color.isValid():
            self.set_color(color)


class CarPaintDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(
            f"Stageviz Car Paint - {qt_name}"
        )
        self.resize(460, 390)

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
            "Create and apply a MaterialX car paint appearance "
            "with a glossy clear-coat layer."
        )
        description.setWordWrap(True)
        main_layout.addWidget(description)

        form = QtWidgets.QFormLayout()
        main_layout.addLayout(form)

        self.color_button = ColorButton(
            self.make_color(
                0.80,
                0.015,
                0.008,
            ),
            self,
        )
        form.addRow(
            "Paint color:",
            self.color_button,
        )

        self.roughness_spin = QtWidgets.QDoubleSpinBox()
        self.roughness_spin.setRange(0.0, 1.0)
        self.roughness_spin.setDecimals(3)
        self.roughness_spin.setSingleStep(0.02)
        self.roughness_spin.setValue(0.22)
        form.addRow(
            "Base roughness:",
            self.roughness_spin,
        )

        self.specular_spin = QtWidgets.QDoubleSpinBox()
        self.specular_spin.setRange(0.0, 1.0)
        self.specular_spin.setDecimals(2)
        self.specular_spin.setSingleStep(0.05)
        self.specular_spin.setValue(0.8)
        form.addRow(
            "Specular:",
            self.specular_spin,
        )

        self.metalness_spin = QtWidgets.QDoubleSpinBox()
        self.metalness_spin.setRange(0.0, 1.0)
        self.metalness_spin.setDecimals(2)
        self.metalness_spin.setSingleStep(0.05)
        self.metalness_spin.setValue(0.0)
        form.addRow(
            "Metalness:",
            self.metalness_spin,
        )

        self.coat_spin = QtWidgets.QDoubleSpinBox()
        self.coat_spin.setRange(0.0, 1.0)
        self.coat_spin.setDecimals(2)
        self.coat_spin.setSingleStep(0.05)
        self.coat_spin.setValue(1.0)
        form.addRow(
            "Clear coat:",
            self.coat_spin,
        )

        self.coat_roughness_spin = QtWidgets.QDoubleSpinBox()
        self.coat_roughness_spin.setRange(0.0, 1.0)
        self.coat_roughness_spin.setDecimals(3)
        self.coat_roughness_spin.setSingleStep(0.01)
        self.coat_roughness_spin.setValue(0.08)
        form.addRow(
            "Coat roughness:",
            self.coat_roughness_spin,
        )

        self.recursive_check = QtWidgets.QCheckBox(
            "Apply recursively to children"
        )
        self.recursive_check.setChecked(True)
        main_layout.addWidget(self.recursive_check)

        preset_group = QtWidgets.QGroupBox("Presets")
        preset_layout = QtWidgets.QHBoxLayout(
            preset_group
        )

        red_button = QtWidgets.QPushButton("Red")
        blue_button = QtWidgets.QPushButton("Blue")
        black_button = QtWidgets.QPushButton("Black")
        silver_button = QtWidgets.QPushButton("Silver")

        preset_layout.addWidget(red_button)
        preset_layout.addWidget(blue_button)
        preset_layout.addWidget(black_button)
        preset_layout.addWidget(silver_button)

        main_layout.addWidget(preset_group)

        red_button.clicked.connect(self.set_red)
        blue_button.clicked.connect(self.set_blue)
        black_button.clicked.connect(self.set_black)
        silver_button.clicked.connect(self.set_silver)

        self.status_label = QtWidgets.QLabel("")
        self.status_label.setWordWrap(True)
        main_layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        main_layout.addLayout(button_row)

        self.apply_button = QtWidgets.QPushButton(
            "Apply Car Paint"
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

    def set_common_gloss(self):
        self.roughness_spin.setValue(0.22)
        self.specular_spin.setValue(0.8)
        self.metalness_spin.setValue(0.0)
        self.coat_spin.setValue(1.0)
        self.coat_roughness_spin.setValue(0.08)

    def set_red(self):
        self.color_button.set_color(
            self.make_color(
                0.82,
                0.012,
                0.006,
            )
        )
        self.set_common_gloss()

    def set_blue(self):
        self.color_button.set_color(
            self.make_color(
                0.015,
                0.055,
                0.72,
            )
        )
        self.set_common_gloss()

    def set_black(self):
        self.color_button.set_color(
            self.make_color(
                0.008,
                0.008,
                0.008,
            )
        )
        self.roughness_spin.setValue(0.18)
        self.specular_spin.setValue(0.85)
        self.metalness_spin.setValue(0.0)
        self.coat_spin.setValue(1.0)
        self.coat_roughness_spin.setValue(0.055)

    def set_silver(self):
        self.color_button.set_color(
            self.make_color(
                0.48,
                0.50,
                0.52,
            )
        )
        self.roughness_spin.setValue(0.20)
        self.specular_spin.setValue(0.85)
        self.metalness_spin.setValue(0.65)
        self.coat_spin.setValue(1.0)
        self.coat_roughness_spin.setValue(0.07)

    def apply_material(self):
        try:
            base_color = vec3_from_qcolor(
                self.color_button.color()
            )

            count, material_path = apply_car_paint(
                base_color=base_color,
                roughness=self.roughness_spin.value(),
                specular=self.specular_spin.value(),
                metalness=self.metalness_spin.value(),
                coat=self.coat_spin.value(),
                coat_roughness=self.coat_roughness_spin.value(),
                recursive=self.recursive_check.isChecked(),
            )

            self.status_label.setText(
                f"Applied car paint to {count} prim(s).\n"
                f"Material: {material_path}"
            )

            print(
                "[Stageviz Car Paint] "
                f"Applied car paint to {count} prim(s)."
            )

        except Exception as e:
            self.status_label.setText(
                f"Error: {type(e).__name__}: {e}"
            )
            traceback.print_exc()


def show_car_paint_dialog():
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

    win = CarPaintDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


car_paint_dialog = show_car_paint_dialog()
