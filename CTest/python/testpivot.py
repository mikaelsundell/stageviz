# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Pivot-frame API and viewport checks for a disposable Stageviz instance.

Load through runpy, construct PivotTest(disposable=True), and call setup(),
api(mode), gesture(mode), and cleanup() from an external MCP controller. Each
phase checks undo/redo before another MCP Python entry clears command history.
"""

from pathlib import Path
import runpy

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name("testtransformoperation.py")))
tb = _base["tb"]


class PivotTest(_base["TransformOperationTest"]):
    targets = ("/World/Plain", "/World/Instance", "/World/Unselected")

    def frames(self):
        stage = stageviz.session().stage()
        cache = UsdGeom.XformCache()
        result = {}
        for path in self.targets:
            prim = stage.GetPrimAtPath(path)
            attr = prim.GetAttribute("xformOp:transform:pivot")
            local = attr.Get() if attr else Gf.Matrix4d(1)
            result[path] = local * cache.GetLocalToWorldTransform(prim)
        return result

    def setup(self):
        super().setup()
        self.activate_mode("Move")
        action = tb.action_info("displayPivot")
        assert action is not None and action["checkable"]
        if not action["checked"]:
            assert tb.trigger_action("displayPivot")
        tb.process_events(100)
        assert tb.action_info("displayPivot")["checked"], "pivot is an independent toggle"
        self.snapshot("pivot_enabled")

    def finish_pivot(self, before, original, before_frames, label, expected=None):
        assert self.matches(before), label + " keeps all geometry fixed"
        after_frames = self.frames()
        delta = before_frames[self.targets[0]].GetInverse() * after_frames[self.targets[0]]
        assert not Gf.IsClose(delta, Gf.Matrix4d(1), 1e-6), label + " changes the pivot frame"
        if expected is not None:
            assert Gf.IsClose(delta, expected, 1e-6)
        for path in self.targets[:2]:
            assert Gf.IsClose(after_frames[path], before_frames[path] * delta, 1e-6)
        assert Gf.IsClose(after_frames[self.targets[-1]], before_frames[self.targets[-1]], 1e-6)
        assert list(stageviz.session().paths()) == self.selected
        authored = self.layer_text()
        assert stageviz.command.can_undo()
        stageviz.command.undo()
        assert tb.wait_until(lambda: self.layer_text() == original and self.matches(before))
        assert not stageviz.command.can_undo(), "one pivot gesture produces one undo step"
        stageviz.command.redo()
        assert tb.wait_until(lambda: self.layer_text() == authored and self.matches(before))
        self.snapshot(label)
        print("PASS", label, "fixed geometry, shared pivot delta, preserved instances, exact undo/redo")

    def api(self, mode):
        delta = {"Move": Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(0.3, 0.2, 0.1)),
                 "Rotate": Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d(0, 0, 1), 20)),
                 "Scale": Gf.Matrix4d(1).SetScale(Gf.Vec3d(1.1, 0.9, 1.2))}[mode]
        before, original, frames = self.worlds(), self.layer_text(), self.frames()
        stageviz.command.clear()
        stageviz.command.transform_pivots(self.selected, [list(row) for row in delta])
        self.finish_pivot(before, original, frames, "pivot_api_" + mode.lower(), delta)

    def gesture(self, mode):
        QtCore, _, QtTest, _ = tb.qt()
        frames = self.frames()
        pivot = (frames[self.targets[0]].ExtractTranslation() + frames[self.targets[1]].ExtractTranslation()) * 0.5
        camera = stageviz.session().viewState().camera()
        camera.frame((tuple(pivot - Gf.Vec3d(4)), tuple(pivot + Gf.Vec3d(4))))
        camera.distance(1.6)
        self.activate_mode(mode)
        assert tb.action_info("displayPivot")["checked"], "changing tools retains pivot mode"
        tb.process_events(100)
        before, original = self.worlds(), self.layer_text()
        stageviz.command.clear()
        widget = self.viewport()
        start = QtCore.QPoint(widget.width() // 2, widget.height() // 2)
        end = start + QtCore.QPoint(28, -8)
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=start)
        QtTest.QTest.mouseMove(widget, end, 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(150)
        self.finish_pivot(before, original, frames, "pivot_gizmo_" + mode.lower())

    def property_edit(self):
        stageviz.command.select_paths(["/World/Instance"])
        tb.process_events(100)
        stage = stageviz.session().stage()
        attr = stage.GetPrimAtPath("/World/Instance").GetAttribute("xformOp:transform:pivot")
        value = attr.Get()[3][0] + 0.25
        before, original = self.worlds(), self.layer_text()
        stageviz.command.clear()
        QtCore = tb.qt()[0]
        tree = self.tree("propertyTree")
        items = tree.findItems("xformOp:transform:pivot", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)
        assert len(items) == 1
        group = items[0]
        group.setExpanded(True)
        row = group.child(3)
        row.setExpanded(True)
        component = row.child(0)
        component.setText(1, str(value))
        assert tb.wait_until(lambda: abs(attr.Get()[3][0] - value) < 1e-8)
        assert self.matches(before), "Property Editor pivot edit keeps geometry fixed"
        stageviz.command.undo()
        assert tb.wait_until(lambda: self.layer_text() == original)
        assert not stageviz.command.can_undo(), "Property Editor pivot edit is one command"
        stageviz.command.redo()
        assert tb.wait_until(lambda: abs(attr.Get()[3][0] - value) < 1e-8)
        assert self.matches(before)
        self.snapshot("pivot_property_edit")
        print("PASS pivot Property Editor component edit and exact undo/redo")

    def edge_cases(self):
        # Put the fixture behind a reference so the root edit layer starts with
        # no prim specs for the selected paths or their intermediate ancestors.
        wrapper_path = self.directory / "pivot_reference.usda"
        wrapper = Usd.Stage.CreateNew(str(wrapper_path))
        root = UsdGeom.Xform.Define(wrapper, "/World")
        root.GetPrim().GetReferences().AddReference(self.fixture_path, "/World")
        wrapper.SetDefaultPrim(root.GetPrim())
        wrapper.GetRootLayer().Save()
        assert stageviz.session().loadFromFile(str(wrapper_path))
        tb.process_events(100)
        stage = stageviz.session().stage()
        stageviz.command.select_paths(self.selected)
        stageviz.command.clear()
        before, original = self.worlds(), self.layer_text()
        delta = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(0.2, 0.1, 0.3))
        stageviz.command.transform_pivots(self.selected, [list(row) for row in delta])
        assert self.matches(before)
        stageviz.command.undo()
        assert tb.wait_until(lambda: self.layer_text() == original), "undo prunes only newly created ancestor overs"
        stageviz.command.clear()
        singular = Gf.Matrix4d(1).SetScale(Gf.Vec3d(0, 1, 1))
        stageviz.command.transform_pivots(self.selected, [list(row) for row in singular])
        assert self.layer_text() == original, "singular delta is rejected before authoring"
        reset = UsdGeom.Xform.Define(stage, "/World/ResetPivot")
        reset.AddTranslateOp().Set(Gf.Vec3d(3, 2, 1))
        reset.SetResetXformStack(True)
        original_world = UsdGeom.XformCache().GetLocalToWorldTransform(reset.GetPrim())
        stageviz.command.transform_pivots([str(reset.GetPath())], [list(row) for row in delta])
        assert reset.GetResetXformStack(), "pivot authoring preserves resetXformStack"
        assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(reset.GetPrim()), original_world, 1e-6)
        print("PASS reference-layer exact undo, singular rejection, and resetXformStack")

    def centered_object_drag(self, mode, full_frame=False, center=True):
        """Centering and normal dragging retain the same active pivot pair."""
        QtCore, _, QtTest, _ = tb.qt()
        stage = stageviz.session().stage()
        path = "/World/PivotDrag"
        prim = UsdGeom.Cube.Define(stage, path).GetPrim()
        xf = UsdGeom.Xformable(prim)
        xf.ClearXformOpOrder()
        xf.AddTranslateOp().Set(Gf.Vec3d(2, 3, 1))
        xf.AddRotateXYZOp().Set(Gf.Vec3f(15, 25, 35))
        xf.AddScaleOp().Set(Gf.Vec3f(1.2, 0.8, 1.1))
        stageviz.command.select_paths([path])
        tb.process_events(100)
        if full_frame:
            delta = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(0.4, 0.3, 0.2))
            stageviz.command.transform_pivots([path], [list(row) for row in delta])
            assert tb.wait_until(lambda: bool(prim.GetAttribute("xformOp:transform:pivot")))
        pivot_name = "xformOp:transform:pivot" if full_frame and not center else "xformOp:translate:pivot"
        if center:
            stageviz.command.center_pivots([path])
            assert tb.wait_until(lambda: bool(prim.GetAttribute(pivot_name)))
        if tb.action_info("displayPivot")["checked"]:
            assert tb.trigger_action("displayPivot")
        self.activate_mode(mode)
        pivot_value = prim.GetAttribute(pivot_name).Get()
        local_pivot = pivot_value.ExtractTranslation() if isinstance(pivot_value, Gf.Matrix4d) else pivot_value
        world = lambda: UsdGeom.XformCache().GetLocalToWorldTransform(prim)
        before = world()
        pivot = before.Transform(local_pivot)
        camera = stageviz.session().viewState().camera()
        camera.frame((tuple(pivot - Gf.Vec3d(3)), tuple(pivot + Gf.Vec3d(3))))
        camera.distance(1.6)
        tb.process_events(150)
        original = self.layer_text()
        stageviz.command.clear()
        widget = self.viewport()
        start = QtCore.QPoint(widget.width() // 2, widget.height() // 2)
        end = start + QtCore.QPoint(30, -15)
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=start)
        assert Gf.IsClose(world(), before, 1e-6), "press keeps geometry fixed"
        assert xf.GetXformOpOrderAttr().Get() == [pivot_name, "xformOp:transform", "!invert!" + pivot_name], (
            "press retains the active pivot")
        QtTest.QTest.mouseMove(widget, end, 50)
        during = world()
        assert not Gf.IsClose(during, before, 1e-6), "gesture moves the object"
        assert prim.GetAttribute(pivot_name).Get() == pivot_value
        if mode != "Move":
            assert Gf.IsClose(during.Transform(local_pivot), pivot, 1e-6), "rotate/scale stay around the centered pivot"
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(100)
        assert Gf.IsClose(world(), during, 1e-6), "release does not change the transform"
        self.snapshot(("centered_drag_" if center else "frame_drag_") + mode.lower()
                      + ("_after_frame" if full_frame else ""))
        authored = self.layer_text()
        stageviz.command.undo()
        assert tb.wait_until(lambda: self.layer_text() == original and Gf.IsClose(world(), before, 1e-6))
        stageviz.command.redo()
        assert tb.wait_until(lambda: self.layer_text() == authored and Gf.IsClose(world(), during, 1e-6))
        stage.RemovePrim(path)
        stageviz.command.clear()
        print("PASS centered object drag", mode, "after full frame", full_frame, "press/move/release and exact undo/redo")

    def cleanup(self):
        action = tb.action_info("displayPivot")
        if action and action["checked"]:
            assert tb.trigger_action("displayPivot")
        super().cleanup()
