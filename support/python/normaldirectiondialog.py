# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade
import stageviz

try:
    from PySide6 import QtCore, QtWidgets
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtCore, QtWidgets
    qt_name = "PyQt6"


DIALOG_OBJECT_NAME = "stagevizNormalDirectionDialog"
DIALOG_GLOBAL_NAME = "_stageviz_normal_direction_dialog"

MATERIAL_ROOT = Sdf.Path("/Materials")
MATERIAL_PATH = Sdf.Path("/Materials/NormalDirection")

# Darker, clay-like diagnostic colors so lighting/shading remains readable.
FRONT_COLOR = Gf.Vec3f(0.12, 0.50, 0.16)
BACK_COLOR = Gf.Vec3f(0.62, 0.10, 0.32)

MATERIAL_MODE_ALL = 0
MATERIAL_MODE_OVERRIDE = 2

DOUBLE_SIDED_PRIMITIVE = 0
DOUBLE_SIDED = 1
SINGLE_SIDED = 2

# Saved while the normal-direction diagnostic is active so the user's
# previous viewport culling mode can be restored afterward.
_previous_double_sided_mode = None
_normal_check_active = False


def get_session():
    try:
        return stageviz.session()
    except Exception:
        return stageviz.Session()


def get_context():
    session = get_session()

    stage = session.stage()
    if not stage:
        raise RuntimeError("No document stage available.")

    auxiliary = session.auxiliary()
    if not auxiliary:
        raise RuntimeError("No auxiliary stage available.")

    view_state = session.viewState()
    if view_state is None:
        raise RuntimeError("No ViewState available.")

    selection = session.selectionList()
    if selection is None:
        raise RuntimeError("No SelectionList available.")

    return session, stage, auxiliary, view_state, selection


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


def connect_input(shader_input, source_shader, source_output_name="out"):
    shader_input.ConnectToSource(
        source_shader.ConnectableAPI(),
        source_output_name,
    )


def ensure_normal_direction_material(stage):
    """
    Create the normal-direction diagnostic material.

    MaterialX graph:

        ND_facingratio_float
            faceforward = false
                    |
                    v
        ND_ifgreater_color3
          > 0 -> green
         <= 0 -> magenta
                    |
                    v
        ND_standard_surface_surfaceshader
               base_color

    The final Standard Surface uses the same material response as the
    Stageviz clay material:
        base       = 1.0
        metalness  = 0.0
        roughness  = 0.68
        specular   = 0.35
    """
    UsdGeom.Scope.Define(stage, MATERIAL_ROOT)

    material = UsdShade.Material.Define(stage, MATERIAL_PATH)

    facing = UsdShade.Shader.Define(
        stage,
        MATERIAL_PATH.AppendChild("FacingRatio"),
    )
    facing.CreateIdAttr("ND_facingratio_float")

    facing.CreateInput(
        "faceforward",
        Sdf.ValueTypeNames.Bool,
    ).Set(False)

    facing.CreateInput(
        "invert",
        Sdf.ValueTypeNames.Bool,
    ).Set(False)

    facing.CreateOutput(
        "out",
        Sdf.ValueTypeNames.Float,
    )

    face_color = UsdShade.Shader.Define(
        stage,
        MATERIAL_PATH.AppendChild("FaceColor"),
    )
    face_color.CreateIdAttr("ND_ifgreater_color3")

    connect_input(
        face_color.CreateInput(
            "value1",
            Sdf.ValueTypeNames.Float,
        ),
        facing,
    )

    face_color.CreateInput(
        "value2",
        Sdf.ValueTypeNames.Float,
    ).Set(0.0)

    face_color.CreateInput(
        "in1",
        Sdf.ValueTypeNames.Color3f,
    ).Set(FRONT_COLOR)

    face_color.CreateInput(
        "in2",
        Sdf.ValueTypeNames.Color3f,
    ).Set(BACK_COLOR)

    face_color.CreateOutput(
        "out",
        Sdf.ValueTypeNames.Color3f,
    )

    surface = UsdShade.Shader.Define(
        stage,
        MATERIAL_PATH.AppendChild("Surface"),
    )
    surface.CreateIdAttr("ND_standard_surface_surfaceshader")

    surface.CreateInput(
        "base",
        Sdf.ValueTypeNames.Float,
    ).Set(1.0)

    connect_input(
        surface.CreateInput(
            "base_color",
            Sdf.ValueTypeNames.Color3f,
        ),
        face_color,
    )

    surface.CreateInput(
        "metalness",
        Sdf.ValueTypeNames.Float,
    ).Set(0.0)

    surface.CreateInput(
        "roughness",
        Sdf.ValueTypeNames.Float,
    ).Set(0.68)

    surface.CreateInput(
        "specular",
        Sdf.ValueTypeNames.Float,
    ).Set(0.35)

    surface.CreateOutput(
        "out",
        Sdf.ValueTypeNames.Token,
    )

    material.CreateSurfaceOutput().ConnectToSource(
        surface.ConnectableAPI(),
        "out",
    )

    return material


def enable_normal_direction_override():
    global _previous_double_sided_mode
    global _normal_check_active

    _, _, auxiliary, view_state, _ = get_context()

    material = ensure_normal_direction_material(auxiliary)
    path = material.GetPath()

    # Save the user's original state only once, on transition into
    # Normal Check mode.
    if not _normal_check_active:
        _previous_double_sided_mode = view_state.doubleSidedMode()

    # Force SingleSided rendering regardless of the primitive's authored USD doubleSided.
    # This maps to CULL_STYLE_BACK in ImagingGLWidget.
    view_state.setDoubleSidedMode(SINGLE_SIDED)

    view_state.setOverrideMaterial(str(path))
    view_state.setMaterialMode(MATERIAL_MODE_OVERRIDE)

    _normal_check_active = True

    print(
        "[Stageviz Normal Direction] "
        f"override enabled: {path}; "
        "double-sided mode forced to SingleSided"
    )

    return path

def disable_normal_direction_override():
    global _previous_double_sided_mode
    global _normal_check_active

    _, _, _, view_state, _ = get_context()

    view_state.setOverrideMaterial("")
    view_state.setMaterialMode(MATERIAL_MODE_ALL)

    if _normal_check_active and _previous_double_sided_mode is not None:
        view_state.setDoubleSidedMode(
            int(_previous_double_sided_mode)
        )

    restored = _previous_double_sided_mode

    _previous_double_sided_mode = None
    _normal_check_active = False

    print(
        "[Stageviz Normal Direction] "
        "override disabled; "
        f"double-sided mode restored to {restored}"
    )

def selected_meshes(stage, selection):
    """
    Return unique UsdGeom.Mesh prims under the current selection.

    Selecting a mesh flips that mesh.
    Selecting an Xform/group flips all descendant meshes.
    """
    result = []
    seen = set()

    for path in selection.paths():
        prim = stage.GetPrimAtPath(path)
        if not prim:
            continue

        for candidate in Usd.PrimRange(prim):
            if not candidate or not candidate.IsA(UsdGeom.Mesh):
                continue

            candidate_path = candidate.GetPath()
            key = str(candidate_path)

            if key in seen:
                continue

            seen.add(key)
            result.append(UsdGeom.Mesh(candidate))

    return result


def flip_selected_mesh_orientation():
    """
    Toggle UsdGeomMesh orientation between rightHanded and leftHanded.

    This changes which winding is treated as the front face without rewriting
    faceVertexIndices or face-varying primvars.
    """
    _, stage, _, _, selection = get_context()

    meshes = selected_meshes(stage, selection)
    if not meshes:
        return 0, []

    changed = []

    for mesh in meshes:
        orientation_attr = mesh.GetOrientationAttr()
        current = orientation_attr.Get()

        if current == UsdGeom.Tokens.leftHanded:
            value = UsdGeom.Tokens.rightHanded
        else:
            value = UsdGeom.Tokens.leftHanded

        mesh.CreateOrientationAttr().Set(value)
        changed.append((mesh.GetPath(), value))

    return len(changed), changed


class NormalDirectionDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(
            f"Stageviz Normal Direction - {qt_name}"
        )
        self.resize(420, 250)

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

        layout = QtWidgets.QVBoxLayout(self)

        description = QtWidgets.QLabel(
            "Front faces are shown in green.\n"
            "Back faces are shown in magenta.\n"
            "Normal Check temporarily forces the viewport to single-sided rendering "
            "so authored doubleSided values cannot hide incorrect winding."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        colors = QtWidgets.QHBoxLayout()
        layout.addLayout(colors)

        front_label = QtWidgets.QLabel("Front")
        front_label.setStyleSheet(
            "QLabel {"
            "background-color: rgb(31, 128, 41);"
            "padding: 6px 10px;"
            "}"
        )
        colors.addWidget(front_label)

        back_label = QtWidgets.QLabel("Back")
        back_label.setStyleSheet(
            "QLabel {"
            "background-color: rgb(158, 26, 82);"
            "padding: 6px 10px;"
            "}"
        )
        colors.addWidget(back_label)

        colors.addStretch(1)

        override_row = QtWidgets.QHBoxLayout()
        layout.addLayout(override_row)

        self.enable_button = QtWidgets.QPushButton(
            "Enable Normal Check"
        )
        self.disable_button = QtWidgets.QPushButton(
            "Scene Materials"
        )

        override_row.addWidget(self.enable_button)
        override_row.addWidget(self.disable_button)
        override_row.addStretch(1)

        self.flip_button = QtWidgets.QPushButton(
            "Flip Selected Normals"
        )
        layout.addWidget(self.flip_button)

        self.status_label = QtWidgets.QLabel("")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)
        button_row.addStretch(1)

        self.close_button = QtWidgets.QPushButton("Close")
        button_row.addWidget(self.close_button)

        self.enable_button.clicked.connect(
            self.enable_override
        )
        self.disable_button.clicked.connect(
            self.disable_override
        )
        self.flip_button.clicked.connect(
            self.flip_selected
        )
        self.close_button.clicked.connect(self.close)

    def enable_override(self):
        try:
            path = enable_normal_direction_override()

            self.status_label.setText(
                "Normal direction check enabled.\n"
                "Double-sided mode: SingleSided\n"
                f"Material: {path}"
            )

        except Exception as exc:
            self.status_label.setText(
                f"Error: {type(exc).__name__}: {exc}"
            )
            traceback.print_exc()

    def disable_override(self):
        try:
            #disable_normal_direction_override()

            self.status_label.setText(
                "Using authored scene materials.\n"
                "Previous double-sided mode restored."
            )

        except Exception as exc:
            self.status_label.setText(
                f"Error: {type(exc).__name__}: {exc}"
            )
            traceback.print_exc()

    def closeEvent(self, event):
        try:
            disable_normal_direction_override()
        except Exception:
            traceback.print_exc()

        super().closeEvent(event)

    def flip_selected(self):
        try:
            count, changed = flip_selected_mesh_orientation()

            if count == 0:
                self.status_label.setText(
                    "No selected mesh prims."
                )
                return

            lines = [
                f"Flipped {count} mesh"
                + ("" if count == 1 else "es")
                + "."
            ]

            for path, orientation in changed[:6]:
                lines.append(
                    f"{path}: {orientation}"
                )

            if len(changed) > 6:
                lines.append(
                    f"... and {len(changed) - 6} more."
                )

            self.status_label.setText(
                "\n".join(lines)
            )

            print(
                "[Stageviz Normal Direction] "
                f"flipped {count} selected mesh(es)"
            )

        except Exception as exc:
            self.status_label.setText(
                f"Error: {type(exc).__name__}: {exc}"
            )
            traceback.print_exc()


def show_normal_direction_dialog():
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

    win = NormalDirectionDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


normal_direction_dialog = show_normal_direction_dialog()
