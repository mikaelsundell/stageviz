from pathlib import Path
import hashlib,json,subprocess
app=Path(__file__).resolve().parents[2]
target=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test5-selection-transform\before\20261104T155910Z')
relative='sources/properties/propertytree.cpp'
data=subprocess.check_output(['git','show','HEAD:'+relative],cwd=app)
# This file was clean in the pre-investigation git status; only the new include
# and scope were added to it. Verify that reconstruction before archiving.
current=(app/relative).read_text(encoding='utf-8')
restored=current.replace('#include "perfscope.h"\n','').replace('    PerfScope perfScope("ui.property_selection");\n','')
assert restored==data.decode(), 'Property tree has unrelated local edits'
path=target/'stageviz'/relative;path.parent.mkdir(parents=True,exist_ok=True)
assert not path.exists();path.write_bytes(data)
manifest=target/'manifest.json';rows=json.loads(manifest.read_text())
rows.append({'source':str(app/relative),'sha256':hashlib.sha256(data).hexdigest(),'snapshot':'Clean HEAD verified against removal of only the new instrumentation'})
manifest.write_text(json.dumps(rows,indent=2))
