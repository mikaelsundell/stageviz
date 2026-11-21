# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Viewport and swatches follow material render-context changes on layer mute."""
from pathlib import Path
import json
import runpy
import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class MaterialViewportCompositionTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.weak = self.directory / 'preview.usda'
        self.strong = self.directory / 'materialx.usda'
        self.fixture = self.directory / 'root.usda'
        stage = Usd.Stage.CreateNew(str(self.weak))
        sphere = UsdGeom.Sphere.Define(stage, '/Sphere')
        sphere.GetRadiusAttr().Set(2)
        material = UsdShade.Material.Define(stage, '/Materials/Paint')
        shader = UsdShade.Shader.Define(stage, '/Materials/Paint/Preview')
        shader.CreateIdAttr('UsdPreviewSurface')
        shader.CreateInput('diffuseColor', Sdf.ValueTypeNames.Color3f).Set((.6,.05,.05))
        material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
        UsdShade.MaterialBindingAPI.Apply(sphere.GetPrim()).Bind(material)
        stage.GetRootLayer().Save()
        stage = Usd.Stage.CreateNew(str(self.strong))
        material = UsdShade.Material(stage.OverridePrim('/Materials/Paint'))
        material.CreateSurfaceOutput().GetAttr().SetConnections([])
        shader = UsdShade.Shader.Define(stage, '/Materials/Paint/StandardSurface')
        shader.CreateIdAttr('ND_standard_surface_surfaceshader')
        shader.CreateInput('base_color', Sdf.ValueTypeNames.Color3f).Set((.05,.1,.7))
        material.CreateSurfaceOutput('mtlx').ConnectToSource(shader.CreateOutput('out', Sdf.ValueTypeNames.Token))
        stage.GetRootLayer().Save()
        for path in ('/Materials','/Materials/Paint'):
            stage.GetRootLayer().GetPrimAtPath(path).specifier = Sdf.SpecifierOver
        stage.GetRootLayer().Save()
        root = Sdf.Layer.CreateNew(str(self.fixture))
        root.subLayerPaths = [str(self.strong),str(self.weak)]
        root.Save()
        self.load(self.weak)
        self.reference_preview = self.pixels()
        self.load(self.fixture)
        assert tb.trigger_action('materialEditor')
        assert tb.wait_until(lambda: self.swatch() is not None, timeout=20)
        self.reference_mtlx = self.pixels()
        assert self.distance(self.reference_preview,self.reference_mtlx)>20
        self.check(False, 'initial')

    def load(self, filename):
        assert stageviz.session().load(str(filename)) and tb.wait_for_commands()
        camera = stageviz.session().viewState().camera()
        camera.setFocusPoint(0,0,0)
        camera.setCameraDistance(12)
        camera.setCameraView(stageviz.CameraViewFront)
        stageviz.render().capture()
        tb.process_events(500)

    def swatch(self):
        QtCore, _, _, QtWidgets = tb.qt()
        dialogs = [w for w in tb.all_widgets() if w.objectName()=='materialDialog' and w.isVisible()]
        if not dialogs:
            return None
        icons = dialogs[0].findChild(QtWidgets.QListWidget,'icons')
        if not icons or icons.count()!=1:
            return None
        image = icons.item(0).data(QtCore.Qt.ItemDataRole.UserRole+1)
        return image if image is not None and not image.isNull() and image.pixelColor(image.width()//2,image.height()//2).alpha()>200 else None

    def pixels(self):
        stageviz.render().capture()
        tb.process_events(200)
        image = self.viewport().grab().toImage()
        return [image.pixelColor(int(image.width()*x),int(image.height()*y)).getRgb()[:3]
                for x,y in ((.5,.5),(.47,.47),(.53,.47),(.47,.53),(.53,.53))]

    @staticmethod
    def distance(a,b):
        return max(abs(x-y) for pa,pb in zip(a,b) for x,y in zip(pa,pb))

    def check(self, muted, label):
        expected = self.reference_preview if muted else self.reference_mtlx
        assert tb.wait_until(lambda:self.distance(self.pixels(),expected)<6,timeout=8), (label,self.pixels(),expected)
        def swatch_matches():
            image = self.swatch()
            if image is None:
                return False
            color = image.pixelColor(image.width()//2,image.height()//2)
            return color.red()>color.blue()+25 if muted else color.blue()>color.red()+25
        assert tb.wait_until(swatch_matches,timeout=15), label
        material = UsdShade.Material(stageviz.session().stage().GetPrimAtPath('/Materials/Paint'))
        surface = material.ComputeSurfaceSource('mtlx')[0]
        assert str(surface.GetIdAttr().Get()) == ('UsdPreviewSurface' if muted else 'ND_standard_surface_surfaceshader')
        self.snapshot(label)
        print('MATERIAL_VIEWPORT_CHECKPOINT',json.dumps({'muted':muted,'layer':str(self.strong),'pixels':self.pixels()}))

    def toggle(self):
        for index, muted in enumerate((True,False,True,False)):
            stageviz.command.set_layer_muted(str(self.strong),muted)
            assert tb.wait_for_commands()
            self.check(muted,'toggle-'+str(index))
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.check(True,'undo')
        stageviz.command.redo()
        assert tb.wait_for_commands()
        self.check(False,'redo')

    def isolated(self):
        stageviz.command.isolate_paths(['/Sphere'])
        assert tb.wait_for_commands()
        for index,muted in enumerate((True,False,True)):
            stageviz.command.set_layer_muted(str(self.strong),muted)
            assert tb.wait_for_commands()
            self.check(muted,'isolated-'+str(index))
        stageviz.command.isolate_paths([])
        assert tb.wait_for_commands()
        self.check(True,'restored-full-scene')
        stageviz.command.set_layer_muted(str(self.strong),False)
        assert tb.wait_for_commands()
        self.check(False,'full-scene-materialx')

    def availability(self):
        stageviz.command.set_layer_muted(str(self.weak),True)
        assert tb.wait_for_commands()
        assert not stageviz.session().stage().GetPrimAtPath('/Sphere')
        assert tb.wait_until(lambda:self.swatch() is None)
        self.snapshot('geometry-layer-muted')
        stageviz.command.set_layer_muted(str(self.strong),True)
        assert tb.wait_for_commands()
        assert not list(stageviz.session().stage().Traverse())
        stageviz.command.set_layer_muted(str(self.weak),False)
        assert tb.wait_for_commands()
        self.check(True,'only-preview-layer')
        stageviz.command.set_layer_muted(str(self.strong),False)
        assert tb.wait_for_commands()
        self.check(False,'both-layers-restored')
