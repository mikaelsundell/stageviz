# Large-data pipeline audit

These external controllers launch disposable macOS Stageviz processes. They never
connect to the user's running app. Build the application with the normal release
pipeline, preserving an immutable baseline application for comparisons:

```sh
DYLD_IMAGE_SUFFIX=_debug ./build_app.sh release --github
```

`../mcp_runtime.py` owns each process, confirms its native MCP tools list, records
the executable hash and environment, samples RSS/CPU/system memory pressure, and
closes the process on completion or failure. The dedicated MCP port must be free.
The macOS bootstrap uses the current native Application/MCP ABI; fail rather than
substituting a user application when that ABI is unavailable. The finite activity
scope prevents App Nap during measurements. It does not disable idle system sleep.
Native macOS preferences remain shared between app processes.

## Correctness pipeline

```sh
python3 CTest/benchmark/pipeline/run_regressions.py \
  --app /absolute/path/Stageviz.app/Contents/MacOS/stageviz \
  --output /absolute/path/new-results-directory
```

This runs the existing embedded Python API, UI, layer, transform, pivot, shared
source, light, numeric editor, capture, HDRI, scene statistics, axis sizing and
console suites. Every suite gets a fresh app. External MCP stage/selection
snapshots bracket its phases. Explicit Python completion markers catch assertions
that the embedded interpreter reports as ordinary text rather than MCP errors.
Use `--suites` to select suites; run `--help` for the accepted syntax.
`--suites testpayloadhierarchy` exercises a disposable 15-file/four-level payload
fixture with real Outliner checkboxes, nested load-rule undo/redo, references,
instance payloads, overlapping targets, root/group undo, queued click bursts,
menu actions, filtering/collapse, row selection, namespace changes, fixed-seed
commands, load-policy changes and reload in Payloads mode. Checkpoints assert all
visible gate and grouping states; external MCP prim-info queries verify every
visible gate and relevant hidden nested payloads independently. Use `--port`
(default 37657) to give concurrent disposable runs independent MCP listeners.

Run the native and offscreen GUI CTests separately. Embedded Python tests do not
replace native coverage, and their own historical performance section is disabled
here so correctness and controlled performance measurements stay separate.

## Outliner, property and render pipeline

```sh
python3 CTest/benchmark/pipeline/run_namespace.py \
  --app /absolute/path/Stageviz.app/Contents/MacOS/stageviz \
  --fixture /absolute/path/complex.usdc \
  --root /World --branch /World/Assembly --max-selection 250000 \
  --label baseline --output /absolute/path/new-baseline-directory
```

Choose an editable root and authored branch from a trusted fixture. The workflow
selects fixed-seed mesh sets, repeats selection, refreshes properties, tests a
parent/child selection, performs inline renames, and dispatches native Qt tree
drop events for single/multiple reparenting. It checks world-transform preservation
under a rotated, non-uniformly scaled destination, undo/redo, invalid descendant
drops, root namespace edits, materials and wire/shaded rendering. Default prim
rename protection is checked before clearing that metadata through a command in
the disposable stage. These are native handler tests, not OS mouse trajectories.

Use QModelIndex/proxy tree access from the existing test UI helpers. Retaining
Python wrappers for native pointer-backed Outliner items across updates is unsafe.
Selection comes from Session/MCP, not QTreeWidgetItem.isSelected().

Every important phase checks composed USD and external MCP state. Render captures
must contain visible paths that resolve in the current stage. Viewer images and
plain-data Qt snapshots record presentation. Final cleanup reloads the original
file, verifies prim count/default prim/clean root and empty selection/history.
The fixture is never saved, and its on-disk SHA is checked even on failure.

Add `--selection-only --property-pane-width 360 --render-repeats 3` for a
focused visible Property Editor run. It collapses and expands multi-selection
sections, records displayed child rows, and verifies expansion survives visibility
refresh and undo. Add `--render-only --render-repeats 5` for repeated materials,
wireframe and shaded transitions with a small fixed-seed selection and the full
viewport. These modes also reload and verify the untouched source at cleanup.

`run_modifiers.py --app APP --fixture GENERATED_SCENE --label LABEL --output DIR`
uses the same external controller for real Qt Shift-sweep events on the generated
large `/World` fixture. It checks identical/overlapping/full-viewport and empty-hit
sweeps plus selection undo/redo. An ordered symmetric-difference oracle, computed
outside the recorded timing spans, checks every selected path rather than only
counts. The current empty-hit clearing behavior is preserved explicitly.

`run_bounds.py --app APP --fixture FILE --root ROOT --branch BRANCH --output DIR`
measures first and repeated default-time selection bounds for a branch, a root,
a fixed-seed mesh sample, and overlapping selections. Each result is checked
against a fresh USD cache using the existing framing traversal. Synchronous
transform edits, undo, and redo must invalidate cached results. Reload cleanup
checks the clean source and external MCP state. The first query for each case
may reuse entries from earlier cases; it is not necessarily a fully cold cache.
Use fresh app processes and compare identical ordered cases and bounds.

Run the baseline and candidate serially with identical fixtures, viewport sizes,
environment and scripts. Do not build or run other benchmarks concurrently.

```sh
python3 CTest/benchmark/pipeline/compare.py \
  /absolute/path/baseline /absolute/path/candidate /absolute/path/comparison.md
```

The comparison refuses incomplete runs or mismatched selection, widget geometry,
property sections, action state, persistent root index, child ordering and cleanup
state. New runs also record and compare camera framing, projection, clipping and
render settings through the shared sweep helper. Earlier run artifacts retain
their original fields; compare each pair using the same harness revision.
It also supports the existing `sweep_scale/controller.py` outputs.
Response means Session completion and three viewport presentations; end-to-end
adds a fixed settle period. Native scopes are inclusive. Captures, assertions and
external queries are outside the timed spans. Report single samples as such;
repeat marginal or unexpected results before claiming a speedup or regression.

Each disposable process writes `process-outcome.json` at cleanup. It records the
exit status before cleanup, watchdog errors, and the final cleanup exit status.
A negative pre-cleanup status identifies a signal that terminated the app;
`null` means it was still running when cleanup began. Keep failed runs and retry
results separate: a successful retry does not explain an earlier crash or turn
the original run into a clean pass.

The sweep-scale base retains the owned viewer's viewport QObject wrapper and
reconnects presentation tracking if that widget changes. Viewport lookup uses the
viewer subtree instead of wrapping every QApplication widget inside timed input
spans. Compare runs made with the same harness revision.
