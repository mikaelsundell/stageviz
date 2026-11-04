"""Summarize measured spans; native scopes are inclusive and must not be added."""
from pathlib import Path
import csv
import json
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else r'C:\Temp\usdtest\performance')
rows=[]
for directory in sorted(root.iterdir()):
    if not directory.is_dir() or not (directory/'config.json').exists():continue
    if (directory/'excluded.json').exists():continue
    config=json.loads((directory/'config.json').read_text())
    if config.get('smoke'):continue
    results=json.loads((directory/'results.json').read_text()) if (directory/'results.json').exists() else []
    events=[json.loads(line) for line in (directory/'native-phases.jsonl').read_text().splitlines()] if (directory/'native-phases.jsonl').exists() else []
    for result in results:
        start=result['start_wall_ms'];end=start+result['end_to_end_ms']
        selected=[event for event in events if start<=event.get('wall_start_ms',-1)<end]
        maxima={phase:max([event['duration_ms'] for event in selected if event['phase']==phase] or [0])/1000 for phase in ('usd.mute_layer','usd.unmute_layer','hydra.render_cpu','session.flush_prims','ui.outliner_prims','ui.scene_statistics','session.bounding_box')}
        rows.append({'run':directory.name,'staging_MiB':config['stagingMiB'],'phase':result['phase'],'dispatch_s':result['dispatch_ms']/1000,'end_to_end_s':result['end_to_end_ms']/1000,'prims':result['prims'],**{key+'_max_s':value for key,value in maxima.items()}})
if rows:
    with (root/'summary.csv').open('w',newline='') as output:
        writer=csv.DictWriter(output,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    print(json.dumps(rows,indent=2))
