# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Live far-plane regression and bounded large-selection drag measurements."""
import json
import math
from pathlib import Path
import random
import runpy
import statistics
import time

import stageviz
from pxr import Gf, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class DragClippingTest(_base['ViewportTest']):
    def setup(self, fixture=None, count=1):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.count = count
        if fixture:
            assert stageviz.session().loadFromFile(str(fixture))
        else:
            assert stageviz.session().newStage()
            stageviz.command.new_xform('/World', 'Group')
            assert tb.wait_for_commands()
            stageviz.command.new_prim('/World/Group', 'Item00000', 'Cube')
            assert tb.wait_for_commands()
        self.paths = ['/World/Group/Item%05d' % i for i in range(count)]
        state = stageviz.session().viewState()
        state.setGridEnabled(False)
        state.setSceneStatsEnabled(False)
        state.setPerformanceStatsEnabled(False)
        self.activate_mode('Move')
        _, _, _, widgets = tb.qt()
        def set_world_frame():
            for widget in tb.all_widgets():
                if isinstance(widget, widgets.QComboBox) and widget.objectName() == 'referenceFrame':
                    widget.setCurrentIndex(0)
                    return True
            return False
        assert tb.wait_until(set_world_frame), 'live reference-frame control after stage replacement'
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(3)
        tb.process_events(100)

    def move_event(self, point):
        core, gui, _, widgets = tb.qt()
        widget = self.viewport()
        event = gui.QMouseEvent(core.QEvent.Type.MouseMove, core.QPointF(point),
                               core.QPointF(widget.mapToGlobal(point)), core.Qt.MouseButton.NoButton,
                               core.Qt.MouseButton.LeftButton, core.Qt.KeyboardModifier.NoModifier)
        widgets.QApplication.sendEvent(widget, event)

    def drag(self, case='all', samples=12, verify_clipping=True):
        core, _, qt_test, widgets = tb.qt()
        session = stageviz.session()
        stage = session.stage()
        if case == 'parent':
            selected = ['/World/Group']
        elif case == 'random':
            selected = random.Random(814).sample(self.paths, min(5000, self.count))
        else:
            selected = list(self.paths)
        stageviz.command.select_paths(selected)
        assert tb.wait_for_commands()
        stageviz.command.clear()
        cache = UsdGeom.XformCache()
        before = {path: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)) for path in selected}
        root_before = stage.GetRootLayer().ExportToString()
        pivot = before[selected[-1]].ExtractTranslation()
        camera = session.viewState().camera()
        camera.setCameraUp(stageviz.CameraUpY)
        camera.setCameraView(stageviz.CameraViewFront)
        camera.frame((tuple(pivot - Gf.Vec3d(12)), tuple(pivot + Gf.Vec3d(12))))
        camera.setAxisYaw(0)
        camera.setAxisPitch(25)
        camera.setAxisRoll(45)
        camera.setCameraDistance(40)
        tb.process_events(100)
        widget = self.viewport()
        widget.repaint()
        # Project the world-X handle using the configured orthographic camera.
        rotation = (Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d.XAxis(), -25))
                    * Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d.YAxis(), -45)))
        right = rotation.TransformDir(Gf.Vec3d.XAxis())
        up = rotation.TransformDir(Gf.Vec3d.YAxis())
        axis = core.QPointF(right[0], -up[0])
        size = stageviz.ui().gizmoSize(stageviz.UI.Medium)
        center = core.QPointF(widget.width() / 2, widget.height() / 2)
        start = (center + axis * (size * .72)).toPoint()
        endpoint = (core.QPointF(start) + axis * 320).toPoint()
        dispatch = []
        rendered = []
        bounds_unchanged = session.boundingBox()
        qt_test.QTest.mousePress(widget, core.Qt.MouseButton.LeftButton, pos=start)
        assert session.primsUpdate() == stageviz.PrimsDeferred, 'X-axis handle starts a deferred drag'
        if widget.property('transformActiveAxis') is not None:
            assert widget.property('transformActiveAxis') == 1, 'world-X handle'
        try:
            for i in range(samples + 3):
                # A deterministic oscillation keeps both builds on the same path.
                point = (core.QPointF(start) + axis * (240 + 80 * math.sin(i * .37))).toPoint()
                started = time.perf_counter()
                self.move_event(point)
                moved = time.perf_counter()
                widget.repaint()
                painted = time.perf_counter()
                if i >= 3:
                    dispatch.append((moved - started) * 1000)
                    rendered.append((painted - started) * 1000)
                assert session.primsUpdate() == stageviz.PrimsDeferred
                endpoint = point
            cache = UsdGeom.XformCache()
            after = {path: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)) for path in selected}
            delta = before[selected[-1]].GetInverse() * after[selected[-1]]
            displacement = delta.ExtractTranslation()
            assert displacement[0] > 1 and abs(displacement[1]) < 1e-6 and abs(displacement[2]) < 1e-6
            for path in selected:
                assert Gf.IsClose(after[path], before[path] * delta, 1e-5), path
            # No USD bounds query occurs in the measured loop. A constant test
            # box records the pre-drag state for the report only.
            row = {'case': case, 'objects': self.count, 'selected': len(selected),
                   'dispatchMs': dispatch, 'paintMs': rendered,
                   'dispatchMedianMs': statistics.median(dispatch),
                   'paintMedianMs': statistics.median(rendered),
                   'displacement': list(displacement), 'cachedBounds': bounds_unchanged}
            if self.count == 1:
                visible = stageviz.render().capturePaths(accumulate=False)
                row['visibleDuringDrag'] = self.paths[0] in visible
                if verify_clipping:
                    assert row['visibleDuringDrag'], 'geometry remains visible beyond the stale rear clip'
            assert tb.viewer().grab().save(str(self.directory / (case + '-during-drag.png')))
        finally:
            qt_test.QTest.mouseRelease(widget, core.Qt.MouseButton.LeftButton, pos=endpoint)
        assert tb.wait_for_commands()
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == root_before
        stageviz.command.redo()
        assert tb.wait_for_commands()
        cache = UsdGeom.XformCache()
        assert all(Gf.IsClose(cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)), after[path], 1e-5)
                   for path in selected)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == root_before
        assert list(session.paths()) == selected
        # Collapse only after the populated-branch measurements, keeping the
        # reusable snapshot bounded for massive selections.
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.collapseAll()
        self.snapshot(case + '-restored')
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(3)
        (self.directory / (case + '-timings.json')).write_text(json.dumps(row, indent=2))
        print('DRAG_CLIPPING_RESULT ' + json.dumps(row))
        return row


def run(disposable=False):
    test = DragClippingTest(disposable=disposable)
    try:
        test.setup()
        test.drag()
    finally:
        test.cleanup()
