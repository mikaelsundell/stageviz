# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from pxr import Usd, UsdGeom, Gf
import stageviz

try:
    from PySide6 import QtWidgets, QtCore
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtWidgets, QtCore
    qt_name = "PyQt6"


DIALOG_OBJECT_NAME = "stagevizTransformDialog"
DIALOG_GLOBAL_NAME = "_stageviz_transform_dialog"


ROTATION_ORDERS = {
    "XYZ": UsdGeom.XformCommonAPI.RotationOrderXYZ,
    "XZY": UsdGeom.XformCommonAPI.RotationOrderXZY,
    "YXZ": UsdGeom.XformCommonAPI.RotationOrderYXZ,
    "YZX": UsdGeom.XformCommonAPI.RotationOrderYZX,
    "ZXY": UsdGeom.XformCommonAPI.RotationOrderZXY,
    "ZYX": UsdGeom.XformCommonAPI.RotationOrderZYX,
}


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


def selected_paths(session):
    try:
        return [str(path) for path in session.paths()]
    except Exception:
        return []


def add_form_row(form, label_text, widget):
    label = QtWidgets.QLabel(label_text)
    label.setAlignment(
        QtCore.Qt.AlignmentFlag.AlignRight |
        QtCore.Qt.AlignmentFlag.AlignVCenter
    )
    label.setFixedHeight(widget.sizeHint().height())
    form.addRow(label, widget)


def make_spin(value=0.0):
    spin = QtWidgets.QDoubleSpinBox()
    spin.setDecimals(6)
    spin.setRange(-1000000000.0, 1000000000.0)
    spin.setSingleStep(1.0)
    spin.setValue(value)
    spin.setMinimumWidth(110)
    return spin


def make_vec3_row(values=(0.0, 0.0, 0.0)):
    widget = QtWidgets.QWidget()
    layout = QtWidgets.QHBoxLayout(widget)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(6)

    spins = []
    for name, value in zip(("X", "Y", "Z"), values):
        label = QtWidgets.QLabel(name)
        spin = make_spin(value)
        layout.addWidget(label)
        layout.addWidget(spin)
        spins.append(spin)

    layout.addStretch(1)
    return widget, spins


def vec3_from_spins(spins):
    return [float(spin.value()) for spin in spins]


def set_spins(spins, values):
    for spin, value in zip(spins, values):
        spin.setValue(float(value))


def rotation_order_name(order):
    for name, value in ROTATION_ORDERS.items():
        if order == value:
            return name
    return "XYZ"


class TransformDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(f"Stageviz Transform - {qt_name}")
        self.resize(620, 340)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        self._session = stageviz.session()
        self._last_paths = None
        self._current_path = ""
        self._loading = False
        self._dirty = False

        layout = QtWidgets.QVBoxLayout(self)

        layout.addWidget(QtWidgets.QLabel("Edit transform for selected Xformable prim:"))

        form = QtWidgets.QFormLayout()
        form.setLabelAlignment(
            QtCore.Qt.AlignmentFlag.AlignRight |
            QtCore.Qt.AlignmentFlag.AlignVCenter
        )
        form.setVerticalSpacing(10)
        form.setHorizontalSpacing(16)
        layout.addLayout(form)

        self.prim_label = QtWidgets.QLabel("No selection")
        add_form_row(form, "Prim:", self.prim_label)

        self.translate_widget, self.translate_spins = make_vec3_row()
        add_form_row(form, "Translate:", self.translate_widget)

        self.rotate_widget, self.rotate_spins = make_vec3_row()
        add_form_row(form, "Rotate:", self.rotate_widget)

        self.scale_widget, self.scale_spins = make_vec3_row((1.0, 1.0, 1.0))
        add_form_row(form, "Scale:", self.scale_widget)

        self.rotation_order_combo = QtWidgets.QComboBox()
        for name in ROTATION_ORDERS.keys():
            self.rotation_order_combo.addItem(name)
        add_form_row(form, "Rotation order:", self.rotation_order_combo)

        self.reset_stack_check = QtWidgets.QCheckBox("Reset xform stack")
        layout.addWidget(self.reset_stack_check)

        self.status_label = QtWidgets.QLabel("")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.refresh_button = QtWidgets.QPushButton("Refresh")
        self.apply_button = QtWidgets.QPushButton("Apply")
        self.zero_translate_button = QtWidgets.QPushButton("Zero T")
        self.zero_rotate_button = QtWidgets.QPushButton("Zero R")
        self.unit_scale_button = QtWidgets.QPushButton("Unit S")
        self.identity_button = QtWidgets.QPushButton("Identity")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addWidget(self.refresh_button)
        button_row.addStretch(1)
        button_row.addWidget(self.zero_translate_button)
        button_row.addWidget(self.zero_rotate_button)
        button_row.addWidget(self.unit_scale_button)
        button_row.addWidget(self.identity_button)
        button_row.addWidget(self.apply_button)
        button_row.addWidget(self.close_button)

        self.refresh_button.clicked.connect(lambda: self.refresh(force=True))
        self.apply_button.clicked.connect(self.apply_transform)
        self.zero_translate_button.clicked.connect(self.zero_translate)
        self.zero_rotate_button.clicked.connect(self.zero_rotate)
        self.unit_scale_button.clicked.connect(self.unit_scale)
        self.identity_button.clicked.connect(self.identity)
        self.close_button.clicked.connect(self.close)

        for spin in self.translate_spins + self.rotate_spins + self.scale_spins:
            spin.valueChanged.connect(self.mark_dirty)

        self.rotation_order_combo.currentIndexChanged.connect(self.mark_dirty)
        self.reset_stack_check.toggled.connect(self.mark_dirty)

        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.refresh)
        self.timer.start()

        self.refresh(force=True)

    def mark_dirty(self):
        if not self._loading:
            self._dirty = True

    def set_status(self, text):
        self.status_label.setText(text)

    def set_controls_enabled(self, enabled):
        for widget in (
            self.translate_widget,
            self.rotate_widget,
            self.scale_widget,
            self.rotation_order_combo,
            self.reset_stack_check,
            self.apply_button,
            self.zero_translate_button,
            self.zero_rotate_button,
            self.unit_scale_button,
            self.identity_button,
        ):
            widget.setEnabled(enabled)

    def current_prim(self):
        if not self._current_path:
            return None

        stage = self._session.stage()
        if stage is None:
            return None

        prim = stage.GetPrimAtPath(self._current_path)
        if not prim or not prim.IsValid():
            return None

        return prim

    def current_xformable(self):
        prim = self.current_prim()
        if not prim:
            return None

        xformable = UsdGeom.Xformable(prim)
        if not xformable:
            return None

        return xformable

    def refresh(self, force=False):
        paths = selected_paths(self._session)

        if not force and paths == self._last_paths:
            return

        self._last_paths = paths

        if not paths:
            self._current_path = ""
            self.prim_label.setText("No selection")
            self.set_controls_enabled(False)
            self.set_status("Select an Xformable prim.")
            return

        self._current_path = paths[0]
        self.prim_label.setText(self._current_path)

        try:
            prim = self.current_prim()
            if not prim:
                self.set_controls_enabled(False)
                self.set_status(f"Invalid prim: {self._current_path}")
                return

            xformable = UsdGeom.Xformable(prim)
            if not xformable:
                self.set_controls_enabled(False)
                self.set_status(f"Selected prim is not Xformable: {self._current_path}")
                return

            api = UsdGeom.XformCommonAPI(prim)

            translation = Gf.Vec3d(0.0, 0.0, 0.0)
            rotation = Gf.Vec3f(0.0, 0.0, 0.0)
            scale = Gf.Vec3f(1.0, 1.0, 1.0)
            pivot = Gf.Vec3f(0.0, 0.0, 0.0)
            rotation_order = UsdGeom.XformCommonAPI.RotationOrderXYZ

            try:
                result = api.GetXformVectors(Usd.TimeCode.Default())
                if result:
                    translation, rotation, scale, pivot, rotation_order = result
            except Exception:
                pass

            try:
                reset_stack = xformable.GetResetXformStack()
            except Exception:
                reset_stack = False

            self._loading = True

            set_spins(self.translate_spins, translation)
            set_spins(self.rotate_spins, rotation)
            set_spins(self.scale_spins, scale)

            order_name = rotation_order_name(rotation_order)
            self.rotation_order_combo.setCurrentText(order_name)
            self.reset_stack_check.setChecked(bool(reset_stack))

            self._loading = False
            self._dirty = False

            self.set_controls_enabled(True)
            self.set_status("Transform loaded from selected prim.")

        except Exception as e:
            self._loading = False
            self.set_controls_enabled(False)
            self.set_status(f"Error reading transform: {type(e).__name__}: {e}")
            traceback.print_exc()

    def apply_transform(self):
        prim = self.current_prim()
        if not prim:
            self.set_status("No valid prim selected.")
            return

        try:
            api = UsdGeom.XformCommonAPI(prim)

            t = vec3_from_spins(self.translate_spins)
            r = vec3_from_spins(self.rotate_spins)
            s = vec3_from_spins(self.scale_spins)

            order_name = self.rotation_order_combo.currentText()
            rotation_order = ROTATION_ORDERS.get(
                order_name,
                UsdGeom.XformCommonAPI.RotationOrderXYZ,
            )

            api.SetTranslate(Gf.Vec3d(t[0], t[1], t[2]))
            api.SetRotate(Gf.Vec3f(r[0], r[1], r[2]), rotation_order)
            api.SetScale(Gf.Vec3f(s[0], s[1], s[2]))
            api.SetResetXformStack(self.reset_stack_check.isChecked())

            self._dirty = False
            self.set_status(f"Transform applied to {prim.GetPath()}.")

            print("[Stageviz Transform]")
            print(f"Prim: {prim.GetPath()}")
            print(f"Translate: {t}")
            print(f"Rotate: {r} {order_name}")
            print(f"Scale: {s}")
            print(f"Reset stack: {self.reset_stack_check.isChecked()}")

        except Exception as e:
            self.set_status(f"Error applying transform: {type(e).__name__}: {e}")
            print(f"[Stageviz Transform] Error: {type(e).__name__}: {e}")
            traceback.print_exc()

    def zero_translate(self):
        set_spins(self.translate_spins, (0.0, 0.0, 0.0))
        self.mark_dirty()

    def zero_rotate(self):
        set_spins(self.rotate_spins, (0.0, 0.0, 0.0))
        self.mark_dirty()

    def unit_scale(self):
        set_spins(self.scale_spins, (1.0, 1.0, 1.0))
        self.mark_dirty()

    def identity(self):
        set_spins(self.translate_spins, (0.0, 0.0, 0.0))
        set_spins(self.rotate_spins, (0.0, 0.0, 0.0))
        set_spins(self.scale_spins, (1.0, 1.0, 1.0))
        self.rotation_order_combo.setCurrentText("XYZ")
        self.reset_stack_check.setChecked(False)
        self.mark_dirty()


def show_transform_dialog():
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

    win = TransformDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


transform_dialog = show_transform_dialog()