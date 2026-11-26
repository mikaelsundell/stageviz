# Python file-format plugins

Stageviz's conversion plugins adapt external formats to USD. The shared registry
lives in `sources/core/fileformats.h/.cpp`; native providers use `registerFormat`.
The direct native `stageviz.plugins.registerFileFormat` binding adapts Python callbacks
into the same core registry. Open/Merge/Export call core C++ functions; selecting a
Python provider invokes its callbacks through that adapter. Python does not own
a separate registration table. Conversion plugins are separate from OpenUSD Sdf
file-format plugins: they do not teach USD itself to resolve external references
or payloads. No Cadio dependency is bundled.

## Register a converter

`stageviz.plugins.registerFileFormat` accepts a display name, a sequence of extensions,
and loader/saver callbacks. At least one callback is required. Registration is
immediate, including after the viewer starts; subsequent file dialogs use the
current registry. Names/extensions must be unique and cannot replace a format
registered with OpenUSD. Native Sdf plugin extensions are discovered directly with
`SdfFileFormat::FindAllFileFormatExtensions`; reading/writing filters use USD's
advertised capabilities without loading the format plugin. Installed native Sdf
formats work through the normal C++ USD path and need no Stageviz adapter.
Native-only Save Layer/Save As and Reference/Payload dialogs also use this discovery.
Configure native USD plugins before launching Stageviz; restart after adding or
changing them because Sdf initializes its native format registry on first use.
Stageviz Python adapter registration remains available during a running session.
`plugins.fileFormats()` lists Stageviz adapter registrations; native Sdf formats
are discovered independently through USD.

```python
import stageviz


def load(source, options):
    # Convert source with your library, returning an in-memory USD stage.
    # This example creates a synthetic USD cube instead of reading a real format.
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Cube.Define(stage, '/Cube')
    return stage


stageviz.plugins.registerFileFormat('Example format', ['example'], loader=load)
```

Callback contracts:

- `loader(source_filename, options) -> Usd.Stage` returns a live USD stage, usually
  created with `Usd.Stage.CreateInMemory()`. Never mutate the active Session from
  a converter. Core captures the composed stage into independent anonymous layers;
  it does not write any intermediate USD file. Invalid return types raise TypeError.
- `saver(stage, destination_filename, options)` receives an isolated, flattened,
  in-memory USD stage and writes one self-contained external file. Return values
  are ignored. Edits to the supplied snapshot do not change the caller's stage.
  The external output location is a temporary sibling directory using the requested
  base filename and extension; it is atomically published only after success.
  Multi-file sidecar exports are not supported yet. No USD file cache is involved.
- `options(filename, operation, parent)` optionally returns a dictionary, or None
  to cancel. `operation` is `open`, `merge`, or `export`. The parent is the viewer
  Qt widget. Use PySide6 or PyQt6 and a modal dialog whose result is returned before
  conversion; do not create another QApplication. Put substantial custom dialogs
  in `.ui` files. UI single-file operations invoke this callback on the main thread.
  Noninteractive API/batch calls skip it and pass an empty dictionary. No dialog
  is generated automatically. Avoid commands/stage edits from options callbacks.

Conversion runs synchronously on the Qt thread. For long operations the adapter
should provide progress/cancellation in its library integration; this initial
API does not run Python callbacks on a worker. Recursive conversions are rejected.
Exceptions propagate to Python; UI calls print tracebacks and report status errors.
Cancellation returns None from `readFile` or False from `writeFile` and changes no stage/file.

## Open, merge and export

Open (including recent files, the command line, OS file-open events and drops)
accepts readable registered extensions. The loader returns a USD stage; Stageviz
adopts an isolated in-memory snapshot as an anonymous USD document. Its title is Untitled,
Save As saves USD, and the original external file is never overwritten by Save.
The original external filename remains in recent files. Resolve all converter
asset paths relative to the external source or use absolute paths in generated
USD. Flattening anchors USD assets, but does not copy texture/resource bytes.
Converter-generated resources must remain available for the document lifetime.

Merge and Merge Flattened convert before dispatching the existing undoable USD
merge commands; redo reuses the snapshot rather than re-running the plugin or
showing options. Reference/Payload remain USD-only because their dependencies
are persistent composition arcs, whereas adapters return imported stage data.
Save an imported document as USD first when you need a durable reference. Save Layer and Save All Layers
also remain USD operations.

Export All, Export Selected and Export Visible list writable plugins. Export
passes an isolated in-memory USD stage to the saver, leaving document identity and dirty
state intact. Include the external extension in the destination filename.
`stageviz.plugins.readFile` and `writeFile` expose the same conversion boundary to
scripts; their default noninteractive behavior is suitable for batch loops.
`Session.load` / `loadFromFile` also import registered external files, with
`interactive=False` by default. Set `interactive=True` for a single-file dialog.
Merge commands skip dialogs; use `plugins.readFile` with interactive=True before
`command.merge_stage` when scripting an interactive merge:

```python
import stageviz
stage = stageviz.plugins.readFile('/path/to/model.obj', interactive=True, operation='merge')
if stage is not None:
    stageviz.command.merge_stage(stage)
```

Stages remain alive through ordinary references. Undoable merges retain their own
snapshot, so the source stage and provider may be released or unregistered before
undo/redo. No temporary USD files or process-lifetime conversion cache are used.
`Session.snapshot()` and `snapshotPaths(paths)` provide isolated all/selected stage
snapshots when scripts need them. The registry's `writeFile(stage, filename)` also
isolates its input automatically. Only actual external export publication uses a
temporary staging file. If a conversion library itself requires USD filenames,
its adapter can manage that implementation detail.


The runnable [Simple Shape adapter](../../scripts/fileformats/simple_shapes.py)
implements load, save and an optional scale dialog for JSON `.svshape` files.
The [Wavefront OBJ adapter](../../support/fileformats/README.md) adds polygon import/export
and a Qt Designer settings dialog with remembered triangulation preferences.
For Cadio, wrap the installed module's actual USD conversion functions with these
same callbacks; no particular Cadio API/version is assumed by the framework.

## Startup scripts

Copy the [startup template](../../scripts/stageviz.py) to either location.
Stageviz runs `stageviz.py` from the executable directory first, then
`~/stageviz.py`, after Python and the viewer exist and before opening the
command-line document. On macOS the executable directory is
`Stageviz.app/Contents/MacOS`. Both files run if present. Each has separate globals
and a correct `__file__`. A failing script prints a traceback and does not prevent
the next script or application startup. Use `--no-startup` to skip both scripts
when diagnosing a plugin problem. Missing files are silently ignored.

A startup file can import adapters from an explicitly configured module directory:

```python
import sys
sys.path.insert(0, '/path/to/my/stageviz/plugins')
import simple_shapes
simple_shapes.register()
```

Registrations are not persisted independently. Edit the startup file and restart
to initialize plugins on every launch. `unregisterFileFormat(name)` removes a registration
in the current process; existing imported stages are unaffected.
