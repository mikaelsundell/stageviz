# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from PySide6 import QtCore, QtGui, QtWidgets
import stageviz


DIALOG_OBJECT_NAME = "stagevizStyleEditor"
DIALOG_GLOBAL_NAME = "_stageviz_style_editor"


COLOR_ROLES = [
    ("Base", stageviz.ColorBase),
    ("BaseAlt", stageviz.ColorBaseAlt),
    ("Accent", stageviz.ColorAccent),
    ("AccentAlt", stageviz.ColorAccentAlt),
    ("Text", stageviz.ColorText),
    ("Highlight", stageviz.ColorHighlight),
    ("HighlightAlt", stageviz.ColorHighlightAlt),
    ("Border", stageviz.ColorBorder),
    ("BorderAlt", stageviz.ColorBorderAlt),
    ("Handle", stageviz.ColorHandle),
    ("Progress", stageviz.ColorProgress),
    ("Button", stageviz.ColorButton),
    ("ButtonAlt", stageviz.ColorButtonAlt),
    ("Item", stageviz.ColorItem),
    ("ItemAlt", stageviz.ColorItemAlt),
    ("Render", stageviz.ColorRender),
    ("RenderAlt", stageviz.ColorRenderAlt),
    ("Selection", stageviz.ColorSelection),
    ("SelectionAlt", stageviz.ColorSelectionAlt),
    ("Warning", stageviz.ColorWarning),
    ("Error", stageviz.ColorError),
]

ICON_ROLES = [
    ("BranchOpen", stageviz.IconBranchOpen),
    ("BranchClosed", stageviz.IconBranchClosed),
    ("Clear", stageviz.IconClear),
    ("Code", stageviz.IconCode),
    ("Collapse", stageviz.IconCollapse),
    ("Expand", stageviz.IconExpand),
    ("Export", stageviz.IconExport),
    ("ExportImage", stageviz.IconExportImage),
    ("FrameAll", stageviz.IconFrameAll),
    ("Follow", stageviz.IconFollow),
    ("Hidden", stageviz.IconHidden),
    ("Visible", stageviz.IconVisible),
    ("Checked", stageviz.IconChecked),
    ("Dropdown", stageviz.IconDropdown),
    ("Left", stageviz.IconLeft),
    ("Material", stageviz.IconMaterial),
    ("Mesh", stageviz.IconMesh),
    ("PartiallyChecked", stageviz.IconPartiallyChecked),
    ("Open", stageviz.IconOpen),
    ("Payload", stageviz.IconPayload),
    ("Prim", stageviz.IconPrim),
    ("Redo", stageviz.IconRedo),
    ("Right", stageviz.IconRight),
    ("Run", stageviz.IconRun),
    ("Shaded", stageviz.IconShaded),
    ("Undo", stageviz.IconUndo),
    ("Wireframe", stageviz.IconWireframe),
]

UI_SCALES = [
    ("Small", stageviz.UIScaleSmall),
    ("Medium", stageviz.UIScaleMedium),
    ("Large", stageviz.UIScaleLarge),
]


def find_stageviz_main_window():
    app = QtWidgets.QApplication.instance()
    if app is None:
        return None

    active = app.activeWindow()
    if active and active.objectName() != DIALOG_OBJECT_NAME:
        return active

    for widget in app.topLevelWidgets():
        if not widget.isWindow():
            continue
        if not widget.isVisible():
            continue
        if widget.objectName() == DIALOG_OBJECT_NAME:
            continue
        if isinstance(widget, QtWidgets.QDialog):
            continue

        return widget

    return active


class ColorButton(QtWidgets.QPushButton):
    colorChanged = QtCore.Signal(QtGui.QColor)

    def __init__(self, color=None, parent=None):
        super().__init__(parent)
        self._color = QtGui.QColor(color) if color else QtGui.QColor(0, 0, 0)
        self.clicked.connect(self.chooseColor)
        self.setFixedHeight(28)
        self.updateSwatch()

    def color(self):
        return QtGui.QColor(self._color)

    def setColor(self, color):
        color = QtGui.QColor(color)
        if not color.isValid():
            return

        self._color = color
        self.updateSwatch()
        self.colorChanged.emit(self._color)

    def chooseColor(self):
        color = QtWidgets.QColorDialog.getColor(
            self._color,
            self.window(),
            "Choose Color",
        )
        if color.isValid():
            self.setColor(color)

    def updateSwatch(self):
        rgba = (
            f"rgba({self._color.red()}, "
            f"{self._color.green()}, "
            f"{self._color.blue()}, "
            f"{self._color.alpha()})"
        )

        h, s, l, a = self._color.getHsl()
        if h < 0:
            h = 0

        self.setText(f"H:{h}, S:{s}, L:{l}, A:{a}")
        self.setStyleSheet(f"""
            QPushButton {{
                text-align: left;
                padding-left: 8px;
                border: 1px solid rgba(90, 90, 90, 255);
                background-color: {rgba};
                color: {'black' if self._color.lightness() > 128 else 'white'};
            }}
        """)


class StyleEditor(QtWidgets.QDialog):
    def __init__(self, style, parent=None):
        super().__init__(parent)

        self.style = style

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle("Stageviz Style Editor")
        self.resize(980, 760)

        self.activated = False
        self.last_pos = None

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        app = QtWidgets.QApplication.instance()
        if app is not None:
            app.applicationStateChanged.connect(self._application_state_changed)

        self.colorButtons = {}
        self.fontSpinBoxes = {}
        self.iconSpinBoxes = {}
        self.iconPathEdits = {}

        self._buildUi()
        self._loadFromStyle()

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

    def _buildUi(self):
        root = QtWidgets.QVBoxLayout(self)

        tabs = QtWidgets.QTabWidget()
        root.addWidget(tabs)

        tabs.addTab(self._buildColorsPage(), "Colors")
        tabs.addTab(self._buildFontsPage(), "Fonts")
        tabs.addTab(self._buildIconsPage(), "Icons")
        tabs.addTab(self._buildRenderingPage(), "Rendering")

        buttonRow = QtWidgets.QHBoxLayout()
        root.addLayout(buttonRow)

        self.autoRefreshCheck = QtWidgets.QCheckBox("Auto refresh")
        self.autoRefreshCheck.setChecked(True)
        buttonRow.addWidget(self.autoRefreshCheck)

        buttonRow.addStretch(1)

        self.refreshButton = QtWidgets.QPushButton("Refresh")
        self.reloadButton = QtWidgets.QPushButton("Reload From Style")
        self.closeButton = QtWidgets.QPushButton("Close")

        buttonRow.addWidget(self.refreshButton)
        buttonRow.addWidget(self.reloadButton)
        buttonRow.addWidget(self.closeButton)

        self.refreshButton.clicked.connect(self.refreshStyle)
        self.reloadButton.clicked.connect(self._loadFromStyle)
        self.closeButton.clicked.connect(self.close)

    def _buildColorsPage(self):
        page = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(page)

        table = QtWidgets.QTableWidget(len(COLOR_ROLES), 3)
        table.setHorizontalHeaderLabels(["Role", "Normal", "Disabled"])
        table.verticalHeader().setVisible(False)
        table.horizontalHeader().setStretchLastSection(True)
        table.horizontalHeader().setSectionResizeMode(0, QtWidgets.QHeaderView.ResizeToContents)
        table.horizontalHeader().setSectionResizeMode(1, QtWidgets.QHeaderView.Stretch)
        table.horizontalHeader().setSectionResizeMode(2, QtWidgets.QHeaderView.Stretch)

        for row, (name, role) in enumerate(COLOR_ROLES):
            item = QtWidgets.QTableWidgetItem(name)
            item.setFlags(item.flags() & ~QtCore.Qt.ItemIsEditable)
            table.setItem(row, 0, item)

            normalButton = ColorButton()
            disabledButton = ColorButton()
            disabledButton.setEnabled(False)

            normalButton.colorChanged.connect(
                lambda color, r=role: self._onColorChanged(r, color)
            )

            self.colorButtons[(role, stageviz.UIStateNormal)] = normalButton
            self.colorButtons[(role, stageviz.UIStateDisabled)] = disabledButton

            table.setCellWidget(row, 1, normalButton)
            table.setCellWidget(row, 2, disabledButton)

        layout.addWidget(table)
        return page

    def _buildFontsPage(self):
        page = QtWidgets.QWidget()
        layout = QtWidgets.QFormLayout(page)

        for name, scale in UI_SCALES:
            spin = QtWidgets.QSpinBox()
            spin.setRange(1, 512)
            spin.valueChanged.connect(
                lambda value, s=scale: self._onFontSizeChanged(s, value)
            )
            self.fontSpinBoxes[scale] = spin
            layout.addRow(name, spin)

        return page

    def _buildIconsPage(self):
        page = QtWidgets.QWidget()
        outer = QtWidgets.QVBoxLayout(page)

        sizesGroup = QtWidgets.QGroupBox("Icon Sizes")
        sizesLayout = QtWidgets.QFormLayout(sizesGroup)

        for name, scale in UI_SCALES:
            spin = QtWidgets.QSpinBox()
            spin.setRange(1, 512)
            spin.valueChanged.connect(
                lambda value, s=scale: self._onIconSizeChanged(s, value)
            )
            self.iconSpinBoxes[scale] = spin
            sizesLayout.addRow(name, spin)

        outer.addWidget(sizesGroup)

        pathsGroup = QtWidgets.QGroupBox("Icon Paths")
        pathsLayout = QtWidgets.QGridLayout(pathsGroup)
        pathsLayout.addWidget(QtWidgets.QLabel("Role"), 0, 0)
        pathsLayout.addWidget(QtWidgets.QLabel("Path"), 0, 1)
        pathsLayout.addWidget(QtWidgets.QLabel(""), 0, 2)

        for row, (name, role) in enumerate(ICON_ROLES, start=1):
            label = QtWidgets.QLabel(name)
            edit = QtWidgets.QLineEdit()
            button = QtWidgets.QPushButton("Browse...")

            button.clicked.connect(
                lambda checked=False, r=role: self._browseIconPath(r)
            )
            edit.editingFinished.connect(
                lambda r=role: self._onIconPathEdited(r)
            )

            self.iconPathEdits[role] = edit

            pathsLayout.addWidget(label, row, 0)
            pathsLayout.addWidget(edit, row, 1)
            pathsLayout.addWidget(button, row, 2)

        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(pathsGroup)
        outer.addWidget(scroll)

        return page

    def _buildRenderingPage(self):
        page = QtWidgets.QWidget()
        layout = QtWidgets.QFormLayout(page)

        self.colorSpaceCombo = QtWidgets.QComboBox()
        self.colorSpaceCombo.addItems(["sRGB", "DisplayP3", "AdobeRGB", "Bt2020"])
        self.colorSpaceCombo.currentTextChanged.connect(self._onColorSpaceChanged)

        layout.addRow("Color Space", self.colorSpaceCombo)
        return page

    def _loadFromStyle(self):
        blockers = []

        for _, role in COLOR_ROLES:
            normal = self._safeColor(role, stageviz.UIStateNormal)
            disabled = self._safeColor(role, stageviz.UIStateDisabled)

            normalButton = self.colorButtons[(role, stageviz.UIStateNormal)]
            disabledButton = self.colorButtons[(role, stageviz.UIStateDisabled)]

            blockers.append(QtCore.QSignalBlocker(normalButton))
            blockers.append(QtCore.QSignalBlocker(disabledButton))

            normalButton.setColor(normal)
            disabledButton.setColor(disabled)

        for _, scale in UI_SCALES:
            blockers.append(QtCore.QSignalBlocker(self.fontSpinBoxes[scale]))
            blockers.append(QtCore.QSignalBlocker(self.iconSpinBoxes[scale]))

            self.fontSpinBoxes[scale].setValue(self.style.fontSize(scale))
            self.iconSpinBoxes[scale].setValue(self.style.iconSize(scale))

        for _, role in ICON_ROLES:
            edit = self.iconPathEdits[role]
            blockers.append(QtCore.QSignalBlocker(edit))

            try:
                edit.setText(self.style.iconPath(role))
            except Exception:
                edit.setText("")

        blockers.append(QtCore.QSignalBlocker(self.colorSpaceCombo))

        color_space = self._safeColorSpace()
        index = self.colorSpaceCombo.findText(
            color_space,
            QtCore.Qt.MatchFlag.MatchFixedString,
        )
        if index >= 0:
            self.colorSpaceCombo.setCurrentIndex(index)

    def _safeColor(self, role, state):
        try:
            value = self.style.color(role, state)
        except Exception:
            return QtGui.QColor(0, 0, 0)

        if isinstance(value, (tuple, list)) and len(value) == 4:
            return QtGui.QColor(value[0], value[1], value[2], value[3])

        return QtGui.QColor(0, 0, 0)

    def _safeColorSpace(self):
        try:
            value = self.style.colorSpace()
            if not value:
                return "sRGB"

            value = str(value)
            value_lower = value.lower()

            if "display" in value_lower and "p3" in value_lower:
                return "DisplayP3"
            if "adobe" in value_lower:
                return "AdobeRGB"
            if "2020" in value_lower:
                return "Bt2020"

            return "sRGB"
        except Exception:
            return "sRGB"

    def _onColorChanged(self, role, color):
        self.style.setColor(
            role,
            color.red(),
            color.green(),
            color.blue(),
            color.alpha(),
        )
        self._updateDisabledPreview(role)
        self._refreshIfNeeded()

    def _updateDisabledPreview(self, role):
        disabled = self._safeColor(role, stageviz.UIStateDisabled)
        button = self.colorButtons[(role, stageviz.UIStateDisabled)]

        blocker = QtCore.QSignalBlocker(button)
        button.setColor(disabled)
        del blocker

    def _onFontSizeChanged(self, scale, value):
        self.style.setFontSize(scale, value)
        self._refreshIfNeeded()

    def _onIconSizeChanged(self, scale, value):
        self.style.setIconSize(scale, value)
        self._refreshIfNeeded()

    def _onColorSpaceChanged(self, text):
        self.style.setColorSpace(text)
        self._refreshIfNeeded()

    def _browseIconPath(self, role):
        edit = self.iconPathEdits[role]

        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "Choose Icon",
            edit.text(),
            "Images (*.png *.svg *.jpg *.jpeg *.bmp *.webp);;All Files (*)",
        )

        if not path:
            return

        edit.setText(path)
        self._applyIconPath(role, path)

    def _onIconPathEdited(self, role):
        self._applyIconPath(role, self.iconPathEdits[role].text())

    def _applyIconPath(self, role, path):
        self.style.setIconPath(role, path)
        self._refreshIfNeeded()

    def _refreshIfNeeded(self):
        if self.autoRefreshCheck.isChecked():
            self.refreshStyle()

    def refreshStyle(self):
        self.style.refresh()


def show_style_editor():
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
            traceback.print_exc()

        globals()[DIALOG_GLOBAL_NAME] = None

    parent = find_stageviz_main_window()
    style = stageviz.style()

    win = StyleEditor(style, parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


style_editor = show_style_editor()