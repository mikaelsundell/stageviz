"""Read-only USD composition control, without Qt, Hydra, or GPU resources."""
from pathlib import Path
import json
import os
import sys
import time

from run import LAYERS

prefix=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
handles=[os.add_dll_directory(str(prefix/subdir)) for subdir in ('bin','lib')]
sys.path.insert(0,str(prefix/'lib/python'))
from pxr import Sdf, Usd, UsdGeom

output=Path(r'C:\Temp\usdtest\performance\composition-control.json')
stage=Usd.Stage.CreateInMemory()
stage.SetDefaultPrim(stage.DefinePrim('/World','Xform'))
records=[]

def measure(name,operation):
    start=time.perf_counter();operation();elapsed=time.perf_counter()-start
    record={'phase':name,'seconds':elapsed,
            'active_prims':sum(1 for _ in stage.Traverse()),
            'all_prims':sum(1 for _ in stage.Traverse(Usd.PrimAllPrimsPredicate)),
            'all_with_proxies':sum(1 for _ in stage.Traverse(Usd.TraverseInstanceProxies(Usd.PrimAllPrimsPredicate))),
            'muted':stage.GetMutedLayers()}
    records.append(record)
    output.write_text(json.dumps({'cachePolicy':'Fresh process; existing OS/network caches; no Qt/Hydra; retained local sublayers','results':records},indent=2))
    print(json.dumps(record),flush=True)

measure('add_front',lambda:stage.GetRootLayer().subLayerPaths.append(LAYERS[0]))
measure('add_axle',lambda:stage.GetRootLayer().subLayerPaths.append(LAYERS[1]))
# Match Stageviz's retained local sublayer subtree for undo/unmute caching.
retained=[];seen=set()
def retain(layer):
    if not layer or layer.identifier in seen:return
    seen.add(layer.identifier);retained.append(layer)
    for path in layer.subLayerPaths:retain(Sdf.Layer.FindRelativeToLayer(layer,path))
retain(Sdf.Layer.Find(LAYERS[0]))
measure('mute_front',lambda:stage.MuteLayer(LAYERS[0]))
measure('unmute_front',lambda:stage.UnmuteLayer(LAYERS[0]))
front=UsdGeom.Imageable(stage.GetPrimAtPath('/RCC_B13RH/FA__1_'))
assert front
measure('hide_front_branch',front.MakeInvisible)
assert front.ComputeVisibility()=='invisible'
measure('show_front_branch',front.MakeVisible)
assert front.ComputeVisibility()=='inherited'
assert not any(layer.dirty for layer in stage.GetUsedLayers() if not layer.anonymous)
