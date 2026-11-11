# SPDX-License-Identifier: BSD-3-Clause
"""Own a disposable Stageviz process and independently validate benchmark checkpoints."""
import argparse
import hashlib
import json
import os
import platform
import re
from pathlib import Path
import subprocess
import time
import urllib.request
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mcp_runtime import macos_bootstrap


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    fixture = Path(args.fixture).resolve()
    app = Path(args.app).resolve()
    manifest = json.loads(fixture.with_suffix('.json').read_text())
    count = manifest['meshes']
    parent_index = (manifest['groups'] // 2)
    parent = '/World/Batch_%03d' % parent_index
    config = dict(output=str(output), fixture=str(fixture), meshes=count,
                  lastMesh='/World/Batch_%03d/Mesh_%06d' % ((count - 1) // 1000, count - 1),
                  parent=parent, parentChild=parent + '/Mesh_%06d' % (parent_index * 1000),
                  sweepRect=[.4, .4, .6, .6], settleSeconds=.75, timeoutSeconds=1200,
                  sceneStats=True, activeSweepRepeats=args.active_sweep_repeats)
    if args.viewport_size:
        config['viewportSize'] = args.viewport_size
    if args.sweep_only:
        config['phases'] = ['load', 'frame', 'cold_sweep']
        for i in range(1, 4):
            config['phases'] += ['clear_%d' % i, 'warm_sweep_%d' % i]
        config['phases'].append('show_gizmo')
        for i in range(1, args.active_sweep_repeats + 1):
            config['phases'] += ['clear_active_%d' % i, 'warm_sweep_active_%d' % i]
        config['phases'].append('cleanup')
    elif args.parent_repeats:
        config['phases'] = ['load', 'frame']
        for i in range(1, args.parent_repeats + 1):
            config['phases'] += ['parent_select_%d' % i, 'parent_%d_press' % i,
                                 'parent_%d_drag_1' % i, 'parent_%d_drag_2' % i,
                                 'parent_%d_release' % i, 'parent_%d_undo' % i,
                                 'parent_%d_redo' % i, 'parent_%d_restore' % i]
        config['phases'].append('cleanup')
    (output / 'profile.json').write_text(json.dumps(config, indent=2))
    script = Path(__file__).with_name('embedded.py').resolve()
    (output / 'input-controller.py').write_bytes(Path(__file__).read_bytes())
    (output / 'input-embedded.py').write_bytes(script.read_bytes())
    provenance = dict(revision=args.revision, label=args.label, app=str(app), app_sha256=hashlib.sha256(app.read_bytes()).hexdigest(),
                      fixture=manifest, controller_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      embedded_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),
                      started=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                      activity='NSActivityUserInitiatedAllowingIdleSystemSleep; viewer stays on top',
                      build=args.build + '; DYLD_IMAGE_SUFFIX=_debug; sequential disposable processes',
                      measurement='first present, session-ready, three-present response, and an additional 750ms settle floor; checkpoints outside spans')
    provenance.update(host=platform.platform(), machine=platform.machine(), logical_cpus=os.cpu_count())
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2))
    # Use an independent listener, preserving the user's running app and undo history.
    bootstrap = macos_bootstrap(args.port)
    (output / 'mcp-start.py').write_text(bootstrap)

    def rpc(method, params, timeout=180):
        request = urllib.request.Request('http://127.0.0.1:%d/mcp' % args.port,
            json.dumps(dict(jsonrpc='2.0', id=1, method=method, params=params)).encode(),
            {'Content-Type': 'application/json', 'Host': '127.0.0.1:%d' % args.port})
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.load(response)

    def tool(name, arguments=None):
        result = rpc('tools/call', dict(name=name, arguments=arguments or {}))
        assert 'error' not in result, result
        assert not result['result'].get('isError'), result
        return result

    env = os.environ.copy()
    env['STAGEVIZ_TEST_SETTINGS_DIR'] = str(output / 'settings')
    env.update(DYLD_IMAGE_SUFFIX='_debug', STAGEVIZ_PERF_LOG=str(output / 'native-phases.jsonl'),
               STAGEVIZ_SCALE_PROFILE=str(output / 'profile.json'), HD_ENABLE_GPU_FRUSTUM_CULLING='0')
    # Verify the dedicated port is unused before starting anything.
    try:
        rpc('tools/list', {}, timeout=1)
    except Exception:
        pass
    else:
        raise RuntimeError('Dedicated MCP port is already occupied')
    with (output / 'app.log').open('w') as log:
        process = subprocess.Popen([str(app), '--execute', 'exec(open(%r).read())' % str(output / 'mcp-start.py')],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        provenance['pid'] = process.pid
        (output / 'provenance.json').write_text(json.dumps(provenance, indent=2))
        try:
            deadline = time.monotonic() + 90
            while True:
                try:
                    tools = rpc('tools/list', {}, timeout=2)
                    break
                except Exception:
                    assert process.poll() is None and time.monotonic() < deadline, 'App failed during startup (exit %s)' % process.poll()
                    time.sleep(.25)
            (output / 'tools.json').write_text(json.dumps(tools, indent=2))
            before = {name: tool(name) for name in ('stageviz_get_stage_info', 'stageviz_get_selection')}
            (output / 'before-oracle.json').write_text(json.dumps(before, indent=2))
            code = "import runpy; runpy.run_path(%r); print('STAGEVIZ_SCALE_STARTED')" % str(script)
            result = tool('stageviz_run_python', {'code': code})
            (output / 'bootstrap.json').write_text(json.dumps(result, indent=2))
            # Embedded Python failures can be returned as text without isError.
            assert any('STAGEVIZ_SCALE_STARTED' in block.get('text', '')
                       for block in result['result']['content']), result
            last_resource = last_progress = last_pressure = 0
            previous = None
            acknowledged = set()
            deadline = time.monotonic() + 7200
            while True:
                now = time.monotonic()
                assert process.poll() is None, 'App exited: %s' % process.returncode
                assert now < deadline, 'External watchdog expired'
                if now - last_resource >= 1:
                    raw = subprocess.check_output(['ps', '-o', 'rss=,pcpu=', '-p', str(process.pid)], text=True).split()
                    rss = int(raw[0]) / 1024
                    resource = dict(wall_ms=time.time() * 1000, rss_mib=rss, cpu_percent=float(raw[1]))
                    if now - last_pressure >= 10:
                        swap = subprocess.check_output(['sysctl', '-n', 'vm.swapusage'], text=True).strip()
                        match = re.search(r'used = ([0-9.]+)([MGK])', swap)
                        resource['swap_usage'] = swap
                        if match:
                            resource['system_swap_used_mib'] = float(match[1]) * {'M': 1, 'G': 1024, 'K': 1 / 1024}[match[2]]
                        resource['vm_stat'] = subprocess.check_output(['vm_stat'], text=True)
                        last_pressure = now
                    with (output / 'resources.jsonl').open('a') as stream:
                        stream.write(json.dumps(resource) + '\n')
                    assert rss < args.max_rss_mib, 'Owned benchmark exceeded its RSS budget: %.0f MiB' % rss
                    last_resource = now
                try:
                    state = json.loads((output / 'status.json').read_text())
                except (FileNotFoundError, json.JSONDecodeError):
                    time.sleep(.25)
                    continue
                key = (state.get('state'), state.get('phase'))
                if key != previous or now - last_progress > 30:
                    print(args.label, *key, 'RSS %.0f MiB' % rss, flush=True)
                    previous = key
                    last_progress = now
                if state['state'] == 'failed':
                    raise RuntimeError(state['error'])
                if state['state'] == 'complete':
                    break
                if state['state'] == 'checkpoint' and state['phase'] not in acknowledged:
                    phase = state['phase']
                    oracle = {name: tool(name) for name in ('stageviz_get_stage_info', 'stageviz_get_selection')}
                    stage_info = json.loads(oracle['stageviz_get_stage_info']['result']['content'][0]['text'])
                    assert stage_info['loaded'] and stage_info['filename'] == ''
                    assert stage_info['rootLayer'].startswith('anon:') and stage_info['defaultPrim'] == '/World'
                    assert stage_info['layerCount'] == 3
                    selected = json.loads(oracle['stageviz_get_selection']['result']['content'][0]['text'])
                    row = state['result']
                    assert len(selected) == row['selection_count'], phase
                    assert hashlib.sha256('\n'.join(selected).encode()).hexdigest() == row['selection_sha256'], phase
                    if row['selected_sample']:
                        oracle['prim'] = tool('stageviz_get_prim_info', {'path': row['selected_sample'][0]})
                        prim_info = json.loads(oracle['prim']['result']['content'][0]['text'])
                        assert prim_info['path'] == row['selected_sample'][0] and prim_info['active'] and prim_info['loaded']
                        assert not prim_info['instance'] and not prim_info['instanceProxy']
                    (output / (phase + '-oracle.json')).write_text(json.dumps(oracle, indent=2))
                    print('RESULT', args.label, phase, 'dispatch %.1fms response %.1fms settled %.1fms selected %d' % (
                        row['dispatch_ms'], row['responsive_ms'], row['end_to_end_ms'], row['selection_count']), flush=True)
                    (output / 'ack.txt').write_text(phase)
                    acknowledged.add(phase)
                time.sleep(.25)
            # Verify the fixture itself remains byte-identical after all authored previews.
            assert hashlib.sha256(fixture.read_bytes()).hexdigest() == manifest['sha256']
            (output / 'passed.json').write_text(json.dumps(dict(passed=True, checkpoints=len(acknowledged), source_unchanged=True), indent=2))
            print('PASS', args.label, len(acknowledged), 'checkpoints', flush=True)
        except BaseException as error:
            (output / 'failure.json').write_text(json.dumps(dict(type=type(error).__name__, message=str(error)), indent=2))
            raise
        finally:
            # This process owns only generated fixtures and never saves them.
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--sweep-only', action='store_true')
    parser.add_argument('--viewport-size', type=int, nargs=2, metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--active-sweep-repeats', type=int, default=0)
    parser.add_argument('--parent-repeats', type=int, default=0)
    parser.add_argument('--port', type=int, default=37657)
    parser.add_argument('--revision', default=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip())
    parser.add_argument('--build', default='Debug')
    parser.add_argument('--max-rss-mib', type=int, default=24576)
    run(parser.parse_args())
