# SPDX-License-Identifier: BSD-3-Clause
"""Compare real gizmo drags in serial disposable baseline/candidate apps.

Uses synthetic distant geometry, populated Outliner branches, a fixed-seed random
selection, all-object selections and parent moves. Measures mouse dispatch and
dispatch-plus-repaint separately; framebuffer captures and USD assertions run
outside the timing loop. Repaint calls may coalesce or skip frames. These are
CPU wall times, not GPU execution timings; use dispatch and native clipping
phase timings to assess input-path overhead.
"""
import argparse
import json
from pathlib import Path
import statistics

from mcp_runtime import DisposableStageviz

ROOT = Path(__file__).resolve().parents[2]


def fixture(path, count):
    width = max(1, int(count ** .5))
    with path.open('w', encoding='utf-8') as stream:
        stream.write('#usda 1.0\n(defaultPrim = "World"\nupAxis = "Y"\nmetersPerUnit = 1)\n'
                     'def Xform "World" {\ndef Xform "Group" {\n'
                     'double3 xformOp:translate = (120000, -250000, 340000)\n'
                     'uniform token[] xformOpOrder = ["xformOp:translate"]\n')
        for i in range(count):
            stream.write('def Cube "Item%05d" {\ndouble size = %g\n'
                         'double3 xformOp:translate = (%g, 0, %g)\n'
                         'uniform token[] xformOpOrder = ["xformOp:translate"]\n}\n'
                         % (i, 2 if count == 1 else .08, (i % width) * .12, (i // width) * .12))
        stream.write('}\n}\n')


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    reports = []
    for count in args.counts:
        scene = output / ('scene-%d.usda' % count)
        fixture(scene, count)
        for label, executable in (('baseline', args.baseline), ('candidate', args.candidate)):
            directory = output / ('%s-%d' % (label, count))
            trace = directory / 'native-phases.jsonl'
            with DisposableStageviz(executable, directory, port=0, configuration=args.configuration,
                                    environment={'STAGEVIZ_PERF_LOG': str(trace)}) as app:
                script = str(ROOT / 'CTest/python/testdragclipping.py')
                app.python('import runpy,json\n'
                           'ns=runpy.run_path(%r,init_globals={"_VIEWPORT_AUTORUN":False})\n'
                           'test=ns["DragClippingTest"](disposable=True)\n'
                           'test.setup(%r,%d)\n' % (script, str(scene), count))
                cases = ('all',) if count == 1 else ('random', 'all', 'parent')
                for case in cases:
                    before = app.oracle()
                    result = app.python('row=test.drag(%r,samples=%d,verify_clipping=%r)' %
                                        (case, args.samples, label == 'candidate'))
                    after = app.oracle()
                    after['prim'] = app.tool('stageviz_get_prim_info', {'path': '/World/Group/Item00000'})
                    for block in result['result']['content']:
                        for line in block.get('text', '').splitlines():
                            if line.startswith('DRAG_CLIPPING_RESULT '):
                                row = json.loads(line.split(' ', 1)[1])
                                row.update(build=label, case=case)
                                reports.append(row)
                    selected = json.loads(after['stageviz_get_selection']['result']['content'][0]['text'])
                    assert len(selected) == row['selected'], (len(selected), row)
                    info_before = json.loads(before['stageviz_get_stage_info']['result']['content'][0]['text'])
                    info_after = json.loads(after['stageviz_get_stage_info']['result']['content'][0]['text'])
                    assert info_before == info_after, (info_before, info_after)
                    prim = json.loads(after['prim']['result']['content'][0]['text'])
                    assert prim['type'] == 'Cube' and prim['active'] and prim['defined'], prim
                    (directory / (case + '-oracle.json')).write_text(
                        json.dumps(dict(before=before, result=result, after=after), indent=2))
                    print('PASS', label, count, case, flush=True)
                app.python('test.cleanup()')
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            clipping = [event['duration_ms'] for event in events if event['phase'] == 'transform.preview_clipping']
            (directory / 'clipping-cost.json').write_text(json.dumps({
                'samples': len(clipping), 'medianMs': statistics.median(clipping) if clipping else None,
                'maxMs': max(clipping) if clipping else None}, indent=2))
            (output / 'measurements.json').write_text(json.dumps(reports, indent=2))
    for count in args.counts:
        for case in (('all',) if count == 1 else ('random', 'all', 'parent')):
            pair = [row for row in reports if row['objects'] == count and row['case'] == case]
            assert len(pair) == 2
            if count == 1:
                assert not pair[0]['visibleDuringDrag'] and pair[1]['visibleDuringDrag'], pair
            print(count, case, 'dispatch median baseline/candidate ms:',
                  *(round(row['dispatchMedianMs'], 3) for row in pair), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--counts', nargs='+', type=int, default=[1, 2000, 20000])
    parser.add_argument('--samples', type=int, default=12)
    parser.add_argument('--configuration', default='Debug', choices=('Debug', 'Release'))
    run(parser.parse_args())
