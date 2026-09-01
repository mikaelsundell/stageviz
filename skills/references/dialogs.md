# Stageviz Python Dialogs

Use this reference when creating, modifying, or debugging Stageviz Python UI tools such as standalone dialogs, dockable panels, inspectors, browsers, material/look creators, primitive tools, and selection-driven utilities.

## Contents

- Core rules
- Imports and compatibility
- Session, stage, and selection access
- Parent window lookup
- Standalone dialog lifecycle
- Dockable panels
- Qt color handling
- USD authoring conventions
- MaterialX authoring
- Material binding
- Batch authoring and prim updates
- Shortcut ownership
- Error handling
- Editing existing dialogs
- Output checklist

## Core rules

- Return a complete runnable script unless the user explicitly asks for a patch.
- Prefer a non-modal Qt tool parented to the Stageviz main window.
- Use `QDockWidget` only when the user asks for a dockable panel/editor.
- Support both PySide6 and PyQt6 unless the user's existing script deliberately targets one binding.
- Use the current Stageviz session and currently loaded USD stage. Do not open a separate stage unless requested.
- Use the current Stageviz selection for selection-driven tools.
- Preserve the user's existing coding style and lifecycle scaffolding when modifying supplied code.
- Prefer `stageviz.command` for operations that should participate in Stageviz undo/redo. Use direct `pxr` authoring only when the Stageviz command API does not expose the operation.
- Treat [python-api.md](python-api.md) as canonical for Stageviz-specific methods and signatures.

## Imports and compatibility

Use this pattern when applicable:

```python
import sys
import traceback

from pxr import Gf, Sdf, UsdGeom, UsdShade

import stageviz

try:
    from PySide6 import QtCore, QtGui, QtWidgets
    QT_NAME = "PySide6"
except Exception:
    from PyQt6 import QtCore, QtGui, QtWidgets
    QT_NAME = "PyQt6"
```

Only import extra USD modules when needed.

For cross-binding signals, avoid assuming `QtCore.Signal` exists in PyQt6. Use a helper when defining custom signals:

```python
Signal = getattr(QtCore, "Signal", getattr(QtCore, "pyqtSignal", None))
```

## Session, stage, and selection access

Prefer the API proven by the bundled binding reference:

```python
def get_session():
    return stageviz.Session()


def get_stage():
    session = get_session()
    stage = session.stage()
    if not stage:
        raise RuntimeError("No stage loaded.")
    return stage, session


def selected_paths(session):
    return [str(path) for path in session.paths()]
```

If a user-provided Stageviz build demonstrably exposes convenience functions such as `stageviz.session()` or `stageviz.application()`, preserve them in existing code. Do not invent those module-level functions from C++ naming alone.

For selection-driven tools:

```python
paths = selected_paths(session)
if not paths:
    raise RuntimeError("No prims selected.")
```

## Parent window lookup

Use the public `Application` wrapper first:

```python
def find_stageviz_main_window():
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
```

`Application.window()` relies on PySide6/shiboken6 or PyQt6/sip wrapping support.

## Standalone dialog lifecycle

Give each dialog a stable object name and global reference:

```python
DIALOG_OBJECT_NAME = "stagevizExampleDialog"
DIALOG_GLOBAL_NAME = "_stageviz_example_dialog"
```

For a standalone dialog:

```python
class ExampleDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName(DIALOG_OBJECT_NAME)
        self.setWindowTitle("Example")
        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)
```

Use a single show function that closes an earlier instance and keeps the new dialog alive:

```python
def show_example_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(sys.argv)
        owns_app = True

    old_window = globals().get(DIALOG_GLOBAL_NAME)
    if old_window is not None:
        try:
            old_window.close()
            old_window.deleteLater()
        except Exception:
            pass
        globals()[DIALOG_GLOBAL_NAME] = None

    parent = find_stageviz_main_window()
    window = ExampleDialog(parent)
    window.show()
    window.raise_()
    window.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = window

    if owns_app:
        app.exec()

    return window


example_dialog = show_example_dialog()
```

When Stageviz already owns the Qt application, do not start a second event loop.

`stageviz.Application().show(widget)` is also available and retains the widget internally while scheduling `show()`, `raise_()`, and `activateWindow()` on the Qt event loop. Prefer the explicit lifecycle above when the tool needs replacement-by-object-name, docking, or custom cleanup.

## Dockable panels

For dockable tools:

- Subclass `QDockWidget` or create one explicitly.
- Parent it to the Stageviz main window.
- Put controls in a child content widget and call `setWidget(content)`.
- Register it with `QMainWindow.addDockWidget(...)`.
- Do not apply the standalone `Qt::Tool` window flag.
- Give the dock a stable object name so saved Qt window state can identify it.

Example:

```python
class ExampleDock(QtWidgets.QDockWidget):
    def __init__(self, parent=None):
        super().__init__("Example", parent)
        self.setObjectName("stagevizExampleDock")

        content = QtWidgets.QWidget(self)
        layout = QtWidgets.QVBoxLayout(content)
        layout.addWidget(QtWidgets.QLabel("Stageviz tool"))
        self.setWidget(content)


def show_example_dock():
    main_window = find_stageviz_main_window()
    if main_window is None or not hasattr(main_window, "addDockWidget"):
        raise RuntimeError("Stageviz main window is not available as a QMainWindow.")

    old = main_window.findChild(QtWidgets.QDockWidget, "stagevizExampleDock")
    if old is not None:
        old.raise_()
        old.show()
        return old

    dock = ExampleDock(main_window)
    main_window.addDockWidget(QtCore.Qt.DockWidgetArea.RightDockWidgetArea, dock)
    dock.show()
    return dock
```

## Qt color handling

Always store a real `QColor`, not a `Qt.GlobalColor` enum value:

```python
class ColorButton(QtWidgets.QPushButton):
    colorChanged = Signal(object)

    def __init__(self, color, parent=None):
        super().__init__(parent)
        self._color = QtGui.QColor(color)
        self.clicked.connect(self.choose_color)
        self.update_style()

    def color(self):
        return QtGui.QColor(self._color)

    def set_color(self, color):
        self._color = QtGui.QColor(color)
        self.update_style()
        self.colorChanged.emit(QtGui.QColor(self._color))

    def update_style(self):
        color_name = self._color.name()
        self.setText(color_name.upper())
        self.setStyleSheet(
            "QPushButton {"
            f"background-color: {color_name};"
            "padding: 5px;"
            "}"
        )

    def choose_color(self):
        color = QtWidgets.QColorDialog.getColor(self._color, self, "Choose Color")
        if color.isValid():
            self.set_color(color)
```

Create floating-point RGB colors with:

```python
QtGui.QColor.fromRgbF(r, g, b, 1.0)
```

Convert to USD:

```python
def vec3_from_qcolor(color):
    color = QtGui.QColor(color)
    return Gf.Vec3f(color.redF(), color.greenF(), color.blueF())
```

## USD authoring conventions

Use valid `Sdf.ValueTypeNames` members. In particular use `Sdf.ValueTypeNames.Int`, not `Integer`.

Common useful names include:

- `Float`
- `Float2`
- `Float3`
- `Color3f`
- `Int`
- `String`
- `Token`

When a traceback reports a missing `ValueTypeNames` attribute, correct the type name instead of masking the exception.

For generated looks, `/World/Looks/<MaterialName>` is a reasonable default only when it matches the current project structure. Preserve a user's existing stage organization when it deliberately differs.

Example helpers:

```python
def ensure_xform(stage, path):
    prim = stage.GetPrimAtPath(path)
    if prim and prim.IsValid():
        return UsdGeom.Xform(prim)
    return UsdGeom.Xform.Define(stage, path)


def ensure_looks_scope(stage):
    ensure_xform(stage, "/World")
    ensure_xform(stage, "/World/Looks")
```

## MaterialX authoring

For generated looks, prefer MaterialX through `UsdShade` unless the user requests another shading system.

A basic Standard Surface network:

```python
material = UsdShade.Material.Define(stage, material_path)
surface = UsdShade.Shader.Define(stage, material_path.AppendChild("Surface"))
surface.CreateIdAttr("ND_standard_surface_surfaceshader")
surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f).Set(base_color)
surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(0.0)
surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.4)
surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(0.5)
surface.CreateOutput("out", Sdf.ValueTypeNames.Token)
material.CreateSurfaceOutput().ConnectToSource(surface.ConnectableAPI(), "out")
```

Useful car-paint inputs include `base_color`, `roughness`, `specular`, `metalness`, `coat`, and `coat_roughness`.

For procedural networks:

```python
def connect_input(shader_input, source_shader, source_output_name):
    shader_input.ConnectToSource(source_shader.ConnectableAPI(), source_output_name)
```

Do not assume every MaterialX node identifier or input exists in every Stageviz/OpenUSD/MaterialX build. If authoring succeeds but rendering fails:

1. Identify the first unsupported node identifier or input.
2. Reduce to a known-good Standard Surface.
3. Add procedural nodes back incrementally.
4. Keep useful diagnostic output until the network renders correctly.

## Material binding

Prefer the Stageviz command API when an undoable bind is desired:

```python
stageviz.command.bind_material(paths, material_path_string)
```

When direct USD binding is intentionally required:

```python
def bind_material(prim, material):
    UsdShade.MaterialBindingAPI.Apply(prim).Bind(material)
```

Recursive traversal helper:

```python
def iter_bindable_prims(prim, recursive=True):
    if not prim or not prim.IsValid():
        return

    if UsdGeom.Imageable(prim):
        yield prim

    if recursive:
        for child in prim.GetChildren():
            yield from iter_bindable_prims(child, recursive=True)
```

For material tools, an `Apply recursively to children` option is useful unless recursion would be misleading or unsafe.

## Batch authoring and prim updates

Large direct-USD authoring operations may benefit from temporarily deferring Stageviz prim-update processing. The canonical API proves these methods exist:

- `session.primsUpdate()`
- `session.setPrimsUpdate(value)`
- `session.flushPrimsUpdates()`

The current binding reference does **not** prove symbolic enum members or numeric ordinals for the prim-update policy. Therefore:

- Do not invent `stageviz.Session.PrimsUpdate.Deferred` or `Immediate` unless the user's installed Stageviz API or supplied code proves those symbols exist.
- Preserve and restore the prior value in `finally` whenever changing update policy.
- If the required deferred-policy value is unknown, do not guess it. Either use a user-supplied/documented value or omit the optimization.

Safe structural pattern when a known deferred value is available:

```python
previous_update = session.primsUpdate()
session.setPrimsUpdate(DEFERRED_VALUE)
try:
    # Perform the batch of direct USD edits.
    ...
finally:
    session.setPrimsUpdate(previous_update)
    session.flushPrimsUpdates()
```

Use this only for sufficiently large edits. A one-attribute or one-shader-input change does not need deferred processing.

## Shortcut ownership

Stageviz main-window actions may own application shortcuts such as Select All. A non-modal tool should explicitly claim shortcuts that have dialog-local meaning so the main-window action does not also fire.

For dialog-wide ownership, install an application event filter and handle both `ShortcutOverride` and `KeyPress` while the tool window is active:

```python
def eventFilter(self, obj, event):
    if self.isActiveWindow():
        event_type = event.type()

        if event_type == QtCore.QEvent.Type.ShortcutOverride:
            if event.matches(QtGui.QKeySequence.StandardKey.SelectAll):
                event.accept()
                return True

        if event_type == QtCore.QEvent.Type.KeyPress:
            if event.matches(QtGui.QKeySequence.StandardKey.SelectAll):
                self.select_all_visible_items()
                event.accept()
                return True

    return super().eventFilter(obj, event)
```

Install it once during initialization:

```python
app = QtWidgets.QApplication.instance()
if app is not None:
    app.installEventFilter(self)
```

Remove the event filter during cleanup when the dialog can be destroyed while the application remains alive.

Use `QKeySequence.StandardKey` rather than hard-coded platform modifiers. For filtered list tools, Select All should normally select only currently visible/filter-matched rows. Reuse the same selection function for shortcuts and buttons.

If a shortcut should apply only to one widget and its children, a widget-owned `QShortcut` with `WidgetWithChildrenShortcut` is acceptable.

## Error handling

Wrap user-triggered operations in `try/except`, show a concise error in the UI, and print the traceback:

```python
def apply_operation(self):
    try:
        # Gather controls, validate selection, author USD or run commands.
        self.status_label.setText("Applied successfully.")
    except Exception as exc:
        self.status_label.setText(f"Error: {type(exc).__name__}: {exc}")
        traceback.print_exc()
```

When the user supplies a traceback, fix the immediate failing line and exception first. Do not redesign unrelated Stageviz code to solve a local compatibility error.

## Editing existing dialogs

When adapting a working Stageviz dialog:

- Preserve its session-access pattern if it is proven to work in that Stageviz build.
- Preserve parent-window lookup and lifecycle code.
- Preserve recursive-selection behavior unless the new tool needs different semantics.
- Reuse established helpers rather than introducing a second architecture.
- Replace only the tool-specific controls and operation logic when possible.
- Preserve formatting and unrelated whitespace when the user supplies a source file and asks for edits.

## Dialog UI conventions

For material/look dialogs, prefer:

1. A short description.
2. `QFormLayout` for editable parameters.
3. Optional preset controls.
4. Optional recursive-selection checkbox.
5. Status label.
6. Apply and Close buttons.

Use `QDoubleSpinBox` for bounded numeric shader parameters instead of free-form text. Common normalized ranges include roughness, metalness, specular, and coat in `0.0 .. 1.0`.

Preset buttons should normally update controls without silently applying changes unless the user explicitly requests live application.

## Output checklist

Before returning a Stageviz dialog script, verify:

- It is complete and runnable in the Stageviz Python editor.
- It uses the current Stageviz stage rather than opening a separate stage.
- Empty selection is handled when selection is required.
- Stageviz-specific methods exist in `python-api.md` or are proven by user-supplied code.
- Undoable user edits use `stageviz.command` when an appropriate command exists.
- No enum value is invented.
- Large direct USD authoring restores prim-update state in `finally` if update deferral is used.
- `QColor` handling uses actual `QtGui.QColor` instances.
- `Sdf.ValueTypeNames.Int` is used for integer shader inputs.
- Material binding uses `stageviz.command.bind_material` for undoable binding or `UsdShade.MaterialBindingAPI` for intentional direct authoring.
- Material paths are absolute valid `Sdf.Path` values.
- MaterialX outputs are connected to the material surface output.
- Standalone dialogs are non-modal and retain a global reference.
- Dockable tools use `QDockWidget`, a child content widget, and `addDockWidget()` on the Stageviz main window.
- Dialog-local standard shortcuts suppress conflicting Stageviz main-window shortcuts while active.
- User-triggered errors are visible in the UI and print a traceback.
