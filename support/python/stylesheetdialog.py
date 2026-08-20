# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

import stageviz

try:
    from PySide6 import QtWidgets, QtCore, QtGui
    qt_name = "PySide6"
except Exception:
    try:
        from PyQt6 import QtWidgets, QtCore, QtGui
        qt_name = "PyQt6"
    except Exception:
        print("[Stageviz StyleSheet] Failed to import Qt bindings")
        traceback.print_exc()
        raise


DIALOG_OBJECT_NAME = "stagevizStyleSheetDialog"
DIALOG_GLOBAL_NAME = "_stageviz_stylesheet_dialog"


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


class StyleSheetDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(f"Stageviz Stylesheet - {qt_name}")
        self.resize(1100, 800)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        self._style = stageviz.Style()
        self._loaded_stylesheet = ""

        layout = QtWidgets.QVBoxLayout(self)

        self.editor = QtWidgets.QPlainTextEdit()
        self.editor.setLineWrapMode(QtWidgets.QPlainTextEdit.LineWrapMode.NoWrap)
        self.editor.setTabStopDistance(
            QtGui.QFontMetricsF(self.editor.font()).horizontalAdvance(" ") * 4
        )

        font = QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.SystemFont.FixedFont)
        self.editor.setFont(font)

        layout.addWidget(self.editor)

        self.status_label = QtWidgets.QLabel()
        self.status_label.setText("Ready.")
        layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.reload_button = QtWidgets.QPushButton("Reload")
        self.apply_button = QtWidgets.QPushButton("Apply")
        self.refresh_button = QtWidgets.QPushButton("Refresh")
        self.revert_button = QtWidgets.QPushButton("Revert")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addWidget(self.reload_button)
        button_row.addWidget(self.apply_button)
        button_row.addWidget(self.refresh_button)
        button_row.addWidget(self.revert_button)
        button_row.addStretch(1)
        button_row.addWidget(self.close_button)

        self.reload_button.clicked.connect(self.reload)
        self.apply_button.clicked.connect(self.apply)
        self.refresh_button.clicked.connect(self.refresh_style)
        self.revert_button.clicked.connect(self.revert)
        self.close_button.clicked.connect(self.close)

        self.editor.textChanged.connect(self.update_modified_state)

        self.reload()

    def set_status(self, text):
        self.status_label.setText(text)

    def update_modified_state(self):
        modified = self.editor.toPlainText() != self._loaded_stylesheet
        self.apply_button.setEnabled(modified)
        self.revert_button.setEnabled(modified)

        title = f"Stageviz Stylesheet - {qt_name}"
        if modified:
            title += " *"
        self.setWindowTitle(title)

    def reload(self):
        try:
            stylesheet = self._style.styleSheet()
            self._loaded_stylesheet = stylesheet

            self.editor.blockSignals(True)
            self.editor.setPlainText(stylesheet)
            self.editor.blockSignals(False)

            self.update_modified_state()
            self.set_status("Stylesheet loaded from application.")

        except Exception as e:
            self.set_status(f"Failed to load stylesheet: {type(e).__name__}: {e}")
            traceback.print_exc()

    def apply(self):
        try:
            stylesheet = self.editor.toPlainText()

            self._style.setStyleSheet(stylesheet)
            self._style.refresh()

            self._loaded_stylesheet = stylesheet
            self.update_modified_state()
            self.set_status("Stylesheet applied.")

        except Exception as e:
            self.set_status(f"Failed to apply stylesheet: {type(e).__name__}: {e}")
            traceback.print_exc()

    def refresh_style(self):
        try:
            self._style.refresh()
            self.set_status("Stylesheet refreshed.")

        except Exception as e:
            self.set_status(f"Failed to refresh stylesheet: {type(e).__name__}: {e}")
            traceback.print_exc()

    def revert(self):
        self.editor.blockSignals(True)
        self.editor.setPlainText(self._loaded_stylesheet)
        self.editor.blockSignals(False)

        self.update_modified_state()
        self.set_status("Editor reverted to last loaded/applied stylesheet.")


def show_stylesheet_dialog():
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

    win = StyleSheetDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


stylesheet_dialog = show_stylesheet_dialog()