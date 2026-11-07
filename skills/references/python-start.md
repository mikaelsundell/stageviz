# Start writing Stageviz Python

Stageviz brings three toolboxes into one interpreter: **Stageviz** for application
state and undoable commands, **OpenUSD (`pxr`)** for scene queries and USD schemas,
and **Qt** for interfaces. Start small: find something, change it, then give your
useful idea a button.

This guide and the [API reference](python-api.md) are sufficient to begin. You do
not need repository example scripts or the optional `scripts/dialogs.py` helper.
For interfaces, continue with [dialogs and panels](python-dialogs.md). For API
boundaries, see [binding coverage](python-binding-coverage.md).

## Where your script runs

Run code in Stageviz's Python editor or save it as a shelf tool. `stageviz` is an
embedded module registered by the application, not a package to install with pip.
An ordinary terminal Python interpreter will not provide it. The running app
already owns the Qt application and event loop.

The executable also accepts Python source through `--execute`, after the viewer
starts. For a file, read and execute it explicitly:

```sh
DYLD_IMAGE_SUFFIX=_debug stageviz --execute 'from pathlib import Path; p = Path("/absolute/path/tool.py"); exec(compile(p.read_text(), str(p), "exec"))'
```

The macOS prefix selects the required debug-suffixed Qt libraries. On other
platforms omit it. Replace `stageviz` with your executable's full path if needed.
There is no `--script` option. Use absolute paths; editor/shelf execution need not
define `__file__` or put your script directory on `sys.path`. Add your own module
directory to `sys.path` explicitly when splitting a tool into importable modules.

**Execution changes history:** starting a Python script clears the previous native
undo/redo stack. Commands executed by that script build a fresh stack. Keep an edit
and its programmatic undo/redo checks in one execution. Direct `pxr` authoring is
not recorded in that stack. File writes cannot be undone with scene commands.

## Meet the current scene

```python
import stageviz

session = stageviz.session()
stage = session.stage()
if stage is None:
    raise RuntimeError("Open a USD stage first.")

print("Stage:", session.filename())
print("Selection:", session.paths())
for prim in stage.Traverse():
    print(str(prim.GetPath()), prim.GetTypeName())
```

`session` is the application's live state; `stage` is a real `pxr.Usd.Stage`.
`stageviz.Session()` is another wrapper for that same session, not a new document.
Reacquire the stage after loading, reloading or closing a file. A saved `Usd.Prim`
or Qt widget wrapper may refer to an object that has since been replaced.

Paths cross the Stageviz API as strings: `/World/Robot` names a prim,
`/World/Robot.size` names a property. Lists of targets are lists of strings,
even when you have only one target. Use `str(prim.GetPath())` when starting from
an OpenUSD object. `/` is the parent path for root prims.

## Let commands finish before the next step

Some commands dispatch USD work to a worker and return before it finishes. This
includes creation and property edits. Wait after each command before reading
its result, running another command, undoing, saving or replacing the stage.
A visible changed value alone is not a completion barrier.

Run this small setup once in the editor namespace; the editing examples below
reuse `run_command`. It pumps Qt events while waiting, so it belongs in controlled
scripts. Do not run it inside your own Deferred USD batch, and do not let users
start another edit while it pumps events. Interactive tools should disable Apply
and use a timer continuation instead of a nested wait.

```python
import time
import stageviz
try:
    from PySide6 import QtCore, QtTest
except ImportError:
    from PyQt6 import QtCore, QtTest

def run_command(function, *args, **kwargs):
    session = stageviz.session()
    if session.primsUpdate() != stageviz.PrimsImmediate:
        raise RuntimeError("Wait for the current edit/batch before starting another.")
    function(*args, **kwargs)
    deadline = time.monotonic() + 30.0
    while True:
        QtCore.QCoreApplication.processEvents()
        if session.primsUpdate() == stageviz.PrimsImmediate:
            return
        if time.monotonic() >= deadline:
            raise TimeoutError("Command is still running; do not start another edit.")
        QtTest.QTest.qWait(10)
```

The native Deferred-to-Immediate transition marks command completion for these
workflows. It is not a general thread-pool barrier or a transaction API. In
repository tests, use `CTest/python/testbench.py`'s `wait_for_commands()` instead.

## Build a tiny scene and try undo

Run this in a new/disposable document: it replaces the active scene. The sphere
is deliberately called Moon, because every blank stage deserves a little orbit.

```python
import stageviz

session = stageviz.session()
if not session.newStage():
    raise RuntimeError("Could not create a new stage.")

# A new Stageviz document already contains /World as its default prim.
run_command(stageviz.command.new_prim, "/World", "Moon", "Sphere")
moon = session.paths()[0]  # Creation selects the actual, uniquely named prim.
run_command(stageviz.command.set_attribute_value, moon + ".radius", 2.0)
assert session.stage().GetPrimAtPath(moon).GetAttribute("radius").Get() == 2.0

run_command(stageviz.command.undo)
assert session.stage().GetPrimAtPath(moon).GetAttribute("radius").Get() == 1.0
run_command(stageviz.command.redo)
assert session.stage().GetPrimAtPath(moon).GetAttribute("radius").Get() == 2.0

session.viewState().camera().frame(session.boundingBox([moon]))
```

Commands return `None`; creation does not return a prim. Read selection or query
USD for the result. Commands can report native rejection through Stageviz status
messages, so use observable checks when the result matters. Python argument
errors still raise exceptions.

## Selection is your tool's input

Read `session.paths()` when the user presses Apply, rather than keeping the
selection from when a dialog first opened. Validate the prim/schema your operation
needs. For example, make the selected geometry temporarily invisible:

```python
import stageviz

paths = stageviz.session().paths()
if not paths:
    raise RuntimeError("Select something to hide.")
run_command(stageviz.command.hide_paths, paths)
# The user's normal Undo restores visibility.
```

Visibility changes USD. Isolation changes the viewport mask instead:
`stageviz.command.isolate_paths(paths)` isolates those objects, and
`stageviz.command.isolate_paths([])` removes isolation. Selection changes use
`stageviz.command.select_paths(paths)` when history is desired; direct
`session.selection().updatePaths(paths)` is runtime selection without a command.

## Choose the edit layer before authoring

USD composes layers; the prim you see may come from a reference or payload.
The edit target determines where a command authors its opinion:

```python
import stageviz

session = stageviz.session()
print("Local layers:", session.editLayers())
print("Current edit target:", session.editLayer())
# Given an identifier returned above:
# stageviz.command.set_edit_layer(layer_identifier)
```

Namespace operations have stronger ownership/composition restrictions than
authoring an attribute override. Moving an arbitrary referenced descendant may
be rejected even when changing its visibility is valid. Transform commands
resolve instance proxies/subsets to editable object boundaries; read their
[contract](python-api.md#transforms-and-pivots) before working with instances.

Use composition commands to import content: `add_sublayer(filename)` preserves a
separate layer, `add_reference(filename, target_path)` references an asset on an
existing prim, and `merge_stage(filename)` merges content into the edit layer.
All live under `stageviz.command` and support undo. Session has no merge methods.

## Move an object without guessing matrix layout

OpenUSD uses row-vector transforms. Query each selected object's current world
matrix, then postmultiply a world-space translation. Convert `Gf.Matrix4d` to
plain nested tuples for the Stageviz binding:

```python
import stageviz
from pxr import Gf, Usd, UsdGeom

session = stageviz.session()
stage = session.stage()
if stage is None:
    raise RuntimeError("Open a stage first.")
paths = session.paths()
if not paths:
    raise RuntimeError("Select an object to move.")
cache = UsdGeom.XformCache(Usd.TimeCode.Default())
delta = Gf.Matrix4d(1.0).SetTranslate(Gf.Vec3d(1, 0, 0))

def rows(matrix):
    return tuple(tuple(float(matrix[r][c]) for c in range(4)) for r in range(4))

before, after = [], []
for path in paths:
    prim = stage.GetPrimAtPath(path)
    if not prim or not UsdGeom.Xformable(prim):
        raise ValueError("Select transformable prims: " + path)
    world = cache.GetLocalToWorldTransform(prim)
    before.append(rows(world))
    after.append(rows(world * delta))
run_command(stageviz.command.set_transforms, paths, before, after)
```

The delta is one stage unit along world X (one millimeter in a new Stageviz
document; loaded files may use other units). `copy_transforms(paths, rows(delta))`
creates moved sibling copies instead. Neither operation accepts a flat 16-number
list. Camera movements are separate: `session.viewState().camera()` controls the
viewport camera and does not author a USD camera prim.

## Attributes: start with simple values

`set_attribute_value()` handles bool, signed 64-bit int, double, string, and
numeric tuples/lists of length 2–4 converted to double vectors. It does not accept
arbitrary `Gf` objects, arrays, tokens, asset paths or matrices. Native authoring
also checks compatibility with the target attribute type. For a custom property:

```python
import stageviz

paths = stageviz.session().paths()
if not paths:
    raise RuntimeError("Select prims to rate.")
run_command(stageviz.command.add_attributes, paths, "user:rating", "double", custom=True)
run_command(stageviz.command.set_attribute_values, [p + ".user:rating" for p in paths], 5.0)
```

Creation protects existing properties. Before rerunning a tool, query whether
each attribute exists and create only missing ones. Use direct `pxr` APIs for
unsupported operations, making the untracked authoring explicit. There is no
Python wrapper that automatically makes arbitrary USD code undoable.

## View polish, capture and saving

View setters change runtime presentation rather than USD:

```python
import stageviz

view = stageviz.session().viewState()
view.setBackgroundColor((0.07, 0.09, 0.15))  # Floating-point RGB, 0..1.
view.setAmbientOcclusionEnabled(True)
view.setAmbientOcclusionQuality(stageviz.AmbientOcclusionHigh)
view.camera().setCameraView(stageviz.CameraViewPerspective)
```

For a clean viewport image, `stageviz.render().capture().save(filename)` captures
an initialized on-screen viewport without selection outlines or Qt overlays.
The owned snapshot also supports visible-object color queries. For offscreen
rendering, create `stageviz.RenderEngine()`, set its stage, size and USD camera
path, then call `render(filename)`. That renderer has its own settings; changing
ViewState does not configure it. Both require supported graphics facilities.

To collect visible objects across camera views, use the same scan and collection
as the Edit menu: `view = stageviz.render()`, then `view.capturePaths()` after
each view change. Read the union with `view.visibleCapturePaths()` and start over
with `view.clearVisibleCapture()`. For a fresh path-only query that leaves the
collection untouched, use `view.capturePaths(accumulate=False)`. This tiled scan
has finer object coverage than the image snapshot. Capturing itself never
selects anything; use `stageviz.command.select_paths(paths)` when ready.

Save deliberately with `session.save(filename)` and check its bool result.
`copy(filename)` exports a root-layer copy without retargeting the document;
`flatten(filename)` exports composition. See the reference for layer Save As,
sublayer persistence and companion state. Runtime AO/camera changes are view
state, while `Sdf.Layer.dirty` describes authored USD layer changes.

## Give the idea a window

Read [dialogs and panels](python-dialogs.md) for a complete small tool, parenting,
retained lifetime, docking, Qt binding compatibility and local shortcuts. Use
Stageviz commands in button callbacks, reacquire the current stage/selection,
and show useful errors. Qt callbacks run in the existing event loop; do not
start another loop or use blocking sleeps to wait for UI updates.

## When something goes wrong

| Symptom | First check |
| --- | --- |
| `ModuleNotFoundError: stageviz` | Run inside Stageviz, not system Python. |
| `AttributeError` | Check spelling and the installed binding version against the reference. C++ methods are not automatically Python methods. |
| Keyword argument rejected | Most methods are positional-only; check the reference's keyword-support table. |
| No prim returned by creation | Commands return None; query the stage/selection. |
| Edit did nothing | Check native status output, target existence, schema, edit layer and permissions. |
| Undo disappeared | A new Python execution clears old history; direct USD edits are untracked. |
| Dialog vanished or duplicates appear | Retain the window and use stable objectName replacement. |
| Color is wrong | ViewState uses 0..1 floats; UI theme colors use 0..255 integers. |
| USD looks correct, UI is stale | Allow queued Qt updates and reacquire widgets. For repeatable tests, use the documented command-completion helpers. |

Use `hasattr(stageviz.command, "operation_name")` to check capability on an older
installed build. Current source and installed binaries can differ. If an API is
missing from these docs, consult the matching CPython binding before inventing
a replacement or asserting it does not exist. For tests and external MCP
inspection, read [Qt testing](python-qt-testing.md); MCP calls must come from an
external controller while the embedded Qt thread executes Python.
