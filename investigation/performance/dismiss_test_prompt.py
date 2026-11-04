"""Finish the already-running old controller's final anonymous-fixture prompt."""
from pathlib import Path
import json
import sys
import time
from run import tool

directory=Path(sys.argv[1])
launch=json.loads((directory/'launch.json').read_text())
while True:
    state=json.loads((directory/'status.json').read_text())
    if state.get('state')=='complete':break
    if state.get('state')=='failed':raise RuntimeError(state)
    time.sleep(.5)
# All measured checkpoints and independent oracles are now complete. Only this
# known disposable process and its anonymous, clean-file-backed fixture qualify.
code=f'''
import os
from PySide6.QtWidgets import QApplication, QAbstractButton, QLabel
assert os.getpid()=={launch['pid']}
s=stageviz.session()
stage=s.stage()
if stage:
    assert stage.GetRootLayer().anonymous
    assert not any(layer.dirty for layer in stage.GetUsedLayers() if not layer.anonymous)
for window in QApplication.topLevelWidgets():
    if window.objectName()!='MessageDialog' or not window.isVisible():
        continue
    labels=' '.join(label.text() for label in window.findChildren(QLabel))
    if 'Save changes to Untitled?' not in labels:
        continue
    for button in window.findChildren(QAbstractButton):
        if button.text().replace('&','')=="Don't Save":
            s.viewState().setSceneMaterialsEnabled(True)
            s.viewState().setSceneStatsEnabled(True)
            button.click()
            print('TEMP_FIXTURE_DISCARDED')
'''
deadline=time.monotonic()+120
while time.monotonic()<deadline:
    try:
        response=tool('stageviz_run_python',{'code':code})
    except Exception as error:
        print('Test app already closing:',error,flush=True);break
    if 'TEMP_FIXTURE_DISCARDED' in json.dumps(response):
        print('Discarded only the final anonymous fixture; no dataset saved',flush=True);break
    time.sleep(.5)
