import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

p=argparse.ArgumentParser()
p.add_argument('run')
args=p.parse_args()
d=Path(r'C:\Temp\usdtest\storm-investigation')/args.run
rows=[]
for line in (d/'storm-trace.txt').read_text().splitlines():
    if not line.startswith('seq='): continue
    fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
    for name in ('cpu','gpu'):
        if name in fields:
            fields[name]=[int(v) for v in fields[name].split(',') if v]
    rows.append(fields)
results=[]
for row in rows:
    if row['phase']!='submit':continue
    seq=row['seq']
    record={'seq':seq, 'visible':row['visible'], 'cpu':row.get('cpu',[])[:5], 'gpu':row.get('gpu',[])[:5]}
    for resource,expected in [('vertex-points','points'),('topology-indices','indices'),('fvar-normals','normals')]:
        path=d/(seq+'-'+resource+'.bin')
        baseline=d/('expected-'+expected+'.bin')
        if not (path.exists() and baseline.exists()):continue
        actual=path.read_bytes(); wanted=baseline.read_bytes()
        detail={'bytes':len(actual), 'expectedBytes':len(wanted), 'exact':actual==wanted, 'sha256':hashlib.sha256(actual).hexdigest()}
        if expected=='indices':
            indices=struct.unpack('<%di'%(len(actual)//4),actual)
            detail.update(min=min(indices),max=max(indices))
        elif expected=='points':
            points=struct.unpack('<%df'%(len(actual)//4),actual)
            detail.update(min=[min(points[a::3]) for a in range(3)],max=[max(points[a::3]) for a in range(3)])
        record[expected]=detail
    results.append(record)
(d/'analysis.json').write_text(json.dumps(results, indent=2))
for r in results:print(json.dumps(r))
