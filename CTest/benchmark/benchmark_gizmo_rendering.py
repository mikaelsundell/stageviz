# SPDX-License-Identifier: BSD-3-Clause
"""Compare viewport submission costs in two disposable Stageviz builds.

No frame readbacks, waits, diagnostic switches or timing logs are used during
measurement. Repaint wall time measures CPU submission and any driver stalls;
it is not a GPU execution timer. Screenshots are taken after measurements.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys

from mcp_runtime import DisposableStageviz


WORKFLOW = r'''
import contextlib, io, json, math, runpy, time
from pathlib import Path
import stageviz
from pxr import UsdGeom
ns = runpy.run_path(str(ROOT / 'CTest/python/testgizmostability.py'),
                    init_globals={'_VIEWPORT_AUTORUN': False})
tb = ns['tb']
test = ns['GizmoStabilityTest'](disposable=True)
session = stageviz.session()
fixture = OUT / ('scene-%d.usda' % COUNT)
assert session.loadFromFile(str(fixture))
assert sum(1 for prim in session.stage().Traverse()) == COUNT + 2
stageviz.command.select_paths(['/World/Sphere'])
assert tb.wait_for_commands()
widget = test.viewport()
# Show a populated Outliner branch, not just a closed root.
_, _, _, widgets = tb.qt()
for tree in tb.all_widgets():
    if isinstance(tree, widgets.QTreeWidget) and tree.objectName() == 'outlinerTree':
        root = tree.topLevelItem(0)
        if root:
            root.setExpanded(True)
            for i in range(root.childCount()):
                if root.child(i).text(0) == 'World':
                    root.child(i).setExpanded(True)
camera = session.viewState().camera()
camera.frame(((-4., -4., -4.), (4., 4., 4.)))
camera.setCameraDistance(8)
session.viewState().setSceneStatsEnabled(False)
session.viewState().setPerformanceStatsEnabled(False)
original = session.stage().GetRootLayer().ExportToString()
rows = []
for grid in (False, True):
    session.viewState().setGridEnabled(grid)
    for mode in ('None', 'Move', 'Rotate', 'Scale'):
        if mode == 'None':
            stageviz.command.select_paths([])
            assert tb.wait_for_commands()
        else:
            stageviz.command.select_paths(['/World/Sphere'])
            assert tb.wait_for_commands()
            test.activate_mode(mode)
        camera.setAxisYaw(0)
        camera.setAxisPitch(20)
        camera.setAxisRoll(0)
        tb.process_events(100)
        elapsed = []
        frames = []
        def rendered(ms):
            frames.append(int(ms))
        widget = test.viewport()
        widget.renderReady.connect(rendered)
        completed = 0
        skipped = 0
        try:
            for i in range(2 * (WARMUP + SAMPLES)):
                # Exercise distance-dependent sizing as well as camera rotation.
                camera.setCameraDistance(8 + .5 * math.sin(i * .08))
                camera.setAxisYaw(i * .7)
                previous_frames = len(frames)
                start = time.perf_counter()
                widget.repaint()
                tb.process_events(0)
                duration = (time.perf_counter() - start) * 1000
                if len(frames) == previous_frames:
                    skipped += 1
                    continue
                if completed >= WARMUP:
                    elapsed.append(duration)
                completed += 1
                if completed == WARMUP + SAMPLES:
                    break
        finally:
            widget.renderReady.disconnect(rendered)
        assert len(elapsed) == SAMPLES, 'each sample must actually render within the bounded retry limit'
        assert session.stage().GetRootLayer().ExportToString() == original
        expected = [] if mode == 'None' else ['/World/Sphere']
        assert list(session.paths()) == expected
        rows.append(dict(cubes=COUNT, grid=grid, mode=mode,
                         repaint_ms=elapsed, rendered_frames=len(frames), skipped_repaints=skipped,
                         viewport=[widget.width(), widget.height(), widget.devicePixelRatioF()]))
(OUT / ('samples-%d.json' % COUNT)).write_text(json.dumps(rows, indent=2))
with contextlib.redirect_stdout(io.StringIO()):
    snapshot = tb.snapshot_qt_state('benchmark after measurements')
(OUT / ('qt-%d.json' % COUNT)).write_text(json.dumps(snapshot, indent=2))
assert tb.viewer().grab().save(str(OUT / ('viewer-%d.png' % COUNT)))
'''


def write_fixture(filename, count):
    # Author disposable test data without per-prim USD notices in the UI process.
    lines = ['#usda 1.0', '(defaultPrim = "World"', 'upAxis = "Z"',
             'metersPerUnit = 1)', 'def Xform "World" {', 'def Sphere "Sphere" {}']
    for i in range(count):
        lines.extend(['def Cube "Cube_%05d" {' % i, 'double size = 0.12',
                      'double3 xformOp:translate = (%s, %s, -1.2)' % (
                          (i % 50 - 25) * .16, (i // 50 - 20) * .16),
                      'uniform token[] xformOpOrder = ["xformOp:translate"]', '}'])
    lines.append('}')
    filename.write_text('\n'.join(lines), encoding='utf-8')


def percentile(values, fraction):
    return sorted(values)[round((len(values) - 1) * fraction)]


def run(args):
    root = Path(__file__).resolve().parents[2]
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = []
    # Reverse each alternate round to reduce ordering/thermal bias.
    for repeat in range(args.rounds):
        variants = [('before', args.before), ('after', args.after)]
        if repeat % 2:
            variants.reverse()
        for label, binary in variants:
            directory = output / ('%s-%d' % (label, repeat))
            with DisposableStageviz(binary, directory, port=0) as app:
                app.python("from PySide6 import QtCore; import stageviz; w=stageviz.application().window(); w.resize(1500,1000); w.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint,True); w.show(); w.raise_(); w.activateWindow()")
                for count in args.cubes:
                    write_fixture(directory / ('scene-%d.usda' % count), count)
                    before = app.oracle()
                    code = ('ROOT=__import__("pathlib").Path(%r)\n'
                            'OUT=__import__("pathlib").Path(%r)\n'
                            'COUNT=%d\nWARMUP=%d\nSAMPLES=%d\n') % (
                                str(root), str(directory), count, args.warmup, args.samples)
                    app.python(code + WORKFLOW)
                    after = app.oracle()
                    stage_info = json.loads(after['stageviz_get_stage_info']['result']['content'][0]['text'])
                    selected = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                    assert Path(stage_info['filename']).resolve() == (directory / ('scene-%d.usda' % count)).resolve()
                    assert selected == ['/World/Sphere'], 'independent MCP selection agrees with the workflow'
                    (directory / ('oracle-%d.json' % count)).write_text(
                        json.dumps(dict(before=before, after=after), indent=2))
                    rss = None
                    if sys.platform != 'win32':
                        rss = float(subprocess.check_output(
                            ['ps', '-o', 'rss=', '-p', str(app.process.pid)], text=True)) / 1024
                    rows = json.loads((directory / ('samples-%d.json' % count)).read_text())
                    for row in rows:
                        row.update(variant=label, repeat=repeat, rss_mib=rss)
                    results.extend(rows)
                    print(label, repeat, count, 'completed', flush=True)
    comparisons = []
    for count in args.cubes:
        for grid in (False, True):
            for mode in ('None', 'Move', 'Rotate', 'Scale'):
                item = dict(cubes=count, grid=grid, mode=mode)
                for variant in ('before', 'after'):
                    rows = [r for r in results if r['variant'] == variant and r['cubes'] == count
                            and r['grid'] == grid and r['mode'] == mode]
                    assert len(rows) == args.rounds
                    assert len({tuple(r['viewport']) for r in results if r['cubes'] == count}) == 1, 'matching viewport sizes'
                    # Medians across independent processes, not a pooled best run.
                    item[variant + '_median_ms'] = statistics.median(
                        statistics.median(r['repaint_ms']) for r in rows)
                    item[variant + '_p95_ms'] = statistics.median(
                        percentile(r['repaint_ms'], .95) for r in rows)
                    item[variant + '_rss_mib'] = statistics.median(r['rss_mib'] for r in rows
                                                                 if r['rss_mib'] is not None) if sys.platform != 'win32' else None
                item['median_change_percent'] = 100 * (item['after_median_ms'] / item['before_median_ms'] - 1)
                comparisons.append(item)
    (output / 'summary.json').write_text(json.dumps(dict(results=results, comparisons=comparisons), indent=2))
    print(json.dumps(comparisons, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', required=True)
    parser.add_argument('--after', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--samples', type=int, default=120)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--cubes', type=int, nargs='+', default=[0, 2000])
    arguments = parser.parse_args()
    if arguments.rounds < 1 or arguments.samples < 1 or arguments.warmup < 0 or min(arguments.cubes) < 0:
        parser.error('rounds/samples must be positive and warmup/cube counts nonnegative')
    run(arguments)
