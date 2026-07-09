# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import os
import sys
import traceback

from pxr import Sdf
import stageviz

try:
    from PySide6 import QtWidgets, QtCore
    qt_name = "PySide6"
except Exception:
    try:
        from PyQt6 import QtWidgets, QtCore
        qt_name = "PyQt6"
    except Exception:
        print("[Stageviz Payload] Failed to import Qt bindings")
        traceback.print_exc()
        raise


DIALOG_OBJECT_NAME = "stagevizPayloadDialog"
DIALOG_GLOBAL_NAME = "_stageviz_payload_dialog"

_last_payload_file = ""


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


def first_payload(prim):
    try:
        for spec in prim.GetPrimStack():
            payload_list = spec.payloadList

            for attr_name in (
                "explicitItems",
                "addedItems",
                "prependedItems",
                "appendedItems",
            ):
                try:
                    items = getattr(payload_list, attr_name)
                except Exception:
                    continue

                for payload in items:
                    return payload

    except Exception:
        traceback.print_exc()

    return None


def prim_payload_asset_path(prim):
    payload = first_payload(prim)
    if not payload:
        return ""

    try:
        return str(payload.assetPath)
    except Exception:
        return ""


def prim_payload_prim_path(prim):
    payload = first_payload(prim)
    if not payload:
        return ""

    try:
        if payload.primPath and not payload.primPath.IsEmpty():
            return str(payload.primPath)
    except Exception:
        pass

    return ""


class PayloadDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(f"Stageviz Payload - {qt_name}")
        self.resize(680, 260)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        self._session = stageviz.session()
        self._last_paths = None
        self._current_path = ""

        layout = QtWidgets.QVBoxLayout(self)

        layout.addWidget(QtWidgets.QLabel("Create or update payload on selected prim:"))

        form = QtWidgets.QFormLayout()
        form.setLabelAlignment(
            QtCore.Qt.AlignmentFlag.AlignRight |
            QtCore.Qt.AlignmentFlag.AlignVCenter
        )
        form.setVerticalSpacing(10)
        form.setHorizontalSpacing(16)
        layout.addLayout(form)

        self.prim_label = QtWidgets.QLabel("No selection")
        form.addRow("Prim:", self.prim_label)

        file_row = QtWidgets.QHBoxLayout()

        self.file_edit = QtWidgets.QLineEdit()
        self.file_edit.setPlaceholderText("Payload file path")

        self.browse_button = QtWidgets.QPushButton("Browse...")

        file_row.addWidget(self.file_edit, 1)
        file_row.addWidget(self.browse_button)

        form.addRow("File:", file_row)

        self.prim_path_edit = QtWidgets.QLineEdit()
        self.prim_path_edit.setPlaceholderText("Optional payload prim path, e.g. /World")
        form.addRow("Payload prim:", self.prim_path_edit)

        self.clear_existing_check = QtWidgets.QCheckBox("Replace existing payloads on selected prim")
        self.clear_existing_check.setChecked(True)
        layout.addWidget(self.clear_existing_check)

        self.status_label = QtWidgets.QLabel("")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.refresh_button = QtWidgets.QPushButton("Refresh")
        self.apply_button = QtWidgets.QPushButton("Add / Update Payload")
        self.remove_button = QtWidgets.QPushButton("Remove Payloads")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addWidget(self.refresh_button)
        button_row.addStretch(1)
        button_row.addWidget(self.apply_button)
        button_row.addWidget(self.remove_button)
        button_row.addWidget(self.close_button)

        self.browse_button.clicked.connect(self.browse_file)
        self.refresh_button.clicked.connect(self.refresh)
        self.apply_button.clicked.connect(self.apply_payload)
        self.remove_button.clicked.connect(self.remove_payloads)
        self.close_button.clicked.connect(self.close)

        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.refresh)
        self.timer.start()

        self.refresh(force=True)

    def set_status(self, text):
        self.status_label.setText(text)

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

    def refresh(self, force=False):
        paths = selected_paths(self._session)

        if not force and paths == self._last_paths:
            return

        self._last_paths = paths

        if not paths:
            self._current_path = ""
            self.prim_label.setText("No selection")
            self.apply_button.setEnabled(False)
            self.remove_button.setEnabled(False)
            self.set_status("Select a prim to add or update a payload.")
            return

        self._current_path = paths[0]
        self.prim_label.setText(self._current_path)
        self.apply_button.setEnabled(True)

        try:
            prim = self.current_prim()
            if not prim:
                self.remove_button.setEnabled(False)
                self.set_status(f"Invalid prim: {self._current_path}")
                return

            has_payload = prim.HasPayload()
            self.remove_button.setEnabled(has_payload)

            payload_file = prim_payload_asset_path(prim)
            payload_prim_path = prim_payload_prim_path(prim)

            if payload_file:
                self.file_edit.setText(payload_file)
                global _last_payload_file
                _last_payload_file = payload_file
                self.set_status("Selected prim has an authored payload.")
            else:
                if _last_payload_file and not self.file_edit.text().strip():
                    self.file_edit.setText(_last_payload_file)

                self.set_status("Selected prim has no payload. Current file input is kept.")

            if payload_prim_path:
                self.prim_path_edit.setText(payload_prim_path)

        except Exception as e:
            self.set_status(f"Error refreshing payload info: {type(e).__name__}: {e}")
            traceback.print_exc()

    def browse_file(self):
        start_dir = ""

        current = self.file_edit.text().strip()
        if current:
            start_dir = os.path.dirname(current)

        if not start_dir:
            try:
                filename = self._session.filename()
                if filename:
                    start_dir = os.path.dirname(filename)
            except Exception:
                pass

        filename, _ = QtWidgets.QFileDialog.getOpenFileName(
            self,
            "Select Payload File",
            start_dir,
            "USD Files (*.usd *.usda *.usdc);;All Files (*)",
        )

        if not filename:
            return

        self.file_edit.setText(filename)

        global _last_payload_file
        _last_payload_file = filename

    def apply_payload(self):
        prim = self.current_prim()
        if not prim:
            self.set_status("No valid prim selected.")
            return

        asset_path = self.file_edit.text().strip()
        if not asset_path:
            self.set_status("Payload file path is empty.")
            return

        payload_prim_path = self.prim_path_edit.text().strip()

        try:
            payloads = prim.GetPayloads()

            if self.clear_existing_check.isChecked():
                payloads.ClearPayloads()

            if payload_prim_path:
                payloads.AddPayload(asset_path, Sdf.Path(payload_prim_path))
            else:
                payloads.AddPayload(asset_path)

            global _last_payload_file
            _last_payload_file = asset_path

            self.remove_button.setEnabled(True)
            self.set_status(f"Payload updated on {prim.GetPath()}.")

            print("[Stageviz Payload]")
            print(f"Prim: {prim.GetPath()}")
            print(f"Asset path: {asset_path}")
            if payload_prim_path:
                print(f"Payload prim path: {payload_prim_path}")

        except Exception as e:
            self.set_status(f"Error applying payload: {type(e).__name__}: {e}")
            print(f"[Stageviz Payload] Error: {type(e).__name__}: {e}")
            traceback.print_exc()

    def remove_payloads(self):
        prim = self.current_prim()
        if not prim:
            self.set_status("No valid prim selected.")
            return

        try:
            prim.GetPayloads().ClearPayloads()
            self.remove_button.setEnabled(False)
            self.set_status(f"Removed payloads from {prim.GetPath()}.")

            print("[Stageviz Payload]")
            print(f"Removed payloads from {prim.GetPath()}")

        except Exception as e:
            self.set_status(f"Error removing payloads: {type(e).__name__}: {e}")
            print(f"[Stageviz Payload] Error: {type(e).__name__}: {e}")
            traceback.print_exc()


def show_payload_dialog():
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

    win = PayloadDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


payload_dialog = show_payload_dialog()