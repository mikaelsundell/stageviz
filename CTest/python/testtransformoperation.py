# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Check the shared Python/gizmo transform boundary in a disposable viewer.

Load with runpy, construct TransformOperationTest(disposable=True), and call
setup(), api(mode), gizmo(mode), conflicts(), and cleanup() from an external MCP
controller. Each action checks its own undo/redo because MCP Python entry clears
history. This module does not start tests automatically.
"""

from pathlib import Path
import runpy

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom

_namespace = runpy.run_path(str(Path(__file__).with_name("testsubsets.py")),
                           init_globals={"_SUBSETS_AUTORUN": False})
tb = _namespace["tb"]


class TransformOperationTest(_namespace["SubsetTest"]):
    def setup(self):
        assert self.disposable, "transform operation tests require a disposable viewer"
        super().setup()
        stage = stageviz.session().stage()
        other = UsdGeom.Xform.Define(stage, "/World/Unselected")
        other.GetPrim().GetReferences().AddInternalReference("/World/Plain")
        other.GetPrim().SetInstanceable(True)
        self.selected = [owner + "/" + subset for owner in self.owners for subset in ("Front", "Back")]
        self.observed = list(self.owners) + ["/World/Unselected/Mesh"]
        stageviz.command.select_paths(self.selected)
        tb.process_events(100)
        stageviz.command.clear()
        self.snapshot("transform_operation_loaded")

    def worlds(self):
        stage = stageviz.session().stage()
        cache = UsdGeom.XformCache()
        return {path: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)) for path in self.observed}

    def layer_text(self):
        return stageviz.session().stage().GetEditTarget().GetLayer().ExportToString()

    def matches(self, expected):
        actual = self.worlds()
        return all(Gf.IsClose(actual[path], matrix, 1e-6) for path, matrix in expected.items())

    def finish_action(self, before, original, label, expected_delta=None, individual_rotation=False):
        after = self.worlds()
        delta = before[self.owners[0]].GetInverse() * after[self.owners[0]]
        assert not Gf.IsClose(delta, Gf.Matrix4d(1), 1e-6), label + " changes geometry"
        if expected_delta is not None:
            assert Gf.IsClose(delta, expected_delta, 1e-6), label + " applies requested world delta"
        for owner in self.owners:
            if individual_rotation:
                assert Gf.IsClose(after[owner].ExtractTranslation(), before[owner].ExtractTranslation(), 1e-6), (
                    owner + ' keeps its own rotation pivot')
                owner_delta = before[owner].GetInverse() * after[owner]
                for row in range(3):
                    for col in range(3):
                        assert abs(owner_delta[row][col] - delta[row][col]) < 1e-6
            else:
                assert Gf.IsClose(after[owner], before[owner] * delta, 1e-6), owner + " receives the delta exactly once"
        assert Gf.IsClose(after[self.observed[-1]], before[self.observed[-1]], 1e-6), "unselected instance stays fixed"
        assert list(stageviz.session().paths()) == self.selected, "Session keeps exact subset paths"
        assert not stageviz.session().stage().GetPrimAtPath(self.owners[0]).GetAttribute("xformOp:transform"), (
            "source mesh contents remain unmodified")
        authored = self.layer_text()
        assert stageviz.command.can_undo()
        stageviz.command.undo()
        assert tb.wait_until(lambda: self.matches(before) and self.layer_text() == original)
        assert not stageviz.command.can_undo(), "one operation produces one undo step"
        stageviz.command.redo()
        assert tb.wait_until(lambda: self.matches(after) and self.layer_text() == authored)
        assert list(stageviz.session().paths()) == self.selected
        self.snapshot(label)
        print("PASS", label, "shared target resolution, one delta, selection, undo and redo")

    def api(self, mode):
        stageviz.command.select_paths(self.selected)
        tb.process_events(80)
        stageviz.command.clear()
        before = self.worlds()
        original = self.layer_text()
        if mode == "Move":
            delta = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(0.3, 0.4, 0.2))
        elif mode == "Rotate":
            delta = Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d(0, 0, 1), 17))
        else:
            assert mode == "Scale"
            delta = Gf.Matrix4d(1).SetScale(Gf.Vec3d(1.1, 0.9, 1.2))
        matrices = [before[str(Sdf.Path(path).GetParentPath())] for path in self.selected]
        as_rows = lambda matrix: [list(row) for row in matrix]
        stageviz.command.set_transforms(self.selected, [as_rows(matrix) for matrix in matrices],
                                       [as_rows(matrix * delta) for matrix in matrices])
        expected = {path: matrix * delta if path in self.owners else matrix for path, matrix in before.items()}
        assert tb.wait_until(lambda: self.matches(expected))
        self.finish_action(before, original, "python_" + mode.lower(), delta)

    def gizmo(self, mode):
        QtCore, _, QtTest, _ = tb.qt()
        stageviz.command.select_paths(self.selected)
        tb.process_events(80)
        self.activate_mode(mode)
        self.frame_handle()
        before = self.worlds()
        original = self.layer_text()
        stageviz.command.clear()
        widget = self.viewport()
        start = QtCore.QPoint(widget.width() // 2, widget.height() // 2)
        end = start + QtCore.QPoint(28, -8)
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=start)
        QtTest.QTest.mouseMove(widget, end, 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(180)
        self.finish_action(before, original, "gizmo_" + mode.lower(), individual_rotation=mode == "Rotate")

    def conflicts(self):
        original = self.layer_text()
        matrix = self.worlds()[self.owners[0]]
        moved = matrix * Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(1, 0, 0))
        as_rows = lambda value: [list(row) for row in value]
        stageviz.command.set_transforms(self.selected[:2], [as_rows(matrix), as_rows(matrix)],
                                       [as_rows(moved), as_rows(matrix)])
        tb.process_events(100)
        assert self.layer_text() == original, "conflicting subset transforms are rejected before any authoring"
        assert list(stageviz.session().paths()) == self.selected
        print("PASS conflicting subset deltas leave stage and selection unchanged")

    def composed_batch(self, count=128):
        """Exercise newly authored matrix ops through the public Python API."""
        assert self.disposable
        stage = stageviz.session().stage()
        root = stage.GetRootLayer()
        self.composed_source = Usd.Stage.CreateInMemory()
        paths = ["/Composed/Batch/P" + str(i) for i in range(count)]
        for i, path in enumerate(paths):
            cube = UsdGeom.Cube.Define(self.composed_source, path)
            cube.AddTranslateOp().Set(Gf.Vec3d(i % 16, i // 16, 0))
        source_text = self.composed_source.GetRootLayer().ExportToString()
        # Direct USD authoring constructs this anonymous test fixture. The
        # operation under test below goes through the public undoable command.
        root.subLayerPaths = list(root.subLayerPaths) + [self.composed_source.GetRootLayer().identifier]
        Sdf.CreatePrimInLayer(root, "/Composed")
        assert stageviz.session().setEditLayer(root.identifier)
        stageviz.command.select_paths(paths)
        tb.process_events(80)
        stageviz.command.clear()
        original = root.ExportToString()
        cache = UsdGeom.XformCache()
        before = [cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)) for path in paths]
        delta = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(0.3, 0.4, 0.2))
        after = [matrix * delta for matrix in before]
        as_rows = lambda matrix: [list(row) for row in matrix]
        def matches(matrices):
            cache = UsdGeom.XformCache()
            return all(Gf.IsClose(cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)), matrix, 1e-7)
                       for path, matrix in zip(paths, matrices))
        stageviz.command.set_transforms(paths, list(map(as_rows, before)), list(map(as_rows, after)))
        assert tb.wait_until(lambda: matches(after))
        authored = root.ExportToString()
        assert authored != original
        assert list(stageviz.session().paths()) == paths
        self.snapshot("composed_batch_applied")
        stageviz.command.undo()
        assert tb.wait_until(lambda: matches(before) and root.ExportToString() == original)
        assert not stageviz.command.can_undo(), "bulk transform creates one undo step"
        stageviz.command.redo()
        assert tb.wait_until(lambda: matches(after) and root.ExportToString() == authored)
        self.snapshot("composed_batch_redone")
        stageviz.command.undo()
        assert tb.wait_until(lambda: matches(before) and root.ExportToString() == original)
        assert self.composed_source.GetRootLayer().ExportToString() == source_text
        self.snapshot("composed_batch_restored")
        print("PASS composed batch", count, "public command, exact undo, redo, and unchanged source")
