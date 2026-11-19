# SPDX-License-Identifier: BSD-3-Clause
"""Compare synthetic isolation timings through disposable Stageviz MCP processes."""
import argparse
import json
import statistics
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mcp_runtime import DisposableStageviz


def run(args):
    assert args.repeats > 0 and all(count >= 200 and count % 100 == 0 for count in args.counts)
    assert args.max_regression_percent is None or args.max_regression_percent >= 0
    assert args.regression_slack_ms >= 0
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    script = Path(__file__).resolve().parents[1] / 'isolation_performance.py'
    results = []
    raw_results = []
    for count in args.counts:
        for kind in ('ordinary', 'instances'):
            for repeat in range(args.repeats):
                case = output / ('%s-%d-run%d' % (kind, count, repeat + 1))
                fixture = None
                if args.fixtures:
                    root = Path(args.fixtures).resolve()
                    candidates = [root / ('%s-%d' % (kind, count)) / 'fixture.usda',
                                  root / ('%s-%d-run1' % (kind, count)) / 'fixture.usda']
                    fixture = str(next(path for path in candidates if path.is_file()))
                print('START', kind, count, repeat + 1, flush=True)
                with DisposableStageviz(args.app, case, port=0) as app:
                    for phase, code in [
                        ('setup', "import runpy; ns=runpy.run_path(%r); bench=ns['IsolationPerformance'](%r,%r,%d,%r); bench.setup()" %
                         (str(script), str(case), kind, count, fixture)),
                        ('exercise', 'bench.exercise()'), ('cleanup', 'bench.cleanup()')]:
                        before = app.oracle()
                        result = app.python(code, timeout=1800)
                        after = app.oracle()
                        (case / (phase + '-mcp.json')).write_text(json.dumps(dict(before=before, result=result, after=after), indent=2))
                        print('PASS', kind, count, repeat + 1, phase, flush=True)
                timing = json.loads((case / 'timings.json').read_text())
                resources = [json.loads(line) for line in (case / 'resources.jsonl').read_text().splitlines()]
                timing['peak_rss_mib'] = max((row['rss_mib'] for row in resources), default=0)
                raw_results.append(timing)
                (output / 'runs.json').write_text(json.dumps(raw_results, indent=2))
            runs = [row for row in raw_results if row['kind'] == kind and row['count'] == count]
            assert len({row['fixture_sha256'] for row in runs}) == 1
            rows = [row for run in runs for row in run['rows']]
            summary = {}
            for label in sorted({row['label'] for row in rows}):
                values = [row['total_ms'] for row in rows if row['label'] == label]
                summary[label] = dict(median_ms=statistics.median(values), max_ms=max(values), samples=len(values))
            combined = dict(kind=kind, count=count, fixture_sha256=runs[0]['fixture_sha256'], rows=rows,
                            summary=summary, peak_rss_mib=max(run['peak_rss_mib'] for run in runs))
            results.append(combined)
            (output / 'summary.json').write_text(json.dumps(results, indent=2))
            print(json.dumps(summary), flush=True)
    if args.baseline:
        baseline = {(r['kind'], r['count']): r for r in json.loads(Path(args.baseline).read_text())}
        comparison = []
        for result in results:
            old = baseline[(result['kind'], result['count'])]
            assert result['fixture_sha256'] == old['fixture_sha256'], 'Fixture changed between comparisons'
            for label, values in result['summary'].items():
                before = old['summary'][label]['median_ms']
                after = values['median_ms']
                regressed = (args.max_regression_percent is not None
                             and after > before * (1 + args.max_regression_percent / 100)
                             and after - before > args.regression_slack_ms)
                comparison.append(dict(kind=result['kind'], count=result['count'], phase=label,
                                       before_ms=before, after_ms=after,
                                       ratio=after/before, regressed=regressed))
        (output / 'comparison.json').write_text(json.dumps(comparison, indent=2))
        regressions = [row for row in comparison if row['regressed']]
        assert not regressions, 'Isolation performance regressions: %r' % regressions


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--baseline')
    parser.add_argument('--fixtures', help='Reuse fixture files from an earlier benchmark output')
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--counts', nargs='+', type=int, default=[2000, 10000])
    parser.add_argument('--max-regression-percent', type=float,
                        help='Fail comparisons exceeding this percentage and the absolute slack')
    parser.add_argument('--regression-slack-ms', type=float, default=5,
                        help='Absolute noise allowance when enforcing a regression budget (default: 5 ms)')
    run(parser.parse_args())
