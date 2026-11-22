# SPDX-License-Identifier: BSD-3-Clause
"""Run the existing Python suites in isolated apps with external MCP evidence."""
import argparse
import json
from pathlib import Path
import sys
import signal
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mcp_runtime import DisposableStageviz, WindowsDesktopUnavailable


DEFAULT_SUITES = ['testapi','testui','testlayers','testpayloadhierarchy','testpayloadindicator','testviewport','testgizmostability','testsubsets','testlights','testnumericeditors','testviewporthdri','testviewportcapture','testscenestats','testaxissize','testconsole','testhierarchytransforms','testtransformoperation','testpivot','testsharedviewport','testxformgrouping','testlayercolors','testexportinstances','testsourcechild','testmakeeditable','testlayermultiselection','testlayerupdate','testclearopinions','testpreservestate','testchangesdialog']


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
    if name == 'testselectionoutline':
        return [('setup', load + "; test=ns['SelectionOutlineTest'](disposable=True); test.setup()"),
                ('overlap', 'test.overlap()'), ('cleanup', 'test.cleanup()')]
    if name == 'testisolationinstances':
        return [('setup', load + "; test=ns['IsolationInstancesTest'](disposable=True); test.setup()"),
                ('isolate', 'test.isolate()'), ('cleanup', 'test.cleanup()'),
                ('ordinary-setup', "test=ns['IsolationInstancesTest'](disposable=True,instanced=False); test.setup()"),
                ('ordinary-isolate', 'test.isolate()'), ('ordinary-cleanup', 'test.cleanup()')]
    if name == 'testmaterialassignment':
        return [('setup', load + "; test=ns['MaterialAssignmentTest'](disposable=True); test.setup()"),
                ('api', 'test.api()'), ('editor-material-menu', 'test.ui()'),
                ('viewport-drop', 'test.drop()'), ('cleanup', 'test.cleanup()')]
    if name == 'testexportinstances':
        return [('setup', load + "; test=ns['ExportInstancesTest'](disposable=True); test.setup()"),
                ('selected', 'test.selected()'), ('cleanup', 'test.cleanup()')]
    if name == 'testgizmostability':
        return [('setup', load + "; test=ns['GizmoStabilityTest'](disposable=True); test.setup()"),
                ('reload', 'test.reload()'), ('orbit', 'test.orbit()'), ('distances', 'test.distances()'), ('edge-on', 'test.edge_on()'), ('option-drag', 'test.option_drag()'), ('move-axis-on', 'test.move_axis_on()'), ('clipping', 'test.clipping()'), ('cleanup', 'test.cleanup()')]
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
                ('multi-menu', 'test.multiple()'),
                ('source-availability', 'test.source_availability()'),
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
    if name == 'testpackagedmaterialassignment':
        return [('setup', load + "; test=ns['PackagedMaterialAssignmentTest'](disposable=True); test.setup()"),
                ('menu', 'test.menu()'), ('assignment', 'test.assignment()'),
                ('subsets', 'test.subsets()'), ('follow', 'test.follow()'), ('cleanup', 'test.cleanup()')]
    if name == 'testcrosslayercreation':
        return [('setup', load + "; test=ns['CrossLayerCreationTest'](disposable=True); test.setup()"),
                ('create-history', 'test.create(); test.history()'), ('cleanup', 'test.cleanup()')]
    if name == 'testviewportpurpose':
        return [('setup', load + "; test=ns['ViewportPurposeTest'](disposable=True); test.setup()"),
                ('menu', 'test.menu()'), ('isolation', 'test.isolation()'),
                ('persistence', 'test.persistence()'), ('cleanup', 'test.cleanup()')]
    if name == 'testmaterialviewportcomposition':
        return [('setup', load + "; test=ns['MaterialViewportCompositionTest'](disposable=True); test.setup()"),
                ('toggle', 'test.toggle()'), ('isolated', 'test.isolated()'), ('availability', 'test.availability()'), ('cleanup', 'test.cleanup()')]
    if name == 'testinstancecleanup':
        return [('setup', load + "; test=ns['InstanceCleanupTest'](disposable=True); test.setup()"),
                ('shared', 'test.shared()'), ('last', 'test.last()'), ('cleanup', 'test.cleanup()')]
    if name == 'testcameraclipping':
        return [('setup', load + "; test=ns['CameraClippingTest'](disposable=True); test.setup()"),
                ('move-tumble', 'test.move_and_tumble()'), ('cleanup', 'test.cleanup()')]
    if name == 'testlayerauthoring':
        return [('setup', load + "; test=ns['LayerAuthoringTest'](disposable=True); test.setup()"),
                ('override', 'test.override()'), ('outliner-replace', 'test.outliner_replace()'),
                ('browser-replace', 'test.browser_replace()'), ('bindings', 'test.bindings()'),
                ('cleanup', 'test.cleanup()')]
    if name == 'testmaterialcomposition':
        return [('setup', load + "; test=ns['MaterialCompositionTest'](disposable=True); test.setup()"),
                ('references', 'test.references_phase()'), ('switching', 'test.switching()'),
                ('reload', 'test.reload_composition()'), ('opinions', 'test.opinions()'), ('cleanup', 'test.cleanup()')]
    if name == 'testmaterialcontainer':
        return [('setup', load + "; test=ns['MaterialContainerTest'](disposable=True); test.setup()"),
                ('create', 'test.create()'), ('mute', 'test.mute()'), ('restore', 'test.restore()'), ('cleanup', 'test.cleanup()')]
    if name == 'testmateriallayerrename':
        return [('setup', load + "; test=ns['MaterialLayerRenameTest'](disposable=True); test.setup()"),
                ('rename-tooltips', 'test.exercise()'), ('cleanup', 'test.cleanup()')]
    if name == 'testtooltips':
        return [('setup', load + "; test=ns['TooltipsTest'](disposable=True); test.setup()"),
                ('tooltips', 'test.exercise()'), ('cleanup', 'test.cleanup()')]
    if name == 'testoutlinerpurpose':
        return [('setup', load + "; test=ns['OutlinerPurposeTest'](disposable=True); test.setup()"),
                ('filters', 'test.exercise()'), ('cleanup', 'test.cleanup()')]
    if name == 'testcontextmenu':
        return [('setup', load + "; test=ns['ContextMenuTest'](disposable=True); test.setup()"),
                ('structure', 'test.structure()'), ('actions', 'test.actions()'), ('cleanup', 'test.cleanup()')]
    if name == 'testattributetarget':
        return [('setup', load + "; test=ns['AttributeTargetTest'](disposable=True); test.setup()"),
                ('target-history', 'test.exercise()'), ('pivot-history', 'test.pivot_history()'),
                ('cleanup', 'test.cleanup()')]
    if name == 'testclearopinions':
        return [('setup', load + "; test=ns['ClearOpinionsTest'](disposable=True); test.setup()"),
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
            with DisposableStageviz(args.app,output/name,port=args.port,
                                    configuration=getattr(args, 'configuration', None)) as app:
                # Changing native window flags on Windows recreates the HWND
                # and can invalidate an already initialized QOpenGLWidget.
                app.python("from PySide6 import QtCore,QtWidgets,QtOpenGLWidgets; import stageviz,sys; w=stageviz.application().window(); w.resize(1500,1000)\n"
                           "if sys.platform != 'win32': w.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint,True)\n"
                           "w.show()\n"
                           "if sys.platform == 'win32':\n"
                           " import ctypes\n"
                           " assert ctypes.windll.user32.SetWindowPos(ctypes.c_void_p(int(w.winId())),ctypes.c_void_p(-1),0,0,0,0,0x13), 'Could not keep test viewer on top'\n"
                           "w.raise_();w.activateWindow()")
                for label,code in phases(root,name, getattr(args, 'performance', False), getattr(args, 'performance_baseline', None)):
                    print('RUN',name,label,flush=True)
                    before=app.oracle()
                    # The complete API/performance suite is one request and can
                    # exceed fifteen minutes in an unoptimized Windows build.
                    timeout = 900
                    if name == 'testapi':
                        timeout = 1800
                    elif name in ('testpreservestate', 'testlayermultiselection', 'testchangesdialog'):
                        timeout = 120
                    result=app.python(code, timeout=timeout)
                    after=app.oracle()
                    if name == 'testattributetarget' and label != 'cleanup':
                        selection = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                        assert selection == (['/Asset/Matrix'] if label == 'pivot-history' else ['/Asset']), selection
                        after['fixture'] = {}
                        for path in ('/Asset', '/Asset/Matrix'):
                            response = app.tool('stageviz_get_prim_info', {'path': path})
                            assert json.loads(response['result']['content'][0]['text'])['defined']
                            after['fixture'][path] = response
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
                    if name == 'testpackagedmaterialassignment' and label != 'cleanup':
                        after['mesh']=app.tool('stageviz_get_prim_info', {'path':'/Asset/Mesh'})
                        assert json.loads(after['mesh']['result']['content'][0]['text'])['type']=='Mesh'
                        after['subsets']=app.tool('stageviz_list_prims', {'path':'/Asset/Mesh','maxDepth':1,'limit':10})
                        assert len([p for p in json.loads(after['subsets']['result']['content'][0]['text'])['prims'] if p['type']=='GeomSubset'])==2, after['subsets']
                    if name == 'testcrosslayercreation' and label != 'cleanup':
                        after['children']=app.tool('stageviz_list_prims', {'path':'/World','maxDepth':1,'limit':10})
                        paths={p['path'] for p in json.loads(after['children']['result']['content'][0]['text'])['prims']}
                        assert '/World/Existing' in paths
                        assert '/World/Xform' not in paths, paths
                    if name == 'testviewportpurpose' and label != 'cleanup':
                        lines = [line for block in result['result']['content'] for line in block.get('text','').splitlines()
                                 if line.startswith('VIEWPORT_PURPOSE_CHECKPOINT ')]
                        expected=json.loads(lines[-1].split(' ',1)[1])
                        after['prims']=app.tool('stageviz_list_prims', {'path':'/World','maxDepth':3,'limit':30})
                        prims=json.loads(after['prims']['result']['content'][0]['text'])['prims']
                        assert len([p for p in prims if p['type']=='Sphere'])==4, prims
                    if name == 'testmaterialviewportcomposition' and label != 'cleanup':
                        lines = [line for block in result['result']['content'] for line in block.get('text','').splitlines()
                                 if line.startswith('MATERIAL_VIEWPORT_CHECKPOINT ')]
                        expected = json.loads(lines[-1].split(' ',1)[1])
                        after['material'] = app.tool('stageviz_get_prim_info', {'path':'/Materials/Paint'})
                        after['prims'] = app.tool('stageviz_list_prims', {'path':'/Materials/Paint','maxDepth':2,'limit':10})
                        material = json.loads(after['material']['result']['content'][0]['text'])
                        assert (material['strongestLayer']==expected['layer']) == (not expected['muted']), (material, expected)
                    if name == 'testinstancecleanup' and label != 'cleanup':
                        lines = [line for block in result['result']['content'] for line in block.get('text', '').splitlines()
                                 if line.startswith('INSTANCE_CLEANUP_CHECKPOINT ')]
                        expected = json.loads(lines[-1].split(' ', 1)[1])
                        after['prims'] = app.tool('stageviz_list_prims', {'path': '/', 'maxDepth': 1, 'limit': 30})
                        prims = json.loads(after['prims']['result']['content'][0]['text'])['prims']
                        assert sorted(p['path'] for p in prims) == expected['roots'], (prims, expected)
                        after['helper'] = app.rpc('tools/call', {'name': 'stageviz_get_prim_info', 'arguments': {'path': expected['helper']}})
                        helper = after['helper']['result']
                        assert (not helper.get('isError', False)) == expected['helperExists'], (helper, expected)
                    if name == 'testcameraclipping' and label != 'cleanup':
                        after['prims'] = app.tool('stageviz_list_prims', {'path':'/World', 'maxDepth':2, 'limit':10})
                        prims = json.loads(after['prims']['result']['content'][0]['text'])['prims']
                        assert {'/World/Left','/World/Right'}.issubset({p['path'] for p in prims})
                        after['right'] = app.tool('stageviz_get_prim_info', {'path':'/World/Right'})
                        assert json.loads(after['right']['result']['content'][0]['text'])['defined']
                    if name == 'testlayerauthoring' and label != 'cleanup':
                        lines = [line for block in result['result']['content'] for line in block.get('text','').splitlines()
                                 if line.startswith('AUTHORING_CHECKPOINT ')]
                        expected = json.loads(lines[-1].split(' ',1)[1])
                        after['material'] = app.tool('stageviz_get_prim_info', {'path':expected['material']})
                        material = json.loads(after['material']['result']['content'][0]['text'])
                        assert material['defined'] and not expected['muted']
                        selection = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                        assert selection == expected['selection']
                    if name == 'testmaterialcomposition' and label in ('references', 'switching'):
                        lines = [line for block in result['result']['content']
                                 for line in block.get('text','').splitlines() if line.startswith('COMPOSITION_CHECKPOINT ')]
                        expected = json.loads(lines[-1].split(' ',1)[1])
                        after['prims'] = app.tool('stageviz_list_prims', {'path':'/', 'maxDepth':4, 'limit':100})
                        prims = json.loads(after['prims']['result']['content'][0]['text'])['prims']
                        assert any(p['path'] == expected['path'] for p in prims) == expected['present']
                        if expected['present']:
                            after['material'] = app.tool('stageviz_get_prim_info', {'path': expected['path']})
                            material = json.loads(after['material']['result']['content'][0]['text'])
                            assert material['defined']
                    if name == 'testmaterialcontainer' and label in ('create', 'mute'):
                        checkpoints = [line for block in result['result']['content']
                                       for line in block.get('text', '').splitlines()
                                       if line.startswith('CONTAINER_CHECKPOINT ')]
                        assert checkpoints
                        expected = json.loads(checkpoints[-1].split(' ', 1)[1])
                        after['material'] = app.tool('stageviz_get_prim_info', {'path': expected['path']})
                        material = json.loads(after['material']['result']['content'][0]['text'])
                        assert material['defined'] and material['definingLayer'] == expected['layer']
                    if name == 'testmateriallayerrename' and label != 'cleanup':
                        checkpoints = [line for block in result['result']['content']
                                       for line in block.get('text', '').splitlines()
                                       if line.startswith('RENAME_CHECKPOINT ')]
                        assert checkpoints, 'Missing layered rename checkpoint'
                        expected = json.loads(checkpoints[-1].split(' ', 1)[1])
                        after['material'] = app.tool('stageviz_get_prim_info', {'path': expected['path']})
                        actual = json.loads(after['material']['result']['content'][0]['text'])
                        assert all(actual[key] == expected[key] for key in ('definingLayer', 'strongestLayer', 'editLayer')), (actual, expected)
                        after['parent'] = app.tool('stageviz_get_prim_info', {'path': '/Materials'})
                        parent = json.loads(after['parent']['result']['content'][0]['text'])
                        assert parent['strongestLayer'] != actual['editLayer'], (parent, actual)
                    if name == 'testtooltips' and label != 'cleanup':
                        after['shape'] = app.tool('stageviz_get_prim_info', {'path': '/World/Group/Shape'})
                        after['material'] = app.tool('stageviz_get_prim_info', {'path': '/Materials/Paint'})
                        assert json.loads(after['shape']['result']['content'][0]['text'])['defined']
                    if name == 'testoutlinerpurpose' and label != 'cleanup':
                        after['proxy'] = app.tool('stageviz_get_prim_info', {'path': '/World/Instance/Cube'})
                        assert json.loads(after['proxy']['result']['content'][0]['text'])['instanceProxy']
                    if name == 'testcontextmenu' and label != 'cleanup':
                        after['child'] = app.tool('stageviz_get_prim_info', {'path': '/World/Group/Cube'})
                        after['proxy'] = app.tool('stageviz_get_prim_info', {'path': '/World/Instance/Cube'})
                        assert json.loads(after['proxy']['result']['content'][0]['text'])['instanceProxy']
                        child = json.loads(after['child']['result']['content'][0]['text'])
                        assert child['defined'] and 'link' in child['relationships'], child
                        checkpoints = [line for block in result['result']['content']
                                       for line in block.get('text', '').splitlines()
                                       if line.startswith('CONTEXT_CHECKPOINT ')]
                        if checkpoints:
                            expected = json.loads(checkpoints[-1].split(' ', 1)[1])
                            actual = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                            assert actual == expected['selection'], (actual, expected)
                    if name == 'testclearopinions' and label != 'cleanup':
                        after['prim'] = app.tool('stageviz_get_prim_info', {'path': '/Group/Sphere'})
                    if name == 'testmaterialassignment' and label != 'cleanup':
                        after['instances'] = {}
                        prototypes = []
                        for path in ('/World/A', '/World/B', '/World/A/Group/Left'):
                            response = app.tool('stageviz_get_prim_info', {'path': path})
                            prim = json.loads(response['result']['content'][0]['text'])
                            assert prim['instanceProxy'] if path.endswith('/Left') else prim['instance'], prim
                            if prim['instance']:
                                prototypes.append(prim['prototype'])
                            after['instances'][path] = response
                        assert len(set(prototypes)) == 1, prototypes
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
            if isinstance(error, WindowsDesktopUnavailable):
                row['desktop_unavailable'] = True
            failure = output / name / 'failure.txt'
            failure.parent.mkdir(parents=True, exist_ok=True)
            failure.write_text(str(error), encoding='utf-8')
            print('FAIL',name,str(error)[:300], 'Full details:', str(failure),flush=True)
        row['seconds']=time.monotonic()-started
        summary.append(row)
        (output/'summary.json').write_text(json.dumps(summary,indent=2))
        if row.get('desktop_unavailable'):
            break
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
    parser.add_argument('--configuration', choices=('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'),
                        help='Expected build configuration; macOS also verifies linked Qt framework names')
    parser.add_argument('--performance', action='store_true', help='Include the API suite performance checks')
    parser.add_argument('--performance-baseline', help='Optional persistent API performance baseline file')
    parser.add_argument('--port',type=int,default=37657,help='Isolated MCP listener port')
    run(parser.parse_args())
