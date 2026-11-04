from pathlib import Path
import hashlib
import json
import shutil
import zipfile

source=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
release=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
overlay=Path(r'C:\git\3rdparty\patches\openusd')
app=Path(r'C:\cadio-tools\Stageviz\Stageviz_0.4.19_261103_Release')
out=Path(r'C:\Temp\usdtest\storm-investigation\bounded-staging')
here=Path(__file__).parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
entries=json.loads((overlay/'manifest.json').read_text())
for entry in entries:
    assert hashlib.sha256((source/entry['path']).read_text().encode()).hexdigest()==entry['patched']
for relative in ('pxr/imaging/hdSt/indirectDrawBatch.cpp','pxr/imaging/hgiGL/ops.cpp',
                 'pxr/imaging/hgi/enums.h','pxr/imaging/hgiGL/buffer.cpp'):
    with zipfile.ZipFile(r'C:\git\3rdparty\packages\OpenUSD-25.11.zip') as archive:
        name=next(name for name in archive.namelist() if name.endswith('/'+relative))
        original=archive.read(name).decode().replace('\r\n','\n')
    assert (source/relative).read_text()==original, relative
binary={}
for lib in ('hdSt','hgiGL'):
    name='usd_'+lib+'.dll'
    assert sha(release/'lib'/name)==sha(app/name)
    data=(release/'lib'/name).read_bytes()
    for experimental in (b'STORM_TRACE_BYPASS_STAGING',b'STORM_TRACE_UPLOAD_CHUNK_MB',b'storm-trace.txt'):
        assert experimental not in data, (name,experimental)
    binary[name]={'sha256':sha(release/'lib'/name),'installed':str(release/'lib'/name),'deployed':str(app/name)}
report={
    'version':'OpenUSD 25.11 + bounded-staging local patch',
    'configuration':'MSVC vc143 x86_amd64 Release',
    'defaultBatchBytes':64*1024*1024,
    'environmentRequired':False,
    'sources':entries,
    'patch':str(overlay/'bounded-staging.patch'),
    'patchSHA256':sha(overlay/'bounded-staging.patch'),
    'binaries':binary,
    'fullBuild':'passed','install':'passed',
    'nativeReadbackTests':['4 KiB passed','64 MiB passed','invalid zero fallback passed'],
    'liveRuns':['run10-bounded-final'],
    'preliminaryLiveRuns':['run08-bounded','run09-bounded'],
    'limitations':['UMA/Metal/Vulkan not tested','Debug not built','broad test suites not run','performance not benchmarked'],
}
for run in report['liveRuns']:
    check=json.loads((out.parent/run/'normalized-all-verification.json').read_text())
    assert check['hiddenMeshes']==0 and check['meshes']==38349 and not check['originalDirty']
    assert (out.parent/run/'normalized-all-viewport.png').exists()
    report[run]=check
(release/'openusd-bounded-staging.json').write_text(json.dumps(report,indent=2))
(out/'release-manifest.json').write_text(json.dumps(report,indent=2))
doc=(here/'BOUNDED-STAGING.txt').read_text()
doc+='\nLive validation completed: two preliminary fresh processes and the final Release\nprocess rendered the assembled vehicle with normal staging/culling and no bypass.\nThe final source also preserves UMA staging ownership for oversized sources;\nUMA itself was not tested. All 38,349 meshes were visible at time 0; the original\nUSD layer remained clean. The known target rendered in isolation in the first\nrun. Screenshots and Qt/Hydra states are saved in the run folders. Per-prim GPU\ngeometry byte dumps are not present in the clean production DLL; byte-exact GPU\ntransfer checks were performed by the native regression instead.\n'
(out/'README.txt').write_text(doc)
(overlay/'README.md').write_text(doc)
shutil.copy2(here/'patch_upstream_upload.py',out/'historical-upstream-backport.py')
shutil.copy2(here/'bounded-test/CMakeLists.txt',out/'standalone-test-CMakeLists.txt')
shutil.copy2(here/'finalize_bounded.py',out/'finalize_bounded.py')
with zipfile.ZipFile(out/'bounded-staging-release.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for name in binary:
        archive.write(release/'lib'/name,'lib/'+name)
    archive.write(out/'README.txt','README.txt')
    archive.write(out/'release-manifest.json','release-manifest.json')
    for path in overlay.rglob('*'):
        if path.is_file():archive.write(path,'patches/openusd/'+path.relative_to(overlay).as_posix())
    archive.write(Path(r'C:\git\3rdparty\build_win.py'),'build_win.py')
    for name in ('LICENSE.txt','NOTICE.txt'):
        if (source/name).exists(): archive.write(source/name,name)
print('Verified Release hashes, wrote provenance, build notes and distribution patch bundle')
