# Large-scene sweep and transform benchmark

This creates 250,000 independent mesh prims, each with 80 quads: 20 million
**authored quad faces**, potentially 40 million raster triangles. Another 251
Xforms organize meshes into 250 branches of at most 1,000 meshes. Each mesh has
an existing matrix transform and authored extent. Geometry values can be
file-deduplicated, but prims are not USD instances. There are no materials.

Use only disposable Stageviz processes. The controller creates an anonymous
edit root above the generated fixture; it never saves temporary edits into the
fixture. It verifies the original file hash afterward. The active user app and
its MCP listener are preserved.

## Build and generate

For macOS standalone Release comparisons, build both revisions using the same
SDK and the project's packaging command:

```sh
DYLD_IMAGE_SUFFIX=_debug THIRDPARTY_DIR=/path/to/arm64.release \
  ./build_app.sh release --github
```

The generator runs in a Python environment with the matching `pxr` package:

```sh
python generate_scene.py /absolute/output/scene-250k.usdc
```

Use `--meshes 1000` for an initial harness smoke test. A manifest records the
actual prim/face counts and fixture SHA256. Large fixtures and measured outputs
belong outside the source tree.

## Baseline timing probes

The interaction branch already contains optional `PerfScope` instrumentation.
To compare master with the same probes, use an isolated checkout/worktree:

```sh
python instrument_baseline.py /absolute/master-worktree /absolute/interaction-worktree
```

This adds timing scopes and opt-in dynamic gizmo-state properties. It does not
backport path indexes, batching fixes, bounds changes or rendering behavior.
Archive that checkout's diff and copied `perfscope.h` with the results. Build
it with the same configuration and SDK as the interaction branch.

## Run serially

Do not run other benchmarks or builds during measurements. Use the identical
fixture and application display for both runs:

```sh
python controller.py --app /absolute/master/Stageviz.app/Contents/MacOS/stageviz \
  --label master --revision BASE_SHA --build Release \
  --fixture /absolute/output/scene-250k.usdc --output /absolute/output/master
python controller.py --app /absolute/current/Stageviz.app/Contents/MacOS/stageviz \
  --label interaction --revision BRANCH_SHA --build Release \
  --fixture /absolute/output/scene-250k.usdc --output /absolute/output/interaction
python analyze.py /absolute/output/master /absolute/output/interaction \
  /absolute/output/comparison.md
```

The output directory must not already exist. The controller owns a dedicated
native MCP listener on port 37657, validates `tools/list`, then uses read-only
stage/selection/prim tools as an independent oracle at every checkpoint. The
embedded Qt timer never calls MCP. An RSS watchdog stops only the owned app if
it exceeds `--max-rss-mib` (24 GiB by default).

The run uses a fixed 1500x1000 viewer, normalized sweep rectangle, camera,
scene statistics enabled, materials disabled and GPU frustum culling disabled.
A bounded Outliner branch is populated, not every scene row. Captures record
actual viewport size and DPR; comparison rejects mismatched viewports or
selection identities/order.

Phases cover load/frame, one cold BVH sweep, three warm sweeps, Move gizmo
activation, bulk press, five drag steps, release, undo, redo, restoration, then
parent move/undo/redo and cleanup. Assertions verify stage/face counts, sampled
world matrices, selection preservation, no dirty source layers, and restoration.
Older master can leave inert ancestor overs after undo. That difference is
reported as `root_restored=false`; only inert overs are tolerated when checking
otherwise exact authored restoration.

## Interpret the timings

- Dispatch: synchronous input/action handler duration.
- First present: first viewport presentation after dispatch.
- Response: Session completion and at least three presentations.
- End-to-end: response plus an explicit 750ms event-loop settle floor so
  deferred UI/statistics work is not hidden between checkpoints.
- Native phases: optional inclusive CPU wall times. Nested phases must not be
  added together; these are not hardware GPU timings.

Screenshots, Qt snapshots, USD assertions and external MCP oracles occur after
the measured span. Profiling output, resource samples, executable/script hashes
and fixture metadata are retained per run. Warm repeats within one process are
not independent fresh-load samples. OS caches are not cleared. Discrete drag
step timings do not measure sustained FPS, and this synthetic scene does not
establish performance on a textured production assembly.

For a focused parent control on the same full-size scene, add
`--parent-repeats 3`. This runs load/frame followed by three parent
press/drag/release/undo/redo/restoration cycles without first performing the
bulk selection workflow. Every phase retains its own external MCP checkpoint.

Add `--active-sweep-repeats 3` to repeat the same sweep three more times with
the Move tool active. This distinguishes selection processing for a visible
transform panel from sweeps with the tool disabled. Selection identities and
ordering must still match the original cold sweep.

The harness explicitly gives the Outliner 240 logical pixels and collapses the
Property Editor to avoid inherited splitter settings. For comparisons with a
previous run, `--viewport-size WIDTH HEIGHT` checks the actual GL widget against that
run's recorded logical size. The analyzer rejects viewport or selection mismatches.

Use `--sweep-only` for a confirmation run of load/frame, cold and warm sweeps,
gizmo activation, optional active-tool sweeps, and cleanup without transform drags.
This is useful when repeating selection measurements after a noisy run. Keep the
original samples and report both runs rather than silently discarding outliers.

The disposable viewer stays on top during the benchmark so another window
cannot occlude it and suspend viewport presentations. The controller closes
that viewer at completion; existing user app windows are unaffected.

On macOS the owned process holds an `NSProcessInfo` user-initiated activity
allowing idle system sleep for the finite run, then ends it at completion. This
keeps system background-work heuristics from distorting timings without changing
global power settings. Earlier runs without this control must be identified
separately; timings should not be silently pooled across activity configurations.
