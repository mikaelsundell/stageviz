import json
import os
from pathlib import Path
import stageviz
from pxr import UsdGeom
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QAction
session = stageviz.session()
stage = session.stage()
directory = Path(os.environ['STORM_TRACE_DIR'])
# Route live visibility edits through the public undoable command API,
# targeting a disposable wrapper. Never save the original layer.
assert Path(stage.GetRootLayer().realPath).resolve() == (directory/'visible-wrapper.usda').resolve(), (stage.GetRootLayer().realPath, str(directory))
stageviz.command.show_paths(['/Master'], recursive=True)
meshes = [p for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
hidden = [str(p.GetPath()) for p in meshes if UsdGeom.Imageable(p).ComputeVisibility(0)=='invisible']
assert not hidden, hidden[:10]
stage.GetRootLayer().Save()
view = session.viewState()
view.setSceneMaterialsEnabled(False)
view.setDoubleSidedMode(1)
session.viewState().camera().frame(session.boundingBox())
report = {'meshes':len(meshes), 'hiddenMeshes':len(hidden), 'time':0, 'materials':False, 'doubleSidedMode':1, 'editLayer':stage.GetEditTarget().GetLayer().identifier}
(directory/'normalized.json').write_text(json.dumps(report, indent=2))
print(report)
