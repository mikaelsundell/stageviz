# Native regression tests

This directory registers native C++ regression tests through CTest. Enable
them with `BUILD_TESTING=ON` in a configured Stageviz development build. The
native executable uses a separate `QCoreApplication`; the GUI executable uses
`QApplication` and offscreen rendering. Neither starts the full viewer or
initializes the embedded Python interpreter.

Native CTest coverage includes stage/session persistence, namespace and
composition edits, payload state, USD utilities, selection-list ordering and
large-set semantics, command undo/redo, tree selection backends, UI settings,
materials, geometry subsets, and RenderEngine initialization/image output.
Coverage also includes repeated ASCII and Crate saves, failure recovery, Save As
asset rebasing, namespace load rules, merge asset-path anchoring, and selected
export of session-layer opinions.

`indexed_path_api` checks indexed namespace coverage and intersection, stable
root reduction, duplicate collection, relative parent paths, and a 100,000-entry
selection. `batched_transform_authoring` checks 1,000 composed objects without
existing transform attributes, mapped variant edit targets, and exact undo
restoration while retaining pre-existing inert ancestor specs. Existing
transform, pivot, property, payload, namespace, and GUI cases cover the migrated
bulk callers. Performance comparisons belong in the external MCP benchmark;
these correctness tests deliberately avoid wall-clock thresholds.

`bbox_cache_invalidation` compares shared and Session bounds with a fresh USD
cache immediately after mutations, before deferred Qt delivery. It covers
transforms, inherited/reflected scale, size, visibility, and activation. It also
compares cold and warm selection bounds with the previous descendant-traversal
policy, including overlapping selections, guide/render purpose changes, reset
transform stacks, and replacement stages with identical paths. Selection bounds
reuse the shared cache with a purpose filter so they continue to exclude guide
geometry. Purpose changes do not invalidate USD's per-purpose cache entries.
`session_file_api` verifies reload publishes one closed/loaded transition pair.
`transform_instance_targets` checks ordinary geometry, point-instancer sources,
bare instanceable prims, native internal/external references and proxies, and
source-target updates after activation and payload loading changes. The shared
resolver can skip a whole-stage instance scan when USD has no native prototypes.
`spec_deletion_api` checks permissions, sibling/root/variant ownership, bounded
bulk diagnostics, and complete layer equivalence with the former namespace
deletion implementation. `batched_attribute_authoring` checks composed custom
attribute creation, rename/removal, time samples, metadata, and undo.
`python/testtransformoperation.py` additionally exposes `composed_batch()` for
public Python command, exact undo/redo, selection, screenshot, and source checks
in an explicitly disposable viewer.

The Python suites in `python/testapi.py` and `python/testui.py` run inside the
Stageviz Python editor. They cover the embedded command API and live Outliner UI
respectively; CTest cannot launch them because they require an initialized
Stageviz application session. The UI suite also checks the `stageviz.UI`,
`stageviz.ui()`, and `Application.ui()` bindings, including gizmo sizing and
transform snap settings. Run the suites manually from that editor after
loading the expected Stageviz application environment.

The in-app API suite covers command selection, select-all/invert, isolation,
selection lifecycle during edits, and now native instance proxy paths. The UI
suite covers Outliner selection projection and now verifies visible rows for
two proxies sharing one prototype, preserving the selected proxy/root identity
without selecting the source or sibling instance. CTest registers every case
implemented by the native and GUI executables.

The offscreen RenderEngine smoke test validates renderer setup and image
production. `python/testviewport.py` supplies an on-screen fixture for sweep
selection and Move/Rotate/Scale center-handle gestures; it pairs USD/session
assertions with screenshots and Qt snapshots. Axis-handle drags, snapping,
and performance on large datasets are outside that test's coverage.

Tests use temporary directories and do not modify project fixtures. Runtime
dependency paths must be available as for a normal development build.

On macOS, the application and test executable link `TBB::tbb` explicitly so
Debug builds inherit TBB's debug compile definitions and runtime selection.

The GUI case `viewport_components_api` covers viewport component boundaries:
masked selection caching, Y/Z-up auxiliary grids, high-DPI camera axes, gizmo
mode hit testing and visibility, auxiliary-stage resource replacement, and
widget transform-mode forwarding. It checks that presentation changes leave
the document stage untouched. `viewport_transform_pivot_api` checks that
manipulation uses fresh selection bounds even when the shared session bounds
cache still contains the previous transforms.

`python/testviewport.py` runs against a disposable on-screen viewer. It sweeps
multiple instance proxies, drags Move/Rotate/Scale center handles, verifies
owner deltas on instances and their proxies, and checks undo
and redo for each gesture. It captures viewer screenshots and plain Qt snapshots.
By default the test refuses to replace a stage with unsaved work and restores
the original file afterward. Set `_VIEWPORT_DISPOSABLE=True` only when launching
it in a disposable process. External MCP controllers can call its setup, sweep, drag, and
cleanup phases independently for read-only USD/session observations between
steps; keep MCP calls outside the embedded interpreter.

Geometry subsets remain exact USD paths in Session selection. They are also
inspectable under their owning geometry's Attributes > subsets section.
`geometry_subset_api` checks exact subset selection/undo/redo, object-action
resolution for transforms, framing and isolation, and leaf traversal;
`geometry_subset_gui_api` checks hidden Outliner rows, indices and material
bindings in the Property Editor, single-step indices undo/redo, and read-only
instance-proxy properties. It verifies that Outliner selection highlights the
parent while Session and the PropertyTree retain the subset identity.
`renderengine_api` compares subset and parent outlines and verifies that the
outline is visible. `python/testsubsets.py` exercises ordinary and
instanced geometry in a disposable live viewer, verifies face material colors,
owner picking and sweep selection, exact subset API selection with projected
Outliner/viewport display, binding undo/redo, and grouped properties
for multiple owners. It captures viewer screenshots and Qt snapshots and
supports the same external MCP checkpoint workflow as `testviewport.py`.

`command_move_api` also covers selections containing an internal-reference
source and its instance proxies. Source contents resolve to their reference
root; unselected instance roots retain their original world transforms.
Move, Rotate, Scale, exact layer restoration on undo, and redo are checked.
`python/testsharedviewport.py` provides a disposable live sweep/gesture check
for a saved dataset containing both ordinary source meshes and proxies.

`transform_operation_api` exercises the shared object-action boundary used by
Python matrix commands and interactive previews. It covers subset/proxy/source
resolution, duplicate targets, preservation of unselected instances, repeated
previews, Move/Rotate/Scale, exact undo/redo, and rejection before authoring for
conflicting or invalid inputs. `transform_operation_applied_api` uses the real
CommandStack to verify that Applied registers a preview without executing it
again and that one undo restores its original opinions. The viewport pivot
case also checks exact layer restoration after a no-motion drag.
`python/testtransformoperation.py` compares Python set_transforms and real gizmo
gestures on the same subset/source/proxy fixture in a disposable viewer, with
selection assertions, independent MCP checkpoints, screenshots, and undo/redo.

`pivot_operation_api` covers translated, rotated, and scaled pivot frames with
subset/proxy/source target resolution, repeated previews, fixed object geometry,
preservation of unselected inherited frames, singular-input rejection, and exact
layer undo/redo. `pivot_operation_applied_api` checks history registration without
reapplying a pivot preview. `python/testpivot.py` exercises the independent toolbar
toggle, all three center gestures, exact selection, fixed world geometry, and
undo/redo in a disposable on-screen viewer.

The pivot Python checks also cover normal Move, Rotate, and Scale drags after
Center Pivot, including previously edited pivot frames, unchanged geometry on
press, stable pivots through release, and exact undo/redo.

Pivot checks use `testbench.wait_for_commands()` before reading USD results,
issuing undo/redo, editing fixtures directly, or releasing shared-layer stages.
The reference-layer edge case releases its temporary fixture stage before
session authoring starts. This prevents USD notice delivery from racing the
destruction of a second stage's composition cache. Waiting for a visible value
alone does not establish command completion.
The `shared-layer-lifetime` Python phase repeats queued pivot edits and exact
undo while a second stage observes the same layers, verifies its composed
pivot values, and releases it only after command completion.

`console_output_file_api` covers native stdout/stderr logging, destination
switches, failed opens preserving the active file, append/truncate, synchronous
stop, and continued console capture. `python/testconsole.py` checks the public
logging bindings. `python/testscenestats.py` checks that File > Close resets the
viewport's stats to an empty-stage image, including closing with stats hidden;
it requires a disposable viewer. These scripts expose `run()` and do not autorun.


`viewport_color_query_api` covers hue wraparound, RGB tolerances, shaded magenta,
visible-pixel coverage, background exclusion and grayscale rejection.
`python/testviewportcapture.py` provides disposable MCP phases (`setup`, `colors`,
`inspect`, `diagnostic`, `boundaries`, `cleanup`) for color/object alignment, native instance
resolution, exact selection preservation, selection undo/redo, the normal-direction
MaterialX override, masks/culling, and snapshot lifetime after stage closure.
The `inspect` phase checks read-only USD/Hydra reports, proxy Rprim mapping,
visibility updates with undo/redo, default versus sampled visibility, and
diagnostic argument boundaries.
The script does not autorun; its `run(disposable=True)` executes all phases.

- `copy_transform_api` covers world-space copying below rotated/scaled parents,
  proxy subset deduplication, geometry and instancing through stronger transform
  overrides, exact selection restoration, and redo.
- `testviewport.py` exposes `copy_drag()` for Shift Move gestures, one-step
  undo/redo, original preservation, and stationary-click behavior in a disposable viewer.

- `renderengine_hdri_api` changes opacity through opaque, translucent, zero and
  back to opaque with the dome background off/on and a separate helper pass.
- `testviewporthdri.py` checks Preview Surface and MaterialX opacity with camera
  lighting, HDRI lighting, combined lights, default/custom HDRI paths, background
  toggles, and shaded/wireframe presentation in a disposable viewer. It stores
  viewport screenshots and Qt snapshots for each fixture.

- `light_command_api` covers all standard light presets, shaped spots, selection
  restoration and redo. `viewport_lights_api` covers guide picking, sweep hits,
  isolate masks, visibility and the absence of document authoring by helpers.
- `testlights.py` exercises every Create action and Python light factory, click
  and sweep selection, Move undo/redo, Qt snapshots and clean viewport capture
  in a disposable viewer.

- `numeric_editor_api` checks typing directly into a scrub-mode spin box.
  `testnumericeditors.py` checks numeric replacement, stable row focus, repeated
  property drags without closing the editor, undo/redo refresh and keyboard
  commit through the real Property Editor in a disposable viewer. It also checks
  gizmo-panel visibility across selection changes and non-accumulating numeric
  Move/Scale previews.

- `testlights.py` also isolates the default Distant light with other sources off,
  checking visible illumination, rotation, translation independence, and undo.

`viewport_transform_hierarchy_sync_api` checks absolute local numeric rotation,
translation and scale under rotated, non-uniformly scaled parents, rotation
in World/Object/Parent handle frames, live ring values, Applied registration,
and undo/redo equivalence. `python/testhierarchytransforms.py` drives real Qt
numeric fields, scrub release and all three rotation rings on World and Cube, checks panel/USD agreement
before and after release and undo/redo, and records viewer images and QObject
snapshots. Run it only in a disposable process, using its setup(), numeric(path),
and drag(path) entry points from an external MCP controller; read-only MCP
stage/prim/selection calls can bracket each action without resetting history
inside the action. Numeric fields display absolute local TRS; handle reference
frames affect interactive drag axes.

The hierarchy transform regression also compares the panel and authored matrices
against an independent `UsdGeomXformOp::TypeRotateXYZ` operation, including the
(-60, 56, 70) comparison case and values on both sides of Y=90 degrees. Compound
world-Z snapping uses the same XYZ convention and preserves the X/Y components
through preview, Applied registration and undo/redo.

Corner-axis sizing is centralized in `UI::axisSize` / `setAxisSize`, with 36/72/144
logical-pixel defaults. `ui_api` covers sizing notifications and invalid values;
`viewport_components_api` checks scaled images and hit targets at standard and
high DPI, including an odd logical size. `python/testaxissize.py` checks the
embedded API, immediate live repaint and the visible indicator footprint while
preserving the current USD stage and restoring the UI size.

Viewport gesture scripts frame the rendered active-object handle rather than
assuming it lies at the combined selection bounds center. Interactive Move and Scale check
a shared world delta; interactive Rotate checks a common rotation about each target's own
pivot and verifies that mesh descendants inherit their owner's delta once.
The shared-source fixture tracks internal-reference and instance roots for
these per-object rotation assertions.

## Render scene index removal

`stageviz.render_scene_index_removal` checks cached mesh, geometry, and subset
targets after subtree, missing-path, leaf, and root removals, including similarly
named siblings and readded geometry. It exercises presentation notifications
without creating an OpenGL context.

`stageviz.gui.outliner_layer_mute_api` checks mute, undo and redo against USD
composition and Qt rows, preserves surviving row identity/expansion, and exercises
mixed deletion/info notices below the bulk-delete threshold.

`viewport_transform_selection_refresh_api` checks one numeric-value notification per
selection change, lead-object reordering, deselection, tool reactivation, and stage
invalidation without changing selection. `testhierarchytransforms.py` also checks
selection undo/redo and selecting while the transform tool is disabled before
reactivating it in a live viewer.

`qt_path_hash_api` checks seeded Qt hashing and QHash/QSet lookup/removal for
50,000 USD prim paths plus empty, relative, property, variant and relationship
target paths. It protects the native SdfPath hash integration from key loss or
inconsistent hashing after reparse and container rehashing.

`benchmark/pipeline/README.md` describes the external disposable-app Python
correctness runner and large-data Outliner/property/render workflow. Namespace
edits cover inline rename, native tree drop handlers, undo/redo, world-pose
preservation, rendered path validity and restoration without saving source data.

Path collection conventions are documented in [docs/path-collections.md](../docs/path-collections.md).
`selection_list_semantics` checks identical and duplicate updates with empty paths
and order changes, including notification counts. `material_browser_api` checks
that reordered, removed, and appended entries retain selection and swatches by
USD path. These complement `qt_path_hash_api` and `indexed_path_api` native key
and collection coverage.

Native performance logging regressions cover unset, empty and enabled startup
settings, a cached destination, nested JSONL events and append behavior. See
[performance logging](../docs/performance-logging.md) for developer usage.

`payload_nested_rules` and `payload_overlapping_batch` cover effective load-rule
restoration across nested payload parent/child load, unload, undo and redo,
including duplicate and overlapping targets. Commands retain one rule snapshot
for the entire batch and restore individual edits in reverse execution order.
Rule repair uses scoped loads/unloads, including `LoadWithoutDescendants`,
without triggering a whole-stage root resync. Redundant rule encodings are
ignored when comparing equivalent load policies.

`python/testpayloadhierarchy.py` builds a disposable 15-file fixture with 9,605
composed prims, four payload levels, sublayers, external references, native
instances and independent branches. It exercises real Outliner checkbox clicks,
partial group states, hidden nested load exclusions, instance sibling isolation,
overlapping batches, root load/unload/undo, queued opposite and mixed checkbox
clicks, menu actions, row selection, filtering, collapsed branches, rename/move/delete,
load-policy changes, fixed-seed command sequences and reload in Payloads mode.
The layer-inversion workflow verifies that direct inversion equals Select in
Layer followed by inversion, and that unloading the complement preserves selected
payloads and payloads authored in other layers through undo/redo.
Every checkpoint verifies every visible payload gate and grouping checkbox against
USD; external MCP prim-info calls independently check all visible gates and the
relevant hidden nested payloads. Every checkpoint checks source
file hashes and captures Qt/viewport evidence; the external pipeline brackets
phases with independent MCP session and selection observations.

Run it with `CTest/benchmark/pipeline/run_regressions.py --suites testpayloadhierarchy`
and the required `--app` and `--output` arguments. Payloads mode intentionally
stops Outliner traversal at the first payload gate: its checkbox describes that
prim's own loaded state, rather than aggregating hidden nested payload rules.

`stageviz.gui.outliner_payload_group_checkbox_api` guards grouping rows against
stale ordinary-geometry checkbox state after root undo and command-driven updates.
`stageviz.gui.outliner_payload_queued_checkbox_api` checks that the last request
wins within a queued click batch, cancelled batches add no undo command, and stage
replacement cancels the previous stage's pending checkbox timer.
