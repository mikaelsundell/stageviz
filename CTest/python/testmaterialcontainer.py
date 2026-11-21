# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""New materials retain a defined container when another material layer is muted."""
from pathlib import Path
import json
import runpy

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_helpers = runpy.run_path(str(Path(__file__).with_name('testtooltips.py')))
tb = _helpers['tb']


class MaterialContainerTest(_helpers['TooltipsTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.first_path = self.directory / 'layer1.usda'
        self.second_path = self.directory / 'layer2.usda'
        first = Sdf.Layer.CreateNew(str(self.first_path))
        first.Save()
        second = Usd.Stage.CreateNew(str(self.second_path))
        UsdGeom.Scope.Define(second, '/Materials')
        material = UsdShade.Material.Define(second, '/Materials/Original')
        shader = UsdShade.Shader.Define(second, '/Materials/Original/Surface')
        shader.CreateIdAttr('UsdPreviewSurface')
        material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
        second.GetRootLayer().Save()
        self.fixture = self.directory / 'container.usda'
        root = Sdf.Layer.CreateNew(str(self.fixture))
        root.subLayerPaths = [str(self.first_path), str(self.second_path)]
        root.Save()
        del first, second, root, material, shader
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        assert stageviz.session().setEditLayer(str(self.first_path)) and tb.wait_for_commands()

    def create(self):
        _, _, _, QtWidgets = tb.qt()
        assert tb.trigger_action('materialEditor')
        assert tb.wait_until(lambda: any(w.objectName() == 'materialDialog' and w.isVisible() for w in tb.all_widgets()))
        dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog' and w.isVisible())
        browser = dialog.findChild(QtWidgets.QWidget, 'browserWidget')
        assert browser is not None
        browser.createMaterialRequested.emit('UsdPreviewSurface')
        assert tb.wait_for_commands()
        stage = stageviz.session().stage()
        children = [p for p in stage.GetPrimAtPath('/Materials').GetChildren() if p.GetName() != 'Original']
        assert len(children) == 1
        self.path = str(children[0].GetPath())
        self.created = stage.GetEditTarget().GetLayer().ExportToString()
        assert stage.GetEditTarget().GetLayer().GetPrimAtPath('/Materials').specifier == Sdf.SpecifierDef
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetEditTarget().GetLayer().GetPrimAtPath('/Materials') is None
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stage.GetEditTarget().GetLayer().ExportToString() == self.created
        self.snapshot('container_created_redone')
        self.checkpoint()

    def checkpoint(self):
        print('CONTAINER_CHECKPOINT', json.dumps({'path': self.path, 'layer': str(self.first_path)}))

    def mute(self):
        stageviz.command.set_layer_muted(str(self.second_path), True)
        assert tb.wait_for_commands()
        stage = stageviz.session().stage()
        assert stage.IsLayerMuted(str(self.second_path))
        assert not stage.GetPrimAtPath('/Materials/Original')
        assert stage.GetPrimAtPath(self.path).IsDefined()
        assert stage.GetPrimAtPath('/Materials').GetTypeName() == 'Scope'
        assert any(str(p.GetPath()) == self.path for p in stage.Traverse())
        _, _, _, QtWidgets = tb.qt()
        dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog' and w.isVisible())
        icons = dialog.findChild(QtWidgets.QListWidget, 'icons')
        assert tb.wait_until(lambda: icons.count() == 1)
        self.row(self.path)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert not stage.IsLayerMuted(str(self.second_path))
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stage.IsLayerMuted(str(self.second_path))
        assert tb.wait_until(lambda: icons.count() == 1)
        self.snapshot('container_original_layer_muted')
        assert dialog.grab().save(str(self.directory / 'material-browser-layer-muted.png'))
        self.checkpoint()

    def restore(self):
        _, _, _, QtWidgets = tb.qt()
        stage = stageviz.session().stage()
        dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog' and w.isVisible())
        icons = dialog.findChild(QtWidgets.QListWidget, 'icons')
        stageviz.command.set_layer_muted(str(self.second_path), False)
        assert tb.wait_for_commands()
        assert not stage.IsLayerMuted(str(self.second_path))
        assert tb.wait_until(lambda: icons.count() == 2)
        self.checkpoint()
