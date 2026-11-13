# SPDX-License-Identifier: BSD-3-Clause
"""Run the existing Python suites in isolated apps with external MCP evidence."""
import argparse
import json
from pathlib import Path
import sys
import signal
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mcp_runtime import DisposableStageviz


DEFAULT_SUITES = ['testapi','testui','testlayers','testpayloadhierarchy','testpayloadindicator','testviewport','testgizmostability','testsubsets','testlights','testnumericeditors','testviewporthdri','testviewportcapture','testscenestats','testaxissize','testconsole','testhierarchytransforms','testtransformoperation','testpivot','testsharedviewport','testxformgrouping','testlayercolors','testexportinstances','testsourcechild','testmakeeditable','testlayermultiselection','testlayerupdate','testresetopinions','testpreservestate','testchangesdialog']


def discover_suites(root):
    """Include every embedded suite; only the aggregate and shared helper are excluded."""
    names = {path.stem for path in (Path(root) / 'CTest/python').glob('test*.py')}
    names.difference_update({'test', 'testbench'})
    return [name for name in DEFAULT_SUITES if name in names] + sorted(names.difference(DEFAULT_SUITES))


def phases(root, name, performance=False, performance_baseline=None):
    scripts = root / 'CTest/python'
    path = scripts / (name + '.py')
    load = "import runpy; ns=runpy.run_path(%r, init_globals={'_VIEWPORT_AUTORUN':False,'_SUBSETS_AUTORUN':False,'_VIEWPORT_DISPOSABLE':True})" % str(path)
    if name in ('testapi', 'testui'):
        extra = "; ns['run'].__globals__['_PERF_ENABLED']=%r" % performance if name == 'testapi' else ''
        if name == 'testapi' and performance_baseline:
            extra += "; ns['run'].__globals__['_PERF_BASELINE_PATH']=%r" % str(Path(performance_baseline).resolve())
        return [('suite', load + extra + "; assert ns['run']() is True")]
    if name == 'testlayers':
        return [('suite', load + "; assert ns['_TEST_RESULT'] is True")]
    if name == 'testpayloadindicator':
        return [('setup', load + "; test=ns['PayloadIndicatorTest'](disposable=True); test.setup()"),
                ('mute', 'test.mute()'), ('structure', 'test.structure()'), ('cleanup', 'test.cleanup()')]
    if name == 'testexportinstances':
        return [('setup', load + "; test=ns['ExportInstancesTest'](disposable=True); test.setup()"),
                ('selected', 'test.selected()'), ('cleanup', 'test.cleanup()')]
    if name == 'testgizmostability':
        return [('setup', load + "; test=ns['GizmoStabilityTest'](disposable=True); test.setup()"),
                ('orbit', 'test.orbit()'), ('distances', 'test.distances()'), ('edge-on', 'test.edge_on()'), ('clipping', 'test.clipping()'), ('cleanup', 'test.cleanup()')]
    if name == 'testsourcechild':
        return [('setup', load + "; test=ns['SourceChildTest'](disposable=True); test.setup()")] + [
            ('api-' + mode, 'test.api(%r)' % mode) for mode in ('Move', 'Rotate', 'Scale')
        ] + [('gizmo', 'test.gizmo()'), ('cleanup', 'test.cleanup()')]
    if name == 'testmakeeditable':
        return [('setup', load + "; test=ns['MakeEditableTest'](disposable=True); test.setup()"),
                ('api', "test.exercise('api',test.child)"),
                ('root-menu', "test.exercise('context',test.root)"),
                ('proxy-menu', "test.exercise('context',test.child)"),
                ('gizmo', 'test.gizmo()'),
                ('locked-menu', 'test.locked()'), ('cleanup', 'test.cleanup()')]
    if name == 'testpayloadhierarchy':
        return [('setup', load + "; test=ns['PayloadHierarchyTest'](disposable=True); test.setup()")] + [
            (method, 'test.%s()' % method) for method in (
                'leaf_checkbox', 'group_checkbox', 'nested_rules', 'instance_payloads',
                'overlapping_batch', 'repeated_toggle', 'root_checkbox', 'menu_actions',
                'filtered_collapsed', 'rapid_clicks', 'mixed_clicks', 'selection_clicks',
                'randomized_commands', 'namespace_commands', 'layer_invert_unload', 'load_policy', 'reload', 'cleanup')]
    if name == 'testchangesdialog':
        return [('setup', load + "; test=ns['ChangesDialogTest'](disposable=True); test.setup(); test.checkpoint()")] + [
            (method, 'test.%s(); test.checkpoint()' % method) for method in ('ordinary_save_as', 'anonymous_save', 'nested', 'readonly', 'unavailable', 'cleanup')]
    if name == 'testpreservestate':
        return [('setup', load + "; test=ns['PreserveStateTest'](disposable=True); test.setup(); test.checkpoint()")] + [
            (method, 'test.%s(); test.checkpoint()' % method) for method in ('camera', 'views', 'payloads', 'sublayer_save', 'layer_controls', 'prompts',
                'disabled_and_failures', 'save_as_and_copy', 'pristine', 'anonymous', 'cleanup')]
    if name == 'testlayerupdate':
        return [('setup', load + "; test=ns['LayerUpdateTest'](disposable=True); test.setup(); test.checkpoint()"),
                ('update', 'test.update(); test.checkpoint()'), ('batch', 'test.batch(); test.checkpoint()'),
                ('availability', 'test.availability(); test.checkpoint()'), ('cleanup', 'test.cleanup(); test.checkpoint()')]
    if name == 'testlayermultiselection':
        return [('setup', load + "; test=ns['LayerMultiSelectionTest'](disposable=True); test.setup()"),
                ('selection', 'test.selection()'), ('actions', 'test.actions()'),
                ('batch-api', 'test.batches()'), ('cleanup', 'test.cleanup()')]
    if name == 'testresetopinions':
        return [('setup', load + "; test=ns['ResetOpinionsTest'](disposable=True); test.setup()"),
                ('groups-menu', 'test.exercise()'), ('cleanup', 'test.cleanup()')]
    if name == 'testlayercolors':
        return [('setup', load + "; test=ns['LayerColorsTest'](disposable=True); test.setup()"),
                ('colors', 'test.exercise()'), ('composition', 'test.composition()'), ('cleanup', 'test.cleanup()')]
    if name == 'testxformgrouping':
        return [('setup', load + "; test=ns['XformGroupingTest'](disposable=True); test.setup()"),
                ('edit', "test.exercise('edit')"), ('context', "test.exercise('context')"),
                ('cleanup', 'test.cleanup()')]
    classes = {'testhierarchytransforms':'HierarchyTransformTest', 'testtransformoperation':'TransformOperationTest', 'testpivot':'PivotTest'}
    if name in classes:
        result = [('setup', load + "; test=ns[%r](disposable=True); test.setup()" % classes[name])]
        if name == 'testhierarchytransforms':
            result += [('numeric-parent', "test.numeric('/World')"), ('numeric-child', "test.numeric('/World/Cube')"), ('selection-refresh', 'test.selection_refresh()')]
            for path_name in ('World', 'Cube'):
                path = '/World' if path_name == 'World' else '/World/Cube'
                result.append(('scrub-' + path_name, 'test.scrub(%r)' % path))
                for axis in 'XYZ':
                    result.append(('drag-' + path_name + '-' + axis, 'test.drag(%r,%r)' % (path,axis)))
            result += [('xyz-convention','test.xyz_convention()')]
        else:
            for mode in ('Move','Rotate','Scale'):
                result += [('api-'+mode, 'test.api(%r)' % mode)]
                method = 'gizmo' if name == 'testtransformoperation' else 'gesture'
                result += [(method+'-'+mode, 'test.%s(%r)' % (method,mode))]
            if name == 'testtransformoperation':
                result += [('conflicts','test.conflicts()'),('composed-batch','test.composed_batch()'),('mapped-attributes','test.mapped_attributes()')]
            else:
                result += [('property-edit','test.property_edit()')]
                for mode in ('Move','Rotate','Scale'):
                    for full in (False,True):
                        result += [('centered-%s-%s' % (mode,full), 'test.centered_object_drag(%r,full_frame=%r)' % (mode,full))]
                result += [('edge-cases','test.edge_cases()'),
                           ('shared-layer-lifetime','test.shared_layer_lifetime()')]
        return result + [('cleanup','test.cleanup()')]
    if name == 'testsharedviewport':
        fixture = '''import tempfile
from pxr import Usd,UsdGeom,Gf
from pathlib import Path
shared_file=Path(tempfile.mkdtemp(prefix='stageviz-shared-audit-'))/'fixture.usda'
f=Usd.Stage.CreateNew(str(shared_file))
w=UsdGeom.Xform.Define(f,'/World');f.SetDefaultPrim(w.GetPrim())
source=UsdGeom.Xform.Define(f,'/World/Source');source.AddTranslateOp().Set(Gf.Vec3d(-3,0,0))
mesh=UsdGeom.Mesh.Define(f,'/World/Source/Body')
mesh.GetPointsAttr().Set([(-.5,-.5,-.5),(.5,-.5,-.5),(.5,.5,-.5),(-.5,.5,-.5),(-.5,-.5,.5),(.5,-.5,.5),(.5,.5,.5),(-.5,.5,.5)])
mesh.GetFaceVertexCountsAttr().Set([4]*6)
mesh.GetFaceVertexIndicesAttr().Set([0,3,2,1,4,5,6,7,0,1,5,4,2,3,7,6,0,4,7,3,1,2,6,5])
for label,x in [('A',0),('B',3)]:
    p=UsdGeom.Xform.Define(f,'/World/Instance'+label)
    p.GetPrim().GetReferences().AddInternalReference('/World/Source')
    p.GetPrim().SetInstanceable(True)
    p.GetTranslateOp().Set(Gf.Vec3d(x,0,0))
f.GetRootLayer().Save()
'''
        return [('setup',load+'\n'+fixture+"\ntest=ns['SharedSourceViewportTest'](disposable=True);test.setup(str(shared_file))"),('sweep','test.sweep()')] + [('drag-'+m,'test.drag(%r)'%m) for m in ('Move','Rotate','Scale')] + [('cleanup','test.cleanup()')]
    if name in ('testaxissize','testconsole'):
        return [('suite',load+"; ns['run']()")]
    return [('suite',load+"; assert callable(ns.get('run')), 'Register the new suite in phases()'; ns['run'](disposable=True)")]


def payload_oracle(app, result):
    """Compare native MCP loaded flags with the phase's asserted USD/Qt snapshot."""
    lines = [line for block in result['result']['content']
             for line in block.get('text', '').splitlines()
             if line.startswith('PAYLOAD_CHECKPOINT ')]
    assert lines, 'Payload phase did not publish its checkpoint'
    checkpoint = json.loads(lines[-1].split(' ', 1)[1])
    first = '/World/RegionA/Vehicle01'
    proxy = '/World/Depot/InstanceA/PartA'
    expected = {path: gate['loaded'] for path, gate in checkpoint['payload_gates'].items()}
    if checkpoint['loaded'][first]:
        for path in (first + '/ModuleA/PartA/Bolts', first + '/ModuleA/PartB/Bolts'):
            expected[path] = checkpoint['loaded'][path]
    if checkpoint['loaded'][proxy]:
        expected[proxy + '/Bolts'] = checkpoint['loaded'][proxy + '/Bolts']
    evidence = {}
    for path, loaded in expected.items():
        response = app.tool('stageviz_get_prim_info', {'path': path})
        data = json.loads(response['result']['content'][0]['text'])
        assert data['loaded'] == loaded, (path, data, checkpoint)
        evidence[path] = response
    return evidence


def run(args):
    root = Path(__file__).resolve().parents[3]
    output = Path(args.output).resolve()
    output.mkdir(parents=True,exist_ok=False)
    names = args.suites or discover_suites(root)
    summary=[]
    for name in names:
        started=time.monotonic()
        row=dict(suite=name,passed=False,completed=[])
        try:
            with DisposableStageviz(args.app,output/name,port=args.port) as app:
                app.python("from PySide6 import QtCore,QtWidgets,QtOpenGLWidgets; import stageviz; w=stageviz.application().window(); w.resize(1500,1000); w.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint,True);w.show();w.raise_();w.activateWindow()")
                for label,code in phases(root,name, getattr(args, 'performance', False), getattr(args, 'performance_baseline', None)):
                    print('RUN',name,label,flush=True)
                    before=app.oracle()
                    result=app.python(code, timeout=120 if name in ('testpreservestate', 'testlayermultiselection', 'testchangesdialog') else 900)
                    after=app.oracle()
                    if name == 'testpayloadhierarchy' and label != 'cleanup':
                        after['payload_prims'] = payload_oracle(app, result)
                    if name == 'testpayloadindicator' and label != 'cleanup':
                        after['prims'] = app.tool('stageviz_list_prims', {'path': '/', 'maxDepth': 5, 'limit': 100})
                    if name == 'testexportinstances' and label != 'cleanup':
                        after['prims'] = app.tool('stageviz_list_prims', {'path': '/', 'maxDepth': 5, 'limit': 100})
                        after['instance'] = app.tool('stageviz_get_prim_info', {'path': '/World/Instance'})
                    if name == 'testgizmostability' and label != 'cleanup':
                        selection = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                        assert selection == ['/World/Sphere'], selection
                        after['fixture'] = {}
                        for path in ('/World/Sphere', '/World/Plane', '/Materials/Test'):
                            response = app.tool('stageviz_get_prim_info', {'path': path})
                            assert json.loads(response['result']['content'][0]['text'])['defined']
                            after['fixture'][path] = response
                    if name == 'testsourcechild' and label != 'cleanup':
                        after['prims'] = app.tool('stageviz_list_prims', {'path': '/World', 'maxDepth': 3, 'limit': 30})
                        after['source_child'] = app.tool('stageviz_get_prim_info', {'path': '/World/Source/Sphere'})
                    if name == 'testlayerupdate':
                        lines = [line for block in result['result']['content'] for line in block.get('text', '').splitlines()
                                 if line.startswith('LAYER_UPDATE_CHECKPOINT ')]
                        assert lines, 'Missing layer-update checkpoint'
                        checkpoint = json.loads(lines[-1].split(' ', 1)[1])
                        info = json.loads(after['stageviz_get_stage_info']['result']['content'][0]['text'])
                        selection = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                        assert info['filename'] == checkpoint['filename'] and selection == checkpoint['selection']
                        after['fixture'] = {}
                        for path in checkpoint['present']:
                            response = app.tool('stageviz_get_prim_info', {'path': path})
                            assert json.loads(response['result']['content'][0]['text'])['defined']
                            after['fixture'][path] = response
                    if name == 'testchangesdialog':
                        after['prims'] = app.tool('stageviz_list_prims', {'path': '/', 'maxDepth': 4, 'limit': 40})
                        lines = [line for block in result['result']['content'] for line in block.get('text', '').splitlines()
                                 if line.startswith('CHANGES_CHECKPOINT ')]
                        assert lines, 'Missing changes-dialog checkpoint'
                        checkpoint = json.loads(lines[-1].split(' ', 1)[1])
                        info = json.loads(after['stageviz_get_stage_info']['result']['content'][0]['text'])
                        selection = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                        assert info['filename'] == checkpoint['filename'], (info, checkpoint)
                        assert selection == checkpoint['selection'], (selection, checkpoint)
                        after['edited'] = {}
                        for path in checkpoint['present']:
                            response = app.tool('stageviz_get_prim_info', {'path': path})
                            assert json.loads(response['result']['content'][0]['text'])['defined']
                            after['edited'][path] = response
                    if name == 'testpreservestate':
                        lines = [line for block in result['result']['content'] for line in block.get('text', '').splitlines()
                                 if line.startswith('STATE_CHECKPOINT ')]
                        assert lines, 'Missing companion state checkpoint'
                        checkpoint = json.loads(lines[-1].split(' ', 1)[1])
                        info = json.loads(after['stageviz_get_stage_info']['result']['content'][0]['text'])
                        selection = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                        assert info['filename'] == checkpoint['filename'], (info, checkpoint)
                        assert selection == checkpoint['selection'], (selection, checkpoint)
                        if checkpoint['payloadLoaded'] is not None:
                            after['payload'] = app.tool('stageviz_get_prim_info', {'path': '/Payload'})
                            payload = json.loads(after['payload']['result']['content'][0]['text'])
                            assert payload['loaded'] == checkpoint['payloadLoaded'], (payload, checkpoint)
                    if name == 'testlayermultiselection' and label != 'cleanup':
                        after['prims'] = app.tool('stageviz_list_prims', {'path': '/', 'maxDepth': 3, 'limit': 30})
                    if name == 'testresetopinions' and label != 'cleanup':
                        after['prim'] = app.tool('stageviz_get_prim_info', {'path': '/Group/Sphere'})
                    if name == 'testmakeeditable' and label != 'cleanup':
                        after['instance'] = app.tool('stageviz_get_prim_info', {'path': '/World/First'})
                        after['child'] = app.tool('stageviz_get_prim_info', {'path': '/World/First/Cube'})
                    (app.output/(label+'.json')).write_text(json.dumps(dict(before=before,result=result,after=after),indent=2))
                    row['completed'].append(label)
                    print('PASS',name,label,flush=True)
                row['passed']=True
        except Exception as error:
            row['passed']=False
            row['error']=str(error)
            print('FAIL',name,str(error)[:300],flush=True)
        row['seconds']=time.monotonic()-started
        summary.append(row)
        (output/'summary.json').write_text(json.dumps(summary,indent=2))
    assert all(row['passed'] for row in summary), [(r['suite'],r.get('error','')[:300]) for r in summary if not r['passed']]
    print('PASS ALL',len(summary),'suites',sum(len(r['completed']) for r in summary),'phases',flush=True)


if __name__=='__main__':
    def terminate(signum, frame):
        raise KeyboardInterrupt('Test controller terminated')
    signal.signal(signal.SIGTERM, terminate)
    if hasattr(signal, 'SIGBREAK'):
        signal.signal(signal.SIGBREAK, terminate)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',required=True)
    parser.add_argument('--output',required=True)
    parser.add_argument('--suites',nargs='+')
    parser.add_argument('--performance', action='store_true', help='Include the API suite performance checks')
    parser.add_argument('--performance-baseline', help='Optional persistent API performance baseline file')
    parser.add_argument('--port',type=int,default=37657,help='Isolated MCP listener port')
    run(parser.parse_args())
