# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

import stageviz

try:
    from PySide6 import QtWidgets, QtCore
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtWidgets, QtCore
    qt_name = "PyQt6"


DIALOG_OBJECT_NAME = "stagevizViewStateDialog"
DIALOG_GLOBAL_NAME = "_stageviz_viewstate_dialog"


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


class ViewStateDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(f"Stageviz View State - {qt_name}")
        self.resize(720, 600)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        self._session = stageviz.session()
        self._view_state = self._session.viewState()
        self._camera = self._view_state.camera()
        self._last_state = None

        layout = QtWidgets.QVBoxLayout(self)

        self.output = QtWidgets.QPlainTextEdit()
        self.output.setReadOnly(True)
        self.output.setLineWrapMode(QtWidgets.QPlainTextEdit.LineWrapMode.NoWrap)
        layout.addWidget(self.output)

        form = QtWidgets.QFormLayout()
        layout.addLayout(form)

        self.fov = QtWidgets.QDoubleSpinBox()
        self.fov.setRange(1.0, 179.0)
        self.fov.setDecimals(3)
        self.fov.setSingleStep(1.0)

        self.fit = QtWidgets.QDoubleSpinBox()
        self.fit.setRange(0.01, 100.0)
        self.fit.setDecimals(3)
        self.fit.setSingleStep(0.1)

        self.distance = QtWidgets.QDoubleSpinBox()
        self.distance.setRange(0.001, 100000000.0)
        self.distance.setDecimals(3)
        self.distance.setSingleStep(1.0)

        self.yaw = QtWidgets.QDoubleSpinBox()
        self.yaw.setRange(-36000.0, 36000.0)
        self.yaw.setDecimals(3)
        self.yaw.setSingleStep(5.0)

        self.pitch = QtWidgets.QDoubleSpinBox()
        self.pitch.setRange(-36000.0, 36000.0)
        self.pitch.setDecimals(3)
        self.pitch.setSingleStep(5.0)

        self.roll = QtWidgets.QDoubleSpinBox()
        self.roll.setRange(-36000.0, 36000.0)
        self.roll.setDecimals(3)
        self.roll.setSingleStep(5.0)

        self.near_clipping = QtWidgets.QDoubleSpinBox()
        self.near_clipping.setRange(0.000001, 100000000.0)
        self.near_clipping.setDecimals(6)
        self.near_clipping.setSingleStep(0.01)

        self.far_clipping = QtWidgets.QDoubleSpinBox()
        self.far_clipping.setRange(0.000001, 1000000000.0)
        self.far_clipping.setDecimals(3)
        self.far_clipping.setSingleStep(10.0)

        form.addRow("FOV", self.fov)
        form.addRow("Fit", self.fit)
        form.addRow("Distance", self.distance)
        form.addRow("Yaw", self.yaw)
        form.addRow("Pitch", self.pitch)
        form.addRow("Roll", self.roll)
        form.addRow("Near clipping", self.near_clipping)
        form.addRow("Far clipping", self.far_clipping)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.apply_button = QtWidgets.QPushButton("Apply")
        self.frame_button = QtWidgets.QPushButton("Frame All")
        self.reset_view_button = QtWidgets.QPushButton("Reset View")
        self.reset_button = QtWidgets.QPushButton("Reset Camera")
        self.refresh_button = QtWidgets.QPushButton("Refresh")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addWidget(self.apply_button)
        button_row.addWidget(self.frame_button)
        button_row.addWidget(self.reset_view_button)
        button_row.addWidget(self.reset_button)
        button_row.addStretch(1)
        button_row.addWidget(self.refresh_button)
        button_row.addWidget(self.close_button)

        self.apply_button.clicked.connect(self.apply_values)
        self.frame_button.clicked.connect(self.frame_all)
        self.reset_view_button.clicked.connect(self.reset_view)
        self.reset_button.clicked.connect(self.reset_camera)
        self.refresh_button.clicked.connect(self.refresh_controls)
        self.close_button.clicked.connect(self.close)

        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.check_camera_changed)
        self.timer.start()

        self.refresh_controls()
        self.check_camera_changed(force=True)

    def camera_state(self):
        near = self._camera.nearClipping()
        far = self._camera.farClipping()

        return {
            "identity": self._camera.isIdentity(),
            "aspectRatio": self._camera.aspectRatio(),
            "fov": self._camera.fov(),
            "fovDirection": self._camera.fovDirection(),
            "focusPoint": self._camera.focusPoint(),
            "boundingBox": self._camera.boundingBox(),
            "fit": self._camera.fit(),
            "cameraUp": self._camera.cameraUp(),
            "axisYaw": self._camera.axisYaw(),
            "axisPitch": self._camera.axisPitch(),
            "axisRoll": self._camera.axisRoll(),
            "cameraMode": self._camera.cameraMode(),
            "nearClipping": near,
            "farClipping": far,
            "clippingRange": (near, far),
            "cameraDistance": self._camera.cameraDistance(),
        }

    def state_to_text(self, state):
        lines = []
        lines.append("ViewState:")
        lines.append(f"  viewState: {self._view_state}")
        lines.append(f"  camera: {self._camera}")
        lines.append("")
        lines.append("Camera:")

        for key, value in state.items():
            lines.append(f"  {key}: {value}")

        return "\n".join(lines)

    def check_camera_changed(self, force=False):
        try:
            state = self.camera_state()
            if force or state != self._last_state:
                self._last_state = state
                self.output.setPlainText(self.state_to_text(state))
        except Exception as e:
            self.output.setPlainText(f"Error reading camera state:\n\n{type(e).__name__}: {e}")
            traceback.print_exc()

    def refresh_controls(self):
        try:
            self.fov.setValue(self._camera.fov())
            self.fit.setValue(self._camera.fit())
            self.distance.setValue(self._camera.cameraDistance())
            self.yaw.setValue(self._camera.axisYaw())
            self.pitch.setValue(self._camera.axisPitch())
            self.roll.setValue(self._camera.axisRoll())
            self.near_clipping.setValue(self._camera.nearClipping())
            self.far_clipping.setValue(self._camera.farClipping())
            self.check_camera_changed(force=True)
        except Exception:
            traceback.print_exc()

    def apply_values(self):
        try:
            self._camera.setFov(self.fov.value())
            self._camera.setFit(self.fit.value())
            self._camera.setCameraDistance(self.distance.value())
            self._camera.setAxisYaw(self.yaw.value())
            self._camera.setAxisPitch(self.pitch.value())
            self._camera.setAxisRoll(self.roll.value())
            self._camera.setNearClipping(self.near_clipping.value())
            self._camera.setFarClipping(self.far_clipping.value())
            self.check_camera_changed(force=True)
        except Exception:
            traceback.print_exc()

    def frame_all(self):
        try:
            self._camera.frameAll()
            self.refresh_controls()
        except Exception:
            traceback.print_exc()

    def reset_view(self):
        try:
            self._camera.resetView()
            self.refresh_controls()
        except Exception:
            traceback.print_exc()

    def reset_camera(self):
        try:
            self._camera.reset()
            self.refresh_controls()
        except Exception:
            traceback.print_exc()


def show_viewstate_dialog():
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

    win = ViewStateDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


viewstate_dialog = show_viewstate_dialog()