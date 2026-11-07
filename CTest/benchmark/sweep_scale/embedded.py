# SPDX-License-Identifier: BSD-3-Clause
"""Asynchronous real-input benchmark; external controller owns MCP and acknowledgments."""
import hashlib
import json
from pathlib import Path
import runpy
import time
import traceback
import stageviz
from pxr import Gf, Sdf, UsdGeom
from PySide6 import QtCore, QtWidgets, QtOpenGLWidgets
import shiboken6

support = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'benchmark_selection_transform.py'),
                        init_globals={'_SELECTION_AUTORUN': False})
Base = support['SelectionBenchmark']


class ScaleBenchmark(Base):
    def __init__(self, config):
        self.config = config
        viewer = support['support']['tb'].viewer()
        self.viewer = viewer
        self._viewport = None
        self._frame_hook_ready = False
        viewer.resize(1500, 1000)
        # Presentation-based timing requires an exposed disposable viewer.
        viewer.setWindowFlag(QtCore.Qt.WindowType.WindowStaysOnTopHint, True)
        viewer.show()
        viewer.raise_()
        viewer.activateWindow()
        # Saved splitter settings must not change the sweep's pixel footprint.
        splitter = viewer.findChild(QtWidgets.QSplitter, 'splitter')
        assert splitter is not None
        splitter.setSizes([240, max(400, splitter.width() - 240), 0])
        self.directory = Path(config['output'])
        self.results = []
        self.phases = ['load', 'frame', 'cold_sweep']
        for i in range(1, 4):
            self.phases.extend(['clear_%d' % i, 'warm_sweep_%d' % i])
        self.phases += ['show_gizmo']
        for i in range(1, config.get('activeSweepRepeats', 0) + 1):
            self.phases.extend(['clear_active_%d' % i, 'warm_sweep_active_%d' % i])
        self.phases += ['bulk_press'] + ['bulk_drag_%d' % i for i in range(1, 6)]
        self.phases += ['bulk_release', 'bulk_undo', 'bulk_redo', 'bulk_restore',
                        'parent_select', 'parent_press', 'parent_drag_1', 'parent_drag_2',
                        'parent_release', 'parent_undo', 'parent_redo', 'parent_restore', 'cleanup']
        self.phases = config.get('phases', self.phases)
        self.active = False
        self.waiting = None
        self.current = None
        self.initial_selection = None
        self.saved_sweep = None
        self.samples = {}
        self.root_before = None
        self.frames = []
        self.last_sample = None
        self.viewport().frameSwapped.connect(self.on_frame)
        self._frame_hook_ready = True
        self.timer = QtCore.QTimer()
        self.timer.setInterval(25)
        self.timer.timeout.connect(self.poll)
        self.timer.start()
        self.status('ready')

    def viewport(self):
        current = self._viewport
        if current is not None and shiboken6.isValid(current) and current.isVisible():
            return current
        if not shiboken6.isValid(self.viewer):
            raise RuntimeError('Disposable viewer was destroyed')
        # Retain this QObject wrapper and scope discovery to the owned viewer.
        # Global widget wrapping is both expensive and unrelated to input latency.
        for widget in self.viewer.findChildren(QtOpenGLWidgets.QOpenGLWidget, 'imagingGLWidget'):
            if not widget.isVisible():
                continue
            if self._frame_hook_ready:
                if current is not None and shiboken6.isValid(current):
                    try:
                        current.frameSwapped.disconnect(self.on_frame)
                    except (RuntimeError, TypeError):
                        pass
                widget.frameSwapped.connect(self.on_frame)
            self._viewport = widget
            return widget
        raise RuntimeError('Visible viewport missing in the disposable viewer')

    def view_state(self):
        """Record presentation inputs outside the measured input span."""
        view = stageviz.session().viewState()
        camera = view.camera()
        camera_keys = ('projectionMode', 'aspectRatio', 'aspectRatioLocked',
                       'letterboxEnabled', 'fov', 'fovDirection', 'focalLength',
                       'sensorWidth', 'sensorHeight', 'focusPoint', 'boundingBox',
                       'fit', 'cameraUp', 'axisYaw', 'axisPitch', 'axisRoll',
                       'cameraMode', 'nearClipping', 'farClipping', 'cameraDistance')
        render_keys = ('renderMode', 'complexityLevel', 'sceneMaterialsEnabled',
                       'sceneLightsEnabled', 'doubleSidedMode', 'defaultCameraLightEnabled',
                       'defaultDomeLightEnabled', 'domeLightTexture', 'domeLightCameraVisibility',
                       'rendererAov', 'ambientOcclusionEnabled', 'sceneStatsEnabled',
                       'performanceStatsEnabled', 'cameraAxisEnabled')
        return dict(camera={key: getattr(camera, key)() for key in camera_keys},
                    render={key: getattr(view, key)() for key in render_keys})

    def status(self, state, **extra):
        # Publish atomically only after the complete subclass checkpoint exists.
        target = self.directory / 'status.json'
        pending = target.with_suffix('.pending')
        pending.write_text(json.dumps(dict(state=state, phase=self.current,
                                           completed=len(self.results), **extra), indent=2))
        pending.replace(target)

    def on_frame(self):
        if self.active and self.dispatched:
            self.frames.append(time.perf_counter() * 1000)

    def start(self):
        self.current = self.phases[len(self.results)]
        self.frames = []
        self.ready_at = None
        self.responsive_at = None
        self.active = True
        self.dispatched = False
        self.start_ms = time.perf_counter() * 1000
        self.start_wall = time.time() * 1000
        self.status('running', start_wall_ms=self.start_wall)
        phase = self.current
        session = stageviz.session()
        widget = self.viewport()
        if phase == 'load':
            assert session.newStage()
            view = session.viewState()
            view.setSceneMaterialsEnabled(False)
            view.setSceneStatsEnabled(self.config.get('sceneStats', True))
            view.setDefaultCameraLightEnabled(True)
            view.setDefaultDomeLightEnabled(False)
            view.setDoubleSidedMode(1)
            camera = view.camera()
            camera.setAspectRatioLocked(False)
            camera.setLetterboxEnabled(False)
            camera.setCameraView(0)
            camera.setAxisYaw(45)
            camera.setAxisPitch(-35)
            camera.setAxisRoll(0)
            stageviz.command.add_sublayer(self.config['fixture'])
        elif phase == 'frame':
            session.viewState().camera().frame(session.boundingBox())
            session.viewState().camera().distance(1.1)
            self.mode('Select')
            # Populate a bounded branch, not all 250,000 Qt rows.
            for tree in QtWidgets.QApplication.allWidgets():
                if isinstance(tree, QtWidgets.QTreeWidget) and tree.objectName() == 'outlinerTree':
                    index = QtCore.QModelIndex()
                    for _ in range(3):
                        index = tree.model().index(0, 0, index)
                        if index.isValid():
                            tree.expand(index)
        elif phase == 'cold_sweep' or phase.startswith('warm_sweep_'):
            if not phase.startswith('warm_sweep_active_'):
                self.mode('Select')
            r = self.config['sweepRect']
            a = QtCore.QPoint(round(widget.width() * r[0]), round(widget.height() * r[1]))
            b = QtCore.QPoint(round(widget.width() * r[2]), round(widget.height() * r[3]))
            self.sweep_pixels = [a.x(), a.y(), b.x(), b.y()]
            qt = support['QtTest']
            qt.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=a)
            self.mouse_move(b, QtCore.Qt.MouseButton.LeftButton)
            qt.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=b)
        elif phase.startswith('clear_'):
            stageviz.command.select_paths([])
        elif phase == 'show_gizmo':
            self.mode('Move')
        elif phase.startswith('parent_select'):
            self.mode('Move')
            self.saved_bulk_selection = list(session.paths())
            stageviz.command.select_paths([self.config['parent']])
            stage = session.stage()
            cache = UsdGeom.BBoxCache(0, UsdGeom.Imageable.GetOrderedPurposeTokens(), True)
            bounds = cache.ComputeWorldBound(stage.GetPrimAtPath(self.config['parent'])).ComputeAlignedRange()
            bounds.UnionWith(UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath(self.config['parent'])).ExtractTranslation())
            session.viewState().camera().frame((tuple(bounds.GetMin()), tuple(bounds.GetMax())))
            session.viewState().camera().distance(1.1)
        elif phase.endswith('_press'):
            point = widget.property('transformPivotScreen')
            assert widget.property('transformPivotValid') and point is not None
            self.drag_origin = QtCore.QPoint(round(point.x()), round(point.y()))
            assert widget.rect().contains(self.drag_origin), 'Gizmo must be visible'
            support['QtTest'].QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=self.drag_origin)
            assert widget.property('transformDragging') is True
            assert widget.property('transformActiveAxis') == 11
        elif '_drag_' in phase:
            index = int(phase.rsplit('_', 1)[1])
            self.drag_end = self.drag_origin + QtCore.QPoint(index * 8, -index * 3)
            self.mouse_move(self.drag_end, QtCore.Qt.MouseButton.LeftButton)
        elif phase.endswith('_release'):
            support['QtTest'].QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=self.drag_end)
        elif phase.endswith('_undo') or phase.endswith('_restore'):
            stageviz.command.undo()
        elif phase.endswith('_redo'):
            stageviz.command.redo()
        elif phase == 'cleanup':
            stageviz.command.select_paths([])
            stageviz.command.clear()
            self.mode('Select')
        else:
            raise AssertionError(phase)
        self.dispatch_end = time.perf_counter() * 1000
        self.dispatched = True
        widget.update()

    def expected(self):
        phase = self.current
        session = stageviz.session()
        deferred = phase.endswith('_press') or '_drag_' in phase
        if session.primsUpdate() != (stageviz.PrimsDeferred if deferred else stageviz.PrimsImmediate):
            return False
        if phase == 'load':
            return bool(session.stage().GetPrimAtPath(self.config['lastMesh']))
        if phase == 'cold_sweep' or phase.startswith('warm_sweep_'):
            return bool(session.paths())
        if phase == 'show_gizmo' or phase.startswith('parent_select'):
            return bool(session.auxiliary().GetPrimAtPath('/Display/Transform')) and self.viewport().property('transformPivotValid') is True
        if deferred:
            return self.viewport().property('transformDragging') is True
        if phase.endswith('_release'):
            return self.viewport().property('transformDragging') is False and stageviz.command.can_undo()
        return True

    def restored_values(self, text):
        if text == self.root_before:
            return True
        # Older master can leave inert ancestor overs after undo. Verify that
        # those are the only differences, and report exact restoration separately.
        layer = Sdf.Layer.CreateAnonymous('benchmark_restore_check.usda')
        assert layer.ImportFromString(text)
        paths = []
        layer.Traverse(Sdf.Path.absoluteRootPath, lambda path: paths.append(path))
        for path in sorted(paths, key=lambda p: p.pathElementCount, reverse=True):
            prim = layer.GetPrimAtPath(path)
            if prim and prim.specifier == Sdf.SpecifierOver and prim.isInert:
                edit = Sdf.BatchNamespaceEdit()
                edit.Add(path, Sdf.Path.emptyPath)
                assert layer.Apply(edit)
        return layer.ExportToString() == self.root_before

    def finish(self):
        end = time.perf_counter() * 1000
        self.active = False
        session = stageviz.session()
        stage = session.stage()
        phase = self.current
        paths = list(session.paths())
        root_text = stage.GetRootLayer().ExportToString()
        sample_hash = None
        if self.config.get('viewportSize'):
            assert [self.viewport().width(), self.viewport().height()] == self.config['viewportSize'], 'Viewport differs from the comparison baseline'
        if phase == 'load':
            meshes = faces = prims = 0
            for prim in stage.Traverse():
                prims += 1
                if prim.IsA(UsdGeom.Mesh):
                    meshes += 1
                    faces += len(UsdGeom.Mesh(prim).GetFaceVertexCountsAttr().Get())
                    assert not prim.IsInstance()
            assert meshes == self.config['meshes'] and faces == meshes * 80
            self.counts = dict(prims=prims, meshes=meshes, faces=faces)
            self.root_before = root_text
            stageviz.command.clear()
        if phase == 'cold_sweep':
            self.saved_sweep = paths
        if phase.startswith('warm_sweep_'):
            assert paths == self.saved_sweep, 'Warm selection must match cold selection exactly, including order'
        if phase == 'show_gizmo' or phase.startswith('parent_select'):
            selected = paths if phase == 'show_gizmo' else [self.config['parent'], self.config['parentChild']]
            sample = selected[::max(1, len(selected) // 64)][:64]
            self.before = self.worlds(sample)
            self.expected_selection = paths
            assert self.restored_values(root_text)
            stageviz.command.clear()
        if phase.startswith(('bulk_', 'parent_')) and not phase.startswith('parent_select'):
            actual = self.worlds(self.before)
            if phase.endswith(('_undo', '_restore')):
                assert all(Gf.IsClose(matrix, self.before[path], 1e-7) for path, matrix in actual.items())
                assert self.restored_values(root_text), 'Undo must restore authored values; only inert ancestor overs may differ'
            elif '_drag_' in phase or phase.endswith(('_release', '_redo')):
                assert any(not Gf.IsClose(matrix, self.before[path], 1e-7) for path, matrix in actual.items())
                delta = self.before[next(iter(self.before))].GetInverse() * actual[next(iter(self.before))]
                assert all(Gf.IsClose(matrix, self.before[path] * delta, 1e-6) for path, matrix in actual.items())
            assert paths == self.expected_selection
            sample_hash = hashlib.sha256(repr([(p, [list(row) for row in m]) for p, m in actual.items()]).encode()).hexdigest()
        if phase == 'cleanup':
            assert self.restored_values(root_text) and not paths and not stageviz.command.can_undo()
        dirty = [layer.identifier for layer in stage.GetUsedLayers() if not layer.anonymous and layer.dirty]
        assert not dirty, dirty
        row = dict(phase=phase, start_wall_ms=self.start_wall, dispatch_ms=self.dispatch_end - self.start_ms,
                   end_wall_ms=time.time() * 1000, end_to_end_ms=end - self.start_ms,
                   first_present_ms=self.frames[0] - self.start_ms if self.frames else None,
                   responsive_ms=self.responsive_at - self.start_ms,
                   ready_ms=self.ready_at - self.start_ms,
                   settle_floor_ms=self.config.get('settleSeconds', .75) * 1000,
                   frames=len(self.frames), qt_version=QtCore.qVersion(), selection_count=len(paths),
                   selection_sha256=hashlib.sha256('\n'.join(paths).encode()).hexdigest(),
                   selected_sample=paths[:16], sampled_world_sha256=sample_hash,
                   source_dirty_layers=dirty, root_restored=root_text == self.root_before,
                   authored_values_restored=self.restored_values(root_text),
                   widget_size=[self.viewport().width(), self.viewport().height()],
                   device_pixel_ratio=self.viewport().devicePixelRatioF(), counts=self.counts,
                   sweep_pixels=getattr(self, 'sweep_pixels', None),
                   view_state=self.view_state(), verified=True)
        if phase in ('frame', 'cold_sweep', 'warm_sweep_3', 'show_gizmo', 'bulk_release', 'parent_release', 'cleanup'):
            assert support['support']['tb'].viewer().grab().save(str(self.directory / (phase + '-viewer.png')))
            (self.directory / (phase + '-windows.json')).write_text(json.dumps([dict(name=w.objectName(), visible=w.isVisible(), enabled=w.isEnabled()) for w in QtWidgets.QApplication.topLevelWidgets()], indent=2))
            for tree in QtWidgets.QApplication.allWidgets():
                if isinstance(tree, QtWidgets.QTreeWidget) and tree.objectName() == 'outlinerTree':
                    (self.directory / (phase + '-outliner.json')).write_text(json.dumps(self.outliner_snapshot(tree), indent=2))
                    break
        self.results.append(row)
        (self.directory / 'results.json').write_text(json.dumps(self.results, indent=2))
        self.waiting = phase
        self.status('checkpoint', result=row)

    def poll(self):
        try:
            if self.waiting:
                ack = self.directory / 'ack.txt'
                if ack.exists() and ack.read_text() == self.waiting:
                    self.waiting = None
                    if len(self.results) == len(self.phases):
                        self.timer.stop()
                        if hasattr(stageviz, '_scaleEndActivity'):
                            stageviz._scaleEndActivity()
                        self.status('complete')
                    else:
                        QtCore.QTimer.singleShot(0, self.safe_start)
                return
            if not self.active:
                if not self.results:
                    QtCore.QTimer.singleShot(0, self.safe_start)
                return
            if not self.dispatched:
                return
            now = time.perf_counter() * 1000
            if now - self.start_ms > self.config.get('timeoutSeconds', 1200) * 1000:
                raise TimeoutError(self.current)
            if self.expected():
                if self.ready_at is None:
                    self.ready_at = now
                if len(self.frames) >= 3 and self.responsive_at is None:
                    self.responsive_at = now
                if self.responsive_at is not None and now - self.responsive_at >= self.config.get('settleSeconds', .75) * 1000:
                    self.finish()
                    return
            if len(self.frames) < 3:
                self.viewport().update()
        except Exception:
            self.timer.stop()
            self.status('failed', error=traceback.format_exc())

    def safe_start(self):
        try:
            self.start()
        except Exception:
            self.timer.stop()
            self.status('failed', error=traceback.format_exc())


if globals().get('_SCALE_AUTORUN', True):
    config = json.loads(Path(__import__('os').environ['STAGEVIZ_SCALE_PROFILE']).read_text())
    stageviz._scaleBenchmark = ScaleBenchmark(config)
