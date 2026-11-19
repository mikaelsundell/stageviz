# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Native-instance isolation preserves rendered geometry without leaking siblings."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile

import stageviz
from pxr import Gf, UsdGeom

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


class IsolationInstancesTest:
    def __init__(self, disposable=False, instanced=True):
        assert disposable, "isolation tests require a disposable app"
        self.directory = Path(tempfile.mkdtemp(prefix="stageviz-isolation-instances-"))
        self.session = stageviz.session()
        self.instanced = instanced

    def command(self, function, *args):
        function(*args)
        assert tb.wait_for_commands()

    def move(self, path, position):
        prim = self.session.stage().GetPrimAtPath(path)
        before = UsdGeom.XformCache().GetLocalToWorldTransform(prim)
        after = Gf.Matrix4d(before)
        after.SetTranslateOnly(Gf.Vec3d(*position))
        self.command(stageviz.command.set_transforms, [path], [before], [after])

    def setup(self):
        assert self.session.newStage()
        self.command(stageviz.command.new_prim, "/World", "Source", "Xform")
        self.command(stageviz.command.new_prim, "/World/Source", "Body", "Cube")
        self.command(stageviz.command.new_prim, "/World/Source", "Inner", "Xform")
        self.command(stageviz.command.new_prim, "/World/Source/Inner", "Body", "Cube")
        self.move("/World/Source/Inner", (0, 4, 0))
        duplicate = stageviz.command.instance_paths if self.instanced else stageviz.command.copy_paths
        self.command(duplicate, ["/World/Source/Inner"])
        self.inner = self.session.paths()[0].rsplit("/", 1)[1]
        self.move(self.session.paths()[0], (0, -4, 0))
        self.instances = []
        for x in (8, -8):
            self.command(duplicate, ["/World/Source"])
            path = self.session.paths()[0]
            assert self.session.stage().GetPrimAtPath(path).IsInstance() == self.instanced
            self.instances.append(path)
            self.move(path, (x, 0, 0))
        a, b = [self.session.stage().GetPrimAtPath(path) for path in self.instances]
        if self.instanced:
            assert a.GetPrototype() == b.GetPrototype(), "fixture shares a native prototype"
        else:
            assert not self.session.stage().GetPrototypes(), "ordinary fixture has no native instances"
        state = self.session.viewState()
        stageviz.ui().setColor(stageviz.UI.ColorRole.Selection, 255, 0, 255)
        state.setGridEnabled(False)
        state.setRenderMode(stageviz.RenderShaded)
        viewer = tb.viewer()
        viewer.resize(1400, 1000)
        viewer.show()
        tb.process_events(100)
        state.camera().frame(self.session.boundingBox())
        tb.process_events(250)
        self.original = self.session.stage().GetRootLayer().ExportToString()
        self.original_population = self.session.stage().GetPopulationMask()
        self.snapshot("baseline", [])

    def snapshot(self, label, mask):
        tb.process_events(100)
        capture = stageviz.render().capture()
        paths = capture.paths()
        assert paths, (label, "rendered geometry disappeared", mask)
        assert all(self.session.stage().GetPrimAtPath(path) for path in paths), (label, "non-USD path", paths)
        if mask:
            assert all(any(path == root or path.startswith(root + "/") for root in mask)
                       for path in paths), (label, "unselected geometry leaked", mask, paths)
        if label in ("instance", "redo"):
            viewport = next(widget for widget in tb.all_widgets()
                            if widget.objectName() == "imagingGLWidget" and widget.isVisible())
            viewport.repaint()
            image = viewport.grabFramebuffer()
            rgba = image.convertToFormat(image.Format.Format_RGBA8888)
            data = bytes(rgba.constBits())
            assert sum(r > 220 and g < 70 and b > 220 for r, g, b in
                       zip(data[0::4], data[1::4], data[2::4])) > 8, "isolated instance keeps its selection outline"
        assert list(self.session.mask()) == mask
        assert self.session.stage().GetRootLayer().ExportToString() == self.original
        assert self.session.stage().GetPopulationMask() == self.original_population
        capture.save(str(self.directory / (label + "-capture.png")))
        assert tb.viewer().grab().save(str(self.directory / (label + ".png")))
        with contextlib.redirect_stdout(io.StringIO()):
            snapshot = tb.snapshot_qt_state(label)
        snapshot["isolation"] = {"mask": mask, "paths": paths,
                                 "selection": list(self.session.paths())}
        (self.directory / (label + ".json")).write_text(json.dumps(snapshot, indent=2))
        print("ISOLATION_CHECKPOINT", json.dumps(snapshot["isolation"]))
        return set(paths)

    def isolate(self):
        a, b = self.instances
        self.command(stageviz.command.select_paths, [a])
        self.command(stageviz.command.isolate_paths, [a])
        paths = self.snapshot("instance", [a])
        assert a + "/Body" in paths
        viewport = next(widget for widget in tb.all_widgets()
                        if widget.objectName() == "imagingGLWidget" and widget.isVisible())
        viewport.repaint()
        shaded = viewport.grabFramebuffer()
        self.session.viewState().setRenderMode(stageviz.RenderWireframe)
        tb.process_events(100)
        viewport.repaint()
        wireframe = viewport.grabFramebuffer()
        assert not wireframe.isNull() and wireframe != shaded, "isolated wireframe changes the image"
        assert wireframe.save(str(self.directory / "instance-wireframe.png"))
        assert list(self.session.mask()) == [a]
        self.session.viewState().setRenderMode(stageviz.RenderShaded)
        self.command(stageviz.command.undo)
        self.snapshot("undo", [])
        self.command(stageviz.command.redo)
        self.snapshot("redo", [a])
        cases = [("proxy", [a + "/Body"]),
                 ("shared-instances", [a, b]),
                 ("mixed", [a, "/World/Source/Body"]),
                 ("nested", [a + "/Inner"]),
                 ("nested-cross", [a + "/Inner", b + "/" + self.inner]),
                 ("parent", ["/World"]),
                 ("ordinary", ["/World/Source/Body"])]
        for label, mask in cases:
            self.command(stageviz.command.isolate_paths, mask)
            self.snapshot(label, mask)
        self.command(stageviz.command.isolate_paths, [])
        paths = self.snapshot("cleared", [])
        assert {a + "/Body", b + "/Body", "/World/Source/Body"} <= paths

        # Exercise actual viewport picking after the rendering population has
        # changed, rather than only checking the captured object-ID mapping.
        self.command(stageviz.command.isolate_paths, [a])
        self.command(stageviz.command.select_paths, [])
        tb.process_events(100)
        frame = stageviz.render().capture()
        target = frame.paths().index(a + "/Body")
        pixels = [i for i, value in enumerate(memoryview(frame.objectIds()).cast("i")) if value == target]
        assert pixels
        pixel = pixels[len(pixels) // 2]
        width, _ = frame.size()
        viewport = next(widget for widget in tb.all_widgets()
                        if widget.objectName() == "imagingGLWidget" and widget.isVisible())
        QtCore, _, QtTest, _ = tb.qt()
        ratio = viewport.devicePixelRatioF()
        point = QtCore.QPoint(int((pixel % width) / ratio), int((pixel // width) / ratio))
        QtTest.QTest.mouseClick(viewport, QtCore.Qt.MouseButton.LeftButton,
                               QtCore.Qt.KeyboardModifier.NoModifier, point)
        assert tb.wait_until(lambda: list(self.session.paths()) == [a + "/Body"]), (
            "isolated proxy remains pickable", list(self.session.paths()), point.x(), point.y())
        self.snapshot("picked", [a])
        self.command(stageviz.command.isolate_paths, [])

    def cleanup(self):
        assert tb.wait_for_commands()
        assert self.session.newStage()
        stageviz.command.clear()
        print("Isolation instance artifacts:", self.directory)


def run(disposable=False):
    for instanced in (True, False):
        test = IsolationInstancesTest(disposable=disposable, instanced=instanced)
        try:
            test.setup()
            test.isolate()
        finally:
            test.cleanup()
