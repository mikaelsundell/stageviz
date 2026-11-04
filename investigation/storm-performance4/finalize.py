"""Archive iteration 4 source, exact binaries, logs, and independently checked runs."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

app=Path(__file__).resolve().parents[2]
usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
root=usd/'stageviz-investigation/test4-storm-cpu'
temporary=Path(r'C:\Temp\usdtest')
stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
after=root/'after'/stamp
after.mkdir(parents=True,exist_ok=False)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
records=[]
def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
    records.append({'source':str(source),'archive':str(target.relative_to(root)), 'sha256':sha(source)})

index=usd/'stageviz-investigation/README.md'
if index.exists() and 'test4-storm-cpu' not in index.read_text(encoding='utf-8'):
    previous=root/'before/documentation/README.md'
    previous.parent.mkdir(parents=True,exist_ok=True)
    assert not previous.exists(), 'Refuse to overwrite earlier documentation snapshot'
    shutil.copy2(index,previous)
    with index.open('a',encoding='utf-8') as output:
        output.write('\n## Iteration 4\n\n[test4-storm-cpu](test4-storm-cpu/report.md) adds optional native CPU timings and bounded topology cleanup. Eager cleanup remains the default after a slower full-drain result.\n')
if index.exists():copy(index,after/'investigation-README.md')

overlay=Path(r'C:\git\3rdparty\patches\openusd')
for entry in json.loads((overlay/'manifest.json').read_text()):
    copy(usd/entry['path'],after/'usd'/entry['path'])
changed=subprocess.check_output(['git','diff','--name-only'],cwd=app,text=True).splitlines()
changed+=['sources/core/perfscope.h','CTest/benchmark/benchmark_storm_layers.py']
for relative in sorted(set(changed)):
    copy(app/relative,after/'stageviz'/relative)
for relative in ('investigation/performance','investigation/storm-performance4'):
    shutil.copytree(app/relative,after/'stageviz'/relative,ignore=shutil.ignore_patterns('build','__pycache__'))
shutil.copytree(overlay,after/'windows-overlay',ignore=shutil.ignore_patterns('__pycache__'))
copy(Path(r'C:\git\3rdparty\build_win.py'),after/'build_win.py')
(after/'stageviz-working-tree.diff').write_bytes(subprocess.check_output(['git','diff','--'],cwd=app))

binaries=root/'binaries'/sha(app/'deploy.Release/Stageviz.exe')
binaries.mkdir(parents=True,exist_ok=True)
copy(app/'deploy.Release/Stageviz.exe',binaries/'Stageviz.exe')
dlls=[]
for source in sorted((sdk/'lib').glob('usd_*.dll')):
    deployed=app/'deploy.Release'/source.name
    assert sha(source)==sha(deployed),source
    copy(source,binaries/source.name)
    dlls.append({'name':source.name,'sha256':sha(source)})
assert sha(app/'build.Release/Release/Stageviz.exe')==sha(app/'deploy.Release/Stageviz.exe')
copy(app/'deploy.Release/openusd-bounded-staging.json',binaries/'openusd-bounded-staging.json')
test=app/'investigation/storm-performance4/build/Release/testHdInstanceRegistryBudget.exe'
copy(test,root/'test-binaries'/sha(test)/test.name)
(root/'deployment-verification.json').write_text(json.dumps({'exeSHA256':sha(binaries/'Stageviz.exe'),'usdDLLs':dlls},indent=2))
(root/'deployment-manifest.json').write_text(json.dumps({'exe':str(app/'deploy.Release/Stageviz.exe'),'exeSHA256':sha(binaries/'Stageviz.exe'),'usd':dlls},indent=2))

# The original deployment helper wrote its latest manifest into iteration 3.
# Keep that incidental output here and restore iteration 3 from its immutable
# binary backup, cross-checking the earlier independent verification record.
previous=usd/'stageviz-investigation/test3-performance'
manifest=previous/'deployment-manifest.json'
baseline=root/'baseline-binaries'
verification=json.loads((previous/'deployment-verification.json').read_text())
assert sha(baseline/'Stageviz.exe')==verification['exeSHA256']
expected={entry['name']:entry['sdkSHA256'] for entry in verification['usdDLLs']}
baseline_dlls=[]
for source in sorted(baseline.glob('usd_*.dll')):
    assert sha(source)==expected[source.name]
    baseline_dlls.append({'name':source.name,'sha256':sha(source)})
copy(manifest,after/'deployment-helper-iteration3-output.json')
manifest.write_text(json.dumps({'exe':str(app/'deploy.Release/Stageviz.exe'),'exeSHA256':sha(baseline/'Stageviz.exe'),'usd':baseline_dlls},indent=2))

verified=[]
def copy_changed(source,target):
    source,target=Path(source),Path(target)
    if target.exists():
        left,right=source.stat(),target.stat()
        if (left.st_size,left.st_mtime_ns)==(right.st_size,right.st_mtime_ns):return str(target)
    return shutil.copy2(source,target)
for directory in sorted((temporary/'performance').iterdir()):
    if not directory.is_dir() or not (directory/'launch.json').exists():continue
    launch=json.loads((directory/'launch.json').read_text())
    if not launch['config'].get('name') and not ('storm-' in directory.name or 'fixture-' in directory.name):continue
    if (directory/'excluded.json').exists() or launch['config'].get('smoke'):continue
    status=json.loads((directory/'status.json').read_text())
    if status['state']!='complete':continue
    results=json.loads((directory/'results.json').read_text())
    for row in results:
        assert row['verified'] and not row['dirty_file_layers'],row['phase']
        oracle=json.loads((directory/(row['phase']+'-oracle.json')).read_text())
        decode=lambda name:json.loads(oracle[name]['result']['content'][0]['text'])
        assert decode('stageviz_get_stage_info')['layerCount']==len(row['layer_stack'])
        assert decode('stageviz_get_selection')==row['selection']
    for dataset in launch['datasets']:
        stat=Path(dataset['path']).stat()
        assert stat.st_size==dataset['bytes'] and stat.st_mtime_ns==dataset['mtime_ns']
    if 'fixture-budget' in directory.name:
        assert next(row for row in results if row['phase']=='mute_front_1')['render_converged'] is False
        assert next(row for row in results if row['phase']=='settle_after_mute')['render_converged'] is True
    if 'fixture-pressure' in directory.name:
        events=[json.loads(line) for line in (directory/'storm-phases.jsonl').read_text().splitlines()]
        assert any(event.get('forced_eager') for event in events)
    verified.append({'run':directory.name,'checkpoints':len(results),'independentMCPStateMatches':True,'sourceLayerMetadataUnchanged':True})
    shutil.copytree(directory,root/'runs'/directory.name,dirs_exist_ok=True,copy_function=copy_changed)
(root/'checkpoint-verification.json').write_text(json.dumps(verified,indent=2))

logs=root/'build-and-test-logs';logs.mkdir(exist_ok=True)
for pattern in ('storm-cpu-*.log','storm-overlay-*.log'):
    for source in temporary.glob(pattern):copy(source,logs/source.name)
for source in (temporary/'performance/storm-cpu-symbol-maps').iterdir():
    if source.suffix not in ('.dll','.exe','.exp','.lib'):
        copy(source,root/'symbol-maps'/source.name)
for source in (temporary/'performance').glob('storm-cpu-*.csv'):
    copy(source,root/source.name)
copy(app/'investigation/storm-performance4/report.md',root/'report.md')
copy(app/'investigation/storm-performance4/README.md',root/'settings.md')
(after/'manifest.json').write_text(json.dumps(records,indent=2))
print('Archived',after,'; verified',len(dlls),'SDK DLLs and',len(verified),'complete runs')
