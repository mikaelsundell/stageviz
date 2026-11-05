"""Summarize verified checkpoints; nested native scopes are inclusive."""
from pathlib import Path
import csv
import json

base=Path(r'C:\Temp\usdtest\performance')
baseline_names={
    '20261104T213829Z-00-64MiB-volvo-sweep-move-baseline',
    '20261104T214948Z-00-64MiB-volvo-existing-transform-parent-control',
}
rows=[]
for directory in sorted(base.iterdir()):
    if directory.name not in baseline_names and not any(name in directory.name for name in
            ('-indexed-loops','-direct-delete','-sorted-roots')):
        continue
    if not (directory/'results.json').exists():
        continue
    status=json.loads((directory/'status.json').read_text())
    events=[json.loads(line) for line in (directory/'native-phases.jsonl').read_text().splitlines()]
    launch=json.loads((directory/'launch.json').read_text())
    results=json.loads((directory/'results.json').read_text())
    for index,row in enumerate(results):
        oracle=directory/(row['phase']+'-oracle.json')
        if not oracle.exists():
            continue
        scopes={}
        deferred_stats=0
        next_start=results[index+1]['start_wall_ms'] if index+1<len(results) else float('inf')
        for event in events:
            if row['start_wall_ms']<=event['wall_start_ms']<=row['end_wall_ms']:
                scopes[event['phase']]=scopes.get(event['phase'],0)+event['duration_ms']/1000
            elif (event['phase']=='ui.scene_statistics'
                  and row['end_wall_ms']<event['wall_start_ms']<next_start):
                deferred_stats+=event['duration_ms']/1000
        rows.append(dict(run=directory.name, state=status['state'],phase=row['phase'],
                         response_s=row['end_to_end_ms']/1000,dispatch_s=row['dispatch_ms']/1000,
                         selected=len(row['selection']),targets=row.get('transform_target_count'),
                         exact_root_restored=row.get('root_restored'),exeSHA256=launch['exeSHA256'],
                         deferred_scene_statistics_s=deferred_stats,scopes=scopes))
output=Path(__file__).with_name('measurements.json')
output.write_text(json.dumps(rows,indent=2))
with output.with_suffix('.csv').open('w',newline='') as stream:
    fields=['run','state','phase','response_s','dispatch_s','selected','targets','exact_root_restored','exeSHA256']
    writer=csv.DictWriter(stream,fieldnames=fields,extrasaction='ignore')
    writer.writeheader();writer.writerows(rows)
for row in rows:
    if row['phase'] in ('invert_selection','move_gizmo','drag_press','drag_step_1','drag_step_2','drag_release','undo_drag'):
        print(json.dumps({key:value for key,value in row.items() if key not in ('scopes','exeSHA256')}))
