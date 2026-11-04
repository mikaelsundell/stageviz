"""Build a reviewable report from raw runs, including incomplete/excluded cases."""
from pathlib import Path
from datetime import datetime, timezone
import json
import shutil

root=Path(r'C:\Temp\usdtest\performance')
archive=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test3-performance')
runs=[]
for directory in sorted(root.iterdir()):
    if not directory.is_dir() or not (directory/'launch.json').exists():continue
    launch=json.loads((directory/'launch.json').read_text())
    if launch['config'].get('smoke') or (directory/'excluded.json').exists():continue
    status=json.loads((directory/'status.json').read_text())
    results=json.loads((directory/'results.json').read_text()) if (directory/'results.json').exists() else []
    runs.append((directory,launch,status,results))
final=[run for run in runs if 'scriptSHA256' in run[1]]
lines=['# Two-layer Stageviz / Storm investigation',
       '', 'Generated UTC: '+datetime.now(timezone.utc).isoformat(),
       '',f'Final matrix: {sum(run[2]["state"]=="complete" for run in final)} of 5 profiles complete.',
       '', 'The original long stalls have multiple causes: quadratic Stageviz rendering-cache removal, repeated addition indexing, Outliner reconciliation, and substantial CPU work inside Storm rendering. The bounded-staging fix remains installed; staging size is a separate loading/upload control.',
       '', '## Measurements',
       '', 'Times below are seconds from command dispatch through session readiness and three viewport presents. Add operations include explicit frame-all. Metadata, screenshots and external MCP checks follow the measured interval. Each final profile uses a fresh process; OS/network/shader caches are retained.',
       '', '| Run | State | Front load | Axle load | Visibility hide | Visibility undo | Front mute | Front unmute |',
       '|---|---|---:|---:|---:|---:|---:|---:|']
phases=['add_front','add_axle','hide_front_roots','undo_visibility','mute_front_1','unmute_front_1']
for directory,launch,status,results in runs:
    by_phase={item['phase']:item for item in results}
    values=[f'{by_phase[name]["end_to_end_ms"]/1000:.2f}' if name in by_phase else '—' for name in phases]
    state='stopped' if (directory/'controller-stop.json').exists() else status['state']
    lines.append('| '+directory.name+' | '+state+' | '+' | '.join(values)+' |')
lines+=['', 'Observed settings: keep 64 MiB staging as the default; 16/256 MiB produced similar loading times. Disabling scene statistics reduced front load from 100.68 to 80.45 seconds. Disabling scene materials reduced axle load from 258.74 to 142.25 seconds and made the dark assembly clearly visible using its fallback/display colors. Layer mute still took about 109–114 seconds.',
        '', 'The UI controls are “Use scene shaders” (Shift+S), and View > Head-up display > Scene tree (Ctrl+Space) for statistics. Profile JSON exposes these as `materials` and `sceneStats`.',
        '', 'The original and removal-only diagnostics were stopped while muting; their missing mute totals are lower bounds, not successful measurements. See `controller-stop.json`. Runs with `excluded.json` and all startup smoke tests are excluded from this table.',
        '', 'The final 64 MiB baseline includes visibility/undo before mute/unmute. The statistics/materials controls use the direct empty/add/add/mute/unmute workflow, so their later mute/unmute timings are not perfectly isolated single-factor comparisons. Loading uses the same initial sequence in all five profiles.',
        '', '## USD-only control',
        '', 'This separate process composes the same anonymous stage without Qt, Hydra or a GPU context. It retains local sublayers for mute/unmute caching. Traversal/count collection occurs after each timed operation.',
        '', '| Operation | Seconds | Active composed prims | Prims including proxies |', '|---|---:|---:|---:|']
bounds=[]
for directory,launch,status,results in runs:
    stop=directory/'controller-stop.json'
    if stop.exists() and status.get('start_wall_ms'):
        elapsed=(json.loads(stop.read_text())['wall_ms']-status['start_wall_ms'])/1000
        bounds.append(f'- {directory.name}: `{status["phase"]}` was still incomplete after {elapsed:.1f} seconds.')
if bounds:
    position=lines.index('## USD-only control')
    lines[position:position]=bounds+['']
control=root/'composition-control.json'
if control.exists():
    for row in json.loads(control.read_text())['results']:
        lines.append(f'| {row["phase"]} | {row["seconds"]:.4f} | {row["active_prims"]:,} | {row["all_with_proxies"]:,} |')
lines+=['', 'The interactive visibility control targets `/RCC_B13RH/FA__1_`. Both sublayers contribute beneath this shared container, so it hides both assemblies. The layer-mute control removes only the front contribution. Visibility undo restores the original property/spec state; the USD-only show control authors inherited visibility.',
        '', '## Native CPU attribution',
        '', 'Native scopes are inclusive wall times and overlap. Do not add them as independent phases. `hydra.render_cpu` is the wall time inside the render call, including CPU preparation, submission and waits. The on-screen “GPU time” label also uses a CPU timer; these values are not GPU hardware queries.',
        '', '| Profile / operation | Dispatch | Largest render call | Largest Outliner update | Largest scene-statistics update | USD mute/unmute call |',
        '|---|---:|---:|---:|---:|---:|']
for directory,launch,status,results in final:
    events=[json.loads(line) for line in (directory/'native-phases.jsonl').read_text().splitlines()]
    for row in results:
        if row['phase']=='empty':continue
        selected=[event for event in events if row['start_wall_ms']<=event.get('wall_start_ms',-1)<row['start_wall_ms']+row['end_to_end_ms']]
        def maximum(names):return max([event['duration_ms'] for event in selected if event['phase'] in names] or [0])/1000
        values=[row['dispatch_ms']/1000,maximum(['hydra.render_cpu']),maximum(['ui.outliner_prims']),maximum(['ui.scene_statistics']),maximum(['usd.mute_layer','usd.unmute_layer'])]
        lines.append('| '+directory.name+' / '+row['phase']+' | '+' | '.join(f'{value:.3f}' for value in values)+' |')
lines+=['', '## Implemented changes',
        '', '- Rendering cache removal: use ordered SdfPath subtree ranges instead of scanning all three complete target sets for every removal notification.',
        '- Rendering cache addition: index each individually notified prim once; retain recursive indexing for initial population and subtree renames.',
        '- Outliner deletion: for verified absent paths, remove their Qt subtree and reconcile each surviving parent’s direct children once. Preserve conservative handling for reused paths.',
        '- Optional native logging: `STAGEVIZ_PERF_LOG` records command, USD mute/unmute, session, scene-index, scene-statistics, Outliner, layer-tree and render scopes. It is disabled when unset.',
        '- Repeatable external controller and embedded asynchronous benchmark, profile JSON, snapshots and regression coverage.',
        '', 'Changed Stageviz files:',
        '', '- `sources/rendering/rendersceneindex.cpp`, `sources/outliner/outlinertree.cpp`',
        '- `sources/core/perfscope.h` (new), `sources/core/session.cpp`, `sources/commands/commandlayer.cpp`',
        '- `sources/outliner/outlinerview.cpp`, `sources/layers/layertree.cpp`, `sources/rendering/renderengine.cpp`, `sources/rendering/viewportwidget.cpp`',
        '- `CTest/regression.cpp`, `CTest/gui_regression.cpp`, `CTest/CMakeLists.txt`, `CTest/README.md`',
        '- `CTest/benchmark/benchmark_storm_layers.py` (new), `CTest/benchmark/README.md`, and new helpers under `investigation/performance/`',
        '', 'Windows build/archive integration additionally changes `C:/git/3rdparty/build_win.py`, adds `patches/openusd/preserve_investigation.py`, and refreshes `patches/openusd/build-win-integration.patch`. Full source/tool manifests and patches are in each immutable after snapshot. These Stageviz optimizations require the rebuilt executable as well as the bounded-staging USD DLLs.',
        '', 'The OpenUSD production source changes remain the previously installed bounded-staging overlay: `stagingBuffer.cpp`, `stagingBuffer.h`, `hdSt/CMakeLists.txt`, and `testHdStStagingBatches.cpp`. This performance iteration adds Stageviz changes and investigation artifacts; it does not add another USD rendering patch.',
        '', '## Sampling evidence',
        '', 'The original main-thread samples repeatedly hit Stageviz `RenderSceneIndex::unindexSubtree` and `SdfPath::HasPrefix` inside Hydra removal notification delivery. The intermediate removal-only diagnostic reached Storm resource garbage collection and then lengthy Outliner invalidation. Matching USD maps resolve the mute sample to `HdStResourceRegistry::_GarbageCollect`, mesh-topology instance-registry garbage collection, topology destruction and heap cleanup. The final visibility sample hit Storm drawing-program setup, `HdSt_CodeGen` destruction and heap/string cleanup.',
        '', 'Final-binary scratch linker maps were generated from existing Release objects. `.text` hashes match the deployed Stageviz, usd_hdSt and usd_hgiGL code exactly; see `symbol-maps/*.provenance.json`. Private USD function names are resolved using these maps rather than relying on nearest exported names from stack sampling. Samples are brief diagnostic snapshots, not an exhaustive CPU profile.',
        '', 'Storm `drawBatch.cpp` constructs/resolves binding metadata and creates a code generator before its compiled-program registry lookup. That path also runs for cache hits. The observed `CompileShader` stack therefore does not establish repeated GPU shader compilation or shader-cache misses.',
        '', '## Validation and limits',
        '', '- Final Release build and deployment copied all 72 installed USD DLLs explicitly from the Release SDK; executable and DLL hashes are in `deployment-manifest.json`.',
        '- Three focused native tests passed: render scene-index removal, layer commands, and visibility commands.',
        '- Three focused Qt offscreen tests passed: layer mute/undo/redo, instance-payload resync, and geometry subsets. The initial harness lacked the offscreen plugin path; the successful run explicitly used the installed Qt plugins.',
        '- The revised controller closes the disposable stage before quitting. A final empty-scene MCP smoke run passed and exited without a save prompt. The measured matrix used the older shutdown path; its anonymous-scene prompts were discarded or the owned test process was stopped by the controller.',
        '- External MCP stage/prim/selection oracles and embedded USD/Qt assertions accompany checkpoints. File-backed layers remained clean. Viewer/viewport/layer screenshots were captured; mute and unmute screenshots were inspected.',
        '- The pure USD control and brief stack sampling overlapped parts of the 64 MiB diagnostic. No statistical confidence intervals or cold-cache claims are made.',
        '- A Python USD-notice logger caused a GIL/stage-lock deadlock during worker-thread authoring. It was removed; that run has `excluded.json`. Native scopes and independent external MCP remain.',
        '- GPU resource samples are device-wide `nvidia-smi` readings. Process RSS/private memory are sampled separately. Screenshots show dark source materials under the camera light; these tests do not verify every individual mesh by GPU readback.',
        '- No broad CTest run, Debug build, macOS, UMA, other GPU vendors or other graphics backends were tested in this iteration.',
        '', '## Reproduction and archive',
        '', 'Use `investigation/performance/README.md` in the Stageviz repository. `focused-profiles.json` defines the final matrix: 64 MiB full workflow, 16/256 MiB loading controls, statistics off, and materials off. `profiles.json` provides additional culling and layer-editor examples that are not automatically part of the measured matrix.',
        '', 'Each run contains config, launch provenance, results, native phases, resource samples, screenshots, Qt snapshots and MCP oracles. Immutable source/tool snapshots are under `after/<UTC timestamp>`. Before snapshots include the original source and intermediate fixes. Iterations 1 and 2 preserve earlier missing-geometry investigations and the bounded-staging Windows overlay.',
        '', 'Hardware and driver details are in `hardware.json`. The Windows build overlay preserves the root investigation directory across CLEAN extraction using a checked sibling stash.', '']
text='\n'.join(lines)
(root/'report.md').write_text(text,encoding='utf-8')
(archive/'report.md').write_text(text,encoding='utf-8')
for name in ('composition-control.json','composition-control.log','hardware.json','summary.csv'):
    if (root/name).exists():shutil.copy2(root/name,archive/name)
print(root/'report.md')
