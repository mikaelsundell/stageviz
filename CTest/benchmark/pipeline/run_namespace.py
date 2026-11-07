# SPDX-License-Identifier: BSD-3-Clause
"""Measure real Outliner edits and namespace commands with external MCP oracles."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from mcp_runtime import DisposableStageviz


def workflow(root,branch,max_selection,selection_only=False,render_repeats=0,render_only=False):
    renamed=str(Path(branch).parent/(Path(branch).name+'_Audit'))
    moved='/AuditTarget/'+Path(branch).name
    renamed_root=root+'_Audit';moved_root='/AuditTarget/'+Path(root).name
    steps=[]
    def add(name,action,**values):steps.append(dict(name=name,action=action,**values))
    add('load','load',exists=[root,branch]);add('frame','frame')
    if render_only:
        add('select_3','select',count=3)
        for i in range(max(1,render_repeats)):
            add('materials_on_%d'%i,'materials',enabled=True)
            add('wire_%d'%i,'wire');add('shaded_%d'%i,'shaded')
            add('materials_off_%d'%i,'materials',enabled=False)
        add('cleanup','cleanup',exists=[root],absent=['/AuditTarget'])
        return steps
    if not selection_only:
        add('prepare','prepare',exists=['/AuditTarget',branch])
    for count in dict.fromkeys([1,1000,max_selection]):
        add('select_%d'%count,'select',count=count)
        add('select_%d_repeat'%count,'repeat_selection')
    if selection_only:
        add('collapse_properties','collapse_properties');add('expand_properties','expand_properties')
    add('property_refresh','property_refresh');add('visibility_restore','restore_visibility')
    if selection_only:
        for i in range(render_repeats):
            add('materials_on_%d'%i,'materials',enabled=True)
            add('wire_%d'%i,'wire');add('shaded_%d'%i,'shaded')
            add('materials_off_%d'%i,'materials',enabled=False)
        add('cleanup','cleanup',exists=[root],absent=['/AuditTarget'])
        return steps
    add('select_parent_child','select_parent_child');add('select_branch','select_branch')
    add('rename_editor','rename_editor',exists=[renamed],absent=[branch],tree_exists=[renamed],sample_remap=[branch,renamed])
    add('rename_undo','undo',exists=[branch],absent=[renamed],tree_exists=[branch])
    add('rename_redo','redo',exists=[renamed],absent=[branch],tree_exists=[renamed],sample_remap=[branch,renamed])
    add('rename_restore','undo',exists=[branch],absent=[renamed])
    add('move_drop','move_drop',exists=[moved],absent=[branch],tree_exists=[moved],sample_remap=[branch,moved])
    add('move_undo','undo',exists=[branch],absent=[moved],tree_exists=[branch])
    add('move_redo','redo',exists=[moved],absent=[branch],sample_remap=[branch,moved])
    add('move_restore','undo',exists=[branch],absent=[moved])
    add('multi_prepare','multi_prepare');add('multi_drop','multi_drop')
    add('multi_undo','undo',exists=[branch]);add('multi_redo','redo');add('multi_restore','undo')
    add('invalid_drop','invalid_drop',exists=[branch])
    add('default_prim_guard','default_guard',exists=[root],absent=[renamed_root])
    add('clear_default_prim','clear_default',exists=[root])
    add('root_rename','root_rename',exists=[renamed_root],absent=[root],tree_exists=[renamed_root],sample_remap=[root,renamed_root])
    add('root_rename_restore','undo',exists=[root],absent=[renamed_root])
    add('root_move','root_move',exists=[moved_root],absent=[root],tree_exists=[moved_root],sample_remap=[root,moved_root])
    add('root_move_restore','undo',exists=[root],absent=[moved_root])
    add('materials_on','materials',enabled=True);add('wire','wire');add('shaded','shaded')
    add('materials_off','materials',enabled=False);add('cleanup','cleanup',exists=[root],absent=['/AuditTarget'])
    return steps


def run(args,script=None,steps=None,extra_config=None):
    output=Path(args.output).resolve();fixture=Path(args.fixture).resolve()
    digest=hashlib.sha256(fixture.read_bytes()).hexdigest()
    config=dict(fixture=str(fixture),root=args.root,branch=args.branch,output=str(output),settleSeconds=.75,timeoutSeconds=300,
                propertyPaneWidth=args.property_pane_width,
                steps=workflow(args.root,args.branch,args.max_selection,args.selection_only,args.render_repeats,args.render_only))
    if steps is not None:config['steps']=steps
    config.update(extra_config or {})
    script=Path(script) if script is not None else Path(__file__).with_name('namespace_workflow.py')
    try:
        with DisposableStageviz(args.app,output,environment={'STAGEVIZ_PIPELINE_PROFILE':str(output/'profile.json'),'STAGEVIZ_PERF_LOG':str(output/'native-phases.jsonl'),'HD_ENABLE_GPU_FRUSTUM_CULLING':'0'}) as app:
            (output/'profile.json').write_text(json.dumps(config,indent=2))
            (output/'input-embedded.py').write_bytes(script.read_bytes())
            (output/'input-controller.py').write_bytes(Path(__file__).read_bytes())
            (output/'fixture.json').write_text(json.dumps(dict(file=str(fixture),sha256=digest),indent=2))
            (output/'before-oracle.json').write_text(json.dumps(app.oracle(),indent=2))
            app.python('import runpy;runpy.run_path(%r)'%str(script))
            acknowledged=set();previous=None;last_progress=0;deadline=time.monotonic()+7200
            while True:
                assert app.process.poll() is None,app.watchdog_error or 'App exited'
                assert time.monotonic()<deadline,'External deadline exceeded'
                try:state=json.loads((output/'status.json').read_text())
                except (FileNotFoundError,json.JSONDecodeError):time.sleep(.25);continue
                key=(state['state'],state.get('phase'))
                if key!=previous or time.monotonic()-last_progress>30:
                    print(args.label,*key,flush=True);previous=key;last_progress=time.monotonic()
                assert state['state']!='failed',state.get('error')
                if state['state']=='complete':break
                if state['state']=='checkpoint' and state['phase'] not in acknowledged:
                    phase=state['phase'];row=state['result'];oracle=app.oracle()
                    selected=json.loads(oracle['stageviz_get_selection']['result']['content'][0]['text'])
                    assert len(selected)==row['selection_count']
                    assert hashlib.sha256('\n'.join(selected).encode()).hexdigest()==row['selection_sha256']
                    if selected:oracle['prim']=app.tool('stageviz_get_prim_info',{'path':selected[0]})
                    (output/(phase+'-oracle.json')).write_text(json.dumps(oracle,indent=2))
                    print('RESULT',args.label,phase,'dispatch %.1fms response %.1fms selected %d'%(row['dispatch_ms'],row['responsive_ms'],len(selected)),flush=True)
                    (output/'ack.txt').write_text(phase);acknowledged.add(phase)
                time.sleep(.25)
            assert hashlib.sha256(fixture.read_bytes()).hexdigest()==digest,'Source file changed on disk'
            (output/'passed.json').write_text(json.dumps(dict(passed=True,checkpoints=len(acknowledged),source_unchanged=True),indent=2))
            print('PASS',args.label,len(acknowledged),'checkpoints',flush=True)
    finally:
        assert hashlib.sha256(fixture.read_bytes()).hexdigest()==digest,'Source file changed on disk'


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',required=True);parser.add_argument('--fixture',required=True)
    parser.add_argument('--root',required=True);parser.add_argument('--branch',required=True)
    parser.add_argument('--max-selection',type=int,default=25000)
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--selection-only',action='store_true')
    mode.add_argument('--render-only',action='store_true')
    parser.add_argument('--property-pane-width',type=int,default=0)
    parser.add_argument('--render-repeats',type=int,default=0)
    parser.add_argument('--output',required=True);parser.add_argument('--label',required=True)
    args=parser.parse_args()
    if args.selection_only and args.property_pane_width<=0:
        parser.error('--selection-only requires --property-pane-width greater than zero')
    run(args)
