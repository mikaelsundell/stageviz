from pathlib import Path
import json
root=Path(__file__).resolve().parent
dataset=r'\\shared-fs2\clients\volvobuses\assets\received\261009_chassis_updates\B13R_4x2_6x2_Rear_I-pump\LHD\assembly\LHD_Front.usd'
base={'stagingMiB':64,'env':{'HDST_TOPOLOGY_GC_BUDGET_US':'0'}}
profiles=[{**base,'name':'volvo-sweep-move-baseline','view':{'layers':[dataset],'sweepRect':[0.48,0.48,0.62,0.62]}}]
(root/'profiles.json').write_text(json.dumps({'profiles':profiles},indent=2))
profiles=[{**base,'name':'volvo-cpu-invert-'+str(count),'view':{'layers':[dataset],'cpuOnly':True,'seedCount':count}} for count in (100,1000)]
(root/'cpu-profiles.json').write_text(json.dumps({'profiles':profiles},indent=2))
fixture=Path(r'C:\Temp\usdtest\selection5-fixture/grid.usda')
(root/'cpu-fixture.json').write_text(json.dumps({'profiles':[{**base,'name':'selection-cpu-fixture','view':{'layers':[str(fixture)],'cpuOnly':True,'seedCount':10}}]},indent=2))
(root/'cpu-transform-fixture.json').write_text(json.dumps({'profiles':[{**base,'name':'selection-cpu-transform-fixture','view':{'layers':[str(fixture)],'cpuTransform':True,'seedCount':10}}]},indent=2))
(root/'cpu-invert-large.json').write_text(json.dumps({'profiles':[{**base,'name':'volvo-cpu-invert-10000','view':{'layers':[dataset],'cpuOnly':True,'seedCount':10000}}]},indent=2))
(root/'cpu-transform.json').write_text(json.dumps({'profiles':[{**base,'name':'volvo-cpu-transform-1000','view':{'layers':[dataset],'cpuTransform':True,'seedCount':1000}}]},indent=2))
print('Wrote real-data and CPU-control profiles')
