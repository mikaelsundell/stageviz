from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess

usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
app=Path(r'C:\git\github\stageviz')
root=usd/'stageviz-investigation/test4-storm-cpu'
stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
destination=root/'before'/stamp
destination.mkdir(parents=True,exist_ok=False)
paths=['pxr/imaging/hd/instanceRegistry.h','pxr/imaging/hd/CMakeLists.txt',
       'pxr/imaging/hdSt/resourceRegistry.cpp','pxr/imaging/hdSt/resourceRegistry.h',
       'pxr/imaging/hdSt/renderDelegate.cpp','pxr/imaging/hdSt/renderPass.cpp','pxr/imaging/hdSt/renderPass.h',
       'pxr/imaging/hdSt/drawBatch.cpp','pxr/imaging/hdSt/commandBuffer.cpp','pxr/imaging/hdSt/CMakeLists.txt',
       'pxr/imaging/hdSt/stagingBuffer.cpp','pxr/imaging/hdSt/stagingBuffer.h','pxr/imaging/hdSt/testenv/testHdStStagingBatches.cpp']
records=[]
def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    records.append({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
for relative in paths:copy(usd/relative,destination/'usd'/relative)
for relative in subprocess.check_output(['git','diff','--name-only'],cwd=app,text=True).splitlines()+['sources/core/perfscope.h','sources/rendering/renderengine.h','CTest/benchmark/benchmark_storm_layers.py']:
    copy(app/relative,destination/'stageviz'/relative)
shutil.copytree(app/'investigation/performance',destination/'stageviz/investigation/performance',ignore=shutil.ignore_patterns('build','__pycache__'))
shutil.copytree(Path(r'C:\git\3rdparty\patches\openusd'),destination/'windows-overlay',ignore=shutil.ignore_patterns('__pycache__'))
copy(Path(r'C:\git\3rdparty\build_win.py'),destination/'build_win.py')
binaries=root/'baseline-binaries';binaries.mkdir(exist_ok=False)
for source in (app/'deploy.Release').glob('usd_*.dll'):copy(source,binaries/source.name)
copy(app/'deploy.Release/Stageviz.exe',binaries/'Stageviz.exe')
copy(app/'deploy.Release/openusd-bounded-staging.json',binaries/'openusd-bounded-staging.json')
(destination/'manifest.json').write_text(json.dumps(records,indent=2))
(root/'README.md').write_text('''# Storm CPU investigation, iteration 4

Preserves iteration 3 before adding opt-in aggregate Storm instrumentation and
bounded topology garbage collection. Defaults retain eager reclamation until
the experimental mode has been validated. Shared datasets are never saved.

GPU-related topology references are reclaimed on the render thread. Scheduling
and destruction are measured separately from first responsive presentation.
''')
print(destination)
