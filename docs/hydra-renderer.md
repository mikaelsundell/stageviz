# Independent Hydra rendering

This branch implements phase 1: metadata-only native discovery and its embedded
Python binding. Native render sessions, workers, buffers, and the interactive
Python render window are **not implemented yet**. Storm rendering is unchanged.

## Discovery

Inside Stageviz:

```python
import stageviz
for renderer in stageviz.HydraRenderer.available_renderers():
    print(renderer)
```

Dictionaries contain `id`, `display_name`, `library_path`, `priority`, and
`loaded`, in Hydra registry order. `loaded` reports existing Plug library state;
discovery does not change it. Registration does not establish compatibility,
device support, dependencies, or licensing. No delegates are created during
module initialization or enumeration. `HydraRenderer` cannot yet be instantiated.

Configure `PXR_PLUGINPATH_NAME` before starting Stageviz for compatible builds.
Do not register incompatible Maya plugins just to make RenderMan appear:
registration can affect other USD clients and default renderer selection.

## RenderMan preflight on macOS

Run outside Stageviz, from the repository root:

```sh
python3 scripts/renderman_preflight.py --architecture arm64
```

Use `--json` for structured output. Target defaults are USD 25.11 and Python
3.9; override with `--usd-version` and `--python-version` for another build.
`--architecture` must describe Stageviz, which can differ from the Python
running this tool under Rosetta.

The script inspects matching Maya/Pro Server releases in the default installation
directory, configurable with `--pixar-root`, using Mach-O headers and `nm`/`otool`.
It does not load libraries, request a
license, change plugin search paths, or start rendering. Exit code 1 means no
candidate passed the initial checks. Zero establishes necessary architecture,
USD, and Python conditions only: transitive dependencies, compiler/C++ ABI,
devices, and licensing still require integration validation. Custom delegate
builds outside the Maya layout need separate inspection.

In-process rendering requires matching process architecture, USD/Python ABI,
and runtime dependencies. Rosetta does not resolve USD/Python ABI mismatches.
An external renderer worker would require its own compatible runtime and an
explicit IPC/synchronization design.

## Subsequent implementation phases

1. Reuse Session's stage lock, stage/prims notifications, and ViewCamera's
   GfCamera snapshot. Add a PIMPL/Data-based native session owning an independent
   delegate, render index, scene indices, tasks, buffers, and worker. Never
   borrow Storm resources.
2. Integrate the installed USD 25.11 `UsdImagingCreateSceneIndices`,
   `UsdImagingStageSceneIndex::SetStage`/`ApplyPendingUpdates`,
   `HdRenderIndex::InsertSceneIndex`, and Hydra task/AOV APIs. Verify integration
   against installed headers and each delegate. Avoid reusing RenderEngine's
   configuration: its global scene-index registration can append Stageviz
   viewport filters to other engines. Exclude those filters and auxiliary data.
3. Serialize synchronization/execution on a native worker, using Session's read
   lock for USD access and Hydra synchronization while editing uses the write
   lock. Audit delegate background callbacks and thread affinity before enabling
   it. Coalesce ordinary notices and apply incremental updates. Stage replacement
   detaches/rebinds the chain. Shutdown joins workers before Session destruction.
4. Define live-off behavior with an isolated in-memory stage/layer snapshot,
   captured only on explicit sync/restart. Disabling notices while referencing
   mutable live USD is insufficient.
5. Resolve/map/unmap AOVs into owned CPU snapshots. Keep floats for EXR and
   expose immutable Python bytes with size/format/generation metadata. Throttle
   notifications to roughly 10 Hz, delivered on the Qt thread; generate previews
   separately. Release the GIL around blocking cancellation/join operations.
6. Extend the CPython type with real sessions once lifecycle/cancellation works.
   Add a non-modal PySide6 window parented through Application, a Designer `.ui`
   layout, typed options, cameras, AOVs, exposure, and native image export. Use
   settings for output directories. USD persistence remains a separate undoable
   authoring operation.
7. Validate compatible RenderMan RIS first using descriptor keys. Treat XPU and
   denoising as separate capabilities. Add lifecycle/live/buffer, Qt, embedded
   Python, and optional RenderMan integration regressions as phases become
   executable. Keep viewport rendering independent.

## Current tests

`stageviz.hydra_renderer_discovery` registers metadata for a missing library and
checks repeated enumeration without loading it. No RenderMan or graphics context
is required. `CTest/python/testhydrarenderer.py` checks the binding and unchanged
USD/selection state with Qt snapshots. `python3 CTest/test_renderman_preflight.py`
checks synthetic Mach-O headers and mocked compatibility inspection.
