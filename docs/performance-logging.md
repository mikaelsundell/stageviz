# Native performance logging

`STAGEVIZ_PERF_LOG` enables optional `PerfScope` phase logging in Debug and
Release builds. Set it to a writable file path before launching:

```sh
DYLD_IMAGE_SUFFIX=_debug STAGEVIZ_PERF_LOG=/tmp/stageviz-phases.jsonl \
  deploy.release/Stageviz.app/Contents/MacOS/stageviz \
  --execute 'stageviz.startMcpServer()'
```

The parent directory must exist. An unset or empty value disables logging and
benchmark observation properties: no timing clocks, JSON serialization or file
writes run. The destination is cached at first use; restart to change it.
There is no persistent preference or UI switch.

Output appends rather than truncates. Use a fresh file for each run. Writes are
serialized between threads within one process; use separate files for separate
processes. An unavailable destination silently produces no output. Logging is
synchronous and intended for bounded diagnostic runs; files have no automatic
size limit or rotation.

## Event fields

| Field | Meaning |
| --- | --- |
| `phase` | Stable operation name, such as `selection.sweep` |
| `start_ms` | Steady-clock timestamp in milliseconds; compare within the process |
| `wall_start_ms` | Unix epoch milliseconds for external controller correlation |
| `duration_ms` | Inclusive elapsed wall time of the scope |
| `thread` | Hexadecimal Qt thread identifier, meaningful within the process |

Events are written when scopes finish, so nested scopes precede their parents.
Sort by `start_ms` to inspect execution order. Durations include lock waits and
synchronous callees; they are neither CPU utilization nor GPU hardware timings.
`hydra.render_cpu` measures the elapsed host render call and can include driver
or GPU waits. Do not add nested or overlapping scope durations together.

## Coverage

- `selection.*`: sweep, cold BVH construction, warm spatial query, selection
  publication, inversion workers and USD-to-Hydra highlighting path mapping.
- `transform.*`: target and native-instance resolution, pivot preparation,
  numeric input refresh, drag begin/update/end and transform authoring/restore.
- `command.*`: execute, undo and redo dispatch, plus selected layer operations.
  Dispatch includes synchronous signal handlers, but not queued worker completion
  or later viewport presentation. Execute also covers recording an already
  applied command; it does not always mean fresh USD authoring.
- `usd.namespace_*`: validated batch removal, rename and reparent operations,
  including synchronous USD change processing.
- `session.*`: loading, bounds, notice processing and publication.
- `ui.*`: Outliner selection/stage/prims, filtering and depth expansion;
  Property Editor summary/discovery and notice refresh; material refresh/list
  reconciliation; selection actions, layer tree and scene statistics.
- `scene_index.*`, `hydra.*`, `viewport.paint`: scene-index add/remove handling
  and viewport rendering. Per-prim accessors and per-item loops are deliberately
  not instrumented, to avoid one log event per object in a large scene.

When enabled, the viewport also publishes `transformPivotValid`,
`transformPivotScreen`, `transformDragging`, `transformActiveAxis` and
`transformTargetCount` as Qt dynamic properties. The reusable benchmarks use
these to verify gizmo readiness and drag state. They are diagnostic observations,
not a supported authoring API.

## Measuring and extending

Use [the MCP benchmarks](../CTest/benchmark/README.md) on disposable processes
for end-to-end response measurements and independent USD/session assertions.
Logging adds clock, allocation and synchronous file-I/O overhead; confirm final
responsiveness with logging disabled. Control the fixture, view, settings, build
and process order, and report cold and warm results separately. Completed
dispatch does not imply completed asynchronous editing or a presented frame.

Use `PerfScope scope("category.operation")` with a static string literal around
a meaningful operation. Keep names stable for comparison scripts. Prefer
function or batch boundaries over primitive, attribute, tree-row or pixel loops.
Keep nested scopes only when they distinguish actionable costs. Use
`PerfScope::enabled()` for associated diagnostic observations so they share the
cached startup gate. Never use this gate to change editing, selection or
rendering behavior.

`sources/core/profile.h` provides a separate explicit-recording/aggregate API.
`STAGEVIZ_PERF_LOG` controls only `PerfScope` JSONL output and the observation
properties above. It does not enable USD Trace, lock tracing or GPU profiling.
