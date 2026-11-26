# Wavefront OBJ sample plugin

A runnable, dependency-free Stageviz loader/saver example. It uses the bundled
OpenUSD and Qt modules. Keep this folder intact, including `objoptions.ui`.

Add the repository's **support directory** to Python's module path, then register:

```python
import sys
sys.path.insert(0, '/path/to/stageviz/support')
import fileformats
fileformats.register()
```

Put this in `~/stageviz.py` (or beside the executable) to register on every
launch. OBJ then appears in Open, Merge, and Export All/Selected/Visible. The
callbacks are `objloader.load` and `objsaver.save`; `objformat.register` connects
both and the settings callback to `stageviz.plugins`.

The sample dialog has a **Triangulate all faces** checkbox. Accepted values are
stored in Stageviz's QSettings under `fileFormats/WavefrontOBJ`, separately for
import and export, and restored on subsequent launches. Open and Merge share
import preferences. Cancel changes neither the document nor remembered options.
The dialog lives in Qt Designer's `objoptions.ui` and supports PySide6/PyQt6.

Noninteractive calls never open a dialog and default to preserving polygons;
they do not inherit saved UI preferences. Scripted conversion can pass options
directly to `stage = objloader.load(source, {'triangulate': True})` or
`objsaver.save(stage, destination, {'triangulate': True})`. Use the registry's
`stageviz.plugins.writeFile` function for atomic output replacement; calling the saver directly writes
to the supplied destination immediately. Loaders return in-memory USD stages and
savers consume stages; neither sample writes an intermediate USD file.

Supported geometry:

- Import polygon faces, separate object/group batches, positive/negative indices,
  homogeneous vertices, UVs and normals. Each batch becomes a polygon Mesh below
  `/World`, with Y up and meters as the sample's explicit unit convention (OBJ
  does not encode these). Material libraries are ignored with a warning.
- Export visible polygon Mesh control cages at default time, baking world-space
  transforms and expanding instances. UVs and built-in normals are preserved;
  holes are omitted and reflected/left-handed meshes have corrected winding.
- Optional triangulation handles simple planar convex and concave polygons while
  retaining corner UV/normal assignments. Degenerate or unsupported polygons may
  be rejected; this is not a mesh-repair tool.

This is a geometry adapter, not a full OBJ/MTL implementation: no materials,
textures, smoothing-group conversion, vertex colors, curves, animation or automatic
axis/unit conversion. Mixed missing UV/normal indices cause that channel to be
omitted for the affected mesh with a warning. Analytic USD geometry must be
converted to Mesh before exporting. See the [framework guide](../../skills/references/python-file-formats.md).
