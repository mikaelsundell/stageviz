# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Rename an active-layer material beneath a stronger scope; inspect layer provenance."""
from pathlib import Path
import json
import runpy

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_helpers = runpy.run_path(str(Path(__file__).with_name('testtooltips.py')))
tb = _helpers['tb']


class MaterialLayerRenameTest(_helpers['TooltipsTest']):
    path = '/Materials/Material2'

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.first_path = self.directory / 'layer1.usda'
        self.second_path = self.directory / 'layer2.usda'
        first = Usd.Stage.CreateNew(str(self.first_path))
        material = UsdShade.Material.Define(first, self.path)
        shader = UsdShade.Shader.Define(first, self.path + '/Surface')
        shader.CreateIdAttr('UsdPreviewSurface')
        material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
        first.GetPrimAtPath('/Materials').SetChildrenReorder(['Material2'])
        first.GetRootLayer().Save()
        second = Sdf.Layer.CreateNew(str(self.second_path))
        second.Save()
        self.fixture = self.directory / 'scene.usda'
        root = Sdf.Layer.CreateNew(str(self.fixture))
        root.subLayerPaths = [str(self.first_path), str(self.second_path)]
        scene = Usd.Stage.Open(root)
        UsdGeom.Scope.Define(scene, '/Materials')
        shape = UsdGeom.Cube.Define(scene, '/World/Shape')
        UsdShade.MaterialBindingAPI.Apply(shape.GetPrim()).Bind(UsdShade.Material(scene.GetPrimAtPath(self.path)))
        root.Save()
        del first, material, shader, second, scene, shape, root
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        assert stageviz.session().setEditLayer(str(self.first_path)) and tb.wait_for_commands()
        stage = stageviz.session().stage()
        self.original = {layer.identifier:layer.ExportToString() for layer in stage.GetLayerStack()}
        assert stage.GetPrimAtPath('/Materials').GetPrimStack()[0].layer == stage.GetRootLayer()
        assert stage.GetPrimAtPath(self.path).GetPrimStack()[0].layer == stage.GetEditTarget().GetLayer()
        self.checkpoint(self.path)
        self.snapshot('layer_rename_loaded')

    def checkpoint(self, path):
        stage = stageviz.session().stage()
        prim = stage.GetPrimAtPath(path)
        defining = next(spec.layer.identifier for spec in prim.GetPrimStack() if spec.specifier == Sdf.SpecifierDef)
        print('RENAME_CHECKPOINT', json.dumps({'path':path, 'definingLayer':defining,
              'strongestLayer':prim.GetPrimStack()[0].layer.identifier, 'editLayer':stage.GetEditTarget().GetLayer().identifier}))

    def exercise(self):
        QtCore, _, _, QtWidgets = tb.qt()
        assert tb.trigger_action('materialEditor')
        assert tb.wait_until(lambda: any(w.objectName() == 'materialDialog' and w.isVisible() for w in tb.all_widgets()))
        dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog' and w.isVisible())
        icons = dialog.findChild(QtWidgets.QListWidget, 'icons')
        assert tb.wait_until(lambda: icons.count() == 1)
        tooltip = icons.item(0).toolTip()
        assert 'Layer' in tooltip and 'layer1.usda' in tooltip
        self.capture_tooltip(icons.viewport(), icons.visualItemRect(icons.item(0)).center(), tooltip, 'material-owner')
        icons.item(0).setText('RenamedPaint')
        assert tb.wait_for_commands()
        stage = stageviz.session().stage()
        renamed = '/Materials/RenamedPaint'
        assert tb.wait_until(lambda: bool(stage.GetPrimAtPath(renamed)))
        assert not stage.GetPrimAtPath(self.path)
        assert UsdShade.MaterialBindingAPI(stage.GetPrimAtPath('/World/Shape')).ComputeBoundMaterial()[0].GetPath() == Sdf.Path(renamed)
        self.snapshot('material_renamed')
        self.checkpoint(renamed)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetPrimAtPath(self.path) and not stage.GetPrimAtPath(renamed)
        assert all(layer.ExportToString() == self.original[layer.identifier] for layer in stage.GetLayerStack())
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stage.GetPrimAtPath(renamed)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        dialog.hide()
        tree, row = self.row(self.path)
        name = row.data(0, QtCore.Qt.ItemDataRole.ToolTipRole)
        assert 'Layer' in name and 'layer1.usda' in name
        self.capture_tooltip(tree.viewport(), tree.visualItemRect(row).center(), name, 'prim-owner')
        assert tb.trigger_action('fileLayers')
        assert tb.wait_until(lambda: any(w.objectName() == 'layerDialog' and w.isVisible() for w in tb.all_widgets()))
        layer_dialog = next(w for w in tb.all_widgets() if w.objectName() == 'layerDialog' and w.isVisible())
        layer_tree = layer_dialog.findChild(QtWidgets.QTreeWidget, 'layerTree')
        layer_tree.expandAll()
        it = QtWidgets.QTreeWidgetItemIterator(layer_tree)
        while it.value() and 'layer1.usda' not in it.value().text(0):
            it += 1
        assert it.value() is not None
        row = it.value()
        layer_tree.scrollToItem(row)
        tooltip = row.toolTip(0)
        assert tooltip.startswith('<qt>') and 'Current edit layer' in tooltip and str(self.first_path.name) in tooltip
        self.capture_tooltip(layer_tree.viewport(), layer_tree.visualItemRect(row).center(), tooltip, 'layer-tooltip')
        layer_dialog.hide()
        self.checkpoint(self.path)
