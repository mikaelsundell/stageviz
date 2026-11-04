"""Append an immutable source/tools snapshot to the USD investigation archive."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess

app=Path(__file__).resolve().parents[2]
usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
archive=usd/'stageviz-investigation/test3-performance'
target=archive/'after'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
target.mkdir(parents=True,exist_ok=False)
files=[]
changed=subprocess.check_output(['git','diff','--name-only'],cwd=app,text=True).splitlines()
changed+=['sources/core/perfscope.h','CTest/benchmark/benchmark_storm_layers.py']
changed+=[str(path.relative_to(app)) for path in (app/'investigation/performance').rglob('*') if path.is_file() and not any(part in ('build','__pycache__') for part in path.parts)]
for relative in sorted(set(changed)):
    source=app/relative
    destination=target/'stageviz'/relative
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    files.append({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
for relative in ('pxr/imaging/hdSt/stagingBuffer.cpp','pxr/imaging/hdSt/stagingBuffer.h','pxr/imaging/hdSt/CMakeLists.txt','pxr/imaging/hdSt/testenv/testHdStStagingBatches.cpp'):
    source=usd/relative;destination=target/'usd'/relative
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    files.append({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
third=Path(r'C:\git\3rdparty')
shutil.copy2(third/'build_win.py',target/'build_win.py')
shutil.copytree(third/'patches/openusd',target/'windows-build-overlay',ignore=shutil.ignore_patterns('__pycache__'))
(target/'manifest.json').write_text(json.dumps(files,indent=2))
(target/'stageviz-tracked.patch').write_bytes(subprocess.check_output(['git','diff','--binary'],cwd=app))
(target/'git-head.txt').write_bytes(subprocess.check_output(['git','rev-parse','HEAD'],cwd=app))
binary=app/'deploy.Release/Stageviz.exe'
binary_hash=hashlib.sha256(binary.read_bytes()).hexdigest()
binary_dir=archive/'binaries'/binary_hash
binary_dir.mkdir(parents=True,exist_ok=True)
for source in (binary,app/'deploy.Release/usd_hdSt.dll',app/'deploy.Release/openusd-bounded-staging.json'):
    destination=binary_dir/source.name
    if destination.exists():
        assert destination.read_bytes()==source.read_bytes(),destination
    else:shutil.copy2(source,destination)
shutil.copy2(archive/'deployment-manifest.json',binary_dir/'deployment-manifest.json')
# Tracked test/documentation files were clean before this iteration. Preserve
# their original repository content alongside the live before snapshots.
baseline=archive/'before-git-head'
for relative in changed:
    if not (app/relative).is_file():continue
    destination=baseline/relative
    if destination.exists():continue
    original=subprocess.run(['git','show','HEAD:'+relative.replace('\\','/')],cwd=app,capture_output=True)
    if original.returncode:continue
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(original.stdout)
# Keep the exact embedded script beside each run when its recorded hash matches
# this snapshot. Older source variants remain recoverable from after snapshots.
script=app/'CTest/benchmark/benchmark_storm_layers.py'
script_hash=hashlib.sha256(script.read_bytes()).hexdigest()
for run in Path(r'C:\Temp\usdtest\performance').iterdir():
    if not run.is_dir() or not (run/'launch.json').exists():continue
    provenance=json.loads((run/'launch.json').read_text())
    if provenance.get('scriptSHA256')==script_hash:
        shutil.copy2(script,run/'benchmark-used.py')
print(target)
