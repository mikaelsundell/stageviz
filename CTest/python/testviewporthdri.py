# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""HDRI/opacity presentation checks in a disposable viewer (no autorun)."""

from pathlib import Path
import runpy
import stageviz
from pxr import Gf, Plug, Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


class ViewportHdriTest(_base["ViewportTest"]):
    def setup(self):
        assert self.disposable, "use a disposable viewer"
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.fixtures = []
        for kind in ("preview", "materialx"):
            for opacity in (1.0, 0.5, 0.0):
                filename = self.directory / f"{kind}_{opacity}.usda"
                fixture = Usd.Stage.CreateNew(str(filename))
                world = UsdGeom.Xform.Define(fixture, "/World")
                fixture.SetDefaultPrim(world.GetPrim())
                sphere = UsdGeom.Sphere.Define(fixture, "/World/Sphere")
                material = UsdShade.Material.Define(fixture, "/Materials/Red")
                shader = UsdShade.Shader.Define(fixture, "/Materials/Red/Surface")
                if kind == "preview":
                    shader.CreateIdAttr("UsdPreviewSurface")
                    shader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(.8, .02, .01))
                    shader.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(.35)
                    shader.CreateInput("opacity", Sdf.ValueTypeNames.Float).Set(opacity)
                    output = shader.CreateOutput("surface", Sdf.ValueTypeNames.Token)
                    material.CreateSurfaceOutput().ConnectToSource(output)
                else:
                    shader.CreateIdAttr("ND_standard_surface_surfaceshader")
                    shader.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.)
                    shader.CreateInput("base_color", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(.8, .02, .01))
                    shader.CreateInput("specular_roughness", Sdf.ValueTypeNames.Float).Set(.35)
                    shader.CreateInput("opacity", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(opacity))
                    output = shader.CreateOutput("out", Sdf.ValueTypeNames.Token)
                    material.CreateSurfaceOutput("mtlx").ConnectToSource(output)
                UsdShade.MaterialBindingAPI.Apply(sphere.GetPrim()).Bind(material)
                fixture.GetRootLayer().Save()
                self.fixtures.append((kind, opacity, str(filename)))
        plugin = Plug.Registry().GetPluginWithName("hdx")
        texture = Path(plugin.resourcePath) / "textures" / "StinsonBeach.hdr"
        assert texture.is_file(), texture
        self.textures = ("", str(texture))

    @staticmethod
    def red_pixels(image):
        count = 0
        for y in range(image.height() // 4, image.height() * 3 // 4, max(1, image.height() // 80)):
            for x in range(image.width() // 4, image.width() * 3 // 4, max(1, image.width() // 80)):
                color = image.pixelColor(x, y)
                count += color.red() > 25 and color.red() > color.green() * 1.5 and color.red() > color.blue() * 1.5
        return count

    def modes(self):
        checked = 0
        for kind, opacity, filename in self.fixtures:
            assert stageviz.session().loadFromFile(filename)
            state = stageviz.session().viewState()
            state.setMaterialMode(0)
            state.setSceneMaterialsEnabled(True)
            state.setGridEnabled(True)
            state.setAmbientOcclusionEnabled(False)
            state.setComplexityLevel(2)
            state.camera().setAspectRatioLocked(False)
            tb.trigger_action("displayFrameAll")
            state.camera().distance(1.8)
            stageviz.command.select_paths([])
            for texture_index, texture in enumerate(self.textures):
                state.setDomeLightTexture(texture)
                for camera, dome in ((True, False), (False, True), (True, True)):
                    state.setDefaultCameraLightEnabled(camera)
                    state.setDefaultDomeLightEnabled(dome)
                    for mode in (0, 1):
                        state.setRenderMode(mode)
                        for background in (False, True):
                            state.setDomeLightCameraVisibility(background)
                            tb.process_events(100)
                            label = f"{kind}_{opacity}_texture{texture_index}_camera{int(camera)}_dome{int(dome)}_mode{mode}_bg{int(background)}"
                            image = self.viewport().grab().toImage()
                            assert image.save(str(self.directory / (label + ".png")))
                            count = self.red_pixels(image)
                            if opacity > 0:
                                assert count > 30, (label, "object disappeared", count)
                            elif mode == 0:
                                assert count == 0, (label, "zero opacity must hide the surface", count)
                            checked += 1
            self.snapshot(f"{kind}_{opacity}_complete")
        print("PASS HDRI lighting, background, custom texture, shaded/wireframe and opacity:", checked, "combinations")


def run(disposable=False):
    test = ViewportHdriTest(disposable=disposable)
    try:
        test.setup()
        test.modes()
    finally:
        test.cleanup()
    return str(test.directory)
