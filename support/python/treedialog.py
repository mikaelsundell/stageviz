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


class TreeDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName("stagevizTreeDialog")
        self.setWindowTitle(f"Stageviz Tree Selection Test - {qt_name}")
        self.resize(620, 520)

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        layout = QtWidgets.QVBoxLayout(self)

        parent_text = "None"
        if parent is not None:
            parent_text = f"{type(parent).__name__}: {parent.objectName()}"

        layout.addWidget(QtWidgets.QLabel("Native QTreeWidget selection test"))
        layout.addWidget(QtWidgets.QLabel(f"Parent: {parent_text}"))

        controls = QtWidgets.QHBoxLayout()
        layout.addLayout(controls)

        self.mode_combo = QtWidgets.QComboBox()
        self.mode_combo.addItem("SingleSelection", QtWidgets.QAbstractItemView.SelectionMode.SingleSelection)
        self.mode_combo.addItem("MultiSelection", QtWidgets.QAbstractItemView.SelectionMode.MultiSelection)
        self.mode_combo.addItem("ExtendedSelection", QtWidgets.QAbstractItemView.SelectionMode.ExtendedSelection)
        self.mode_combo.addItem("ContiguousSelection", QtWidgets.QAbstractItemView.SelectionMode.ContiguousSelection)
        controls.addWidget(QtWidgets.QLabel("Selection mode:"))
        controls.addWidget(self.mode_combo)

        self.behavior_combo = QtWidgets.QComboBox()
        self.behavior_combo.addItem("SelectItems", QtWidgets.QAbstractItemView.SelectionBehavior.SelectItems)
        self.behavior_combo.addItem("SelectRows", QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows)
        self.behavior_combo.addItem("SelectColumns", QtWidgets.QAbstractItemView.SelectionBehavior.SelectColumns)
        controls.addWidget(QtWidgets.QLabel("Behavior:"))
        controls.addWidget(self.behavior_combo)

        self.drag_check = QtWidgets.QCheckBox("Drag enabled")
        self.drag_check.setChecked(True)
        controls.addWidget(self.drag_check)

        self.tree = QtWidgets.QTreeWidget()
        self.tree.setColumnCount(2)
        self.tree.setHeaderLabels(["Name", "Info"])
        self.tree.setAlternatingRowColors(True)
        self.tree.setRootIsDecorated(True)
        self.tree.setDragEnabled(True)
        self.tree.setSelectionMode(QtWidgets.QAbstractItemView.SelectionMode.ExtendedSelection)
        self.tree.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows)
        layout.addWidget(self.tree, 1)

        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumHeight(150)
        layout.addWidget(self.log)

        buttons = QtWidgets.QHBoxLayout()
        layout.addLayout(buttons)

        clear_button = QtWidgets.QPushButton("Clear selection")
        clear_button.clicked.connect(self.tree.clearSelection)
        buttons.addWidget(clear_button)

        expand_button = QtWidgets.QPushButton("Expand all")
        expand_button.clicked.connect(self.tree.expandAll)
        buttons.addWidget(expand_button)

        close_button = QtWidgets.QPushButton("Close")
        close_button.clicked.connect(self.close)
        buttons.addWidget(close_button)

        self.populate_tree()

        self.mode_combo.currentIndexChanged.connect(self.apply_settings)
        self.behavior_combo.currentIndexChanged.connect(self.apply_settings)
        self.drag_check.toggled.connect(self.apply_settings)
        self.tree.itemSelectionChanged.connect(self.selection_changed)

        self.apply_settings()
        self.tree.expandAll()

    def populate_tree(self):
        self.tree.clear()

        for group_index in range(1, 5):
            group = QtWidgets.QTreeWidgetItem(
                [f"Group {group_index}", f"/World/Group_{group_index}"]
            )
            group.setData(0, QtCore.Qt.ItemDataRole.UserRole, f"/World/Group_{group_index}")
            group.setFlags(group.flags() | QtCore.Qt.ItemFlag.ItemIsDragEnabled)
            self.tree.addTopLevelItem(group)

            for item_index in range(1, 6):
                item = QtWidgets.QTreeWidgetItem(
                    [f"Item {group_index}.{item_index}", f"/World/Group_{group_index}/Item_{item_index}"]
                )
                item.setData(
                    0,
                    QtCore.Qt.ItemDataRole.UserRole,
                    f"/World/Group_{group_index}/Item_{item_index}",
                )
                item.setFlags(item.flags() | QtCore.Qt.ItemFlag.ItemIsDragEnabled)
                group.addChild(item)

    def apply_settings(self):
        mode = self.mode_combo.currentData()
        behavior = self.behavior_combo.currentData()

        self.tree.setSelectionMode(mode)
        self.tree.setSelectionBehavior(behavior)
        self.tree.setDragEnabled(self.drag_check.isChecked())

        self.write_log("settings changed")

    def selection_changed(self):
        selected = []
        for item in self.tree.selectedItems():
            path = item.data(0, QtCore.Qt.ItemDataRole.UserRole)
            selected.append(path or item.text(0))

        self.write_log("selection changed", selected)

    def write_log(self, title, selected=None):
        if selected is None:
            selected = [
                item.data(0, QtCore.Qt.ItemDataRole.UserRole) or item.text(0)
                for item in self.tree.selectedItems()
            ]

        self.log.setPlainText(
            f"Qt binding: {qt_name}\n"
            f"Selection mode: {self.mode_combo.currentText()}\n"
            f"Selection behavior: {self.behavior_combo.currentText()}\n"
            f"Drag enabled: {self.tree.dragEnabled()}\n"
            f"Selected count: {len(selected)}\n"
            f"Selected:\n" + "\n".join(str(path) for path in selected)
        )


def show_tree_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old_dialog = globals().get("_stageviz_tree_dialog")
    if old_dialog is not None:
        try:
            old_dialog.close()
            old_dialog.deleteLater()
        except Exception:
            pass

    parent = find_stageviz_main_window()
    print("[Stageviz Tree Dialog] parent:", parent)

    dialog = TreeDialog(parent)
    dialog.show()
    dialog.raise_()
    dialog.activateWindow()

    globals()["_stageviz_tree_dialog"] = dialog

    if owns_app:
        app.exec()


show_tree_dialog()