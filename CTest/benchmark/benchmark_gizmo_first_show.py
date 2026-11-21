# SPDX-License-Identifier: BSD-3-Clause
"""Measure first handle presentation and later switches in fresh disposable apps.

Run serially for each build. Timings cover CPU dispatch through renderReady,
including driver stalls; they are not GPU execution queries. Shader/OS caches
are not cleared. Screenshots and independent MCP oracles are outside timings.
"""
import argparse
import json
from pathlib import Path
import statistics

from mcp_runtime import DisposableStageviz

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = r'''
import json, time, runpy
from pathlib import Path
import stageviz
from pxr import UsdGeom
ns = runpy.run_path(str(ROOT / 'CTest/python/testviewport.py'), init_globals={'_VIEWPORT_AUTORUN': False})
tb = ns['tb']
test = ns['ViewportTest'](disposable=True)
session = stageviz.session()
assert session.load(str(OUT / 'scene.usda')) and tb.wait_for_commands()
(QtCore, _, _, QtWidgets) = tb.qt()

def button(mode):
    return next(w for w in tb.all_widgets()
                if w.objectName() == mode.lower()
                and isinstance(w, QtWidgets.QToolButton) and w.isVisible())
button('Select').click()
session.viewState().camera().setFocusPoint(0, 0, 0)
session.viewState().camera().setCameraDistance(8)
session.viewState().setSceneStatsEnabled(False)
session.viewState().setPerformanceStatsEnabled(False)
stageviz.command.select_paths(['/World/Cube'])
assert tb.wait_for_commands()
tb.process_events(250)
widget = test.viewport()
assert widget.grab().save(str(OUT / 'select.png'))
(OUT / 'qt-before.json').write_text(json.dumps(tb.snapshot_qt_state('Before first handle', root=widget), indent=2))
original = session.stage().GetRootLayer().ExportToString()
original_session = session.stage().GetSessionLayer().ExportToString()
aux = session.auxiliary()
built_before = bool(aux.GetPrimAtPath('/Display/Transform/Move/X'))
root_prim = aux.GetPrimAtPath('/Display/Transform')
if root_prim:
    assert UsdGeom.Imageable(root_prim).ComputeVisibility() == 'invisible'

def colored_pixels():
    image = widget.grab().toImage().scaledToWidth(320)
    ratio = image.width() / widget.width()
    count = 0
    for y in range(image.height()):
        for x in range(image.width()):
            if x < 80 * ratio and y > image.height() - 80 * ratio:
                continue
            c = image.pixelColor(x, y)
            (r, g, b) = (c.red(), c.green(), c.blue())
            if ((r > 100 and r > 2.5*g and r > 2*b)
                    or (g > 100 and g > 1.7*r and g > 1.7*b)
                    or (b > 100 and b > 1.7*r and b > 1.5*g)):
                count += 1
    return count
assert colored_pixels() == 0, 'hidden warmup must not present handles'
frames = []

def rendered(ms):
    frames.append((time.perf_counter(), int(ms)))
widget.renderReady.connect(rendered)
results = []
try:
    for repeat in range(4):
        for mode in [FIRST] + [m for m in ['Move', 'Rotate', 'Scale'] if m != FIRST]:
            button('Select').click()
            widget.repaint()
            previous = len(frames)
            start = time.perf_counter()
            button(mode).click()
            dispatch = (time.perf_counter() - start) * 1000
            widget.repaint()
            assert tb.wait_until(lambda: len(frames) > previous, timeout=5)
            results.append({'mode': mode, 'repeat': repeat, 'dispatch_ms': dispatch, 'first_frame_ms': (frames[previous][0] - start) * 1000, 'render_ms': frames[previous][1]})
finally:
    widget.renderReady.disconnect(rendered)
for mode in ('Move', 'Rotate', 'Scale'):
    button(mode).click()
    widget.repaint()
    assert colored_pixels() >= 5, (mode, 'handles must render after warmup')
    assert widget.grab().save(str(OUT / (mode.lower() + '.png')))
(OUT / 'qt-after.json').write_text(json.dumps(tb.snapshot_qt_state('Handles ready', root=widget), indent=2))
assert session.stage().GetRootLayer().ExportToString() == original
assert session.stage().GetSessionLayer().ExportToString() == original_session
assert list(session.paths()) == ['/World/Cube']
assert widget.grab().save(str(OUT / 'handles.png'))
(OUT / 'measurements.json').write_text(json.dumps({'first_mode': FIRST, 'built_before': built_before, 'viewport': [widget.width(), widget.height()], 'rows': results}, indent=2))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--rounds', type=int, default=2)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('--rounds must be positive')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    rows = []
    for round_index in range(args.rounds):
        for mode in ('Move', 'Rotate', 'Scale'):
            directory = output / ('%d-%s' % (round_index, mode))
            with DisposableStageviz(args.app.resolve(), directory, port=0) as app:
                (directory / 'scene.usda').write_text(
                    '#usda 1.0\n(defaultPrim="World"\nupAxis="Z")\n'
                    'def Xform "World" {\n def Cube "Cube" {\n }\n}\n')
                before = app.oracle()
                prefix = ('from pathlib import Path\nROOT=Path(%r)\nOUT=Path(%r)\nFIRST=%r\n'
                          % (str(ROOT), str(directory), mode))
                app.python(prefix + WORKFLOW)
                after = app.oracle()
                (directory / 'oracles.json').write_text(
                    json.dumps({'before': before, 'after': after}, indent=2))
                data = json.loads((directory / 'measurements.json').read_text())
                data['round'] = round_index
                rows.append(data)
                warm = statistics.median(v['first_frame_ms'] for v in data['rows'] if v['repeat'] > 0)
                print(round_index, mode, 'prepared:', data['built_before'],
                      'first: %.2f ms, repeated: %.2f ms' % (data['rows'][0]['first_frame_ms'], warm), flush=True)
    (output / 'summary.json').write_text(json.dumps(rows, indent=2))


if __name__ == '__main__':
    main()
