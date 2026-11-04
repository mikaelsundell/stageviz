from pathlib import Path
import json,os,sys
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
handles=[os.add_dll_directory(str(sdk/name)) for name in ('bin','lib')]
sys.path.insert(0,str(sdk/'lib/python'))
from pxr import Usd,UsdGeom
root=Path(r'C:\Temp\usdtest\selection5-fixture');root.mkdir(exist_ok=True)
path=root/'grid.usda'
stage=Usd.Stage.CreateNew(str(path))
for i in range(100):
    xform=UsdGeom.Xform.Define(stage,'/Grid/Part'+str(i));xform.AddTranslateOp().Set((i%10,i//10,0))
    UsdGeom.Cube.Define(stage,xform.GetPath().AppendChild('Cube')).CreateSizeAttr(0.7)
stage.GetRootLayer().Save()
profile={'name':'selection-fixture','stagingMiB':64,'env':{'HDST_TOPOLOGY_GC_BUDGET_US':'0'},
         'view':{'layers':[str(path)],'sweepRect':[0.3,0.3,0.7,0.7]}}
(root/'profiles.json').write_text(json.dumps({'profiles':[profile]},indent=2))
print(root/'profiles.json')
