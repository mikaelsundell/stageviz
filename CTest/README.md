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
On Windows, native CTests prepend the configured dependency runtime directories
to their process-local `PATH` so another configuration's OpenUSD DLLs on the
parent shell's `PATH` cannot override them.
The complete-suite controller uses the same generated runtime-path manifest
for embedded Python app processes. Reconfigure once after updating this setup
before using `--no-build`. The multi-layer GUI case uses the native Windows
platform because the offscreen plugin does not reproduce disabled icon styling.
Embedded Windows tests use `SetWindowPos` to keep the viewer on top without
recreating its native window after OpenGL initialization and invalidating its
GL context.
Use USD's forward-slash identifiers (`Path.as_posix()` for these file-backed
fixtures) for layer commands and row lookups, and compare filesystem paths with
`Path` objects. Assert the edit target after switching it: command completion
alone does not establish that the requested edit was accepted.

Command waits default to 60 seconds; the large payload fixture allows 120
seconds for Debug rendering. The complete API/performance suite runs as one MCP
request with a 30-minute limit. Other phase request limits remain unchanged.
Complete failure details are saved in each failed suite's `failure.txt`.
Owned Windows app processes request that the system and display stay awake
during testing, then restore the previous thread execution request on exit.
The controller checks input-desktop access before startup and while the app
runs. Locking the workstation stops verification with an explicit desktop
error rather than cascading blank-viewport failures. Keep it unlocked for GUI
verification; the controller does not change screen-lock policy.

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

The native Material Display popup case also uses the offscreen backend on
macOS/Linux so desktop focus changes cannot dismiss its inspection popup. Its
scoped timer is stopped when the popup closes, and the test distinguishes a
cancelled inspection from an incorrect filter highlight. The packaged live suite
continues to exercise the actual desktop menu. The preserve-state menu test
explicitly saves an anonymous remembered-state baseline before checking first
root-save refresh; authored Untitled stages can legitimately track view state.

`session_layer_state_api` and the `testpreservestate` layer-controls phase cover
lock/mute/edit-target companion-state round trips, permission undo/redo, dirty
tracking, unchanged USD files, legacy state files and invalid lock data. Locked
layers remain retained while muted, and the Layer Editor refreshes its mute and
lock icons after restoration. Missing, muted or locked edit targets fall back
to the root layer.

Save summaries show expanded layer hierarchy, including clean ancestors needed
to locate nested edits. Location cells show filenames and retain full paths in
tooltips and Reveal context actions. Remembered state is summarized on the root
row rather than as a separate file. GUI and MCP cases cover hierarchy, clean
read-only ancestors, basename presentation and the Reveal action's full-path
target. The File menu retains Remember state but has no separate Save state
action. Layer context menus separate edit-target selection from saving.

The changes dialog uses a compact default size, a filling Status column and
interactive resizing on all five headers. State-only changes identify the root document.
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
explicitly polygonal, rather than inheriting USD's Catmull-Clark fallback. It also
checks preparation before a tool is selected, hidden initial visibility,
idempotence, and unchanged document USD. The disposable first-show benchmark
(`benchmark/benchmark_gizmo_first_show.py`) measures first and repeated tool
frames separately, verifies hidden/visible handle pixels and unchanged document
root/session layers, and records independent MCP oracles.
`testgizmostability.py` keeps Rotate active while creating a sphere and plane,
opens the Material Editor, and orbits through multiple camera revolutions and
distances, including repeated left/right sweeps through an edge-on red ring.
Option/Meta-left drags starting on each Move/Rotate/Scale centre must orbit
without authoring USD. Move is also swept across end-on axes with wireframe
overlay enabled. It checks unchanged handle topology, finite transforms, selection,
and rendered color bounds to detect spikes that only occur on the GPU. The
external MCP regression controller records independent stage/selection evidence.

`viewport_gizmo_clipping_api` checks perspective and orthographic gizmo bounds
outside the document clipping range, unchanged viewport framing, and rejection
behind the eye. The scene, grid and gizmo rendering changes are also exercised
by `testgizmostability.py` camera sweeps and an independent-clipping phase
that keeps all three tool modes visible and resizing outside the document
near/far range. Frame captures can mask
intermittent timing faults; passing screenshot checks alone does not prove that
all asynchronous rendering artifacts are absent.

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
menus through MCP. It verifies the divider below Select, followed by Make editable / Source,
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

`render_coincident_selection_api` checks either coincident drawable and native
instance proxy, parent selection, true occlusion, visibility changes and resized
selection buffers. It also verifies that unselected startup frames initialize
selection resources once and that first/next selections reuse them without
resource creation or another warmup. `python/testselectionoutline.py` checks the
same overlapping plane workflow in a disposable viewer, including command undo/redo, actual
outline pixels and Qt/viewer snapshots.

`render_display_mode_warmup_api` checks that shaded and wireframe-on-surface
pipelines prepare before the first switch, hidden preparation does not change
the presented shaded image, and quality/new-geometry changes prepare each mode
once while ordinary transform edits reuse them. The benchmark records
first/repeated wireframe switches separately and
saves a wireframe viewer/Qt snapshot for each scene size.

For a focused before/after performance comparison, run
`python3 CTest/benchmark/benchmark_selection_outline.py --app /path/to/stageviz
--output /new/result/directory` against each binary. Add `--baseline` only for
the old binary to record its known overlap failure. The default 10,000- and
30,000-object fixtures populate and expand the Outliner, warm each selection
mode, and record 60 redraw/readback samples, fixed-seed random selection changes,
and five parent transform/undo cycles. `--performance-first` measures the first
and next selections before the overlap workflow. Stage preparation and first
selection timings are recorded separately from warmed redraw samples. Use
`--parent-moves 0` to isolate redraw/selection timings when transform coverage
is already available. Optional `STAGEVIZ_PERF_LOG` traces distinguish resource
creation and selection warmup. Frame timings include GPU synchronization through
framebuffer readback; they are not GPU-only timings. Fixture creation, loading
and snapshots are excluded from redraw and selection timings. The external MCP
controller confirms the endpoint with `tools/list` and records independent
stage, selection and prim observations. Large-scene comparisons are opt-in;
the discoverable embedded suite runs the focused correctness checks only.

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
groups remain in the model and follow the Untyped filter, which starts unchecked.
Inactive typed prims, unloaded payloads, instances and proxies remain visible.

`new_xform_grouping_api` checks grouping a single selected prim, redundant
descendant selections, placement under transformed parents, selection and
undo/redo, plus explicit empty-child creation. The embedded `testapi.py` suite
also checks single-selection grouping through `new_xform`.
`testxformgrouping.py` drives Xform > New and Xform > Parent in the real Edit
and Outliner context menus with single and multiple selections. It checks empty
creation versus grouping, Parent enabled state, shortcut ownership, hierarchy,
selection, undo/redo and Qt snapshots in a disposable viewer. It also verifies
that Xform creation/grouping is available in Edit and omitted from Create,
with no shared submenu attachments or cycles in the menu graph.

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
  double-click replacement, scrub/text cursor transitions and shortcut ownership during numeric entry.
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
logical-pixel defaults. `ui_api` covers sizing notifications and invalid values,
and verifies that header hover cursors cover the widened grip, splitter/header
drags retain their resize cursor and restore
the previous cursor on release;
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

Outliner Opinions uses a disabled Clear header, then All followed by the property-opinion groups actually authored
in the active edit layer. `clear_opinion_groups_api` covers all eight groups,
recursive and overlapping selections, definition preservation, locked layers and
exact undo after an edit-target switch, and mapped variant targets.
`outliner_clear_opinions_menu_api` checks the offscreen menu labels, divider,
available groups and locked-layer state. `testclearopinions.py` checks the live
menu, grouped and All commands, undo/redo, retained metadata and source-layer
isolation through a disposable MCP viewer. Discovery visits authored edit-layer
specs rather than traversing the entire composed payload hierarchy.

Layer Editor multi-selection is covered by `session_layer_batch_api` (nested
layer removal, retained handles, mixed mute/permission states and one-step
undo/redo), `layer_tree_multi_selection_api` (selection retention and batch
context actions, active edit-layer icons and disabled mute eyes), and
`testlayermultiselection.py` through disposable MCP.
Batch remove/mute/permission operations share the single-layer command rules.

`session_state_dirty_api` checks camera/view dirtiness with coalesced notifications,
state-only writes without USD changes, failed writes, orthographic scale and AO,
payload-rule round trips, root pairing with an active sublayer, copy isolation,
and preservation disabled. The live `testpreservestate.py` suite checks root-save
menu availability, sublayer-save isolation, USD title dirtiness, restoration and
remembered-state autosaves through an external disposable MCP controller.

With Remember state enabled, companion state is paired with the root USD
filename, not the active edit layer. Saving the root document includes its
companion; saving an individual sublayer leaves root state pending. State-only
root saves preserve the root USD file's bytes and modification timestamp.
Remembered state autosaves on New, Reload and Close, including a failed companion
write preventing document replacement.
`preserve_state_menu_api` checks the save menu, root title behavior and state
autosave when a stage is replaced without a document prompt.

The disposable controller compares the state suite's filename, selection and
payload checkpoints against independent MCP queries. Anonymous-root Save/Cancel
and color-dialog tests use Qt's non-native backend only inside their disposable
process; production still uses system dialogs. These tests do not claim visual
coverage of the native OS file picker. Runtime preferences are redirected to
disposable INI storage before startup so testing does not change another running
Stageviz session's preferences.

The dedicated Changes dialog summarizes the root document and modified sublayers
in a five-column, scrollable table. `changes_dialog_api` checks
rows, headers, full-path tooltips, explicit Save all/Don't save/Cancel, Escape,
blocked entries and a 1,000-row list. `testchangesdialog.py` verifies the actual
Viewer flow for ordinary Save As without a Changes dialog, explicit Save all
review before choosing an anonymous root's filename, read-only/anonymous/session-layer entries,
discard and cancel. `testpreservestate.py` checks compact prompts for state-only
and single-root USD changes.

The Changes dialog is used when sublayers have unsaved changes, or when a layer
cannot be saved and needs an actionable diagnostic. It includes clean root and
ancestor layers as context without marking them as dirty. With root-layer USD
changes, New/Open/Reload/Close/Exit use the compact Save/Don't save/Cancel question.
Remembered state is saved automatically when replacing or closing a named stage
and does not mark the USD window title dirty; explicit Save all also writes it.
The active sublayer's dirty marker is also shown in parentheses in the window
title, independently of the overall document marker; the state suite verifies
that saving the sublayer clears its marker while root state remains dirty.
Ordinary Save layer and Save layer as do not open it. Save all offers Save and
Cancel; document-replacement prompts also offer Don't save. Accepting a review
does not cause a second Changes dialog after choosing an anonymous root filename.

The layer workflow also checks the initially missing root companion, confirms the
Save all review for dirty sublayers, and verifies that saving a sublayer leaves
remembered root state pending. The first anonymous-root save must refresh Save
layer after writing its companion, even without a state-dirty transition;
`preserve_state_menu_api` covers that notification edge case natively.

The multi-layer selection suite also checks that the Assign color dialog and its
OK button remain visible and enabled while its Layer Editor owner is modal-blocked,
then clicks OK and verifies the assigned colors through saved session state.

Native GUI cases isolate their QSettings in a temporary INI directory and keep
stdout/stderr visible in Release as well as Debug builds. Pure Qt popup action
cases run offscreen on macOS/Linux to avoid native focus dismissal; they do not
claim OS popup integration coverage. The asset-editor chooser test polls until
the Qt dialog is visible, rejects it on a bounded watchdog, and pumps events
while waiting for workers. Payload-checkbox fixtures disable Remember state so
LoadNone resets are independent of companion autosaving. The dedicated session
state tests separately verify current-state autosaving on Reload and explicit
restoration of older muted-layer/payload-rule snapshots.

On macOS the complete runner and disposable app controllers hold a scoped
`caffeinate -i` assertion to prevent idle system sleep during the run, including
native CTests. It ends on completion or interruption and watches the controller
PID so a crashed controller cannot leave it active. System power preferences
are unchanged; this does not prevent explicit sleep or closing the lid.

### Viewport color management

`stageviz.gui.render_color_management_api` compares the native GPU processor
against OCIO's CPU reference for every bundled sRGB display view, including HDR,
negative and partially transparent samples, exposure and viewing gamma.
`render_color_management_gl_api` exercises HgiGL explicitly on Windows/Linux;
macOS uses Metal because HgiGL requires OpenGL 4.5, beyond macOS support.
`testcolormanagement.py` checks the embedded `PyOpenColorIO` import, Display menu
structure and preset separators, bidirectional ViewState/Python/preset updates,
custom values with no checked preset, validation, remembered
state and selection outline colors. It requires a disposable app process and
writes viewer captures and a Qt snapshot. Depth/ID capture and coincident-outline
regressions remain separate focused checks.

The initial viewport OCIO configuration is the built-in
`studio-config-v2.2.0_aces-v1.3_ocio-v2.4`, with explicit Linear Rec.709 input and
`sRGB - Display` output. Un-tone-mapped is the default view. Raw bypasses OCIO
but keeps exposure/gamma adjustments. This does not change USD/MaterialX texture
input authoring or infer arbitrary source texture color spaces.

For a packaged application, run
`python3 CTest/benchmark/check_ocio_deployment.py --app <executable>`.
It verifies that the native Python extension is inside the app, creates an OCIO
processor and checks a reference gray value. On macOS it also verifies that
exactly one OCIO library is loaded, from inside the bundle. The macOS build
preserves versioned OCIO library symlinks so the executable and Python extension
resolve to that same library. The smoke test verifies OCIO availability and
imports; deployment checking stays generic and accepts the valid bare
`@loader_path` rpath. Sanitization removes external
build-directory rpaths, including Qt's build tree.

### Material swatch view changes

`stageviz.gui.material_swatch_color_view_api` verifies that floating-point linear
capture retains HDR values, cached CPU OCIO conversion matches GPU display
output, and view changes reuse shaderball renders. It checks transparent
backgrounds, repeated view round trips and independence from viewport exposure
and gamma. `testmaterialcolorview.py` exercises the open material editor and
lazy refresh after hiding/reopening it, with screenshots and optional render
trace assertions. Both cases use synthetic materials.

The material renderer keeps one linear image and one current display image per
cached swatch in a 128 MiB LRU cache. It applies the selected session view with
neutral exposure/gamma; texture-node image thumbnails retain their existing
OpenImageIO preview path. Browser images remain visible while replacements are
requested, and only visible/requested swatches are reprocessed after view changes.
Cached conversions use the same progressive queue as geometry renders, keeping
menu changes responsive. Queued conversions retain source requests rather than
extra floating-point images; LRU eviction falls back to a normal render.

### Empty material display filters

`stageviz.gui.material_browser_display_menu_api` covers unchecking every material
type or layer, reopening the menu, preserving an empty type filter across a
browser refresh, and re-enabling one or all options. It verifies matching row
visibility in icon, list and detail modes. An explicit empty selection shows no
materials; an inactive filter shows all materials.

`stageviz.gui.outliner_display_menu_api` checks the same empty-selection behavior
for Outliner layers and named prim types, independently of Untyped. It verifies
menu reopen, tree refresh, and re-enabling filters without losing the other
filter group's state. Ancestors remain visible when needed for matching children.

### Numeric arrow release

`stageviz.gui.numeric_editor_api` verifies that both spin arrows step normally
and stop repeating on release, including after dragging outside the control.
Dragging from either arrow scrubs the value using pointer displacement. The test
processes events past the style's repeat delay both during the scrub and after
release to verify that native stepping cannot compete with the drag or continue
afterward.

### Material input file reveal

`stageviz.gui.material_reveal_file_api` checks the platform-specific file reveal
action and preceding divider in material input context menus. It covers relative
assets authored in sublayers, current absolute values, and omission for missing,
empty, directory and non-asset values. It inspects the action target without
opening the operating system's file browser.

### Population-mask isolation

`stageviz.render_isolation_instances` verifies private render populations for
native and nested instances, independent nested selections sharing prototypes,
external proxy material bindings, scene lighting, live edits and material
rebinding. It checks that the source population and authored layer contents are
unchanged. `stageviz.render_isolation_ordinary_prims` checks the same population
path for meshes, geometric primitives, curves, points, transforms, scopes and
cameras, including mixed selections and reuse after clearing isolation.

The embedded `testisolationinstances` suite creates Xform instances through the
public commands and verifies actual captured geometry for instance roots, proxy
children, mixed ordinary/instanced selections, shared siblings, nested instance
selections and ancestor roots. It checks undo/redo and clearing isolation, saves
viewer screenshots and Qt snapshots, and rejects geometry outside the requested
mask. It also clicks a captured surface to verify that isolated instances remain
pickable. The suite repeats with ordinary copies instead of instances. Separate
setup/isolate/cleanup phases retain external MCP state evidence for both cases.

All isolation uses a private population-masked render stage sharing the
source layers. Scene light paths are cached between structural notices, and
material lookup caches are shared across each population update. It preserves
load rules, muted layers, lights and required shader dependencies without
authoring source visibility. The original stage and one masked stage keep
separate Hydra engines with a shared GPU backend. Clearing isolation restores
the original engine; reentering reuses the masked engine and stage. The cache is
bounded to those two engines rather than growing with the number of masks.
Masked engines reuse the full scene's display-mode preparation while its source
topology, shader dependencies and render settings remain compatible.
`stageviz.gui.render_display_mode_warmup_api` checks isolated shaded/wireframe
images, that mask changes do not repeat warm-up, and that geometry authored
while isolated refreshes preparation before it is displayed.

The opt-in performance comparison uses generated ordinary and native-instance
scenes with 2,000 and 10,000 meshes, two fresh app processes per case, an expanded
Outliner branch, deterministic leaf/group/multiple selections, parent moves and
undo, clearing and reentering isolation, and normal-frame samples. Run it before
and after a build, reusing the fixtures and baseline:

```sh
DYLD_IMAGE_SUFFIX=_debug python3 CTest/benchmark/pipeline/benchmark_isolation.py \
  --app build.debug/Debug/Stageviz.app/Contents/MacOS/stageviz \
  --output build.test-results/isolation-before
# Rebuild the changed application before the second command.
DYLD_IMAGE_SUFFIX=_debug python3 CTest/benchmark/pipeline/benchmark_isolation.py \
  --app build.debug/Debug/Stageviz.app/Contents/MacOS/stageviz \
  --output build.test-results/isolation-after \
  --fixtures build.test-results/isolation-before \
  --baseline build.test-results/isolation-before/summary.json
```

Timings include command completion and normal viewport framebuffer readback.
Object-ID captures verify isolated geometry outside the timed intervals; full
scene restoration checks framebuffer coverage. The controller records external
MCP state, Qt snapshots, screenshots, fixture hashes, app hashes, memory samples
and median timing comparisons. Source layer contents and population must remain
unchanged after the measured moves are undone. This benchmark is separate from
the automatic suites and must run without other GPU tests or builds competing
for resources. Add `--max-regression-percent 10` to enforce a comparison budget;
the default absolute noise allowance is 5 ms, adjustable with
`--regression-slack-ms`. Both limits must be exceeded to fail, and the comparison
report is saved before reporting a regression. Cold transitions have only two
samples per default case; repeated leaf and group switches have sixteen and
twelve respectively, so interpret small cold-operation differences cautiously.

### Instance menus with multiple selections

The `testmakeeditable` multi-menu phase checks Instance actions for multiple
roots and proxy children, including duplicate children from one instance.
Select and Source deduplicate their targets; Make editable converts every
selected instance in one undoable operation and retains the selected paths.
The menu places its divider immediately below Select, when Select is present.

The `testmakeeditable` source-availability phase keeps Source enabled for real
in-stage internal references and disables it for external-only sources, proxy
children of those instances, and mixed selections containing either case.
Asset-local paths cannot accidentally select an unrelated same-named stage prim.

### Native material assignment and clearing

`material_assignment_instances_api` checks direct parent bindings, proxy-to-owner
promotion, shared prototype preservation, variant edit targets, locked layers,
and exact undo in the original edit layer. `clearMaterial` removes native binding
relationships without custom markers, including bindings authored outside Assign
and purpose-specific bindings; unrelated opinions and composition remain intact.
Generic opinion clearing retains its existing override-only behavior.

`testmaterialassignment` checks rendered colors for ordinary, parent and proxy
assignments, whole-instance changes, Material Editor Assign/Select, viewport
material drops, and undo/redo. The Outliner and viewport context menu share
Material > Source / divider / Clear below Transform. The suite checks these
labels, Pivot > Center / divider / Reset, source selection, and Clear availability
when only a weaker binding remains. Captures pair Qt state with composed bindings
and visible object colors.

### Development test runtime paths

The automatic runner configures `DEPLOY_BUILD=OFF` alongside `BUILD_TESTING=ON`,
including Release. This resets deployment mode left by a previous packaging
build so embedded Python uses the same dependency prefix as the native app,
rather than loading stale bundled USD libraries. `stageviz.test_app_runner`
covers overriding a cached deployment configuration. `--no-build` leaves the
existing configuration and binaries unchanged.

Release embedded checks also wait for the numeric inspector's queued selection
refresh and locate the Color menu through its current `displayColor` object name.

### Grouped prim context menu

`testcontextmenu` checks the shared Outliner/viewport menu hierarchy, disabled
Show/Hide and Clear headers, the divider before Other, native material provenance
(inherited, stronger ancestor, instance owner and mixed owners), Source/Owner
navigation, generic relationship targets, visibility commands and relationship
clearing with exact undo/redo. External MCP prim and selection queries bracket
the phases. Generic relationship Clear removes current-target relationship specs;
it does not promote proxy properties or traverse ancestors. Material clearing
retains the explicit instance-owner promotion used by material assignment.

`relationship_opinions_api` covers complete relationship-spec removal, weaker
fallback targets, exact metadata restoration, original edit targets, variant path
mapping, locked layers and read-only proxies. The property inspector uses Clear
for values, dependencies and opinions. MaterialX Default Value remains a distinct
authoring operation because restoring a NodeDef default can write a value.

Python opinion-removal commands now use `clear_*` names: `clear_opinions`,
`clear_pivots`, `clear_attribute_values`, `clear_attribute_opinion`, `clear_attribute_opinions`,
`clear_dependencies`, `clear_shader_inputs`, and `clear_relationships`.
The duplicate transform-reset entry point is consolidated into
`identity_transforms`. Existing external scripts must migrate to these names;
the obsolete reset aliases are not retained.

### Outliner purpose display filter

`stageviz.gui.outliner_purpose_filter_api` checks the final Purpose category,
independent Default/Render/Proxy/Guide choices, explicit none, inherited purposes,
explicit default overrides, inheritance through non-imageable nodes and native
instance proxies. Filtering retains ancestors of matches, intersects with type
and text filters, follows purpose edits and undo, and preserves row identity.
All selected purposes bypass USD purpose computation during ordinary filtering.
Non-imageable prims remain governed by the existing layer/type/text controls.

`testoutlinerpurpose` exercises the actual menu through a disposable MCP app,
compares visible rows against USD's reference purpose computation, captures menus
and Qt/viewer snapshots, and checks selection and viewport capture paths remain
unchanged. Explicit none survives menu reopening and stage reload. Purpose
filtering authors no USD opinions and does not change viewport purpose visibility.

### Rich Outliner and material tooltips

`stageviz.gui.rich_tooltips_api` checks shared escaped rich-text formatting,
long-path wrapping without losing text, matching icon/list/detail material
presentations, prim name/type/path fields, direct and descendant override
summaries, material targets and binding strength, large-array summaries, refresh
after clearing a binding, and active edit-layer changes. Override details are
computed only when requested and use the existing authored override index;
property and descendant lists are bounded rather than scanning composed stages.

`testtooltips` drives actual Qt help events over Outliner names, override icons
and Material Editor swatches, captures the displayed tooltip windows, verifies
proxy status and binding clear/undo refresh, and stores Qt/viewer snapshots.
External MCP stage and prim inspection brackets the reusable synthetic fixture.

### Layer ownership and material rename

`rename_material_layer_parent` covers renaming a material whose definition is in
an active sublayer while its parent scope has a stronger root-layer opinion.
The same-parent rename retains the child's own strongest-layer validation and
USD namespace-editor checks; only the unnecessary parent-strength requirement
is relaxed. Tests cover binding updates, exact undo and unrelated/stronger child
layers remaining protected. `testmateriallayerrename` exercises Material Browser
rename, undo/redo, ownership tooltips and Layer Editor hover captures through a
disposable app. External MCP verifies the defining, strongest and active layers.

Prim and material tooltips distinguish the defining Layer from a different
Strongest opinion layer. Override tooltips also identify the definition layer,
while retaining the active override layer. Layer Editor names and status icons
use the shared formatted tooltip, including paths, edit target, permissions,
unsaved changes, opinion counts and reorder hints. Native MCP stage info exposes
`editLayer`; prim info adds `definingLayer`, `strongestLayer` and `editLayer` so
read-only inspection does not need an embedded Python execution.

### Independent material containers across layers

`material_container_layers` creates Preview Surface, MaterialX Standard Surface and
OpenPBR materials in a different layer from the original `/Materials` definition.
It checks that muting the original layer retains the new material and that undo
restores the exact previous container and metadata, including an existing `over`.
`testmaterialcontainer` exercises the Material Browser creation signal, undo/redo,
layer mute/unmute and both browser and Outliner presentation in a disposable app.
The external MCP controller independently checks the material's defining layer.

### Material composition and inert opinion cleanup

`testmaterialcomposition` constructs a shared material path with a universal
Preview Surface output in one layer and a MaterialX output in another, plus
nested empty root-layer overs. It compares live mute/unmute transitions with
independently rendered compositions, samples the browser and large preview,
checks that displayed property inputs belong to existing composed shader nodes,
checks material removal/restoration, reloads a different document at the same
material path, and records switching time and Qt event-loop gaps. Timings include
an explicit 700 ms settling interval; they are diagnostic rather than a universal
performance threshold. External MCP inspection verifies composed prim presence.
`clear_inert_material_opinions` verifies Clear All removes empty over chains,
restores exact specs and sibling order on undo, and preserves metadata and the
underlying definitions. `testclearopinions` continues to cover grouped property
clears and the real Opinions menu.

The material renderer retains content-keyed composition images within the same
128 MiB LRU budget. `material_swatch_color_view_api` checks that repeated authored
compositions reuse their linear image, new contents render once, explicit network
invalidation flushes variants, and transient edits do not reuse an old image.

### Explicit overrides and material network replacement

`explicit_override_paths_api` checks authored overs without duplicate prims,
original-target undo/redo and rejection of invalid batches.
`replace_material_network_api` checks replacement with Preview Surface,
Standard Surface and OpenPBR, competing universal/MaterialX terminals, fresh
shader node paths, preserved bindings/metadata, stronger-opinion guards and
exact local-layer restoration. `testlayerauthoring` drives the actual Outliner
and Material Browser menus, both Python commands, swatch refresh and undo/redo
without muting any layer. The external controller checks composed material
presence and selection through named MCP tools.

`layer_authoring_variants_api` covers mapped variant targets, exact original-target
undo/redo, referenced material replacement, rejection of stronger direct layer
opinions inside variants, locked layers, material instances and proxy materials.

### Current scene clipping bounds after edits

`camera_live_scene_clipping_api` reproduces a far plane fitted to stale framing
bounds and verifies current scene bounds across a complete orbit in all three
projection modes, without changing the view pose or stored framing bounds.
`session_geometry_bounds_notifications_api` checks transform and radius changes
notify render views. `testcameraclipping` moves a sphere outside the original
bounds, switches from Front to a leftward tumble, verifies both sphere IDs stay
visible, and checks undo/redo and unchanged focus/zoom. The external MCP controller
independently verifies that the source prims remain present.

### Material graph layout with shared sources

`material_graph_shared_branch_layout_api` checks branching networks whose texture
nodes share a UV source, including unequal branch lengths and different node
heights. It verifies that node bodies do not overlap, shared sources remain left
of their consumers, network roots align on the right, and supported networks
appear in blocks above unknown networks. It also checks finite placement for
cycles, deterministic automatic rebuilds, preservation of manual positions during
refresh, and unchanged USD layer contents. The disconnected-network layout case
checks the same root alignment for separate networks.

### Confirmed layer creation replacement

`create_sublayer_replace_api` verifies replacement of existing `.usd`, `.usda`
and `.usdc` files, including destinations already loaded into the stage. Undo
restores prior in-memory layer contents and parent sublayer arcs; redo restores
the empty destination without writing the file again. The disk file remains the
confirmed replacement. Creating a sublayer cannot replace its own parent or the
stage root. `layer_create_replace_dialog_api` drives the Qt save chooser and its
Replace confirmation, checks the default `.usda` suffix, and verifies replacement,
attachment, and one-step undo/redo. This chooser test uses Qt's non-native dialog.

### Display filter indicators

The Outliner Display button highlights deviations from its default settings.
The Material Browser Display button remains checked while filters restrict
content, including when all rows are hidden or newly created materials have an
excluded shader type. Both menus remain available from either
left click or the button's context menu. `material_browser_display_menu_api`
checks the active indicator during and after popup interaction, newly added
hidden materials, and clearing filters through a context-opened menu.
`outliner_purpose_filter_api` checks purpose and Untyped restrictions, updates
through the tree's filter setters, and context-menu highlighting. The Outliner's default Untyped exclusion does not activate the indicator;
showing Untyped highlights a departure from the default display settings.

### Unused flattened instance sources

`flattened_instance_cleanup_api`, `flattened_instance_dependencies_api` and
`flattened_instance_nested_cleanup_api` cover Delete cleanup of exported USD
instance sources, shared and nested users, inactive variants, muted layers,
relationships, connections, payloads, inherits and specializes, plus undo/redo.
Only source roots associated with the deleted instances in the edit layer are
eligible; unrelated untyped roots are retained. Missing composition assets skip
cleanup conservatively. `testinstancecleanup` exports and reopens actual USD
instances, checks the last deletion and undo/redo, saves empty USDA/USDC copies,
and captures Qt/viewer state with independent external MCP prim-list checks.

### Material render-context changes in the viewport

`render_material_context_invalidation_api` verifies targeted material-root resyncs
without reintroducing deleted materials or resyncing ordinary geometry. `testmaterialviewportcomposition` compares viewport pixels against
fresh Preview Surface and MaterialX renders, checks swatch colors, repeatedly
mutes/unmutes a stronger layer containing material overs, and exercises undo/redo
and isolation renderer caching. An external MCP
controller independently verifies the material's strongest layer at checkpoints.

### Viewport purpose display

`viewport_purpose_state_api` checks native purpose defaults, change signals and
view-state persistence. `testviewportpurpose` exercises the matching sidebar
Display button with left-click and context menus, filter indication, inherited
purposes, rendered/captured object paths, independent isolation masks and saved
view state. An external MCP check verifies filtered geometry remains composed.
Default-purpose geometry stays included by USD's native rendering controls.

### Material assignment to packaged face subsets

`material_assignment_subsets_api` verifies that assigning a whole mesh overrides
its face-subset materials through native binding strength, preserves original
subset relationships, and restores them through Clear and undo/redo.
`testpackagedmaterialassignment` checks the same workflow through a generated
USDZ reference, rendered colors, package-layer menu labels and identifier tooltips.
Material Editor and Outliner source filters share a Sources submenu containing
nonempty Layers, Referenced assets and Packages groups. Packaged contributors
appear once beneath their package; native menu checks cover empty-group omission
and filtering through nested actions. The packaged live test checks both menus.
It also drives `Assign > Face subsets`, verifies the displayed resolved material
for each subset, and checks rendered subset changes, sibling preservation, and
exact one-step undo/redo of the mesh override. The native case covers multiple
selected subsets and a stronger edit layer over an unchanged source mesh binding.

`material_follow_selection_api` checks the Material Editor's Follow button,
default-off state, direct-material selection, scrolling in all three browser
views and synchronization when reopening. The packaged live test drives Select
on a subset's material binding in the Property Tree and verifies editor selection
and visible scrolling with Follow enabled.
Follow responds only to explicitly selected material prims. Selecting geometry,
groups or subsets preserves the editor selection and never adds their bound
materials to the stage SelectionList; native checks cover each boundary.

`cross_layer_child_creation_api` checks new prims, populated geometry, lights,
materials, references and payloads beneath root-owned and referenced parents in
another edit layer. It verifies exact undo/redo after switching edit targets,
unchanged source layers, instance rejection, and unchanged rename restrictions.
`testcrosslayercreation` drives Outliner Create > Xform beneath a stronger
root-owned /World and checks the visible child, layer specs and undo/redo through
an external MCP controller.

`testmaterialxpreview.py` clones an imported `ND_UsdPreviewSurface_surfaceshader`
with a same-named `carpaint` material and shader. It checks shaded browser and
property swatches before/after a real numeric scrub, transient USD preservation,
commit, undo/redo and reopening. `material_utils_api` exports the colliding names,
checks the preserved material name, and resolves the imported material-interface
connection with USD's value-producing-attribute API to verify the roundtrip color.

`material_dialog_refresh_after_layer_removal_api` waits for the actual debounced
browser refresh and graph-tab deletion, verifies composed layer removal, and
checks browser restoration/removal on undo/redo. The viewport test helper waits
for visible handles and completed renders before and after camera framing;
prewarmed hidden meshes cannot satisfy gesture readiness. The shared-source
fixture explicitly checks hidden handles in Select mode.
