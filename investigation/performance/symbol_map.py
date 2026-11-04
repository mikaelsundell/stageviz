"""Relink existing USD Release objects into scratch output, verifying .text identity."""
from pathlib import Path
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

module=sys.argv[1]
assert module in ('stageviz','hdSt','hgiGL','hd','hdsi','usdImaging','usdImagingGL')
base=Path(r'C:\git\github\stageviz\build.Release') if module=='stageviz' else Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\build\vc143\x86_amd64\Release\pxr')/('usdImaging' if module.startswith('usdImaging') else 'imaging')/module
tlog=base/f'{module}.dir/Release/{module}.tlog/link.command.1.tlog'
scratch=Path(os.environ.get('STAGEVIZ_SYMBOL_MAP_DIR',r'C:\Temp\usdtest\performance\symbol-maps'))
scratch.mkdir(exist_ok=True)
dll=scratch/('Stageviz.exe' if module=='stageviz' else f'usd_{module}.dll')
text='\n'.join(tlog.read_text(encoding='utf-16').splitlines()[1:])
for flag,path in [('OUT',dll),('IMPLIB',dll.with_suffix('.lib')),('PDB',dll.with_suffix('.pdb'))]:
    text=re.sub(r'/'+flag+r':(?:"[^"]+"|\S+)',lambda match:'/'+flag+':"'+str(path)+'"',text,flags=re.I)
text+='\n/MAP:"'+str(dll.with_suffix('.map'))+'"'
rsp=scratch/f'{module}.rsp';rsp.write_text(text)
vc=Path(r'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207')
sdk=Path(r'C:\Program Files (x86)\Windows Kits\10')
env=os.environ.copy()
env['LIB']=';'.join(map(str,[vc/'lib/x64',sdk/'Lib/10.0.26100.0/um/x64',sdk/'Lib/10.0.26100.0/ucrt/x64']))
env['PATH']=str(sdk/'bin/10.0.26100.0/x64')+';'+env['PATH']
with (scratch/f'{module}-link.log').open('w') as log:
    subprocess.run([str(vc/'bin/Hostx64/x64/link.exe'),'@'+str(rsp)],cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)

def text_hash(path):
    data=path.read_bytes();pe=struct.unpack_from('<I',data,60)[0]
    count=struct.unpack_from('<H',data,pe+6)[0]
    offset=pe+24+struct.unpack_from('<H',data,pe+20)[0]
    for index in range(count):
        section=offset+40*index
        if data[section:section+8].rstrip(b'\0')==b'.text':
            size,start=struct.unpack_from('<II',data,section+16)
            return hashlib.sha256(data[start:start+size]).hexdigest()
    raise ValueError('Missing .text')

original=Path(r'C:\git\github\stageviz\deploy.Release')/dll.name
expected=text_hash(original);actual=text_hash(dll)
record={'original':str(original),'originalSHA256':hashlib.sha256(original.read_bytes()).hexdigest(),'originalTextSHA256':expected,'mapTextSHA256':actual,'identicalText':expected==actual}
dll.with_suffix('.provenance.json').write_text(json.dumps(record,indent=2))
assert expected==actual,record
print('Verified matching .text:',dll.with_suffix('.map'))
