from pathlib import Path
import hashlib
import json
import subprocess
import sys
import zipfile

base = Path(r'C:\Temp\usdtest\storm-investigation\bounded-staging')
overlay = Path(r'C:\git\3rdparty\patches\openusd')
fixture = base/('overlay-fixture-'+sys.argv[1] if len(sys.argv)>1 else 'overlay-fixture')
assert not fixture.exists()
fixture.mkdir()
manifest = json.loads((overlay/'manifest.json').read_text())
with zipfile.ZipFile(r'C:\git\3rdparty\packages\OpenUSD-25.11.zip') as archive:
    for entry in manifest:
        candidates = [name for name in archive.namelist() if name.endswith('/'+entry['path'])]
        if not candidates:
            assert entry['original'] == hashlib.sha256(b'').hexdigest(), entry['path']
            continue
        assert len(candidates)==1
        text = archive.read(candidates[0]).decode('utf-8').replace('\r\n','\n')
        assert hashlib.sha256(text.encode()).hexdigest()==entry['original'], entry['path']
        path = fixture/entry['path']
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(text)
cmd = [sys.executable,str(overlay/'apply.py'),str(fixture)]
subprocess.run(cmd,check=True)
subprocess.run(cmd,check=True)
for entry in manifest:
    assert hashlib.sha256((fixture/entry['path']).read_text().encode()).hexdigest()==entry['patched']
altered = fixture/manifest[0]['path']
altered.write_text(altered.read_text()+'\n// unrelated local change\n')
before = altered.read_bytes()
rejected = subprocess.run(cmd,capture_output=True,text=True)
assert rejected.returncode != 0 and altered.read_bytes()==before
report = {'freshArchive':True,'repeatApply':True,'modifiedSourceRejected':True}
(base/('overlay-test-'+sys.argv[1]+'.json' if len(sys.argv)>1 else 'overlay-test.json')).write_text(json.dumps(report,indent=2))
print('PASS',report)
