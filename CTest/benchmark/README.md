# Stageviz performance benchmarks

For optional native timings, launch with
`STAGEVIZ_PERF_LOG=/absolute/path/to/phases.jsonl`. See
[performance logging](../../docs/performance-logging.md) for startup behavior,
event fields, scope coverage, overhead and interpretation.

## Two-layer Storm investigation

`benchmark_storm_layers.py` runs the two supplied Volvo assembly sublayers in an
anonymous stage. Use the external Windows controller
`investigation/performance/run.py`; it starts a disposable deployed Release app,
checks MCP tools, and records independent stage/selection oracles at checkpoints.
The embedded script uses public commands and never saves the source layers.
It starts once so native undo retains muted layers throughout the workflow.

Set `HDST_MAX_STAGING_BUFFER_SIZE` before app startup to choose bounded USD
staging bytes. `STAGEVIZ_PERF_LOG` enables optional native CPU wall-time scopes.
The controller handles these per run; `--settings` accepts the example profile
JSON for staging sizes, materials, scene statistics, culling, and layer-editor
state. Restart between profiles. `--trace` is a separate diagnostic run with USD
Trace overhead. `view.short=true` selects empty/add/add/mute/unmute only.

End-to-end time waits for session updates and three subsequent viewport presents;
add operations include an explicitly timed frame-all. Native scopes are inclusive
CPU wall times, not GPU hardware queries. Metadata, screenshots, Qt snapshots,
and external MCP checks occur after each measured span. OS/network/shader caches
are not cleared, so fresh processes are not cold-cache measurements.

These scripts are manual performance tools, not CTest cases. They load a
generated USDA fixture into the active session and replace its current stage.
Run them only in a disposable Stageviz process without unsaved user work.

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

Each script creates fixtures, waits for deferred session updates to finish,
checks the resulting USD state, and records dispatch and end-to-end times. The
scripts use `testbench` for Qt event processing and viewer screenshots. They
write JSON reports and PNG screenshots under the system temporary directory.

Set `STAGEVIZ_BENCHMARK_COUNT` before launching Stageviz to change the default
fixture size (5,000 prims). `benchmark_hide_delete.py` also accepts
`STAGEVIZ_BENCHMARK_OPERATION=hide` or `delete`.

## Scripts

- `benchmark_prim_commands.py`: selected hide/show, subtree rename, multi-prim
  move, and delete.
- `benchmark_outliner_parent_commands.py`: rename, move, and delete a parent
  with a large expanded Outliner subtree; checks both USD and Qt row counts.
- `benchmark_hide_delete.py`: focused hide or delete run for a large selection.
- `benchmark_selection_transform.py`: real-data cold/warm sweep, invert/undo,
  Move tool activation, separate drag press/moves/release, undo/redo and cleanup.
  The external controller and profiles are in `investigation/selection-performance5`.
  Full sweep/render timing requires a valid OpenGL viewport. CPU-only profiles
  label rendering unavailable and use deterministic path selections. They do
  not measure visible gizmo appearance or rendered drag responsiveness.

The tests must use MCP for live app operation and capture screenshots through
the viewer widget. Do not add these scripts to the automatic CTest suite.

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
