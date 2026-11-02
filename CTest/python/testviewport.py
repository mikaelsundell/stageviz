# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Exercise click/sweep selection and transform drags in a disposable viewer.

Run from the embedded Python editor or through an external MCP controller.
The test refuses to replace a stage containing unsaved work unless the caller
explicitly marks the process disposable with _VIEWPORT_DISPOSABLE=True. An external
controller may load this file with _VIEWPORT_AUTORUN=False and call setup(),
sweep(), drag(mode), and cleanup() separately to inspect MCP state between steps.
Embedded code must never synchronously call the application's MCP server.
"""

import contextlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom

# Register the viewport base type before enumerating custom Qt widgets.
try:
    from PySide6 import QtOpenGLWidgets as _QtOpenGLWidgets
except ImportError:
    from PyQt6 import QtOpenGLWidgets as _QtOpenGLWidgets

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


class ViewportTest:
    def __init__(self, disposable=False):
        self.disposable = disposable
        self.directory = Path(tempfile.mkdtemp(prefix="stageviz-viewport-"))
        self.roots = ["/World/One", "/World/Two"]
        self.proxies = [root + child for root in self.roots for child in ("/Body", "/Detail")]
        self.original_filename = None
        self.original_selection = []
        self.center = None

    def viewport(self):
        for widget in tb.all_widgets():
            try:
                if widget.objectName() != "imagingGLWidget" or not widget.isVisible():
                    continue
                assert widget.metaObject().className() == "stageviz::ViewportWidget", "viewport uses the renamed class"
                return widget
            except RuntimeError:
                continue
        raise AssertionError("visible viewport retains its stable objectName")

    def activate_mode(self, mode):
        QtCore, _, QtTest, QtWidgets = tb.qt()
        for button in tb.all_widgets():
            try:
                if (button.objectName() != mode.lower() or not isinstance(button, QtWidgets.QToolButton)
                        or not button.isVisible()):
                    continue
                if not button.isChecked():
                    QtTest.QTest.mouseClick(button, QtCore.Qt.MouseButton.LeftButton)
                assert button.isChecked(), mode + " mode is checked in the toolbar"
                return
            except RuntimeError:
                continue
        raise AssertionError("visible tool button for " + mode)

    def snapshot(self, label):
        # Retain only plain Python snapshot data across event processing.
        with contextlib.redirect_stdout(io.StringIO()):
            state = tb.snapshot_qt_state(label)
        state["selectedPaths"] = list(stageviz.session().paths())
        (self.directory / (label + ".json")).write_text(json.dumps(state, indent=2))
        assert tb.viewer().grab().save(str(self.directory / (label + ".png")))
        print("Viewport checkpoint:", label, "selection:", state["selectedPaths"])

    def setup(self):
        session = stageviz.session()
        original = session.stage()
        has_prims = bool(original and list(original.GetPseudoRoot().GetChildren()))
        assert self.disposable or not (has_prims and tb.dirty_layers(original)), "use a disposable viewer or a clean saved stage"
        self.original_filename = session.filename()
        self.original_selection = list(session.paths())
        asset_path = self.directory / "asset.usda"
        asset = Usd.Stage.CreateNew(str(asset_path))
        model = UsdGeom.Xform.Define(asset, "/Model")
        asset.SetDefaultPrim(model.GetPrim())
        for name, y in (("Body", -0.8), ("Detail", 0.8)):
            cube = UsdGeom.Cube.Define(asset, "/Model/" + name)
            cube.GetSizeAttr().Set(1)
            cube.AddTranslateOp().Set(Gf.Vec3d(0, y, 0))
        asset.GetRootLayer().Save()
        fixture_path = self.directory / "fixture.usda"
        fixture = Usd.Stage.CreateNew(str(fixture_path))
        UsdGeom.SetStageUpAxis(fixture, UsdGeom.Tokens.y)
        UsdGeom.SetStageMetersPerUnit(fixture, 1.0)
        world = UsdGeom.Xform.Define(fixture, "/World")
        world.AddRotateXYZOp().Set(Gf.Vec3f(12, 23, 8))
        world.AddScaleOp().Set(Gf.Vec3f(1.2, 0.9, 1.1))
        fixture.SetDefaultPrim(world.GetPrim())
        for path, x in zip(self.roots, (-2.0, 2.0)):
            instance = UsdGeom.Xform.Define(fixture, path)
            instance.GetPrim().GetReferences().AddReference(str(asset_path), "/Model")
            instance.GetPrim().SetInstanceable(True)
            instance.AddTranslateOp().Set(Gf.Vec3d(x, 0, 0))
        fixture.GetRootLayer().Save()
        self.fixture_path = str(fixture_path)
        assert session.loadFromFile(self.fixture_path)
        tb.process_events(250)
        stageviz.command.select_paths([])
        session.viewState().camera().setAspectRatioLocked(False)
        self.activate_mode("Select")
        tb.trigger_action("displayFrameAll")
        session.viewState().camera().distance(1.8)
        tb.process_events(300)
        self.snapshot("loaded")
        print("Viewport fixture:", self.fixture_path)

    def sweep(self):
        QtCore, _, QtTest, _ = tb.qt()
        widget = self.viewport()
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=QtCore.QPoint(8, 8))
        QtTest.QTest.mouseMove(widget, QtCore.QPoint(widget.width() - 8, widget.height() - 8), 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton,
                                 pos=QtCore.QPoint(widget.width() - 8, widget.height() - 8))
        tb.process_events(150)
        assert set(stageviz.session().paths()) == set(self.proxies), "sweep retains every proxy identity"
        assert all(stageviz.session().stage().GetPrimAtPath(path).IsInstanceProxy() for path in self.proxies)
        self.center = (widget.width() // 2, widget.height() // 2)
        self.snapshot("swept")

    def matrices(self):
        stage = stageviz.session().stage()
        cache = UsdGeom.XformCache()
        return {path: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path))
                for path in self.roots + self.proxies}

    def drag(self, mode):
        QtCore, _, QtTest, _ = tb.qt()
        assert mode in ("Move", "Rotate", "Scale")
        self.activate_mode(mode)
        tb.process_events(100)
        before = self.matrices()
        stage = stageviz.session().stage()
        cache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_])
        bounds = Gf.Range3d()
        for path in self.proxies:
            bounds.UnionWith(cache.ComputeWorldBound(stage.GetPrimAtPath(path)).ComputeAlignedRange())
        pivot = bounds.GetMidpoint()
        command = stageviz.command
        # MCP clears undo history on entry. Exercise gesture/undo/redo together.
        command.clear()
        start = QtCore.QPoint(*self.center)
        dx, dy = (28, -8) if mode == "Move" else (32, -12)
        end = start + QtCore.QPoint(dx, dy)
        widget = self.viewport()
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=start)
        QtTest.QTest.mouseMove(widget, end, 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(180)
        after = self.matrices()
        delta = before[self.roots[0]].GetInverse() * after[self.roots[0]]
        assert not Gf.IsClose(delta, Gf.Matrix4d(1), 1e-6), mode + " gesture changes the stage"
        for path in self.roots + self.proxies:
            assert Gf.IsClose(after[path], before[path] * delta, 1e-5), path + " receives the shared world delta once"
        if mode == "Move":
            for row in range(3):
                for col in range(3):
                    assert abs(delta[row][col] - (1 if row == col else 0)) < 1e-6
            self.center = (end.x(), end.y())
        elif mode == "Scale":
            factor = math.exp((dx - dy) * 0.01)
            expected = (Gf.Matrix4d(1).SetTranslate(-pivot) * Gf.Matrix4d(1).SetScale(factor)
                        * Gf.Matrix4d(1).SetTranslate(pivot))
            assert Gf.IsClose(delta, expected, 1e-5), "uniform scaling uses one world delta about the shared pivot"
        else:
            assert Gf.IsClose(delta.Transform(pivot), pivot, 1e-5), (
                "rotation preserves the shared pivot: " + repr((pivot, delta.Transform(pivot))))
            for row in range(3):
                assert abs(Gf.Vec3d(*(delta[row][col] for col in range(3))).GetLength() - 1) < 1e-5
        assert command.can_undo(), "completed gesture registers an undoable command"
        def matches(expected):
            current = self.matrices()
            return all(Gf.IsClose(current[path], matrix, 1e-5) for path, matrix in expected.items())

        command.undo()
        assert tb.wait_until(lambda: matches(before))
        assert not command.can_undo(), "one undo reverses the entire gesture"
        command.redo()
        assert tb.wait_until(lambda: matches(after))
        assert set(stageviz.session().paths()) == set(self.proxies), "transform preserves proxy selection"
        self.snapshot(mode.lower())
        print("PASS viewport", mode, "shared delta, proxy targeting, undo and redo")

    def cleanup(self):
        if self.original_filename is None:
            return
        session = stageviz.session()
        if self.original_filename:
            assert session.loadFromFile(self.original_filename)
            stageviz.command.select_paths(self.original_selection)
        else:
            assert session.newStage()
        stageviz.command.clear()
        tb.process_events(150)
        assert session.filename() == self.original_filename
        self.snapshot("restored")
        print("Viewport test artifacts:", self.directory)


def run(disposable=False):
    test = ViewportTest(disposable=disposable)
    try:
        test.setup()
        test.sweep()
        for mode in ("Move", "Rotate", "Scale"):
            test.drag(mode)
        return True
    finally:
        test.cleanup()


if globals().get("_VIEWPORT_AUTORUN", True):
    _TEST_RESULT = run(disposable=globals().get("_VIEWPORT_DISPOSABLE", False))
