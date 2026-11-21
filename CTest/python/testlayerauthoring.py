# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Explicit overs and material network replacement without layer muting."""
from pathlib import Path
import json
import runpy
import traceback

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name('testcontextmenu.py')))
tb = _base['tb']


class LayerAuthoringTest(_base['ContextMenuTest']):
    material = '/Looks/Paint'
    child = '/World/Group/Shape'

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.base_path = self.directory / 'base.usda'
        base = Usd.Stage.CreateNew(str(self.base_path))
        UsdGeom.Scope.Define(base, '/Looks')
        material = UsdShade.Material.Define(base, self.material)
        for node, shader_id, context in [('PreviewSurface','UsdPreviewSurface',''),
                                         ('StandardSurface','ND_standard_surface_surfaceshader','mtlx')]:
            shader = UsdShade.Shader.Define(base, self.material+'/'+node)
            shader.CreateIdAttr(shader_id)
            shader.CreateInput('diffuseColor' if not context else 'base_color', Sdf.ValueTypeNames.Color3f).Set((.02,.8,.1))
            material.CreateSurfaceOutput(context).ConnectToSource(shader.CreateOutput('surface' if not context else 'out', Sdf.ValueTypeNames.Token))
        shape = UsdGeom.Cube.Define(base, self.child)
        UsdShade.MaterialBindingAPI.Apply(shape.GetPrim()).Bind(material)
        base.GetRootLayer().Save()
        self.fixture = self.directory / 'root.usda'
        root = Sdf.Layer.CreateNew(str(self.fixture))
        root.subLayerPaths = [str(self.base_path)]
        root.Save()
        del root, base, material, shader, shape
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        stage = stageviz.session().stage()
        self.before = stage.GetRootLayer().ExportToString()
        self.base_before = Sdf.Layer.Find(str(self.base_path)).ExportToString()
        self.checkpoint()

    def checkpoint(self):
        stage = stageviz.session().stage()
        outputs = {a.GetName():[str(p) for p in a.GetConnections()]
                   for a in stage.GetPrimAtPath(self.material).GetAttributes() if a.GetName().startswith('outputs:')}
        print('AUTHORING_CHECKPOINT',json.dumps({'material':self.material,'outputs':outputs,
              'muted':list(stage.GetMutedLayers()),'selection':list(stageviz.session().paths())}))

    def override(self):
        stage = stageviz.session().stage()
        # Explicit overrides remain a command API; routine UI edits author their
        # required overs automatically, so there is no dedicated Outliner action.
        stageviz.command.override_paths([self.child])
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().GetPrimAtPath(self.child).specifier == Sdf.SpecifierOver
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == self.before
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().GetPrimAtPath(self.child)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.checkpoint()

    def connected(self, expected):
        stage = stageviz.session().stage()
        connections = []
        for output in UsdShade.Material(stage.GetPrimAtPath(self.material)).GetOutputs():
            connections += output.GetAttr().GetConnections()
        assert len(connections) == 1, connections
        shader = UsdShade.Shader(stage.GetPrimAtPath(connections[0].GetPrimPath()))
        assert shader.GetIdAttr().Get() == expected
        assert UsdShade.MaterialBindingAPI(stage.GetPrimAtPath(self.child)).ComputeBoundMaterial()[0].GetPath() == Sdf.Path(self.material)
        assert Sdf.Layer.Find(str(self.base_path)).ExportToString() == self.base_before
        assert not stage.GetMutedLayers()

    def outliner_replace(self):
        stage = stageviz.session().stage()
        self.menu([self.material], 'replaceMaterialNetwork_2')
        assert tb.wait_for_commands()
        self.connected('ND_open_pbr_surface_surfaceshader')
        replaced = stage.GetRootLayer().ExportToString()
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == self.before
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == replaced
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.checkpoint()

    def browser_replace(self):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        assert tb.trigger_action('materialEditor')
        assert tb.wait_until(lambda:any(w.objectName()=='materialDialog' and w.isVisible() for w in tb.all_widgets()))
        self.dialog = next(w for w in tb.all_widgets() if w.objectName()=='materialDialog' and w.isVisible())
        self.icons = self.dialog.findChild(QtWidgets.QListWidget,'icons')
        assert tb.wait_until(lambda:self.icons.count()==1)
        self.icons.setCurrentRow(0)
        def swatch():
            image = self.icons.item(0).data(QtCore.Qt.ItemDataRole.UserRole+1)
            return image if image is not None and not image.isNull() and image.pixelColor(image.width()//2,image.height()//2).alpha()>200 else None
        assert tb.wait_until(lambda:swatch() is not None,timeout=15)
        original = swatch().copy()
        errors, keep = [], []
        def choose():
            try:
                root = QtWidgets.QApplication.activePopupWidget()
                assert root.objectName() == 'materialContextMenu'
                submenu = root.findChild(QtWidgets.QMenu,'replaceMaterialNetworkMenu')
                assert submenu and submenu.isEnabled()
                action = next(a for a in submenu.actions() if a.objectName()=='replaceMaterialNetwork_1')
                root.setActiveAction(submenu.menuAction())
                submenu.popup(root.mapToGlobal(root.actionGeometry(submenu.menuAction()).topRight()))
                keep.extend([root, submenu, action])
                def activate():
                    try:
                        submenu.setActiveAction(action)
                        assert submenu.isVisible()
                        submenu.grab().save(str(self.directory/'replace-material-menu.png'))
                        QtTest.QTest.keyPress(submenu,QtCore.Qt.Key.Key_Return)
                    except Exception:
                        errors.append(traceback.format_exc())
                        for widget in QtWidgets.QApplication.topLevelWidgets():
                            if isinstance(widget,QtWidgets.QMenu): widget.close()
                QtCore.QTimer.singleShot(50,activate)
            except Exception:
                errors.append(traceback.format_exc())
                for w in QtWidgets.QApplication.topLevelWidgets():
                    if isinstance(w,QtWidgets.QMenu): w.close()
        timer = QtCore.QTimer(self.icons)
        timer.setSingleShot(True)
        timer.timeout.connect(lambda:[w.close() for w in QtWidgets.QApplication.topLevelWidgets() if isinstance(w,QtWidgets.QMenu)])
        timer.start(5000)
        QtCore.QTimer.singleShot(50,choose)
        QtTest.QTest.mouseClick(self.icons.viewport(), QtCore.Qt.MouseButton.RightButton,
                               QtCore.Qt.KeyboardModifier.NoModifier, self.icons.visualItemRect(self.icons.item(0)).center())
        timer.stop()
        assert not errors, errors
        assert tb.wait_for_commands()
        self.connected('ND_standard_surface_surfaceshader')
        assert tb.wait_until(lambda:swatch() is not None and swatch()!=original,timeout=15)
        replaced_image = swatch().copy()
        self.dialog.grab().save(str(self.directory/'replacement-swatch.png'))
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetRootLayer().ExportToString()==self.before
        assert tb.wait_until(lambda:swatch() is not None and swatch()==original,timeout=15)
        stageviz.command.redo()
        assert tb.wait_for_commands()
        self.connected('ND_standard_surface_surfaceshader')
        assert tb.wait_until(lambda:swatch() is not None and swatch()==replaced_image,timeout=15)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetRootLayer().ExportToString()==self.before
        self.checkpoint()

    def bindings(self):
        stage = stageviz.session().stage()
        stageviz.command.override_paths([self.child])
        assert tb.wait_for_commands()
        stageviz.command.replace_material_network([self.material], 'preview_surface')
        assert tb.wait_for_commands()
        self.connected('UsdPreviewSurface')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.GetRootLayer().ExportToString() == self.before
        assert stageviz.session().setEditLayer(str(self.base_path)) and tb.wait_for_commands()
        try:
            stageviz.command.replace_material_network([self.material], 'bogus')
            assert False, 'unknown type must fail'
        except ValueError:
            pass
        self.checkpoint()
