"""Archive iteration-six sources, exact binaries, and independently checked runs."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

app=Path(__file__).resolve().parents[2]
usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
root=usd/'stageviz-investigation/test6-interaction-utilities'
after=root/'after'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
after.mkdir(parents=True)
records=[]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
    records.append(dict(source=str(source),archive=str(target.relative_to(root)),sha256=sha(source)))
paths=subprocess.check_output(['git','diff','f991278','--name-only'],cwd=app,text=True).splitlines()
for relative in paths:
    if (app/relative).is_file():
        copy(app/relative,after/'stageviz'/relative)
shutil.copytree(app/'investigation/interaction-utilities6',after/'stageviz/investigation/interaction-utilities6',dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('__pycache__'))
(after/'changes.patch').write_bytes(subprocess.check_output(['git','diff','f991278','--binary'],cwd=app))
(after/'git-log.txt').write_bytes(subprocess.check_output(['git','log','-4','--format=fuller'],cwd=app))
(after/'git-status.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=app))

overlay=Path(r'C:\git\3rdparty\patches\openusd')
shutil.copytree(overlay,after/'windows-overlay',ignore=shutil.ignore_patterns('__pycache__'))
for entry in json.loads((overlay/'manifest.json').read_text()):
    source=usd/entry['path']
    assert hashlib.sha256(source.read_text(encoding='utf-8').encode()).hexdigest()==entry['patched'],entry['path']
    copy(source,after/'usd'/entry['path'])
    if not (root/'before/usd'/entry['path']).exists():
        copy(source,root/'before/usd'/entry['path'])
exe=app/'deploy.Release/Stageviz.exe'
assert sha(exe)==sha(app/'build.Release/Release/Stageviz.exe')
copy(exe,root/'binaries'/sha(exe)/exe.name)
copy(app/'deploy.Release/openusd-bounded-staging.json',root/'binaries'/sha(exe)/'openusd-bounded-staging.json')
libraries=[]
for source in sorted((sdk/'lib').glob('usd_*.dll')):
    assert sha(source)==sha(app/'deploy.Release'/source.name)
    libraries.append(dict(name=source.name,sha256=sha(source)))
(root/'deployment-verification.json').write_text(json.dumps(dict(exeSHA256=sha(exe),usdDLLs=libraries),indent=2))

verified=[];partial=[]
for directory in sorted(Path(r'C:\Temp\usdtest\performance').iterdir()):
    if not any(name in directory.name for name in ('-indexed-loops','-direct-delete','-sorted-roots')) or not (directory/'launch.json').exists():
        continue
    launch=json.loads((directory/'launch.json').read_text())
    for dataset in launch['datasets']:
        stat=Path(dataset['path']).stat()
        assert stat.st_size==dataset['bytes'] and stat.st_mtime_ns==dataset['mtime_ns']
    status=json.loads((directory/'status.json').read_text())
    rows=json.loads((directory/'results.json').read_text())
    audited=[]
    for row in rows:
        oracle=directory/(row['phase']+'-oracle.json')
        if not oracle.exists():
            continue
        assert row['verified'] and not row['dirty_file_layers']
        result=json.loads(oracle.read_text())
        decode=lambda key:json.loads(result[key]['result']['content'][0]['text'])
        assert decode('stageviz_get_selection')==row['selection']
        assert decode('stageviz_get_stage_info')['layerCount']==len(row['layer_stack'])
        if row['phase'] in ('undo_drag','restore_drag'):
            assert row['root_restored'] and row['authored_values_restored']
        audited.append(row['phase'])
    record=dict(run=directory.name,exeSHA256=launch['exeSHA256'],state=status['state'],
                independentMCPMatches=True,sourceMetadataUnchanged=True,verifiedPhases=audited)
    if status['state']=='complete':
        assert len(audited)==len(rows)
        assert rows[-1]['phase']=='reload_baseline' and rows[-1]['root_restored'] and not rows[-1]['selection']
        verified.append(record)
    else:
        record['error']=status.get('error','')[:1000]
        partial.append(record)
    shutil.copytree(directory,root/'runs'/directory.name,dirs_exist_ok=True)
(root/'checkpoint-verification.json').write_text(json.dumps(verified,indent=2))
(root/'partial-checkpoint-verification.json').write_text(json.dumps(partial,indent=2))
for directory in Path(r'C:\Temp\usdtest').glob('interaction6-python-*'):
    shutil.copytree(directory,root/'python-tests'/directory.name,dirs_exist_ok=True)
    cleanup=directory/'cleanup-python.json'
    if cleanup.exists():
        result=json.loads(cleanup.read_text())
        for block in result.get('result',{}).get('content',[]):
            for line in block.get('text','').splitlines():
                if line.startswith('SNAPSHOTS '):
                    artifacts=Path(line[len('SNAPSHOTS '):])
                    if artifacts.is_dir():
                        shutil.copytree(artifacts,root/'python-tests'/directory.name/'artifacts',dirs_exist_ok=True)
for source in (app/'build.Release').glob('interaction-*.log'):
    copy(source,root/'logs'/source.name)
for name in ('report.md','audit.md','measurements.json','measurements.csv'):
    if (app/'investigation/interaction-utilities6'/name).exists():
        copy(app/'investigation/interaction-utilities6'/name,root/name)
index=usd/'stageviz-investigation/README.md'
if 'test6-interaction-utilities' not in index.read_text(encoding='utf-8'):
    copy(index,root/'before/documentation/README.md')
    with index.open('a',encoding='utf-8') as stream:
        stream.write('\n## Iteration 6\n\n[test6-interaction-utilities](test6-interaction-utilities/report.md) records shared path utilities, safe batched authoring, bounds invalidation, exact spec cleanup, and live Volvo interaction comparisons.\n')
(after/'manifest.json').write_text(json.dumps(records,indent=2))
print('Archived',after,'verified runs',len(verified),'partial',len(partial),'matching USD DLLs',len(libraries))
