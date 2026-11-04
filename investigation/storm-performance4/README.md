# Storm CPU maintenance experiment

This iteration adds aggregate native timings and opt-in incremental mesh-topology
garbage collection to the local OpenUSD 25.11 SDK. Eager cleanup remains the
default. The bounded upload fix from iteration 2 remains enabled at 64 MiB.

Set environment variables before starting a **fresh** Stageviz process:

| Setting | Default | Purpose |
| --- | --- | --- |
| `HDST_PERF_LOG` | unset | Native Storm JSONL log filename |
| `STAGEVIZ_PERF_LOG` | unset | Stageviz CPU phase JSONL log filename |
| `HDST_TOPOLOGY_GC_BUDGET_US` | `0` | Soft per-frame topology cleanup budget; zero is eager |
| `HDST_TOPOLOGY_GC_MAX_VISITS` | `8192` | Maximum topology entries visited per budgeted step |
| `HDST_TOPOLOGY_GC_MAX_ENTRIES` | `1000000` | Registry entry count above which cleanup becomes eager |
| `HDST_MAX_STAGING_BUFFER_SIZE` | `67108864` | Upload batch bytes, independent of CPU cleanup |

The time budget covers topology traversal, node erasure and destruction. It does
not bound all rendering, other registry sweeps, or one indivisible destructor.
The entry limit is a count gate, not a GPU or CPU memory byte limit. Delayed
cleanup retains unused objects and their resource references until later frames.
Destruction stays on the rendering thread. Shutdown forces a complete sweep.

Stageviz requests more frames while the renderer reports pending cleanup.
`renderConverged` on the viewport is an observable benchmark property. A first
response with this property false is not complete reclamation. The benchmark
records separate settle phases and checks independent MCP stage/selection state.

## Reproduce

Use the SDK Python 3.9 interpreter and the deployed Release app. Set
`STAGEVIZ_INVESTIGATION_DIR` to the SDK root's
`stageviz-investigation/test4-storm-cpu` folder before running the external
controller:

```powershell
python investigation/performance/run.py --settings investigation/storm-performance4/profiles.json --storm-log
python investigation/performance/run.py --settings investigation/storm-performance4/trace-profile.json --storm-log
python investigation/storm-performance4/analyse.py
```

These runs create an anonymous scene, add the two shared source layers, and never
save them. The controller closes its own session before exiting the app.
It refuses to replace an existing MCP session. Trace runs are diagnostics and
must be kept separate from timing comparisons. OS, network and shader caches
are retained between processes.

Trace runs now default to the aggregated text report. Set `view.traceChrome` to
true in a profile only when individual Chrome events are needed: the large
dataset produces a multi-gigabyte JSON export and its serialization takes much
longer than the measured operation. The first diagnostic run retained both.

The local fixture generator supplies three profiles for eager, visit-limited
and forced-eager pressure behavior. Native regression coverage exercises visit
and soft time limits, live references, reacquisition, insertion/rehash, rescan,
empty registries, and complete drain:

```powershell
cmake --build investigation/storm-performance4/build --config Release --parallel 6
python investigation/storm-performance4/run_native_test.py
```

Use this launcher so the test receives SDK DLL paths and initialization failures
return an error instead of leaving a native error dialog.

## Scope

There is no new shader cache or batch preservation algorithm in this iteration.
The counters measure those paths so a change requires evidence first. Timings
are inclusive CPU wall times; the viewport's historical GPU-time label does not
turn them into hardware GPU measurements.

The guarded Windows overlay contains the new native sources and test alongside
the existing upload fix. `audit_overlay.py` verifies pristine ZIP application,
idempotence and refusal to overwrite local edits. `update_overlay.py` updates
that durable overlay after an intentional SDK source change.
