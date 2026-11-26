#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Launch isolated copies to verify startup order, errors and the opt-out flag."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent / 'benchmark'))
from mcp_runtime import qt_runtime_environment


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--app', required=True)
    parser.add_argument('--configuration', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='stageviz-startup-test-') as directory:
        root = Path(directory)
        appdir, home = root / 'app', root / 'home'
        appdir.mkdir()
        home.mkdir()
        app = appdir / Path(args.app).name
        shutil.copy2(args.app, app)
        environment = qt_runtime_environment(os.environ, args.configuration, Path(args.app))
        environment['PATH'] = str(Path(args.app).resolve().parent) + os.pathsep + environment.get('PATH', '')
        environment['HOME'] = str(home)
        environment['USERPROFILE'] = str(home)
        environment['STAGEVIZ_TEST_SETTINGS_DIR'] = str(root / 'settings')
        app_script = appdir / 'stageviz.py'
        user_script = home / 'stageviz.py'
        app_script.write_text('''import builtins, stageviz
from pathlib import Path
builtins._startup_order = ['app']
assert stageviz.application().window() is not None
assert Path(__file__).parent.name == 'app'
def loader(source, options):
    from pxr import Usd, UsdGeom
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Sphere.Define(stage, '/StartupSphere')
    return stage
stageviz.plugins.registerFileFormat('Startup fixture', ['startupfixture'], loader=loader)
''')
        user_script.write_text('''import builtins
from pathlib import Path
assert Path(__file__).parent.name == 'home'
builtins._startup_order.append('user')
''')
        source = root / 'initial.startupfixture'
        source.write_text('synthetic fixture')

        def launch(check, arguments=()):
            result = root / 'result.txt'
            result.unlink(missing_ok=True)
            code = """import traceback
try:
    from PySide6 import QtCore
except ImportError:
    from PyQt6 import QtCore
try:
%s
    open(%r, 'w').write('passed')
except BaseException:
    open(%r, 'w').write(traceback.format_exc())
finally:
    QtCore.QCoreApplication.quit()
""" % ('\n'.join('    ' + line for line in check.splitlines()), str(result), str(result))
            completed = subprocess.run([str(app), *arguments, '--execute', code], env=environment,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
            assert completed.returncode == 0, completed.stdout.decode(errors='replace')
            assert result.exists() and result.read_text() == 'passed', (result.read_text() if result.exists() else completed.stdout)
            return completed.stdout.decode(errors='replace')

        launch("import builtins\nassert builtins._startup_order == ['app', 'user']\nassert stageviz.session().stage().GetPrimAtPath('/StartupSphere')",
               ['--open', str(source)])
        console_log = root / 'startup-console.log'
        app_script.write_text("import stageviz\nstageviz.setConsoleOutputFile(%r, append=False)\n"
                              "raise SystemExit('expected startup failure')\n" % str(console_log))
        user_script.write_text("import builtins\nbuiltins._startup_recovered = True\n")
        # Release captures console output internally, so process stdout is not
        # the error oracle. Stop logging to drain captured output before reading.
        launch('import builtins\nassert builtins._startup_recovered\n'
               'stageviz.setConsoleOutputFile(None)\n'
               'assert "expected startup failure" in open(%r).read()' % str(console_log))
        launch("import builtins\nassert not hasattr(builtins, '_startup_recovered')\nassert stageviz.plugins.fileFormats() == []", ['--no-startup'])
    print('Startup order, initial external open, failure recovery and --no-startup passed')


if __name__ == '__main__':
    main()
