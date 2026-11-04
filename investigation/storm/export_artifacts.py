from pathlib import Path
import difflib
import hashlib
import json
import shutil
import subprocess

root=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
out=Path(r'C:\Temp\usdtest\storm-investigation')
here=Path(__file__).parent
files=['pxr/imaging/hdSt/indirectDrawBatch.cpp','pxr/imaging/hdSt/stagingBuffer.cpp','pxr/imaging/hgiGL/ops.cpp']
patch=[]
manifest={'source':'OpenUSD-25.11 archive; no .git directory supplied', 'files':[]}
for relative in files:
    path=root/relative
    original=(out/(path.name+'.original')).read_text()
    current=path.read_text()
    patch.extend(difflib.unified_diff(original.splitlines(True),current.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
    manifest['files'].append({'path':relative, 'originalTextSHA256':hashlib.sha256(original.encode()).hexdigest(), 'patchedTextSHA256':hashlib.sha256(current.encode()).hexdigest()})
(out/'openusd-storm-diagnostics.patch').write_text(''.join(patch))
opsOriginal=(out/'ops.cpp.original').read_text()
patched=(root/'pxr/imaging/hgiGL/ops.cpp').read_text()
start=patched.index('        // Diagnostic comparison: preserve staging')
end=patched.index('        _StormTraceCpuUpload(copyOp);',start)
old='''        glNamedBufferSubData(
            glBuffer->GetBufferId(),
            dstOffset,
            copyOp.byteSize,
            src);
'''
proposal=opsOriginal.replace('PXR_NAMESPACE_OPEN_SCOPE','#include <algorithm>\n#include <cstdlib>\n\nPXR_NAMESPACE_OPEN_SCOPE',1).replace(old,patched[start:end],1)
(out/'bounded-uploads-only.patch').write_text(''.join(difflib.unified_diff(opsOriginal.splitlines(True),proposal.splitlines(True),fromfile='a/pxr/imaging/hgiGL/ops.cpp',tofile='b/pxr/imaging/hgiGL/ops.cpp')))
dlls=out/'patched-dlls'
dlls.mkdir(exist_ok=True)
for name in ('hdSt','hgiGL'):
    path=root/('build/vc143/x86_amd64/Release/pxr/imaging/'+name+'/Release/usd_'+name+'.dll')
    shutil.copy2(path,dlls/path.name)
    manifest[name+'SHA256']=hashlib.sha256(path.read_bytes()).hexdigest()
scripts=out/'scripts'
scripts.mkdir(exist_ok=True)
for path in here.glob('*'):
    if path.is_file() and path.suffix in ('.py','.h','.md','.ps1','.txt'):
        shutil.copy2(path,scripts/path.name)
shutil.copytree(here/'probe',scripts/'probe',ignore=shutil.ignore_patterns('build'),dirs_exist_ok=True)
shutil.copy2(here/'REPORT.txt',out/'REPORT.txt')
cache=root/'build/vc143/x86_amd64/Release/CMakeCache.txt'
manifest['buildSettings']=[line for line in cache.read_text().splitlines() if line.startswith(('CMAKE_BUILD_TYPE:','CMAKE_GENERATOR:','CMAKE_GENERATOR_PLATFORM:','CMAKE_INSTALL_PREFIX:','CMAKE_CXX_FLAGS_RELEASE:','PXR_BUILD_MONOLITHIC:','PXR_ENABLE_GL_SUPPORT:','PXR_ENABLE_PYTHON_SUPPORT:'))]
manifest['gpu']=subprocess.check_output(['nvidia-smi','--query-gpu=name,driver_version,memory.total','--format=csv'],text=True).strip()
dataset=Path(r'C:\Temp\usdtest\TEMP_asm.usd')
manifest['dataset']={'path':str(dataset),'bytes':dataset.stat().st_size,'mtimeNs':dataset.stat().st_mtime_ns}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('Exported diagnostic patch, bounded-upload-only patch, DLLs, scripts and manifest')
