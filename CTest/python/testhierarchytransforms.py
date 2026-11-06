# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Hierarchy drag/numeric/Applied/undo regression for a disposable viewer.

Load with runpy; construct HierarchyTransformTest(disposable=True), then call
setup(), numeric('/World'), numeric('/World/Cube'), drag(path), and cleanup().
External MCP controllers can inspect the stage between calls. No embedded MCP
requests are made, and each call checks undo/redo before returning.
"""

import math
from pathlib import Path
import runpy

import stageviz
from pxr import Gf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class HierarchyTransformTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable, 'hierarchy tests require a disposable viewer'
        assert stageviz.session().newStage()
        stageviz.command.new_prim('/World', 'Cube', type_name='Cube')
        tb.process_events(150)
        self.original_filename = ''
        self.original_selection = []
        self.activate_mode('Rotate')
        _, _, _, widgets = tb.qt()
        self.root = tb.viewer()
        combo = tb.find_widget('referenceFrame', widgets.QComboBox, self.root)
        combo.setCurrentIndex(0)  # World
        stageviz.command.clear()
        self.snapshot('hierarchy_loaded')

    def fields(self):
        _, _, _, widgets = tb.qt()
        return [tb.find_widget('value' + axis, widgets.QDoubleSpinBox, self.root) for axis in 'XYZ']

    def values(self):
        return Gf.Vec3d(*(field.value() for field in self.fields()))

    def local(self, path):
        return UsdGeom.Xformable(stageviz.session().stage().GetPrimAtPath(path)).GetLocalTransformation()

    def angles(self, path):
        matrix = Gf.Matrix4d(self.local(path))
        for row in range(3):
            length = Gf.Vec3d(*list(matrix[row])[:3]).GetLength()
            for col in range(3):
                matrix[row, col] /= length
        zyx = matrix.ExtractRotation().Decompose(Gf.Vec3d.ZAxis(), Gf.Vec3d.YAxis(), Gf.Vec3d.XAxis())
        return Gf.Vec3d(zyx[2], zyx[1], zyx[0])

    def check_panel(self, path):
        # Compare orientations: equivalent Euler branches occur near Y=90.
        expected_stage = Usd.Stage.CreateInMemory()
        op = UsdGeom.Xform.Define(expected_stage, '/Expected').AddRotateXYZOp(UsdGeom.XformOp.PrecisionDouble)
        op.Set(self.angles(path))
        actual_rotation = op.GetOpTransform(Usd.TimeCode.Default())
        op.Set(self.values())
        assert Gf.IsClose(actual_rotation, op.GetOpTransform(Usd.TimeCode.Default()), .0004), (
            self.angles(path), self.values())

    def history(self, path, before, preview, shown, label):
        assert Gf.IsClose(self.local(path), preview, 1e-6), 'Applied preserves preview'
        assert Gf.IsClose(self.values(), shown, .015), 'commit preserves displayed values'
        assert stageviz.command.can_undo(), 'gesture is committed immediately'
        stageviz.command.undo()
        assert tb.wait_until(lambda: Gf.IsClose(self.local(path), before, 1e-6))
        self.check_panel(path)
        assert not stageviz.command.can_undo(), 'one gesture is one history entry'
        stageviz.command.redo()
        assert tb.wait_until(lambda: Gf.IsClose(self.local(path), preview, 1e-6))
        self.check_panel(path)
        assert Gf.IsClose(self.values(), shown, .015)
        self.snapshot(label)

    def numeric(self, path):
        core, _, test, widgets = tb.qt()
        stageviz.command.select_paths([path])
        tb.process_events(100)
        stageviz.command.clear()
        before = self.local(path)
        requested = (24.0, -32.0, 19.0) if path == '/World' else (-15.0, 27.0, 38.0)
        for field, value in zip(self.fields(), requested):
            field.setValue(value)
        tb.process_events(80)
        preview = self.local(path)
        expected_stage = Usd.Stage.CreateInMemory()
        expected_op = UsdGeom.Xform.Define(expected_stage, '/Expected').AddRotateXYZOp()
        expected_op.Set(Gf.Vec3f(*requested))
        assert Gf.IsClose(preview, expected_op.GetOpTransform(Usd.TimeCode.Default()), 1e-6), (
            'numeric rotation matches USD rotateXYZ, independently of panel decomposition')
        self.check_panel(path)
        assert Gf.IsClose(self.values(), Gf.Vec3d(*requested), .015)
        # Enter commits the whole numeric preview without changing selection.
        line = self.fields()[-1].findChild(widgets.QLineEdit)
        test.QTest.keyClick(line, core.Qt.Key.Key_Return)
        tb.process_events(80)
        self.history(path, before, preview, Gf.Vec3d(*requested), 'numeric_' + path.rsplit('/', 1)[-1])

    def xyz_convention(self):
        core, _, test, widgets = tb.qt()
        filename = self.directory / 'xyz_convention.usda'
        fixture = Usd.Stage.CreateNew(str(filename))
        UsdGeom.SetStageUpAxis(fixture, UsdGeom.Tokens.y)
        parent = UsdGeom.Xform.Define(fixture, '/World')
        parent.AddRotateXYZOp().Set(Gf.Vec3f(30, -25, 15))
        cube = UsdGeom.Cube.Define(fixture, '/World/Cube')
        cube.AddRotateXYZOp().Set(Gf.Vec3f(-60, 56, 70))
        cube.AddScaleOp().Set(Gf.Vec3f(4.188))
        fixture.GetRootLayer().Save()
        assert stageviz.session().loadFromFile(str(filename))
        stageviz.command.select_paths(['/World/Cube'])
        self.activate_mode('Rotate')
        tb.process_events(150)
        assert Gf.IsClose(self.values(), Gf.Vec3d(-60, 56, 70), .015), (
            'panel matches authored USD XYZ angles under rotated parent')
        self.snapshot('authored_xyz_convention')
        for requested in (Gf.Vec3d(-25, 42, 63), Gf.Vec3d(24, 89.99, -33), Gf.Vec3d(24, 90.01, -33)):
            stageviz.command.clear()
            before = self.local('/World/Cube')
            expected_stage = Usd.Stage.CreateInMemory()
            expected_op = UsdGeom.Xform.Define(expected_stage, '/Expected').AddRotateXYZOp()
            expected_op.Set(Gf.Vec3f(*requested))
            for field, value in zip(self.fields(), requested):
                field.setValue(value)
            preview = self.local('/World/Cube')
            expected = Gf.Matrix4d(1).SetScale(Gf.Vec3d(4.188)) * expected_op.GetOpTransform(Usd.TimeCode.Default())
            assert Gf.IsClose(preview, expected, 1e-6), 'numeric XYZ matches USD while retaining scale'
            test.QTest.keyClick(self.fields()[-1].findChild(widgets.QLineEdit), core.Qt.Key.Key_Return)
            tb.process_events(100)
            self.history('/World/Cube', before, preview, requested, 'numeric_xyz_' + str(requested[1]))
            stageviz.command.undo()
            tb.process_events(100)
            assert Gf.IsClose(self.local('/World/Cube'), before, 1e-6)

    def scrub(self, path):
        core, gui, test, widgets = tb.qt()
        stageviz.command.select_paths([path])
        tb.process_events(100)
        stageviz.command.clear()
        before = self.local(path)
        line = self.fields()[0].findChild(widgets.QLineEdit)
        start = core.QPoint(line.width() // 2, line.height() // 2)
        end = start + core.QPoint(30, 0)
        test.QTest.mousePress(line, core.Qt.MouseButton.LeftButton, pos=start)
        event = gui.QMouseEvent(core.QEvent.Type.MouseMove, core.QPointF(end),
                               core.QPointF(line.mapToGlobal(end)), core.Qt.MouseButton.NoButton,
                               core.Qt.MouseButton.LeftButton, core.Qt.KeyboardModifier.NoModifier)
        widgets.QApplication.sendEvent(line, event)
        preview, shown = self.local(path), self.values()
        assert not Gf.IsClose(before, preview, 1e-6), 'numeric scrub changes rotation'
        self.check_panel(path)
        test.QTest.mouseRelease(line, core.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(80)
        self.history(path, before, preview, shown, 'scrub_' + path.rsplit('/', 1)[-1])

    def drag(self, path, axis="Z"):
        core, _, test, _ = tb.qt()
        stageviz.command.select_paths([path])
        camera = stageviz.session().viewState().camera()
        camera.setCameraUp(1)
        camera.setAxisYaw(90 if axis == "X" else 0)
        camera.setAxisPitch(90 if axis == "Y" else 0)
        camera.setAxisRoll(0)
        camera.frame(((-3, -3, -3), (3, 3, 3)))
        camera.distance(1.6)
        tb.process_events(150)
        stageviz.command.clear()
        before = self.local(path)
        viewport = self.viewport()
        radius = stageviz.ui().gizmoSize(1)
        center = core.QPoint(viewport.width() // 2, viewport.height() // 2)
        def point(angle):
            return center + core.QPoint(round(radius * math.cos(angle)), round(-radius * math.sin(angle)))
        start, end = point(.6), point(1.1)
        test.QTest.mousePress(viewport, core.Qt.MouseButton.LeftButton, pos=start)
        test.QTest.mouseMove(viewport, end, 50)
        tb.process_events(80)
        preview, shown = self.local(path), self.values()
        assert not Gf.IsClose(before, preview, 1e-6), 'ring drag changes transform'
        self.check_panel(path)
        self.snapshot('preview_' + path.rsplit('/', 1)[-1] + '_' + axis)
        test.QTest.mouseRelease(viewport, core.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(100)
        self.history(path, before, preview, shown, 'drag_' + path.rsplit('/', 1)[-1] + '_' + axis)
