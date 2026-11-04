"""Inspect CAD transform types and anonymous-layer normalization; never save."""
from pathlib import Path
import json,os,sys
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
handles=[os.add_dll_directory(str(sdk/name)) for name in ('bin','lib')]
sys.path.insert(0,str(sdk/'lib/python'))
from pxr import Sdf,Usd,UsdGeom
base=Path(r'C:\Temp\usdtest\performance/20261104T164420Z-00-64MiB-volvo-cpu-transform-1000')
config=json.loads((base/'config.json').read_text())
seed=json.loads((base/'selection-domain.json').read_text())['seed']
stage=Usd.Stage.CreateInMemory();root=stage.GetRootLayer();root.subLayerPaths=config['layers']
stage.SetEditTarget(root)
for layer in stage.GetUsedLayers():
    if not layer.anonymous:layer.SetPermissionToEdit(False)
result={'editTargetAnonymous':stage.GetEditTarget().GetLayer().anonymous,'samples':[]}
for path in seed[:8]:
    prim=stage.GetPrimAtPath(path);attr=prim.GetAttribute('xformOp:transform')
    result['samples'].append({'path':path,'instanceProxy':prim.IsInstanceProxy(),
        'existingType':str(attr.GetTypeName()) if attr else None,
        'xformOrder':list(UsdGeom.Xformable(prim).GetXformOpOrderAttr().Get() or [])})
path=seed[0];prim=stage.GetPrimAtPath(path)
with Sdf.ChangeBlock():
    attr=prim.CreateAttribute('xformOp:transform',Sdf.ValueTypeNames.Matrix4d,False)
    result['insideChangeBlock']={'valid':bool(attr),'type':str(attr.GetTypeName())}
result['afterChangeBlock']={'valid':bool(attr),'type':str(attr.GetTypeName()),'rootHasProperty':bool(root.GetPropertyAtPath(path+'.xformOp:transform'))}
assert not [layer.identifier for layer in stage.GetUsedLayers() if not layer.anonymous and layer.dirty]
print(json.dumps(result,indent=2))
