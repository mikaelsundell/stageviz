import json
import os
from pathlib import Path
import stageviz
from pxr import UsdGeom
session=stageviz.session()
stage=session.stage()
target=os.environ['STORM_TRACE_PRIM']
meshes=[p for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
hidden=sum(UsdGeom.Imageable(p).ComputeVisibility(0)=='invisible' for p in meshes)
report={'filename':stage.GetRootLayer().identifier,'prims':sum(1 for p in stage.Traverse()),'meshes':len(meshes),'hiddenMeshes':hidden,'targetVisibility':str(UsdGeom.Imageable(stage.GetPrimAtPath(target)).ComputeVisibility(0)),'rootDirty':stage.GetRootLayer().dirty,'originalDirty':next(l.dirty for l in stage.GetLayerStack() if l.realPath and Path(l.realPath)==Path(r'C:\Temp\usdtest\TEMP_asm.usd'))}
assert report['meshes']==38349
assert not report['originalDirty']
label=os.environ.get('STORM_CAPTURE_LABEL','verify')
(Path(os.environ['STORM_TRACE_DIR'])/(label+'-verification.json')).write_text(json.dumps(report,indent=2))
print(report)
