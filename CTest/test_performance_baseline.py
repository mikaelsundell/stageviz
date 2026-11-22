# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the embedded suite's real baseline helpers without loading Stageviz."""

import ast
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


def performance_namespace():
    # testapi remains self-contained for the embedded editor. Compile only its
    # stdlib imports, performance state and helpers; never execute its app setup.
    source = Path(__file__).with_name('python') / 'testapi.py'
    tree = ast.parse(source.read_text(encoding='utf-8'), filename=str(source))
    imports = {'json', 'os', 'platform', 'statistics', 'subprocess', 'sys', 'time'}
    functions = {'_mac_hardware_profile', '_performance_environment', '_performance_hardware_error',
                 '_load_performance_baseline', '_save_performance_baseline', '_performance_level',
                 '_format_performance_delta', '_compare_performance_metric', '_compare_performance',
                 '_print_performance_summary'}
    nodes = []
    for node in tree.body:
        if isinstance(node, ast.Import) and all(alias.name in imports for alias in node.names):
            nodes.append(node)
        elif isinstance(node, ast.Assign) and all(isinstance(target, ast.Name)
                                                 and target.id.startswith('_PERF_') for target in node.targets):
            nodes.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in functions:
            nodes.append(node)
    namespace = {}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), 'exec'), namespace)
    assert functions.issubset(namespace)
    return namespace


class PerformanceBaselineTest(unittest.TestCase):
    def setUp(self):
        self.ns = performance_namespace()
        temporary = tempfile.TemporaryDirectory(prefix='stageviz-baseline-')
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / 'baseline.json'
        self.ns['_PERF_BASELINE_PATH'] = str(self.path)
        self.ns['_PERF_RESULTS'] = {'sample': 1.0}
        self.profile = {
            'version': 1, 'model': 'MacBookPro18,1', 'cpu': 'Apple M1 Pro',
            'physical_cores': 10, 'logical_cores': 10, 'memory_bytes': 32 * 1024 ** 3,
            'architecture': 'arm64', 'gpus': [{'model': 'Apple M1 Pro', 'cores': 16}],
        }
        self.environment = {'platform': 'macOS-test-arm64', 'machine': 'arm64', 'python': '3.test',
                            'mac_hardware': self.profile}

    def probe(self, devices=None, values=None):
        values = values or {'hw.model': 'MacBookPro18,1', 'machdep.cpu.brand_string': 'Apple M1 Pro',
                            'hw.physicalcpu': '10', 'hw.logicalcpu': '10', 'hw.memsize': str(32 * 1024 ** 3)}
        if devices is None:
            devices = [{'sppci_model': 'Apple M1 Pro', 'sppci_cores': '16',
                        'serial_number': 'synthetic-serial', 'spdisplays_ndrvs': [{'serial': 'synthetic-display'}]}]
        def output(command, **kwargs):
            self.assertEqual(kwargs['timeout'], 15)
            if command[0] == '/usr/sbin/sysctl':
                return values[command[-1]] + '\n'
            self.assertEqual(command, ['/usr/sbin/system_profiler', '-json', 'SPDisplaysDataType'])
            return json.dumps({'SPDisplaysDataType': devices})
        with mock.patch.object(self.ns['subprocess'], 'check_output', side_effect=output), \
                mock.patch.object(self.ns['platform'], 'machine', return_value='arm64'):
            return self.ns['_mac_hardware_profile']()

    def baseline(self, environment=None):
        return {'version': self.ns['_PERF_BASELINE_VERSION'],
                'environment': copy.deepcopy(self.environment if environment is None else environment),
                'tests': {'sample': 1.0}, 'fixture': {'create': .1, 'reload_median': .2}, 'suite': {'total': 2.0}}

    def summary(self, baseline, environment=None, functional_passed=True):
        with mock.patch.dict(self.ns, {'_performance_environment': mock.Mock(
                return_value=self.environment if environment is None else environment)}), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            passed = self.ns['_print_performance_summary'](baseline, .1, .2, 2.0, functional_passed)
        return passed, output.getvalue()

    def test_profile_captures_configuration_without_serials_or_displays(self):
        self.assertEqual(self.probe(), self.profile)
        self.assertNotIn('serial', json.dumps(self.probe()))

    def test_gpu_order_does_not_change_profile(self):
        devices = [{'sppci_model': 'GPU B', 'spdisplays_vram': '8 GB'},
                   {'sppci_model': 'GPU A', 'spdisplays_vram': '4 GB'}]
        self.assertEqual(self.probe(devices), self.probe(list(reversed(devices))))

    def test_unknown_or_incomplete_gpu_identity_is_rejected(self):
        for devices in ([], [{}], [{'sppci_model': 'Apple M1 Pro'}],
                        [{'sppci_model': 'Apple M1 Pro', 'sppci_cores': '0'}]):
            with self.subTest(devices=devices), self.assertRaises(ValueError):
                self.probe(devices)

    def test_probe_timeout_is_not_an_unknown_matching_profile(self):
        with mock.patch.object(self.ns['subprocess'], 'check_output', side_effect=self.ns['subprocess'].TimeoutExpired(
                'system_profiler', 15)), self.assertRaisesRegex(RuntimeError, 'could not read Mac hardware'):
            self.ns['_mac_hardware_profile']()

    def test_only_macos_collects_mac_hardware(self):
        collector = mock.Mock(return_value=self.profile)
        with mock.patch.dict(self.ns, {'_mac_hardware_profile': collector}), \
                mock.patch.object(self.ns['platform'], 'system', return_value='Darwin'):
            self.assertEqual(self.ns['_performance_environment']()['mac_hardware'], self.profile)
        collector.reset_mock()
        with mock.patch.dict(self.ns, {'_mac_hardware_profile': collector}), \
                mock.patch.object(self.ns['platform'], 'system', return_value='Windows'):
            self.assertNotIn('mac_hardware', self.ns['_performance_environment']())
        collector.assert_not_called()

    def test_matching_hardware_retains_performance_comparison(self):
        passed, message = self.summary(self.baseline())
        self.assertTrue(passed)
        self.assertIn('Mac hardware profile matches baseline.', message)
        self.ns['_PERF_RESULTS']['sample'] = 10.0
        self.assertFalse(self.summary(self.baseline())[0], 'hardware guard must not mask real timing failures')

    def test_different_hardware_is_not_compared_or_overwritten(self):
        changes = {'cpu': 'Apple M2 Pro', 'model': 'MacBookPro-other', 'physical_cores': 8,
                   'logical_cores': 8, 'memory_bytes': 16 * 1024 ** 3, 'architecture': 'x86_64',
                   'gpus': [{'model': 'Apple M1 Pro', 'cores': 14}]}
        for key, value in changes.items():
            with self.subTest(field=key):
                baseline = self.baseline()
                baseline['environment']['mac_hardware'][key] = value
                original = json.dumps(baseline)
                self.path.write_text(original)
                compare, save = mock.Mock(), mock.Mock()
                with mock.patch.dict(self.ns, {'_compare_performance': compare, '_save_performance_baseline': save}):
                    passed, message = self.summary(baseline)
                self.assertFalse(passed)
                self.assertIn('hardware profile mismatch', message)
                compare.assert_not_called()
                save.assert_not_called()
                self.assertEqual(self.path.read_text(), original)

    def test_legacy_mac_baseline_requires_explicit_adoption(self):
        environment = {key: value for key, value in self.environment.items() if key != 'mac_hardware'}
        baseline = self.baseline(environment)
        self.path.write_text(json.dumps(baseline))
        passed, message = self.summary(baseline)
        self.assertFalse(passed)
        self.assertIn('no Mac hardware profile', message)
        self.assertEqual(json.loads(self.path.read_text()), baseline)

    def test_new_baseline_stores_hardware_and_existing_timing_format(self):
        self.assertTrue(self.summary(None)[0])
        saved = self.ns['_load_performance_baseline']()
        self.assertEqual(saved['environment']['mac_hardware'], self.profile)
        self.assertEqual(saved['tests'], {'sample': 1.0})
        self.assertEqual(saved['fixture'], {'create': .1, 'reload_median': .2})
        self.assertEqual(saved['suite'], {'total': 2.0})
        self.assertTrue(self.summary(saved)[0])

    def test_probe_failure_never_creates_or_overwrites_baseline(self):
        for baseline in (None, self.baseline()):
            with self.subTest(existing=baseline is not None):
                self.path.write_text('original')
                with mock.patch.dict(self.ns, {'_performance_environment': mock.Mock(side_effect=RuntimeError('probe failed'))}), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertFalse(self.ns['_print_performance_summary'](baseline, .1, .2, 2.0, True))
                self.assertEqual(self.path.read_text(), 'original')

    def test_os_or_python_warning_does_not_bypass_matching_hardware(self):
        baseline = self.baseline()
        baseline['environment']['platform'] = 'older macOS'
        baseline['environment']['python'] = 'older Python'
        passed, message = self.summary(baseline)
        self.assertTrue(passed)
        self.assertIn('baseline environment differs', message)

    def test_windows_baselines_keep_existing_behavior(self):
        environment = {'platform': 'Windows-test', 'machine': 'AMD64', 'python': '3.test'}
        self.assertTrue(self.summary(self.baseline(environment), environment)[0])

    def test_mac_baseline_cannot_be_compared_on_windows(self):
        environment = {'platform': 'Windows-test', 'machine': 'AMD64', 'python': '3.test'}
        self.assertFalse(self.summary(self.baseline(), environment)[0])

    def test_explicit_reset_still_requires_functional_success(self):
        self.ns['_PERF_BASELINE_RESET'] = True
        self.assertTrue(self.summary(self.baseline())[0])
        self.assertEqual(json.loads(self.path.read_text())['environment']['mac_hardware'], self.profile)
        self.path.write_text('original')
        self.assertTrue(self.summary(self.baseline(), functional_passed=False)[0])
        self.assertEqual(self.path.read_text(), 'original')

    def test_disabled_performance_does_not_probe_hardware(self):
        self.ns['_PERF_ENABLED'] = False
        collector = mock.Mock(side_effect=AssertionError('must not probe'))
        with mock.patch.dict(self.ns, {'_performance_environment': collector}), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(self.ns['_print_performance_summary'](self.baseline(), .1, .2, 2.0, True))
        collector.assert_not_called()


if __name__ == '__main__':
    unittest.main()
