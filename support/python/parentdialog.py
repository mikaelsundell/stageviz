# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022 - present Mikael Sundell.
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


class ParentDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName("stagevizParentTestDialog")
        self.setWindowTitle(f"Stageviz Parent Test - {qt_name}")
        self.resize(420, 160)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        layout = QtWidgets.QVBoxLayout(self)

        parent_text = "None"
        if parent is not None:
            parent_text = f"{type(parent).__name__}: {parent.objectName()}"

        layout.addWidget(QtWidgets.QLabel("Dialog parent test"))
        layout.addWidget(QtWidgets.QLabel(f"Parent: {parent_text}"))

        info = QtWidgets.QPlainTextEdit()
        info.setReadOnly(True)
        info.setPlainText(
            f"Qt binding: {qt_name}\n"
            f"Parent object: {repr(parent)}\n"
            f"Is modal: {self.isModal()}\n"
            f"Window flags: {int(self.windowFlags())}"
        )
        layout.addWidget(info)

        close_button = QtWidgets.QPushButton("Close")
        close_button.clicked.connect(self.close)
        layout.addWidget(close_button)


def show_parent_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old_dialog = globals().get("_stageviz_parent_dialog")
    if old_dialog is not None:
        try:
            old_dialog.close()
            old_dialog.deleteLater()
        except Exception:
            pass

    parent = find_stageviz_main_window()
    print("[Stageviz Parent Test] parent:", parent)

    dialog = ParentDialog(parent)
    dialog.show()
    dialog.raise_()
    dialog.activateWindow()

    globals()["_stageviz_parent_dialog"] = dialog

    if owns_app:
        app.exec()


show_parent_dialog()