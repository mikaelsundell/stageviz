# SPDX-License-Identifier: BSD-3-Clause
"""External MCP controller for repeatable selection overlap/performance checks."""
import argparse
import json
from pathlib import Path
from mcp_runtime import DisposableStageviz


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--baseline', action='store_true', help='Record the existing coincident-outline failure')
    parser.add_argument('--performance-first', action='store_true', help='Measure first selection before the overlap workflow')
    parser.add_argument('--counts', nargs='+', type=int, default=[10000, 30000])
    parser.add_argument('--samples', type=int, default=60)
    parser.add_argument('--parent-moves', type=int, default=5, help='Transform/undo cycles; 0 for redraw-only comparisons')
    args = parser.parse_args()
    output = Path(args.output).resolve()
    root = Path(__file__).resolve().parents[2]
    with DisposableStageviz(args.app, output) as app:
        app.python("from PySide6 import QtCore,QtWidgets,QtOpenGLWidgets; import stageviz,sys; w=stageviz.application().window(); w.resize(1500,1000)\n"
                   "if sys.platform != 'win32': w.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint,True)\n"
                   "w.show();w.raise_();w.activateWindow()")
        evidence = {'before': app.oracle()}
        script = root / 'CTest/python/testselectionoutline.py'
        app.python("import runpy; ns=runpy.run_path(%r); test=ns['SelectionOutlineTest'](disposable=True, output=%r, baseline=%r); test.setup()" %
                   (str(script), str(output / 'checks'), args.baseline))
        evidence['setup'] = app.oracle()
        evidence['prims'] = app.tool('stageviz_list_prims', {'path': '/World', 'maxDepth': 1})
        if not args.performance_first:
            app.python('test.overlap()')
            evidence['overlap'] = app.oracle()
        for count in args.counts:
            app.python('test.performance(%d, %d, %d)' % (count, args.samples, args.parent_moves))
            evidence[str(count)] = app.oracle()
            evidence['group-' + str(count)] = app.tool('stageviz_get_prim_info', {'path': '/World/Group'})
        if args.performance_first:
            app.python('test.cleanup(); test.setup(); test.overlap()')
            evidence['overlap'] = app.oracle()
        app.python('test.cleanup()')
        evidence['cleanup'] = app.oracle()
        (output / 'oracle.json').write_text(json.dumps(evidence, indent=2))
    print(output / 'checks/results.json')


if __name__ == '__main__':
    main()
