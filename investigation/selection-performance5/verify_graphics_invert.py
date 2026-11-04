"""Independently check screen-sweep inversion against the original USD data."""
from pathlib import Path
import json,os,sys
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
handles=[os.add_dll_directory(str(sdk/name)) for name in ('bin','lib')]
sys.path.insert(0,str(sdk/'lib/python'))
from pxr import Sdf,Usd,UsdGeom

directory=Path(sys.argv[1])
config=json.loads((directory/'config.json').read_text())
rows=json.loads((directory/'results.json').read_text())
by_name={row['phase']:row for row in rows}
before=by_name['sweep_warm']['selection'];actual=set(by_name['invert_selection']['selection'])
stage=Usd.Stage.CreateInMemory();root=stage.GetRootLayer()
UsdGeom.Xform.Define(stage,'/World');root.subLayerPaths=config['layers']
for layer in stage.GetUsedLayers():
    if not layer.anonymous:layer.SetPermissionToEdit(False)
selected={Sdf.Path(path) for path in before};expected=set();domain_count=0
for prim in stage.Traverse():
    if prim.IsA(UsdGeom.Subset) or any(not child.IsA(UsdGeom.Subset) for child in prim.GetChildren()):continue
    domain_count+=1;path=prim.GetPath();ancestor=path
    while ancestor!=Sdf.Path.emptyPath and ancestor not in selected:ancestor=ancestor.GetParentPath()
    if ancestor==Sdf.Path.emptyPath:expected.add(str(path))
assert expected==actual,{'missing':len(expected-actual),'extra':len(actual-expected)}
proxy_count=0;overlapping_instance_roots=set()
for path in before:
    prim=stage.GetPrimAtPath(path)
    if not prim or not prim.IsInstanceProxy():continue
    proxy_count+=1;ancestor=prim.GetParent()
    while ancestor and not ancestor.IsInstance():ancestor=ancestor.GetParent()
    if ancestor and str(ancestor.GetPath()) in actual:overlapping_instance_roots.add(str(ancestor.GetPath()))
assert not [layer.identifier for layer in stage.GetUsedLayers() if not layer.anonymous and layer.dirty]
report={'verified':True,'selectedBefore':len(before),'domainCount':domain_count,
        'expectedAfter':len(expected),'actualAfter':len(actual),
        'canonicalNamespaceCoverage':True,'selectedProxyPaths':proxy_count,
        'invertedInstanceRootsContainingPreviouslySelectedProxies':len(overlapping_instance_roots),
        'sourceLayersSaved':False}
(directory/'external-invert-verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
