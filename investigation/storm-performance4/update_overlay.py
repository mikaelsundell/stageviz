from pathlib import Path
import difflib
import hashlib
import json
import shutil

usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
overlay=Path(r'C:\git\3rdparty\patches\openusd')
iteration=usd/'stageviz-investigation/test4-storm-cpu'
before=next((iteration/'before').iterdir())/'usd'
entries=json.loads((overlay/'manifest.json').read_text())
by_path={entry['path']:entry for entry in entries}
paths=['pxr/imaging/hd/instanceRegistry.h','pxr/imaging/hd/CMakeLists.txt',
 'pxr/imaging/hd/testenv/testHdInstanceRegistryBudget.cpp',
 'pxr/imaging/hdSt/performance.h','pxr/imaging/hdSt/performance.cpp',
 'pxr/imaging/hdSt/resourceRegistry.cpp','pxr/imaging/hdSt/resourceRegistry.h',
 'pxr/imaging/hdSt/renderDelegate.cpp','pxr/imaging/hdSt/renderPass.cpp','pxr/imaging/hdSt/renderPass.h',
 'pxr/imaging/hdSt/drawBatch.cpp','pxr/imaging/hdSt/commandBuffer.cpp','pxr/imaging/hdSt/CMakeLists.txt']
patch=[]
for relative in paths:
    text=(usd/relative).read_text(encoding='utf-8')
    original=(before/relative).read_text(encoding='utf-8') if (before/relative).exists() else ''
    target=overlay/'files'/relative;target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(text,encoding='utf-8')
    entry=by_path.get(relative,{'path':relative,'original':hashlib.sha256(original.encode()).hexdigest()})
    entry['patched']=hashlib.sha256(text.encode()).hexdigest();by_path[relative]=entry
    patch.extend(difflib.unified_diff(original.splitlines(True),text.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
(overlay/'manifest.json').write_text(json.dumps(list(by_path.values()),indent=2))
(overlay/'storm-cpu.patch').write_text(''.join(patch),encoding='utf-8')
record=overlay/'record_install.py'
text=record.read_text()
text=text.replace('OpenUSD 25.11 + bounded-staging local patch','OpenUSD 25.11 + bounded-staging + opt-in Storm CPU maintenance')
if "'defaultTopologyGCBudgetUs'" not in text:
    text=text.replace("'defaultBatchBytes':64*1024*1024,","'defaultBatchBytes':64*1024*1024,\n    'defaultTopologyGCBudgetUs':0,\n    'stormCpuPatchSHA256':hashlib.sha256((overlay/'storm-cpu.patch').read_bytes()).hexdigest(),")
record.write_text(text)
print('Updated guarded Windows overlay:',len(by_path),'files')
