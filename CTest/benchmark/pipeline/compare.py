# SPDX-License-Identifier: BSD-3-Clause
"""Compare complete pipeline runs only after their observable results match."""
import argparse
import json
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'sweep_scale'))
from analyze import load


def compare(baseline_dir, candidate_dir, output):
    baseline, candidate = [load(directory) for directory in (baseline_dir, candidate_dir)]
    assert baseline['completed'] and candidate['completed'], 'Both runs must pass'
    assert list(baseline['phases']) == list(candidate['phases']), 'Phase lists differ'
    fixture_files = [Path(directory) / 'fixture.json' for directory in (baseline_dir, candidate_dir)]
    if all(path.exists() for path in fixture_files):
        assert len({json.loads(path.read_text())['sha256'] for path in fixture_files}) == 1
    else:
        assert baseline['provenance']['fixture']['sha256'] == candidate['provenance']['fixture']['sha256']

    observables = ('action', 'selection_count', 'selection_sha256', 'selected_sample',
                   'widget_size', 'device_pixel_ratio', 'property_sections', 'property_sections_expanded', 'property_rows', 'action_state',
                   'root_index_survived', 'source_dirty', 'target_children', 'root_restored', 'view_state')
    rows = []
    for phase in baseline['phases']:
        before, after = [run['phases'][phase]['measurement'] for run in (baseline, candidate)]
        for key in observables:
            assert before.get(key) == after.get(key), (phase, key, before.get(key), after.get(key))
        # Raster-edge differences can change visible counts; each capture's paths
        # have already been checked against the composed stage by the workflow.
        rows.append(dict(phase=phase,
                         baseline_dispatch_ms=before['dispatch_ms'], candidate_dispatch_ms=after['dispatch_ms'],
                         baseline_response_ms=before['responsive_ms'], candidate_response_ms=after['responsive_ms'],
                         response_change_percent=(after['responsive_ms'] / before['responsive_ms'] - 1) * 100))
    lines = ['# Pipeline comparison', '',
             'Sequential disposable processes, identical source file and observable checkpoints. '
             'Negative changes are faster. These are individual samples unless grouped below.', '',
             '| Phase | Baseline dispatch ms | Candidate dispatch ms | Baseline response ms | Candidate response ms | Change |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    executable_hash=baseline['provenance'].get('app_sha256')
    if executable_hash and executable_hash==candidate['provenance'].get('app_sha256'):
        lines[2]+=' Same-executable control: the columns identify first and second processes of the same binary.'
    for row in rows:
        lines.append('| {phase} | {baseline_dispatch_ms:.1f} | {candidate_dispatch_ms:.1f} | '
                     '{baseline_response_ms:.1f} | {candidate_response_ms:.1f} | {response_change_percent:+.1f}% |'.format(**row))
    for prefix in ('warm_sweep_', 'warm_sweep_active_', 'wire_', 'shaded_', 'materials_on_', 'materials_off_'):
        group = [row for row in rows if row['phase'].startswith(prefix) and row['phase'][len(prefix):].isdigit()]
        if group:
            before = statistics.median(row['baseline_response_ms'] for row in group)
            after = statistics.median(row['candidate_response_ms'] for row in group)
            lines.extend(['', '%s median response: %.1f → %.1f ms (%+.1f%%), %d repeats.' %
                          (prefix.rstrip('_'), before, after, (after / before - 1) * 100, len(group))])
    lines.extend(['', 'Response waits for Session completion and three viewport presentations. '
                  'End-to-end measurements additionally include a fixed event-loop settle period. '
                  'Screenshots, assertions and external MCP queries are outside these spans.', '',
                  'Native scope timings are inclusive and must not be added together. '
                  'One sequential pair cannot establish small changes or cold-start improvements.', '',
                  'Peak owned-process RSS: baseline %.0f MiB; candidate %.0f MiB.' %
                  (baseline['peak_rss_mib'], candidate['peak_rss_mib'])])
    output = Path(output)
    output.write_text('\n'.join(lines) + '\n')
    output.with_suffix('.json').write_text(json.dumps(rows, indent=2))
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline')
    parser.add_argument('candidate')
    parser.add_argument('output')
    args = parser.parse_args()
    compare(args.baseline, args.candidate, args.output)
