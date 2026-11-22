# SPDX-License-Identifier: BSD-3-Clause
"""Focused before/after selection benchmark using disposable apps and identical fixtures.

Example: python3 CTest/benchmark/compare_selection_pipeline.py --baseline APP
         --candidate APP --output DIRECTORY
No CTest suites are run. Timings include framebuffer readback, not GPU-only time.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
from mcp_runtime import DisposableStageviz

WORKFLOW = r'''import runpy, json, time, random, statistics
from pathlib import Path
from pxr import Usd, UsdGeom, Gf
import stageviz
ns = runpy.run_path(SCRIPT)
test = ns['SelectionOutlineTest'](disposable=True, output=OUTPUT)
tb = ns['tb']
test.setup()
test.performance(COUNT, SAMPLES, 2)
# Nested native instances exercise grouped prototype traversal and flattened IDs.
asset = Path(OUTPUT) / 'nested_asset.usda'
asset.write_text("""#usda 1.0
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
""")
n = COUNT // 2
width = int(n ** .5)
paths = ['/World/Group/I%05d' % i for i in range(n)]
lines = ['#usda 1.0', '(defaultPrim = "World"\nupAxis = "Y")',
         'def Xform "World" {\ndef Xform "Group" {']
for i in range(n):
    lines.append('def Xform "I%05d" (\nprepend references = @nested_asset.usda@</Assembly>\ninstanceable = true\n) {\ndouble3 xformOp:translate = (%g, 0, %g)\nuniform token[] xformOpOrder = ["xformOp:translate"]\n}' % (i, i % width, i // width))
lines.append('}\n}')
fixture = Path(OUTPUT) / 'nested.usda'
fixture.write_text('\n'.join(lines))
assert stageviz.session().loadFromFile(str(fixture))
assert tb.wait_for_commands()
state = stageviz.session().viewState()
state.camera().setCameraView(stageviz.CameraViewTop)
state.camera().frame(stageviz.session().boundingBox())
tree = next(w for w in tb.all_widgets() if w.objectName() == "outlinerTree" and w.isVisible())
tree.expandAll()
tb.process_events(200)
stage = stageviz.session().stage()
assert stage.GetPrimAtPath(paths[0]).IsInstance()
assert stage.GetPrimAtPath(paths[0] + '/Left/Shape').IsInstanceProxy()
model = tree.model()
pending = [model.index(i, 0) for i in range(model.rowCount())]
while pending:
    index = pending.pop()
    if model.data(index) == 'Group':
        assert model.rowCount(index) == n and tree.isExpanded(index)
        break
    pending.extend(model.index(i, 0, index) for i in range(model.rowCount(index)))
else:
    raise AssertionError('populated Outliner branch missing')
test.select([])
for _ in range(8): test.image()
rows = {}
def measure(label, operations):
    samples = []
    start_wall = time.time() * 1000
    for operation in operations:
        start = time.perf_counter()
        operation()
        test.image()
        samples.append((time.perf_counter() - start) * 1000)
    rows[label] = dict(samplesMs=samples, medianMs=statistics.median(samples),
                       p95Ms=sorted(samples)[int(.95*(len(samples)-1))],
                       wallStartMs=start_wall, wallEndMs=time.time()*1000)
selected = paths[::5]
measure('firstGroupedInstances', [lambda: test.select(selected)])
measure('cachedRedraw', [lambda: None] * SAMPLES)
def repeat():
    test.select([])
    test.select(selected)
measure('repeatGroupedInstances', [repeat] * SAMPLES)
rng = random.Random(814)
proxy_groups = [[paths[i] + ('/Left/Shape' if j % 2 else '/Right/Shape')
                 for j, i in enumerate(rng.sample(range(n), min(500, n)))]
                for _ in range(SAMPLES)]
measure('randomProxyGroups', [lambda group=group: test.select(group) for group in proxy_groups])
measure('parentSelection', [lambda: test.select(['/World/Group'])])
measure('parentCachedRedraw', [lambda: None] * SAMPLES)
before = Gf.Matrix4d(1)
after = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(.1, 0, 0))
matrix = lambda m: tuple(tuple(float(m[r][c]) for c in range(4)) for r in range(4))
def move_parent():
    stageviz.command.set_transforms(['/World/Group'], [matrix(before)], [matrix(after)])
    assert tb.wait_for_commands()
    test.image()
    stageviz.command.undo()
    assert tb.wait_for_commands()
    assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath('/World/Group')), before, 1e-10)
measure('parentMoveUndo', [move_parent] * 3)
assert list(stageviz.session().paths()) == ['/World/Group']
assert test.selected_pixels() > 20
test.snapshot('nested-selected')
test.results['nested'] = rows
test.results['nestedInstanceRoots'] = n
test.write_results()
'''


def summarize_native(path, result):
    events = [json.loads(line) for line in path.read_text().splitlines()]
    for row in result['nested'].values():
        times = [event['duration_ms'] for event in events
                 if event['phase'] == 'selection.hydra_mapping'
                 and row['wallStartMs'] <= event['wall_start_ms'] <= row['wallEndMs']]
        row['hydraMapping'] = dict(count=len(times), samplesMs=times,
                                  medianMs=statistics.median(times) if times else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--count', type=int, default=10000)
    parser.add_argument('--samples', type=int, default=24)
    parser.add_argument('--rounds', type=int, default=2)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    script = Path(__file__).resolve().parents[1] / 'python/testselectionoutline.py'
    reports = []
    # Alternate order to expose process-order and thermal/cache effects.
    for round_index in range(args.rounds):
        order = ['baseline', 'candidate'] if round_index % 2 == 0 else ['candidate', 'baseline']
        for label in order:
            directory = output / ('%d-%s' % (round_index + 1, label))
            phases = directory / 'phases.jsonl'
            print('RUN', directory.name, flush=True)
            with DisposableStageviz(getattr(args, label), directory,
                                    environment={'STAGEVIZ_PERF_LOG': str(phases)}) as app:
                app.python("from PySide6 import QtCore; import stageviz; w=stageviz.application().window(); w.resize(1500,1000); w.show(); w.raise_(); w.activateWindow()")
                before = app.oracle()
                code = 'SCRIPT=%r; OUTPUT=%r; COUNT=%d; SAMPLES=%d\n' % (str(script), str(directory/'checks'), args.count, args.samples)
                app.python(code + WORKFLOW)
                oracle = dict(before=before, after=app.oracle(),
                              prim=app.tool('stageviz_get_prim_info', {'path': '/World/Group/I00000/Left/Shape'}),
                              branch=app.tool('stageviz_list_prims', {'path': '/World/Group', 'maxDepth': 1, 'limit': 10}))
                selected = json.loads(oracle['after']['stageviz_get_selection']['result']['content'][0]['text'])
                assert selected == ['/World/Group'], selected
                (directory/'oracle.json').write_text(json.dumps(oracle, indent=2))
                app.python('test.cleanup()')
            result = json.loads((directory/'checks/results.json').read_text())
            summarize_native(phases, result)
            result.update(label=label, round=round_index+1,
                          fixtureSha256=hashlib.sha256((directory/'checks/nested.usda').read_bytes()).hexdigest())
            reports.append(result)
            (output/'comparison.json').write_text(json.dumps(reports, indent=2))
            print('PASS', directory.name, flush=True)
    assert len({row['fixtureSha256'] for row in reports}) == 1
    assert len({tuple(row['viewport']) for row in reports}) == 1
    print(output/'comparison.json', flush=True)


if __name__ == '__main__':
    main()
