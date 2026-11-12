# SPDX-License-Identifier: BSD-3-Clause
"""Test complete-suite discovery, command planning and failure propagation."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import run_all
import run_regressions


class TestRunner(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='stageviz runner ')
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.build = self.directory / 'build with spaces'
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').write_text('CMAKE_COMMAND:INTERNAL=/unused/cmake\n')
        self.app = self.directory / 'app with spaces'
        self.app.touch()
        self.args = argparse.Namespace(build_dir=str(self.build), app=str(self.app), cmake=None,
                                       no_build=False, jobs=2, port=0)

    def plan(self, statuses):
        calls = []
        values = iter(statuses)
        def execute(command, log, environment):
            calls.append(([str(arg) for arg in command], environment))
            return {'returncode': next(values), 'log': str(log)}
        with mock.patch.object(run_all, 'tools', return_value=('cmake', 'ctest')), \
                mock.patch.object(run_all, 'run_logged', side_effect=execute), \
                contextlib.redirect_stdout(io.StringIO()):
            row = run_all.run_configuration(self.args, 'Debug', self.directory / 'results')
        return row, calls

    def test_all_configurations(self):
        self.assertEqual(run_all.configurations('all'), ('Debug', 'Release'))
        self.assertEqual(run_all.configurations('Release'), ('Release',))

    def test_discovery_includes_new_suites(self):
        scripts = self.directory / 'CTest/python'
        scripts.mkdir(parents=True)
        for name in ('test.py', 'testbench.py', 'testconsole.py', 'testnewfeature.py'):
            (scripts / name).touch()
        self.assertEqual(run_regressions.discover_suites(self.directory), ['testconsole', 'testnewfeature'])

    def test_platform_executable_paths(self):
        mac = self.build / 'Debug/Stageviz.app/Contents/MacOS/stageviz'
        mac.parent.mkdir(parents=True)
        mac.touch()
        self.assertEqual(run_all.executable(self.build, 'Debug', 'darwin'), mac.resolve())
        windows = self.build / 'Debug/Stageviz.exe'
        windows.touch()
        self.assertEqual(run_all.executable(self.build, 'Debug', 'win32'), windows.resolve())

    def test_complete_plan_includes_performance_and_serial_ctest(self):
        row, calls = self.plan([0, 0, 0, 0])
        self.assertTrue(row['passed'])
        self.assertEqual(len(calls), 4)
        self.assertEqual([step['name'] for step in row['steps']], ['configure', 'build', 'ctest', 'python'])
        self.assertIn('-S', calls[0][0])
        self.assertIn(str(run_all.ROOT), calls[0][0])
        self.assertIn('-B', calls[0][0])
        self.assertIn(str(self.build.resolve()), calls[0][0])
        self.assertIn('-DBUILD_TESTING=ON', calls[0][0])
        self.assertIn('--no-tests=error', calls[2][0])
        self.assertEqual(calls[2][0][calls[2][0].index('--parallel') + 1], '1')
        self.assertIn('--performance', calls[3][0])
        self.assertNotIn('--suites', calls[3][0])
        self.assertIn('STAGEVIZ_TEST_SETTINGS_DIR', calls[2][1])
        self.assertIn('--port', calls[3][0])

    def test_ctest_failure_does_not_skip_python_or_pass(self):
        row, calls = self.plan([0, 0, 7, 0])
        self.assertFalse(row['passed'])
        self.assertEqual(len(calls), 4)
        self.assertEqual(row['steps'][2]['returncode'], 7)

    def test_python_failure_is_reported(self):
        row, _ = self.plan([0, 0, 0, 1])
        self.assertFalse(row['passed'])

    def test_build_failure_does_not_test_stale_binaries(self):
        row, calls = self.plan([0, 1])
        self.assertFalse(row['passed'])
        self.assertEqual(len(calls), 2)
        self.assertIn('stale', row['error'])

    def test_configure_failure_does_not_build_or_test(self):
        row, calls = self.plan([1])
        self.assertFalse(row['passed'])
        self.assertEqual(len(calls), 1)
        self.assertIn('Configure failed', row['error'])

    def test_no_build_still_runs_both_test_families(self):
        self.args.no_build = True
        row, calls = self.plan([0, 0])
        self.assertTrue(row['passed'])
        self.assertEqual([step['name'] for step in row['steps']], ['ctest', 'python'])

    def test_main_returns_failure_and_retains_summary(self):
        output = self.directory / 'outer'
        with mock.patch.object(run_all, 'run_configuration', return_value={'passed': False}), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_all.main(['--output', str(output)]), 1)
        self.assertFalse(json.loads((output / 'summary.json').read_text())['passed'])

    def test_cleanup_failure_cannot_be_reported_as_pass(self):
        class FailedCleanup:
            def __init__(self, app, output, **kwargs):
                Path(output).mkdir()
            def __enter__(self):
                return self
            def __exit__(self, *args):
                raise RuntimeError('App crashed before cleanup')
            def python(self, *args, **kwargs):
                return {'result': {'content': []}}
            def oracle(self):
                return {}
        output = self.directory / 'pipeline'
        args = argparse.Namespace(output=str(output), suites=['testconsole'], app='unused', port=0)
        with mock.patch.object(run_regressions, 'DisposableStageviz', FailedCleanup), \
                contextlib.redirect_stdout(io.StringIO()), self.assertRaises(AssertionError):
            run_regressions.run(args)
        self.assertFalse(json.loads((output / 'summary.json').read_text())[0]['passed'])

    def test_interruption_is_not_a_passing_run(self):
        output = self.directory / 'interrupted'
        with mock.patch.object(run_all, 'run_configuration', side_effect=KeyboardInterrupt), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_all.main(['--output', str(output)]), 130)
        summary = json.loads((output / 'summary.json').read_text())
        self.assertFalse(summary['passed'])
        self.assertEqual(summary['status'], 'interrupted')


if __name__ == '__main__':
    unittest.main()
