# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Primitive mesh conversion, context-menu presentation and undo/redo."""
from pathlib import Path
import runpy

import stageviz
from pxr import UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testcontextmenu.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class ConvertMeshTest(_base['ContextMenuTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        assert stageviz.session().newStage()
        self.paths = []
        for kind in ('Cube', 'Sphere', 'Cone', 'Cylinder', 'Capsule', 'Plane'):
            self.command(stageviz.command.new_prim, '/World', kind, kind)
            self.paths.append('/World/' + kind)
        self.command(stageviz.command.select_paths, [self.paths[1]])
        self.snapshot('mesh_setup')

    def api(self):
        stage = stageviz.session().stage()
        before = stage.GetRootLayer().ExportToString()
        stageviz.command.convert_to_mesh(paths=self.paths, segments=12)
        assert tb.wait_for_commands()
        for path in self.paths:
            mesh = UsdGeom.Mesh(stage.GetPrimAtPath(path))
            assert mesh and mesh.GetPointsAttr().Get() and mesh.GetFaceVertexCountsAttr().Get()
            assert mesh.GetNormalsAttr().Get() and mesh.GetSubdivisionSchemeAttr().Get() == 'none'
        assert list(stageviz.session().paths()) == [self.paths[1]]
        self.snapshot('mesh_api_converted')
        self.command(stageviz.command.undo)
        assert stage.GetRootLayer().ExportToString() == before
        self.command(stageviz.command.redo)
        assert all(stage.GetPrimAtPath(path).IsA(UsdGeom.Mesh) for path in self.paths)
        self.command(stageviz.command.undo)
        assert stage.GetRootLayer().ExportToString() == before
        for value in (3, 257):
            try:
                stageviz.command.convert_to_mesh(self.paths, value)
            except ValueError:
                pass
            else:
                raise AssertionError('invalid segment count accepted')
        self.snapshot('mesh_api_undone')

    def availability(self):
        rows = self.menu([self.paths[1]])
        names = [row['name'] for row in rows]
        index = names.index('convertToMesh')
        assert rows[index]['text'] == 'Convert to Mesh' and rows[index]['enabled']
        assert rows[index - 1]['separator'] and rows[index + 1]['separator']
        assert names[index + 2] == 'primVisibilityMenu'
        assert not self.find(self.menu(['/World']), 'convertToMesh')['enabled']
        assert not self.find(self.menu([self.paths[1], '/World']), 'convertToMesh')['enabled']
        layer = stageviz.session().stage().GetEditTarget().GetLayer()
        layer.SetPermissionToEdit(False)
        try:
            assert not self.find(self.menu([self.paths[1]]), 'convertToMesh')['enabled']
        finally:
            layer.SetPermissionToEdit(True)

    def ui(self):
        _, _, _, QtWidgets = tb.qt()
        stage = stageviz.session().stage()
        state = stageviz.session().viewState()
        original_level = state.complexityLevel()
        sizes = []
        try:
            for level in (stageviz.ComplexityLow, stageviz.ComplexityMedium,
                          stageviz.ComplexityHigh, stageviz.ComplexityVeryHigh):
                state.setComplexityLevel(level)
                before = stage.GetRootLayer().ExportToString()
                self.menu([self.paths[1]], 'convertToMesh')
                assert tb.wait_for_commands()
                assert not any(isinstance(w, QtWidgets.QInputDialog) and w.isVisible()
                               for w in QtWidgets.QApplication.topLevelWidgets())
                mesh = UsdGeom.Mesh(stage.GetPrimAtPath(self.paths[1]))
                assert mesh and list(stageviz.session().paths()) == [self.paths[1]]
                points = mesh.GetPointsAttr().Get()
                sizes.append(len(points))
                self.snapshot('mesh_ui_complexity_' + str(level))
                self.command(stageviz.command.undo)
                assert stage.GetRootLayer().ExportToString() == before
                state.setComplexityLevel(stageviz.ComplexityLow)
                self.command(stageviz.command.redo)
                assert UsdGeom.Mesh(stage.GetPrimAtPath(self.paths[1])).GetPointsAttr().Get() == points
                self.command(stageviz.command.undo)
                assert stage.GetRootLayer().ExportToString() == before
            assert all(a < b for a, b in zip(sizes, sizes[1:])), sizes
        finally:
            state.setComplexityLevel(original_level)
        self.snapshot('mesh_ui_restored')

    def bake(self):
        self.command(stageviz.command.convert_to_mesh, [self.paths[1]])
        assert stageviz.session().stage().GetPrimAtPath(self.paths[1]).IsA(UsdGeom.Mesh)
        self.command(stageviz.command.hide_paths, [path for path in self.paths if path != self.paths[1]])
        stageviz.session().viewState().camera().frame(stageviz.session().boundingBox([self.paths[1]]))
        tb.process_events(100)
        self.snapshot('mesh_final')


def run(disposable=False):
    test = ConvertMeshTest(disposable=disposable)
    try:
        test.setup()
        test.api()
        test.availability()
        test.ui()
        test.bake()
    finally:
        test.cleanup()
