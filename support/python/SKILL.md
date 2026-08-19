---
name: stageviz-python-dialogs
description: Create, modify, and debug standalone Python dialogs for Stageviz using PySide6/PyQt6, the Stageviz session API, OpenUSD, UsdShade, and MaterialX. Use when asked to build Stageviz shelf/dialog scripts such as material/look creators, primitive tools, inspection utilities, or selection-based actions; when adapting an existing Stageviz Python dialog into another tool; or when fixing runtime errors in these scripts and returning a complete runnable script.
---

# Stageviz Python Dialogs

Create complete, self-contained Python tools that run inside Stageviz and follow the established Stageviz dialog conventions.

## Core requirements

- Return a complete runnable script, not partial snippets, unless the user explicitly asks for only a patch.
- Prefer a non-modal Qt tool dialog parented to the Stageviz main window.
- Support both PySide6 and PyQt6.
- Use the existing Stageviz session and current stage rather than opening a new USD stage.
- Operate on the current Stageviz selection when the tool is selection-based.
- Keep authored USD data organized and predictable.
- Use MaterialX nodes through `UsdShade` for generated looks unless the user requests another shading system.
- Avoid external texture dependencies for procedural examples unless requested.
- Preserve the user's existing coding style when modifying a supplied script.

## Standard imports

Start from this pattern when applicable:

```python
import sys
import traceback

from pxr import UsdShade, UsdGeom, Sdf, Gf
import stageviz

try:
    from PySide6 import QtWidgets, QtCore, QtGui
    qt_name = "PySide6"
except Exception:
    from PyQt6 import QtWidgets, QtCore, QtGui
    qt_name = "PyQt6"
```

Only import additional USD modules when needed.

## Access the Stageviz session and stage

Use this compatibility pattern:

```python
def get_session():
    try:
        return stageviz.session()
    except Exception:
        return stageviz.Session()


def get_stage():
    session = get_session()
    stage = session.stage()

    if not stage:
        raise RuntimeError("No stage loaded.")

    return stage, session
```

Do not create a separate stage when the user's intent is to modify the stage currently open in Stageviz.

## Read the current selection

Use:

```python
def selected_paths(session):
    try:
        return [str(path) for path in session.paths()]
    except Exception:
        return []
```

For selection-driven tools, report a clear error if no prims are selected:

```python
if not paths:
    raise RuntimeError("No prims selected.")
```

## Find the Stageviz parent window

Use this pattern:

```python
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
```

## Dialog lifecycle

Give every dialog a stable object name and global reference:

```python
DIALOG_OBJECT_NAME = "stagevizExampleDialog"
DIALOG_GLOBAL_NAME = "_stageviz_example_dialog"
```

Use a non-modal tool window:

```python
self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)
```

Use a `show_*_dialog()` function that closes any previous instance before creating the new one:

```python
def show_example_dialog():
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

    win = ExampleDialog(parent)
    win.show()
    win.raise_()
    win.activateWindow()

    globals()[DIALOG_GLOBAL_NAME] = win

    if owns_app:
        app.exec()

    return win


example_dialog = show_example_dialog()
```

Keep the final global assignment so the dialog remains alive in embedded Python environments.

## QColor handling

Always use a real `QtGui.QColor`. Do not store `QtCore.Qt.GlobalColor` values and then assume they have the same API.

Use:

```python
class ColorButton(QtWidgets.QPushButton):
    colorChanged = QtCore.Signal(object)

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
        self.colorChanged.emit(self._color)

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
        color = QtWidgets.QColorDialog.getColor(
            self._color,
            self,
            "Choose Color",
        )

        if color.isValid():
            self.set_color(color)
```

Create RGB colors directly:

```python
QtGui.QColor.fromRgbF(r, g, b, 1.0)
```

Convert to USD color values with:

```python
def vec3_from_qcolor(color):
    color = QtGui.QColor(color)
    return Gf.Vec3f(color.redF(), color.greenF(), color.blueF())
```

## USD type names

Use valid `Sdf.ValueTypeNames` names. In particular:

- Use `Sdf.ValueTypeNames.Int`, not `Sdf.ValueTypeNames.Integer`.
- Use `Float`, `Float2`, `Float3`, `Color3f`, `String`, and `Token` as appropriate.

When a runtime error reports a missing `ValueTypeNames` attribute, correct the USD type name rather than masking the error.

## Material locations

For generated looks, prefer:

```text
/World/Looks/<MaterialName>
```

Use helpers such as:

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

If the user's existing project deliberately avoids `/World`, preserve its stage organization rather than forcing this convention.

## MaterialX Standard Surface

For simple generated materials, prefer MaterialX Standard Surface:

```python
surface = UsdShade.Shader.Define(
    stage,
    material_path.AppendChild("Surface"),
)

surface.CreateIdAttr("ND_standard_surface_surfaceshader")
surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f).Set(base_color)
surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(0.0)
surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.4)
surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(0.5)
surface.CreateOutput("out", Sdf.ValueTypeNames.Token)

material.CreateSurfaceOutput().ConnectToSource(
    surface.ConnectableAPI(),
    "out",
)
```

For car paint, useful inputs include:

- `base_color`
- `roughness`
- `specular`
- `metalness`
- `coat`
- `coat_roughness`

For transparent looks, consider `transmission` and `opacity` only when the renderer supports the desired behavior.

## MaterialX procedural networks

Create helper connections with:

```python
def connect_input(shader_input, source_shader, source_output_name):
    shader_input.ConnectToSource(
        source_shader.ConnectableAPI(),
        source_output_name,
    )
```

For procedural materials, prefer object-space or world-space inputs when avoiding UV requirements.

Do not assume every MaterialX node definition exists in every Stageviz/OpenUSD/MaterialX build. Nodes such as noise, extract, sine, mix, or utility nodes can vary by MaterialX version and renderer support.

When a generated material fails after USD authoring succeeds:

1. Identify the first unsupported node identifier or input name.
2. Simplify the network to a known-good Standard Surface.
3. Add procedural nodes incrementally.
4. Keep debugging output until the network renders correctly.

Do not rewrite unrelated Stageviz code when the failure is isolated to MaterialX compatibility.

## Bind generated materials

Use the Material Binding API:

```python
def bind_material(prim, material):
    UsdShade.MaterialBindingAPI.Apply(prim).Bind(material)
```

For recursive application:

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

Expose an `Apply recursively to children` checkbox by default for material tools unless recursion would be unsafe or misleading.

## Batch scene changes and deferred updates

When a tool will author many USD changes in one operation, temporarily defer Stageviz prim updates so Stageviz does not repeatedly process notices, refresh UI state, or recompute scene bounds after every individual edit. This is especially important for tools that create or modify many prims, payloads, materials, bindings, transforms, or attributes.

Prefer this pattern when the Python bindings expose the corresponding Session API:

```python
session = get_session()
previous_update = session.primsUpdate()

session.setPrimsUpdate(stageviz.Session.PrimsUpdate.Deferred)

try:
    # Perform the complete batch of USD edits here.
    ...
finally:
    session.setPrimsUpdate(previous_update)

    # Restoring Immediate already flushes queued updates in the current Session API.
    # Flush explicitly only if the previous mode was not Immediate.
    if previous_update != stageviz.Session.PrimsUpdate.Immediate:
        session.flushPrimsUpdates()
```

Always restore the previous update mode in `finally`, including when authoring raises an exception. Do not permanently force `Immediate` mode, because the caller or another Stageviz workflow may already be using deferred updates.

For small edits such as changing one shader input or one prim property, deferred updates are unnecessary. Use them when batching enough edits that repeated Stageviz notice handling would be wasteful.

## Dialog UI conventions

For look/material dialogs, prefer this layout:

1. Short description.
2. `QFormLayout` for editable parameters.
3. Optional preset group.
4. Recursive-selection checkbox.
5. Status label.
6. Apply and Close buttons.

Use `QDoubleSpinBox` instead of free-form text for numeric shader parameters. Choose ranges that prevent obviously invalid values while still allowing experimentation.

Examples:

- Roughness: `0.0 .. 1.0`
- Metalness: `0.0 .. 1.0`
- Specular: `0.0 .. 1.0`
- Coat: `0.0 .. 1.0`
- Procedural scale: use a wider positive range appropriate to the shader

Presets should update the controls and then let the user press Apply. Do not silently apply a material merely because a preset button was clicked unless the user requested live application.
\n## Dialog shortcut ownership\n\nStageviz main-window actions may define application shortcuts such as `Ctrl+A`. A non-modal tool dialog must explicitly claim shortcuts that have a dialog-local meaning; otherwise the Stageviz menu action can fire while the user is working inside the dialog.\n\nFor shortcuts that should belong to the entire dialog whenever the tool window is active, prefer an application event filter that accepts `ShortcutOverride` and handles the matching `KeyPress`:\n\n```python\ndef eventFilter(self, obj, event):\n    if self.isActiveWindow():\n        event_type = event.type()\n\n        if event_type == QtCore.QEvent.Type.ShortcutOverride:\n            if event.matches(QtGui.QKeySequence.StandardKey.SelectAll):\n                event.accept()\n                return True\n\n        if event_type == QtCore.QEvent.Type.KeyPress:\n            if event.matches(QtGui.QKeySequence.StandardKey.SelectAll):\n                self.select_all_visible_items()\n                event.accept()\n                return True\n\n    return super().eventFilter(obj, event)\n```\n\nInstall the filter once during dialog initialization:\n\n```python\napp = QtWidgets.QApplication.instance()\nif app is not None:\n    app.installEventFilter(self)\n```\n\nUse `QKeySequence.StandardKey` rather than hard-coding platform modifiers so standard shortcuts behave correctly on macOS, Windows, and Linux.\n\nFor a filtered list editor, `Select All` should normally select only currently visible/filter-matched rows rather than hidden rows. Reuse the same function for the dialog shortcut and any `Select Filtered` button so the behavior stays consistent.\n\nIf a shortcut should apply only while a particular widget has focus, a widget-owned `QShortcut` with `WidgetWithChildrenShortcut` is acceptable. Prefer the dialog-wide event-filter approach when the main Stageviz window already owns the same shortcut and the tool needs to suppress that main-window action while active.\n
## Error handling

Wrap the Apply operation in `try/except`:

```python
def apply_material(self):
    try:
        # Gather UI values.
        # Author USD/material.
        # Bind to selection.
        self.status_label.setText("Applied material successfully.")
    except Exception as e:
        self.status_label.setText(
            f"Error: {type(e).__name__}: {e}"
        )
        traceback.print_exc()
```

Show the concise error in the dialog and print the traceback for debugging.

When the user supplies a traceback, use the exact failing line and exception to identify the immediate cause. Fix that cause first and return the complete updated script when requested.

## Editing an existing dialog

When the user supplies a working Stageviz dialog as a template:

- Preserve its session access pattern.
- Preserve its parent-window lookup.
- Preserve its dialog lifecycle.
- Preserve recursive selection handling unless the new tool needs different behavior.
- Reuse established helper functions rather than introducing a second architecture.
- Replace only the tool-specific controls and operation logic.

For example, when converting a wood-material dialog into a car-paint dialog, keep the dialog scaffolding and replace the shader network and parameter controls.

## Output expectations

When asked for code:

- Return one complete script.
- Keep names descriptive, such as `WoodDialog`, `CarPaintDialog`, or `PrimitiveDialog`.
- End with the call that opens the dialog.
- Do not omit helper functions needed for standalone execution.

When asked for a downloadable script, create the `.py` file and provide the download link instead of only pasting the code.

When the user reports an error after testing a generated script, treat the last returned script as the baseline and modify that script rather than starting over.

## Quality checklist

Before returning a Stageviz dialog script, verify:

- Qt imports include `QtGui` when `QColor` is used.
- No `QtCore.Qt.GlobalColor` value is treated as a `QColor` object.
- `Sdf.ValueTypeNames.Int` is used for integer shader inputs.
- The current Stageviz stage is used.
- Large scene-authoring operations use deferred prim updates and restore the previous update mode in `finally`.
- Empty selection is handled.
- Material binding uses `UsdShade.MaterialBindingAPI`.
- Material paths are valid absolute `Sdf.Path` values.
- MaterialX output is connected to the material surface output.
- The dialog is non-modal and retains a global reference.
- Dialog-local standard shortcuts are claimed while the dialog is active so Stageviz main-window actions do not fire accidentally.
- Apply errors are visible in the UI and printed with a traceback.
- The final script is complete and runnable.
