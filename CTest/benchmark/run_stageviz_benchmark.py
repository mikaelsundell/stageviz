# SPDX-License-Identifier: BSD-3-Clause
"""Measure Stageviz viewport swaps against a hardware-matched repository baseline."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile

from benchmark_gizmo_rendering import write_fixture
from compare_viewer_swaps import read_json, summarize, write_json
from mcp_runtime import DisposableStageviz, test_desktop

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / 'CTest'
BASELINES = ROOT / 'docs' / 'performance' / 'stageviz-viewport'
WORKER = Path(__file__).with_name('compare_viewer_swaps.py')
WORKLOADS = {'orbit': 'Orbit', 'orbit_selection': 'Orbit with selection'}
CONFIGURATIONS = {name.lower(): name for name in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')}
# Increment when the timing boundaries or viewport setup change.
PROTOCOL = 1


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def thirdparty_prefix():
    value = os.environ.get('THIRDPARTY_DIR', '')
    if value and Path(value).expanduser().is_dir():
        return Path(value).expanduser().resolve()
    if not sys.stdin.isatty():
        raise ValueError('Set THIRDPARTY_DIR to the 3rdparty prefix used to build Stageviz.')
    if value:
        print('Warning: THIRDPARTY_DIR does not point to a directory.')
    try:
        value = input('3rdparty build prefix (THIRDPARTY_DIR): ').strip().strip('"')
    except EOFError:
        value = ''
    prefix = Path(value).expanduser()
    if not value or not prefix.is_dir():
        raise ValueError('A valid 3rdparty build prefix is required.')
    return prefix.resolve()


def measure_stageviz(args, directory, binary, output, prefix, python_runtime, deploy_build):
    scene = output / 'fixture.usda'
    write_fixture(scene, args.cubes)
    fixture = scene.read_bytes().replace(b'\r\n', b'\n')
    scene.write_bytes(fixture)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    commit = git('rev-parse', 'HEAD')
    dirty = bool(git('status', '--porcelain'))
    config = dict(scene=str(scene), frames=args.frames, size=args.size, selection=[])
    runtime = directory / f'test-runtime-{args.configuration}.txt'
    paths = [prefix / 'bin', prefix / 'lib']
    if runtime.is_file():
        paths[:0] = [Path(line) for line in runtime.read_text(encoding='utf-8').splitlines() if line]
    if python_runtime:
        paths.insert(0, Path(python_runtime).parent)
    environment = {'THIRDPARTY_DIR': str(prefix),
                   'PATH': os.pathsep.join([str(path) for path in paths] + [os.environ.get('PATH', '')]),
                   'STAGEVIZ_PERF_LOG': ''}
    python_dll_paths = ()
    if sys.platform == 'win32' and deploy_build:
        # The executable in the build tree does not have the package layout.
        # Make Qt and CPython find the matching development dependencies there.
        environment['QT_PLUGIN_PATH'] = str(prefix / 'plugins')
        module_paths = (prefix / 'lib' / 'python', prefix / 'site-packages', prefix / 'python')
        environment['PYTHONPATH'] = os.pathsep.join(str(path) for path in module_paths if path.is_dir())
        python_dll_paths = tuple(path for path in (prefix / 'bin', prefix / 'lib') if path.is_dir())
    runs = []
    with test_desktop():
        for number in range(1, args.runs + 1):
            run_dir = output / f'run-{number}'
            print(f'Measuring Stageviz [{number}/{args.runs}]', flush=True)
            try:
                with DisposableStageviz(binary, run_dir, port=0, configuration=args.configuration,
                                        environment=environment, python_dll_paths=python_dll_paths) as app:
                    before = app.oracle()
                    code = (
                        'import runpy; from pathlib import Path\n'
                        f'runpy.run_path({str(WORKER)!r})["stageviz_worker"]({config!r}, Path({str(run_dir)!r}))'
                    )
                    if number == 1:
                        code += (
                            '\nimport json\nfrom testapi import _performance_environment\n'
                            f'Path({str(output / "host.json")!r}).write_text('
                            'json.dumps(_performance_environment()), encoding="utf-8")'
                        )
                    app.python(code, timeout=300)
                    write_json(run_dir / 'oracles.json', {'before': before, 'after': app.oracle()})
            except AssertionError as error:
                raise RuntimeError(f'Stageviz run {number} failed. See {run_dir / "app.log"}.') from error
            runs.append(read_json(run_dir / 'samples.json'))

    if scene.read_bytes() != fixture or hashlib.sha256(binary.read_bytes()).hexdigest() != binary_hash:
        raise RuntimeError('The test fixture or Stageviz binary changed during the benchmark.')
    first = runs[0]
    if any(run[key] != first[key] for run in runs[1:] for key in ('size', 'graphics', 'usd_version')):
        raise RuntimeError('Viewport size, GPU profile, or OpenUSD version changed between runs.')
    if not first['graphics'].get('opengl_renderer') or not first['graphics'].get('opengl_version'):
        raise RuntimeError('Could not identify the active OpenGL renderer and driver.')

    host = read_json(output / 'host.json')
    hardware_key = 'windows_hardware' if sys.platform == 'win32' else 'mac_hardware'
    # The profile and its hash are published. Use only the hardware fields
    # that describe the configuration, never the Windows installation hash.
    hardware_fields = ('version', 'model', 'cpu', 'cpus', 'physical_cores', 'logical_cores',
                       'memory_bytes', 'architecture', 'gpus')
    hardware = {key: host[hardware_key][key] for key in hardware_fields if key in host[hardware_key]}
    identity = dict(protocol=PROTOCOL, hardware=hardware, os=host['platform'], graphics=first['graphics'],
                    openusd=first['usd_version'], configuration=args.configuration, viewport=first['size'],
                    fixture_sha256=hashlib.sha256(fixture).hexdigest(),
                    cubes=args.cubes, frames=args.frames, runs=args.runs)
    measurements = {}
    for workload in WORKLOADS:
        summaries = [summarize(run[workload]) for run in runs]
        measurements[workload] = {
            metric: {
                'median': statistics.median(run[metric]['median'] for run in summaries),
                'p95': statistics.median(run[metric]['p95'] for run in summaries),
                'maximum': max(run[metric]['maximum'] for run in summaries),
            }
            for metric in summaries[0]
        }
    return dict(recorded_on=datetime.now(timezone.utc).isoformat(), profile=identity,
                source={'head': commit, 'working_tree_dirty': dirty, 'binary_sha256': binary_hash},
                measurements=measurements)


def comparison_key(profile):
    return hashlib.sha256(json.dumps(profile, sort_keys=True).encode('utf-8')).hexdigest()


def comparison_rows(report, previous):
    rows = []
    for name, title in WORKLOADS.items():
        current = report['measurements'][name]['input_to_swap_ms']
        baseline = previous['measurements'][name]['input_to_swap_ms'] if previous else None
        cells = []
        for stat in ('median', 'p95'):
            value = current[stat]
            cell = f'{value:.2f} ms'
            if baseline is not None:
                old = baseline[stat]
                cell = f'{old:.2f} ms -> {cell}'
                if old > 0:
                    cell += f' ({100 * (value - old) / old:+.1f}%)'
            cells.append(cell)
        rows.append((title, *cells))
    return rows


def save_baseline(path, report, original):
    path.parent.mkdir(parents=True, exist_ok=True)
    lock = path.with_suffix('.lock')
    lock.mkdir()
    try:
        current = path.read_bytes() if path.exists() else None
        if current != original:
            raise RuntimeError('The baseline changed during review. Re-run the benchmark before replacing it.')
        with tempfile.TemporaryDirectory(dir=path.parent) as temporary:
            candidate = Path(temporary) / path.name
            write_json(candidate, report)
            candidate.replace(path)
    finally:
        lock.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', nargs='?', type=str.lower, choices=CONFIGURATIONS,
                        default='debug' if sys.platform == 'darwin' else 'release')
    parser.add_argument('--build-dir', type=Path, help='Configured build directory')
    parser.add_argument('--output', type=Path, help='New directory for local measurements and logs')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--frames', type=int, default=90)
    parser.add_argument('--size', type=int, nargs=2, default=[890, 750], metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--cubes', type=int, default=1024)
    parser.add_argument('--no-prompt', action='store_true', help='Compare without offering to update the baseline')
    args = parser.parse_args()
    if args.runs < 1 or args.frames < 2 or args.cubes < 1 or min(args.size) < 1:
        parser.error('Use at least one run, two frames, one cube and positive viewport dimensions.')
    if sys.platform not in ('darwin', 'win32'):
        parser.error('Machine-matched benchmark profiles are supported on macOS and Windows.')
    args.configuration = CONFIGURATIONS[args.configuration]

    sys.path.insert(0, str(TESTS))
    from run_all import cache_value, executable

    directory = (args.build_dir or ROOT / ('build.' + args.configuration.lower())).resolve()
    output = (args.output or ROOT / 'build.test-results' / datetime.now().strftime('benchmark-%Y%m%d-%H%M%S-%f')).resolve()
    prefix = thirdparty_prefix()
    configured_prefix = cache_value(directory, 'CMAKE_PREFIX_PATH')
    if configured_prefix and prefix not in [Path(p).resolve() for p in configured_prefix.split(';') if p]:
        raise ValueError('THIRDPARTY_DIR must match the dependency prefix in the Stageviz CMake cache.')
    deploy_build = cache_value(directory, 'DEPLOY_BUILD') == 'ON'
    runtime_configuration = 'DEBUG' if args.configuration == 'Debug' else 'RELEASE'
    python_runtime = cache_value(directory, f'_Python3_RUNTIME_LIBRARY_{runtime_configuration}')
    binary = executable(directory, args.configuration)
    output.mkdir(parents=True, exist_ok=False)
    report = measure_stageviz(args, directory, binary, output, prefix, python_runtime, deploy_build)
    key = comparison_key(report['profile'])
    baseline = BASELINES / (key + '.json')
    original = baseline.read_bytes() if baseline.exists() else None
    previous = json.loads(original) if original is not None else None
    if previous and previous['profile'] != report['profile']:
        raise RuntimeError('The stored baseline does not match its hardware and workload key.')
    rows = comparison_rows(report, previous)
    comparison = dict(key=key, baseline=previous['source'] if previous else None,
                      input_to_swap=[dict(workload=title, median=median, p95=p95) for title, median, p95 in rows])
    write_json(output / 'summary.json', dict(report, comparison=comparison))
    print(f'Active OpenGL renderer: {report["profile"]["graphics"]["opengl_renderer"]}')
    print(f'Baseline: {baseline.relative_to(ROOT)}')
    if previous:
        print('Input-to-swap change (baseline -> current):')
    else:
        print('Warning: no baseline matches this hardware, active GPU, driver and workload.')
    for title, median, p95 in rows:
        print(f'{title}: median {median}, p95 {p95}')
    print(f'Run results: {output / "summary.json"}')

    if not args.no_prompt and sys.stdin.isatty():
        action = 'Replace the benchmark baseline in the repository' if previous else 'Save this benchmark baseline to the repository'
        try:
            answer = input(f'{action}? [y/N] ').strip().lower()
        except EOFError:
            answer = ''
        if answer in ('y', 'yes'):
            save_baseline(baseline, report, original)
            print(f'Baseline saved: {baseline}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
        sys.exit(f'Benchmark failed: {error}')
