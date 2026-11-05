"""Compare complete selection arrays with the verified pre-change checkpoints."""
import json
from pathlib import Path

base=Path(r'C:\Temp\usdtest\performance')
before_name='20261104T213829Z-00-64MiB-volvo-sweep-move-baseline'
before={row['phase']:row for row in json.loads((base/before_name/'results.json').read_text())}
records=[]
for directory in sorted(base.iterdir()):
    if not any(directory.name.endswith('-volvo-'+label) for label in ('indexed-loops','direct-delete','sorted-roots')):
        continue
    after={row['phase']:row for row in json.loads((directory/'results.json').read_text())}
    for phase in ('sweep_cold','sweep_warm','invert_selection'):
        assert (directory/(phase+'-oracle.json')).exists()
        a,b=before[phase]['selection'],after[phase]['selection']
        assert a==b,(directory.name,phase,'selection order or contents changed')
        records.append(dict(beforeRun=before_name,afterRun=directory.name,phase=phase,
                            beforeCount=len(a),afterCount=len(b),identicalOrder=True))
Path(__file__).with_name('selection-equivalence.json').write_text(json.dumps(records,indent=2))
print('Verified exact selection order and contents:',len(records),'checkpoints')
