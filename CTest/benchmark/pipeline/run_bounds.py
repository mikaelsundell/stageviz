# SPDX-License-Identifier: BSD-3-Clause
"""Measure cold/warm selection bounds against fresh USD reference bounds."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mcp_runtime import DisposableStageviz


def run(args):
    fixture = Path(args.fixture).resolve()
    output = Path(args.output).resolve()
    digest = hashlib.sha256(fixture.read_bytes()).hexdigest()
    config = dict(fixture=str(fixture), root=args.root, branch=args.branch,
                  repeats=args.repeats, output=str(output))
    script = Path(__file__).with_name('bounds_workflow.py')
    try:
        with DisposableStageviz(args.app, output) as app:
            (output/'config.json').write_text(json.dumps(config, indent=2))
            (output/'input-embedded.py').write_bytes(script.read_bytes())
            (output/'fixture.json').write_text(json.dumps(dict(file=str(fixture), sha256=digest), indent=2))
            (output/'before-oracle.json').write_text(json.dumps(app.oracle(), indent=2))
            app.python("import stageviz; stageviz.session().setPreserveState(False); "
                       "assert stageviz.session().loadFromFile(%r)" % str(fixture))
            (output/'loaded-oracle.json').write_text(json.dumps(app.oracle(), indent=2))
            app.python("import runpy; runpy.run_path(%r, init_globals={'CONFIG':%r})" % (str(script), config))
            (output/'after-oracle.json').write_text(json.dumps(app.oracle(), indent=2))
            app.python("assert stageviz.session().reload(); stageviz.command.clear(); "
                       "assert not stageviz.session().stage().GetRootLayer().dirty")
            (output/'cleanup-oracle.json').write_text(json.dumps(app.oracle(), indent=2))
            assert hashlib.sha256(fixture.read_bytes()).hexdigest() == digest
            (output/'passed.json').write_text(json.dumps(dict(passed=True, source_unchanged=True)))
    finally:
        assert hashlib.sha256(fixture.read_bytes()).hexdigest() == digest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('--branch', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--repeats', type=int, default=5)
    run(parser.parse_args())
