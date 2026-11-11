# Python binding boundaries

The [API reference](python-api.md) documents what Python can call in this checkout.
The native C++ headers are not a second Python API: a public C++ method, signal,
constructor or overload is callable from Python only when explicitly wrapped.
This document records deliberate boundaries rather than claiming C++ parity.

## Public surfaces

| Surface | Python access | Boundary |
| --- | --- | --- |
| Module | Application/session/UI/selection/viewport factories, MCP controls and console logging | Embedded module, not a standalone pip package. Injected editor objects are not portable module globals. |
| Application | Main window, session, UI, queued callable and widget display | No `console()`, `pythonInterpreter()`, `settings()` or `instance()` methods, inherited QApplication interface, or native signals. Use `stageviz.application()` for the singleton. |
| Session | Stage lifecycle, exports, per-layer saves/copies, companion state, edit target, progress, selection, view state, masks and update policy | No independent Python session construction, native signal binding, Python lock object or CommandStack object. File operations return bool. Composition edits use command functions, not removed Session merge methods. |
| SelectionList | Nine operations/accessors; paths are strings | Direct changes do not create history; signals are not exposed. |
| ViewState | Background/grid, lighting, materials, overlays, display quality and all AO settings | Colors use float RGBA in 0..1. No native signals; state controls the viewport rather than a separately created renderer. |
| ViewCamera | Current camera navigation, projection, view presets, lens/gate settings and framing | No independent camera creation. Native `cameraView()` getter is not exposed. `camera()` returns an opaque capsule, not `pxr.Gf.Camera`. Bounds use min/max tuples without an oriented-box transform. |
| UI | Integer colors, nested enums, icon paths/sizes, axis/gizmo sizes, fonts, snap increments, stylesheet, color space and refresh | No QPixmap-returning `icon()`. Use Qt QIcon with `iconPath()`. Color-space conversion accepts named aliases rather than arbitrary QColorSpace objects. Signals are not exposed. |
| command | Undo/redo/history queries plus selection, composition/layers, metadata, creation, namespace, properties, transforms/pivots and material connection commands | Python functions dispatch commands and return None; they do not return Command objects or generic transactions. Some commands finish asynchronously. Supported attribute values are a limited subset of VtValue. |
| Render / ViewportCapture | Weak active-viewport access, prim diagnostics, owned color/object-ID snapshots, and the menu's tiled visible-path scan/collection | No public constructors. `capturePaths(accumulate=True)` shares the menu collection; False is a fresh scan without collection changes. Image snapshots use a separate viewport-resolution scan. Capture requires an initialized supported viewport/graphics context. |
| RenderEngine | Owned offscreen renderer, stage/size/camera setters, selected settings, file rendering, AOV/Hgi queries | Partial native interface; see below. |

Module integer constants cover the documented enum inputs; UI additionally
exposes nested IntEnum classes and member aliases. Constants alias native enum
values; code should use their names instead of maintaining its own ordinals.

`command.make_editable_paths` exposes undoable native-instance conversion for
instance roots and proxy selections, retaining references and exact selection.

`command.update_layer` reloads a selected layer or batch from disk with undoable
content snapshots; redo uses the captured content. It does not reload the stage
or unselected sublayers.

## Native command factories without direct Python equivalents

The Python command surface is useful but is not every native factory/overload:

- `newMaterial(MaterialType)` creates built-in network presets natively. Python
  `new_material(parent_path, name)` calls the separate `newMaterialPath` factory
  and creates a UsdPreviewSurface material; it does not accept MaterialType.
- `newShaderNode`, `newMaterialXNode` and `deleteShaderNode` have no direct bindings.
  Python exposes node creation **connected to an existing input** through
  `connect_shader_node` and `connect_materialx_node`, and generic `delete_paths`
  subject to its native namespace rules.
- Native `TransformOperation`, `XformEdit`, `PivotOperation` and snapshot-based
  overloads are not Python types. `set_transforms(paths, before, after)` accepts
  world-matrix pairs and captures native state internally; `transform_pivots`
  accepts a world delta. Native operation construction/apply/restore helpers and
  `setPivotTransforms(PivotOperation)` are not direct Python calls.
- There is no Python Command/CommandStack type, generic `run(Command*)`, command
  grouping/context manager, or native command-stack signal interface. Access
  history through `stageviz.command`; `Session.commandStack()` is a pointer integer.
- `set_attribute_value(s)` accepts bool/int/float/string and numeric tuple/list
  vectors of length 2–4. It does not convert arbitrary USD arrays, matrices,
  tokens, asset paths, quaternions, dictionaries or Gf objects. It authors defaults,
  not time samples. Native target-type validation remains in effect.

Use direct OpenUSD only when needed, and describe those edits as untracked.
If a workflow needs undoable unsupported authoring, add a designed native command
and binding rather than inventing an automatic Python undo wrapper.

## RenderEngine gaps

Python is an offscreen file-output interface, not a one-to-one native binding.
Native APIs not exposed through that wrapper include:

- Lifecycle: `initialize`, `reset`, `isInitialized`; Python initializes implicitly.
- State getters: `stage`, `camera`, `size`, `viewport`, `settings`.
- Auxiliary stage: `setAuxiliaryStage`, `auxiliaryStage`, `refreshAuxiliaryStage`.
- Viewport and full settings: `setViewport`, `setSettings`.
- Selection: `setMask`, `setSelected`, `setSelectionBBoxes`, `setSelectionColor`.
- Framebuffer/QImage rendering and picking: `renderToCurrentFramebuffer`,
  `renderImage`, `testIntersection` overloads.
- Diagnostics: `renderStats`, `isColorCorrectionCapable`.

`set_camera(path)` copies a USD camera at default time instead of accepting
GfCamera. `render(filename)` writes a file instead of returning QImage. Most
Settings fields, including dome lighting, override materials, sidedness,
complexity, purposes and selection controls, have no Python setters. ViewState
changes do not configure this independent renderer.

`set_stage()` retains the supplied actual Usd.Stage, including its session layer,
load rules, mask and muted layers. It does not reopen a root layer. Capture from
`stageviz.render()` is a different API for the currently displayed scene.

The current viewport root-mask filtering can produce no captured paths when
isolating only a native-instance root. This existing limitation affects both
image-snapshot path extraction and tiled path scans; it is separate from resolving
visible instance IDs to selectable USD proxy paths in the unmasked view.

## Update delivery and ownership

`stage()`/`auxiliary()` obtain a stage through normal locked access, but the
returned USD object's later operations are not protected by a Python lock.
`stageUnsafe()`/`auxiliaryUnsafe()` assume native locking by the caller;
`stageLock()`/`auxiliaryLock()` expose addresses, not usable context managers.
Do not author concurrently with command workers.

Commands can return before worker completion. Wait for each deferred operation
and its Session completion callback before dependent commands, USD reads, saves,
stage replacement or undo/redo. The [guide](python-start.md#let-commands-finish-before-the-next-step)
contains a self-contained controlled-script pattern; embedded regressions use
`testbench.wait_for_commands()`. This is not a global thread-pool drain.

For large **direct USD** batches, save `primsUpdate()`, set `PrimsDeferred`, and
restore the original policy in `finally`. Returning to `PrimsImmediate` flushes
notices. There is no public `flushPrimsUpdates()` method. Do not use this manual
batch policy around ordinary commands; they manage their own update delivery.

## Maintaining the contract

Run `python3 CTest/check_python_documentation.py` after editing bindings or the
reference. It checks public names by surface, argument counts, keyword support,
module constants and UI enum membership. It also compiles Python documentation
blocks for syntax. It does not establish runtime type conversion, graphics
availability, resource validity, native behavior or complete C++ parity.

Keep API prose, [agent instructions](../SKILL.md), the [start guide](python-start.md)
and this boundary document consistent. Behavior changes still need focused
native/GUI and embedded Python regressions as described in [Qt testing](python-qt-testing.md).

Reset opinion groups are covered by `CTest/python/testresetopinions.py` and the native `reset_opinion_groups_api` case, including active-layer isolation and undo/redo.

Layer remove, mute and permission bindings accept one identifier or a sequence; `testlayermultiselection.py` covers one-step batch undo/redo and the multi-selection Layer Editor UI.

Companion-state dirtiness and root-stage pairing are exercised by `session_state_dirty_api` and `testpreservestate.py`, including state-only save prompts, camera/view changes, payloads, failed writes, copying and preservation disabled.
