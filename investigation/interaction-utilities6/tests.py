"""Run registered tests with the matching Release SDK and no OS error dialogs."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
sdk=Path(r'C:\git\3rdparty\build\win\vc143\x86_amd64\Release')
env=os.environ.copy()
env['PATH']=';'.join(map(str,[sdk/'lib',sdk/'bin',Path(sys.executable).parent]))+';'+env['PATH']
env['QT_PLUGIN_PATH']=str(sdk/'plugins')
env['STAGEVIZ_TEST_FAILURE_LOG']=str(root/'build.Release/interaction-test-failures.log')
ctypes.windll.kernel32.SetErrorMode(0x0001|0x0002|0x8000)
result=subprocess.run(['ctest','--test-dir',str(root/'build.Release'),'-C','Release','--output-on-failure',
                       '-R',sys.argv[1] if len(sys.argv)>1 else 'stageviz\\.(indexed_path_api|batched_transform_authoring|transform_operation_api|usd_path_utils_api)$'],env=env)
raise SystemExit(result.returncode)
