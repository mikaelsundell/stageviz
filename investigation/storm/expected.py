import json
import os
from pathlib import Path
import struct
import stageviz
from pxr import UsdGeom
directory = Path(os.environ['STORM_TRACE_DIR'])
prim = stageviz.session().stage().GetPrimAtPath(os.environ['STORM_TRACE_PRIM'])
mesh = UsdGeom.Mesh(prim)
points = mesh.GetPointsAttr().Get(0)
indices = list(mesh.GetFaceVertexIndicesAttr().Get(0))
counts = list(mesh.GetFaceVertexCountsAttr().Get(0))
assert set(counts)=={3}
(directory/'expected-points.bin').write_bytes(struct.pack('<%df'%(3*len(points)), *[float(v) for p in points for v in p]))
(directory/'expected-indices.bin').write_bytes(struct.pack('<%di'%len(indices), *indices))
normals = UsdGeom.PrimvarsAPI(prim).GetPrimvar('normals').ComputeFlattened(0)
(directory/'expected-normals.bin').write_bytes(struct.pack('<%df'%(3*len(normals)), *[float(v) for p in normals for v in p]))
report = {'points':len(points), 'indices':len(indices), 'faces':len(counts), 'orientation':str(mesh.GetOrientationAttr().Get()), 'normals':len(normals), 'normalInterpolation':str(UsdGeom.PrimvarsAPI(prim).GetPrimvar('normals').GetInterpolation())}
(directory/'expected.json').write_text(json.dumps(report, indent=2))
print(report)
