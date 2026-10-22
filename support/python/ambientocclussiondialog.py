import sys
import traceback
import stageviz

try:
    from PySide6 import QtCore, QtWidgets
except Exception:
    from PyQt6 import QtCore, QtWidgets


DIALOG_OBJECT_NAME = "stagevizAmbientOcclusionDialog"
DIALOG_GLOBAL_NAME = "_stageviz_ambient_occlusion_dialog"

# Keep these in sync with ViewState::AmbientOcclusionSettings. The native
# setters clamp values and remain the source of truth for renderer state.
DEFAULTS = {
    "enabled": False,
    "contact_amount": 1.0,
    "contact_radius": 8.0,
    "broad_amount": 0.4,
    "broad_radius": 48.0,
    "normal_bias": 0.04,
    "falloff": 2.0,
    "contrast": 0.5,
    "edge_sharpness": 1.0,
    "blur_enabled": True,
    "blur_radius": 8.0,
    "quality": 2,
    "debug_mode": 0,
}


def find_stageviz_main_window():
    try:
        window = stageviz.Application().window()
        if window is not None:
            return window
    except Exception:
        traceback.print_exc()

    app = QtWidgets.QApplication.instance()
    return app.activeWindow() if app else None


def spin(minimum, maximum, value_step, decimals=2, suffix=""):
    widget = QtWidgets.QDoubleSpinBox()
    widget.setRange(minimum, maximum)
    widget.setDecimals(decimals)
    widget.setSingleStep(value_step)
    widget.setKeyboardTracking(False)
    widget.setSuffix(suffix)
    return widget


class AmbientOcclusionDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle("Ambient Occlusion")
        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        self._session = stageviz.Session()
        self._view_state = self._session.viewState()

        self._build_ui()
        self._read_state()

    def _group(self, title):
        group = QtWidgets.QGroupBox(title)
        group.setLayout(QtWidgets.QFormLayout())
        return group

    def _build_ui(self):
        layout = QtWidgets.QVBoxLayout(self)

        description = QtWidgets.QLabel(
            "Stageviz contact and broad ambient occlusion. Look effects are "
            "applied to document geometry before grid, helpers and selection."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        self.enabled = QtWidgets.QCheckBox("Enabled")
        layout.addWidget(self.enabled)

        contact = self._group("Contact")
        self.contact_amount = spin(0.0, 10.0, 0.05)
        self.contact_radius = spin(1.0, 128.0, 1.0, 1, " px")
        contact.layout().addRow("Amount", self.contact_amount)
        contact.layout().addRow("Radius", self.contact_radius)
        layout.addWidget(contact)

        broad = self._group("Broad")
        self.broad_amount = spin(0.0, 10.0, 0.05)
        self.broad_radius = spin(1.0, 512.0, 1.0, 1, " px")
        broad.layout().addRow("Amount", self.broad_amount)
        broad.layout().addRow("Radius", self.broad_radius)
        layout.addWidget(broad)

        shape = self._group("Shape")
        self.normal_bias = spin(0.0, 0.5, 0.01, 3)
        self.falloff = spin(0.25, 8.0, 0.1)
        self.contrast = spin(0.1, 4.0, 0.05)
        shape.layout().addRow("Normal bias", self.normal_bias)
        shape.layout().addRow("Falloff", self.falloff)
        shape.layout().addRow("Contrast", self.contrast)
        layout.addWidget(shape)

        filtering = self._group("Filtering")
        self.blur_enabled = QtWidgets.QCheckBox("Enabled")
        self.blur_radius = spin(1.0, 32.0, 1.0, 1, " px")
        self.edge_sharpness = spin(0.0, 4.0, 0.05)
        filtering.layout().addRow("Blur", self.blur_enabled)
        filtering.layout().addRow("Blur radius", self.blur_radius)
        filtering.layout().addRow("Edge sharpness", self.edge_sharpness)
        layout.addWidget(filtering)

        quality = self._group("Quality / Debug")
        self.quality = QtWidgets.QComboBox()
        self.quality.addItems(["Low", "Medium", "High", "Ultra"])
        self.debug_mode = QtWidgets.QComboBox()
        self.debug_mode.addItems(["Composite", "Combined AO", "Contact only", "Broad only"])
        quality.layout().addRow("Quality", self.quality)
        quality.layout().addRow("View", self.debug_mode)
        layout.addWidget(quality)

        reset_button = QtWidgets.QPushButton("Reset")
        reset_button.clicked.connect(self._reset)
        layout.addWidget(reset_button)

        self.status = QtWidgets.QLabel()
        self.status.setWordWrap(True)
        layout.addWidget(self.status)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(self.close)
        layout.addWidget(buttons)

        self.enabled.toggled.connect(
            lambda value: self._apply("setAmbientOcclusionEnabled", bool(value), "Enabled" if value else "Disabled")
        )
        self.contact_amount.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionContactAmount", float(value), f"Contact amount: {value:.2f}")
        )
        self.contact_radius.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionContactRadius", float(value), f"Contact radius: {value:.1f} px")
        )
        self.broad_amount.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionBroadAmount", float(value), f"Broad amount: {value:.2f}")
        )
        self.broad_radius.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionBroadRadius", float(value), f"Broad radius: {value:.1f} px")
        )
        self.normal_bias.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionNormalBias", float(value), f"Normal bias: {value:.3f}")
        )
        self.falloff.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionFalloff", float(value), f"Falloff: {value:.2f}")
        )
        self.contrast.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionContrast", float(value), f"Contrast: {value:.2f}")
        )
        self.blur_enabled.toggled.connect(
            lambda value: self._apply("setAmbientOcclusionBlurEnabled", bool(value), "Blur enabled" if value else "Blur disabled")
        )
        self.blur_radius.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionBlurRadius", float(value), f"Blur radius: {value:.1f} px")
        )
        self.edge_sharpness.valueChanged.connect(
            lambda value: self._apply("setAmbientOcclusionEdgeSharpness", float(value), f"Edge sharpness: {value:.2f}")
        )
        self.quality.currentIndexChanged.connect(
            lambda value: self._apply("setAmbientOcclusionQuality", int(value), f"Quality: {self.quality.currentText()}")
        )
        self.debug_mode.currentIndexChanged.connect(
            lambda value: self._apply("setAmbientOcclusionDebugMode", int(value), f"View: {self.debug_mode.currentText()}")
        )

        self.resize(420, 610)

    def _widgets(self):
        return (
            self.enabled,
            self.contact_amount,
            self.contact_radius,
            self.broad_amount,
            self.broad_radius,
            self.normal_bias,
            self.falloff,
            self.contrast,
            self.blur_enabled,
            self.blur_radius,
            self.edge_sharpness,
            self.quality,
            self.debug_mode,
        )

    def _read_state(self):
        try:
            for widget in self._widgets():
                widget.blockSignals(True)

            state = self._view_state
            self.enabled.setChecked(bool(state.ambientOcclusionEnabled()))
            self.contact_amount.setValue(float(state.ambientOcclusionContactAmount()))
            self.contact_radius.setValue(float(state.ambientOcclusionContactRadius()))
            self.broad_amount.setValue(float(state.ambientOcclusionBroadAmount()))
            self.broad_radius.setValue(float(state.ambientOcclusionBroadRadius()))
            self.normal_bias.setValue(float(state.ambientOcclusionNormalBias()))
            self.falloff.setValue(float(state.ambientOcclusionFalloff()))
            self.contrast.setValue(float(state.ambientOcclusionContrast()))
            self.blur_enabled.setChecked(bool(state.ambientOcclusionBlurEnabled()))
            self.blur_radius.setValue(float(state.ambientOcclusionBlurRadius()))
            self.edge_sharpness.setValue(float(state.ambientOcclusionEdgeSharpness()))
            self.quality.setCurrentIndex(int(state.ambientOcclusionQuality()))
            self.debug_mode.setCurrentIndex(int(state.ambientOcclusionDebugMode()))
            self._update_enabled_state()
            self.status.setText("Ready")
        except Exception as exc:
            self._show_error(exc)
        finally:
            for widget in self._widgets():
                widget.blockSignals(False)

    def _apply(self, method_name, value, status):
        try:
            getattr(self._view_state, method_name)(value)
            self._update_enabled_state()
            self.status.setText(status)
        except Exception as exc:
            self._show_error(exc)

    def _reset(self):
        try:
            for widget in self._widgets():
                widget.blockSignals(True)

            self.contact_amount.setValue(DEFAULTS["contact_amount"])
            self.contact_radius.setValue(DEFAULTS["contact_radius"])
            self.broad_amount.setValue(DEFAULTS["broad_amount"])
            self.broad_radius.setValue(DEFAULTS["broad_radius"])
            self.normal_bias.setValue(DEFAULTS["normal_bias"])
            self.falloff.setValue(DEFAULTS["falloff"])
            self.contrast.setValue(DEFAULTS["contrast"])
            self.blur_enabled.setChecked(DEFAULTS["blur_enabled"])
            self.blur_radius.setValue(DEFAULTS["blur_radius"])
            self.edge_sharpness.setValue(DEFAULTS["edge_sharpness"])
            self.quality.setCurrentIndex(DEFAULTS["quality"])
            self.debug_mode.setCurrentIndex(DEFAULTS["debug_mode"])

            state = self._view_state
            state.setAmbientOcclusionContactAmount(DEFAULTS["contact_amount"])
            state.setAmbientOcclusionContactRadius(DEFAULTS["contact_radius"])
            state.setAmbientOcclusionBroadAmount(DEFAULTS["broad_amount"])
            state.setAmbientOcclusionBroadRadius(DEFAULTS["broad_radius"])
            state.setAmbientOcclusionNormalBias(DEFAULTS["normal_bias"])
            state.setAmbientOcclusionFalloff(DEFAULTS["falloff"])
            state.setAmbientOcclusionContrast(DEFAULTS["contrast"])
            state.setAmbientOcclusionBlurEnabled(DEFAULTS["blur_enabled"])
            state.setAmbientOcclusionBlurRadius(DEFAULTS["blur_radius"])
            state.setAmbientOcclusionEdgeSharpness(DEFAULTS["edge_sharpness"])
            state.setAmbientOcclusionQuality(DEFAULTS["quality"])
            state.setAmbientOcclusionDebugMode(DEFAULTS["debug_mode"])
            self._update_enabled_state()
            self.status.setText("AO parameters reset")
        except Exception as exc:
            self._show_error(exc)
        finally:
            for widget in self._widgets():
                widget.blockSignals(False)

    def _update_enabled_state(self):
        enabled = self.enabled.isChecked()
        for widget in self._widgets()[1:]:
            widget.setEnabled(enabled)
        self.blur_radius.setEnabled(enabled and self.blur_enabled.isChecked())
        self.edge_sharpness.setEnabled(enabled and self.blur_enabled.isChecked())

    def _show_error(self, exc):
        self.status.setText(f"Error: {type(exc).__name__}: {exc}")
        traceback.print_exc()


def show_ambient_occlusion_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old = globals().get(DIALOG_GLOBAL_NAME)
    if old is not None:
        try:
            old.close()
            old.deleteLater()
        except Exception:
            pass
        globals()[DIALOG_GLOBAL_NAME] = None

    dialog = AmbientOcclusionDialog(find_stageviz_main_window())
    dialog.show()
    dialog.raise_()
    dialog.activateWindow()
    globals()[DIALOG_GLOBAL_NAME] = dialog

    if owns_app:
        app.exec()

    return dialog


ambient_occlusion_dialog = show_ambient_occlusion_dialog()
