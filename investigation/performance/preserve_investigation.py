"""Keep the investigation archive across CLEAN extraction of the USD source."""
from pathlib import Path
import shutil
import sys

mode=sys.argv[1]
root=Path(sys.argv[2]).resolve()
archive=root/'stageviz-investigation'
saved=root.parent/(root.name+'.stageviz-investigation.saved')
assert archive.parent.resolve()==root
assert saved.parent.resolve()==root.parent
if mode=='stash' and archive.exists():
    if archive.resolve().parent!=root:raise SystemExit('Refusing an external archive junction')
    if saved.exists():raise SystemExit('An investigation backup already exists: '+str(saved))
    shutil.move(str(archive),str(saved))
elif mode=='restore' and saved.exists():
    if saved.resolve().parent!=root.parent:raise SystemExit('Refusing an external saved archive junction')
    if archive.exists():raise SystemExit('Refusing to overwrite an existing investigation archive')
    root.mkdir(parents=True,exist_ok=True)
    shutil.move(str(saved),str(archive))
elif mode not in ('stash','restore'):raise SystemExit('Expected stash or restore')
print('Investigation archive preservation:',mode)
