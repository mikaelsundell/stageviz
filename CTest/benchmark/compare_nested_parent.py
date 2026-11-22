# SPDX-License-Identifier: BSD-3-Clause
"""Compare warmed nested-instance parent moves in serial disposable viewers.

Reports process medians separately. Standard waits retain testbench's event
settling; completion waits poll the same Session barrier without the extra
post-completion delay. Both synchronize rendering through framebuffer readback.
"""

import argparse
import hashlib
import json
from pathlib import Path
import statistics

from mcp_runtime import DisposableStageviz


WORKFLOW = r'''import json, runpy, time
from pathlib import Path
from pxr import Gf, UsdGeom
import stageviz
ns = runpy.run_path(SCRIPT)
test = ns['SelectionOutlineTest'](disposable=True, output=OUTPUT)
tb = ns['tb']
test.setup()
session = stageviz.session()
session.setPreserveState(False)
assert session.loadFromFile(FIXTURE)
assert tb.wait_for_commands()
session.viewState().camera().setCameraView(stageviz.CameraViewTop)
session.viewState().camera().frame(session.boundingBox())
tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
tree.expandAll()
tb.process_events(200)
model = tree.model()
pending = [model.index(i, 0) for i in range(model.rowCount())]
while pending:
    index = pending.pop()
    if model.data(index) == 'Group':
        assert model.rowCount(index) == COUNT and tree.isExpanded(index)
        break
    pending.extend(model.index(i, 0, index) for i in range(model.rowCount(index)))
else:
    raise AssertionError('populated Outliner branch missing')
stage = session.stage()
proxy = stage.GetPrimAtPath('/World/Group/I00000/Left/Shape')
assert proxy.IsInstanceProxy()
test.select(['/World/Group'])
for _ in range(8):
    test.image()
original = stage.GetRootLayer().ExportToString()
identity = Gf.Matrix4d(1)
translated = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(.1, 0, 0))
matrix = lambda value: tuple(tuple(float(value[r][c]) for c in range(4)) for r in range(4))
before, after = matrix(identity), matrix(translated)
qt_core, _, qt_test, qt_widgets = tb.qt()
frames = [0]
test.viewport().renderReady.connect(lambda _: frames.__setitem__(0, frames[0] + 1))

def wait(mode):
    if mode == 'standard':
        assert tb.wait_for_commands()
        return
    deadline = time.perf_counter() + 60
    flags = (qt_core.QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents
             | qt_core.QEventLoop.ProcessEventsFlag.ExcludeSocketNotifiers)
    while True:
        qt_widgets.QApplication.processEvents(flags)
        if session.primsUpdate() == stageviz.PrimsImmediate:
            return
        assert time.perf_counter() < deadline, 'command completion timed out'
        qt_test.QTest.qWait(1)

def sample(mode):
    row = {'wallStartMs': time.time() * 1000}
    frame_start = frames[0]
    start = previous = time.perf_counter()
    def mark(name):
        nonlocal previous
        now = time.perf_counter()
        row[name] = (now - previous) * 1000
        previous = now
    stageviz.command.set_transforms(['/World/Group'], [before], [after])
    mark('moveDispatchMs')
    wait(mode)
    mark('moveWaitMs')
    test.image()
    mark('moveReadbackMs')
    assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(proxy).ExtractTranslation()[0], .1, 1e-9)
    mark('moveAssertMs')
    stageviz.command.undo()
    mark('undoDispatchMs')
    wait(mode)
    mark('undoWaitMs')
    test.image()
    mark('undoReadbackMs')
    row['totalMs'] = (time.perf_counter() - start) * 1000
    row['frameCount'] = frames[0] - frame_start
    row['wallEndMs'] = time.time() * 1000
    row['windowActive'] = tb.viewer().isActiveWindow()
    row['windowMinimized'] = tb.viewer().isMinimized()
    row['viewportVisible'] = test.viewport().isVisible()
    assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath('/World/Group')), identity, 1e-10)
    assert session.paths() == ['/World/Group']
    return row

results = {'viewport': [test.viewport().width(), test.viewport().height(), test.viewport().devicePixelRatioF()],
           'instanceRoots': COUNT, 'modes': {}}
for mode in MODES:
    warmup = [sample(mode) for _ in range(WARMUP)]
    rows = [sample(mode) for _ in range(SAMPLES)]
    assert stage.GetRootLayer().ExportToString() == original, 'undo restores exact layer state'
    results['modes'][mode] = {'warmup': warmup, 'samples': rows}
assert test.selected_pixels() > 20
test.snapshot('nested-parent-restored')
Path(OUTPUT, 'measurements.json').write_text(json.dumps(results, indent=2))
print('NESTED_PARENT_RESULT ' + json.dumps({mode: [r['totalMs'] for r in data['samples']]
                                         for mode, data in results['modes'].items()}))
'''


def fixture(directory, count):
    asset = directory / 'nested_asset.usda'
    asset.write_text('''#usda 1.0
def Xform "Part" {
    def Cube "Shape" {
        double size = 0.3
    }
}
def Xform "Assembly" {
    def Xform "Left" (
        prepend references = </Part>
        instanceable = true
    ) {}
    def Xform "Right" (
        prepend references = </Part>
        instanceable = true
    ) {
        double3 xformOp:translate = (0.4, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
''')
    width = max(1, int(count ** .5))
    lines = ['#usda 1.0', '(defaultPrim = "World"\nupAxis = "Y")',
             'def Xform "World" {\ndef Xform "Group" {']
    for index in range(count):
        lines.append('''def Xform "I%05d" (
prepend references = @nested_asset.usda@</Assembly>
instanceable = true
) {
double3 xformOp:translate = (%g, 0, %g)
uniform token[] xformOpOrder = ["xformOp:translate"]
}''' % (index, index % width, index // width))
    lines.append('}\n}')
    path = directory / 'nested.usda'
    path.write_text('\n'.join(lines))
    return path


def summarize(reports):
    result = {}
    for mode in reports[0]['modes']:
        row = {}
        for label in ('baseline', 'candidate'):
            processes = [report for report in reports if report['label'] == label]
            if not processes:
                continue
            medians = [statistics.median(sample['totalMs'] for sample in report['modes'][mode]['samples'])
                       for report in processes]
            row[label] = {'processMediansMs': medians, 'meanProcessMedianMs': statistics.mean(medians)}
        if len(row) == 2:
            row['changePercent'] = 100 * (row['candidate']['meanProcessMedianMs']
                                         / row['baseline']['meanProcessMedianMs'] - 1)
        result[mode] = row
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--count', type=int, default=5000, help='Outer instance roots; each contains two nested cubes')
    parser.add_argument('--samples', type=int, default=12)
    parser.add_argument('--warmup', type=int, default=4)
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--instrumented', action='store_true')
    args = parser.parse_args()
    assert min(args.count, args.samples, args.rounds) > 0 and args.warmup >= 0
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    path = fixture(output, args.count)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    scripts = Path(__file__).resolve().parents[1] / 'python'
    (output / 'manifest.json').write_text(json.dumps({
        'arguments': vars(args), 'scriptSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'fixtureSha256': digest,
        'assetSha256': hashlib.sha256((output / 'nested_asset.usda').read_bytes()).hexdigest(),
        'unitOfReplication': 'one disposable process; samples within a process share caches and event-loop state',
    }, indent=2))
    reports = []
    for round_index in range(args.rounds):
        order = ['baseline', 'candidate'] if round_index % 2 == 0 else ['candidate', 'baseline']
        for label in order:
            directory = output / ('%d-%s' % (round_index + 1, label))
            modes = ['standard', 'completion'] if len(reports) % 2 == 0 else ['completion', 'standard']
            environment = {'STAGEVIZ_PERF_LOG': str(directory / 'phases.jsonl') if args.instrumented else ''}
            print('RUN', directory.name, modes, flush=True)
            with DisposableStageviz(getattr(args, label), directory, port=0, environment=environment) as app:
                app.python('import stageviz; w=stageviz.application().window(); w.resize(1500,1000); '
                           'w.show(); w.raise_(); w.activateWindow()')
                before = app.oracle()
                code = 'SCRIPT=%r; OUTPUT=%r; FIXTURE=%r; COUNT=%d; SAMPLES=%d; WARMUP=%d; MODES=%r\n' % (
                    str(scripts / 'testselectionoutline.py'), str(directory / 'checks'), str(path),
                    args.count, args.samples, args.warmup, modes)
                app.python(code + WORKFLOW)
                after = app.oracle()
                prim = app.tool('stageviz_get_prim_info', {'path': '/World/Group/I00000/Left/Shape'})
                assert json.loads(after['stageviz_get_selection']['result']['content'][0]['text']) == ['/World/Group']
                (directory / 'oracle.json').write_text(json.dumps(dict(before=before, after=after, proxy=prim), indent=2))
                report = json.loads((directory / 'checks/measurements.json').read_text())
                report.update(label=label, round=round_index + 1, fixtureSha256=digest,
                              modeOrder=modes, instrumented=args.instrumented)
                reports.append(report)
                (output / 'comparison.json').write_text(json.dumps(reports, indent=2))
                (output / 'summary.json').write_text(json.dumps(summarize(reports), indent=2))
                app.python('test.cleanup()')
            assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, 'fixture on disk changed'
            print('PASS', directory.name, flush=True)
    assert len({tuple(report['viewport']) for report in reports}) == 1
    print(json.dumps(summarize(reports), indent=2), flush=True)


if __name__ == '__main__':
    main()
