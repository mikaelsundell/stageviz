from pathlib import Path
import csv,json,sys

base=Path(r'C:\Temp\usdtest\performance');rows=[]
for directory in sorted(base.iterdir()):
    if not directory.is_dir() or not any(label in directory.name for label in ('volvo-cpu-','selection-cpu-','volvo-sweep-','selection-fixture','volvo-existing-transform-')):continue
    if not (directory/'results.json').exists() or (directory/'excluded.json').exists():continue
    status=json.loads((directory/'status.json').read_text())
    if status['state']!='complete' and '--include-partial' not in sys.argv:continue
    if not (directory/'gl-context.json').exists():continue
    events=[json.loads(line) for line in (directory/'native-phases.jsonl').read_text().splitlines()]
    config=json.loads((directory/'config.json').read_text())
    gl=json.loads((directory/'gl-context.json').read_text())
    for result in json.loads((directory/'results.json').read_text()):
        if not (directory/(result['phase']+'-oracle.json')).exists():continue
        totals={}
        for event in events:
            if result['start_wall_ms']<=event['wall_start_ms']<=result['end_wall_ms']:
                totals[event['phase']]=totals.get(event['phase'],0)+event['duration_ms']/1000
        row={'run':directory.name,'run_state':status['state'],'phase':result['phase'],'dispatch_s':result['dispatch_ms']/1000,
             'response_s':result['end_to_end_ms']/1000,'selected':len(result['selection']),
             'target_count':result.get('transform_target_count'),'cpuOnly':config.get('cpuOnly',False),
             'cpuTransform':config.get('cpuTransform',False),'validGL':gl['valid'],
             'exact_root_restored':result.get('root_restored')}
        for name in ('selection.invert_domain','selection.invert_worker','selection.update_paths','ui.outliner_selection',
                     'ui.property_selection','ui.viewport_selection','ui.scene_statistics',
                     'selection.bvh_build','selection.bvh_query','transform.visible_selection',
                     'transform.resolve_targets','transform.instance_sources','transform.selection_pivot',
                     'transform.capture_operation','transform.prepare_authoring','transform.apply_prepared',
                     'transform.begin_drag','transform.update_drag','transform.end_drag','session.bounding_box',
                     'selection.hydra_mapping','hydra.render_cpu','viewport.paint','transform.gizmo_geometry'):
            row[name]=totals.get(name,0)
        if not any(event['phase']=='ui.property_selection' for event in events):
            row['ui.outliner_selection']=None
            row['ui.property_selection']=None
        rows.append(row)
if rows:
    output=base/'selection5-summary.csv'
    with output.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)
    print(output)
    for row in rows:
        if row['phase'] in ('invert_selection','drag_press','drag_step_1','drag_step_2','drag_release'):
            print(json.dumps({key:value for key,value in row.items() if value not in (0,None,False)}))
