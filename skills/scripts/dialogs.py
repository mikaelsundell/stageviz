"""Reusable Stageviz Qt dialog helpers.

This module is intended as a scaffold/reference for generated Stageviz Python
UI tools. It supports PySide6 and PyQt6, parents tools to the Stageviz main
window when possible, and provides helpers for standalone dialogs and dock
widgets without assuming undocumented Stageviz convenience globals.
"""

from __future__ import annotations

import sys
import traceback

import stageviz

try:
    from PySide6 import QtCore, QtGui, QtWidgets
    QT_BINDING = "PySide6"
except ImportError:
    from PyQt6 import QtCore, QtGui, QtWidgets
    QT_BINDING = "PyQt6"


def get_session():
    """Return the current Stageviz Session wrapper."""
    return stageviz.Session()


def get_stage():
    """Return (stage, session) for the currently loaded Stageviz stage."""
    session = get_session()
    stage = session.stage()
    if not stage:
        raise RuntimeError("No stage loaded.")
    return stage, session


def selected_paths(session=None):
    """Return the current Stageviz selection as strings."""
    if session is None:
        session = get_session()
    return [str(path) for path in session.paths()]


def find_stageviz_main_window():
    """Return the Stageviz main window when available."""
    try:
        app = stageviz.Application()
        window = app.window()
        if window is not None:
            return window
    except Exception:
        traceback.print_exc()

    qt_app = QtWidgets.QApplication.instance()
    if qt_app is None:
        return None
    return qt_app.activeWindow()


def ensure_qapplication():
    """Return (QApplication, owns_app)."""
    app = QtWidgets.QApplication.instance()
    if app is not None:
        return app, False
    return QtWidgets.QApplication(sys.argv), True


def close_existing(globals_dict, global_name):
    """Close and release a previously retained dialog or dock widget."""
    old = globals_dict.get(global_name)
    if old is None:
        return

    try:
        old.close()
        old.deleteLater()
    except Exception:
        traceback.print_exc()

    globals_dict[global_name] = None


def show_dialog(dialog_type, globals_dict, global_name, *args, **kwargs):
    """Create, show, retain, and return a non-modal standalone dialog.

    The dialog class is expected to configure its own object name, flags,
    and WA_DeleteOnClose behavior. This helper handles QApplication creation,
    main-window parenting, replacement of an older instance, and lifetime.
    """
    app, owns_app = ensure_qapplication()
    close_existing(globals_dict, global_name)

    parent = kwargs.pop("parent", None)
    if parent is None:
        parent = find_stageviz_main_window()

    window = dialog_type(parent, *args, **kwargs)
    window.show()
    window.raise_()
    window.activateWindow()
    globals_dict[global_name] = window

    if owns_app:
        app.exec()

    return window


def show_dock(dock_type, globals_dict, global_name, area=None, *args, **kwargs):
    """Create, register, retain, and return a Stageviz QDockWidget."""
    app, owns_app = ensure_qapplication()
    close_existing(globals_dict, global_name)

    main_window = kwargs.pop("parent", None)
    if main_window is None:
        main_window = find_stageviz_main_window()
    if main_window is None:
        raise RuntimeError("Could not resolve the Stageviz main window.")
    if not isinstance(main_window, QtWidgets.QMainWindow):
        raise TypeError("Stageviz main window is not a QMainWindow.")

    if area is None:
        area = QtCore.Qt.DockWidgetArea.RightDockWidgetArea

    dock = dock_type(main_window, *args, **kwargs)
    main_window.addDockWidget(area, dock)
    dock.show()
    dock.raise_()
    globals_dict[global_name] = dock

    if owns_app:
        app.exec()

    return dock


class ToolDialog(QtWidgets.QDialog):
    """Base class for non-modal Stageviz tool dialogs."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)


class ShortcutAwareDialog(ToolDialog):
    """Tool dialog base that can claim dialog-local standard shortcuts.

    Subclasses can override handle_select_all() and return True when handled.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        app = QtWidgets.QApplication.instance()
        if app is not None:
            app.installEventFilter(self)

    def handle_select_all(self):
        return False

    def eventFilter(self, obj, event):
        if self.isActiveWindow():
            event_type = event.type()

            if event_type == QtCore.QEvent.Type.ShortcutOverride:
                if event.matches(QtGui.QKeySequence.StandardKey.SelectAll):
                    event.accept()
                    return True

            if event_type == QtCore.QEvent.Type.KeyPress:
                if event.matches(QtGui.QKeySequence.StandardKey.SelectAll):
                    if self.handle_select_all():
                        event.accept()
                        return True

        return super().eventFilter(obj, event)


class DockTool(QtWidgets.QDockWidget):
    """Base class for Stageviz dockable tools.

    Create a child QWidget and install it with setWidget(). Do not apply the
    Qt.Tool window flag to dock widgets.
    """

    def __init__(self, title="Stageviz Tool", parent=None):
        super().__init__(title, parent)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)


def qcolor(value):
    """Return a defensive QColor copy."""
    return QtGui.QColor(value)


def run_ui_action(owner, callable_, success_message=None):
    """Run a dialog action with concise UI error reporting when possible."""
    try:
        result = callable_()
        if success_message and hasattr(owner, "status_label"):
            owner.status_label.setText(success_message)
        return result
    except Exception as exc:
        if hasattr(owner, "status_label"):
            owner.status_label.setText(f"Error: {type(exc).__name__}: {exc}")
        traceback.print_exc()
        return None
