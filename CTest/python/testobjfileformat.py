# SPDX-License-Identifier: BSD-3-Clause
"""OBJ sample conversion, triangulation, transforms and remembered Qt settings."""
from pathlib import Path
import runpy
import sys

import stageviz
from pxr import Gf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


def meshes(stage):
    return [UsdGeom.Mesh(prim) for prim in stage.Traverse() if prim.IsA(UsdGeom.Mesh)]


def corners(stage):
    result = []
    for mesh in meshes(stage):
        points = mesh.GetPointsAttr().Get()
        indices = mesh.GetFaceVertexIndicesAttr().Get()
        uv = UsdGeom.PrimvarsAPI(mesh).GetPrimvar('st').ComputeFlattened()
        normals = mesh.GetNormalsAttr().Get()
        result.append([(tuple(points[index]), tuple(uv[i]), tuple(normals[i])) for i, index in enumerate(indices)])
    return result


class ObjFileFormatTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        support = Path(__file__).resolve().parents[2] / 'support'
        sys.path.insert(0, str(support))
        from fileformats import objformat, objloader, objsaver
        self.plugin, self.loader, self.saver = objformat, objloader, objsaver
        self.plugin.settings().remove(self.plugin.SETTINGS_GROUP)
        self.plugin.register()
        self.source = self.directory / 'shapes.obj'
        self.source.write_text('''# Synthetic square and concave polygon; repeated group names and negative indices
v 0 0 0
v 2 0 0
v 2 2 0
v 0 2 0
v 3 0 0
v 5 0 0
v 5 2 0
v 4 1 0
v 3 2 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vt 0 0
vt 1 0
vt 1 1
vt .5 .5
vt 0 1
vn 0 0 1
o Quad
f 1/1/1 2/2/1 3/3/1 4/4/1
g Quad
f -5/-5/-1 -4/-4/-1 -3/-3/-1 -2/-2/-1 -1/-1/-1
''')

    def roundtrip(self):
        assert stageviz.session().loadFromFile(str(self.source))
        scene = stageviz.session().stage()
        assert stageviz.session().filename() == ''
        assert [str(m.GetPath()) for m in meshes(scene)] == ['/World/Quad', '/World/Quad_1']
        assert [list(m.GetFaceVertexCountsAttr().Get()) for m in meshes(scene)] == [[4], [5]]
        original = corners(scene)
        self.stage = stageviz.session().snapshot()
        assert self.stage.GetRootLayer().anonymous
        exported = self.directory / 'roundtrip.obj'
        assert stageviz.plugins.writeFile(self.stage, str(exported))
        loaded = stageviz.plugins.readFile(str(exported))
        assert corners(loaded) == original, 'round trip retains points, face UVs and normals'
        assert [list(m.GetFaceVertexCountsAttr().Get()) for m in meshes(loaded)] == [[4], [5]]
        assert stageviz.session().filename() == ''
        stageviz.session().viewState().camera().frame(stageviz.session().boundingBox())
        tb.process_events(150)
        self.snapshot('obj_imported')
        assert stageviz.session().newStage()
        stageviz.command.merge_stage(str(self.source))
        assert tb.wait_for_commands()
        assert len(meshes(stageviz.session().stage())) == 2
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert not meshes(stageviz.session().stage())
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert len(meshes(stageviz.session().stage())) == 2

    def triangulation(self):
        scene = self.loader.load(str(self.source), {'triangulate': True})
        assert scene.GetRootLayer().anonymous
        assert [list(m.GetFaceVertexCountsAttr().Get()) for m in meshes(scene)] == [[3, 3], [3, 3, 3]]
        area = 0
        original = corners(self.stage)
        for mesh, values, allowed in zip(meshes(scene), corners(scene), original):
            assert all(value in allowed for value in values), 'triangulation preserves corner attributes'
            positions = [Gf.Vec3d(v[0]) for v in values]
            for start in range(0, len(positions), 3):
                a, b, c = positions[start:start + 3]
                area += Gf.Cross(b - a, c - a).GetLength() / 2
        assert abs(area - 7.0) < 1e-6, 'concave triangulation preserves polygon area'
        output = self.directory / 'triangles.obj'
        self.saver.save(self.stage, str(output), {'triangulate': True})
        restored = stageviz.plugins.readFile(str(output))
        assert sum(len(m.GetFaceVertexCountsAttr().Get()) for m in meshes(restored)) == 5
        assert all(n == 3 for m in meshes(restored) for n in m.GetFaceVertexCountsAttr().Get())
        assert stageviz.session().newStage()
        stageviz.command.merge_stage(scene)
        assert tb.wait_for_commands()
        self.snapshot('obj_triangulated')

    def transforms(self):
        scene = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(scene, '/Prototype')
        root.AddScaleOp().Set(Gf.Vec3f(-2, 3, 4))
        mesh = UsdGeom.Mesh.Define(scene, '/Prototype/Triangle')
        mesh.CreatePointsAttr([(0, 0, 0), (1, 0, 0), (0, 1, -1)])
        mesh.CreateFaceVertexCountsAttr([3])
        mesh.CreateFaceVertexIndicesAttr([0, 1, 2])
        mesh.CreateNormalsAttr([(0, 1, 1)])
        mesh.SetNormalsInterpolation(UsdGeom.Tokens.constant)
        instance = UsdGeom.Xform.Define(scene, '/Instance')
        instance.GetPrim().GetReferences().AddInternalReference('/Prototype')
        instance.GetPrim().SetInstanceable(True)
        inherited_scale = instance.GetOrderedXformOps()[0]
        translate = instance.AddTranslateOp()
        translate.Set(Gf.Vec3d(10, 0, 0))
        instance.SetXformOpOrder([translate, inherited_scale])
        output = self.directory / 'transformed.obj'
        assert stageviz.plugins.writeFile(scene, str(output))
        restored = stageviz.plugins.readFile(str(output))
        assert len(meshes(restored)) == 2, 'instance proxies exported'
        values = [tuple(p) for m in meshes(restored) for p in m.GetPointsAttr().Get()]
        assert (-2.0, 0.0, 0.0) in values and (8.0, 0.0, 0.0) in values
        expected = Gf.Vec3d(0, 1 / 3, 1 / 4).GetNormalized()
        for mesh in meshes(restored):
            for normal in mesh.GetNormalsAttr().Get():
                assert (Gf.Vec3d(normal) - expected).GetLength() < 1e-6
            points = mesh.GetPointsAttr().Get()
            a, b, c = [Gf.Vec3d(points[i]) for i in mesh.GetFaceVertexIndicesAttr().Get()]
            assert Gf.Dot(Gf.Cross(b - a, c - a), expected) > 0, 'reflection corrects winding'

    def failures(self):
        invalid = self.directory / 'invalid.obj'
        invalid.write_text('v 0 0 0\nf 1 2 0\n')
        before = stageviz.session().stage().GetRootLayer().ExportToString()
        try:
            stageviz.plugins.readFile(str(invalid))
        except ValueError as error:
            assert 'invalid.obj:2:' in str(error)
        else:
            raise AssertionError('Invalid OBJ indices accepted')
        assert stageviz.session().stage().GetRootLayer().ExportToString() == before
        scene = Usd.Stage.CreateInMemory()
        UsdGeom.Sphere.Define(scene, '/Sphere')
        target = self.directory / 'existing.obj'
        target.write_text('original')
        try:
            stageviz.plugins.writeFile(scene, str(target))
        except ValueError as error:
            assert 'non-mesh' in str(error)
        else:
            raise AssertionError('Analytic geometry silently dropped')
        assert target.read_text() == 'original'

    def dialog(self):
        core, _, _, widgets = tb.qt()
        def invoke(operation, expected, value, accepted, action=None):
            errors = []
            def respond():
                dialog = widgets.QApplication.activeModalWidget()
                try:
                    assert dialog.objectName() == 'objOptionsDialog'
                    checkbox = dialog.findChild(widgets.QCheckBox, 'triangulateCheckBox')
                    assert checkbox.isChecked() == expected
                    checkbox.setChecked(value)
                    assert checkbox.toolTip()
                    mode = 'export' if operation == 'export' else 'import'
                    assert dialog.grab().save(str(self.directory / ('obj_options_' + mode + '.png')))
                    self.snapshot('obj_options_' + operation)
                    buttons = dialog.findChild(widgets.QDialogButtonBox, 'buttonBox')
                    button = widgets.QDialogButtonBox.StandardButton.Ok if accepted else widgets.QDialogButtonBox.StandardButton.Cancel
                    buttons.button(button).click()
                except Exception as error:
                    errors.append(repr(error))
                    if dialog is not None:
                        dialog.reject()
            core.QTimer.singleShot(100, respond)
            result = action() if action else self.plugin.options(str(self.source), operation, tb.viewer())
            assert not errors, errors
            if action is None:
                assert result == ({'triangulate': value} if accepted else None)
            return result

        invoke('open', False, True, True)
        invoke('merge', True, False, False)
        invoke('open', True, True, True)
        invoke('export', False, True, True)
        preferences = self.plugin.settings()
        preferences.sync()
        assert preferences.value(self.plugin.SETTINGS_GROUP + '/import/triangulate', False, type=bool)
        assert preferences.value(self.plugin.SETTINGS_GROUP + '/export/triangulate', False, type=bool)
        assert preferences.status() == core.QSettings.Status.NoError
        self.settings_filename = preferences.fileName()
        assert Path(self.settings_filename).is_file(), 'preferences were serialized to disk'
        source = invoke('open', True, True, True,
                        lambda: stageviz.plugins.readFile(str(self.source), interactive=True))
        imported = source
        assert all(n == 3 for m in meshes(imported) for n in m.GetFaceVertexCountsAttr().Get())
        target = self.directory / 'dialog-export.obj'
        assert invoke('export', True, True, True,
                      lambda: stageviz.plugins.writeFile(self.stage, str(target), interactive=True))
        exported = stageviz.plugins.readFile(str(target))
        assert all(n == 3 for m in meshes(exported) for n in m.GetFaceVertexCountsAttr().Get())
        before = stageviz.session().stage().GetRootLayer().ExportToString()
        assert invoke('open', True, False, False,
                      lambda: stageviz.plugins.readFile(str(self.source), interactive=True)) is None
        assert stageviz.session().stage().GetRootLayer().ExportToString() == before
        assert self.plugin.settings().value(self.plugin.SETTINGS_GROUP + '/import/triangulate', False, type=bool)
        # Even with UI triangulation remembered, batch conversion remains polygonal.
        converted = stageviz.plugins.readFile(str(self.source))
        assert [list(m.GetFaceVertexCountsAttr().Get()) for m in meshes(converted)] == [[4], [5]]

    def cleanup(self):
        stageviz.plugins.unregisterFileFormat(self.plugin.NAME)
        self.plugin.settings().remove(self.plugin.SETTINGS_GROUP)
        super().cleanup()


def run(disposable=False):
    test = ObjFileFormatTest(disposable)
    try:
        test.setup()
        test.roundtrip()
        test.triangulation()
        test.transforms()
        test.failures()
        test.dialog()
    finally:
        test.cleanup()
