from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,shutil,subprocess

app=Path(__file__).resolve().parents[2]
usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
root=usd/'stageviz-investigation/test5-selection-transform'
temp=Path(r'C:\Temp\usdtest')
after=root/'after'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
after.mkdir(parents=True,exist_ok=False)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
records=[]
def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    records.append({'source':str(source),'archive':str(target.relative_to(root)),'sha256':sha(source)})

paths=subprocess.check_output(['git','diff','--name-only'],cwd=app,text=True).splitlines()
paths+=['sources/core/perfscope.h','CTest/benchmark/benchmark_storm_layers.py','CTest/benchmark/benchmark_selection_transform.py']
for relative in sorted(set(paths)):copy(app/relative,after/'stageviz'/relative)
for relative in ('investigation/performance','investigation/selection-performance5'):
    shutil.copytree(app/relative,after/'stageviz'/relative,ignore=shutil.ignore_patterns('build','__pycache__'))
(after/'working-tree.diff').write_bytes(subprocess.check_output(['git','diff'],cwd=app))
overlay=Path(r'C:\git\3rdparty\patches\openusd')
shutil.copytree(overlay,after/'windows-overlay',ignore=shutil.ignore_patterns('__pycache__'))
for entry in json.loads((overlay/'manifest.json').read_text()):
    source=usd/entry['path']
    assert hashlib.sha256(source.read_text().encode()).hexdigest()==entry['patched']
    copy(source,after/'usd'/entry['path'])

exe=app/'deploy.Release/Stageviz.exe';assert sha(exe)==sha(app/'build.Release/Release/Stageviz.exe')
copy(exe,root/'binaries'/sha(exe)/exe.name)
copy(app/'deploy.Release/openusd-bounded-staging.json',root/'binaries'/sha(exe)/'openusd-bounded-staging.json')
libraries=[]
for source in sorted((sdk/'lib').glob('usd_*.dll')):
    assert sha(source)==sha(app/'deploy.Release'/source.name)
    libraries.append({'name':source.name,'sha256':sha(source)})
(root/'deployment-verification.json').write_text(json.dumps({'exeSHA256':sha(exe),'usdDLLs':libraries},indent=2))

verified=[];excluded=[];partial=[]
def copy_changed(source,target):
    source,target=Path(source),Path(target)
    if target.exists() and (source.stat().st_size,source.stat().st_mtime_ns)==(target.stat().st_size,target.stat().st_mtime_ns):return str(target)
    return shutil.copy2(source,target)
for directory in sorted((temp/'performance').iterdir()):
    if not directory.is_dir() or not any(label in directory.name for label in ('-selection-','-volvo-cpu-','-volvo-sweep-','-volvo-existing-transform-')):continue
    if not (directory/'launch.json').exists():continue
    launch=json.loads((directory/'launch.json').read_text())
    status=json.loads((directory/'status.json').read_text())
    for dataset in launch['datasets']:
        stat=Path(dataset['path']).stat()
        assert stat.st_size==dataset['bytes'] and stat.st_mtime_ns==dataset['mtime_ns']
    all_rows=json.loads((directory/'results.json').read_text()) if (directory/'results.json').exists() else []
    audited=[]
    for row in all_rows:
        oracle_path=directory/(row['phase']+'-oracle.json')
        if not oracle_path.exists():continue
        assert row['verified'] and not row['dirty_file_layers'],(directory.name,row['phase'])
        oracle=json.loads(oracle_path.read_text())
        decode=lambda key:json.loads(oracle[key]['result']['content'][0]['text'])
        assert decode('stageviz_get_selection')==row['selection']
        assert decode('stageviz_get_stage_info')['layerCount']==len(row['layer_stack'])
        audited.append(row['phase'])
    if status['state']=='complete' and not (directory/'excluded.json').exists():
        rows=all_rows
        assert len(audited)==len(rows)
        assert rows[-1]['phase']=='reload_baseline' and rows[-1]['root_restored'] and not rows[-1]['selection']
        verified.append({'run':directory.name,'checkpoints':len(rows),'independentMCPMatches':True,'sourceMetadataUnchanged':True})
    else:
        excluded.append({'run':directory.name,'state':status['state'],'phase':status.get('phase'),'error':status.get('error','')[:600]})
        if audited and not (directory/'excluded.json').exists():
            partial.append({'run':directory.name,'state':status['state'],'failurePhase':status.get('phase'),
                            'verifiedCompletedPhases':audited,'independentMCPMatches':True,
                            'entireSequencePassed':False})
    shutil.copytree(directory,root/'runs'/directory.name,dirs_exist_ok=True,copy_function=copy_changed)
(root/'checkpoint-verification.json').write_text(json.dumps(verified,indent=2))
(root/'excluded-runs.json').write_text(json.dumps(excluded,indent=2))
(root/'partial-checkpoint-verification.json').write_text(json.dumps(partial,indent=2))
for source in temp.glob('selection5-*.log'):copy(source,root/'logs'/source.name)
copy(temp/'performance/selection5-summary.csv',root/'selection5-summary.csv')
copy(app/'investigation/selection-performance5/report.md',root/'report.md')
copy(app/'investigation/selection-performance5/README.md',root/'settings.md')
index=usd/'stageviz-investigation/README.md'
if 'test5-selection-transform' not in index.read_text(encoding='utf-8'):
    copy(index,root/'before/documentation/README.md')
    with index.open('a',encoding='utf-8') as stream:
        stream.write('\n## Iteration 5\n\n[test5-selection-transform](test5-selection-transform/report.md) records sweep/invert/gizmo diagnosis, CPU controls, and the disconnected-session OpenGL limitation.\n')
copy(index,after/'investigation-README.md')
(after/'manifest.json').write_text(json.dumps(records,indent=2))
print('Archived',after,'; verified',len(verified),'complete runs and',len(libraries),'matching USD DLLs; excluded',len(excluded),'diagnostic attempts')
