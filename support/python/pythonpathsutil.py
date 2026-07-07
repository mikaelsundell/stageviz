# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

import sys
import traceback

from PySide6 import QtCore, QtWidgets


DIALOG_OBJECT_NAME = "stagevizPythonPathUtil"
DIALOG_GLOBAL_NAME = "_stageviz_python_path_util"


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


class PythonPathUtil(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle("Stageviz Python Path Util")
        self.resize(900, 520)

        self.activated = False
        self.last_pos = None

        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)

        app = QtWidgets.QApplication.instance()
        if app is not None:
            app.applicationStateChanged.connect(self._application_state_changed)

        layout = QtWidgets.QVBoxLayout(self)

        info_label = QtWidgets.QLabel("Current sys.path entries:")
        layout.addWidget(info_label)

        self.list_widget = QtWidgets.QListWidget()
        self.list_widget.setSelectionMode(QtWidgets.QAbstractItemView.SelectionMode.ExtendedSelection)
        self.list_widget.setContextMenuPolicy(QtCore.Qt.ContextMenuPolicy.CustomContextMenu)
        self.list_widget.customContextMenuRequested.connect(self.show_context_menu)
        layout.addWidget(self.list_widget)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.reload_button = QtWidgets.QPushButton("Reload")
        self.copy_button = QtWidgets.QPushButton("Copy Selected")
        self.copy_all_button = QtWidgets.QPushButton("Copy All")
        self.close_button = QtWidgets.QPushButton("Close")

        button_row.addWidget(self.reload_button)
        button_row.addWidget(self.copy_button)
        button_row.addWidget(self.copy_all_button)
        button_row.addStretch(1)
        button_row.addWidget(self.close_button)

        self.reload_button.clicked.connect(self.populate)
        self.copy_button.clicked.connect(self.copy_selected)
        self.copy_all_button.clicked.connect(self.copy_all)
        self.close_button.clicked.connect(self.close)

        self.populate()

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

    def populate(self):
        self.list_widget.clear()

        for index, path in enumerate(sys.path):
            text = f"{index:02d}: {path}"
            item = QtWidgets.QListWidgetItem(text)
            item.setData(QtCore.Qt.ItemDataRole.UserRole, path)
            self.list_widget.addItem(item)

    def copy_selected(self):
        items = self.list_widget.selectedItems()
        if not items:
            return

        text = "\n".join(item.text() for item in items)
        QtWidgets.QApplication.clipboard().setText(text)

    def copy_selected_values(self):
        items = self.list_widget.selectedItems()
        if not items:
            return

        text = "\n".join(item.data(QtCore.Qt.ItemDataRole.UserRole) or "" for item in items)
        QtWidgets.QApplication.clipboard().setText(text)

    def copy_item_text(self, item):
        if not item:
            return

        QtWidgets.QApplication.clipboard().setText(item.text())

    def copy_item_value(self, item):
        if not item:
            return

        QtWidgets.QApplication.clipboard().setText(
            item.data(QtCore.Qt.ItemDataRole.UserRole) or ""
        )

    def copy_all(self):
        text = "\n".join(
            self.list_widget.item(i).text()
            for i in range(self.list_widget.count())
        )
        QtWidgets.QApplication.clipboard().setText(text)

    def copy_all_values(self):
        text = "\n".join(
            self.list_widget.item(i).data(QtCore.Qt.ItemDataRole.UserRole) or ""
            for i in range(self.list_widget.count())
        )
        QtWidgets.QApplication.clipboard().setText(text)

    def show_context_menu(self, pos):
        item = self.list_widget.itemAt(pos)
        global_pos = self.list_widget.viewport().mapToGlobal(pos)

        menu = QtWidgets.QMenu(self)

        copy_value_action = menu.addAction("Copy Value")
        copy_text_action = menu.addAction("Copy Row")

        if self.list_widget.selectedItems():
            menu.addSeparator()
            copy_selected_values_action = menu.addAction("Copy Selected Values")
            copy_selected_rows_action = menu.addAction("Copy Selected Rows")
        else:
            copy_selected_values_action = None
            copy_selected_rows_action = None

        menu.addSeparator()
        copy_all_values_action = menu.addAction("Copy All Values")
        copy_all_rows_action = menu.addAction("Copy All Rows")

        if item is None:
            copy_value_action.setEnabled(False)
            copy_text_action.setEnabled(False)

        action = menu.exec(global_pos)
        if action is None:
            return

        if action == copy_value_action:
            self.copy_item_value(item)
        elif action == copy_text_action:
            self.copy_item_text(item)
        elif action == copy_selected_values_action:
            self.copy_selected_values()
        elif action == copy_selected_rows_action:
            self.copy_selected()
        elif action == copy_all_values_action:
            self.copy_all_values()
        elif action == copy_all_rows_action:
            self.copy_all()


def show_python_path_util():
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

    win = PythonPathUtil(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


python_path_util = show_python_path_util()