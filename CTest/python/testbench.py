# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""Reusable Qt and OpenUSD helpers for scripts in CTest/python.

Import this module from Stageviz's embedded Python interpreter. The helpers
avoid retaining Qt wrappers across event processing and stage reloads.
"""

import gc
import json
import os
import time

from pxr import Sdf, Usd


def qt():
    try:
        from PySide6 import QtCore, QtGui, QtTest, QtWidgets
    except ImportError:
        from PyQt6 import QtCore, QtGui, QtTest, QtWidgets
    return QtCore, QtGui, QtTest, QtWidgets


def _application():
    return qt()[3].QApplication.instance()


def top_level_widgets():
    """Return live top-level Qt widgets through QApplication's static API."""
    QtWidgets = qt()[3]
    if QtWidgets.QApplication.instance() is None:
        return []
    try:
        return list(QtWidgets.QApplication.topLevelWidgets())
    except RuntimeError:
        return []


def all_widgets():
    """Return live Qt widgets through QApplication's static API."""
    QtWidgets = qt()[3]
    if QtWidgets.QApplication.instance() is None:
        return []
    try:
        return list(QtWidgets.QApplication.allWidgets())
    except RuntimeError:
        return []


def process_events(wait_ms=30):
    QtCore, _, QtTest, QtWidgets = qt()
    app = QtWidgets.QApplication.instance()
    if app is None:
        return
    app.processEvents()
    if wait_ms:
        QtTest.QTest.qWait(wait_ms)
        app.processEvents()


def release_qt_wrappers():
    """Drain queued UI work and collect wrappers after a stage/UI rebuild."""
    process_events()
    gc.collect()
    process_events()


def wait_until(predicate, timeout=3.0, interval_ms=25):
    _, _, QtTest, QtWidgets = qt()
    app = QtWidgets.QApplication.instance()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if app is not None:
            app.processEvents()
        try:
            if predicate():
                process_events()
                return True
        except RuntimeError:
            # A Qt object was deleted while the UI was rebuilding; retry until
            # the predicate succeeds or the timeout expires.
            pass
        QtTest.QTest.qWait(interval_ms)
    return False


def viewer(object_name="Viewer"):
    for widget in top_level_widgets():
        try:
            if widget.objectName() == object_name:
                return widget
        except RuntimeError:
            continue
    return None


def find_widget(object_name, widget_type=None, parent=None):
    """Find a named widget and validate its native Qt object before returning."""
    _, _, _, QtWidgets = qt()
    widget_type = widget_type or QtWidgets.QWidget
    if parent is not None:
        roots = [parent]
    else:
        roots = []
        main = viewer()
        if main is not None:
            roots.append(main)
        roots.extend(top_level_widgets())

    seen = set()
    for root in roots:
        try:
            identity = id(root)
            if identity in seen:
                continue
            seen.add(identity)
            if root.objectName() == object_name and isinstance(root, widget_type):
                return root
            widget = root.findChild(widget_type, object_name)
            if widget is not None:
                widget.objectName()
                return widget
        except RuntimeError:
            continue
    return None


def widget_visible(object_name, widget_type=None, parent=None):
    widget = find_widget(object_name, widget_type, parent)
    if widget is None:
        return False
    try:
        return bool(widget.isVisible())
    except RuntimeError:
        return False


def action_info(object_name):
    """Return QAction properties as plain data, never a QAction wrapper."""
    _, QtGui, _, _ = qt()
    if _application() is None:
        return None
    for top in top_level_widgets():
        try:
            actions = top.findChildren(QtGui.QAction)
        except RuntimeError:
            continue
        for action in actions:
            try:
                if action.objectName() == object_name:
                    return {
                        "text": str(action.text()),
                        "shortcut": action.shortcut().toString(
                            QtGui.QKeySequence.SequenceFormat.PortableText
                        ),
                        "enabled": bool(action.isEnabled()),
                    }
            except RuntimeError:
                continue
    return None


def trigger_action(object_name):
    """Trigger a live, enabled QAction without returning its wrapper."""
    _, QtGui, _, QtWidgets = qt()
    if _application() is None:
        return False
    roots = []
    main = viewer()
    if main is not None:
        roots.append(main)
    try:
        roots.extend(top for top in top_level_widgets() if top is not main)
    except RuntimeError:
        pass
    for top in roots:
        try:
            actions = top.findChildren(QtGui.QAction)
        except RuntimeError:
            continue
        for action in actions:
            try:
                if action.objectName() == object_name and action.isEnabled():
                    action.trigger()
                    QtWidgets.QApplication.processEvents()
                    return True
            except RuntimeError:
                continue
    return False


def button_enabled(object_name, parent=None):
    _, _, _, QtWidgets = qt()
    parent = parent if parent is not None else viewer()
    if parent is None:
        return False
    try:
        buttons = parent.findChildren(QtWidgets.QAbstractButton)
    except RuntimeError:
        return False
    for button in buttons:
        try:
            if button.objectName() == object_name:
                return bool(button.isEnabled())
        except RuntimeError:
            continue
    return False


def click_button(object_name, parent=None):
    _, _, _, QtWidgets = qt()
    parent = parent if parent is not None else viewer()
    if parent is None:
        return False
    try:
        buttons = parent.findChildren(QtWidgets.QAbstractButton)
    except RuntimeError:
        return False
    for button in buttons:
        try:
            if button.objectName() == object_name and button.isEnabled():
                button.click()
                return True
        except RuntimeError:
            continue
    return False


def status_message(object_name="statusbar"):
    _, _, _, QtWidgets = qt()
    status = find_widget(object_name, QtWidgets.QStatusBar)
    if status is None:
        return ""
    try:
        return str(status.currentMessage())
    except RuntimeError:
        return ""


def progress_state(dialog_name="progressDialog", status_name="status", tree_name="progressTree"):
    _, _, _, QtWidgets = qt()
    dialog = find_widget(dialog_name, QtWidgets.QDialog)
    if dialog is None:
        return None, None, None
    return (
        dialog,
        find_widget(status_name, QtWidgets.QLabel, dialog),
        find_widget(tree_name, QtWidgets.QTreeWidget, dialog),
    )


def snapshot_qt_state(label="Qt checkpoint", root=None):
    """Print and return a plain-data snapshot of named widgets, dialogs, trees, and actions."""
    QtCore, QtGui, _, QtWidgets = qt()
    app = QtWidgets.QApplication.instance()
    if app is None:
        return {"label": str(label), "windows": [], "dialogs": []}

    def read_text(widget, method_name):
        try:
            method = getattr(widget, method_name, None)
            return str(method()) if callable(method) else ""
        except (RuntimeError, TypeError):
            return ""

    def tree_rows(tree, parent=None):
        rows = []
        try:
            count = tree.topLevelItemCount() if parent is None else parent.childCount()
            columns = tree.columnCount()
            for row_index in range(count):
                item = tree.topLevelItem(row_index) if parent is None else parent.child(row_index)
                if item is None:
                    continue
                try:
                    user_data = item.data(0, QtCore.Qt.ItemDataRole.UserRole + 1)
                except (NameError, RuntimeError, TypeError):
                    user_data = None
                rows.append({
                    "columns": [str(item.text(column)) for column in range(columns)],
                    "tooltips": [str(item.toolTip(column)) for column in range(columns)],
                    "identifier": str(user_data) if user_data is not None else "",
                    "expanded": bool(item.isExpanded()),
                    "selected": bool(item.isSelected()),
                    "children": tree_rows(tree, item),
                })
        except RuntimeError:
            return rows
        return rows

    roots = [root] if root is not None else top_level_widgets()
    windows = []
    dialogs = []
    for window in roots:
        try:
            window_state = {
                "objectName": str(window.objectName()),
                "class": str(window.metaObject().className()),
                "title": read_text(window, "windowTitle"),
                "visible": bool(window.isVisible()),
                "enabled": bool(window.isEnabled()),
                "widgets": [],
                "trees": [],
                "actions": [],
            }
            descendants = [window] + list(window.findChildren(QtWidgets.QWidget))
            for widget in descendants:
                try:
                    is_dialog = isinstance(widget, QtWidgets.QDialog)
                    object_name = str(widget.objectName())
                    # Include named controls everywhere and all dialog children,
                    # including unnamed labels and buttons useful in failures.
                    if widget is not window and not object_name and not is_dialog \
                            and not isinstance(window, QtWidgets.QDialog):
                        continue
                    state = {
                        "objectName": object_name,
                        "class": str(widget.metaObject().className()),
                        "visible": bool(widget.isVisible()),
                        "enabled": bool(widget.isEnabled()),
                        "text": read_text(widget, "text"),
                        "title": read_text(widget, "windowTitle"),
                        "toolTip": read_text(widget, "toolTip"),
                        "accessibleName": read_text(widget, "accessibleName"),
                    }
                    if isinstance(widget, QtWidgets.QAbstractButton):
                        state["checked"] = bool(widget.isChecked())
                    if isinstance(widget, QtWidgets.QLineEdit):
                        state["placeholder"] = read_text(widget, "placeholderText")
                    if isinstance(widget, QtWidgets.QComboBox):
                        state["currentText"] = read_text(widget, "currentText")
                    if isinstance(widget, QtWidgets.QDialog):
                        state["modal"] = bool(widget.isModal())
                        state["modality"] = str(widget.windowModality())
                        dialogs.append(state.copy())
                    if isinstance(widget, QtWidgets.QTreeWidget):
                        window_state["trees"].append({
                            "objectName": object_name,
                            "headers": [str(widget.headerItem().text(column))
                                        for column in range(widget.columnCount())]
                            if widget.headerItem() else [],
                            "rows": tree_rows(widget),
                        })
                    window_state["widgets"].append(state)
                except RuntimeError:
                    continue

            for action in window.findChildren(QtGui.QAction):
                try:
                    window_state["actions"].append({
                        "objectName": str(action.objectName()),
                        "text": str(action.text()),
                        "enabled": bool(action.isEnabled()),
                        "checkable": bool(action.isCheckable()),
                        "checked": bool(action.isChecked()),
                        "shortcut": action.shortcut().toString(
                            QtGui.QKeySequence.SequenceFormat.PortableText
                        ),
                    })
                except RuntimeError:
                    continue
            windows.append(window_state)
        except RuntimeError:
            continue

    snapshot = {"label": str(label), "windows": windows, "dialogs": dialogs}
    # The embedded Stageviz Python console may expose an ASCII-only stdout.
    # JSON escaping keeps UI labels and tooltips safe to log in that console.
    print("[ Qt Snapshot ] " + json.dumps(snapshot, ensure_ascii=True, sort_keys=True))
    return snapshot


def require(condition, message):
    if not condition:
        raise AssertionError(message)
    print("[ OK ] " + message)


def pause(title, instructions):
    QtCore, _, _, QtWidgets = qt()
    if QtWidgets.QApplication.instance() is None:
        raise RuntimeError("No Qt application is available for interactive checkpoints")
    box = QtWidgets.QMessageBox()
    box.setWindowTitle(title)
    box.setIcon(QtWidgets.QMessageBox.Icon.Information)
    box.setText(instructions)
    continue_button = box.addButton("Continue", QtWidgets.QMessageBox.ButtonRole.AcceptRole)
    box.addButton("Stop", QtWidgets.QMessageBox.ButtonRole.RejectRole)
    box.setWindowModality(QtCore.Qt.WindowModality.NonModal)
    loop = QtCore.QEventLoop()
    box.finished.connect(loop.quit)
    box.show()
    process_events(wait_ms=0)
    snapshot_qt_state("Checkpoint dialog shown: " + title)
    loop.exec()
    should_continue = box.clickedButton() == continue_button
    snapshot_qt_state("Checkpoint dialog closed: " + title)
    box.deleteLater()
    if not should_continue:
        raise RuntimeError("Interactive test stopped at: " + title)


def session():
    import stageviz
    value = stageviz.session()
    if value is None:
        raise RuntimeError("Stageviz session is unavailable")
    return value


def command():
    import stageviz
    return stageviz.command


def stage():
    value = session().stage()
    if value is None:
        raise RuntimeError("No stage is loaded")
    return value


def layer_stack(usd_stage=None, include_session_layers=False):
    usd_stage = usd_stage if usd_stage is not None else stage()
    layers = list(usd_stage.GetLayerStack(includeSessionLayers=include_session_layers))
    root = usd_stage.GetRootLayer()
    if root not in layers:
        layers.insert(0, root)
    return usd_stage, layers


def dirty_layers(usd_stage=None):
    _, layers = layer_stack(usd_stage)
    return [layer for layer in layers if layer and layer_is_dirty(layer)]


def layer_is_dirty(layer):
    return layer_property(layer, "dirty", "IsDirty")


def layer_property(layer, property_name, method_name):
    """Read a USD layer property across bindings that expose property or method APIs."""
    value = getattr(layer, property_name, None)
    if value is not None:
        return bool(value)
    return bool(getattr(layer, method_name)())


def open_layer(identifier_or_path):
    layer = Sdf.Layer.Find(identifier_or_path) or Sdf.Layer.FindOrOpen(identifier_or_path)
    if not layer:
        raise RuntimeError("Could not resolve USD layer: " + str(identifier_or_path))
    return layer


def create_layer_file(path, author=None):
    """Create and save a USD stage at path; author(stage) can add fixture data."""
    usd_stage = Usd.Stage.CreateNew(path)
    if not usd_stage:
        raise RuntimeError("Could not create USD fixture: " + path)
    if author is not None:
        author(usd_stage)
    if not usd_stage.GetRootLayer().Save():
        raise RuntimeError("Could not save USD fixture: " + path)
    return path


def create_sublayered_stage(path, sublayers, author=None):
    """Create a saved root stage with the supplied sublayer paths."""
    def populate(usd_stage):
        if author is not None:
            author(usd_stage)
        usd_stage.GetRootLayer().subLayerPaths = list(sublayers)

    return create_layer_file(path, populate)


def find_layer(usd_stage, identifier):
    for layer in usd_stage.GetLayerStack(includeSessionLayers=True):
        if layer and identifier in (layer.identifier, layer.realPath, layer.GetDisplayName()):
            return layer
    root = usd_stage.GetRootLayer()
    if root and identifier in (root.identifier, root.realPath, root.GetDisplayName()):
        return root
    return None


def normalize_path(path):
    return os.path.normcase(os.path.realpath(os.path.abspath(path)))
