# Stageviz / Storm performance investigation

The durable archive is
`C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation`.
Iterations 1 and 2 preserve the earlier buffer diagnosis and bounded-staging
fix. Iteration 3 contains immutable before/after source snapshots, Release
deployment provenance, and copies of measured runs.

Build USD into the existing installed Release prefix, build Stageviz with that
prefix, then use `deploy.py --refresh-only` after the first complete deployment.
The helper explicitly copies all installed USD DLLs from `Release\lib` and hashes
them; it does not rely on an older DLL found on PATH.

From the Stageviz repository, using the configured Python 3.9 installation:

```powershell
python investigation/performance/deploy.py --refresh-only
python investigation/performance/run.py --settings investigation/performance/focused-profiles.json
python investigation/performance/analyse.py
python investigation/performance/snapshot.py
```

The controller launches only its own disposable `deploy.Release\Stageviz.exe`.
It refuses to use an existing MCP server. It creates an anonymous stage, adds
the specified front assembly, waits for presentation, adds the rear axle, and
executes the selected operations. Source datasets are never saved. The benchmark
starts once through MCP so undo history retains muted Sdf layers. An external
controller captures read-only MCP stage/prim/selection checks between operations.
After measurement it closes the disposable stage before quitting, so anonymous
fixture changes are discarded without a save prompt. The older matrix controller
precedes that cleanup change; its save prompts must be answered Don't Save.

Each run has config, executable/script SHA256, dataset sizes/mtimes, selected
environment variables, CPU/RAM/GPU resource samples, native inclusive phase
times, Qt state, screenshots, and independent MCP oracles. Output is under
`C:\Temp\usdtest\performance`, copied to `test3-performance\runs` on exit.
The external watchdog aborts an operation after 30 minutes. A failed or stopped
run is partial evidence, not a completed measurement.

`profiles.json` is a broader example matrix; `focused-profiles.json` selects the
actual comparison. Staging-size profiles measure loading/upload, since changing
upload size cannot fix a CPU removal scan. The default workflow also includes
repeat mute/unmute, axle muting, visibility undo, and deferred-render controls.
`view.phases` selects known phase names explicitly; `view.short` selects the first
five. Visibility targets the front branch beneath shared single-child containers;
the exact paths are recorded. Visibility and layer mute have different composition
and memory semantics.

Settings applied before process startup:

- `HDST_MAX_STAGING_BUFFER_SIZE`: bytes, bounded staging; 64 MiB default.
- `STAGEVIZ_PERF_LOG`: optional JSONL CPU wall-time scopes; unset disables logging.
- Profile `env`: additional explicitly requested Storm settings, e.g.
  `HD_ENABLE_GPU_FRUSTUM_CULLING=0`.
- Profile `view`: `materials`, `sceneStats`, `layerEditor` (booleans).

Use `--trace` in a separate diagnostic run for USD Trace on the first front mute.
Trace and stack sampling add overhead and are not timing baselines. The standalone
sampler under `sampler` briefly suspends only an explicitly identified test thread;
samples record suspension duration. Export names without PDBs can be misleading:
use module/RVA and a matching linker map for private function attribution.

Native times include nested work and must not be summed as independent phases.
`hydra.render_cpu` is CPU wall time inside the render call, not GPU hardware time.
End-to-end spans wait for session readiness and three viewport presentations;
loading spans include explicit frame-all. Screenshots and metadata are collected
after the span. Fresh processes retain OS/network/shader caches. The intermediate
removal-only diagnostic overlapped native compilation and sampling; final profiles
must be interpreted separately.

The Windows USD build overlay preserves the entire investigation directory across
`build_win.py CLEAN` extraction using a checked sibling stash. `snapshot.py` appends
a timestamped source/tools snapshot and never overwrites an earlier snapshot.
