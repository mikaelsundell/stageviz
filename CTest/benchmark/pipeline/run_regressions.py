# SPDX-License-Identifier: BSD-3-Clause
"""Run the existing Python suites in isolated apps with external MCP evidence."""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mcp_runtime import DisposableStageviz


def phases(root, name):
    scripts = root / 'CTest/python'
    path = scripts / (name + '.py')
    load = "import runpy; ns=runpy.run_path(%r, init_globals={'_VIEWPORT_AUTORUN':False,'_SUBSETS_AUTORUN':False,'_VIEWPORT_DISPOSABLE':True})" % str(path)
    if name in ('testapi', 'testui'):
        extra = "; ns['run'].__globals__['_PERF_ENABLED']=False" if name == 'testapi' else ''
        return [('suite', load + extra + "; assert ns['run']() is True")]
    if name == 'testlayers':
        return [('suite', load + "; assert ns['_TEST_RESULT'] is True")]
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
                result += [('edge-cases','test.edge_cases()')]
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
    return [('suite',load+"; ns['run'](disposable=True)")]


def run(args):
    root = Path(__file__).resolve().parents[3]
    output = Path(args.output).resolve()
    output.mkdir(parents=True,exist_ok=False)
    names = args.suites or ['testapi','testui','testlayers','testviewport','testsubsets','testlights','testnumericeditors','testviewporthdri','testviewportcapture','testscenestats','testaxissize','testconsole','testhierarchytransforms','testtransformoperation','testpivot','testsharedviewport']
    summary=[]
    for name in names:
        started=time.monotonic()
        row=dict(suite=name,passed=False,completed=[])
        try:
            with DisposableStageviz(args.app,output/name) as app:
                app.python("from PySide6 import QtCore,QtWidgets,QtOpenGLWidgets; import stageviz; w=stageviz.application().window(); w.resize(1500,1000); w.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint,True);w.show();w.raise_();w.activateWindow()")
                for label,code in phases(root,name):
                    print('RUN',name,label,flush=True)
                    before=app.oracle()
                    result=app.python(code)
                    after=app.oracle()
                    (app.output/(label+'.json')).write_text(json.dumps(dict(before=before,result=result,after=after),indent=2))
                    row['completed'].append(label)
                    print('PASS',name,label,flush=True)
                row['passed']=True
        except Exception as error:
            row['error']=str(error)
            print('FAIL',name,str(error)[:300],flush=True)
        row['seconds']=time.monotonic()-started
        summary.append(row)
        (output/'summary.json').write_text(json.dumps(summary,indent=2))
    assert all(row['passed'] for row in summary), [(r['suite'],r.get('error','')[:300]) for r in summary if not r['passed']]
    print('PASS ALL',len(summary),'suites',sum(len(r['completed']) for r in summary),'phases',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',required=True)
    parser.add_argument('--output',required=True)
    parser.add_argument('--suites',nargs='+')
    run(parser.parse_args())
