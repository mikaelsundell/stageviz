# Iteration 4: Storm CPU maintenance

The new local SDK implements optional bounded mesh-topology cleanup and native
phase counters. It is built and installed in the Release SDK, and Stageviz's
deployed Release app uses the matching DLLs. **Bounded topology cleanup remains
off by default.** Its first response improved, but complete reclamation was
substantially slower in the measured large-scene run.

## Results

Each row is one fresh process using the user's front-plus-rear-axle workflow.
OS, network and shader caches were retained; these are single-run observations,
not statistical speedup claims. Both layers share namespace ancestors.

| Profile | Add front | Add axle | Mute first response | Subsequent cleanup settle | Unmute first response |
| --- | ---: | ---: | ---: | ---: | ---: |
| Instrumented eager | 108.47 s | 271.82 s | 122.42 s | 0.19 s | 124.06 s |
| 20 ms topology budget | 102.78 s | 269.56 s | 100.41 s | 158.93 s | 128.19 s |
| 20 ms, unmute before cleanup finishes | 99.10 s | 267.10 s | 98.85 s | deliberately skipped | 121.92 s |

The quick-toggle run reached convergence 1.08 s into its final settle phase.
The normal budget run reached it 0.11 s into final settle after unmute.
Checkpoint screenshots, independent MCP reads, and Qt/USD snapshots separate
first response from completion. There is controller/oracle work between phases;
adding response and settle values is not an exact uninterrupted elapsed time.

In the eager mute:

- 77,294 topology entries were visited; 72,979 were reclaimed.
- Topology node erasure took 61 ms; object destruction took 16.90 s.
- The topology step took 16.99 s; all resource garbage collection took 23.12 s.
- Resource commit took 29.29 s, inclusively; the Stageviz Hydra CPU scope took
  101.73 s. These nested times must not be added together.
- Shader preparation: 36 calls, 1.8 ms, all program-cache hits, no misses.
- Full batch rebuilding: 74.9 ms.

These measurements do not justify a new shader cache or batch preservation
algorithm for this mute operation. Most of the delay occurs outside resource
cleanup. Brief matching-binary CPU samples show scene-index prim-addition
propagation and native-instance/prototype processing before resource commit.
The trace diagnostic is recorded separately from timing comparisons.

The separate traced mute confirms that split. `UsdImagingGLEngine::PrepareBatch`
took 53.57 s: 30.23 s in `UsdImagingStageSceneIndex::ApplyPendingUpdates` and
23.34 s in `HdNoticeBatchingSceneIndex::Flush`. Within pending updates, deleting
merging scene indices took 10.39 s across 404 prototype scene-index destructors,
and rerooted removal processing took 8.36 s across 658 calls. One merging-index
removal path made 583,059 `GetPrim` calls; the later flush made 591,280 at another
level. `HdRenderIndex::SyncAll` took another 18.97 s under `HdEngine::Execute`.
These are nested scopes from a traced run, not independent additive totals.

The next targeted optimization should therefore reduce repeated removal/resync
propagation and preserve unaffected prototype scene-index state. Full namespace
removal needs a fast path that still respects fallback opinions from surviving
inputs. Prototype destruction itself also needs measurement separately from
notice generation. A shader-cache change would miss the measured bottleneck.

The budgeted version revisits other resource registries on each cleanup frame.
Its settle phase spent 98.92 s in resource GC, including 33.33 s in topology
destruction. Additional rendering and event-loop scheduling increase elapsed
time further. The largest measured topology step was 124.5 ms: the 20 ms limit
is soft and one destructor, allocator operation or scheduling pause can exceed
it. This is a reason to retain eager behavior for normal builds.

The budget comparison includes a brief diagnostic thread sample during its
cleanup, and the quick-toggle comparison includes one during scene-index
processing. Sampler logs record suspension duration; comparisons are not pristine
unprofiled benchmark repetitions.

## Implementation

The caller owns a resumable cursor in `HdInstanceRegistry`. The implementation
checks reference ownership before erasing an entry, separates node-erasure and
destructor timing, supports a requested rescan, and limits visits/time per step.
It performs no background destruction. Registry instance layouts are unchanged.

Storm stores cursor state in its existing resource-registry extension map.
RenderDelegate continues maintenance on later frames; the existing render-pass
convergence interface reports pending work. Stageviz requests another frame when
either document or auxiliary renderer has not converged. Shutdown forces eager
cleanup. A registry entry-count threshold also forces eager cleanup; it is not
a memory-byte limit.

Native counters cover resource commit/GC, shader binding and code generation,
program lookup/compilation, and draw-batch validation/rebuild. Logging is opt-in
and aggregate; it emits no per-prim log stream. Enabling the native log while
using eager cleanup selects the instrumented collector, which moves the retired
shared pointer out of its node before erasure to time destruction separately.
Without logging or a budget, the original eager collector is used.

See [settings and reproduction](C:/git/github/stageviz/investigation/storm-performance4/README.md).
Set `HDST_TOPOLOGY_GC_BUDGET_US=20000` only for an experimental run in a fresh
process; unset it or use zero for normal eager behavior. The earlier bounded
staging uploads remain enabled at their normal 64 MiB default.

## Verification and limits

- Release USD build/install and Release Stageviz app/test builds passed.
- Native registry regression passed: visit/time limits, live references,
  reacquisition, insertion/rehash, rescan, empty registry, final drain, and exact
  destruction counts. The launcher explicitly sets SDK DLL paths and suppresses
  initialization-error UI. An earlier failed restricted launch left the native
  startup dialog the user observed; the corrected launcher passed.
- Three focused GUI cases passed: layer mute/undo/redo, subset presentation,
  and instance/payload resync.
- The separate `renderengine_api` offscreen regression failed at renderer
  initialization. A further plugin-path diagnostic exited with 0x80000003.
  This remains unresolved; the full renderer regression set is not green.
- The live viewport smoke and three synthetic fixture profiles passed. The
  visit-limited fixture returned with cleanup pending and later converged;
  the entry-pressure fixture exercised forced eager cleanup.
- The large eager, budgeted and quick-toggle runs completed with expected
  composed counts: 587,594 active prims with both layers; 26,895 after muting
  the front; 587,594 after restoring it. Independent MCP layer counts and
  selection are checked against each checkpoint, alongside source file size
  and modification time. Source layers were never saved.
- The large test sessions took long enough to close that MCP shutdown requests
  timed out. Earlier controller versions did not record whether the subsequent
  process wait required termination. Do not infer clean full SDK destruction
  from their completed operation results. The fixture drain tests are separate.
  The later trace controller recorded exit code 0 without termination after
  51.90 s of shutdown; its initial 30 s MCP close request had timed out.
- The guarded 16-file Windows overlay passed fresh ZIP application, repeat
  application, and refusal to overwrite an intentional local edit.
- These are Windows/OpenGL checks on one machine, not cross-platform or
  multi-machine performance validation. Timings are CPU wall time, not GPU
  hardware timings. No upstream PR or commit has been made.

The trace run completed at the expected muted state and measured 122.75 s for
the operation. Building/exporting the diagnostic reports happened afterward;
the individual-event Chrome file is approximately 9.6 GB. Future trace runs
default to the compact text report unless `traceChrome` is explicitly enabled.

## Changed source files

The native CPU work changes:

```text
pxr/imaging/hd/instanceRegistry.h
pxr/imaging/hd/CMakeLists.txt
pxr/imaging/hd/testenv/testHdInstanceRegistryBudget.cpp
pxr/imaging/hdSt/CMakeLists.txt
pxr/imaging/hdSt/performance.h
pxr/imaging/hdSt/performance.cpp
pxr/imaging/hdSt/resourceRegistry.h
pxr/imaging/hdSt/resourceRegistry.cpp
pxr/imaging/hdSt/renderDelegate.cpp
pxr/imaging/hdSt/renderPass.h
pxr/imaging/hdSt/renderPass.cpp
pxr/imaging/hdSt/drawBatch.cpp
pxr/imaging/hdSt/commandBuffer.cpp
```

Stageviz adds convergence polling in `renderengine.h/.cpp` and
`viewportwidget.cpp`, a convergence assertion and optional failure log in the GUI
test harness, and maintenance phases in `benchmark_storm_layers.py`. External
controllers and iteration scripts preserve source, configuration, exact DLL
hashes, checkpoint results, screenshots, logs and matching-binary symbols.

The existing upload patch files are also retained in the overlay and snapshots:
`stagingBuffer.cpp`, `stagingBuffer.h` and `testHdStStagingBatches.cpp`.
The overlay's `record_install.py` now records the CPU patch and default budget.
The normal Windows dependency build continues to apply the guarded overlay.

All before/after source snapshots, earlier fixes, baseline binaries, current
binaries and evidence are under
`C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test4-storm-cpu`.
