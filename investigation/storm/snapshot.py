import json
import contextlib
import io
import os
from pathlib import Path
import stageviz
import sys
sys.path.insert(0, r'C:\git\github\stageviz\CTest\python')
import testbench as tb
from PySide6.QtWidgets import QApplication
directory = Path(os.environ['STORM_TRACE_DIR'])
target = os.environ['STORM_TRACE_PRIM']
label = os.environ.get('STORM_CAPTURE_LABEL', 'frame-all')
viewer = tb.viewer()
assert viewer.grab().save(str(directory/(label+'-viewer.png')))
viewport = next(w for w in QApplication.allWidgets() if w.objectName()=='imagingGLWidget' and w.isVisible())
assert viewport.grab().save(str(directory/(label+'-viewport.png')))
with contextlib.redirect_stdout(io.StringIO()):
    qt_state = tb.snapshot_qt_state('frame-all')
(directory/(label+'-qt.json')).write_text(json.dumps(qt_state, indent=2))
(directory/(label+'-target-hydra.json')).write_text(json.dumps(stageviz.render().inspectPrim(target), indent=2))
print('Saved screenshot, Qt state and target Hydra state')
