import json
import os
from pathlib import Path
import sys
import stageviz
from pxr import UsdGeom
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QAction
sys.path.insert(0, r'C:\git\github\stageviz\CTest\python')
import testbench as tb
directory = Path(os.environ['STORM_TRACE_DIR'])
session = stageviz.session()
target = os.environ['STORM_TRACE_PRIM']
stage = session.stage()
prim = stage.GetPrimAtPath(target)
assert prim
session.viewState().camera().frame(session.boundingBox())
report = {'target':target, 'visibility':str(UsdGeom.Imageable(prim).ComputeVisibility(0)), 'root':stage.GetRootLayer().identifier}
(directory/'baseline.json').write_text(json.dumps(report, indent=2))
print(report)
