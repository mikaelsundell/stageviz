# Stageviz performance benchmarks

## Stageviz viewport baseline

Run `test_benchmark.bat Release` on Windows or `bash ./test_benchmark.sh Debug`
on macOS from a checkout with Stageviz already built. The runner accepts
`Debug`, `Release`, `RelWithDebInfo`, and `MinSizeRel`. Use `--build-dir`
for a build outside the default configuration directory. Set `THIRDPARTY_DIR` to
the same configuration-specific dependency prefix used by the build. If the
variable is missing or invalid, the runner prompts for it in an interactive
terminal. The answer applies to that run. On Windows the default is the executable
in the selected build directory, including when it was configured with
`DEPLOY_BUILD=ON`. The benchmark never builds the app or launches a second viewer.

The normal run measures **Orbit**, **Orbit with selection**, and
**Large hierarchy selection**. Four passes feed one result per workload.
Orbit measures input to frame swap, with and
without changing the selection during camera movement. Use `--quick` to run
only the two orbit workloads. Reports show median and p95 timing changes as
percentages when a matching baseline exists. The full JSON also includes
paint-end-to-swap and frame interval measurements.

The baseline key includes the machine profile, active OpenGL renderer and
version, OpenUSD, build configuration, viewport size, fixture hash, and sample
settings. The pass count, Git revision, and binary hash are recorded without
affecting baseline matching. A machine or GPU change can produce a new key.

The runner looks up each baseline under `docs/performance/stageviz-viewport/`.
It prints the previous and current timings with their percentage differences,
or reports that there is no match. Answer **yes** to save or overwrite the
matching JSON files in the working tree. The normal run numbers all eight
passes in sequence and asks once after both comparisons. Press Enter or use
`--no-prompt` to keep the existing baselines. Noninteractive runs only
report and compare.

Review and commit the JSON when you want to share the baseline. The file omits
installation hashes, local paths, and raw logs. Raw results remain under
`build.test-results/`.

### Large hierarchy selection

The hierarchy workload runs by default and uses its own baseline. The normal
run writes its report under `hierarchy/summary.json` in the results directory.
Pass `--hierarchy` to run this workload on its own.

The default fixture contains **240,421 composed prims**, including 40,000
polygon meshes and 40,000 analytic shapes. Each component has nested Xforms,
geometry, and a metadata branch. Meshes include triangles, quads, pyramids,
and subdivided boxes. Analytic shapes include spheres, cubes, cones, cylinders,
and capsules.

With the camera fixed and an Outliner branch expanded, the benchmark cycles
through four repeatable sets of up to 96 selections across the hierarchy.
Each set mixes parent Xforms and individual geometry paths. The primary
measurement is selection dispatch to the first fresh frame swap after the
selection completes. The benchmark checks the composed prim count, exact
selection, and unchanged USD data. The baseline matches the fixture,
selection sequence, and sample settings.

The default is four passes of 24 samples. Use `--runs` to change the pass
count. For a smaller fixture, use
`--hierarchy 400 --frames 6 --no-prompt`.

For optional native timings, launch with
`STAGEVIZ_PERF_LOG=/absolute/path/to/phases.jsonl`. See
[performance logging](../../docs/performance-logging.md) for startup behavior,
event fields, scope coverage, overhead and interpretation.

## Two-layer Storm benchmark

`benchmark_storm_layers.py` adds two caller-provided USD layers to an anonymous
stage. Use generated fixtures for examples and shared results. Launch a
disposable Stageviz process through `mcp_runtime.DisposableStageviz`. The profile
selected by `STAGEVIZ_STORM_PROFILE` supplies two absolute paths in `layers` and
an existing `output` directory. Start the embedded script once through MCP so
undo retains muted layers. At each checkpoint, query the stage and selection
through the external client and write the phase name to `ack.txt` in the output
directory to advance.
The benchmark leaves the input layer files unchanged.

Set `HDST_MAX_STAGING_BUFFER_SIZE` before app startup to choose bounded USD
staging bytes. `STAGEVIZ_PERF_LOG` enables optional native CPU wall-time scopes.
Set these in the disposable process environment before launch. Restart between
profiles. The profile's `short` option limits the run to stage creation, layer
addition, and the first mute and unmute. Set `traceOperation` to a phase name
for a separate diagnostic run with USD Trace overhead. Use phase names such
as `add_layer_1`, `add_layer_2`, and `mute_layer_1_1` in custom profiles.

End-to-end time waits for session updates and three subsequent viewport presents;
add operations include an explicitly timed frame-all. Native scopes are inclusive
CPU wall times, not GPU hardware queries. Metadata, screenshots, Qt snapshots,
and external MCP checks occur after each measured span. OS/network/shader caches
are not cleared, so fresh processes are not cold-cache measurements.

Run these manual benchmarks in a disposable Stageviz process. They replace the
active stage and can write input paths, prim names, and screenshots to their
output directory. Keep results from private inputs local.

## Run through MCP

Start a debug build with the MCP server enabled:

```sh
DYLD_IMAGE_SUFFIX=_debug build.debug/Debug/stageviz.app/Contents/MacOS/stageviz \
  --execute 'stageviz.startMcpServer()'
```

Use the external MCP client to call `stageviz_run_python` with:

```python
import runpy
runpy.run_path("/absolute/path/to/stageviz/CTest/benchmark/benchmark_prim_commands.py")
```

The generated-fixture scripts wait for deferred session updates, check the
resulting USD state, and record dispatch and end-to-end times. They use
`testbench` for Qt event processing and viewer screenshots and write JSON
reports and PNG screenshots under the system temporary directory.

Set `STAGEVIZ_BENCHMARK_COUNT` before launching Stageviz to change the default
fixture size (5,000 prims). `benchmark_hide_delete.py` also accepts
`STAGEVIZ_BENCHMARK_OPERATION=hide` or `delete`.

## Scripts

- `benchmark_prim_commands.py`: selected hide/show, subtree rename, multi-prim
  move, and delete.
- `benchmark_outliner_parent_commands.py`: rename, move, and delete a parent
  with a large expanded Outliner subtree; checks both USD and Qt row counts.
- `benchmark_hide_delete.py`: focused hide or delete run for a large selection.
- `benchmark_selection_transform.py`: cold/warm sweep, invert/undo,
  Move tool activation, separate drag press/moves/release, undo/redo and cleanup.
  The benchmark uses the first layer in the `STAGEVIZ_STORM_PROFILE` configuration.
  Full sweep/render timing requires a valid OpenGL viewport. CPU-only profiles
  label rendering unavailable and use deterministic path selections. They do
  not measure visible gizmo appearance or rendered drag responsiveness.

The tests must use MCP for live app operation and capture screenshots through
the viewer widget. Do not add these scripts to the automatic CTest suite.

## Stageviz and usdview viewport swaps

On Windows, compare a Release Stageviz build with the standalone usdview
`StageView` from a matching Release OpenUSD prefix:

```powershell
py -3 CTest/benchmark/compare_viewer_swaps.py `
  --stageviz build.Release/Release/Stageviz.exe `
  --stageviz-runtime build.Release/test-runtime-Release.txt `
  --usd-python 'path/to/python.exe' --usd-prefix 'path/to/release-prefix' `
  --synthetic-cubes 1024 --output build.test-results/viewer-swaps
```

The runner uses the same USD file, orbit sequence, and physical viewport size
in both viewers and checks the final camera framing. Each viewer retains its
own depth clipping policy. The runner waits for a fresh buffer swap after each change and reports
median, p95, and maximum times for input-to-swap, paint-end-to-swap, and swap
intervals. It also measures Stageviz orbit with selection changes and a
populated Outliner branch.
Use `--select-prim` to choose which prims to cycle.

The comparison runs without native tracing. A separate Stageviz run records
inclusive viewport, Hydra, Outliner, and Property Editor durations. Scope
totals can overlap and must not be added together. Each viewer uses its own
rendering and UI pipeline. The standalone StageView has no Outliner, and this
is a serialized redraw benchmark with scripted camera changes.

Use `--scene` in place of `--synthetic-cubes` to benchmark an existing USD
file. The output includes screenshots, raw samples, and MCP checkpoints.
Keep results from private inputs local. See
[performance references](../../docs/performance/README.md) for the repository
baseline workflow.

## Synthetic 250k-prim / 20m-face comparison

See [`sweep_scale/README.md`](sweep_scale/README.md) for the generated independent
mesh fixture, serial standalone-build comparison, native phase breakdown and
external MCP verification. It covers cold and repeated warm sweeps, gizmo
activation, bulk dragging and parent moves, with undo/redo. Timings distinguish
synchronous dispatch, viewport response and deferred UI settling.

## Payload checkbox aggregation at 200k prims

`benchmark_payload_indicators.py` generates 500 populated Xform groups, each
with 400 ordinary child Xforms and a payload gate, plus a separate mutable layer
branch. The unloaded scene contains 201,004 USD prims and 201,005 Outliner rows;
loading adds 501 payload geometry prims. Run on a disposable process using
`mcp_runtime.DisposableStageviz` and an independent MCP listener. Load the module
through MCP, call `make_fixture(directory)`, create `PayloadIndicatorBenchmark`
with the returned filename and an output directory, then run its `setup`,
`load`, `mute`, `parent_move`, and `unload` phases as separate MCP requests.
Capture `stageviz_get_stage_info`, `stageviz_get_selection`, `stageviz_list_prims`
and representative `stageviz_get_prim_info` responses between phases.

Reports distinguish cold aggregate reads after model invalidation, repeated warm
reads, forced viewport painting into a pixmap, stage loading, payload loading,
layer muting and a populated parent move with undo. Screenshots and JSON reports
are written per phase. Use the same fixture, build settings, active branch and
serialized process order for before/after comparisons; monitor memory through
`DisposableStageviz`. This is a targeted UI/command benchmark, not a geometry or
renderer throughput benchmark.

## Gizmo renderer before/after comparison

`benchmark_gizmo_rendering.py` compares two built revisions in isolated MCP
processes, reversing their order each round. It exercises camera rotation and
zoom with no selection and with Move/Rotate/Scale, grid on/off, and small and
2,000-cube scenes. It verifies unchanged USD data and selection, viewport
renderReady signals, and captures Qt snapshots, screenshots and independent MCP
stage/selection evidence outside timed spans.

```sh
python3 CTest/benchmark/benchmark_gizmo_rendering.py \
  --before /path/to/before/stageviz --after /path/to/after/stageviz \
  --output /path/to/new-results-directory
```

The default is three fresh-process rounds per revision, 30 warm-up frames and
120 measured frames per condition. Repaints without a renderReady signal are
excluded and retried with a bounded limit. Results include median and p95 repaint wall
times and process RSS. These timings include CPU submission and driver stalls,
not GPU hardware execution. No readback, synchronization wait or diagnostic
logging is added during measurement. RSS is sampled after each scene's measured
conditions and final checkpoint, so it includes allocated checkpoint resources.

## First transform-handle presentation

`benchmark_gizmo_first_show.py --app /absolute/path/to/stageviz --output /new/output`
starts a fresh disposable process for each first tool (Move, Rotate, Scale) and
round. It compares the first `renderReady` response with repeated switches,
records unchanged document root/session layers and selection, and saves rendered
handles, a Select-mode frame, Qt snapshots and independent MCP session oracles.
Run builds serially with equal viewport sizes; use `--rounds` for replication.
Timings include CPU submission and driver stalls, not GPU hardware execution.
The first frame of the stage pays for hidden renderer preparation, so this moves
the first-interaction cost to viewport initialization. No OS/shader caches are
cleared, and screenshots/readbacks occur outside measured spans.

## Selection pipeline before/after comparison

`compare_selection_pipeline.py` compares two app binaries using the same SDK,
configuration, fixture and viewport. It runs disposable processes serially in
baseline/candidate/candidate/baseline order, without invoking CTest:

```sh
python3 CTest/benchmark/compare_selection_pipeline.py \
  --baseline /path/to/baseline/stageviz --candidate /path/to/current/stageviz \
  --output build.test-results/selection-comparison --count 10000 --samples 24
```

The ordinary-mesh workload reuses `testselectionoutline.py`. A second workload
uses nested native instances and fixed-seed proxy groups to exercise grouped
prototype traversal and flattened instance indices. Both populate the Outliner;
parent moves include undo and USD assertions. External MCP verifies final stage,
selection and a representative proxy. Viewer screenshots and Qt snapshots are
saved outside measured intervals.

`comparison.json` includes first-selection response, repeated selections, cached
redraws and parent moves, plus `selection.hydra_mapping` samples correlated with
the nested workload. Response times include framebuffer readback. Native logging
is enabled equally for both binaries; these are instrumented wall-clock timings,
not GPU-only measurements or uninstrumented release-performance claims. Fixture
hashes, viewport sizes and executable provenance are recorded for comparison.

## Focused nested-instance parent move/undo comparison

`compare_nested_parent.py` isolates the parent-edit workload from selection and
startup benchmarks. Its default fixture has 5,000 outer instances containing
10,000 nested cube proxies, with the Outliner expanded and the parent selected.

```sh
python3 CTest/benchmark/compare_nested_parent.py \
  --baseline /path/to/master/stageviz --candidate /path/to/audit/stageviz \
  --output build.test-results/nested-parent-comparison \
  --rounds 4 --samples 12 --warmup 4
```

Processes run serially in alternating baseline/candidate order. Each process
records warmups separately and measures both the standard `testbench` command
wait and the same Session completion barrier without an added settling delay;
the wait-mode order is counterbalanced. Both modes include framebuffer readback
after the move and undo. Per-sample dispatch, waiting, readback, total time and
rendered-frame counts distinguish command costs from event-loop settling.

Every round trip verifies USD transforms and selection; each wait-mode block
checks exact layer restoration. External MCP observations, viewer screenshots,
Qt snapshots, fixture hashes and binary provenance accompany the results.
Compare process medians rather than treating samples from one process as
independent runs. Native logging is off by default; add `--instrumented` for a
separate phase-attribution run. These timings include readback and event-loop
work and are not GPU-only measurements.

The broader selection comparator records only three nested parent round trips
per process, without a dedicated parent-edit warmup. Use this focused comparison
when deciding whether to revert a change for parent-edit latency; a small broad
comparison is a signal to investigate, rather than sufficient regression evidence.
