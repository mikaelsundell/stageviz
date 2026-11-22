# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Attribute history uses mapped targets and respects target-change invalidation."""
from pathlib import Path
import runpy
import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class AttributeTargetTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.fixture = self.directory / 'attribute_target.usda'
        fixture = Usd.Stage.CreateNew(str(self.fixture))
        prim = fixture.DefinePrim('/Asset', 'Xform')
        variants = prim.GetVariantSets().AddVariantSet('look')
        variants.AddVariant('A')
        variants.SetVariantSelection('A')
        with variants.GetVariantEditContext():
            prim.CreateAttribute('value', Sdf.ValueTypeNames.Double).Set(2.0)
            xform = UsdGeom.Xformable(prim)
            xform.AddTranslateOp(UsdGeom.XformOp.PrecisionDouble, 'pivot').Set(Gf.Vec3d(1, 0, 0))
            xform.AddRotateZOp().Set(30)
            xform.AddTranslateOp(UsdGeom.XformOp.PrecisionDouble, 'pivot', True)
            child = UsdGeom.Xform.Define(fixture, '/Asset/Matrix')
            child.AddTransformOp(UsdGeom.XformOp.PrecisionDouble, 'pivot').Set(
                Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(1, 0, 0)))
            child.AddRotateZOp().Set(30)
            child.AddTransformOp(UsdGeom.XformOp.PrecisionDouble, 'pivot', True)
        prim.CreateAttribute('xformOp:translate:outsideVariant', Sdf.ValueTypeNames.Double3).Set(Gf.Vec3d(7, 8, 9))
        fixture.GetRootLayer().Save()
        fixture = None
        assert stageviz.session().load(str(self.fixture))
        assert tb.wait_for_commands()
        stageviz.command.select_paths(['/Asset'])
        assert tb.wait_for_commands()
        self.snapshot('attribute_target_loaded')

    def exercise(self):
        stage = stageviz.session().stage()
        root = stage.GetRootLayer()
        path = Sdf.Path('/Asset.value')
        target = stage.GetPrimAtPath('/Asset').GetVariantSets().GetVariantSet('look').GetVariantEditTarget()
        spec_path = target.MapToSpecPath(path)
        stage.SetEditTarget(target)
        stageviz.command.set_attribute_value(str(path), 9.0)
        assert tb.wait_for_commands()
        assert root.GetAttributeAtPath(spec_path).default == 9.0
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert root.GetAttributeAtPath(spec_path).default == 2.0
        assert not root.GetAttributeAtPath(path)
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert root.GetAttributeAtPath(spec_path).default == 9.0
        assert not root.GetAttributeAtPath(path)
        assert stage.GetAttributeAtPath(path).Get() == 9.0
        self.snapshot('attribute_target_redone')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert root.GetAttributeAtPath(spec_path).default == 2.0
        self.snapshot('attribute_target_restored')
        # The stack deliberately invalidates history when the edit target changes.
        stageviz.command.redo()
        assert tb.wait_for_commands()
        stage.SetEditTarget(root)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert not stageviz.command.can_undo()
        assert root.GetAttributeAtPath(spec_path).default == 9.0
        stage.SetEditTarget(target)
        stageviz.command.set_attribute_value(str(path), 2.0)
        assert tb.wait_for_commands()

    def matrix_pivot_editor(self):
        core = tb.qt()[0]
        tree = tb.find_widget('propertyTree', tb.qt()[3].QTreeWidget)
        assert tree is not None and tree.isVisible()
        items = tree.findItems('xformOp:transform:pivot',
                               core.Qt.MatchFlag.MatchExactly | core.Qt.MatchFlag.MatchRecursive, 0)
        assert len(items) == 1
        group = items[0]
        group.setExpanded(True)
        row = group.child(3)
        row.setExpanded(True)
        editor = tree.itemWidget(row.child(0), 1)
        assert isinstance(editor, tb.qt()[3].QDoubleSpinBox)
        editor.value()
        return editor

    def pivot_history(self):
        stage = stageviz.session().stage()
        root = stage.GetRootLayer()
        stage.SetEditTarget(root)
        target = stage.GetPrimAtPath('/Asset').GetVariantSets().GetVariantSet('look').GetVariantEditTarget()
        stage.SetEditTarget(target)
        for path, matrix in (('/Asset', False), ('/Asset/Matrix', True)):
            stageviz.command.select_paths([path])
            assert tb.wait_for_commands()
            prim = stage.GetPrimAtPath(path)
            name = 'xformOp:transform:pivot' if matrix else 'xformOp:translate:pivot'
            attr = prim.GetAttribute(name)
            original = root.ExportToString()
            world = UsdGeom.XformCache().GetLocalToWorldTransform(prim)
            if matrix:
                # Exercise the real Property Editor path for a matrix value,
                # which is intentionally outside the simple Python converters.
                tb.release_qt_wrappers()
                assert tb.wait_until(lambda: abs(self.matrix_pivot_editor().value() - 1.0) < 1e-8)
                editor = self.matrix_pivot_editor()
                editor.setValue(4.0)
                editor.editingFinished.emit()
                del editor
            else:
                stageviz.command.set_attribute_value(str(attr.GetPath()), (4.0, 0.0, 0.0))
            assert tb.wait_for_commands()
            value = attr.Get()
            assert abs((value[3][0] if matrix else value[0]) - 4.0) < 1e-8, (path, value)
            assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(prim), world, 1e-8)
            authored = root.ExportToString()
            if matrix:
                assert tb.wait_until(lambda: abs(self.matrix_pivot_editor().value() - 4.0) < 1e-8)
            self.snapshot('variant_pivot_edited_' + str(matrix))
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert root.ExportToString() == original, 'undo restores exact mapped transform specs'
            assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(prim), world, 1e-8)
            if matrix:
                assert tb.wait_until(lambda: abs(self.matrix_pivot_editor().value() - 1.0) < 1e-8)
            self.snapshot('variant_pivot_undone_' + str(matrix))
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert root.ExportToString() == authored, 'redo restores exact mapped pivot edit'
            assert Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(prim), world, 1e-8)
            if matrix:
                assert tb.wait_until(lambda: abs(self.matrix_pivot_editor().value() - 4.0) < 1e-8)
            self.snapshot('variant_pivot_redone_' + str(matrix))
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert root.ExportToString() == original
