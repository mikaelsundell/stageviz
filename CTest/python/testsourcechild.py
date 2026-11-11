# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Edit an ordinary source child while retaining native instance sharing."""
from pathlib import Path
import runpy

import stageviz
from pxr import Gf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class SourceChildTest(_base['ViewportTest']):
    source = '/World/Source'
    child = '/World/Source/Sphere'
    instance = '/World/Instance'
    observed = [source, child, source + '/Cube', instance, instance + '/Sphere', instance + '/Cube']

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        path = self.directory / 'source.usda'
        stage = Usd.Stage.CreateNew(str(path))
        source = UsdGeom.Xform.Define(stage, self.source)
        source.GetPrim().SetInstanceable(True)
        UsdGeom.Sphere.Define(stage, self.child).AddTranslateOp().Set(Gf.Vec3d(0, 3, 0))
        UsdGeom.Cube.Define(stage, self.source + '/Cube').AddTranslateOp().Set(Gf.Vec3d(0, -3, 0))
        instance = UsdGeom.Xform.Define(stage, self.instance)
        assert instance.GetPrim().GetReferences().AddInternalReference(self.source)
        instance.GetPrim().SetInstanceable(True)
        instance.AddTranslateOp().Set(Gf.Vec3d(8, 0, 0))
        assert stage.GetRootLayer().Save()
        del stage, source, instance
        assert stageviz.session().load(str(path))
        assert tb.wait_for_commands()
        stageviz.command.select_paths([self.child])
        assert tb.wait_for_commands()
        self.activate_mode('Move')
        self.snapshot('source_child_loaded')

    def worlds(self):
        stage = stageviz.session().stage()
        cache = UsdGeom.XformCache()
        return {p: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(p)) for p in self.observed}

    def matches(self, expected):
        return all(Gf.IsClose(self.worlds()[p], m, 1e-6) for p, m in expected.items())

    def finish(self, before, original, label):
        after = self.worlds()
        assert not Gf.IsClose(before[self.child], after[self.child], 1e-6)
        for path in (self.source, self.source + '/Cube', self.instance, self.instance + '/Cube'):
            assert Gf.IsClose(before[path], after[path], 1e-6), path + ' stays fixed'
        assert Gf.IsClose(after[self.instance + '/Sphere'],
                          after[self.child] * before[self.source].GetInverse() * before[self.instance], 1e-6)
        assert stageviz.session().stage().GetPrimAtPath(self.instance).IsInstance()
        assert list(stageviz.session().paths()) == [self.child]
        root = stageviz.session().stage().GetRootLayer()
        authored = root.ExportToString()
        self.snapshot(label)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert self.matches(before) and root.ExportToString() == original
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert self.matches(after) and root.ExportToString() == authored
        self.snapshot(label + '_redone')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert self.matches(before) and root.ExportToString() == original

    def api(self, mode):
        before = self.worlds()
        original = stageviz.session().stage().GetRootLayer().ExportToString()
        delta = {'Move': Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(.5, .3, .2)),
                 'Rotate': Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d(0, 0, 1), 20)),
                 'Scale': Gf.Matrix4d(1).SetScale(Gf.Vec3d(1.2, .8, 1.1))}[mode]
        rows = lambda m: [list(row) for row in m]
        stageviz.command.set_transforms([self.child], [rows(before[self.child])], [rows(before[self.child] * delta)])
        assert tb.wait_for_commands()
        assert Gf.IsClose(self.worlds()[self.child], before[self.child] * delta, 1e-6)
        self.finish(before, original, 'source_child_' + mode.lower())

    def gizmo(self):
        QtCore, _, QtTest, _ = tb.qt()
        self.activate_mode('Move')
        pivot = self.frame_handle()
        assert Gf.IsClose(pivot, self.worlds()[self.child].ExtractTranslation(), 1e-6), 'gizmo belongs to source child'
        before = self.worlds()
        original = stageviz.session().stage().GetRootLayer().ExportToString()
        stageviz.command.clear()
        viewport = self.viewport()
        start = QtCore.QPoint(viewport.width() // 2, viewport.height() // 2)
        end = start + QtCore.QPoint(28, -8)
        QtTest.QTest.mousePress(viewport, QtCore.Qt.MouseButton.LeftButton, pos=start)
        QtTest.QTest.mouseMove(viewport, end, 50)
        QtTest.QTest.mouseRelease(viewport, QtCore.Qt.MouseButton.LeftButton, pos=end)
        assert tb.wait_for_commands()
        self.finish(before, original, 'source_child_gizmo')
