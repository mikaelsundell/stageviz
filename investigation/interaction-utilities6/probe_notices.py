"""Inspect the notice contract for stronger transform overs on a tiny fixture."""
from pathlib import Path
import json
import os
import sys

sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
handles=[os.add_dll_directory(str(sdk/name)) for name in ('lib','bin')]
sys.path.insert(0,str(sdk/'lib/python'))
from pxr import Gf, Sdf, Tf, Usd, UsdGeom, Vt

source=Usd.Stage.CreateInMemory()
parent=UsdGeom.Xform.Define(source,'/World/Parent')
parent.AddTransformOp().Set(Gf.Matrix4d(1))
UsdGeom.Cube.Define(source,'/World/Parent/Cube')
stage=Usd.Stage.CreateInMemory()
root=stage.GetRootLayer()
root.subLayerPaths=[source.GetRootLayer().identifier]
phase=['prepare'];events=[]
def notice(change,sender):
    events.append(dict(phase=phase[0],info=[dict(path=str(p),fields=list(change.GetChangedFields(p)))
                  for p in change.GetChangedInfoOnlyPaths()],resync=list(map(str,change.GetResyncedPaths()))))
listener=Tf.Notice.Register(Usd.Notice.ObjectsChanged,notice,stage)
with Sdf.ChangeBlock():
    prim=Sdf.CreatePrimInLayer(root,'/World/Parent')
    matrix=Sdf.AttributeSpec(prim,'xformOp:transform',Sdf.ValueTypeNames.Matrix4d)
    matrix.default=Gf.Matrix4d(1)
    order=Sdf.AttributeSpec(prim,'xformOpOrder',Sdf.ValueTypeNames.TokenArray,Sdf.VariabilityUniform)
    order.default=Vt.TokenArray(['xformOp:transform'])
phase[0]='update'
matrix.default=Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(1,2,3))
phase[0]='undo'
with Sdf.ChangeBlock():
    prim.RemoveProperty(matrix)
    prim.RemoveProperty(order)
    edits=Sdf.BatchNamespaceEdit()
    edits.Add('/World/Parent',Sdf.Path.emptyPath)
    assert root.Apply(edits)
print(json.dumps(events,indent=2))
