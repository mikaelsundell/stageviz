"""Resolve sampled RVAs using scratch linker maps with matching .text hashes."""
from pathlib import Path
import bisect
import ctypes
import json
import re
import sys

maps=Path(r'C:\Temp\usdtest\performance\storm-cpu-symbol-maps')
modules={}
for path in maps.glob('*.map'):
    provenance=json.loads(path.with_suffix('.provenance.json').read_text())
    assert provenance['identicalText'], 'Binary does not match map: '+str(path)
    text=path.read_text()
    base=int(re.search(r'Preferred load address is ([0-9A-Fa-f]+)',text)[1],16)
    rows=[]
    for match in re.finditer(r'^\s+0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s',text,re.M):
        rows.append((int(match[2],16)-base,match[1]))
    rows.sort();modules[path.stem]=([row[0] for row in rows],rows)

def undecorate(name):
    output=ctypes.create_string_buffer(8192)
    ctypes.windll.dbghelp.UnDecorateSymbolName(name.encode(),output,len(output),0)
    return output.value.decode() or name

source=Path(sys.argv[1])
output=[]
for line in source.read_text().splitlines():
    match=re.match(r'  (\w+)\+0x([0-9a-f]+)',line)
    if match and match[1] in modules:
        offsets,rows=modules[match[1]];address=int(match[2],16)
        index=bisect.bisect_right(offsets,address)-1
        if index>=0:
            offset,name=rows[index]
            line='  '+match[1]+'+0x'+match[2]+' '+undecorate(name)+' +0x'+format(address-offset,'x')
    output.append(line)
destination=source.with_name(source.stem+'-decoded.txt')
destination.write_text('\n'.join(output),encoding='utf-8')
print(destination)
