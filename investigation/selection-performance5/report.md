# Iteration 5: Volvo selection, invert and transform diagnosis

Two application-side problems were identified: expensive selection inversion,
and premature validation of newly created USD transform attributes inside a
change block. No performance algorithm was changed in this iteration.

## Measured inversion cost

Dataset: the user's `LHD_Front.usd`, composed as a sublayer of a disposable
anonymous stage. It contains 560,704 active prims and 407,320 selection-domain
leaves under the existing invert command's rules. Controls use a fixed seed
(`20261104`) to select leaves. They are CPU/UI controls, not actual screen-sweep
selections or rendered-completion measurements.

| Initially selected paths | Invert completion | Native matching worker, inclusive | Build candidate domain | Resulting selection |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 8.60 s | 0.540 s | 0.155 s | 407,220 |
| 1,000 | 13.98 s | 5.292 s | 0.152 s | 406,320 |
| 10,000 | 103.10 s | 95.439 s | 0.154 s | 397,320 |

Each count is a single fresh-process observation, with OS/network caches
retained. These controls ran without an initialized OpenGL renderer. The larger
delay therefore exists independently of Storm rendering. CPU scope times are
nested/inclusive and must not be added to their parent scope or elapsed span.
End-to-end control timing also includes Python readiness/verification work.

`selectInvert()` enumerates the candidate domain, then calls
`path::isCoveredByRoots(previousSelection, path)` for every candidate. That helper
loops through the selected paths and checks equality/prefix coverage. For a
10,000-path selection this exposes up to roughly 4.07 billion candidate/selection
comparisons. Candidate enumeration itself took only 0.15 seconds; most of the
large run was matching. Selection callbacks and scene-statistics updates add
further latency after matching finishes.

The first recommended fix is to build a lookup of selected roots once and walk
each candidate's ancestors, preserving coverage semantics, mask behavior and
output order. That replaces repeated selection-wide scans with work proportional
to path depth. It belongs in Stageviz's selection implementation; no USD core
change is required for that loop. It has not been implemented or benchmarked yet.

The 100/1,000-path runs preceded a refinement of the UI scopes. Their total
`selection.update_paths` measurements remain usable (5.11/5.67 seconds), but an
early `ui.outliner_selection` scope was attached to a getter, not its update
callback. The analyzer marks that UI split unavailable. Later builds instrument
Outliner, Property Editor and viewport selection callbacks separately.

## Transform preparation defect

The small grid fixture successfully exercised a native free-translation handle
(axis 11), two movement events, release, undo and redo. Sampled world matrices
changed and were restored; the final disposable-root contents were restored.

A separate 1,000-visible-geometry Volvo control reached the native gizmo frame
preparation and exact projected pivot. Native measurements included:

- Selection pivot computation: approximately 0.212 seconds.
- Operation capture: approximately 0.179 seconds, including target resolution.
- Transform authoring preparation: approximately 0.106 seconds.
- Drag-start handler: approximately 9.08 seconds, **then failure and rollback**.

The failed handler reported `failed to create matrix transform override` for
the selected targets. This was not a successful drag and supplies no rendered
drag-frame timing. A previous mixed-leaf control included non-geometry leaves
and was also rejected; it is excluded from geometry-performance conclusions.

The targeted standalone probe recreated the attribute issue without Qt or
rendering. It composed the actual Volvo file below an anonymous root, explicitly
used that root as edit target, and disabled editing on every file-backed layer.
The sampled mesh prims were not instance proxies and had no existing
`xformOp:transform` attribute. Creating that attribute under `Sdf.ChangeBlock`
produced this observation:

| Query point | Attribute valid | Type |
| --- | --- | --- |
| Inside the change block | false | empty |
| After the change block closes | true | `matrix4d` |

`prepareTransforms()` currently creates the USD attribute and immediately tests
its validity/type inside the same `SdfChangeBlock`. On this composed asset that
checks stale/deferred composition state and rejects a property that becomes
valid when the block closes. It then restores captured transform state and
refreshes presentation, accounting for additional delay and failed interaction.

The proposed fix is to resolve existing USD state before the block, perform
batched authoring through Sdf, close the block, then obtain and validate fresh USD
attributes/ops for dragging. This is a Stageviz use-of-USD issue. It has not been
fixed here, and the probe is not a claim about every possible transform failure.
Evidence is in `selection5-transform-types.log` and the failed run's native log.

## Undo observation

The grid fixture's undo restored sampled world matrices and authored property
values, but left empty ancestor `over` specs in the anonymous layer. An exact
root-text comparison caught this. The test now reports exact text restoration
separately from restoration of meaningful authored values and sampled transforms.
It does not silently count the inert specs as an exact restore. Final cleanup
recreates the disposable assembly when needed and asserts exact root text,
cleared selection/history, unchanged source metadata and independent MCP state.
The over-spec cleanup behavior remains a separate issue to address.

## Initial graphics boundary

Windows reported session 2 as disconnected. The tiny fixture's OpenGL context
was invalid (requested/reported default version 2.0). Qt's independent `qtdiag`
also reported `Unable to create an Open GL context.` This establishes a graphics
environment limitation independently of the CAD file or the new timings.

At that point, no valid cold/warm screen-sweep, visible gizmo-appearance, or
rendered dragging timings had been collected. Screenshots from CPU controls verify
Qt selection/presentation and counts; the blank GL area is not evidence of
missing CAD geometry. Reconnect an interactive graphics-capable Windows desktop
and run the full profile to measure these remaining phases. The connected-session
follow-up below records the subsequent retry.

The full profile has scopes for sweep BVH build/query, visible selection,
transform target/source resolution, pivot computation, operation capture,
authoring preparation, drag updates/end, gizmo geometry, Hydra selection mapping,
and the surrounding render/UI work. The real viewport handlers receive Qt input
events and expose optional diagnostic properties for handle position, active
axis, dragging state and target count.

## What changed

This iteration adds optional profiling scopes to:

```text
sources/commands/commandselection.cpp
sources/commands/commandtransform.cpp
sources/core/selectionlist.cpp
sources/usd/primutils.cpp
sources/outliner/outlinertree.cpp
sources/properties/propertytree.cpp
sources/rendering/viewportselection.cpp
sources/rendering/viewporttransform.cpp
sources/rendering/viewportgizmo.cpp
sources/rendering/viewportwidget.cpp
sources/rendering/renderengine.cpp
```

`viewporttransform.cpp` also publishes diagnostic properties only when
`STAGEVIZ_PERF_LOG` is set. The transform-frame, target selection, authoring and
undo algorithms remain unchanged. The shared Storm benchmark gained an autorun
guard, an overridable readiness check for intentional deferred drag state, and
in-place Windows status updates that tolerate a concurrent external reader.
The new selection benchmark, profiles, controller, fixture, probes and analyzer
are preserved with source snapshots. There are no OpenUSD SDK source changes
in iteration 5. The earlier 64 MiB staging fix remains in the SDK.

## Verification

- Release profiling builds and deployment completed; normal launches keep
  logging disabled unless the diagnostic environment variables are set.
- Three Volvo invert controls completed, checked the exact inversion complement,
  undid it, reloaded the original file and restored baseline state.
- Small CPU fixture controls exercised inversion and real transform events,
  undo/redo and exact final cleanup. The final benchmark version also captures
  populated Outliner Qt snapshots plus independent MCP selection/stage reads.
- The Volvo CPU drag attempt and graphics-invalid sweep attempts are explicitly
  failed/excluded, not passed performance runs. Earlier harness failures are
  retained as diagnostics and excluded from timing comparisons.
- No embedded synchronous MCP callbacks or Python USD notice callbacks are used.
- No shared dataset was saved. Completed checkpoints assert that file-backed
  layers are not dirty; source size/mtime and MCP stage/selection state are
  independently verified by the finalizer.
- The initial hardware OpenGL test was blocked; see the connected-session
  follow-up below. An optimized before/after comparison remains outstanding.
  No commit or upstream PR was made.

## Connected-session follow-up

After the user restored Remote Desktop, the viewport obtained a valid NVIDIA
OpenGL 4.6 context. The complete 100-cube fixture passed actual rectangle selection,
inversion, gizmo activation, translation, undo/redo and exact final cleanup.

The actual Volvo screen sweep selected 49,296 canonical paths. With the same
selection restored between operations:

| Operation | End-to-end response |
| --- | ---: |
| Load and frame the front dataset | 101.86 s |
| First screen sweep | 9.48 s |
| Clear selection | 0.13 s |
| Repeat the identical sweep | 1.95 s |
| Invert the actual sweep selection | 182.94 s |
| Undo inversion | 3.10 s |
| Activate Move and present the gizmo | 17.69 s |
| Press the bulk Move handle | 55.42 s in the native handler, then failure |

Cold sweep-index construction took 4.38 seconds. Querying the built index took
0.061 seconds initially and 0.064 seconds on the repeated sweep. Thus index
construction and selection/presentation work account for the first-sweep delay;
the spatial query itself is a small fraction. The timings label the index cold,
not the operating-system/network/shader caches.

The inversion's native worker took 172.02 seconds. Candidate enumeration was
0.154 seconds. The inversion returned 366,874 paths under the existing command's
canonical namespace-coverage rules. It was measured on the actual screen-sweep
result, in addition to the earlier deterministic CPU controls.

An independent USD process composed the original file with a disposable `/World`
root and verified the complete 366,874-path result against the current command's
ancestor-coverage rules. All file-backed layers were locked against edits in that
verifier. The screen sweep included 8,850 instance-proxy paths. Inversion selected
332 instance roots containing some of those previously selected proxies. The
current command uses ordinary stage leaves while the sweep includes proxies;
matching its existing rules is not proof of a strict visual geometry complement.
That selection-domain mismatch should be reviewed alongside the performance fix.

Move activation spent 16.76 seconds computing the selection frame, including
11.33 seconds resolving editable transform targets and two visible-selection
passes totaling about 1.84 seconds. The target resolver repeatedly compares
paths against previously accumulated targets to remove duplicates and covered
descendants. This supplies another concrete application-side optimization target:
indexed root/ancestor lookup, with reuse of resolution for an unchanged selection.
Bounds computation is another component; these are inclusive nested timings.

Bulk drag start repeated target resolution (11.07 seconds). Operation capture
took 20.26 seconds inclusively, and authoring preparation took 10.51 seconds.
The 55.42-second handler then reported the previously reproduced matrix-override
validation errors and restored the attempted operation. No successful bulk-drag
frame timing can be claimed from this failed attempt. This failure now reproduces
with working graphics. The local OpenUSD `pxr/usd/sdf/changeBlock.h` explicitly
documents that downstream USD queries must occur after the block closes, because
dependent representations have not yet updated.

One initial CAD retry was excluded because an unbounded Qt snapshot recursively
walked the entire Outliner after the load timing had ended, causing an MCP
checkpoint timeout. The capture now records at most 256 rows, 24 per parent,
through six expanded levels, using model indices and plain values. The bounded
fixture passed before retrying the CAD run. This was a benchmark capture cost.

The bulk run exited with code 0 without controller termination. The controller
preserves completed phase oracles and the failed drag-start diagnostics separately.
All edits remained in the anonymous root; the shared dataset was not saved.

### Successful existing-transform parent control

To separate new-attribute creation from rendered dragging, another fresh process
loaded the same complete Volvo stage and selected a small parent with an already
authored `matrix4d` transform. Its bounds and pivot were framed before testing.
The native free-translation handle, active-drag state, sampled world transforms,
undo/redo and final cleanup were verified. This control does not represent the
49,296-path bulk selection.

| Operation | End-to-end response | Bounds work | Statistics work | Native render scope total |
| --- | ---: | ---: | ---: | ---: |
| Move tool activation | 0.30 s | — | — | — |
| Handle press | 1.27 s | — | — | 1.05 s |
| First isolated move update | 0.21 s | 0 | 0 | 0.021 s |
| Second isolated move update | 0.18 s | 0 | 0 | 0.029 s |
| Release | 11.71 s | 8.28 s | 2.23 s | 0.018 s |
| Undo | 10.30 s | 7.55 s | 2.26 s | 0.038 s |
| Redo | 11.90 s | 8.20 s | 2.37 s | 0.036 s |

Applying the prepared transform during each move took about 0.11 ms. End-to-end
move timing waits for multiple presented frames and readiness polling; it is not
a sustained-drag FPS measurement. Native rendering scopes are CPU wall times,
not hardware GPU query times. This is a single-run diagnostic control.

The release/undo/redo cost is mostly outside native rendering. Inspection matches
the timings: `SessionPrivate::boundingBox()` clears the shared bounds cache and
computes the pseudo-root bound when dirty; `ViewportWidgetPrivate::updatePrims()`
unconditionally marks scene totals dirty, so `updateSceneStats()` traverses the
whole stage again. Transform-only updates should preserve geometry-count caches.
Bounds should be updated incrementally or recomputed when actually required,
with correct invalidation for changed ancestors and instance relationships.

The parent control completed every phase, reloaded/recreated the disposable
assembly to restore exact root contents, and exited with code 0 without controller
termination. Original source file metadata and independent MCP checkpoint state
are checked by the finalizer. Full-window screenshots show actual geometry,
selection outlines and the Move gizmo with the restored OpenGL context.

The concrete fix priorities from these measurements are:

1. Replace selection-wide prefix scans in inversion with indexed ancestor lookup.
2. Remove repeated target-set scans and reuse valid selection/target resolution.
3. Move USD queries/validation outside Sdf change blocks in transform preparation.
4. Avoid full scene-bounds/statistics recomputation for every transform completion.

These are Stageviz changes. The earlier internal USD scene-index/prototype
profiling remains relevant to layer composition updates, but it does not replace
these now-measured application-side fixes. None of these algorithmic fixes has
been applied in this test-only follow-up.
