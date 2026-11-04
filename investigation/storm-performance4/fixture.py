from pathlib import Path
import json
import math
import os
import sys
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
handles=[os.add_dll_directory(str(sdk/name)) for name in ('bin','lib')]
sys.path.insert(0,str(sdk/'lib/python'))
from pxr import Usd, UsdGeom

directory=Path(r'C:\Temp\usdtest\storm-cpu-fixture');directory.mkdir(exist_ok=True)
front=directory/'unique-topologies.usda';keep=directory/'keep.usda'
stage=Usd.Stage.CreateNew(str(front))
for index in range(300):
    count=index+3
    mesh=UsdGeom.Mesh.Define(stage,f'/Meshes/M{index}')
    mesh.CreateSubdivisionSchemeAttr('none')
    mesh.CreatePointsAttr([(index%30+0.4*math.cos(2*math.pi*i/count),index//30+0.4*math.sin(2*math.pi*i/count),0) for i in range(count)])
    mesh.CreateFaceVertexCountsAttr([count]);mesh.CreateFaceVertexIndicesAttr(list(range(count)))
assert stage.GetRootLayer().Save()
stage=Usd.Stage.CreateNew(str(keep));UsdGeom.Cube.Define(stage,'/Keep').CreateSizeAttr(0.25)
assert stage.GetRootLayer().Save()
profiles=[]
for name,budget,limit,visits in [('fixture-eager',0,1000000,1),('fixture-budget',20000,1000000,1),('fixture-pressure',20000,1,1)]:
    profiles.append({'name':name,'stagingMiB':64,
      'env':{'HDST_TOPOLOGY_GC_BUDGET_US':str(budget),'HDST_TOPOLOGY_GC_MAX_VISITS':str(visits),'HDST_TOPOLOGY_GC_MAX_ENTRIES':str(limit)},
      'view':{'layers':[str(front),str(keep)],'phases':['empty','add_front','add_axle','mute_front_1','settle_after_mute','unmute_front_1','settle_after_unmute']}})
(directory/'profiles.json').write_text(json.dumps({'profiles':profiles},indent=2))
print(directory/'profiles.json')
