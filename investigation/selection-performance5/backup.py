"""Preserve the existing application fixes and USD overlay before iteration 5."""
from datetime import datetime,timezone
from pathlib import Path
import hashlib,json,shutil,subprocess

app=Path(__file__).resolve().parents[2]
usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
root=usd/'stageviz-investigation/test5-selection-transform'
stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
target=root/'before'/stamp;target.mkdir(parents=True,exist_ok=False)
paths=subprocess.check_output(['git','diff','--name-only'],cwd=app,text=True).splitlines()
paths+=['sources/core/perfscope.h','CTest/benchmark/benchmark_storm_layers.py',
 'sources/rendering/viewportselection.cpp','sources/rendering/viewporttransform.cpp',
 'sources/rendering/viewportgizmo.cpp','sources/core/selectionlist.cpp',
 'sources/usd/primutils.cpp','sources/commands/commandtransform.cpp',
 'sources/commands/commandselection.cpp']
records=[]
def copy(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    records.append({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
for relative in sorted(set(paths)):copy(app/relative,target/'stageviz'/relative)
overlay=Path(r'C:\git\3rdparty\patches\openusd')
for entry in json.loads((overlay/'manifest.json').read_text()):copy(usd/entry['path'],target/'usd'/entry['path'])
shutil.copytree(overlay,target/'windows-overlay',ignore=shutil.ignore_patterns('__pycache__'))
shutil.copytree(app/'investigation/performance',target/'stageviz/investigation/performance',ignore=shutil.ignore_patterns('build','__pycache__'))
copy(app/'deploy.Release/Stageviz.exe',target/'binaries/Stageviz.exe')
marker=app/'deploy.Release/openusd-bounded-staging.json'
if not marker.exists():marker=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release\openusd-bounded-staging.json')
if marker.exists():copy(marker,target/'binaries/openusd-bounded-staging.json')
(target/'manifest.json').write_text(json.dumps(records,indent=2))
print(target)
