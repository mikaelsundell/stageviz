# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

from PySide6 import QtCore, QtWidgets
import sys


class PythonPathsDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Python Module Paths")
        self.resize(900, 520)

        layout = QtWidgets.QVBoxLayout(self)

        info_label = QtWidgets.QLabel("Current sys.path entries:")
        layout.addWidget(info_label)

        self.list_widget = QtWidgets.QListWidget()
        self.list_widget.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)
        self.list_widget.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.list_widget.customContextMenuRequested.connect(self.showContextMenu)
        layout.addWidget(self.list_widget)

        button_row = QtWidgets.QHBoxLayout()
        layout.addLayout(button_row)

        self.reload_button = QtWidgets.QPushButton("Reload")
        self.reload_button.clicked.connect(self.populate)
        button_row.addWidget(self.reload_button)

        self.copy_button = QtWidgets.QPushButton("Copy Selected")
        self.copy_button.clicked.connect(self.copySelected)
        button_row.addWidget(self.copy_button)

        self.copy_all_button = QtWidgets.QPushButton("Copy All")
        self.copy_all_button.clicked.connect(self.copyAll)
        button_row.addWidget(self.copy_all_button)

        button_row.addStretch()

        self.close_button = QtWidgets.QPushButton("Close")
        self.close_button.clicked.connect(self.close)
        button_row.addWidget(self.close_button)

        self.populate()

    def populate(self):
        self.list_widget.clear()
        for index, path in enumerate(sys.path):
            text = f"{index:02d}: {path}"
            item = QtWidgets.QListWidgetItem(text)
            item.setData(QtCore.Qt.UserRole, path)
            self.list_widget.addItem(item)

    def copySelected(self):
        items = self.list_widget.selectedItems()
        if not items:
            return
        text = "\n".join(item.text() for item in items)
        QtWidgets.QApplication.clipboard().setText(text)

    def copySelectedValues(self):
        items = self.list_widget.selectedItems()
        if not items:
            return
        text = "\n".join(item.data(QtCore.Qt.UserRole) or "" for item in items)
        QtWidgets.QApplication.clipboard().setText(text)

    def copyItemText(self, item):
        if not item:
            return
        QtWidgets.QApplication.clipboard().setText(item.text())

    def copyItemValue(self, item):
        if not item:
            return
        QtWidgets.QApplication.clipboard().setText(item.data(QtCore.Qt.UserRole) or "")

    def copyAll(self):
        text = "\n".join(self.list_widget.item(i).text() for i in range(self.list_widget.count()))
        QtWidgets.QApplication.clipboard().setText(text)

    def copyAllValues(self):
        text = "\n".join(
            (self.list_widget.item(i).data(QtCore.Qt.UserRole) or "")
            for i in range(self.list_widget.count())
        )
        QtWidgets.QApplication.clipboard().setText(text)

    def showContextMenu(self, pos):
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
            self.copyItemValue(item)
        elif action == copy_text_action:
            self.copyItemText(item)
        elif action == copy_selected_values_action:
            self.copySelectedValues()
        elif action == copy_selected_rows_action:
            self.copySelected()
        elif action == copy_all_values_action:
            self.copyAllValues()
        elif action == copy_all_rows_action:
            self.copyAll()


def show_python_paths_dialog():
    app = QtWidgets.QApplication.instance()
    if app is None:
        raise RuntimeError("No QApplication instance available")

    dialog = PythonPathsDialog(app.activeWindow())
    dialog.setAttribute(QtCore.Qt.WA_DeleteOnClose)
    dialog.show()
    return dialog


python_paths_dialog = show_python_paths_dialog()