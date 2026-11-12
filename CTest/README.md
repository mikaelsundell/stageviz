# Native regression tests

## Run all tests

After configuring/building a development checkout with `build_app`, run:

```sh
./test_app.sh debug
./test_app.sh release
./test_app.sh all
```

On Windows, use `test_app.bat Debug`, `test_app.bat Release`, or
`test_app.bat All`. Both entry points use the same Python 3 controller and
refresh the existing CMake configuration with `BUILD_TESTING=ON` and incrementally build `stageviz` and
`stageviz_tests` before running **every registered CTest and every embedded
Python suite**. Native/GUI tests run
serially, followed by isolated application processes controlled through MCP.
The embedded API performance checks and UI large-tree checks are included.
Manual benchmark comparisons requiring supplied datasets are separate tools.
A graphical desktop and working viewport renderer are required; GUI checks
show disposable application windows. Runtime dependencies must be available
as they are when launching the development app normally.

`--list` lists embedded suites, `--no-build` uses current binaries, and
`--output PATH` chooses a new results directory. `--build-dir PATH`, `--app PATH`
and `--cmake PATH` support custom single-configuration builds. Examples:

```sh
./test_app.sh debug --no-build
./test_app.sh debug --output /absolute/path/new-results
```

The default output is `build.test-results/<run>/`, containing an overall
`summary.json`, configure/build/CTest/Python logs, native `ctest.xml`, per-suite
summaries, MCP oracles, resource samples and screenshot paths. Test failures
return a nonzero exit status. Python suites still run after CTest failures;
configure/build failures stop that configuration to avoid testing stale binaries. API timing baselines
are kept per configuration under the build's `test-baselines/` directory.

The disposable runner starts the public native MCP server on a free loopback
port and never connects to the user's app. Test settings use fresh per-process
INI storage before app initialization. `STAGEVIZ_TEST_SETTINGS_DIR` and
`STAGEVIZ_TEST_MCP_PORT` are set by the controller; port `0` requests a free port.
The macOS environment includes `DYLD_IMAGE_SUFFIX=_debug` for build and run.
Windows resource monitoring uses the native process API; macOS additionally
records system memory pressure and holds a finite App Nap activity scope.

`stageviz.test_app_runner` tests discovery, paths with spaces, configuration
selection, configuration-before-build ordering and failure propagation. The
configuration step refreshes IDE source lists before MSBuild/Xcode starts, so
new implementation files are compiled after switching branches or pulling changes.
`stageviz.gui.mcp_server_api` checks native
tools discovery on an ephemeral port and rejection of invalid Host/Origin
headers and invalid port settings.

`stageviz.clang_format_scope` uses disposable Git repositories to check formatting
scope: committed branch edits, staged/unstaged changes, new files, renames,
deletions, spaces in paths and a diverged base. Unchanged and ignored files are
excluded, and a clean branch never invokes the formatter.

## Regression coverage

The native Layer Editor multi-selection case runs with Qt's offscreen backend
so desktop focus changes cannot cancel its synthetic context-menu interaction.
It still checks selection preservation, batch lock/mute actions and undo. The
MCP `testlayermultiselection` suite checks the visible desktop dialog separately.

`session_layer_state_api` and the `testpreservestate` layer-controls phase cover
lock/mute/edit-target companion-state round trips, permission undo/redo, dirty
tracking, unchanged USD files, legacy state files and invalid lock data. Locked
layers remain retained while muted, and the Layer Editor refreshes its mute and
lock icons after restoration. Missing, muted or locked edit targets fall back
to the root layer.

Save summaries show expanded layer hierarchy, including clean ancestors needed
to locate nested edits. Location cells show filenames and retain full paths in
tooltips and Reveal context actions. State without a filename uses No filename
and Choose filename on save. GUI and MCP cases cover hierarchy, clean read-only
ancestors, basename presentation and the Reveal action's full-path target.
Save state precedes layer saves in the File menu, stays visible while Preserve
state is off, and is disabled then. Layer context menus separate edit-target
selection from saving.

The changes dialog uses a compact default size, a filling Status column and
interactive resizing on all five headers. Companion entries are labeled State.
The native state and GUI menu regressions and `testpreservestate.py` verify that
camera-only changes on a pristine Untitled stage neither dirty state nor prompt
on New; authoring content or loading a file establishes normal state tracking.
The close-stage statistics fixture disables Remember state so its rendering
checks remain noninteractive; state-save prompts have dedicated coverage.

`session_layer_update_api` checks selected file-backed layer updates, dirty
content restoration, deterministic redo, parent/child batches, edit-target
fallback/restoration, and preservation of unselected layers. The Layer Editor
places Update layer(s) below Save layer(s), disabling it for anonymous, locked,
or muted layers. `testlayerupdate.py` checks the menu actions, batch undo/redo,
external disk edits, unselected sublayers, and unchanged camera state via MCP.

`viewport_components_api` verifies that all nine tessellated gizmo meshes are
explicitly polygonal, rather than inheriting USD's Catmull-Clark fallback.
`testgizmostability.py` keeps Rotate active while creating a sphere and plane,
opens the Material Editor, and orbits through multiple camera revolutions and
distances, including repeated left/right sweeps through an edge-on red ring.
It checks unchanged handle topology, finite transforms, selection,
and rendered color bounds to detect spikes that only occur on the GPU. The
external MCP regression controller records independent stage/selection evidence.

The Outliner composition marker uses one strip per colored layer with a prim
spec, ordered left to right from weaker to stronger opinions. The identifiers
are cached lazily per row and reset by the existing prim invalidation path;
color changes use session state without rebuilding the USD stack. Repeated specs
from one layer contribute one strip. `outliner_layer_colors_api` checks selected
row pixels, adding/resetting overrides, undo, duplicate layers, cached rereads,
and clearing one layer color while retaining other contributors.

`make_editable_instance_api` checks disabling instancing in place on an instance
or proxy selection, including generated undefined prototype sources and weaker
composed instances. Children accept independent transform overrides while other
instances and placements remain fixed. Exact undo/redo preserves references,
selection, prior metadata, inert specs and the captured edit layer. Locked and
ineffective weaker edit targets leave no edits behind.
`testmakeeditable.py` exercises the public command and both root/proxy context
menus through MCP. It verifies Make editable / divider / Source ordering,
locked-layer availability, independent child movement, retained references and
selection, exact undo/redo, and captured menu/viewer presentation.

`export_subset_metadata` verifies that subset exports clear an omitted default
prim, filter stale root-order names, preserve surviving order and unrelated stage
metadata, and retain native instances without adding their original source
objects. Cleanup affects only the exported layer; the source remains unchanged.

`transform_source_child_api` verifies that moving, rotating, or scaling an
ordinary source child keeps its siblings and source/instance roots fixed while
the corresponding instance child inherits the shared edit. It checks resolved
gizmo targets, preview, exact undo, redo, and preservation of native instancing.
Mixed source/proxy selections retain their group transform handling.
`testsourcechild.py` checks the same behavior through public Python transforms
and a real Move gizmo drag, including gizmo placement, session selection,
unchanged siblings, corresponding proxy changes, exact undo/redo, and screenshots.

`render_primitive_selection_api` renders cone, cube, sphere, cylinder, and capsule
selection outlines through the viewport GL path. Selecting each primitive's
Xform parent must produce the same visible outline as selecting it directly.
Selection classification uses Hydra's complete renderable-primitive token
registry, covering implicit surfaces, meshes, curves, points, and volumes.

`export_instances` checks subset exports containing an ordinary prim together
with a native instance root or proxy child. Reopened files retain instance
geometry and bound material dependencies without exporting the source library
as a scene object. Export-visible traversal includes proxy geometry and prunes
invisible proxy branches.
`testexportinstances.py` exercises selected instance roots and proxy children
through the Python Session API, reopens the files in the live viewer, and records
Qt snapshots alongside external MCP prim inspection.

`outliner_empty_group_checkbox_api` checks mixed parent state when muting a layer
empties an Xform, with cached rereads, load/unload, group insertion, parent moves,
and undo/redo. Ordinary geometry-only branches remain excluded from payload
aggregation. The embedded payload hierarchy checks count empty Xform/Scope rows
as unchecked, including intentional empty groups. `testpayloadindicator.py`
reproduces layer muting and namespace changes through the public command bindings
with Qt state, screenshots, repeated cached reads, and undo/redo.

`property_reveal_file_api` checks platform reveal actions for payloads and
references authored with sublayer-relative paths, resolved asset attributes,
strongest-layer and stage filenames. Missing files and USD prim paths have no
reveal action. The test checks menu targets without launching a file browser.

`session_layer_colors_api` checks session-only layer colors, clear/restore, root
and sublayer Save As, and state round trips without USD color authoring.
`outliner_layer_colors_api` checks strongest-spec ownership, actual strip pixels,
selection, override/reset and undo, and clearing colors. It also checks matching
strips on selected root and sublayer rows in the Layer Editor. `testlayercolors.py`
checks Color and Clear Color availability in the Layer Editor context menu,
restores and saves color state, checks selected-row pixels, and captures viewer/Qt
snapshots. Native OS color panels are not automated.

`outliner_edit_layer_font_api` checks italics for prims absent from the active
edit layer, payload contents, refresh on edit-layer switches, and override
authoring/reset with undo/redo. The pseudo-root keeps its normal font.

`outliner_defined_prims_api` checks that flattened prototype roots and other
undefined prims stay hidden, including after definition changes. Defined untyped
groups, inactive prims, unloaded payloads, instances and proxies remain visible.

`new_xform_grouping_api` checks grouping a single selected prim, redundant
descendant selections, placement under transformed parents, selection and
undo/redo, plus explicit empty-child creation. The embedded `testapi.py` suite
also checks single-selection grouping through `new_xform`.
`testxformgrouping.py` drives Xform > New and Xform > Parent in the real Edit
and Outliner context menus with single and multiple selections. It checks empty
creation versus grouping, Parent enabled state, shortcut ownership, hierarchy,
selection, undo/redo and Qt snapshots in a disposable viewer. It also verifies
that Edit and Create own distinct Xform submenus sharing the same actions, with
no shared submenu attachments or cycles in the Xform menu graph.

The standalone `stageviz.python_documentation` case compares the reference with
the CPython registrations by owning surface, argument count and keyword names,
checks the keyword-support table, all module constants and UI enum members, and
compiles Python documentation blocks. It also checks local links in `skills/`.
`stageviz.python_documentation_guard` exercises detection of removed/renamed
APIs, argument/keyword drift, constant/enum drift and invalid Python syntax.
Run them without a build using `python3 CTest/check_python_documentation.py` and
`python3 CTest/test_python_documentation.py`. These checks do not initialize USD,
Qt or the viewer, and do not replace behavior regressions or graphics checks.

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
`viewport_capture_paths_api` exercises the shared native RenderView scan in a
real GL viewport: native-instance proxy resolution, masks applied immediately before capture, fresh versus
accumulating paths, legacy entry-point parity, duplicate suppression, completion
signals, clearing, stage closure, and unchanged selection/USD/history.
`python/testviewportcapture.py` provides disposable MCP phases (`setup`, `colors`,
`visible_paths`, `inspect`, `diagnostic`, `boundaries`, `cleanup`) for color/object alignment, native instance
resolution, exact selection preservation, selection undo/redo, the normal-direction
MaterialX override, masks/culling, and snapshot lifetime after stage closure.
The `visible_paths` phase checks the shared menu/Python tiled scan, accumulating
and one-shot modes, immediate mask/camera synchronization, collection across
opposite camera views, copied path lists, duplicate
suppression, menu selection undo/redo, unchanged USD/history during capture and
GUI-thread guards. Occluded and offscreen geometry are excluded. Qt snapshots and viewer images cover collection and selection
checkpoints. These tests require a disposable on-screen viewer with scene-ID
support; they do not run in the headless aggregate suite.
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
  It also compares default dome lighting with the explicit HDx Stinson Beach HDRI.
- `testviewporthdri.py` checks Preview Surface and MaterialX opacity with camera
  lighting, HDRI lighting, combined lights, default/custom HDRI paths, background
  toggles, and shaded/wireframe presentation in a disposable viewer. It stores
  viewport screenshots and Qt snapshots for each fixture.

- `light_command_api` covers all standard light presets, shaped spots, selection
  restoration, the HDx Stinson Beach dome texture and redo.
  `render_resources_api` checks HDx resource lookup and the Radiance header for
  the Stinson Beach HDRI shared by viewport, dome lights and material previews.
  `viewport_lights_api` covers guide picking, sweep hits,
  isolate masks, visibility and the absence of document authoring by helpers.
- `testlights.py` exercises every Create action and Python light factory, click
  and sweep selection, Move undo/redo, Qt snapshots and clean viewport capture
  in a disposable viewer.

- `numeric_editor_api` checks typing directly into a scrub-mode spin box,
  double-click replacement and shortcut ownership during numeric entry.
  `testnumericeditors.py` checks double-click replacement with conflicting
  application shortcuts, numeric replacement, stable row focus, repeated
  property drags without closing the editor, undo/redo refresh and keyboard
  commit through the real Property Editor in a disposable viewer. It also checks
  gizmo-panel visibility across selection changes and non-accumulating numeric
  Move/Scale previews.

- `material_numeric_editor_api` checks shared spin-box keyboard ownership,
  double-click replacement, integer and vector multi-material commits, scrub
  release, and undo/redo without duplicate commits when editing finishes.
- `property_asset_editor_api` checks the asset Open helper, preserved text
  editing, USD allowed-token dropdowns, Qt file-chooser cancellation and selection,
  and asset path undo/redo. It does not automate the native OS file chooser.
  It also checks the Designer-created Property View retains the Browse column
  and keeps the dome-light Open icon visible while editing the path.
- `property_material_light_dropdown_api` checks shader-definition choices for
  USD UV-texture wrapping/color space and MaterialX image addressing/filtering,
  schema choices on dome lights/light filters, and free-form string editing.

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
USD path, with transparent placeholders while rendering is pending.
`material_swatch_alpha_api` checks fully transparent background corners and an
opaque shaderball center for fallback, authored-network and interactive swatches.
These complement `qt_path_hash_api` and `indexed_path_api` native key
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

Outliner Reset shows All followed by the property-opinion groups actually authored
in the active edit layer. `reset_opinion_groups_api` covers all eight groups,
recursive and overlapping selections, definition preservation, locked layers and
exact undo after an edit-target switch, and mapped variant targets.
`outliner_reset_opinions_menu_api` checks the offscreen menu labels, divider,
available groups and locked-layer state. `testresetopinions.py` checks the live
menu, grouped and All commands, undo/redo, retained metadata and source-layer
isolation through a disposable MCP viewer. Discovery visits authored edit-layer
specs rather than traversing the entire composed payload hierarchy.

Layer Editor multi-selection is covered by `session_layer_batch_api` (nested
layer removal, retained handles, mixed mute/permission states and one-step
undo/redo), `layer_tree_multi_selection_api` (selection retention and batch
context actions), and `testlayermultiselection.py` through disposable MCP.
Batch remove/mute/permission operations share the single-layer command rules.

`session_state_dirty_api` checks camera/view dirtiness with coalesced notifications,
state-only writes without USD changes, failed writes, orthographic scale and AO,
payload-rule round trips, root pairing with an active sublayer, copy isolation,
and preservation disabled. The live `testpreservestate.py` suite checks Save state
menu availability, title dirtiness, restoration and Save/Don't Save/Cancel paths
through an external disposable MCP controller.

With Remember state enabled, companion state is paired with the root USD
filename, not the active edit layer. File > Save state writes only that sidecar;
ordinary Save handles state-only documents and saves pending state alongside
the active layer. Save/Don't Save/Cancel tests cover New, Reload and Close,
including a failed companion write preventing document replacement.
`preserve_state_menu_api` checks the save menu, root title dirty marker and
state-only Cancel behavior without requiring manual dialog confirmation.

The disposable controller compares the state suite's filename, selection and
payload checkpoints against independent MCP queries. Anonymous-root Save/Cancel
and color-dialog tests use Qt's non-native backend only inside their disposable
process; production still uses system dialogs. These tests do not claim visual
coverage of the native OS file picker. Runtime preferences are redirected to
disposable INI storage before startup so testing does not change another running
Stageviz session's preferences.

The dedicated Changes dialog summarizes root layers, modified sublayers and root
companion state in a five-column, scrollable table. `changes_dialog_api` checks
rows, headers, full-path tooltips, explicit Save all/Don't save/Cancel, Escape,
blocked entries and a 1,000-row list. `testchangesdialog.py` verifies the actual
Viewer flow for anonymous roots with multiple dirty sublayers, one save decision
before choosing the root filename, read-only/anonymous/session-layer entries,
discard and cancel. `testpreservestate.py` checks the state-only and mixed tables.
