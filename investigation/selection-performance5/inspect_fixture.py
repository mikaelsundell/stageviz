import sys
sys.path.insert(0,'investigation/performance')
import run
print(run.tool('stageviz_run_python',{'code':'''b=stageviz._selectionBenchmark
print(b.timer.isActive(), b.timer.interval(), b.active, b.current, b.directory)
print(b.directory.joinpath('status.json').read_text())
import sys
print(getattr(sys,'last_value',None))
from PySide6.QtWidgets import QApplication
print([(w.objectName(),w.metaObject().className(),w.isVisible()) for w in QApplication.topLevelWidgets()])
print(b.timer.thread())
b.directory.joinpath('excluded.json').write_text('"Fixture controller status-file rename failure; no dataset actions ran"')
b.directory.joinpath('status.json').write_text(b.directory.joinpath('status.tmp').read_text())
'''}))
