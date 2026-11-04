from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
for path in (root/'sources').rglob('*.cpp'):
    text=path.read_text(encoding='utf-8')
    fixed=re.sub(r'(?m)^(\s*)\{\n        (PerfScope perfScope\("(?:selection\.|transform\.)[^\n]+)',
                 lambda match:match[1]+'{\n'+match[1]+'    '+match[2],text)
    if fixed!=text:path.write_text(fixed,encoding='utf-8')
