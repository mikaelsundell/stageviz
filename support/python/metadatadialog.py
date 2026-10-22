# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2022 - present Mikael Sundell.
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from pxr import Usd, UsdGeom
import stageviz

try:
    from PySide6 import QtWidgets, QtCore
    qt_name = "PySide6"
except Exception:
    try:
        from PyQt6 import QtWidgets, QtCore
        qt_name = "PyQt6"
    except Exception:
        print("[Stageviz Metadata] Failed to import Qt bindings")
        traceback.print_exc()
        raise


DIALOG_OBJECT_NAME = "stagevizMetadataDialog"
DIALOG_GLOBAL_NAME = "_stageviz_metadata_dialog"


def format_value(value):
    if value is None:
        return "None"
    try:
        return repr(value)
    except Exception:
        try:
            return str(value)
        except Exception:
            return "<unprintable>"


def format_matrix(matrix):
    lines = []
    for row in range(4):
        lines.append(
            "  "
            + " ".join(
                f"{matrix[row][col]: .6f}"
                for col in range(4)
            )
        )
    return lines


def get_local_matrix_and_reset_flag(xformable):
    result = xformable.GetLocalTransformation()

    if isinstance(result, tuple):
        local_matrix = result[0]
    else:
        local_matrix = result

    try:
        resets_stack = xformable.GetResetXformStack()
    except Exception:
        resets_stack = False

    return local_matrix, resets_stack


def xform_to_lines(prim):
    lines = []

    xformable = UsdGeom.Xformable(prim)
    if not xformable:
        lines.append("Xform:")
        lines.append("  Prim is not xformable.")
        return lines

    lines.append("Xform:")

    try:
        local_matrix, resets_stack = get_local_matrix_and_reset_flag(xformable)
        world_matrix = xformable.ComputeLocalToWorldTransform(Usd.TimeCode.Default())

        lines.append(f"  resetXformStack: {resets_stack}")
        lines.append("")

        lines.append("  Local matrix:")
        lines.extend(format_matrix(local_matrix))
        lines.append("")

        lines.append("  World matrix:")
        lines.extend(format_matrix(world_matrix))
        lines.append("")

        ordered_ops = xformable.GetOrderedXformOps()

        if ordered_ops:
            lines.append("  Ordered xform ops:")
            for op in ordered_ops:
                try:
                    value = op.Get()
                except Exception as e:
                    value = f"<error reading op value: {e}>"

                lines.append(f"    {op.GetOpName()}: {format_value(value)}")
        else:
            lines.append("  Ordered xform ops: none")

    except Exception as e:
        lines.append(f"  <error reading xform: {type(e).__name__}: {e}>")

    return lines


def payload_to_lines(prim):
    lines = []
    lines.append("Payloads:")

    try:
        if not prim.HasPayload():
            lines.append("  none")
            return lines
    except Exception:
        pass

    found = False

    try:
        for spec in prim.GetPrimStack():
            layer = spec.layer
            payload_list = spec.payloadList

            items = []

            for attr_name in (
                "explicitItems",
                "addedItems",
                "prependedItems",
                "appendedItems",
                "deletedItems",
            ):
                try:
                    items.extend(getattr(payload_list, attr_name))
                except Exception:
                    pass

            for payload in items:
                found = True
                lines.append(f"  Layer: {layer.identifier}")
                lines.append(f"    assetPath: {payload.assetPath}")
                lines.append(f"    primPath: {payload.primPath}")
                lines.append(f"    layerOffset: {payload.layerOffset}")

    except Exception as e:
        lines.append(f"  <error reading payloads: {type(e).__name__}: {e}>")
        return lines

    if not found:
        lines.append("  Has payload, but no authored payload items found in prim stack.")

    return lines


def payload_ancestry_to_lines(prim):
    lines = []
    lines.append("Payload ancestry:")

    try:
        current = prim
        found = False

        ancestry = []
        while current and current.IsValid():
            ancestry.append(current)
            if current.GetPath().IsAbsoluteRootPath():
                break
            current = current.GetParent()

        ancestry.reverse()

        for ancestor in ancestry:
            try:
                has_payload = ancestor.HasPayload()
            except Exception:
                has_payload = False

            if has_payload:
                found = True
                lines.append(f"  {ancestor.GetPath()}")

                try:
                    for spec in ancestor.GetPrimStack():
                        payload_list = spec.payloadList
                        items = []

                        for attr_name in (
                            "explicitItems",
                            "addedItems",
                            "prependedItems",
                            "appendedItems",
                        ):
                            try:
                                items.extend(getattr(payload_list, attr_name))
                            except Exception:
                                pass

                        for payload in items:
                            lines.append(f"    layer: {spec.layer.identifier}")
                            lines.append(f"    assetPath: {payload.assetPath}")
                            lines.append(f"    primPath: {payload.primPath}")
                except Exception as e:
                    lines.append(f"    <error reading ancestor payload: {e}>")

        if not found:
            lines.append("  none")

    except Exception as e:
        lines.append(f"  <error reading payload ancestry: {type(e).__name__}: {e}>")

    return lines


def layer_stack_to_lines(prim):
    lines = []
    lines.append("Prim stack / authored layers:")

    try:
        prim_stack = prim.GetPrimStack()
    except Exception as e:
        lines.append(f"  <error reading prim stack: {type(e).__name__}: {e}>")
        return lines

    if not prim_stack:
        lines.append("  none")
        return lines

    for index, spec in enumerate(prim_stack):
        try:
            layer_identifier = spec.layer.identifier
        except Exception:
            layer_identifier = "<unknown layer>"

        try:
            spec_path = spec.path
        except Exception:
            spec_path = "<unknown path>"

        try:
            specifier = spec.specifier
        except Exception:
            specifier = "<unknown specifier>"

        lines.append(f"  [{index}] Layer: {layer_identifier}")
        lines.append(f"      specPath: {spec_path}")
        lines.append(f"      specifier: {specifier}")

    return lines


def metadata_to_lines(prim):
    lines = []

    lines.append("=" * 80)
    lines.append(f"Path: {prim.GetPath()}")
    lines.append(f"TypeName: {prim.GetTypeName()}")
    lines.append(f"Name: {prim.GetName()}")
    lines.append(f"Active: {prim.IsActive()}")
    lines.append(f"Defined: {prim.IsDefined()}")
    lines.append(f"Abstract: {prim.IsAbstract()}")
    lines.append(f"Instance: {prim.IsInstance()}")
    lines.append("")

    lines.extend(xform_to_lines(prim))
    lines.append("")

    lines.extend(payload_to_lines(prim))
    lines.append("")

    lines.extend(payload_ancestry_to_lines(prim))
    lines.append("")

    lines.extend(layer_stack_to_lines(prim))
    lines.append("")

    keys = list(prim.GetAllMetadata().keys())
    keys = sorted(set(keys))

    lines.append("Metadata:")

    if not keys:
        lines.append("  No metadata.")
    else:
        for key in keys:
            try:
                value = prim.GetMetadata(key)
            except Exception as e:
                value = f"<error reading metadata: {e}>"

            lines.append(f"  {key}: {format_value(value)}")

    try:
        custom_data = prim.GetCustomData()
    except Exception:
        custom_data = {}

    if custom_data:
        lines.append("")
        lines.append("customData:")
        for key in sorted(custom_data.keys()):
            lines.append(f"  {key}: {format_value(custom_data[key])}")

    return lines


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


class MetadataDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle(f"Stageviz Metadata - {qt_name}")
        self.resize(1000, 700)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        self._session = stageviz.session()
        self._last_paths = None

        layout = QtWidgets.QVBoxLayout(self)

        splitter = QtWidgets.QSplitter()
        layout.addWidget(splitter)

        self.path_list = QtWidgets.QListWidget()
        splitter.addWidget(self.path_list)

        self.text = QtWidgets.QPlainTextEdit()
        self.text.setReadOnly(True)
        self.text.setLineWrapMode(QtWidgets.QPlainTextEdit.LineWrapMode.NoWrap)
        splitter.addWidget(self.text)

        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.refresh_button = QtWidgets.QPushButton("Refresh")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addWidget(self.refresh_button)
        button_row.addStretch(1)
        button_row.addWidget(self.close_button)

        self.refresh_button.clicked.connect(self.refresh)
        self.close_button.clicked.connect(self.close)
        self.path_list.currentRowChanged.connect(self.on_row_changed)

        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.refresh)
        self.timer.start()

        self.refresh()

    def selected_paths(self):
        try:
            return [str(path) for path in self._session.paths()]
        except Exception:
            return []

    def refresh(self):
        paths = self.selected_paths()

        if paths == self._last_paths:
            return

        self._last_paths = paths

        current_path = None
        current_item = self.path_list.currentItem()
        if current_item:
            current_path = current_item.text()

        self.path_list.blockSignals(True)
        self.path_list.clear()

        for path in paths:
            self.path_list.addItem(path)

        self.path_list.blockSignals(False)

        if not paths:
            self.text.setPlainText("No selected prims.")
            return

        row = 0
        if current_path:
            for index, path in enumerate(paths):
                if path == current_path:
                    row = index
                    break

        self.path_list.setCurrentRow(row)
        self.show_metadata_for_path(paths[row])

    def on_row_changed(self, row):
        if row < 0:
            self.text.setPlainText("No selected prims.")
            return

        item = self.path_list.item(row)
        if not item:
            return

        self.show_metadata_for_path(item.text())

    def show_metadata_for_path(self, path):
        try:
            stage = self._session.stage()
            if stage is None:
                self.text.setPlainText("No stage loaded.")
                return

            prim = stage.GetPrimAtPath(path)
            if not prim:
                self.text.setPlainText(f"Invalid prim: {path}")
                return

            lines = metadata_to_lines(prim)
            self.text.setPlainText("\n".join(lines))

        except Exception as e:
            self.text.setPlainText(
                f"Error reading metadata for {path}\n\n"
                f"{type(e).__name__}: {e}"
            )
            traceback.print_exc()


def show_metadata_dialog():
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

    win = MetadataDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


metadata_dialog = show_metadata_dialog()