from pathlib import Path
import csv
import json

base=Path(r'C:\Temp\usdtest\performance')
rows=[]
for directory in sorted(base.iterdir()):
    log=directory/'storm-phases.jsonl'
    if not log.exists() or not (directory/'results.json').exists():continue
    config=json.loads((directory/'config.json').read_text())
    if config.get('smoke'):continue
    events=[json.loads(line) for line in log.read_text().splitlines()]
    for result in json.loads((directory/'results.json').read_text()):
        selected=[event for event in events if result['start_wall_ms']<=event['wall_ms']<=result['end_wall_ms']]
        totals={}
        for event in selected:
            for key,value in event.get('metrics',{}).items():
                target=totals.setdefault(key,{'count':0,'ms':0})
                for field in ('count','ms'):target[field]+=value[field]
        topo=[event for event in selected if event['event']=='topology_gc']
        row={'run':directory.name,'phase':result['phase'],'response_s':result['end_to_end_ms']/1000,
             'converged':result['render_converged'],'prims':result['prims'],
             'topology_visited':sum(event['visited'] for event in topo),
             'topology_reclaimed':sum(event['reclaimed'] for event in topo),
             'topology_erase_s':sum(event['erase_ms'] for event in topo)/1000,
             'topology_destroy_s':sum(event['destroy_ms'] for event in topo)/1000,
             'topology_step_max_ms':max([event['elapsed_ms'] for event in topo] or [0])}
        for key in ('commit_resources','resource_gc','shader_prepare','resolve_bindings','codegen_setup','codegen_hash','codegen_destroy','compile_link','batch_rebuild_all'):
            row[key+'_s']=totals.get(key,{}).get('ms',0)/1000
        for key in ('program_hit','program_miss','draw_batches','draw_items'):
            row[key]=totals.get(key,{}).get('count',0)
        rows.append(row)
if rows:
    destination=base/'storm-cpu-summary.csv'
    with destination.open('w',newline='') as output:
        writer=csv.DictWriter(output,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    print(destination)
    for row in rows:
        if 'fixture' not in row['run']:
            print(json.dumps(row))
