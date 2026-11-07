# SPDX-License-Identifier: BSD-3-Clause
"""Summarize measured interaction spans and inclusive native phase timings."""
import argparse
import csv
import json
from pathlib import Path
import statistics

GROUPS = [('Load', ['load']), ('Frame', ['frame']), ('Cold sweep', ['cold_sweep']),
          ('Warm sweep (median of 3)', ['warm_sweep_1', 'warm_sweep_2', 'warm_sweep_3']),
          ('Show Move gizmo', ['show_gizmo']), ('Bulk press', ['bulk_press']),
          ('Bulk drag step (median of 5)', ['bulk_drag_%d' % i for i in range(1, 6)]),
          ('Bulk release', ['bulk_release']), ('Bulk undo', ['bulk_undo']), ('Bulk redo', ['bulk_redo']),
          ('Parent press', ['parent_press']), ('Parent drag step (median of 2)', ['parent_drag_1', 'parent_drag_2']),
          ('Parent release', ['parent_release']), ('Parent undo', ['parent_undo']), ('Parent redo', ['parent_redo'])]


def load(directory):
    directory = Path(directory)
    rows = json.loads((directory / 'results.json').read_text())
    provenance = json.loads((directory / 'provenance.json').read_text())
    events = [json.loads(line) for line in (directory / 'native-phases.jsonl').read_text().splitlines()]
    resources = [json.loads(line) for line in (directory / 'resources.jsonl').read_text().splitlines()]
    by_phase = {}
    for row in rows:
        timings = {}
        for event in events:
            start = event['wall_start_ms']
            if row['start_wall_ms'] <= start and start + event['duration_ms'] <= row['start_wall_ms'] + row['end_to_end_ms']:
                timing = timings.setdefault(event['phase'], dict(calls=0, total_ms=0.0, maximum_ms=0.0))
                timing['calls'] += 1
                timing['total_ms'] += event['duration_ms']
                timing['maximum_ms'] = max(timing['maximum_ms'], event['duration_ms'])
        by_phase[row['phase']] = dict(measurement=row, native_inclusive=timings)
    summary = dict(provenance=provenance, phases=by_phase,
                   recorded_scopes=sorted({event['phase'] for event in events}),
                   peak_rss_mib=max(item['rss_mib'] for item in resources),
                   completed=(directory / 'passed.json').exists())
    (directory / 'analysis.json').write_text(json.dumps(summary, indent=2))
    return summary


def compare(baseline_dir, current_dir, output):
    baseline, current = load(baseline_dir), load(current_dir)
    assert baseline['completed'] and current['completed'], 'Only compare complete runs'
    assert baseline['provenance']['fixture']['sha256'] == current['provenance']['fixture']['sha256']
    for phase in baseline['phases']:
        before = baseline['phases'][phase]['measurement']
        after = current['phases'][phase]['measurement']
        assert before['selection_sha256'] == after['selection_sha256'], phase
        assert before['widget_size'] == after['widget_size'] and before['device_pixel_ratio'] == after['device_pixel_ratio'], phase
    lines = ['# Large-scene interaction comparison', '',
             'Same fixture, viewport, sweep rectangle, SDK and build configuration; sequential disposable app processes.', '',
             '| Operation | Master dispatch (s) | Branch dispatch (s) | Master response (s) | Branch response (s) | Response change |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    csv_rows = []
    for label, phases in GROUPS:
        b = [baseline['phases'][phase]['measurement'] for phase in phases]
        c = [current['phases'][phase]['measurement'] for phase in phases]
        bd = statistics.median(row['dispatch_ms'] for row in b) / 1000
        cd = statistics.median(row['dispatch_ms'] for row in c) / 1000
        br = statistics.median(row['responsive_ms'] for row in b) / 1000
        cr = statistics.median(row['responsive_ms'] for row in c) / 1000
        delta = (cr / br - 1) * 100
        lines.append('| %s | %.3f | %.3f | %.3f | %.3f | %+.1f%% |' % (label, bd, cd, br, cr, delta))
        csv_rows.append(dict(operation=label, master_dispatch_s=bd, branch_dispatch_s=cd,
                             master_response_s=br, branch_response_s=cr, response_change_percent=delta))
    lines += ['', 'Response means Session completion and at least three viewport presentations. '
              'The separate end-to-end measurement adds a 750ms event-loop settle floor. '
              'Dispatch measures the synchronous input handler. These are discrete drag steps, not sustained FPS.', '',
              '## Native breakdown', '',
              'Scopes are inclusive CPU wall times; nested scopes must not be added together. '
              'Checkpoint screenshots, USD assertions and external MCP queries are outside the measured spans. '
              'A dash means that scope was not recorded in that revision; it does not mean zero cost.', '']
    for phase in ('cold_sweep', 'warm_sweep_3', 'show_gizmo', 'bulk_press', 'bulk_drag_5', 'bulk_release', 'bulk_undo', 'parent_release'):
        lines.extend(['### ' + phase, '', '| Scope | Master total (s) | Branch total (s) |', '| --- | ---: | ---: |'])
        names = set(baseline['phases'][phase]['native_inclusive']) | set(current['phases'][phase]['native_inclusive'])
        names = sorted(names, key=lambda name: max(baseline['phases'][phase]['native_inclusive'].get(name, {}).get('total_ms', 0),
                                                  current['phases'][phase]['native_inclusive'].get(name, {}).get('total_ms', 0)), reverse=True)
        for name in names[:12]:
            b = baseline['phases'][phase]['native_inclusive'].get(name, {}).get('total_ms', 0) / 1000
            c = current['phases'][phase]['native_inclusive'].get(name, {}).get('total_ms', 0) / 1000
            before = '%.3f' % b if name in baseline['recorded_scopes'] else '—'
            after = '%.3f' % c if name in current['recorded_scopes'] else '—'
            lines.append('| %s | %s | %s |' % (name, before, after))
        lines.append('')
    lines += ['## Verification and limits', '',
              '- Master revision: ' + baseline['provenance']['revision'] + '; branch revision: ' + current['provenance']['revision'] + '.',
              '- Build: ' + current['provenance']['build'] + '.',
              '- Peak owned-process RSS: master %.0f MiB; branch %.0f MiB.' % (baseline['peak_rss_mib'], current['peak_rss_mib']),
              '- Fixture: ' + json.dumps(current['provenance']['fixture'], sort_keys=True) + '.',
              '- Selection identities and ordering match between revisions at every checkpoint.',
              '- Every action has independent external MCP stage/selection evidence; transforms have sampled matrix and undo/redo checks.',
              '- Exact root restoration after final undo: master %s; branch %s.' % (
                  baseline['phases']['cleanup']['measurement']['root_restored'], current['phases']['cleanup']['measurement']['root_restored']),
              '- One fresh process per revision. Warm sweep repeats share a process; they are not independent cold-load samples.',
              '- OS caches are retained. Fixture uses repeated topology, authored extents, no materials and no instancing; it does not model a textured production assembly.',
              '- Authored quads are counted as faces; 20 million quads can produce 40 million raster triangles.']
    output = Path(output)
    output.write_text('\n'.join(lines) + '\n')
    with output.with_suffix('.csv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=csv_rows[0])
        writer.writeheader()
        writer.writerows(csv_rows)
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline')
    parser.add_argument('current')
    parser.add_argument('output')
    args = parser.parse_args()
    compare(args.baseline, args.current, args.output)
