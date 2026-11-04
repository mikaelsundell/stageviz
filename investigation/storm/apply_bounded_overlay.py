"""Apply the version-locked OpenUSD 25.11 overlay; refuse unknown source edits."""
import hashlib
import json
from pathlib import Path
import sys

here = Path(__file__).resolve().parent
root = Path(sys.argv[1]).resolve()
manifest = json.loads((here/'manifest.json').read_text())
pending = []
for entry in manifest:
    path = root/entry['path']
    text = path.read_text() if path.exists() else ''
    digest = hashlib.sha256(text.encode()).hexdigest()
    if digest == entry['patched']:
        continue
    if digest != entry['original']:
        raise SystemExit('Refusing to overwrite modified or incompatible source: '+str(path))
    replacement = (here/'files'/entry['path']).read_text()
    if hashlib.sha256(replacement.encode()).hexdigest() != entry['patched']:
        raise SystemExit('Overlay content does not match manifest: '+entry['path'])
    pending.append((path,replacement))
for path,text in pending:
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(text)
print('OpenUSD bounded staging overlay: applied' if pending else 'OpenUSD bounded staging overlay: already applied')
