import os
from pathlib import Path
import stageviz
from pxr import Sdf
directory = Path(os.environ['STORM_TRACE_DIR'])
root = stageviz.session().stage().GetRootLayer()
wrapper = Sdf.Layer.CreateNew(str(directory/'visible-wrapper.usda'))
wrapper.subLayerPaths = [root.identifier]
for key in ('upAxis', 'metersPerUnit', 'startTimeCode', 'endTimeCode', 'timeCodesPerSecond'):
    if root.pseudoRoot.HasInfo(key):
        wrapper.pseudoRoot.SetInfo(key, root.pseudoRoot.GetInfo(key))
wrapper.Save()
assert stageviz.session().load(wrapper.identifier)
print('Loaded disposable wrapper', wrapper.identifier)
