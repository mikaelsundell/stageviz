# Stageviz Python API Reference

This is the public Python surface implemented in `sources/python/`. Start with the [Python guide](python-start.md) for execution, scene editing and tool-building concepts. You do not need to read example scripts to use this reference. See [binding coverage](python-binding-coverage.md) for remaining differences from the C++ interfaces.

## Contents

- [Module entry points](#module-entry-points)
- [Module constants](#module-constants)
- [API conventions](#api-conventions)
- [MCP server control](#mcp-server-control)
- [Console file logging](#console-file-logging)
- [Viewport capture](#viewport-capture)
- [stageviz.Application](#stagevizapplication)
- [stageviz.Session](#stagevizsession)
- [stageviz.SelectionList](#stagevizselectionlist)
- [stageviz.command](#stagevizcommand)
- [stageviz.ViewState](#stagevizviewstate)
- [stageviz.ViewCamera](#stagevizviewcamera)
- [stageviz.UI](#stagevizui)
- [stageviz.RenderEngine](#stagevizrenderengine)
- [Important behavioral notes](#important-behavioral-notes)

## API conventions

### Naming

- `stageviz.command`: snake_case functions.
- Other `stageviz` module functions use camelCase names.
- `Application`, `Session`, `SelectionList`, `ViewState`, `ViewCamera`, `UI`, `Render`, `ViewportCapture`: camelCase methods.
- `RenderEngine`: snake_case methods.

### Calling conventions and types

Arguments are **positional-only unless the callable appears in the table below**.
Defaults in a signature describe optional values; they do not imply keyword
support. For example, use `app.invokeLater(callback, 100)`, not a `delay=` keyword.
All arguments of the following callables may be passed by the displayed names:

| Surface | Callables accepting keywords |
| --- | --- |
| Module | `setConsoleOutputFile(path, append=True)` |
| plugins | `registerFileFormat(name, extensions, loader=None, saver=None, options=None)`, `unregisterFileFormat(name)`, `readFile(path, interactive=False, operation="open")`, `writeFile(stage, path, interactive=False)` |
| Session | `beginProgressBlock(name, count=0)`, `updateProgressNotify(message, completed, paths=None, status=0, details=None)`, `newStage(policy=stageviz.LoadAll)`, `load(filename, policy=stageviz.LoadAll, interactive=False)`, `loadFromFile(filename, policy=stageviz.LoadAll, interactive=False)`, `boundingBox(paths=None)` |
| command | `convert_to_mesh(paths, segments=0)`, `select_all(recursive=False)`, `show_paths(paths, recursive=False)`, `hide_paths(paths, recursive=False)`, `load_payloads(paths, variant_set="", variant_value="")`, `new_light(parent_path, name, type="point")`, `new_prim(parent_path, name, type_name="")`, `new_reference(parent_path, name, asset_path, prim_path="")`, `new_payload(parent_path, name, asset_path, prim_path="")`, `move_path(paths, parent_path, insert_index=-1, preserve_world_transform=True)`, `add_attributes(prim_paths, name, type_name, custom=False)` |
| UI | `color(role, state=stageviz.UI.Normal)` |
| Render | `capturePaths(accumulate=True)` |
| ViewportCapture | `pathsByColor(color, tolerance=30, minPixels=8, minCoverage=0.0)`, `pathsByHue(hue, tolerance=15, minSaturation=0.35, minValue=0.05, minPixels=8, minCoverage=0.0)` |

In the entries below, `paths` means a list/tuple of absolute USD path strings,
`path` means one string, `filename`/`asset_path` mean filesystem or asset strings,
`enabled` means bool, numeric settings take float unless an enum/int is stated,
and `bbox` means `((min_x, min_y, min_z), (max_x, max_y, max_z))`.
Pass `str(sdf_path)` when holding an OpenUSD `Sdf.Path`; wrappers do not generally
accept path objects. Use ordinary Python containers rather than Qt value objects.
`-> None` is a void return, not an indication that the operation failed.

### Paths

Use absolute paths such as `/World/Robot` for prims and `/World/Robot.size` for properties. A path list must contain strings, never one bare path string. `/` is the stage pseudo-root and is accepted as the parent of new root prims. Commands reject empty single paths; individual operations also validate schema, ownership and editability. An empty path list generally means no targets, not “all prims”.

### Return shapes

- Selected paths and masks are returned as Python lists of path strings.
- `ViewState` colors are floating-point RGBA tuples in 0..1; its setters accept RGB/RGBA numeric sequences in that range. `UI.color()` uses integer RGBA in 0..255, and `UI.setColor()` takes integer channels separately.
- Bounding boxes are returned in the Stageviz helper representation `((min_x, min_y, min_z), (max_x, max_y, max_z))`.
- `ViewCamera.focusPoint()` returns `(x, y, z)`.

### Undoable edits

Functions in `stageviz.command` create native Stageviz `Command` objects and run them on the current session's `CommandStack`. Prefer them for edits that should participate in undo/redo.
Starting an editor/shelf/`--execute`/MCP Python script clears the existing native
history because arbitrary Python may author USD outside commands. Commands in
that script build a fresh history. An undo demonstration must perform the edit,
undo and redo in the same execution; running a second script clears that history.
Qt callbacks installed by a script can subsequently call commands normally.

Binding argument errors raise `TypeError`/`ValueError`; unavailable application
subsystems raise `RuntimeError`. Native commands may reject an edit and report a
Stageviz status message while the Python function still returns `None`. Verify
important results by querying the stage/session rather than treating a void
return as a success flag. File operations returning bool must be checked.
There is no public Python command-group/context-manager API.

### Command completion and threads

The native CommandStack releases the Python GIL during dispatch and history
operations (`undo`, `redo`, `clear`), including Qt menu actions triggered from
Python that bypass `stageviz.command`. This allows
USD worker notice callbacks and Python object cleanup to acquire the GIL even
when the UI waits for a stage lock. Python arguments and results are handled
with the GIL held; this does not change command completion semantics.

Commands may return after dispatching a worker, before USD edits and queued Qt
completion callbacks finish. Run commands serially. Before dependent commands,
USD queries, undo/redo, saves or stage replacement, wait for that operation's
Deferred-to-Immediate Session transition. The [guide's command runner](python-start.md#let-commands-finish-before-the-next-step)
is self-contained; repository tests use `testbench.wait_for_commands()`. A changed
attribute is not proof that notice delivery has finished. This check assumes no
caller-owned Deferred batch and is not a global thread-pool drain.

For interactive tools, disable editing controls and schedule a Qt timer to
check completion before proceeding; keep callbacks short and prevent overlapping
commands. Do not use blocking sleeps on the GUI thread. `invokeLater()` queues
work on the application event loop; it is not a USD-worker synchronization API.
Stage and auxiliary getters do not lock subsequent operations on returned USD
objects. Do not read/write that stage concurrently with command workers.

## Module entry points

`stageviz.application() -> stageviz.Application | None`
: Wrap the running application; return `None` if it is unavailable.

`stageviz.session() -> stageviz.Session`
: Wrap the application's current session. A Session wrapper can exist with no
  loaded USD stage; test `session.isLoaded()` or `session.stage()`.

`stageviz.ui() -> stageviz.UI | None`
: Wrap the application theme/icon service, or return `None` without an application.

`stageviz.selectionList() -> stageviz.SelectionList`
: Wrap the current selection; equivalent to `stageviz.session().selection()`.

`stageviz.render() -> stageviz.Render`
: Access the active viewport; see [viewport capture](#viewport-capture).

The `Application()`, `Session()`, `SelectionList()`, `ViewState()`, `ViewCamera()`
and `UI()` constructors wrap the corresponding current application subsystem;
they do not create independent applications, sessions, selections or cameras.
Use the factories/accessors for clear ownership. `RenderEngine()` creates an
independent offscreen renderer. `Render` and `ViewportCapture` are returned by
accessors and cannot be constructed directly.

The embedded interpreter injects `stageviz` plus the **objects** `application`,
`session`, and `ui` into its editor namespace. Portable scripts should explicitly
`import stageviz` and obtain their own wrappers; do not call those injected
objects as functions or assume they exist in an imported module.

## Module constants

These integer constants are attributes of `stageviz` (for example,
`stageviz.PrimsDeferred`). Use names rather than hard-coded ordinals. A getter
returning an enum integer can be compared directly with its matching constant.

| Purpose | Names |
| --- | --- |
| Session load policy | `LoadAll`, `LoadNone` |
| Progress state | `ProgressIdle`, `ProgressRunning` |
| Prim notice delivery | `PrimsImmediate`, `PrimsDeferred` |
| Stage lifecycle | `StageLoaded`, `StageFailed`, `StageClosed` |
| Stage up axis | `StageUpY`, `StageUpZ` |
| Notification status | `NotifySuccess`, `NotifyInfo`, `NotifyProgress`, `NotifyWarning`, `NotifyError` |
| Camera up axis | `CameraUpX`, `CameraUpY`, `CameraUpZ` |
| Interaction mode | `CameraModeNone`, `CameraModeTruck`, `CameraModeTumble`, `CameraModeZoom`, `CameraModePick` |
| Field-of-view direction | `FovVertical`, `FovHorizontal` |
| Projection model | `ProjectionFieldOfView`, `ProjectionPhysical`, `ProjectionOrthographic` |
| View preset | `CameraViewPerspective`, `CameraViewTop`, `CameraViewFront`, `CameraViewSide` |
| Viewport display | `RenderShaded`, `RenderWireframe` |
| Viewport refinement | `ComplexityLow`, `ComplexityMedium`, `ComplexityHigh`, `ComplexityVeryHigh` |
| Viewport material mode | `MaterialAll`, `MaterialClay`, `MaterialOverride` |
| Viewport sidedness | `SidednessPrimitive`, `SidednessDoubleSided`, `SidednessSingleSided` |
| Viewport AO | `AmbientOcclusionLow`, `AmbientOcclusionMedium`, `AmbientOcclusionHigh`, `AmbientOcclusionUltra`, `AmbientOcclusionComposite`, `AmbientOcclusionCombined`, `AmbientOcclusionContact`, `AmbientOcclusionBroad` |
| Legacy UI color alias | `ColorBase`, `ColorBaseAlt`, `ColorAccent`, `ColorAccentAlt`, `ColorText`, `ColorHighlight`, `ColorHighlightAlt`, `ColorBorder`, `ColorBorderAlt`, `ColorHandle`, `ColorProgress`, `ColorButton`, `ColorButtonAlt`, `ColorItem`, `ColorItemAlt`, `ColorRender`, `ColorRenderAlt`, `ColorSelection`, `ColorSelectionAlt`, `ColorWarning`, `ColorReadonly`, `ColorError` |
| Legacy UI icon alias | `IconArrow`, `IconBranchClosed`, `IconBranchOpen`, `IconChecked`, `IconClear`, `IconCode`, `IconCollapse`, `IconDropdown`, `IconDisplay`, `IconExpand`, `IconExport`, `IconExportImage`, `IconFollow`, `IconFrameAll`, `IconGeometry`, `IconHidden`, `IconLeft`, `IconLock`, `IconMaterial`, `IconOpen`, `IconPartiallyChecked`, `IconPayload`, `IconPrim`, `IconRedo`, `IconRight`, `IconRun`, `IconShaded`, `IconUndo`, `IconUnlock`, `IconVisible`, `IconWireframe` |
| Legacy UI scale alias | `UIScaleSmall`, `UIScaleMedium`, `UIScaleLarge` |
| Legacy UI state alias | `UIStateNormal`, `UIStateDisabled` |

`NotifyInfo` is an alias of `NotifySuccess` (0). Legacy `Color*`/`Icon*`/`UIScale*`/
`UIState*` aliases are only a subset of the UI enums; prefer `stageviz.UI.ColorRole`,
`IconRole`, `UIScale` and `UIState` for the complete sets. In particular the legacy
spelling is `IconDropdown`, while the nested enum is `UI.IconRole.DropDown`.
Viewport material/sidedness constants are not RenderEngine Settings objects.

## MCP server control

`stageviz.startMcpServer() -> str`
: Start the local MCP server and return its URL. Raises `RuntimeError` if the
  server cannot start.

`stageviz.stopMcpServer() -> None`
: Stop the local MCP server.

`stageviz.isMcpServerRunning() -> bool`
: Return whether the local MCP server is running.

## Console file logging

`stageviz.setConsoleOutputFile(path, append=True) -> None`
: Log future captured stdout and stderr to a file. Captures native USD/Qt output
  and normal embedded Python output, and continues displaying it in the console.
  Starts console capture if needed, including in debug builds. Logging writes
  from the capture thread even while the UI is busy. Previous
  console history is not exported. Pass `append=False` to replace an existing
  file, or `None` to stop logging. Switching or stopping drains pending native
  output before closing the previous file. Raises `OSError` if the destination
  cannot be opened; an existing destination remains active on failure.

`stageviz.consoleOutputFile() -> str | None`
: Return the active absolute output filename, or `None` when disabled.

```python
stageviz.setConsoleOutputFile(r"C:\temp\stageviz.log")
# Load the stage or run the operation to diagnose.
stageviz.setConsoleOutputFile(None)
```

Explicit Python stream redirects (including MCP's returned Python output) stay
with their chosen stream. Native output from those scripts is still captured.

## Viewport capture

`stageviz.render() -> stageviz.Render`
: Return weak access to the active render view. Access and capture must run on the
  GUI thread; use `stageviz.application().invokeLater(...)` from other threads.

`render.capture() -> stageviz.ViewportCapture`
: Render an owned color/object snapshot from the current camera, gate, masks,
  material overrides, lighting and sidedness. Uses the color AOV and filled
  surface display modes. Excludes auxiliary grid/gizmo geometry, selection
  outlines and Qt overlays. Requires an initialized on-screen viewport and
  Storm scene-ID support. Raises `RuntimeError` on an unavailable stage, context,
  renderer or unsupported display mode. Capturing does not change USD, selection
  or undo history. Snapshots survive subsequent stage/camera changes and closure.

### Visible path scans and the Edit menu

`render.capturePaths(accumulate=True) -> list[str]`
: Run the same 4-by-4 tiled scene-ID scan as Edit's visible-selection **Capture
  view** action. With `accumulate=True` (the default), add newly visible paths to
  the menu's shared collection and return the complete collection. With `False`,
  return only the current scan and leave that collection unchanged. Synchronizes
  the current camera, mask and display settings before scanning. Requires the
  GUI thread and a rendered viewport; an unavailable rendered view produces an
  empty scan. This does not select objects, author USD or create undo history.

`render.visibleCapturePaths() -> list[str]`
: Copy the collection shared with the menu without rendering. Paths are unique,
  ordered by first capture; the returned Python list is independent of the native
  collection. Native instance proxy geometry keeps its actual USD path.

`render.clearVisibleCapture() -> None`
: Clear that same collection, like the menu's **Clear** action. Does not change
  selection. Replacing/closing the stage also clears the collection.

All three methods raise `RuntimeError` when called off the GUI thread or after
their viewport has closed. The menu and Python share one native implementation;
capturing through either interface contributes to the same collection. Capture
is synchronous. Wait for any outstanding USD command before capturing.

```python
import stageviz

view = stageviz.render()
view.clearVisibleCapture()
paths_now = view.capturePaths(accumulate=False)  # A fresh scan, without collecting.
view.capturePaths()                            # Collect this view, like the menu.
# Change the view, then call view.capturePaths() again to add more objects.
collected = view.visibleCapturePaths()
if collected:
    stageviz.command.select_paths(collected)   # Selection is a separate undoable edit.
```

Use `capture()` for a color image, pixel IDs and color queries. Its snapshot
`paths()` describes one viewport-resolution image and does not use or modify the
menu collection. The finer tiled `capturePaths()` scan can find small objects
missing from that image's pixel grid; the two path sets need not match. Neither
scan includes objects fully occluded in its rendered view. The collection is
historical: changing visibility/camera does not remove earlier entries. Clear
and recapture when you need a fresh set.

### Snapshot diagnostics and queries

`render.inspectPrim(path) -> dict`
: Diagnose an existing geometry prim, including native-instance proxy geometry.
  Reports USD visibility at default and viewport time, mesh topology and point
  bounds, mapped Hydra Rprims, scene-index/delegate visibility, draw-item bounds,
  camera-frustum intersection, and Storm buffer allocation metadata. Also reports
  the viewport mask, camera, and culling debug flags. Requires the GUI thread and
  an initialized renderer. Invalid paths raise `ValueError`; missing or
  non-geometry prims and unavailable renderers raise `RuntimeError`.
  Hydra data describes the last synchronization, while USD data is current.
  This call does not render, read back GPU buffers, change selection or author USD.
  Buffer handles and sizes do not prove that geometry was rasterized successfully.

```python
import json
report = stageviz.render().inspectPrim("/World/Body")
print(json.dumps(report, indent=2))
```

Snapshot methods:

- `size() -> (width, height)`: physical/device-pixel dimensions, top-left origin.
- `paths() -> list[str]`: unique front-most geometry paths, including actual
  native-instance proxy paths, with no subset or prototype paths. Ordering is
  first appearance scanning top to bottom, left to right.
- `prims() -> list[dict]`: `path` and `visiblePixels` for each path.
- `rgba() -> bytes`: tightly packed, top-left row-major displayed RGBA8 pixels.
- `objectIds() -> bytes`: matching native-endian signed int32 indices into
  `paths()`, with -1 for background. Read with `memoryview(data).cast("i")`.
- `pathAt(x, y) -> str | None`: lookup in device pixels; outside the image raises
  `IndexError`, background returns `None`.
- `save(path) -> None`: save the clean captured image; failure raises `OSError`.
- `pathsByColor(color, tolerance=30, minPixels=8, minCoverage=0.0) -> list[str]`:
  color is an RGB integer triple in 0..255. Each channel must be within tolerance.
- `pathsByHue(hue, tolerance=15, minSaturation=0.35, minValue=0.05,
  minPixels=8, minCoverage=0.0) -> list[str]`: hue/tolerance use degrees, with hue
  wrapping at 360. Saturation, value and coverage use 0..1. Hue tolerance is
  0..180; RGB tolerance is 0..255. `minPixels` must be positive. Invalid parameters
  raise `ValueError`.

Queries scan the owned frame in C++, perform no GPU work, and never select
anything themselves. Coverage is the fraction of an object's visible pixels
matching the query; a whole mesh can match because one of its material regions
has the queried color. Hidden/occluded geometry is not included. Display colors
include lighting/color processing; they are not raw USD material input values.
For transparent surfaces the front-most geometry owns the pixel and the sampled
color can be a blend, so this is not a per-layer transparency analysis.

```python
frame = stageviz.render().capture()
for prim in frame.prims():
    print(prim["path"], prim["visiblePixels"])

# Select small visible pink/magenta diagnostic regions, including proxy meshes.
paths = frame.pathsByHue(335, tolerance=30, minPixels=8)
stageviz.command.select_paths(paths)
```

Returned paths preserve object identity. Existing transform commands resolve
proxy meshes to editable instance roots and deduplicate them as usual. The capture
binding does not perform normal edits, create subsets, or add a dialog button.

## stageviz.Application

Construct with:

```python
app = stageviz.Application()
```

The wrapper points to `Application::instance()` and does not own the native application.

### Methods

`session() -> stageviz.Session`
: Return the application's session wrapper.

`ui() -> stageviz.UI`
: Return the application's UI wrapper.

`invokeLater(callable, delay=0) -> None`
: Queue a Python callable on the Qt event loop after an optional delay in milliseconds. Raises `TypeError` when the argument is not callable.

`show(widget) -> None`
: Retain and show a Python Qt widget on the Qt event loop, then call `raise_()` and `activateWindow()` when available.

`window() -> QWidget | None`
: Return the Stageviz main window wrapped for Python. Wrapping first tries PySide6/shiboken6 and then PyQt6/sip. Raises `RuntimeError` if neither wrapping mechanism is available.

## stageviz.Session

Construct with:

```python
session = stageviz.Session()
```

The wrapper defaults to the current native Stageviz session and does not own it.

### Progress

`beginProgressBlock(name, count=0) -> None`
: Begin named progress with a non-negative total; zero means unspecified. Pair with `endProgressBlock()` in `finally`. Cancellation is cooperative: poll `isProgressBlockCancelled()` in your loop.

`updateProgressNotify(message, completed, paths=None, status=0, details=None) -> None`
: Update the active progress block. Use a non-negative completed count; `paths` is an optional absolute-path list. `details` is a dict with string keys and simple bool/int/float/string/list/dict/None values. Status uses the Notify constants; 0 means success.

`cancelProgressBlock() -> None`
: Request cooperative cancellation of the active progress block.

`endProgressBlock() -> None`
: Finish the active progress block, including after cancellation or failure.

`isProgressBlockCancelled() -> bool`
: Read the progress cancellation flag.

### Stage lifecycle and file operations

`newStage(policy=stageviz.LoadAll) -> bool`
: Replace the active document with an anonymous stage containing `/World` as its
  default prim, Z up, millimeter units, and the root layer as edit target. Clears
  selection, mask and command history. Discards unsaved stage edits.

`load(filename, policy=stageviz.LoadAll, interactive=False) -> bool`
: Replace the document with a USD file, choosing LoadAll or LoadNone for payloads,
  or import a registered external format into an anonymous USD document. External
  imports flatten composition and skip settings unless interactive=True. Conversion
  failure/cancellation preserves the existing document. Check the bool result and
  reacquire stage/prim/Qt objects after successful replacement.

`loadFromFile(filename, policy=stageviz.LoadAll, interactive=False) -> bool`
: Alias of `load`.

Composition edits belong to [stageviz.command](#stagevizcommand), not Session.
Use `merge_stage`, `merge_flattened_stage`, `add_sublayer`, `add_reference`, and
`add_payload` for undoable changes. There are no `Session.merge*` methods.

`save(filename) -> bool`

`saveToFile(filename) -> bool`
: Alias of `save`. Save the root and modified file-backed local sublayers.
  A new filename performs Save As and retains asset anchors. Anonymous sublayers
  need backing files first. A failure can occur after earlier layers were saved;
  this is not a transactional multi-file save. With Remember state enabled,
  this root-document save also writes its `.session` companion. An unchanged
  root USD file is not rewritten for state-only changes.

`saveLayer(identifier) -> bool`
: Save one existing file-backed local layer; anonymous layers need Save As first.
  Saving the root also saves its companion when Remember state is enabled.
  Saving a sublayer does not change the root companion by itself.

`saveLayerAs(identifier, filename) -> bool`
: Export one local layer and retarget the live stage to the new file. Parent
  sublayer references change in memory and must be saved separately. This does
  not save the retargeted layer's sublayers.

`copyLayerToFile(identifier, filename) -> bool`
: Export one local layer with asset anchors preserved, leaving the live stage
  and its filenames unchanged.

`copy(filename) -> bool`

`copyToFile(filename) -> bool`
: Alias of `copy`. Export the root layer without changing the active filename; dirty sublayers are not saved.

`flatten(filename) -> bool`

`flattenToFile(filename) -> bool`
: Alias of `flatten`. Export the composed stage as a flattened USD file.

`snapshot() -> Usd.Stage | None`
: Return an isolated, anonymous snapshot of the composed stage entirely in memory,
  including session opinions. Returns None if no stage is loaded. Captures under
  the Session read lock; changes to the result do not affect the document.

`snapshotPaths(paths) -> Usd.Stage | None`
: Snapshot selected hierarchies and their dependencies in memory, with the same
  population-mask, payload, muted-layer, resolver-context and metadata rules as
  `flattenPathsToFile`. Empty paths or no stage return None. No USD files are written.

`flattenPaths(paths, filename) -> bool`

`flattenPathsToFile(paths, filename) -> bool`
: Alias of `flattenPaths`. Export selected hierarchies and dependencies, preserving session-layer opinions, load rules and muted-layer composition. Clear an omitted default prim and filter missing root-order names in the output without changing the source stage. Empty paths fail.

`loadState(filename) -> bool`
: Restore companion Stageviz state, including camera projection/orthographic scale, view and ambient-occlusion settings, payload load rules, muted layers, layer locks, the active local edit layer and layer colors. An unavailable, muted or locked saved edit layer falls back to the root. This does not author USD layer files.

`saveState(filename) -> bool`
: Save that state to the explicitly supplied companion-state filename. Automatic
  persistence uses the root USD filename plus `.session`, regardless of the active
  edit layer; explicit save/loadState calls take the sidecar filename itself.
  This is a low-level serialization API; the UI has no separate Save state action.
  Root-document saves include remembered state, while individual sublayer saves
  leave it pending. State-only changes are shown on the root row in save prompts
  and do not require rewriting unchanged USD. An unnamed stage must first be
  saved to establish its root filename. Saving a root copy does not clear the
  source document's unsaved state.

`isStateDirty() -> bool`
: Camera and view changes on a pristine, unnamed New stage do not mark state
  dirty. Tracking starts after authored document changes or loading/naming a
  root file; an untouched loaded file can still have dirty camera state.
  Returns whether preserved application/session state has changed since being saved;
  distinct from `Sdf.Layer.dirty` for authored USD. Camera/view changes, payload
  load rules, layer muting, locks, edit-target changes and colors participate. The viewer includes this flag
  in document dirtiness and save prompts only while Remember state is enabled.

`setPreserveState(enabled) -> None`
: Enable automatic companion-state saving/restoration with file operations. Muting is stage state; USD files alone do not preserve the mute list.

`reload() -> bool`
: Reload the current file and discard unsaved USD edits. With Remember state
  enabled, dirty companion state is saved first and restored, including current
  payload load rules and muted layers. Use `loadState` to explicitly restore an
  older saved state snapshot. Reacquire USD/Qt objects afterward.

`close() -> bool`
: Close the current stage. This is a lifecycle operation, not an undoable edit.

`isLoaded() -> bool`
: Whether a main USD stage is loaded.

### Stage/session state

`mask() -> list[str]`
: Return viewport isolation-mask paths.

`setMask(paths) -> None`
: Set the viewport isolation mask; an empty list removes isolation. This is runtime state, not USD visibility.

`stageUp() -> int`

`setStageUp(value) -> None`
: Use `stageviz.StageUpY` or `stageviz.StageUpZ`. This direct Session setter is not the undoable `command.set_stage_up()` operation.

`loadPolicy() -> int`
: Return LoadAll or LoadNone.

`boundingBox(paths=None) -> tuple`
: Return stage bounds, or bounds of the supplied absolute prim paths. Accepts `paths=`.

`filename() -> str`
: Return the current stage filename; an anonymous/new stage may have an empty filename.

### Edit layer

`editLayer() -> str | None`
: Return the active edit layer identifier.

`editLayers() -> list[str]`
: Return identifiers for the local layer stack (`GetLayerStack(false)`).

`setEditLayer(identifier_or_real_path) -> bool`
: Select a layer that is already present in the local layer stack. Runs the undoable edit-layer command; undo restores the previous edit target. Returns `True` after dispatch (also for an unchanged target). Raises `RuntimeError` if no stage is loaded and `ValueError` if no matching local layer is found.

### USD stages and locks

`stage() -> pxr.Usd.Stage | None`
: Return the main USD stage through the binding helper's normal locked access.

`stageUnsafe() -> pxr.Usd.Stage | None`
: Return the main USD stage without locking.

`stageLock() -> int | None`
: Return the native `QReadWriteLock*` address as an integer. This is not a Python lock object.

`auxiliary() -> pxr.Usd.Stage | None`
: Return the Stageviz-owned auxiliary USD stage.

`auxiliaryUnsafe() -> pxr.Usd.Stage | None`
: Return it without locking.

`auxiliaryLock() -> int | None`
: Return the native auxiliary `QReadWriteLock*` address as an integer.

### Prim update policy

`primsUpdate() -> int`

`setPrimsUpdate(value) -> None`
: Set the prim-notice update policy. Use `stageviz.PrimsImmediate` or `stageviz.PrimsDeferred`. Switching from `Deferred` to `Immediate` automatically flushes accumulated prim changes; no explicit Python flush method is exposed. Restore the previous policy in `finally` when batching direct USD edits.

### Command stack and selection

`commandStack() -> int | None`
: Return the native `CommandStack*` address as an integer; not a Python command-stack wrapper.

`selectionList() -> stageviz.SelectionList`

`selection() -> stageviz.SelectionList`
: Alias of `selectionList()`.

`paths() -> list[str]`
: Return currently selected paths.

### View state

`viewState() -> stageviz.ViewState`

### Notifications

`notifyRedraw() -> None`
: Request viewport redraw. Normal Stageviz commands and view setters deliver their own notifications.

`notifyStatus(status, message, details="") -> None`
: `status` is `stageviz.NotifySuccess`, `NotifyProgress`, `NotifyWarning` or `NotifyError`. `details` is a string, unlike the dictionary accepted by `updateProgressNotify()`.

`setStatus(message, details="") -> None`
: Emit a success-status notification.

## stageviz.SelectionList

Normally obtain from `stageviz.Session().selection()` or `.selectionList()`.

### Methods

`isSelected(path) -> bool`
: `path` must be absolute.

`paths() -> list[str]`

`isEmpty() -> bool`
: Whether no paths are selected.

`isValid() -> bool`
: Whether the selection has a valid native backing object.

`addPaths(paths) -> None`
: Add absolute paths to the current selection.

`removePaths(paths) -> None`
: Remove absolute paths from the current selection.

`togglePaths(paths) -> None`
: Toggle membership for each supplied absolute path.

`updatePaths(paths) -> None`
: Replace/update the current native selection with the supplied paths.

`clear() -> None`

Direct `SelectionList` mutation is not automatically described by the command binding as undoable. Use `stageviz.command.select_paths()` when command-stack semantics are desired.

## stageviz.command

Import/use as:

```python
import stageviz
stageviz.command.select_paths(["/World/Car"])
```

All command functions run a native Stageviz command on the current session command stack.

### Composition and layers

These functions author in the active edit layer unless another layer is identified.
Layer identifiers come from `session.editLayers()` or `Sdf.Layer.identifier`;
use identifiers rather than display labels. Muted layers may no longer appear in
the composed layer stack, so retain their identifier before muting.

`merge_stage(source) -> None`
: Merge authored USD content into the active edit layer; undo restores its
  previous contents. Source may be a filename or live `pxr.Usd.Stage`. External
  files are loaded noninteractively before dispatch; in-memory stages are captured
  immediately. Redo reuses that detached snapshot even if the caller changes or
  releases its stage. Use when
  you want editable imported content rather than an arc.

`merge_flattened_stage(source) -> None`
: Accept a filename or live `pxr.Usd.Stage`, snapshot its composed content and merge
  it into the edit layer with the same ownership and undo/redo rules as `merge_stage`.

`add_sublayer(filename) -> None`
: Add a USD file to the edit layer's sublayer list. The file remains a separate layer.

`reorder_sublayers(parent_identifier, source_identifier, target_identifier, after_target) -> None`
: Move source before target (`False`, stronger) or after target (`True`, weaker)
  within the same parent's sublayer list. All three arguments identify layers.

`remove_layer(layer_identifier) -> None`
: Remove the layer's sublayer arc from its parent; keep the file on disk.
  `layer_identifier` accepts a string or sequence of strings; a sequence is one
  undo step and removes children before parents.

`update_layer(layer_identifier) -> None`
: Reload only the selected file-backed, local, unmuted, editable layers from disk.
  Accept a string or sequence of strings; a batch is one undo step. Unsaved
  contents are replaced. Undo restores the previous contents and redo restores
  the captured update without reading disk again. Anonymous and locked layers
  are not updated; unselected sublayers and companion state are preserved.

`set_layer_muted(layer_identifier, muted) -> None`
: Mute/unmute a local layer with undo. Persist this through Stageviz companion
  state; muting is not an authored USD layer change. The identifier accepts a
  string or sequence of strings. A batch is one undo step, muting children first
  and unmuting parents first; undo restores individual original states.

`set_layer_permission_to_edit(layer_identifier, editable) -> None`
: Change local edit permission with undo. This is an in-process layer permission,
  not filesystem chmod. The identifier accepts a string or sequence of strings;
  a sequence is one undo step that restores individual permissions on undo.
  Check permissions before authoring.

`set_edit_layer(layer_identifier) -> None`
: Choose an existing local edit target with undo. `Session.setEditLayer()` also
  supports matching a real path and returns bool instead of None.

`add_reference(filename, target_path) -> None`
: Add a reference to an existing absolute prim path in this stage.

`add_payload(filename, target_path) -> None`
: Add a payload arc to an existing absolute prim path in this stage.

`set_variant_selection(paths, set_name, value) -> None`
: Author the named variant selection in the edit layer. Only prims exposing that
  set and value are modified; undo restores previous selections and created overs.

### Command history

`undo() -> None`
: Undo the latest command if available.

`redo() -> None`
: Reapply the latest undone command if available.

`clear() -> None`

`can_undo() -> bool`
: Whether history has a command available to undo.

`can_redo() -> bool`
: Whether history has a command available to redo.

`can_clear() -> bool`
: Whether either undo or redo history is present.

### Selection

`select_paths(paths) -> None`
: Replace the selection; an empty list clears it.

`select_all(recursive=False) -> None`
: Select prims in the native selection domain; recursive=True includes descendants.

`select_invert() -> None`
: Invert selection in the current native selection domain.

`select_payload() -> None`
: Select the nearest owning payload for the current selection.

`select_layer() -> None`
: Select the nearest payload authored in the active edit layer.

`select_invert_in_payload() -> None`
: Invert selection within the nearest payload.

`select_invert_in_layer() -> None`
: Invert payload selection within the active edit layer.

### Visibility/isolation

`isolate_paths(paths) -> None`
: Set the viewport isolation mask with undo; an empty list removes isolation.

`show_paths(paths, recursive=False) -> None`
: Author visible USD visibility opinions; recursive=True processes descendants.

`hide_paths(paths, recursive=False) -> None`
: Author invisible USD visibility opinions; recursive=True processes descendants.

### Payloads

`load_payloads(paths, variant_set="", variant_value="") -> None`
: Load payloads and optionally choose the named variant as part of the command.

`load_neighbor_payloads(paths) -> None`
: Load spatially neighboring payloads using `extentsHint` according to the native Stageviz implementation.

`unload_payloads(paths) -> None`
: Unload payloads through stage load rules; undo restores previous rules.

### Stage metadata

`set_stage_up(value) -> None`
: Use `stageviz.StageUpY` or `stageviz.StageUpZ`; author the USD stage up axis with undo support.

`set_default_prim(path) -> None`
: Set an existing root prim as the USD default prim.

`clear_default_prim() -> None`
: Clear the USD default-prim metadata.

### Namespace and prim creation

`delete_paths(paths) -> None`
: Delete eligible strongest-editable prims, reducing overlapping selections to root paths. Remove affected authored dependencies and unused local flattened instance sources associated with the deleted instances. Surviving authored users in inactive variants and muted or unloaded layer assets protect shared sources; unresolved assets conservatively prevent cleanup. Undo restores deleted prims and cleaned sources, ordering, selection and mask.

`copy_paths(paths) -> None`
: Copy one or more prim paths as sibling prims.

`instance_paths(paths) -> None`
: Create instanceable sibling prims that internally reference the selected prims.

`convert_to_mesh(paths, segments=0) -> None`
: Convert Cube, Sphere, Cone, Cylinder, Capsule and Plane prims to polygon
  meshes at the same paths. Accepts positional or keyword arguments. `segments`
  defaults to 0, which uses the current viewport complexity: Low/Medium/High/VeryHigh
  select 16/32/64/128 radial segments. An explicit integer from 4 to 256
  overrides that choice; axial resolution is half the radial value. Resolution
  is captured on first execution so redo retains the same geometry.
  Preserves transforms, material bindings, children and selection. Bakes authored
  shape-dimension samples into points/normals/extent samples; interpolation
  between baked samples follows mesh interpolation. The entire batch is rejected
  for unsupported prims, instances/proxies/prototypes, invalid dimensions or an
  uneditable target. Undo restores exact edit-layer opinions; redo retains the
  original target even after an edit-target change. No new constants are needed.

`make_editable_paths(paths) -> None`
: Disable native instancing in the active edit target. Proxy children resolve to
  their owning non-proxy instance root, with duplicate roots processed once.
  Retain references, placement and selection so children can receive local
  overrides. Nested instances remain instanced. Undo restores exact metadata and
  removes newly created inert override specs. Locked layers and ineffective
  weaker edit targets are rejected without leaving edits behind.

`new_prim(parent_path, name, type_name="") -> None`
: Create an untyped prim or a USD schema prim such as `Cube`, `Sphere`, `Camera`
  or `Xform`. Names are sanitized and made unique. Creation commands generally
  select the created prim, so read `session.paths()` for the actual resulting path.
  Mesh/curve schemas created here do not imply populated topology; use the
  dedicated creation commands when you want their built-in geometry.
  Creation can add children beneath composed parents defined in other layers,
  including stronger root-layer definitions and referenced structures. Parent
  specs are authored as needed in the edit layer; source definitions are retained.
  Native instance/prototype namespaces and inactive parents are rejected. Creation
  undo/redo restores local child-order opinions and removes newly created inert
  parent specs. This does not relax move/rename ownership rules.

`new_scope(parent_path, name) -> None`
: Create a Scope container.

`new_mesh(parent_path, name) -> None`
: Create a populated wavy Mesh, ready to see in the viewer.

`new_points(parent_path, name) -> None`
: Create a populated particle-style Points prim.

`new_basis_curves(parent_path, name) -> None`
: Create populated BasisCurves.

`new_nurbs_curves(parent_path, name) -> None`
: Create populated NurbsCurves.

`new_nurbs_patch(parent_path, name) -> None`
: Create a populated NurbsPatch.

`new_point_instancer(parent_path, name) -> None`
: Create a populated PointInstancer with a Cube prototype.

`new_material(parent_path, name) -> None`
: Create a material with a UsdPreviewSurface shader.

`new_light(parent_path, name, type="point") -> None`
: Create and select a standard USD light. Types are `point`, `spot`, `distant`,
  `rect`, `disk`, `cylinder` and `dome`; see [USD lights](#usd-lights) for schemas
  and defaults.

`new_reference(parent_path, name, asset_path, prim_path="") -> None`
: Create an Xform with a reference arc. Empty `prim_path` uses the asset default prim; otherwise supply an absolute path inside that asset.

`new_payload(parent_path, name, asset_path, prim_path="") -> None`
: Create an Xform with a payload arc; the same default-prim rule applies. Loading follows the stage load policy/rules.

`rename_path(path, name) -> None`
: Rename a prim within its parent, remapping affected selection and mask. Native namespace/ownership restrictions apply.

`new_xform(parent_path, name) -> None`
: Create an Xform and group selected eligible siblings under it, including a single selected sibling. Selecting `parent_path` itself creates an empty child Xform. Use `new_prim(parent_path, name, "Xform")` when you need a plain new Xform without grouping selected objects.

`move_path(paths, parent_path, insert_index=-1, preserve_world_transform=True) -> None`
: Reparent paths; -1 uses default insertion, otherwise choose sibling position. Preserve world transforms by default. Invalid cycles/collisions and unsupported composition boundaries are rejected. Undo restores namespace, transforms, ordering, selection and mask.

### Attribute editing

Property-path arguments must be valid USD property paths. Commands author default
values in the active edit target; time-sampled authoring is not exposed here.

`add_attributes(prim_paths, name, type_name, custom=False) -> None`
: Add an attribute on each target without overwriting existing properties.
  `name` is a valid namespaced identifier, e.g. `user:rating`; `type_name` is a
  registered USD type string, e.g. `double`, `bool`, `string` or `color3f` (not a
  Python class or `Sdf.ValueTypeNames` object). Use `custom=True` for user properties.

`remove_attributes(attribute_paths) -> None`
: Remove custom attributes in the edit layer. Built-in schema attributes are protected.

`rename_attributes(attribute_paths, new_name) -> None`
: Rename custom attributes to a valid, non-colliding property name on each prim.

`set_attribute_value(attribute_path, value) -> None`
: Author one existing attribute default value using the supported conversions below.

`set_attribute_values(attribute_paths, value) -> None`
: Author the same supported default value on several existing attributes.
  Attribute value commands restore opinions at their mapped edit-target paths,
  including variants. Pivot edits preserve the composed transform and restore the
  complete mapped transform property specs on undo. The command stack clears history
  when it detects a different stage or edit target.

Supported Python values for the two setter functions are:

- `bool`
- `int` (converted to native signed 64-bit integer value)
- `float` (double)
- `str`
- numeric Python tuple/list of length 2 -> `GfVec2d`
- numeric Python tuple/list of length 3 -> `GfVec3d`
- numeric Python tuple/list of length 4 -> `GfVec4d`

Other value types are rejected by this binding.

`clear_attribute_values(attribute_paths) -> None`
: Clear authored default values from attributes in the active edit layer.

`clear_attribute_opinion(attribute_path) -> None`
: Remove the eligible edit-layer attribute override to expose its weaker property opinion.

`clear_attribute_opinions(attribute_paths) -> None`
: Apply the same override reset to several attributes.

### Transforms and pivots

`set_transforms(paths, before, after) -> None`
: Apply undoable world transforms. The three sequences must have equal lengths;
  each matrix is four rows of four numbers. Each pair defines a world delta
  `inverse(before) * after` using USD row-vector matrices. Subsets resolve to
  geometry and proxy children to instance roots. Ordinary source children remain
  independently editable, with their shared edits inherited by instance children.
  Mixed source/proxy selections resolve source contents to reference roots to
  avoid applying a shared delta twice. Resolved targets are deduplicated;
  conflicting deltas for one object are rejected before writes. When editing a
  source root, unselected inherited instance roots remain fixed.
  Selection paths are retained. Undo/redo restore captured edit-layer opinions.
  Supply world matrices, not native TransformOperation/XformEdit objects.

`copy_transforms(paths, world_delta) -> None`
: Create sibling copies transformed by a 4x4 world delta; select the copies.
  Uses the same object-boundary resolution as transforms. Originals remain fixed.
  Undo removes copies and restores selection. See [copy behavior](#copy-with-a-world-transform).

`transform_pivots(paths, delta) -> None`
: Apply a finite affine invertible 4x4 world delta to pivot frames while keeping
  geometry fixed; see [pivot frame editing](#pivot-frame-editing).

`center_pivots(paths) -> None`
: Center pivots on target bounds.

`clear_pivots(paths) -> None`
: Clear Stageviz-authored pivots while preserving local transforms.

`identity_transforms(paths) -> None`
: Remove local transform opinions, exposing weaker transforms or authoring identity when none exist.

`clear_opinions(paths, group="all") -> None`
: Recursively remove edit-layer property overrides on composed prims, exposing weaker opinions. All and Other also remove inert `over` containers. Preserve metadata, composition arcs and edit-layer-owned definitions. Optional positional `group` is `all`, `transforms`, `visibility`, `materials`, `geometry`, `primvars`, `lights`, `cameras`, or `other`. Materials includes bindings and shading inputs/outputs; geometry uses USD schema properties. Undo restores the removed property specs in the original mapped authoring layer. The command stack clears history
  when it detects a different stage or edit target; cross-target history is not retained.

### Materials

`override_paths(paths) -> None`
: Author explicit `over` specs for existing ordinary prims in the current edit
  target. Preserve existing local definitions and metadata; reject proxies,
  prototypes and `/`. Undo removes newly created inert specs and ancestor overs
  in the original target. Positional-only.

`replace_material_network(paths, type="preview_surface") -> None`
: Replace existing materials' connected networks in the active edit target.
  Type is `preview_surface`, `standard_surface`, or `open_pbr_surface`. Retain
  material paths, bindings and metadata, author fresh shader nodes and explicit
  connections, and disconnect weaker outputs in every render context. Weaker
  nodes remain unchanged. Reject proxies, locked targets and stronger terminal
  opinions that the current target cannot override. Undo restores the exact
  previous local material subtree in the original target. Positional-only.

`assign_material(paths, material_path) -> None`
: Bind an existing material directly to selected ordinary prims, groups or
  instance roots. Proxy selections resolve upward to their nearest non-proxy
  ancestor, normally the instance root; duplicate owners are processed once.
  Assigning through one proxy therefore affects all children of that instance.
  Parent bindings and whole-mesh assignments with material-binding face subsets
  use stronger-than-descendants strength. Existing subset bindings are retained;
  clearing the mesh assignment reveals them again. Assigning a face subset under
  a strong direct mesh binding automatically preserves all other subsets' visible
  materials in the edit layer and makes the mesh binding weaker than descendants.
  The selected subset can then use its own material; one undo restores the exact
  mesh and subset opinions. No geometry,
  instancing or source assets are changed. Undo and redo use the original edit
  target and restore exact authored property/API-schema state.
  Independent per-proxy materials require making the instance editable first.

`clear_material(paths) -> None`
: Remove material-binding relationships authored on the selected prims in the
  current edit layer. Proxy children promote to their nearest non-proxy ancestor,
  affecting the whole instance. Other prim properties, references and geometry
  remain unchanged. Weaker or inherited materials may become visible; this does
  not author an explicit unbind. Undo restores exact relationship specs, including
  binding purposes and metadata, in the original edit target. Positional-only.

`bind_material(paths, material_path) -> None`
: Bind an existing material to each valid prim. Authors direct bindings; undo
  restores previous bindings. Expand target descendants yourself when requested.
  Instance proxies cannot receive direct edits; use `assign_material` for them.

`connect_shader_input(input_path, source_output_path) -> None`
: Connect existing shader properties, e.g. `/World/Looks/Paint/Surface.inputs:diffuseColor`
  and `/World/Looks/Paint/Texture.outputs:rgb`. Both are USD property-path strings.

`connect_shader_node(input_path, shader_id, node_name, output_name) -> None`
: Create a sibling shader node beside the input's owning shader and connect its
  named output to the existing input. `shader_id` and `output_name` must be nonempty;
  use a shader identifier supported by the installed registry. Undo removes the
  created node and restores the original input.

`connect_materialx_node(input_path, node_def, node_name) -> None`
: Create a node from a MaterialX NodeDef identifier and connect its output to an
  existing input. Authors the complete typed NodeDef interface and defaults.
  Available definitions depend on the installed MaterialX libraries.

`disconnect_shader_inputs(input_paths) -> None`
: Remove incoming connections authored in the edit layer while retaining input properties.

`clear_shader_inputs(input_paths) -> None`
: Remove edit-layer input properties to expose weaker opinions/schema defaults.

`clear_relationships(property_paths) -> None`
: Remove complete relationship specs in the current edit target. Proxy and
  prototype properties are read-only. Weaker relationship targets may reappear;
  no empty-target blocking opinion is authored. Undo/redo use the original edit
  target and preserve exact targets and metadata. Positional-only.

`clear_dependencies(property_paths) -> None`
: Remove edit-layer relationship targets and attribute connections; retain the
  properties and values. Undo restores previous property specs.

## stageviz.ViewState

Obtain with:

```python
view_state = stageviz.Session().viewState()
```

The wrapper is non-owning.

### Camera

`camera() -> stageviz.ViewCamera`

### Background and grid

`backgroundColor() -> tuple[float, float, float, float]`

`setBackgroundColor(color) -> None`
: Accept an RGB or RGBA numeric sequence in 0..1, e.g. `(0.12, 0.14, 0.2)`. Alpha defaults to 1.0. This differs from UI integer colors.

`gridColor() -> tuple[float, float, float, float]`

`setGridColor(color) -> None`

`gridEnabled() -> bool`

`setGridEnabled(enabled) -> None`

### Materials and lighting

`materialMode() -> int`

`setMaterialMode(mode) -> None`
: Use `stageviz.MaterialAll`, `MaterialClay` or `MaterialOverride`. Override mode uses the material path on the auxiliary stage.

`overrideMaterial() -> str`

`setOverrideMaterial(path) -> None`
: Accept empty path or an absolute prim path on the auxiliary stage.

`defaultCameraLightEnabled() -> bool`

`setDefaultCameraLightEnabled(enabled) -> None`

`defaultDomeLightEnabled() -> bool`

`setDefaultDomeLightEnabled(enabled) -> None`

`domeLightTexture() -> str`

`setDomeLightTexture(path) -> None`
: Empty string restores HDx's built-in Stinson Beach HDRI.

`domeLightCameraVisibility() -> bool`

`setDomeLightCameraVisibility(visible) -> None`

`sceneLightsEnabled() -> bool`

`setSceneLightsEnabled(enabled) -> None`

`showGuides() -> bool`
: Return whether viewport guides purpose geometry is displayed.

`setShowGuides(enabled) -> None`
: Set viewport guides purpose visibility through native Hydra controls. Saved with preserved view state; independent of Outliner filtering and isolation. Default-purpose geometry remains included.

`showProxy() -> bool`
: Return whether viewport proxy purpose geometry is displayed.

`setShowProxy(enabled) -> None`
: Set viewport proxy purpose visibility through native Hydra controls. Saved with preserved view state; independent of Outliner filtering and isolation. Default-purpose geometry remains included.

`showRender() -> bool`
: Return whether viewport render purpose geometry is displayed.

`setShowRender(enabled) -> None`
: Set viewport render purpose visibility through native Hydra controls. Saved with preserved view state; independent of Outliner filtering and isolation. Default-purpose geometry remains included.

`sceneMaterialsEnabled() -> bool`

`setSceneMaterialsEnabled(enabled) -> None`

### Color management

The viewport renders in scene-linear Rec.709/sRGB primaries. An OCIO display
transform targets an sRGB display, after scene AO and before selection outlines.
The pinned built-in configuration is
`studio-config-v2.2.0_aces-v1.3_ocio-v2.4`; no external config/LUT files are needed.
These view-only settings participate in remembered session state, not USD undo.
The Display menu offers exposure/gamma presets; Python accepts custom values
within the ranges below. No menu preset is checked for a custom value.
Texture input conversion remains the responsibility of USD/MaterialX.
Rendered material swatches follow `colorView()` while keeping exposure zero and
viewing gamma one. Visible swatches reuse cached floating-point linear renders;
hidden ones refresh when shown. Their linear/display image cache uses a 128 MiB
LRU budget per material renderer. Previously rendered layer compositions share
that budget and can be reused when their composed networks match. Texture image
thumbnails keep the existing OpenImageIO preview conversion.

`exposure() -> float`

`setExposure(value) -> None`
: Exposure in stops, clamped to -20..20; zero is neutral. Rejects non-finite values.

`gamma() -> float`

`setGamma(value) -> None`
: Additional viewing gamma after OCIO, clamped to 0.1..4; one is neutral.
  Rejects non-finite values. This is not the sRGB encoding setting.

`colorView() -> str`

`setColorView(view) -> None`
: Select an exact name returned by `colorViews()`; unsupported names raise
  `ValueError`. Default is `Un-tone-mapped`. `Raw` bypasses OCIO while retaining
  exposure and gamma. Alpha, depth, picking IDs and editor outline colors are preserved.

`colorViews() -> list[str]`
: Available views for the fixed `sRGB - Display` output display.

The embedded interpreter also provides the upstream `PyOpenColorIO` package:
`import PyOpenColorIO as OCIO`. It is independent of `stageviz`; changing its
current global config does not replace the viewport's pinned configuration.

### Ambient occlusion

Stageviz viewport AO is runtime view state, not USD scene authoring. Numeric
setters below clamp finite values to the stated native range. Radii use device
pixels. The quality and debug setters validate their enum range.

`ambientOcclusionEnabled() -> bool`

`setAmbientOcclusionEnabled(value) -> None`
: Enable the AO pass.

`ambientOcclusionContactAmount() -> float`

`setAmbientOcclusionContactAmount(value) -> None`
: Contact strength, 0..10.

`ambientOcclusionContactRadius() -> float`

`setAmbientOcclusionContactRadius(value) -> None`
: Contact radius, 1..128 device pixels.

`ambientOcclusionBroadAmount() -> float`

`setAmbientOcclusionBroadAmount(value) -> None`
: Broad strength, 0..10.

`ambientOcclusionBroadRadius() -> float`

`setAmbientOcclusionBroadRadius(value) -> None`
: Broad radius, 1..512 device pixels.

`ambientOcclusionNormalBias() -> float`

`setAmbientOcclusionNormalBias(value) -> None`
: Normal bias, 0..0.5.

`ambientOcclusionFalloff() -> float`

`setAmbientOcclusionFalloff(value) -> None`
: Falloff, 0.25..8.

`ambientOcclusionContrast() -> float`

`setAmbientOcclusionContrast(value) -> None`
: Contrast, 0.1..4.

`ambientOcclusionEdgeSharpness() -> float`

`setAmbientOcclusionEdgeSharpness(value) -> None`
: Bilateral edge sharpness, 0..4.

`ambientOcclusionBlurEnabled() -> bool`

`setAmbientOcclusionBlurEnabled(value) -> None`
: Enable bilateral blur.

`ambientOcclusionBlurRadius() -> float`

`setAmbientOcclusionBlurRadius(value) -> None`
: Blur radius, 1..32.

`ambientOcclusionQuality() -> int`

`setAmbientOcclusionQuality(value) -> None`
: Use AmbientOcclusionLow, AmbientOcclusionMedium, AmbientOcclusionHigh or AmbientOcclusionUltra from stageviz.

`ambientOcclusionDebugMode() -> int`

`setAmbientOcclusionDebugMode(value) -> None`
: Use AmbientOcclusionComposite, AmbientOcclusionCombined, AmbientOcclusionContact or AmbientOcclusionBroad from stageviz.

### Double-sided mode

`doubleSidedMode() -> int`

`setDoubleSidedMode(mode) -> None`

Documented values in the binding:

- `0 = Primitive`
- `1 = DoubleSided`
- `2 = SingleSided`

### Rendering/view quality

`renderMode() -> int`

`setRenderMode(mode) -> None`
: Use `stageviz.RenderShaded` or `stageviz.RenderWireframe`.

`complexityLevel() -> int`

`setComplexityLevel(level) -> None`
: Use `stageviz.ComplexityLow`, `ComplexityMedium`, `ComplexityHigh` or `ComplexityVeryHigh`.

`rendererAov() -> str`

`setRendererAov(name) -> None`

### View overlays

`sceneStatsEnabled() -> bool`

`setSceneStatsEnabled(enabled) -> None`

`performanceStatsEnabled() -> bool`

`setPerformanceStatsEnabled(enabled) -> None`

`cameraAxisEnabled() -> bool`

`setCameraAxisEnabled(enabled) -> None`

## stageviz.ViewCamera

Obtain from `stageviz.Session().viewState().camera()`.

### Interaction

`frame(bbox) -> None`
: Set bounds and frame them with the current camera. Pass `session.boundingBox()`
  or `session.boundingBox(paths)` to frame the stage or selection.

`isIdentity() -> bool`

`resetView() -> None`

`tumble(x, y) -> None`

`truck(right, up) -> None`

`distance(factor) -> None`
: Adjust camera distance by a zoom factor.

`mapToFrustumHeight(height) -> float`
: Map integer pixel height to frustum height for camera interaction.

`camera() -> capsule`
: Return a Python capsule named `stageviz.GfCamera` containing a heap-allocated copy of the native `GfCamera`. This is not exposed as a normal `pxr.Gf.Camera` Python object by this binding.

### Aspect ratio and letterbox

`aspectRatio() -> float`

`setAspectRatio(value) -> None`

`aspectRatioLocked() -> bool`

`setAspectRatioLocked(value) -> None`

`letterboxEnabled() -> bool`

`setLetterboxEnabled(value) -> None`

`letterboxOpacity() -> float`

`setLetterboxOpacity(value) -> None`
: Set opacity in 0..1; the native camera clamps the value.

### Projection and physical camera

`projectionMode() -> int`

`setProjectionMode(value) -> None`

Documented values:

- `0 = FieldOfView`
- `1 = Physical`
- `2 = Orthographic`

Use `stageviz.ProjectionFieldOfView`, `ProjectionPhysical` or
`ProjectionOrthographic`.

`setCameraView(preset) -> None`
: Choose `stageviz.CameraViewPerspective`, `CameraViewTop`, `CameraViewFront` or
  `CameraViewSide`. The preset adjusts orientation and projection. The C++
  `cameraView()` getter is not exposed in Python.

`fov() -> float`

`setFov(value) -> None`

`fovDirection() -> int`

`setFovDirection(value) -> None`
: Use `stageviz.FovVertical` or `stageviz.FovHorizontal`. Field-of-view and axis angles use degrees.

`focalLength() -> float`

`setFocalLength(mm) -> None`

`sensorWidth() -> float`

`setSensorWidth(mm) -> None`

`sensorHeight() -> float`

`setSensorHeight(mm) -> None`

### Framing

`focusPoint() -> tuple[float, float, float]`

`setFocusPoint(x, y, z) -> None`

`boundingBox() -> tuple`

`setBoundingBox(bbox) -> None`
: Accept the Stageviz bounding-box tuple representation.

`fit() -> float`

`setFit(value) -> None`

### Orientation

`cameraUp() -> int`

`setCameraUp(value) -> None`
: Use `stageviz.CameraUpX`, `CameraUpY` or `CameraUpZ`.

`axisYaw() -> float`

`setAxisYaw(value) -> None`

`axisPitch() -> float`

`setAxisPitch(value) -> None`

`axisRoll() -> float`

`setAxisRoll(value) -> None`

### Interaction mode

`cameraMode() -> int`

`setCameraMode(value) -> None`
: Use `stageviz.CameraModeNone`, `CameraModeTruck`, `CameraModeTumble`, `CameraModeZoom` or `CameraModePick`.

### Clipping and distance

`nearClipping() -> float`

`setNearClipping(value) -> None`

`farClipping() -> float`

`setFarClipping(value) -> None`

`cameraDistance() -> float`

`setCameraDistance(value) -> None`

`reset() -> None`

## stageviz.UI

Construct with:

```python
ui = stageviz.ui()
```

The wrapper points to the application's native `UI` object and does not own it.
`stageviz.UI()` and `stageviz.application().ui()` return the same application-owned subsystem.

### Enums

The binding creates Python `IntEnum` classes and also exposes every member directly on `UI`, so both of these forms are valid:

```python
stageviz.UI.ColorRole.AxisX
stageviz.UI.AxisX
```

#### ColorRole members

`Accent`, `AccentAlt`, `AxisX`, `AxisY`, `AxisZ`, `Base`, `BaseAlt`, `Border`, `BorderAlt`, `Button`, `ButtonAlt`, `Error`, `Graph`, `Grid`, `Guide`, `Handle`, `Highlight`, `HighlightAlt`, `Item`, `ItemAlt`, `Progress`, `Render`, `RenderAlt`, `Selection`, `SelectionAlt`, `Sweep`, `Text`, `TextAlt`, `Warning`, `Readonly`.

#### IconRole members

`Arrow`, `BranchClosed`, `BranchOpen`, `Checked`, `Clear`, `Code`, `Collapse`, `DefaultPrim`, `Display`, `Down`, `DropDown`, `Expand`, `Export`, `ExportImage`, `Follow`, `FrameAll`, `Geometry`, `Hidden`, `Instance`, `Layer`, `EditLayer`, `Left`, `List`, `Lock`, `Material`, `Move`, `Nested`, `New`, `Open`, `Over`, `Override`, `PartiallyChecked`, `Payload`, `Pick`, `Pivot`, `Prim`, `Proxy`, `Redo`, `Right`, `Rotate`, `Run`, `Scale`, `Select`, `Shaded`, `Transform`, `Undo`, `Unlock`, `Up`, `Visible`, `Wireframe`.

#### UIScale members

`Small`, `Medium`, `Large`.

#### UIState members

`Normal`, `Disabled`.

### Methods

`color(role, state=UI.UIState.Normal) -> tuple[int, int, int, int]`
: Read a theme color using a ColorRole and Normal/Disabled state. Channels are
  integers in 0..255, unlike ViewState's float colors. For a Qt color use
  `QtGui.QColor(*ui.color(stageviz.UI.ColorRole.Accent))`.

`setColor(role, r, g, b, a=255) -> None`
: Set the theme color with separate integer channels.

`iconPath(role) -> str`
: Get an IconRole resource/path string; pass it to `QtGui.QIcon` for a Qt widget.
  There is no Python `UI.icon()` method returning QPixmap.

`setIconPath(role, path) -> None`
: Set the icon path for that role.

`fontSize(scale) -> int`

`setFontSize(scale, size) -> None`
: Read/set integer font size for `UI.Small`, `UI.Medium` or `UI.Large`.

`iconSize(scale) -> int`

`setIconSize(scale, size) -> None`
: Read/set integer icon size for the chosen UI scale.

`axisSize(scale) -> int`

`setAxisSize(scale, size) -> None`
: Get/set the corner-axis image size for the chosen UI scale.

`gizmoSize(scale) -> int`

`setGizmoSize(scale, size) -> None`
: Read/set integer gizmo size for the chosen UI scale. Size accessors do not
  return QSize objects.

`transformMoveSnap() -> float` and `setTransformMoveSnap(value) -> None` use stage units.

`transformRotateSnap() -> float` and `setTransformRotateSnap(value) -> None` use degrees.

`transformScaleSnap() -> float` and `setTransformScaleSnap(value) -> None` use scale units.

`styleSheet() -> str`
: Return the unresolved application stylesheet template.

`setStyleSheet(style_sheet) -> None`
: Set the unresolved stylesheet template and refresh.

`refresh() -> None`

`colorSpace() -> str | int | None`
: On Qt 6, return a description string when available, otherwise the native primaries enum integer; return `None` if invalid.

`setColorSpace(name) -> None`

Accepted case-insensitive names/aliases:

- `srgb`
- `displayp3`, `display-p3`
- `adobergb`, `adobe-rgb`
- `bt2020`, `rec2020`

## stageviz.RenderEngine

Construct directly:

```python
renderer = stageviz.RenderEngine()
```

The wrapper owns a native offscreen `RenderEngine` and destroys it when the Python object is released.

### Stage

`set_stage(stage) -> None`
: Accept a native `pxr.Usd.Stage` and retain that same stage, including its session layer, load rules, population mask, and muted layers. The binding does not reopen the root layer. Objects that only imitate `GetRootLayer()` are not accepted.

### Resolution and camera

`set_size(width, height) -> None`
: Both dimensions must be greater than zero.

`set_camera(path) -> None`
: `path` must be an absolute prim path to an existing `UsdGeomCamera` on the renderer stage.

### Render settings

`set_background(r, g, b, a=1.0) -> None`
: Components must each be in `[0.0, 1.0]`.

`set_scene_lights_enabled(enabled) -> None`

`set_default_camera_light_enabled(enabled) -> None`

`set_scene_materials_enabled(enabled) -> None`

`set_aov(name) -> None`
: Name must be non-empty.

### Query/render

`aovs() -> list[str]`
: Initialize the renderer if needed and return supported AOV names.

`hgi_api() -> str`
: Initialize if needed and return the active Hgi API name.

`render(filename) -> str`
: Render the current stage offscreen, save the resulting `QImage`, and return the absolute output path. A stage must be set first. Calls require a working graphics context/backend; this is offscreen rendering inside Stageviz, not a promise of GPU-free headless operation. `set_camera()` copies the USD camera at default time; call it again after changing the camera prim.

## Important behavioral notes

### Commands versus direct mutation

`stageviz.command` is explicitly connected to the native `CommandStack`, so use it for Stageviz edits that should be undoable. Direct mutations through `SelectionList`, `ViewState`, `ViewCamera`, `UI`, or normal `pxr` APIs do not automatically gain command-stack behavior from these bindings.

### Edit layer

Attribute commands and transform reset behavior are described by their bindings as active-edit-layer aware. `Session.setEditLayer()` only accepts layers already present in the local layer stack and is an explicit exception to direct-setter behavior: it runs an undoable command.

### Unsafe stages

The presence of `stageUnsafe()` and `auxiliaryUnsafe()` is intentional but dangerous to use casually. The Python wrappers expose native lock addresses only; they do not provide a Python context manager for those locks. Prefer the normal stage accessors unless surrounding native code already guarantees the lock.

### Qt widget lifetime

`Application.show()` retains a Python reference in a process-global vector before queuing the widget display. Scripts do not need to keep their own only reference solely to survive until the queued `show()` call, but the current binding does not expose a corresponding explicit release mechanism for that retained reference.

### Unsupported value types in attribute commands

The command binding intentionally supports only a small simple subset of `VtValue` conversions. Do not pass matrices, tokens, arrays, asset paths, quaternions, dictionaries, or arbitrary `pxr.Gf` objects to `set_attribute_value(s)` through this binding unless the binding is expanded.

### Symbolic constants

Use the [module constants](#module-constants) for integer enum arguments. UI also exposes nested IntEnum classes. ViewState constants describe viewport state, not the native RenderEngine Settings enums; there is no full Python RenderEngine Settings wrapper.

### Pivot frame editing

The `transform_pivots` command applies a 4-by-4 world-space
delta to the selected objects' pivot frames while keeping their geometry fixed.
Subset and instance-proxy paths resolve through the same target boundary as
object transforms. Targets are deduplicated, unselected inherited instances are
preserved, and undo restores the original edit-layer transform property specs.
The delta must be finite, affine, and invertible.

The viewport's independent Edit Pivot toggle uses the same Move/Rotate/Scale
gestures, with dotted axes and hollow handles. Pivot editing authors paired
`xformOp:transform:pivot` operations and a compensated object matrix.

### Copy with a world transform

The `copy_transforms` command creates sibling copies at
the original world transforms multiplied by the supplied 4x4 row-vector delta.
Subsets and proxy children resolve through the same object boundary as move
commands; multiple selected children copy their instance root once. Originals
remain unchanged, copies become selected, and one undo removes the copies and
restores the exact previous selection. Redo recreates the copies.

In the viewport, hold Shift before starting a Move drag to copy on release.
Shift pressed during an ordinary drag keeps snapping; pivot, Rotate and Scale
drags retain their existing Shift behavior. A stationary click creates no copy.

### USD lights

The `new_light` command creates a standard USD light and selects it.
Supported types are `point`, `spot`, `distant`, `rect`,
`disk`, `cylinder`, and `dome`. Point/Spot use `SphereLight`; Spot also applies
`ShapingAPI`. The other types map to their corresponding UsdLux schemas. Creation
uses the current edit layer, unique sibling names, and the normal undo/redo stack.

```python
stageviz.command.new_light("/World", "Key", type="rect")
```

The Create menu exposes the same presets. Intensity, exposure, color, shape and
spot-cone inputs are ordinary USD properties. A Dome starts with Stageviz's
HDx Stinson Beach environment texture, replaceable through `inputs:texture:file`.
Material previews use the same HDx Stinson Beach HDRI as dome and viewport lighting.
Viewport light guides support click and sweep selection; existing transform
commands move and rotate the real light.
The guides are editor overlays, visible through geometry and excluded from USD
exports and viewport color capture. Scene lighting still follows Scene Lights.

## stageviz.plugins

The core file-format registry is exposed through `stageviz.plugins`.
Python callback adapters and native C++ providers share the same registrations.
All functions accept positional or keyword arguments. Registrations are process
local; initialize them in `stageviz.py`. See [file formats](python-file-formats.md)
for callback contracts, dialogs, startup discovery and a runnable plugin.

`registerFileFormat(name, extensions, loader=None, saver=None, options=None) -> None`

Register a unique name and sequence of simple extensions (without wildcards).
Names/extensions cannot conflict with another registration or built-in USD.
At least one conversion callback is required. Invalid types raise TypeError;
invalid/conflicting registrations raise ValueError.

`unregisterFileFormat(name) -> bool`

Remove a registration; returns False if absent. Existing imports remain usable.

`fileFormats() -> list[dict]`

Return metadata copies: name, extensions (tuple), readable and writable (bool).

`readFile(path, interactive=False, operation="open") -> Usd.Stage | None`

Convert one external file to an isolated, flattened, anonymous USD stage in memory.
The loader is called as `loader(filename, options)` and must return a live
`pxr.Usd.Stage`; other return types raise TypeError. None from this function means
options cancellation. It does not replace the active document or clear history.
The result owns its USD data independently of the loader and registration; its
lifetime follows ordinary stage references. There is no conversion file cache.
Noninteractive calls use empty options and skip the settings callback. Use
operation="merge" for merges. Exceptions propagate to Python; UI operations log
them and report status errors.

`writeFile(stage, path, interactive=False) -> bool`

Export a live `pxr.Usd.Stage` to an external format. Filenames/None are rejected as
stage arguments. The core takes an isolated anonymous in-memory snapshot and calls
`saver(stage, destination_filename, options)`. A saver may edit that snapshot
without affecting the supplied stage or active document. Returns False on options
cancellation; conversion errors raise. Only the external output is staged in a
sibling temporary directory and atomically published; there is no intermediate
USD file. Multi-file outputs are rejected. Document identity, dirty state and
undo history are unchanged.
