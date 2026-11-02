# Python binding coverage audit

Scope: the types registered in `pymodule.cpp`, their corresponding C++ public interfaces, and the `stageviz.command` module. This audit compares declarations and binding registrations; method presence does not establish full type or runtime parity.

## Coverage after this audit

| C++ interface | Python coverage | Remaining differences |
| --- | --- | --- |
| Session | All public operation/accessor names are represented, including file operations, edit layers, progress, auxiliary stage, and prim-update policy. | Locks and `commandStack()` return pointer integers, not usable Python wrappers. Signals are not exposed. |
| SelectionList | All nine public operations/accessors are bound. | Paths use strings; Qt signals are not exposed. |
| ViewState | All public operations/accessors are bound. | Colors and paths use Python tuples/strings; Qt signals are not exposed. Enum constants were added at module level. |
| ViewCamera | All public operation names are bound after adding `frame()`. | Constructors wrap the current session camera instead of creating independent cameras. `camera()` returns an opaque capsule rather than `pxr.Gf.Camera`. Bounding-box tuples cannot preserve an oriented box's transform. Signals are not exposed. |
| UI | Color, icon paths/sizes, gizmo sizes, fonts, transform snap increments, stylesheet, color-space and refresh methods are bound. | `icon()` returning a QPixmap is missing. Python color-space conversion is narrower than arbitrary QColorSpace objects. Signals are not exposed. |
| Application | Window, session and UI accessors are bound; Python-specific `show()` and `invokeLater()` helpers also exist. | `console()`, `pythonInterpreter()`, `settings()` and static `instance()` have no corresponding methods. Construction and `stageviz.application()` provide access to the singleton. Signals and inherited QApplication APIs are not generally wrapped. |
| Command factories | Every public factory has a Python entry point after adding `set_transforms()`. | XformState snapshots are captured internally and are not accepted as Python arguments. Attribute values support only bool/int/float/string and numeric sequences of length 2–4, rather than all VtValue types. Commands execute immediately through the current session instead of returning Command objects. |
| CommandStack | Undo, redo, clear and all three availability queries are accessible through `stageviz.command`. | No CommandStack type or generic `run(Command*)` binding; no signals. |
| RenderEngine | Offscreen construction, stage/size setters, camera-by-path, selected rendering settings, image-file rendering, AOV names and Hgi API name. | Substantial partial coverage; see below. |

## RenderEngine gaps

The wrapper remains an offscreen rendering interface rather than a one-to-one binding. Missing native methods are:

- Lifecycle: `initialize`, `reset`, `isInitialized` (initialization currently happens implicitly).
- State getters: `stage`, `camera`, `size`, `viewport`, `settings`.
- Auxiliary stage: `setAuxiliaryStage`, `auxiliaryStage`, `refreshAuxiliaryStage`.
- Viewport and settings: `setViewport`, full `setSettings`.
- Selection: `setMask`, `setSelected`, `setSelectionBBoxes`, `setSelectionColor`.
- Rendering/picking: `renderToCurrentFramebuffer`, QImage-returning `renderImage`, both `testIntersection` overloads.
- Diagnostics: `renderStats`, `isColorCorrectionCapable`.

The existing `set_camera` accepts a USD camera prim path, whereas native `setCamera` accepts GfCamera. `render(filename)` writes an image file instead of returning QImage. Most Settings fields, including dome lighting, material override, draw mode, sidedness, complexity, purposes and selection-related controls, have no Python setters.

The stage conversion bug was fixed: `set_stage()` now retains the supplied USD stage instead of opening another stage from its root layer and losing session-specific composition state.

## Deferred prim-update behavior

The current `Session` C++ interface exposes `primsUpdate()` and `setPrimsUpdate()`, but not `flushPrimsUpdates()`. `Immediate = 0` and `Deferred = 1` are defined in `session.h`. Changing the policy back to `Immediate` flushes pending notices automatically in `session.cpp`. The Python binding exposes the two policy accessors and does not need a separate flush function. Large direct USD edit batches should restore the previous policy in `finally`; ordinary commands should rely on native Stageviz update handling.

## UI enum registration

The `pyui.cpp` binding registers the native `UI` enum members and the `UI` wrapper methods. This is a static source audit; it does not validate icon resources or runtime behavior.

## Compatibility and follow-up

Existing method names and opaque return forms were retained. Expanding Qt object, signal, renderer and USD value conversions requires explicit API design and runtime validation; this audit does not claim those gaps are closed. New constants are aliases of the corresponding C++ enum values rather than separately maintained ordinals.
