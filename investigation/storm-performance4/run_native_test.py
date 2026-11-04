"""Run the focused native test with explicit SDK dependencies and no error UI."""
import ctypes
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parent
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
python=Path(r'C:\Users\msundell\AppData\Local\Programs\Python\Python39')
environment=os.environ.copy()
environment['PATH']=';'.join(map(str,[sdk/'lib',sdk/'bin',python]))+';'+environment['PATH']
# Children inherit this mode. Missing dependencies must produce a failing exit
# status rather than leaving an application-initialization dialog on screen.
ctypes.windll.kernel32.SetErrorMode(0x0001|0x0002|0x8000)
result=subprocess.run([str(root/'build/Release/testHdInstanceRegistryBudget.exe')],env=environment)
raise SystemExit(result.returncode)
