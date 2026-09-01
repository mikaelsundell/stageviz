# Stageviz Python API Reference

This reference is generated from the Stageviz CPython binding sources supplied on 2026-09-01. Treat it as canonical for this skill. It documents only behavior exposed by those bindings; it intentionally does not invent missing module globals or enum ordinals.

## Contents

- [API conventions](#api-conventions)
- [stageviz.Application](#stagevizapplication)
- [stageviz.Session](#stagevizsession)
- [stageviz.SelectionList](#stagevizselectionlist)
- [stageviz.command](#stagevizcommand)
- [stageviz.ViewState](#stagevizviewstate)
- [stageviz.ViewCamera](#stagevizviewcamera)
- [stageviz.Style](#stagevizstyle)
- [stageviz.RenderEngine](#stagevizrenderengine)
- [Important behavioral notes](#important-behavioral-notes)

## API conventions

### Naming

- `stageviz.command`: snake_case functions.
- `Application`, `Session`, `SelectionList`, `ViewState`, `ViewCamera`, `Style`: camelCase methods.
- `RenderEngine`: snake_case methods.

### Paths

Stageviz path-taking functions accept Python strings or string sequences and convert them to `SdfPath`. Commands reject empty paths. Several APIs require absolute prim paths or property paths as noted below.

### Return shapes

- Selected paths and masks are returned as Python lists of path strings.
- Colors are returned as `(r, g, b, a)` integer tuples unless noted otherwise.
- Bounding boxes are returned in the Stageviz helper representation `((min_x, min_y, min_z), (max_x, max_y, max_z))`.
- `ViewCamera.focusPoint()` returns `(x, y, z)`.

### Undoable edits

Functions in `stageviz.command` create native Stageviz `Command` objects and run them on the current session's `CommandStack`. Prefer them for edits that should participate in undo/redo.

## stageviz.Application

Construct with:

```python
app = stageviz.Application()
```

The wrapper points to `Application::instance()` and does not own the native application.

### Methods

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

`updateProgressNotify(message, completed, paths=None, status=0, details=None) -> None`
: Update the active progress block. `paths` is converted to a path list and `details` to a QVariant map.

`cancelProgressBlock() -> None`

`endProgressBlock() -> None`

`isProgressBlockCancelled() -> bool`

### Stage lifecycle and file operations

`newStage(policy=<native All value>) -> bool`

`load(filename, policy=<native All value>) -> bool`

`loadFromFile(filename, policy=<native All value>) -> bool`
: Alias of `load`.

`merge(filename) -> bool`

`mergeFromFile(filename) -> bool`
: Alias of `merge`; destructively merge authored content into the current root layer.

`mergeFlattened(filename) -> bool`

`mergeFlattenedFromFile(filename) -> bool`
: Alias; flatten an incoming stage and destructively merge it into the current root layer.

`mergeSublayer(filename) -> bool`

`mergeSublayerFromFile(filename) -> bool`

`mergeReference(filename, path) -> bool`

`mergeReferenceFromFile(filename, path) -> bool`
: `path` must be an absolute USD prim path.

`mergePayload(filename, path) -> bool`

`mergePayloadFromFile(filename, path) -> bool`
: `path` must be an absolute USD prim path.

`save(filename) -> bool`

`saveToFile(filename) -> bool`

`copy(filename) -> bool`

`copyToFile(filename) -> bool`

`flatten(filename) -> bool`

`flattenToFile(filename) -> bool`

`flattenPaths(paths, filename) -> bool`

`flattenPathsToFile(paths, filename) -> bool`

`loadState(filename) -> bool`

`saveState(filename) -> bool`

`setPreserveState(enabled) -> None`

`reload() -> bool`

`close() -> bool`

`isLoaded() -> bool`

### Stage/session state

`mask() -> list[str]`

`setMask(paths) -> None`

`stageUp() -> int`

`setStageUp(value) -> None`
: Accepts a native StageUp integral value. Exact ordinals are not defined by the supplied binding source.

`loadPolicy() -> int`

`boundingBox() -> tuple`

`filename() -> str`

### Edit layer

`editLayer() -> str | None`
: Return the active edit layer identifier.

`editLayers() -> list[str]`
: Return identifiers for the local layer stack (`GetLayerStack(false)`).

`setEditLayer(identifier_or_real_path) -> bool`
: Select a layer that is already present in the local layer stack. Raises `RuntimeError` if no stage is loaded and `ValueError` if no matching local layer is found.

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
: Exact native enum ordinals are not defined by the supplied binding source.

`flushPrimsUpdates() -> None`

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

`notifyStatus(status, message, details="") -> None`
: `status` is a native notification-status integral value. Exact ordinals are not all defined by the supplied binding source.

`setStatus(message, details="") -> None`
: Emit a success-status notification.

## stageviz.SelectionList

Normally obtain from `stageviz.Session().selection()` or `.selectionList()`.

### Methods

`isSelected(path) -> bool`
: `path` must be absolute.

`paths() -> list[str]`

`isEmpty() -> bool`

`isValid() -> bool`

`addPaths(paths) -> None`

`removePaths(paths) -> None`

`togglePaths(paths) -> None`

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

### Command history

`undo() -> None`

`redo() -> None`

`clear() -> None`

`can_undo() -> bool`

`can_redo() -> bool`

### Selection

`select_paths(paths) -> None`

`select_all(recursive=False) -> None`

`select_invert() -> None`

`select_payload() -> None`
: Select the nearest owning payload for the current selection.

`select_invert_payload() -> None`
: Invert the current payload selection.

### Visibility/isolation

`isolate_paths(paths) -> None`

`show_paths(paths, recursive=False) -> None`

`hide_paths(paths, recursive=False) -> None`

### Payloads

`load_payloads(paths, variant_set="", variant_value="") -> None`

`load_neighbor_payloads(paths) -> None`
: Load spatially neighboring payloads using `extentsHint` according to the native Stageviz implementation.

`unload_payloads(paths) -> None`

### Stage metadata

`set_stage_up(value) -> None`
: Accept a native StageUp integral value; exact ordinals are not defined here.

`set_default_prim(path) -> None`

`clear_default_prim() -> None`

### Namespace and prim creation

`delete_paths(paths) -> None`

`duplicate_paths(paths) -> None`

`new_prim(parent_path, name, type_name="") -> None`

`new_scope(parent_path, name) -> None`

`new_material(parent_path, name) -> None`
: Create a material with a UsdPreviewSurface shader.

`new_reference(parent_path, name, asset_path, prim_path="") -> None`

`new_payload(parent_path, name, asset_path, prim_path="") -> None`

`rename_path(path, name) -> None`

`new_xform(parent_path, name) -> None`

`move_path(paths, parent_path, insert_index=-1, preserve_world_transform=True) -> None`

### Attribute editing

Property-path arguments must be valid USD property paths.

`set_attribute_value(attribute_path, value) -> None`

`set_attribute_values(attribute_paths, value) -> None`

Supported Python values for the two setter functions are:

- `bool`
- `int` (converted to native signed 64-bit integer value)
- `float` (double)
- `str`
- numeric sequence of length 2 -> `GfVec2d`
- numeric sequence of length 3 -> `GfVec3d`
- numeric sequence of length 4 -> `GfVec4d`

Other value types are rejected by this binding.

`reset_attribute_values(attribute_paths) -> None`
: Clear authored default values from attributes in the active edit layer.

`reset_attribute_override(attribute_path) -> None`

`reset_attribute_overrides(attribute_paths) -> None`

### Transforms and pivots

`center_pivots(paths) -> None`

`reset_pivots(paths) -> None`
: Reset Stageviz-authored pivots while preserving local transforms.

`reset_transforms(paths) -> None`
: Reset local transforms in the active edit layer.

`identity_transforms(paths) -> None`
: Set local transforms to identity using edit-layer-aware transform reset behavior.

`reset_overrides(paths) -> None`
: Reset direct root-layer overrides.

### Materials

`bind_material(paths, material_path) -> None`

## stageviz.ViewState

Obtain with:

```python
view_state = stageviz.Session().viewState()
```

The wrapper is non-owning.

### Camera

`camera() -> stageviz.ViewCamera`

### Background and grid

`backgroundColor() -> tuple[int, int, int, int]`

`setBackgroundColor(color) -> None`
: Accept an RGB or RGBA sequence supported by the shared Stageviz color converter.

`gridColor() -> tuple[int, int, int, int]`

`setGridColor(color) -> None`

`gridEnabled() -> bool`

`setGridEnabled(enabled) -> None`

### Materials and lighting

`materialMode() -> int`

`setMaterialMode(mode) -> None`
: Valid native range is `ViewState::All` through `ViewState::Override`; exact ordinals are not defined by this binding source.

`overrideMaterial() -> str`

`setOverrideMaterial(path) -> None`
: Accept empty path or an absolute prim path on the auxiliary stage.

`defaultCameraLightEnabled() -> bool`

`setDefaultCameraLightEnabled(enabled) -> None`

`defaultDomeLightEnabled() -> bool`

`setDefaultDomeLightEnabled(enabled) -> None`

`domeLightTexture() -> str`

`setDomeLightTexture(path) -> None`
: Empty string restores OpenUSD's default dome texture.

`domeLightCameraVisibility() -> bool`

`setDomeLightCameraVisibility(visible) -> None`

`sceneLightsEnabled() -> bool`

`setSceneLightsEnabled(enabled) -> None`

`sceneMaterialsEnabled() -> bool`

`setSceneMaterialsEnabled(enabled) -> None`

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
: Native range is `Shaded` through `Wireframe`; exact ordinals are not explicitly documented here.

`complexityLevel() -> int`

`setComplexityLevel(level) -> None`
: Native range is `Low` through `VeryHigh`; exact ordinals are not explicitly documented here.

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

`isIdentity() -> bool`

`resetView() -> None`

`tumble(x, y) -> None`

`truck(right, up) -> None`

`distance(factor) -> None`
: Adjust camera distance by a zoom factor.

`mapToFrustumHeight(height) -> float`

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
: Binding documentation describes the intended range as 0.0 to 1.0; the wrapper itself forwards the value without additional range validation.

### Projection and physical camera

`projectionMode() -> int`

`setProjectionMode(value) -> None`

Documented values:

- `0 = FieldOfView`
- `1 = Physical`

`fov() -> float`

`setFov(value) -> None`

`fovDirection() -> int`

`setFovDirection(value) -> None`
: Native values are Vertical and Horizontal. Exact ordinals are not documented by this binding source.

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
: Native values are X, Y, Z. Exact ordinals are not documented by this binding source.

`axisYaw() -> float`

`setAxisYaw(value) -> None`

`axisPitch() -> float`

`setAxisPitch(value) -> None`

`axisRoll() -> float`

`setAxisRoll(value) -> None`

### Interaction mode

`cameraMode() -> int`

`setCameraMode(value) -> None`
: Native values include None, Truck, Tumble, Zoom, and Pick. Exact ordinals are not documented by this binding source.

### Clipping and distance

`nearClipping() -> float`

`setNearClipping(value) -> None`

`farClipping() -> float`

`setFarClipping(value) -> None`

`cameraDistance() -> float`

`setCameraDistance(value) -> None`

`reset() -> None`

## stageviz.Style

Construct with:

```python
style = stageviz.Style()
```

The wrapper points to the application's native `Style` object and does not own it.

### Enums

The binding creates Python `IntEnum` classes and also exposes every member directly on `Style`, so both of these forms are valid:

```python
stageviz.Style.ColorRole.AxisX
stageviz.Style.AxisX
```

#### ColorRole members

`Accent`, `AccentAlt`, `AxisX`, `AxisY`, `AxisZ`, `Base`, `BaseAlt`, `Border`, `BorderAlt`, `Button`, `ButtonAlt`, `Error`, `Grid`, `Handle`, `Highlight`, `HighlightAlt`, `Item`, `ItemAlt`, `Progress`, `Render`, `RenderAlt`, `Selection`, `SelectionAlt`, `Text`, `Warning`.

#### IconRole members

`BranchClosed`, `BranchOpen`, `Checked`, `Clear`, `Code`, `Collapse`, `Down`, `DropDown`, `Expand`, `Export`, `ExportImage`, `Follow`, `FrameAll`, `Geometry`, `Hidden`, `Left`, `Material`, `Open`, `PartiallyChecked`, `Payload`, `Prim`, `Redo`, `Right`, `Run`, `Shaded`, `Transform`, `Undo`, `Up`, `Visible`, `Wireframe`.

#### UIScale members

`Small`, `Medium`, `Large`.

#### UIState members

`Normal`, `Disabled`.

### Methods

`color(role, state=Style.UIState.Normal) -> tuple[int, int, int, int]`

`setColor(role, r, g, b, a=255) -> None`

`iconPath(role) -> str`

`setIconPath(role, path) -> None`

`fontSize(scale) -> int`

`setFontSize(scale, size) -> None`

`iconSize(scale) -> int`

`setIconSize(scale, size) -> None`

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
: Accept a Python object that behaves as a `pxr.Usd.Stage` and provides `GetRootLayer()`. The binding resolves the root layer identifier in the native Sdf registry and opens a native stage sharing that root layer. This is designed to work with anonymous in-memory layers as well as file-backed layers.

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
: Render the current stage offscreen, save the resulting `QImage`, and return the absolute output path. A stage must be set first.

## Important behavioral notes

### Commands versus direct mutation

`stageviz.command` is explicitly connected to the native `CommandStack`, so use it for Stageviz edits that should be undoable. Direct mutations through `SelectionList`, `ViewState`, `ViewCamera`, `Style`, or normal `pxr` APIs do not automatically gain command-stack behavior from these bindings.

### Edit layer

Attribute commands and transform reset behavior are described by their bindings as active-edit-layer aware. `Session.setEditLayer()` only accepts layers already present in the local layer stack.

### Unsafe stages

The presence of `stageUnsafe()` and `auxiliaryUnsafe()` is intentional but dangerous to use casually. The Python wrappers expose native lock addresses only; they do not provide a Python context manager for those locks. Prefer the normal stage accessors unless surrounding native code already guarantees the lock.

### Qt widget lifetime

`Application.show()` retains a Python reference in a process-global vector before queuing the widget display. Scripts do not need to keep their own only reference solely to survive until the queued `show()` call, but the current binding does not expose a corresponding explicit release mechanism for that retained reference.

### Unsupported value types in attribute commands

The command binding intentionally supports only a small simple subset of `VtValue` conversions. Do not pass matrices, tokens, arrays, asset paths, quaternions, dictionaries, or arbitrary `pxr.Gf` objects to `set_attribute_value(s)` through this binding unless the binding is expanded.

### Missing enum ordinals

Several C++ enums are accepted as raw integers, but the supplied wrapper sources do not expose their numeric definitions as Python enums. Do not guess them. Prefer existing values read from the object, documented constants from additional source supplied by the user, or a future binding that exposes proper Python enums.
