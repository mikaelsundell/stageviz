# SPDX-License-Identifier: BSD-3-Clause
"""Coincident selection regression and opt-in synthetic viewport benchmark.

Run only in a disposable process. The external controller can call setup(),
overlap(), performance(), and cleanup() separately to record independent MCP
state. Baseline mode records the known overlap failure without accepting it in
the normal regression.
"""

import contextlib
import io
import json
from pathlib import Path
import random
import statistics
import sys
import tempfile
import time

import stageviz
from pxr import Gf, Usd, UsdGeom

try:
    from PySide6 import QtOpenGLWidgets
except ImportError:
    from PyQt6 import QtOpenGLWidgets

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


class SelectionOutlineTest:
    def __init__(self, disposable=False, output=None, baseline=False):
        assert disposable, "selection tests require an explicitly disposable process"
        self.directory = Path(output or tempfile.mkdtemp(prefix="stageviz-selection-outline-"))
        self.directory.mkdir(parents=True, exist_ok=True)
        self.baseline = baseline
        self.results = {}

    def viewport(self):
        return next(w for w in tb.all_widgets() if w.objectName() == "imagingGLWidget" and w.isVisible())

    def select(self, paths):
        stageviz.command.select_paths(paths)
        assert tb.wait_for_commands()
        assert list(stageviz.session().paths()) == paths

    def image(self):
        self.viewport().repaint()
        return self.viewport().grabFramebuffer()

    def selected_pixels(self):
        image = self.image()
        # The test sets a unique saturated color, avoiding lighting/background.
        rgba = image.convertToFormat(image.Format.Format_RGBA8888)
        bits = rgba.constBits()
        if hasattr(bits, 'setsize'):
            bits.setsize(rgba.sizeInBytes())
        data = bytes(bits)
        count = sum(r > 220 and g < 70 and b > 220
                    for r, g, b in zip(data[0::4], data[1::4], data[2::4]))
        return count

    def snapshot(self, label):
        assert tb.viewer().grab().save(str(self.directory / (label + ".png")))
        with contextlib.redirect_stdout(io.StringIO()):
            state = tb.snapshot_qt_state(label)
        state["selectedPaths"] = list(stageviz.session().paths())
        (self.directory / (label + ".json")).write_text(json.dumps(state, indent=2))

    def setup(self):
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.original_color = stageviz.ui().color(stageviz.UI.ColorRole.Selection)
        stageviz.ui().setColor(stageviz.UI.ColorRole.Selection, 255, 0, 255)
        fixture = Usd.Stage.CreateNew(str(self.directory / "overlap.usda"))
        world = UsdGeom.Xform.Define(fixture, "/World")
        fixture.SetDefaultPrim(world.GetPrim())
        UsdGeom.SetStageUpAxis(fixture, UsdGeom.Tokens.y)
        UsdGeom.SetStageMetersPerUnit(fixture, 1.0)
        for name in ("A", "B"):
            mesh = UsdGeom.Mesh.Define(fixture, "/World/" + name)
            mesh.GetPointsAttr().Set([(-2, 0, -2), (-2, 0, 2), (2, 0, 2), (2, 0, -2)])
            mesh.GetExtentAttr().Set([(-2, 0, -2), (2, 0, 2)])
            mesh.GetFaceVertexCountsAttr().Set([4])
            mesh.GetFaceVertexIndicesAttr().Set([0, 1, 2, 3])
            mesh.GetSubdivisionSchemeAttr().Set("none")
            mesh.GetDoubleSidedAttr().Set(True)
            mesh.GetNormalsAttr().Set([(0, 1, 0)])
            mesh.SetNormalsInterpolation("constant")
        fixture.GetRootLayer().Save()
        fixture = None
        assert stageviz.session().loadFromFile(str(self.directory / "overlap.usda"))
        state = stageviz.session().viewState()
        state.setGridEnabled(False)
        state.setAmbientOcclusionEnabled(False)
        state.setSceneStatsEnabled(False)
        state.setPerformanceStatsEnabled(False)
        state.camera().setCameraView(stageviz.CameraViewTop)
        state.camera().frame(stageviz.session().boundingBox())
        tb.process_events(200)
        self.snapshot("before-selection")

    def overlap(self):
        pixels = {}
        for name in ("A", "B"):
            self.select(["/World/" + name])
            pixels[name] = self.selected_pixels()
            self.snapshot("selected-" + name)
        self.select(["/World/A", "/World/B"])
        pixels["both"] = self.selected_pixels()
        self.select([])
        pixels["none"] = self.selected_pixels()
        self.results["overlapPixels"] = pixels
        self.write_results()
        assert pixels["none"] == 0 and pixels["both"] > 20, pixels
        if not self.baseline:
            assert min(pixels["A"], pixels["B"]) > 20, pixels
        # A genuinely hidden selected plane must stay hidden. Use commands and
        # verify undo/redo before restoring the coincident baseline.
        stageviz.command.hide_paths(["/World/A"])
        assert tb.wait_for_commands()
        self.select(["/World/A"])
        assert self.selected_pixels() == 0, "invisible geometry must not acquire an outline"
        stageviz.command.undo()  # selection
        assert tb.wait_for_commands()
        stageviz.command.undo()  # visibility
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath("/World/A").GetAttribute("visibility").Get() != "invisible"
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath("/World/A").GetAttribute("visibility").Get() == "invisible"
        stageviz.command.undo()
        assert tb.wait_for_commands()
        before = tuple(tuple(float(i == j) for j in range(4)) for i in range(4))
        after = [list(row) for row in before]
        after[3][1] = .2
        stageviz.command.set_transforms(["/World/A"], [before], [after])
        assert tb.wait_for_commands()
        self.select(["/World/A"])
        assert self.selected_pixels() == 0, "geometry behind an occluder must not acquire an outline"
        self.snapshot("occluded-selection")
        self.results["occludedPixels"] = 0
        self.write_results()

    def performance(self, count=10000, samples=60):
        # Direct USD authoring is confined to a synthetic fixture file, before
        # loading it into the live session. All measured edits use commands.
        path = self.directory / ("synthetic-%d.usda" % count)
        width = int(count ** 0.5)
        paths = ["/World/Group/C%05d" % index for index in range(count)]
        lines = ['#usda 1.0', '(', 'defaultPrim = "World"', 'upAxis = "Y"', 'metersPerUnit = 1', ')',
                 'def Xform "World" {', 'def Xform "Group" {']
        for index in range(count):
            lines.extend(['def Cube "C%05d" {' % index,
                          'double size = .8',
                          'float3[] extent = [(-.4,-.4,-.4),(.4,.4,.4)]',
                          'double3 xformOp:translate = (%d,0,%d)' % (index % width, index // width),
                          'uniform token[] xformOpOrder = ["xformOp:translate"]', '}'])
        lines.extend(['}', 'def Xform "Target" {}', '}'])
        path.write_text('\n'.join(lines) + '\n')
        assert stageviz.session().loadFromFile(str(path))
        camera = stageviz.session().viewState().camera()
        camera.setCameraView(stageviz.CameraViewTop)
        camera.frame(stageviz.session().boundingBox())
        tb.process_events(200)
        tree = next(w for w in tb.all_widgets() if w.objectName() == "outlinerTree" and w.isVisible())
        tree.expandAll()
        model = tree.model()
        pending = [model.index(i, 0) for i in range(model.rowCount())]
        populated = False
        while pending:
            index = pending.pop()
            if model.data(index) == "Group":
                assert model.rowCount(index) == count and tree.isExpanded(index)
                populated = True
                break
            pending.extend(model.index(i, 0, index) for i in range(model.rowCount(index)))
        assert populated, "benchmark requires every synthetic object in an expanded Outliner branch"
        # Synchronize GPU completion through framebuffer readback. Report it
        # explicitly as redraw+readback wall time, rather than GPU-only timing.
        rng = random.Random(814)
        rows = {}
        for label, selected in (("empty", []), ("single", [paths[count // 2]]),
                                ("many", paths[::10]), ("parent", ["/World/Group"])):
            self.select(selected)
            for _ in range(8):
                self.image()
            timings = []
            for _ in range(samples):
                start = time.perf_counter()
                self.image()
                timings.append((time.perf_counter() - start) * 1000)
            rows[label] = {"medianMs": statistics.median(timings),
                           "p95Ms": sorted(timings)[int(.95 * (len(timings) - 1))], "samplesMs": timings}
        timings = []
        for index in rng.sample(range(count), min(samples, count)):
            start = time.perf_counter()
            self.select([paths[index]])
            self.image()
            timings.append((time.perf_counter() - start) * 1000)
        rows["randomSelection"] = {"medianMs": statistics.median(timings), "samplesMs": timings}
        # Parent moves exercise notices, a populated Outliner and selection
        # cache invalidation. Undo restores the fixture after each measured move.
        rows["parentMoveMs"] = []
        before = Gf.Matrix4d(1)
        after = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(.1, 0, 0))
        matrix = lambda m: tuple(tuple(float(m[r][c]) for c in range(4)) for r in range(4))
        self.select(["/World/Group"])
        for _ in range(5):
            start = time.perf_counter()
            stageviz.command.set_transforms(["/World/Group"], [matrix(before)], [matrix(after)])
            assert tb.wait_for_commands()
            self.image()
            rows["parentMoveMs"].append((time.perf_counter() - start) * 1000)
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(
                stageviz.session().stage().GetPrimAtPath('/World/Group')), before, 1e-10)
        self.results["performance-%d" % count] = rows
        self.results["viewport"] = [self.viewport().width(), self.viewport().height(), self.viewport().devicePixelRatioF()]
        self.write_results()
        self.snapshot("synthetic-%d" % count)

    def write_results(self):
        (self.directory / "results.json").write_text(json.dumps(self.results, indent=2))

    def cleanup(self):
        stageviz.ui().setColor(stageviz.UI.ColorRole.Selection, *self.original_color)
        if self.original_filename:
            assert stageviz.session().loadFromFile(self.original_filename)
            self.select(self.original_selection)
        else:
            assert stageviz.session().newStage()
        self.write_results()


def run(disposable=False):
    test = SelectionOutlineTest(disposable=disposable)
    try:
        test.setup()
        test.overlap()
    finally:
        test.cleanup()
