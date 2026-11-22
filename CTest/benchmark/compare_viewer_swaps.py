# SPDX-License-Identifier: BSD-3-Clause
"""Compare serial viewport swaps with the same USD input on Windows."""

import argparse
import hashlib
from itertools import islice
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def measure(widget, update, frames, qt, rendered_signal, ready=lambda: True):
    """Submit one change at a time and wait for its fresh frame to swap."""
    loop = qt.QEventLoop()
    timeout = qt.QTimer()
    timeout.setSingleShot(True)
    timeout.timeout.connect(loop.quit)
    paint_end = None
    presentation = None

    def renderer_ready(*_):
        nonlocal paint_end
        paint_end = time.perf_counter_ns()

    def frame_swapped():
        nonlocal presentation, paint_end
        if paint_end is not None:
            if presentation is None and ready():
                presentation = paint_end, time.perf_counter_ns()
                loop.quit()
            paint_end = None

    widget.frameSwapped.connect(frame_swapped)
    rendered_signal.connect(renderer_ready)
    samples, before_swap, swaps = [], [], []
    try:
        for index in range(-8, frames):
            presentation = None
            paint_end = None
            start = time.perf_counter_ns()
            update(index)
            if presentation is None:
                widget.update()
                timeout.start(10000)
                loop.exec()
                timeout.stop()
            if presentation is None:
                raise RuntimeError(f'No fresh viewport swap for sample {index}')
            if index >= 0:
                paint_ns, swap_ns = presentation
                samples.append((swap_ns - start) / 1e6)
                before_swap.append((swap_ns - paint_ns) / 1e6)
                swaps.append(swap_ns)
            elif index == -1:
                wall_start = time.time_ns() // 1_000_000
        return dict(input_to_swap_ms=samples, paint_end_to_swap_ms=before_swap, frame_intervals_ms=[
            (end - start) / 1e6 for start, end in zip(swaps, swaps[1:])
        ], wall_start_ms=wall_start, wall_end_ms=time.time_ns() // 1_000_000)
    finally:
        widget.frameSwapped.disconnect(frame_swapped)
        rendered_signal.disconnect(renderer_ready)


def viewport_size(widget):
    ratio = widget.devicePixelRatioF()
    return [round(widget.width() * ratio), round(widget.height() * ratio)]


def graphics_profile(widget, qt):
    profile = dict(qt=qt.qVersion(), python=sys.version.split()[0],
                   msaa_samples=widget.format().samples())
    try:
        from OpenGL import GL
    except ImportError:
        return profile
    widget.makeCurrent()
    try:
        for name, token in (('renderer', GL.GL_RENDERER), ('version', GL.GL_VERSION)):
            value = GL.glGetString(token)
            if value:
                profile[f'opengl_{name}'] = value.decode('ascii', errors='replace')
    finally:
        widget.doneCurrent()
    return profile


def camera_setup(stage):
    from pxr import Usd, UsdGeom

    bounds = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_,
                                                       UsdGeom.Tokens.proxy]).ComputeWorldBound(
        stage.GetPseudoRoot()).ComputeAlignedRange()
    if bounds.IsEmpty():
        raise ValueError('The stage has no visible geometry bounds')
    diameter = max(bounds.GetSize())
    distance = max(1e-3, diameter * 1.8)
    return dict(center=list(bounds.GetMidpoint()), distance=distance,
                near=distance / 1000, far=distance * 100,
                z_up=UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.z)


def stageviz_worker(config, output):
    import stageviz
    from pxr import Usd, UsdGeom
    from PySide6 import QtCore, QtOpenGLWidgets, QtWidgets

    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python'))
    import testbench as tb

    session = stageviz.session()
    if not session.load(config['scene']) or not tb.wait_for_commands():
        raise RuntimeError('Stageviz could not load the benchmark stage')
    stage = session.stage()
    root_text = stage.GetRootLayer().ExportToString()
    camera = camera_setup(stage)
    state = session.viewState()
    view_camera = state.camera()
    view_camera.setCameraUp(stageviz.CameraUpZ if camera['z_up'] else stageviz.CameraUpY)
    view_camera.setCameraView(stageviz.CameraViewPerspective)
    view_camera.setAspectRatioLocked(False)
    view_camera.setFovDirection(stageviz.FovVertical)
    view_camera.setFov(45)
    view_camera.setFocusPoint(*camera['center'])
    view_camera.setCameraDistance(camera['distance'])
    view_camera.setNearClipping(camera['near'])
    view_camera.setFarClipping(camera['far'])
    view_camera.setAxisYaw(0)
    view_camera.setAxisPitch(20)
    state.setGridEnabled(False)
    state.setCameraAxisEnabled(False)
    state.setAmbientOcclusionEnabled(False)
    state.setSceneStatsEnabled(False)
    state.setPerformanceStatsEnabled(False)
    state.setSceneMaterialsEnabled(True)
    state.setSceneLightsEnabled(False)
    state.setDefaultCameraLightEnabled(True)
    state.setDefaultDomeLightEnabled(False)
    state.setRenderMode(stageviz.RenderShaded)
    state.setComplexityLevel(stageviz.ComplexityLow)
    viewer = tb.viewer()
    viewer.show()
    widget = next(w for w in tb.all_widgets() if w.objectName() == 'imagingGLWidget' and w.isVisible())
    viewer.resize(config['size'][0] + viewer.width() - widget.width(),
                  config['size'][1] + viewer.height() - widget.height())
    geometry = list(islice((str(prim.GetPath()) for prim in stage.Traverse() if prim.IsA(UsdGeom.Gprim)), 2))
    selections = config['selection'] or geometry
    if not geometry or any(not stage.GetPrimAtPath(path) for path in selections):
        raise ValueError('The scene must contain geometry and any --select-prim paths must exist')
    tree = viewer.findChild(QtWidgets.QTreeWidget, 'outlinerTree')
    if tree:
        index = QtCore.QModelIndex()
        for _ in range(4):
            index = tree.model().index(0, 0, index)
            if not index.isValid():
                break
            tree.expand(index)
    stageviz.command.select_paths([])
    assert tb.wait_for_commands()
    tb.process_events(400)

    def orbit(index):
        view_camera.setAxisRoll(index * 1.5)

    expected_selection = []

    def orbit_and_select(index):
        nonlocal expected_selection
        orbit(index)
        expected_selection = ([selections[index % len(selections)]]
                              if index % 2 == 0 or len(selections) > 1 else [])
        stageviz.command.select_paths(expected_selection)

    def selection_ready():
        return session.primsUpdate() == stageviz.PrimsImmediate and list(session.paths()) == expected_selection

    frames = config['frames']
    orbit_samples = measure(widget, orbit, frames, QtCore, widget.renderReady)
    orbit_camera = stageviz.render().inspectPrim(geometry[0])['camera']
    assert widget.grab().save(str(output / 'orbit-viewport.png'))
    result = dict(camera=camera, size=viewport_size(widget), logical_size=[widget.width(), widget.height()],
                  usd_version=list(Usd.GetVersion()),
                  orbit=orbit_samples, orbit_camera=orbit_camera,
                  orbit_selection=measure(widget, orbit_and_select, frames, QtCore, widget.renderReady,
                                          selection_ready))
    assert stage.GetRootLayer().ExportToString() == root_text
    assert not any(layer.dirty for layer in stage.GetUsedLayers() if not layer.anonymous)
    result['graphics'] = graphics_profile(widget, QtCore)
    assert viewer.grab().save(str(output / 'viewer.png'))
    write_json(output / 'qt.json', tb.snapshot_qt_state('After orbit and selection', root=viewer))
    write_json(output / 'samples.json', result)


def usdview_worker(config_path):
    from pxr import Gf, Usd
    from pxr.Usdviewq.freeCamera import FreeCamera
    from pxr.Usdviewq.qt import QtCore, QtWidgets
    from pxr.Usdviewq.stageView import StageView

    class MeasuredStageView(StageView):
        rendered = QtCore.Signal()

        def paintGL(self):
            super().paintGL()
            self.rendered.emit()

    config = read_json(config_path)
    app = QtWidgets.QApplication([])
    model = StageView.DefaultDataModel()
    model.stage = Usd.Stage.Open(config['scene'])
    if not model.stage:
        raise RuntimeError('usdview StageView could not open the benchmark stage')
    settings = model.viewSettings
    settings.showHUD = False
    settings.showBBoxes = False
    settings.enableSceneLights = False
    settings.enableSceneMaterials = True
    settings.domeLightEnabled = False
    widget = MeasuredStageView(dataModel=model)
    camera = config['camera']
    free = FreeCamera(camera['z_up'], fov=45, overrideNear=camera['near'], overrideFar=camera['far'])
    free.center = Gf.Vec3d(*camera['center'])
    free.dist = camera['distance']
    free.rotPhi = 20
    settings.freeCamera = free
    widget.setFixedSize(*config['size'])
    widget.show()
    widget.updateView(forceComputeBBox=True)
    app.processEvents()
    # StageView in OpenUSD 25.11 uses a QOpenGLWidget-compatible Qt wrapper.
    if not widget.isValid() or not hasattr(widget, 'frameSwapped'):
        raise RuntimeError('usdview StageView did not provide an onscreen OpenGL swap signal')
    if widget.GetCurrentRendererId() != 'HdStormRendererPlugin':
        if not widget.SetRendererPlugin('HdStormRendererPlugin') or widget.GetCurrentRendererId() != 'HdStormRendererPlugin':
            raise RuntimeError('usdview StageView could not select Storm')
    result = dict(size=viewport_size(widget), usd_version=list(Usd.GetVersion()), orbit=measure(
        widget, lambda index: setattr(free, 'rotTheta', index * 1.5), config['frames'], QtCore, widget.rendered))
    frustum = widget.resolveCamera()[0].frustum
    result['orbit_camera'] = dict(view=[list(row) for row in frustum.ComputeViewMatrix()],
                                 projection=[list(row) for row in frustum.ComputeProjectionMatrix()])
    result['graphics'] = graphics_profile(widget, QtCore)
    assert widget.grab().save(str(config_path.parent / 'viewer.png'))
    write_json(config_path.parent / 'samples.json', result)
    widget.close()


def summarize(samples):
    output = {}
    for label, values in samples.items():
        if isinstance(values, list) and values:
            ordered = sorted(values)
            output[label] = dict(median=statistics.median(ordered),
                                 p95=ordered[(95 * len(ordered) + 99) // 100 - 1],
                                 maximum=ordered[-1])
    return output


def native_summary(path, measured):
    by_phase = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        event = json.loads(line)
        if measured['wall_start_ms'] <= event['wall_start_ms'] < measured['wall_end_ms']:
            by_phase.setdefault(event['phase'], []).append(event['duration_ms'])
    return {name: dict(calls=len(values), median_ms=statistics.median(values), total_ms=sum(values))
            for name, values in sorted(by_phase.items())}


def run(args):
    from mcp_runtime import DisposableStageviz, test_desktop

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    scene = args.scene
    if args.synthetic_cubes is not None:
        from benchmark_gizmo_rendering import write_fixture

        scene = output / 'generated.usda'
        write_fixture(scene, args.synthetic_cubes)
        scene.write_bytes(scene.read_bytes().replace(b'\r\n', b'\n'))
    config = dict(scene=str(scene.resolve(strict=True)), frames=args.frames,
                  size=args.size, selection=args.select_prim)
    original = hashlib.sha256(scene.read_bytes()).hexdigest()
    script = Path(__file__).resolve()
    runtime = args.stageviz_runtime.read_text(encoding='utf-8').splitlines()
    path = os.environ.get('PATH', '')
    env = {'PATH': os.pathsep.join([p for p in runtime if p] + [path]), 'STAGEVIZ_PERF_LOG': ''}

    def stageviz_run(label, trace=False):
        run_dir = output / label
        trace_path = run_dir / 'native.jsonl'
        run_env = dict(env, STAGEVIZ_PERF_LOG=str(trace_path) if trace else '')
        with DisposableStageviz(args.stageviz, run_dir, port=0, configuration='Release',
                                environment=run_env) as app:
            before = app.oracle()
            app.python(f'import runpy; from pathlib import Path\n'
                       f'runpy.run_path({str(script)!r})["stageviz_worker"]({config!r}, Path({str(run_dir)!r}))',
                       timeout=300)
            write_json(run_dir / 'oracles.json', {'before': before, 'after': app.oracle()})
        return read_json(run_dir / 'samples.json')

    with test_desktop():
        stageviz = stageviz_run('stageviz')
        usd_dir = output / 'usdview'
        usd_dir.mkdir()
        usd_config = usd_dir / 'config.json'
        write_json(usd_config, dict(config, camera=stageviz['camera'], size=stageviz['logical_size']))
        prefix = args.usd_prefix.resolve(strict=True)
        usd_env = dict(os.environ, PATH=os.pathsep.join(map(str, [
            args.usd_python.resolve().parent, prefix / 'bin', prefix / 'lib', path])),
                       PYTHONPATH=os.pathsep.join(map(str, [
                           prefix / 'lib/python', prefix / 'site-packages', prefix / 'python'])),
                       QT_PLUGIN_PATH=str(prefix / 'plugins'))
        with (usd_dir / 'app.log').open('w', encoding='utf-8') as log:
            subprocess.run([str(args.usd_python.resolve(strict=True)), str(script), '--usd-worker',
                            str(usd_config)], env=usd_env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=300, check=True)
        usdview = read_json(usd_dir / 'samples.json')
        if usdview['size'] != stageviz['size']:
            raise RuntimeError(f'Physical viewport size mismatch: {usdview["size"]} vs {stageviz["size"]}')
        if usdview['usd_version'] != stageviz['usd_version']:
            raise RuntimeError(f'OpenUSD version mismatch: {usdview["usd_version"]} vs {stageviz["usd_version"]}')
        for key in ('view', 'projection'):
            left, right = ([value for row in report['orbit_camera'][key] for value in row]
                           for report in (stageviz, usdview))
            # Both viewers frame the same image, but Stageviz adapts depth
            # clipping to viewport bounds while usdview keeps the override.
            indices = set(range(16)) - ({10, 14} if key == 'projection' else set())
            if not all(math.isclose(left[i], right[i], rel_tol=1e-4, abs_tol=1e-4) for i in indices):
                raise RuntimeError(f'The final camera {key} framing differs between viewers')
        traced = stageviz_run('stageviz-traced', trace=True)

    if hashlib.sha256(scene.read_bytes()).hexdigest() != original:
        raise RuntimeError('The input USD file changed during the comparison')
    trace_path = output / 'stageviz-traced/native.jsonl'
    summary = dict(viewport=stageviz['size'], frames=args.frames, usd_version=stageviz['usd_version'],
                   graphics={'stageviz': stageviz['graphics'], 'usdview': usdview['graphics']},
                   stageviz_orbit=summarize(stageviz['orbit']),
                   usdview_orbit=summarize(usdview['orbit']),
                   stageviz_orbit_selection=summarize(stageviz['orbit_selection']),
                   stageviz_native={phase: native_summary(trace_path, traced[phase])
                                    for phase in ('orbit', 'orbit_selection')})
    if args.synthetic_cubes is not None:
        summary['synthetic_fixture'] = dict(cubes=args.synthetic_cubes, sha256=original)
    write_json(output / 'summary.json', summary)
    print(output / 'summary.json')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--usd-worker':
        usdview_worker(Path(sys.argv[2]))
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--stageviz', type=Path, required=True)
        parser.add_argument('--stageviz-runtime', type=Path, required=True, help='Generated Release test runtime manifest')
        parser.add_argument('--usd-python', type=Path, required=True, help='Interpreter matching the Release OpenUSD build')
        parser.add_argument('--usd-prefix', type=Path, required=True)
        fixture = parser.add_mutually_exclusive_group(required=True)
        fixture.add_argument('--scene', type=Path)
        fixture.add_argument('--synthetic-cubes', type=int, help='Generate a reproducible local fixture')
        parser.add_argument('--output', type=Path, required=True, help='New ignored local directory')
        parser.add_argument('--frames', type=int, default=90)
        parser.add_argument('--size', type=int, nargs=2, default=[800, 600])
        parser.add_argument('--select-prim', action='append', default=[])
        args = parser.parse_args()
        if args.frames < 2 or min(args.size) < 1:
            parser.error('Use at least two frames and positive viewport dimensions')
        if args.synthetic_cubes is not None and args.synthetic_cubes < 1:
            parser.error('--synthetic-cubes must be positive')
        run(args)
