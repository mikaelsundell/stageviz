# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Build and run every registered CTest and embedded Python suite."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'CTest/benchmark/pipeline'))
from run_regressions import discover_suites


def configurations(value):
    return ('Debug', 'Release') if value.lower() == 'all' else (value.capitalize(),)


def cache_value(directory, key):
    cache = directory / 'CMakeCache.txt'
    if cache.is_file():
        for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
            if line.startswith(key + ':') and '=' in line:
                return line.split('=', 1)[1]
    return None


def executable(directory, configuration, platform=None):
    platform = platform or sys.platform
    names = ('Stageviz.exe', 'stageviz.exe') if platform == 'win32' else ('stageviz', 'Stageviz')
    candidates = []
    for base in (directory / configuration, directory):
        if platform == 'darwin':
            candidates.extend(base / bundle / 'Contents/MacOS' / name
                              for bundle in ('Stageviz.app', 'stageviz.app') for name in names)
        candidates.extend(base / name for name in names)
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError('Stageviz executable not found in %s (%s); build the app first.' % (directory, configuration))


def tools(directory, cmake_override=None):
    cmake = cmake_override or shutil.which('cmake') or cache_value(directory, 'CMAKE_COMMAND')
    if not cmake or not Path(cmake).is_file():
        raise RuntimeError('CMake is required. Put it on PATH or pass --cmake PATH.')
    suffix = '.exe' if sys.platform == 'win32' else ''
    sibling = Path(cmake).with_name('ctest' + suffix)
    ctest = str(sibling) if sibling.is_file() else shutil.which('ctest')
    if not ctest:
        raise RuntimeError('CTest is required alongside CMake.')
    return str(Path(cmake).resolve()), str(Path(ctest).resolve())


def run_logged(command, log, environment):
    """Use argument vectors on both platforms and retain combined output."""
    print('\nRUN', subprocess.list2cmdline([str(arg) for arg in command]), flush=True)
    print('LOG', log, flush=True)
    started = time.monotonic()
    with log.open('w', encoding='utf-8') as stream:
        process = subprocess.Popen([str(arg) for arg in command], cwd=ROOT, env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0,
                                   text=True, encoding='utf-8', errors='replace')
        try:
            for line in process.stdout:
                stream.write(line)
                stream.flush()
                print(line, end='', flush=True)
            code = process.wait()
        except BaseException:
            if process.poll() is None:
                process.send_signal(signal.CTRL_BREAK_EVENT if sys.platform == 'win32' else signal.SIGINT)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise
    return {'command': [str(arg) for arg in command], 'returncode': code,
            'seconds': time.monotonic() - started, 'log': str(log)}


def run_configuration(args, configuration, output):
    directory = Path(args.build_dir).resolve() if args.build_dir else ROOT / ('build.' + configuration.lower())
    row = {'configuration': configuration, 'build_dir': str(directory), 'passed': False, 'steps': []}
    output.mkdir()
    environment = dict(os.environ)
    environment['PYTHONUNBUFFERED'] = '1'
    environment['STAGEVIZ_TEST_SETTINGS_DIR'] = str(output / 'settings')
    if sys.platform == 'darwin':
        environment['DYLD_IMAGE_SUFFIX'] = '_debug'
    try:
        if not (directory / 'CMakeCache.txt').is_file():
            raise RuntimeError('No configured build at %s. Run build_app for %s first.' % (directory, configuration))
        cmake, ctest = tools(directory, args.cmake)
        if not args.no_build:
            step = run_logged([cmake, '--build', directory, '--config', configuration,
                               '--target', 'stageviz', 'stageviz_tests', '--parallel', str(args.jobs)],
                              output / 'build.log', environment)
            row['steps'].append(dict(name='build', **step))
            if step['returncode']:
                raise RuntimeError('Build failed; tests were not run against stale executables.')
        app = Path(args.app).resolve() if args.app else executable(directory, configuration)
        if not app.is_file():
            raise FileNotFoundError('Stageviz executable does not exist: %s' % app)
        step = run_logged([ctest, '--test-dir', directory, '-C', configuration, '--output-on-failure',
                           '--no-tests=error', '--parallel', '1', '--output-junit', output / 'ctest.xml'],
                          output / 'ctest.log', environment)
        row['steps'].append(dict(name='ctest', **step))
        # Continue after CTest failures to produce a complete Python report too.
        baseline = directory / 'test-baselines' / ('testapi_%s.json' % configuration.lower())
        step = run_logged([sys.executable, ROOT / 'CTest/benchmark/pipeline/run_regressions.py',
                           '--app', app, '--output', output / 'python', '--port', str(args.port),
                           '--performance', '--performance-baseline', baseline],
                          output / 'python.log', environment)
        row['steps'].append(dict(name='python', **step))
        row['passed'] = all(step['returncode'] == 0 for step in row['steps'])
    except Exception as error:
        row['error'] = str(error)
        print('FAIL', configuration, error, flush=True)
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', nargs='?', default='debug', type=str.lower,
                        choices=('debug', 'release', 'all'))
    parser.add_argument('--no-build', action='store_true', help='Use existing binaries without rebuilding')
    parser.add_argument('--output', help='New directory for logs, JSON, JUnit and screenshots')
    parser.add_argument('--build-dir', help='Override the configured build directory (single configuration only)')
    parser.add_argument('--app', help='Override the app executable (single configuration only)')
    parser.add_argument('--cmake', help='CMake executable when it is not on PATH or in the build cache')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 6), help='Parallel build jobs; tests remain serial')
    parser.add_argument('--port', type=int, default=0, help='Disposable MCP port; 0 chooses a free port for each app')
    parser.add_argument('--list', action='store_true', help='List embedded suites without building or launching apps')
    args = parser.parse_args(argv)
    if args.jobs < 1 or not 0 <= args.port <= 65535:
        parser.error('--jobs must be positive and --port must be in 0..65535')
    if args.configuration == 'all' and (args.build_dir or args.app):
        parser.error('--build-dir and --app require a single configuration')
    suites = discover_suites(ROOT)
    if not suites:
        parser.error('No embedded Python suites found')
    if args.list:
        print('All registered CTests, followed by %d embedded Python suites:' % len(suites))
        print('\n'.join(suites))
        return 0
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
    output = Path(args.output).resolve() if args.output else ROOT / 'build.test-results' / stamp
    if output.exists():
        parser.error('--output must be a new directory: %s' % output)
    output.mkdir(parents=True)
    summary = {'passed': False, 'status': 'running', 'python_suites': suites, 'configurations': []}
    summary_file = output / 'summary.json'
    summary_file.write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print('Full Stageviz tests. Results:', output, flush=True)
    try:
        for configuration in configurations(args.configuration):
            summary['configurations'].append(run_configuration(args, configuration, output / configuration.lower()))
            summary_file.write_text(json.dumps(summary, indent=2), encoding='utf-8')
    except KeyboardInterrupt:
        summary['status'] = 'interrupted'
        summary_file.write_text(json.dumps(summary, indent=2), encoding='utf-8')
        print('\nInterrupted:', summary_file, flush=True)
        return 130
    summary['passed'] = all(row['passed'] for row in summary['configurations'])
    summary['status'] = 'passed' if summary['passed'] else 'failed'
    summary_file.write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print('\n%s: %s' % ('PASS ALL' if summary['passed'] else 'FAIL', output / 'summary.json'), flush=True)
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
