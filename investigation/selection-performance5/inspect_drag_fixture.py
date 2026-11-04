import sys
sys.path.insert(0,'investigation/performance')
import run
print(run.tool('stageviz_run_python',{'code':'''b=stageviz._selectionBenchmark
import difflib,json
from pxr import Gf
s=stageviz.session()
print('prims state',s.primsUpdate(),'expected',b.expected(),'restored',b.restored())
print(''.join(difflib.unified_diff(b.root_before.splitlines(True),s.stage().GetRootLayer().ExportToString().splitlines(True))))
print([(p,str(v),str(b.world_before[p])) for p,v in b.worlds(b.world_before).items() if not Gf.IsClose(v,b.world_before[p],1e-7)][:3])
b.timer.stop()
b.directory.joinpath('excluded.json').write_text('"CPU drag fixture restoration check failed; diagnostic inspection clears history"')
b.write_status('failed',error='CPU drag fixture restoration check failed; see inspection')
'''}))
