"""Exercise the shipped overlay against pristine source, twice, then local edits."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys
import zipfile

overlay=Path(r'C:\git\3rdparty\patches\openusd')
destination=Path(r'C:\Temp\usdtest')/('storm-overlay-audit-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
destination.mkdir(exist_ok=False)
entries=json.loads((overlay/'manifest.json').read_text())
with zipfile.ZipFile(r'C:\git\3rdparty\packages\OpenUSD-25.11.zip') as archive:
    for entry in entries:
        relative=entry['path']
        matches=[name for name in archive.namelist() if name.endswith('/'+relative)]
        if not matches:
            assert entry['original']==hashlib.sha256(b'').hexdigest(),relative
            continue
        assert len(matches)==1,relative
        target=destination/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(archive.read(matches[0]))
for attempt in range(2):
    subprocess.run([sys.executable,str(overlay/'apply.py'),str(destination)],check=True)
for entry in entries:
    assert hashlib.sha256((destination/entry['path']).read_text().encode()).hexdigest()==entry['patched']
target=destination/entries[0]['path']
target.write_text(target.read_text()+'\n// audit: intentional local edit\n')
before=target.read_bytes()
result=subprocess.run([sys.executable,str(overlay/'apply.py'),str(destination)],capture_output=True,text=True)
assert result.returncode!=0 and 'Refusing to overwrite' in result.stderr
assert target.read_bytes()==before
report={'source':'OpenUSD-25.11.zip','files':len(entries),'freshApply':True,'idempotent':True,'localEditRefused':True,'auditDirectory':str(destination)}
(destination/'audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
