# Selection and transform investigation, iteration 5

Use the first Volvo dataset, a disposable Stageviz process, and the external MCP
controller. No shared source layer is saved. All authoring is confined to an
anonymous root. `STAGEVIZ_PERF_LOG` enables optional Stageviz timings and viewport
diagnostic properties. Normal app launches leave them disabled.

Generate profiles with `profiles.py`, then run with the SDK Python 3.9 interpreter:

```powershell
python investigation/selection-performance5/run.py --settings investigation/selection-performance5/profiles.json --storm-log
```

The full profile measures cold/warm rectangle selection, invert/undo, Move tool
activation, actual center-handle press, two movement events, release, undo/redo,
and reload. It requires a valid OpenGL viewport and rejects invalid contexts.
Move events use Qt mouse events sent to the actual viewport handler; press and
release use QTest. This is Qt input automation, not a physical OS mouse test.

`cpu-profiles.json` and `cpu-invert-large.json` measure deterministic selections
of 100, 1,000 and 10,000 leaf paths and their exact inversion complement.
`cpu-transform.json` selects visible boundable geometry for a separate native
CPU preparation/drag probe. These controls explicitly exclude GPU rendering.
The CPU pivot probe temporarily enables mouse tracking to invoke the native
hover/frame-preparation path, then restores it and uses the reported pivot.

`fixture.py` generates a local test grid. `cpu-fixture.json` and
`cpu-transform-fixture.json` validate orchestration, selection, real transform
events, and cleanup before testing the shared asset.

`graphics-parent-control.json` selects a small existing-matrix-transform parent
inside the complete Volvo stage. It supplies successful rendered drag/undo/redo
timings when the bulk leaf-geometry attempt encounters the change-block defect.
This control is not equivalent to the bulk selection. See the connected-session
follow-up in `report.md` for the distinction and measured results.

MCP bootstrap runs once. The external controller independently captures stage,
prim and selection state at checkpoints; no embedded test calls its own server.
Viewer screenshots and Qt snapshots are recorded after the measured span.
The controller closes only the process it created. Do not use the fixture
inspection scripts against a user session: those diagnostics clear undo history.

`analyse.py` writes `C:\Temp\usdtest\performance\selection5-summary.csv`.
`finalize.py` preserves source snapshots, logs, exact Release binary hashes,
completed checks and excluded attempts under the USD source root's
`stageviz-investigation/test5-selection-transform` directory.

See `report.md` for measured findings, the unavailable OpenGL boundary, and the
observed change-block/undo issues. This iteration adds diagnostics and tests;
it does not replace the selection or transform algorithms.
