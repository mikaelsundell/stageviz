"""Exercise public transforms in an owned disposable app, with external oracles."""
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'investigation/performance'))
import run as controller

try:
    controller.rpc('ping',timeout=1)
except Exception:
    pass
else:
    raise RuntimeError('Preserve the existing MCP session; a disposable process needs a free port')

directory=Path(r'C:\Temp\usdtest')/('interaction6-python-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
directory.mkdir()
ctypes.windll.kernel32.SetErrorMode(0x0001|0x0002|0x8000)
env=os.environ.copy()
env['STAGEVIZ_PERF_LOG']=str(directory/'native-phases.jsonl')
process=subprocess.Popen([str(controller.APP),'--execute','import stageviz; stageviz.startMcpServer()'],
                         cwd=controller.APP.parent,env=env,
                         stdout=(directory/'stdout.log').open('wb'),stderr=(directory/'stderr.log').open('wb'))
(directory/'launch.json').write_text(json.dumps(dict(pid=process.pid,exeSHA256=hashlib.sha256(controller.APP.read_bytes()).hexdigest()),indent=2))
def python(code,label):
    result=controller.tool('stageviz_run_python',{'code':code})
    (directory/(label+'-python.json')).write_text(json.dumps(result,indent=2))
    return result
def oracle(label,prims=()):
    result={name:controller.tool(name) for name in ('stageviz_get_stage_info','stageviz_get_selection')}
    for path in prims:
        result[path]=controller.tool('stageviz_get_prim_info',{'path':path})
    (directory/(label+'-oracle.json')).write_text(json.dumps(result,indent=2))
    return result
try:
    deadline=time.monotonic()+60
    while True:
        assert process.poll() is None,'app exited during startup'
        try:
            tools=controller.rpc('tools/list',timeout=2)
            break
        except Exception:
            if time.monotonic()>deadline:
                raise
            time.sleep(0.25)
    (directory/'tools.json').write_text(json.dumps(tools,indent=2))
    oracle('empty')
    script=root/'CTest/python/testtransformoperation.py'
    python(f"import runpy,stageviz; stageviz._interactionTest=runpy.run_path({str(script)!r})['TransformOperationTest'](disposable=True); stageviz._interactionTest.setup()",'setup')
    oracle('setup')
    for mode in ('Move','Rotate','Scale'):
        python(f'stageviz._interactionTest.api({mode!r})','api-'+mode.lower())
        oracle('api-'+mode.lower())
    python('stageviz._interactionTest.conflicts()','conflicts')
    oracle('conflicts')
    python('stageviz._interactionTest.composed_batch()','composed-batch')
    result=oracle('composed-batch',('/Composed/Batch/P0','/Composed/Batch/P127'))
    selection=json.loads(result['stageviz_get_selection']['result']['content'][0]['text'])
    assert selection==['/Composed/Batch/P'+str(i) for i in range(128)]
    python(f"print('SNAPSHOTS',stageviz._interactionTest.directory); stageviz._interactionTest.cleanup()",'cleanup')
    oracle('cleanup')
    (directory/'passed.json').write_text(json.dumps(dict(passed=True,checks=['Move','Rotate','Scale','conflicts','composed128']),indent=2))
    print('PASS Python public transform regressions:',directory,flush=True)
finally:
    if process.poll() is None:
        try:
            python('from PySide6.QtCore import QTimer; from PySide6.QtWidgets import QApplication; stageviz.session().close(); QTimer.singleShot(0,QApplication.quit)','shutdown')
            process.wait(timeout=30)
        except Exception:
            if process.poll() is None:
                process.terminate();process.wait(timeout=10)
