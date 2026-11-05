# Stageviz interaction utilities — iteration 6

Shared indexed path operations remove the measured quadratic inversion and
transform-setup loops. Safe Sdf batching fixes the failed bulk drag. Synchronous
bounds invalidation permits cache reuse without stale results. Exact spec
deletion reduces bulk undo overhead. The app still has substantial costs when
committing or undoing tens of thousands of transforms.

## Measured seconds

Same first Volvo dataset, fresh disposable processes, same 64 MiB Storm staging
setting and eager topology GC, same camera/sweep rectangle and populated
Outliner. Every completed checkpoint was checked through independent native MCP
queries. These are individual runs, not statistical averages. OS/network/shader
caches were not cleared. The baseline already includes the earlier USD staging
and Stageviz fixes; this is not a fresh vanilla-USD comparison.

| Operation | Before | Optimized interaction build |
| --- | ---: | ---: |
| Load and frame all | 101.86 | 106.54 |
| Cold sweep | 9.48 | 8.25 |
| Warm sweep | 1.95 | 1.97 |
| Invert selection | 182.94 | 10.73 |
| Show Move gizmo | 17.69 | 2.99 |

The sweep selects **49,296 identical paths in identical order**; inversion
returns **366,874 identical paths in identical order**. Native inversion-worker
time falls from 172.02 to
0.301 seconds. The
remaining inversion time is selection/UI presentation and rendering.

The response timestamps above wait for Session completion and several rendered
frames. A deferred statistics timer can still run afterward: inversion's
additional statistics work is 2.18
seconds, recorded separately as `deferred_scene_statistics_s` in the raw
measurements, rather than silently
counting the frame timestamp as complete UI quiescence.

The baseline bulk press failed after 55.4 seconds in its native handler. The
final build successfully resolves **39,810 transform targets**:

| Bulk operation | Final seconds |
| --- | ---: |
| Press, native handler | 2.49 |
| Press, through completed frames | 11.84 |
| Drag update 1 | 1.81 |
| Drag update 2 | 1.87 |
| Release | 16.09 |
| Undo | 40.94 |
| Redo | 36.59 |

The first indexed implementation's bulk undo was 60.48
seconds end to end and 34.80 seconds in
dispatch. The final version takes 40.94 and
17.36 seconds respectively. Both restore the
exact anonymous-root text and sampled world matrices.

The existing-matrix parent control isolates ordinary dragging from bulk work:

| Parent operation | Before | Final seconds |
| --- | ---: | ---: |
| Drag update 1 | 0.21 | 0.21 |
| Drag update 2 | 0.18 | 0.18 |
| Release | 11.71 | 10.46 |
| Undo | 10.30 | 9.51 |
| Redo | 11.90 | 9.72 |

These drag updates include completion polling and multiple frames; they are
not sustained FPS measurements. Native scopes are inclusive CPU wall timings,
not hardware GPU timers, and must not be added together.

## Implementation

* `usdutils`: shared `PathIndex`, `PathRelations`, `PathCollector`, stable/sorted
  root reduction, and typed Sdf attribute creation. Existing ordering and
  empty/property-path contracts remain explicit.
* Selection/transform/payload commands: prepared coverage, membership and
  duplicate indexes replace repeated list scans. Transform capture shares one
  USD transform cache, and pivot setup gathers visible selection once.
* Transform/property authoring: preflight USD queries, mutate Sdf specs inside
  change blocks, then obtain fresh composed handles. Mapped variant roots and
  children remain inside their edit-target namespace.
* Undo: remove exact specs through their Sdf owners and prune only recorded new
  inert ancestor overs. Non-prim ownership retains namespace-edit fallbacks.
* Bounds/statistics: synchronous USD-notice invalidation, lazy cache clearing,
  safe reuse of valid bounds, and topology-count retention for transform-only
  batches and their empty-field prim-stack refreshes.
* Outliner/property editor/isolation: shared root, overlap, and collection
  utilities replace duplicate implementations. Bulk diagnostics retain a
  bounded prefix and omitted-error count.

The timing review caught a statistics regression in the intermediate builds:
warm-sweep statistics rose from about 0.38 to 0.64 seconds. The final
`sortedRootPaths` utility preserves the original sort-and-prefix algorithm
instead of using the more expensive depth-ordered reducer for that caller.
See `audit.md` and `audit-inventory.json` for scope and retained loops.

## Verification and deployment

Interaction timings were collected on `5d82e40` (the sorted-root interaction
build). The final Release additionally fixes mapped property-batch capture,
authoring and undo; those commands are outside the measured transform scenarios
and received separate native and public Python variant regressions. Executable
hashes are retained per run and in the final deployment manifest.

* All **84 native/GUI CTest cases pass** (66 native, 18 GUI), including new
  100,000-entry path, composed transform, variant-owner, bounds-before-Qt,
  Sdf deletion-equivalence, and batched attribute tests.
* Public Python Move/Rotate/Scale, conflicting-input rejection, and 128-object
  composed transform tests pass with exact undo/redo, source and selection
  assertions, Qt snapshots, screenshots, and external MCP observations. Variant
  root/child attribute add, rename, removal and undo/redo are checked too.
* Fixture and Volvo sequences verify authored state, exact root restoration,
  clean file-backed layers, unchanged source metadata, and native MCP selection
  and stage/layer state. Temporary dataset edits are never saved.
* Release executable: `C:/git/github/stageviz/deploy.Release/Stageviz.exe`.
  All 72 deployed USD DLLs match the installed Release SDK. There are no new
  USD SDK source/DLL changes in this iteration. Previous SDK patches are backed
  up alongside the Stageviz sources in the sixth investigation archive.
* Normal launches do not write profiling output. Logging requires explicitly
  setting `STAGEVIZ_PERF_LOG` (and SDK tracing has its own opt-in settings).

## Remaining limits

Bulk release/undo still include expensive Sdf/USD notice publication, complete
scene bounds/statistics refreshes when composition changes, and Hydra
synchronization. The parent release alone includes
9.10 seconds in scene
bounds. These measurements do not establish a fix for those internal costs.
Cold BVH construction and initial loading are largely unchanged.

Inversion retains its existing ordinary-leaf namespace domain; it is not
redefined as the visual complement of partial instance-proxy/subset selections.
The two-layer mute workflow was not retimed in this iteration. A compilation
overlap was recorded during one intermediate cleanup phase; cleanup durations
are excluded from interaction comparisons.

## Reproduction and archived evidence

Run `run.py --settings sorted-profiles.json` with the repository SDK Python and
an available Stageviz MCP port. Use a disposable process and keep the Windows
OpenGL session available. `tests.py '^stageviz\.'` runs registered native/GUI
tests with the matching SDK runtime. `python-tests.py` owns its disposable app.
`analyse.py` creates the per-checkpoint JSON/CSV, and `finalize.py` checks and
archives source, binary, DLL and oracle evidence.
The archive includes the iteration patch against `f991278` and a complete
Stageviz patch/source set against `2ccb0cb`, carrying the earlier fixes forward
for future builds. Its file lists and SHA256 manifest identify every backup.

Baseline run: `20261104T213829Z-00-64MiB-volvo-sweep-move-baseline`. Final run: `20261104T234149Z-01-64MiB-volvo-sorted-roots`.
Parent baseline: `20261104T214948Z-00-64MiB-volvo-existing-transform-parent-control`. Final parent: `20261104T235302Z-02-64MiB-volvo-parent-sorted-roots`.
The raw `measurements.json` records executable SHA256 and run state for every
row; `measurement-notes.json` records any excluded timing circumstances.
