"""Verify deployed bits and synchronize the complete investigation evidence."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

app=Path(__file__).resolve().parents[2]
root=Path(r'C:\Temp\usdtest\performance')
archive=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test3-performance')
prefix=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
deploy=app/'deploy.Release'
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
checks=[]
for source in sorted((prefix/'lib').glob('usd_*.dll')):
    actual=deploy/source.name
    checks.append({'name':source.name,'sdkSHA256':digest(source),'deployedSHA256':digest(actual)})
    assert checks[-1]['sdkSHA256']==checks[-1]['deployedSHA256'],source
assert digest(app/'build.Release/Release/Stageviz.exe')==digest(deploy/'Stageviz.exe')
(root/'deployment-verification.json').write_text(json.dumps({'exeSHA256':digest(deploy/'Stageviz.exe'),'usdDLLs':checks},indent=2))
verified_runs=[]
for directory in root.iterdir():
    if not directory.is_dir() or not (directory/'launch.json').exists():continue
    launch=json.loads((directory/'launch.json').read_text())
    if launch['config'].get('smoke') or (directory/'excluded.json').exists() or not launch.get('scriptSHA256'):continue
    status=json.loads((directory/'status.json').read_text())
    if status['state']!='complete':continue
    results=json.loads((directory/'results.json').read_text())
    for row in results:
        assert row['verified'] and not row['dirty_file_layers'],row
        oracle=json.loads((directory/(row['phase']+'-oracle.json')).read_text())
        decode=lambda key:json.loads(oracle[key]['result']['content'][0]['text'])
        assert decode('stageviz_get_stage_info')['layerCount']==len(row['layer_stack']),row['phase']
        assert decode('stageviz_get_selection')==row['selection'],row['phase']
    for dataset in launch.get('datasets',[]):
        stat=Path(dataset['path']).stat()
        assert stat.st_size==dataset['bytes'] and stat.st_mtime_ns==dataset['mtime_ns'],dataset['path']
    verified_runs.append({'run':directory.name,'checkpoints':len(results),'independentLayerCountsAndSelectionMatch':True,'sourceLayerMetadataUnchanged':True})
(root/'checkpoint-verification.json').write_text(json.dumps(verified_runs,indent=2))

variants={}
sources=list((archive/'after').rglob('benchmark_storm_layers.py'))+[app/'CTest/benchmark/benchmark_storm_layers.py']
for source in sources:variants.setdefault(digest(source),source)
for directory in root.iterdir():
    if not directory.is_dir() or not (directory/'launch.json').exists():continue
    launch=json.loads((directory/'launch.json').read_text())
    key=launch.get('scriptSHA256')
    if key in variants:shutil.copy2(variants[key],directory/'benchmark-used.py')
    shutil.copytree(directory,archive/'runs'/directory.name,dirs_exist_ok=True)
(archive/'benchmark-source-variants.json').write_text(json.dumps({key:str(value) for key,value in variants.items()},indent=2))

symbols=archive/'symbol-maps'
symbols.mkdir(exist_ok=True)
for source in (root/'symbol-maps').iterdir():
    if source.suffix.lower() not in ('.dll','.exe','.lib','.exp'):
        shutil.copy2(source,symbols/source.name)
logs=archive/'build-and-test-logs';logs.mkdir(exist_ok=True)
for source in root.parent.glob('performance-*.log'):shutil.copy2(source,logs/source.name)
for name in ('deployment-verification.json','checkpoint-verification.json','composition-control.json','composition-control.log','hardware.json','summary.csv','final-fixture-cleanup.log'):
    if (root/name).exists():shutil.copy2(root/name,archive/name)
print('Verified Release executable and',len(checks),'USD DLLs; evidence synchronized to',archive)
