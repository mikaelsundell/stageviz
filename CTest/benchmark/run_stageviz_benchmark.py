# SPDX-License-Identifier: BSD-3-Clause
"""Measure Stageviz viewport swaps against a hardware-matched repository baseline."""

import argparse
from contextlib import ExitStack
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
from hierarchy_selection_fixture import SELECTION_BATCHES, selection_batch, write_hierarchy_fixture
from mcp_runtime import DisposableStageviz, test_desktop

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / 'CTest'
BASELINES = ROOT / 'docs' / 'performance' / 'stageviz-viewport'
WORKER = Path(__file__).with_name('compare_viewer_swaps.py')
WORKLOADS = {
    'orbit': 'Orbit',
    'orbit_selection': 'Orbit with selection',
    'hierarchy_selection': 'Selection across hierarchy',
}
HIERARCHY_PARTS = 40_000
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
    config = dict(scene=str(scene), frames=args.frames, size=args.size, selection=[])
    if args.hierarchy is None:
        write_fixture(scene, args.cubes)
        fixture = scene.read_bytes().replace(b'\r\n', b'\n')
        scene.write_bytes(fixture)
        workloads = ('orbit', 'orbit_selection')
    else:
        prims = write_hierarchy_fixture(scene, args.hierarchy)
        batches = [selection_batch(args.hierarchy, batch) for batch in range(SELECTION_BATCHES)]
        config['hierarchy'] = dict(prims=prims, batches=batches)
        print(f'Hierarchical fixture: {prims:,} prims, {args.hierarchy:,} polygon meshes', flush=True)
        fixture = scene.read_bytes()
        workloads = ('hierarchy_selection',)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    commit = git('rev-parse', 'HEAD')
    dirty = bool(git('status', '--porcelain'))
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
            print(f'Measuring Stageviz [{args.pass_offset + number}/{args.total_passes}]', flush=True)
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
                    app.python(code, timeout=900 if args.hierarchy is not None else 300)
                    after = app.oracle()
                    write_json(run_dir / 'oracles.json', {'before': before, 'after': after})
                    if args.hierarchy is not None:
                        selection = after['stageviz_get_selection']['result']['content'][0]['text']
                        expected = batches[(args.frames - 1) % len(batches)]
                        if json.loads(selection) != expected:
                            raise RuntimeError(f'Stageviz run {number} returned a different final selection over MCP.')
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
                    fixture_sha256=hashlib.sha256(fixture).hexdigest(), frames=args.frames, runs=args.runs)
    if args.hierarchy is None:
        identity['cubes'] = args.cubes
    else:
        identity['hierarchy'] = dict(
            parts=args.hierarchy,
            prims=prims,
            selection_sizes=[len(batch) for batch in batches],
            selection_sha256=hashlib.sha256(json.dumps(batches).encode('utf-8')).hexdigest(),
        )
    measurements = {}
    for workload in workloads:
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


def baseline_profile(profile):
    return {key: value for key, value in profile.items() if key != 'runs'}


def comparison_key(profile):
    return hashlib.sha256(json.dumps(baseline_profile(profile), sort_keys=True).encode('utf-8')).hexdigest()


def find_baseline(profile, key):
    path = BASELINES / (key + '.json')
    match = baseline_profile(profile)
    if path.is_file():
        original = path.read_bytes()
        previous = json.loads(original)
        if baseline_profile(previous['profile']) != match:
            raise RuntimeError('The stored baseline does not match its hardware and workload key.')
        return path, original, previous

    # Older files had the pass count in the filename hash. Keep using the
    # newest matching file in place so approval updates the same baseline.
    latest = None
    for candidate in sorted(BASELINES.glob('*.json')):
        try:
            original = candidate.read_bytes()
            previous = json.loads(original)
        except (OSError, ValueError):
            continue
        if isinstance(previous, dict) and isinstance(previous.get('profile'), dict):
            if baseline_profile(previous['profile']) == match:
                recorded = previous.get('recorded_on', '')
                if latest is None or recorded > latest[0]:
                    latest = (recorded, candidate, original, previous)
    return (path, None, None) if latest is None else latest[1:]


def comparison_rows(report, previous):
    rows = []
    for name, measurements in report['measurements'].items():
        current = measurements['input_to_swap_ms']
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
        rows.append((WORKLOADS[name], *cells))
    return rows


def save_baselines(updates):
    BASELINES.mkdir(parents=True, exist_ok=True)
    with ExitStack() as resources:
        for path, _, _ in sorted(updates):
            lock = path.with_suffix('.lock')
            lock.mkdir()
            resources.callback(lock.rmdir)

        staging = Path(resources.enter_context(tempfile.TemporaryDirectory(dir=BASELINES)))
        prepared = []
        for path, original, report in updates:
            current = path.read_bytes() if path.exists() else None
            if current != original:
                raise RuntimeError('A baseline changed during review. Re-run the benchmark before saving.')
            candidate = staging / path.name
            write_json(candidate, report)
            backup = staging / (path.stem + '.bak') if original is not None else None
            if backup is not None:
                backup.write_bytes(original)
            prepared.append((path, candidate, backup))

        replaced = []
        try:
            for path, candidate, backup in prepared:
                candidate.replace(path)
                replaced.append((path, backup))
        except OSError:
            for path, backup in reversed(replaced):
                if backup is None:
                    path.unlink()
                else:
                    backup.replace(path)
            raise


def report_comparison(report, output):
    key = comparison_key(report['profile'])
    baseline, original, previous = find_baseline(report['profile'], key)
    rows = comparison_rows(report, previous)
    comparison = dict(key=baseline.stem, baseline=previous['source'] if previous else None,
                      baseline_passes=previous['profile']['runs'] if previous else None,
                      input_to_swap=[dict(workload=title, median=median, p95=p95) for title, median, p95 in rows])
    write_json(output / 'summary.json', dict(report, comparison=comparison))
    print(f'Active OpenGL renderer: {report["profile"]["graphics"]["opengl_renderer"]}')
    print(f'Baseline: {baseline.relative_to(ROOT)}')
    if previous:
        print(f'Passes: {previous["profile"]["runs"]} baseline, {report["profile"]["runs"]} current')
        print('Input-to-swap change (baseline -> current):')
    else:
        print('Warning: no baseline matches this hardware, active GPU, driver and workload.')
    for title, median, p95 in rows:
        print(f'{title}: median {median}, p95 {p95}')
    print(f'Run results: {output / "summary.json"}')
    return baseline, original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', nargs='?', type=str.lower, choices=CONFIGURATIONS,
                        default='debug' if sys.platform == 'darwin' else 'release')
    parser.add_argument('--build-dir', type=Path, help='Configured build directory')
    parser.add_argument('--output', type=Path, help='New directory for local measurements and logs')
    parser.add_argument('--runs', type=int, default=4, help='Passes per benchmark (default: 4)')
    parser.add_argument('--frames', type=int, help='Override sample count (normally 90 for viewport, 24 for hierarchy)')
    parser.add_argument('--size', type=int, nargs=2, default=[890, 750], metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--cubes', type=int, help='Cube count for the viewport workload (default: 1024)')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--quick', action='store_true', help='Run only the viewport workloads')
    mode.add_argument('--hierarchy', nargs='?', type=int, const=HIERARCHY_PARTS, metavar='PARTS',
                      help='Run only hierarchy selection, with 40,000 components by default')
    parser.add_argument('--no-prompt', action='store_true', help='Compare without offering to update the baseline')
    args = parser.parse_args()
    if args.cubes is not None and args.hierarchy is not None:
        parser.error('--cubes is only used by the viewport workloads.')
    if (args.runs < 1 or (args.frames is not None and args.frames < 2)
            or (args.cubes is not None and args.cubes < 1) or min(args.size) < 1):
        parser.error('Use at least one run, two frames, one cube and positive viewport dimensions.')
    if args.hierarchy is not None and args.hierarchy < SELECTION_BATCHES:
        parser.error(f'Use at least {SELECTION_BATCHES} hierarchy components.')
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
    jobs = []
    if args.hierarchy is None:
        jobs.append(('viewport', None, 90, output))
    if not args.quick:
        jobs.append(('hierarchy', args.hierarchy or HIERARCHY_PARTS, 24, output / 'hierarchy' if jobs else output))
    updates = []
    for job, (label, parts, default_frames, job_output) in enumerate(jobs):
        if job_output != output:
            job_output.mkdir()
        job_args = argparse.Namespace(**(vars(args) | {
            'cubes': args.cubes or 1024, 'hierarchy': parts, 'frames': args.frames or default_frames,
            'pass_offset': job * args.runs, 'total_passes': len(jobs) * args.runs,
        }))
        print(f'Running {label} benchmark', flush=True)
        report = measure_stageviz(job_args, directory, binary, job_output, prefix, python_runtime, deploy_build)
        baseline, original = report_comparison(report, job_output)
        updates.append((baseline, original, report))

    if not args.no_prompt and sys.stdin.isatty():
        target = 'both benchmark baselines' if len(updates) > 1 else 'the benchmark baseline'
        verb = 'Replace' if all(original is not None for _, original, _ in updates) else 'Save'
        try:
            answer = input(f'{verb} {target} in the repository? [y/N] ').strip().lower()
        except EOFError:
            answer = ''
        if answer in ('y', 'yes'):
            save_baselines(updates)
            for baseline, _, _ in updates:
                print(f'Baseline saved: {baseline}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
        sys.exit(f'Benchmark failed: {error}')
