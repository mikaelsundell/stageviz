# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell

import sys
import traceback

import stageviz

try:
    from PySide6 import QtWidgets, QtCore, QtGui
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtWidgets, QtCore, QtGui
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
        self.resize(760, 860)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(
            QtCore.Qt.WidgetAttribute.WA_DeleteOnClose,
            True,
        )

        self._session = stageviz.session()
        self._view_state = self._session.viewState()
        self._camera = self._view_state.camera()

        layout = QtWidgets.QVBoxLayout(self)

        self.output = QtWidgets.QPlainTextEdit()
        self.output.setReadOnly(True)
        self.output.setLineWrapMode(
            QtWidgets.QPlainTextEdit.LineWrapMode.NoWrap
        )
        self.output.setMinimumHeight(240)
        layout.addWidget(self.output)

        tabs = QtWidgets.QTabWidget()
        layout.addWidget(tabs)

        camera_tab = QtWidgets.QWidget()
        viewstate_tab = QtWidgets.QWidget()

        tabs.addTab(camera_tab, "Camera")
        tabs.addTab(viewstate_tab, "View State")

        self._build_camera_tab(camera_tab)
        self._build_viewstate_tab(viewstate_tab)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.refresh_button = QtWidgets.QPushButton("Refresh")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addStretch(1)
        button_row.addWidget(self.refresh_button)
        button_row.addWidget(self.close_button)

        self.refresh_button.clicked.connect(self.refresh_controls)
        self.close_button.clicked.connect(self.close)

        self.refresh_controls()

    def _build_camera_tab(self, widget):
        layout = QtWidgets.QVBoxLayout(widget)

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

        self.apply_camera_button = QtWidgets.QPushButton("Apply Camera")
        self.frame_button = QtWidgets.QPushButton("Frame All")
        self.reset_view_button = QtWidgets.QPushButton("Reset View")
        self.reset_camera_button = QtWidgets.QPushButton("Reset Camera")

        button_row.addWidget(self.apply_camera_button)
        button_row.addWidget(self.frame_button)
        button_row.addWidget(self.reset_view_button)
        button_row.addWidget(self.reset_camera_button)
        button_row.addStretch(1)

        self.apply_camera_button.clicked.connect(
            self.apply_camera_values
        )
        self.frame_button.clicked.connect(self.frame_all)
        self.reset_view_button.clicked.connect(self.reset_view)
        self.reset_camera_button.clicked.connect(self.reset_camera)

        layout.addStretch(1)

    def _build_viewstate_tab(self, widget):
        layout = QtWidgets.QVBoxLayout(widget)

        form = QtWidgets.QFormLayout()
        layout.addLayout(form)

        self.background_color = QtWidgets.QPushButton()
        self.background_color.clicked.connect(
            lambda: self.choose_color(
                self.background_color,
                self._view_state.backgroundColor,
                self._view_state.setBackgroundColor,
            )
        )

        self.grid_color = QtWidgets.QPushButton()
        self.grid_color.clicked.connect(
            lambda: self.choose_color(
                self.grid_color,
                self._view_state.gridColor,
                self._view_state.setGridColor,
            )
        )

        self.grid_enabled = QtWidgets.QCheckBox()
        self.camera_light = QtWidgets.QCheckBox()
        self.scene_lights = QtWidgets.QCheckBox()
        self.scene_materials = QtWidgets.QCheckBox()
        self.scene_stats = QtWidgets.QCheckBox()
        self.performance_stats = QtWidgets.QCheckBox()
        self.camera_axis = QtWidgets.QCheckBox()

        self.render_mode = QtWidgets.QComboBox()
        self.render_mode.addItem("Shaded", 0)
        self.render_mode.addItem("Wireframe", 1)

        self.complexity = QtWidgets.QComboBox()
        self.complexity.addItem("Low", 0)
        self.complexity.addItem("Medium", 1)
        self.complexity.addItem("High", 2)
        self.complexity.addItem("Very High", 3)

        self.material_mode = QtWidgets.QComboBox()
        self.material_mode.addItem("All", 0)
        self.material_mode.addItem("Clay", 1)
        self.material_mode.addItem("Override", 2)

        self.override_material = QtWidgets.QLineEdit()
        self.override_material.setPlaceholderText(
            "/__stageviz/Materials/MyMaterial"
        )

        self.renderer_aov = QtWidgets.QLineEdit()
        self.renderer_aov.setPlaceholderText("color")

        form.addRow("Background color", self.background_color)
        form.addRow("Grid color", self.grid_color)
        form.addRow("Grid", self.grid_enabled)
        form.addRow("Camera light", self.camera_light)
        form.addRow("Scene lights", self.scene_lights)
        form.addRow("Scene materials", self.scene_materials)
        form.addRow("Render mode", self.render_mode)
        form.addRow("Complexity", self.complexity)
        form.addRow("Material mode", self.material_mode)
        form.addRow("Override material", self.override_material)
        form.addRow("Renderer AOV", self.renderer_aov)
        form.addRow("Scene stats", self.scene_stats)
        form.addRow("Performance stats", self.performance_stats)
        form.addRow("Camera axis", self.camera_axis)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.apply_viewstate_button = QtWidgets.QPushButton(
            "Apply View State"
        )
        self.clear_override_button = QtWidgets.QPushButton(
            "Clear Override"
        )

        button_row.addWidget(self.apply_viewstate_button)
        button_row.addWidget(self.clear_override_button)
        button_row.addStretch(1)

        self.apply_viewstate_button.clicked.connect(
            self.apply_viewstate_values
        )
        self.clear_override_button.clicked.connect(
            self.clear_override_material
        )

        layout.addStretch(1)

    @staticmethod
    def _color_tuple(color):
        return tuple(float(value) for value in color)

    @staticmethod
    def _qcolor(color):
        values = list(color)

        if len(values) == 3:
            values.append(1.0)

        return QtGui.QColor.fromRgbF(
            float(values[0]),
            float(values[1]),
            float(values[2]),
            float(values[3]),
        )

    @staticmethod
    def _set_color_button(button, color):
        qcolor = ViewStateDialog._qcolor(color)

        button.setText(
            f"{qcolor.redF():.3f}, "
            f"{qcolor.greenF():.3f}, "
            f"{qcolor.blueF():.3f}"
        )

        button.setStyleSheet(
            "QPushButton {"
            f"background-color: {qcolor.name()};"
            "}"
        )

    @staticmethod
    def _set_combo_value(combo, value):
        index = combo.findData(int(value))
        if index >= 0:
            combo.setCurrentIndex(index)

    def choose_color(self, button, getter, setter):
        try:
            current = self._qcolor(getter())

            color = QtWidgets.QColorDialog.getColor(
                current,
                self,
                "Select Color",
            )

            if not color.isValid():
                return

            value = (
                color.redF(),
                color.greenF(),
                color.blueF(),
                color.alphaF(),
            )

            setter(value)
            self._set_color_button(button, value)
            self.refresh_output()

        except Exception:
            traceback.print_exc()

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

    def view_state(self):
        return {
            "backgroundColor": self._color_tuple(
                self._view_state.backgroundColor()
            ),
            "gridColor": self._color_tuple(
                self._view_state.gridColor()
            ),
            "gridEnabled": self._view_state.gridEnabled(),
            "defaultCameraLightEnabled":
                self._view_state.defaultCameraLightEnabled(),
            "sceneLightsEnabled":
                self._view_state.sceneLightsEnabled(),
            "sceneMaterialsEnabled":
                self._view_state.sceneMaterialsEnabled(),
            "renderMode": self._view_state.renderMode(),
            "complexityLevel":
                self._view_state.complexityLevel(),
            "materialMode": self._view_state.materialMode(),
            "overrideMaterial":
                self._view_state.overrideMaterial(),
            "rendererAov": self._view_state.rendererAov(),
            "sceneStatsEnabled":
                self._view_state.sceneStatsEnabled(),
            "performanceStatsEnabled":
                self._view_state.performanceStatsEnabled(),
            "cameraAxisEnabled":
                self._view_state.cameraAxisEnabled(),
        }

    def complete_state(self):
        return {
            "viewState": self.view_state(),
            "camera": self.camera_state(),
        }

    def state_to_text(self, state):
        lines = []

        lines.append("ViewState:")
        lines.append(f"  object: {self._view_state}")

        for key, value in state["viewState"].items():
            lines.append(f"  {key}: {value}")

        lines.append("")
        lines.append("Camera:")
        lines.append(f"  object: {self._camera}")

        for key, value in state["camera"].items():
            lines.append(f"  {key}: {value}")

        return "\n".join(lines)

    def refresh_output(self):
        try:
            state = self.complete_state()
            self.output.setPlainText(
                self.state_to_text(state)
            )

        except Exception as error:
            self.output.setPlainText(
                "Error reading view state:\n\n"
                f"{type(error).__name__}: {error}"
            )
            traceback.print_exc()

    def refresh_controls(self):
        try:
            self.fov.setValue(
                self._camera.fov()
            )
            self.fit.setValue(
                self._camera.fit()
            )
            self.distance.setValue(
                self._camera.cameraDistance()
            )
            self.yaw.setValue(
                self._camera.axisYaw()
            )
            self.pitch.setValue(
                self._camera.axisPitch()
            )
            self.roll.setValue(
                self._camera.axisRoll()
            )
            self.near_clipping.setValue(
                self._camera.nearClipping()
            )
            self.far_clipping.setValue(
                self._camera.farClipping()
            )

            self._set_color_button(
                self.background_color,
                self._view_state.backgroundColor(),
            )

            self._set_color_button(
                self.grid_color,
                self._view_state.gridColor(),
            )

            self.grid_enabled.setChecked(
                self._view_state.gridEnabled()
            )

            self.camera_light.setChecked(
                self._view_state.defaultCameraLightEnabled()
            )

            self.scene_lights.setChecked(
                self._view_state.sceneLightsEnabled()
            )

            self.scene_materials.setChecked(
                self._view_state.sceneMaterialsEnabled()
            )

            self.scene_stats.setChecked(
                self._view_state.sceneStatsEnabled()
            )

            self.performance_stats.setChecked(
                self._view_state.performanceStatsEnabled()
            )

            self.camera_axis.setChecked(
                self._view_state.cameraAxisEnabled()
            )

            self._set_combo_value(
                self.render_mode,
                self._view_state.renderMode(),
            )

            self._set_combo_value(
                self.complexity,
                self._view_state.complexityLevel(),
            )

            self._set_combo_value(
                self.material_mode,
                self._view_state.materialMode(),
            )

            self.override_material.setText(
                str(self._view_state.overrideMaterial())
            )

            self.renderer_aov.setText(
                self._view_state.rendererAov()
            )

            self.refresh_output()

        except Exception:
            traceback.print_exc()

    def apply_camera_values(self):
        try:
            self._camera.setFov(
                self.fov.value()
            )
            self._camera.setFit(
                self.fit.value()
            )
            self._camera.setCameraDistance(
                self.distance.value()
            )
            self._camera.setAxisYaw(
                self.yaw.value()
            )
            self._camera.setAxisPitch(
                self.pitch.value()
            )
            self._camera.setAxisRoll(
                self.roll.value()
            )
            self._camera.setNearClipping(
                self.near_clipping.value()
            )
            self._camera.setFarClipping(
                self.far_clipping.value()
            )

            self.refresh_output()

        except Exception:
            traceback.print_exc()

    def apply_viewstate_values(self):
        try:
            self._view_state.setGridEnabled(
                self.grid_enabled.isChecked()
            )

            self._view_state.setDefaultCameraLightEnabled(
                self.camera_light.isChecked()
            )

            self._view_state.setSceneLightsEnabled(
                self.scene_lights.isChecked()
            )

            self._view_state.setSceneMaterialsEnabled(
                self.scene_materials.isChecked()
            )

            self._view_state.setRenderMode(
                self.render_mode.currentData()
            )

            self._view_state.setComplexityLevel(
                self.complexity.currentData()
            )

            self._view_state.setSceneStatsEnabled(
                self.scene_stats.isChecked()
            )

            self._view_state.setPerformanceStatsEnabled(
                self.performance_stats.isChecked()
            )

            self._view_state.setCameraAxisEnabled(
                self.camera_axis.isChecked()
            )

            aov = self.renderer_aov.text().strip()
            if aov:
                self._view_state.setRendererAov(aov)

            material_path = self.override_material.text().strip()

            if material_path:
                self._view_state.setOverrideMaterial(
                    material_path
                )
            else:
                self._view_state.setOverrideMaterial("")
                self._view_state.setMaterialMode(
                    self.material_mode.currentData()
                )

            self.refresh_output()

        except Exception:
            traceback.print_exc()

    def clear_override_material(self):
        try:
            self._view_state.setOverrideMaterial("")
            self.override_material.clear()
            self.refresh_controls()

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