"""Record the code inventory and bulk-loop candidates used in the audit."""
import json
from pathlib import Path
import re
import subprocess

root=Path(__file__).resolve().parents[2]
files=subprocess.check_output(['rg','--files','sources','scripts'],cwd=root,text=True).splitlines()
patterns={name:re.compile(pattern) for name,pattern in {
    'loops':r'\b(?:for|while)\s*\(',
    'membership':r'\.contains\s*\(|appendUnique\s*\(',
    'prefix':r'HasPrefix\s*\(',
    'sdf_batch':r'\bSdfChangeBlock\b',
}.items()}
rows=[]
for filename in sorted(files):
    path=root/filename
    if path.suffix not in ('.cpp','.h','.py'):
        continue
    lines=path.read_text(encoding='utf-8-sig').splitlines()
    matches={name:[number for number,line in enumerate(lines,1) if regex.search(line)] for name,regex in patterns.items()}
    rows.append(dict(path=filename.replace('\\','/'),lines=len(lines),matches=matches))
output=Path(__file__).with_name('audit-inventory.json')
output.write_text(json.dumps(dict(files=len(rows),inventory=rows),indent=2))
print('Audited code inventory:',len(rows),'files;',sum(len(r['matches']['loops']) for r in rows),'loop sites')
