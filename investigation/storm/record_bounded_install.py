"""Record binary provenance after the normal Windows dependency install."""
from pathlib import Path
import hashlib
import json
import sys

source = Path(sys.argv[1]).resolve()
release = Path(sys.argv[2]).resolve()
configuration = sys.argv[3]
overlay = Path(__file__).resolve().parent
entries = json.loads((overlay/'manifest.json').read_text())
for entry in entries:
    actual = hashlib.sha256((source/entry['path']).read_text().encode()).hexdigest()
    if actual != entry['patched']:
        raise SystemExit('USD install provenance failed: '+entry['path'])
binaries = {}
for name in ('usd_hdSt.dll','usd_hgiGL.dll'):
    path = release/'lib'/name
    binaries[name] = {'installed':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
report = {
    'version':'OpenUSD 25.11 + bounded-staging local patch',
    'configuration':configuration,
    'defaultBatchBytes':64*1024*1024,
    'environmentRequired':False,
    'patchSHA256':hashlib.sha256((overlay/'bounded-staging.patch').read_bytes()).hexdigest(),
    'sources':entries,
    'binaries':binaries,
    'verification':'Build/install provenance only; test results are recorded separately.',
}
(release/'openusd-bounded-staging.json').write_text(json.dumps(report,indent=2))
print('Recorded installed bounded-staging DLL hashes:',release)
