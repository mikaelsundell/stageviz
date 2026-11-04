from pathlib import Path
import hashlib
import json
import shutil
from datetime import datetime, timezone

USD = Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
APP = Path(r'C:\git\github\stageviz')
HISTORY = Path(r'C:\Temp\usdtest\storm-investigation')
ROOT = USD/'stageviz-investigation'
ROOT.mkdir(exist_ok=True)
stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
first = ROOT/'test1-buffers'
second = ROOT/'test2-staging'
third = ROOT/'test3-performance'
for folder in (first,second,third): folder.mkdir(exist_ok=True)
if not (first/'imported.json').exists():
    for path in HISTORY.iterdir():
        if path.is_file() or path.name in ('original-dlls','patched-dlls','upstream-upload') or (path.name.startswith('run0') and path.name[3:5] in ('01','02','03','04','05','06','07')):
            target = first/'artifacts'/path.name
            target.parent.mkdir(parents=True,exist_ok=True)
            if path.is_dir(): shutil.copytree(path,target,dirs_exist_ok=True)
            else: shutil.copy2(path,target)
    (first/'imported.json').write_text(json.dumps({'source':str(HISTORY),'importedUtc':stamp},indent=2))
if not (second/'imported.json').exists():
    for name in ('bounded-staging','run08-bounded','run09-bounded','run10-bounded-final'):
        if (HISTORY/name).exists(): shutil.copytree(HISTORY/name,second/'artifacts'/name,dirs_exist_ok=True)
    shutil.copytree(Path(r'C:\git\3rdparty\patches\openusd'),second/'windows-build-overlay',dirs_exist_ok=True)
    (second/'imported.json').write_text(json.dumps({'source':str(HISTORY),'importedUtc':stamp},indent=2))
files = [
 ('usd',USD/relative) for relative in ('pxr/imaging/hdSt/stagingBuffer.cpp','pxr/imaging/hdSt/stagingBuffer.h','pxr/imaging/hdSt/testenv/testHdStStagingBatches.cpp','pxr/imaging/hdSt/CMakeLists.txt','pxr/imaging/hdSt/indirectDrawBatch.cpp','pxr/imaging/hgiGL/ops.cpp','pxr/imaging/hgiGL/buffer.cpp','pxr/imaging/hgi/enums.h')]
files += [('stageviz',APP/relative) for relative in ('sources/core/session.cpp','sources/commands/commandlayer.cpp','sources/outliner/outlinerview.cpp','sources/layers/layertree.cpp','sources/rendering/renderengine.cpp','sources/rendering/viewportwidget.cpp','CMakeLists.txt')]
files += [('thirdparty',Path(r'C:\git\3rdparty\build_win.py'))]
for destination in (second/'source-snapshots'/stamp,third/'before'/stamp):
    destination.mkdir(parents=True,exist_ok=False)
    records=[]
    for family,path in files:
        anchor = {'usd':USD,'stageviz':APP,'thirdparty':Path(r'C:\git\3rdparty')}[family]
        target=destination/family/path.relative_to(anchor)
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,target)
        records.append({'family':family,'path':str(path),'snapshot':str(target),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    (destination/'manifest.json').write_text(json.dumps(records,indent=2))
root_readme='''# Stageviz / Storm investigation

Each iteration preserves source snapshots, patches, build/binary provenance,
test settings, results and screenshots. Never overwrite earlier snapshots.

- test1-buffers: imported original missing-geometry investigation and upstream experiments.
- test2-staging: bounded 64 MiB staging fix, native regressions and Windows build overlay.
- test3-performance: two-layer loading/mute/unmute timings and fresh-process Storm settings.

Live run output is written under C:/Temp/usdtest and copied into its iteration.
Shared datasets are read-only test inputs; never save authored changes into them.
Settings cached by USD must be changed before launching a fresh app process.
OS/network caches are not cleared: label first and later loads without calling them cold.
'''
if not (ROOT/'README.md').exists():
    (ROOT/'README.md').write_text(root_readme)
print(json.dumps({'root':str(ROOT),'snapshot':stamp,'performance':str(third)},indent=2))
