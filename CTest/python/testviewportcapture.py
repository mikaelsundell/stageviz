# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Viewport capture phases for a disposable on-screen viewer (no autorun)."""

from pathlib import Path
import runpy
import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


class ViewportCaptureTest(_base["ViewportTest"]):
    expected = {"/World/Front", "/World/Source/Body", "/World/Instance/Body"}
    red = {"/World/Source/Body", "/World/Instance/Body"}

    def setup(self):
        assert self.disposable, "use a disposable viewer"
        session = stageviz.session()
        self.original_filename = session.filename()
        self.original_selection = list(session.paths())
        filename = self.directory / "capture.usda"
        stage = Usd.Stage.CreateNew(str(filename))
        world = UsdGeom.Xform.Define(stage, "/World")
        stage.SetDefaultPrim(world.GetPrim())
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

        def plane(path, offset, back, color):
            mesh = UsdGeom.Mesh.Define(stage, path)
            mesh.CreatePointsAttr([(-0.7, -0.7, 0), (0.7, -0.7, 0), (0.7, 0.7, 0), (-0.7, 0.7, 0)])
            mesh.CreateFaceVertexCountsAttr([4])
            mesh.CreateFaceVertexIndicesAttr([3, 2, 1, 0] if back else [0, 1, 2, 3])
            mesh.CreateNormalsAttr([Gf.Vec3f(0, 0, -1 if back else 1)])
            mesh.SetNormalsInterpolation(UsdGeom.Tokens.constant)
            mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
            mesh.CreateDisplayColorAttr([Gf.Vec3f(*color)])
            mesh.AddTranslateOp().Set(Gf.Vec3d(offset, 0, 0))
            # Unlit colors make the RGB/hue test independent of which side is
            # illuminated. The diagnostic phase separately exercises shading.
            material_path = "/Materials/" + ("Red" if back else "Blue")
            material = UsdShade.Material.Define(stage, material_path)
            shader = UsdShade.Shader.Define(stage, material_path + "/Surface")
            shader.CreateIdAttr("UsdPreviewSurface")
            shader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0))
            shader.CreateInput("emissiveColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*color))
            shader.CreateOutput("surface", Sdf.ValueTypeNames.Token)
            material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), "surface")
            UsdShade.MaterialBindingAPI.Apply(mesh.GetPrim()).Bind(material)
            return mesh

        plane("/World/Front", -2, False, (0.02, 0.02, 0.9))
        source = UsdGeom.Xform.Define(stage, "/World/Source")
        source.AddTranslateOp().Set(Gf.Vec3d(0))
        body = plane("/World/Source/Body", 0, True, (0.9, 0.02, 0.02))
        UsdGeom.Subset.CreateGeomSubset(body, "Face", UsdGeom.Tokens.face, [0])
        instance = UsdGeom.Xform.Define(stage, "/World/Instance")
        instance.GetPrim().GetReferences().AddInternalReference("/World/Source")
        instance.GetPrim().SetInstanceable(True)
        instance.GetPrim().GetAttribute("xformOp:translate").Set(Gf.Vec3d(2, 0, 0))
        stage.GetRootLayer().Save()
        assert session.loadFromFile(str(filename))
        state = session.viewState()
        state.setMaterialMode(0)
        state.setOverrideMaterial("")
        state.setSceneMaterialsEnabled(True)
        state.setDoubleSidedMode(1)
        state.setGridEnabled(True)
        camera = state.camera()
        camera.setCameraUp(1)
        camera.setAxisYaw(0)
        camera.setAxisPitch(0)
        camera.setAxisRoll(0)
        camera.setAspectRatioLocked(False)
        camera.frame(((-3, -1, -0.1), (3, 1, 0.1)))
        camera.distance(1.6)
        self.activate_mode("Move")
        stageviz.command.select_paths(["/World/Instance/Body/Face"])
        tb.process_events(200)
        self.snapshot("capture_fixture")

    def colors(self):
        session = stageviz.session()
        selected = list(session.paths())
        before = session.stage().GetRootLayer().ExportToString()
        self.frame = stageviz.viewport().capture()
        assert set(self.frame.paths()) == self.expected, self.frame.paths()
        assert set(self.frame.pathsByHue(0, tolerance=20, minPixels=8)) == self.red
        assert set(self.frame.pathsByHue(240, tolerance=20, minPixels=8)) == {"/World/Front"}
        assert set(self.frame.pathsByHue(360, tolerance=20)) == self.red
        assert set(self.frame.pathsByHue(-120, tolerance=20)) == {"/World/Front"}
        assert not self.frame.pathsByHue(45, tolerance=5, minPixels=1), "no grid/gizmo/outline colors"
        assert list(session.paths()) == selected and session.stage().GetRootLayer().ExportToString() == before
        width, height = self.frame.size()
        assert len(self.frame.rgba()) == width * height * 4
        ids = memoryview(self.frame.objectIds()).cast("i")
        assert len(ids) == width * height
        for row in self.frame.prims():
            index = self.frame.paths().index(row["path"])
            assert row["visiblePixels"] == sum(value == index for value in ids)
            pixel = next(i for i, value in enumerate(ids) if value == index)
            assert self.frame.pathAt(pixel % width, pixel // width) == row["path"]
        assert self.frame.pathAt(0, 0) is None
        self.frame.save(str(self.directory / "capture_colors.png"))
        stageviz.command.clear()
        paths = self.frame.pathsByHue(0, tolerance=20)
        stageviz.command.select_paths(paths)
        assert set(session.paths()) == self.red
        stageviz.command.undo()
        assert list(session.paths()) == selected
        stageviz.command.redo()
        assert set(session.paths()) == self.red
        print("PASS viewport colors, native instance paths, buffers, read-only capture, selection undo/redo")

    def diagnostic(self):
        # Same facing-ratio MaterialX graph as the normal-direction dialog.
        stage = stageviz.session().auxiliary()
        material = UsdShade.Material.Define(stage, "/Materials/CaptureNormalDirection")
        def node(name, identifier, output_type):
            shader = UsdShade.Shader.Define(stage, str(material.GetPath()) + "/" + name)
            shader.CreateIdAttr(identifier)
            shader.CreateOutput("out", output_type)
            return shader
        facing = node("Facing", "ND_facingratio_float", Sdf.ValueTypeNames.Float)
        facing.CreateInput("faceforward", Sdf.ValueTypeNames.Bool).Set(False)
        facing.CreateInput("invert", Sdf.ValueTypeNames.Bool).Set(False)
        color = node("Color", "ND_ifgreater_color3", Sdf.ValueTypeNames.Color3f)
        color.CreateInput("value1", Sdf.ValueTypeNames.Float).ConnectToSource(facing.ConnectableAPI(), "out")
        color.CreateInput("value2", Sdf.ValueTypeNames.Float).Set(0.0)
        color.CreateInput("in1", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0.12, 0.50, 0.16))
        color.CreateInput("in2", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0.62, 0.10, 0.32))
        surface = node("Surface", "ND_standard_surface_surfaceshader", Sdf.ValueTypeNames.Token)
        surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(1.0)
        surface.CreateInput("base_color", Sdf.ValueTypeNames.Color3f).ConnectToSource(color.ConnectableAPI(), "out")
        surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(0.0)
        surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.68)
        material.CreateSurfaceOutput().ConnectToSource(surface.ConnectableAPI(), "out")
        state = stageviz.session().viewState()
        state.setOverrideMaterial(str(material.GetPath()))
        state.setMaterialMode(2)
        state.setDoubleSidedMode(1)
        tb.process_events(200)
        frame = stageviz.viewport().capture()
        frame.save(str(self.directory / "capture_normal_direction_clean.png"))
        assert set(frame.pathsByHue(335, tolerance=30, minPixels=8)) == self.red, frame.prims()
        assert set(frame.pathsByHue(120, tolerance=35, minPixels=8)) == {"/World/Front"}
        self.snapshot("capture_normal_direction")
        print("PASS normal-direction diagnostic magenta selects source and proxy backfaces")

    def boundaries(self):
        session = stageviz.session()
        state = session.viewState()
        state.setMaterialMode(0)
        state.setOverrideMaterial("")
        state.setDoubleSidedMode(2)
        tb.process_events(100)
        frame = stageviz.viewport().capture()
        assert set(frame.paths()) == {"/World/Front"}, "capture honors back-face culling"
        state.setDoubleSidedMode(1)
        stageviz.command.isolate_paths(["/World/Source"])
        tb.process_events(100)
        frame = stageviz.viewport().capture()
        assert frame.paths() == ["/World/Source/Body"], "capture honors viewport masks"
        stageviz.command.undo()
        old = self.frame.paths()
        assert session.close()
        assert self.frame.paths() == old and set(self.frame.pathsByHue(0, tolerance=20)) == self.red
        try:
            stageviz.viewport().capture()
        except RuntimeError:
            pass
        else:
            raise AssertionError("capture without a stage must fail")
        for method, kwargs in ((self.frame.pathsByHue, {"hue": float("nan")}),
                               (self.frame.pathsByHue, {"hue": 0, "minCoverage": 2}),
                               (self.frame.pathsByColor, {"color": (256, 0, 0)})):
            try:
                method(**kwargs)
            except ValueError:
                pass
            else:
                raise AssertionError("invalid color query was accepted")
        print("PASS culling, masks, snapshot lifetime, no-stage and argument boundaries")

    def cleanup(self):
        state = stageviz.session().viewState()
        state.setOverrideMaterial("")
        state.setMaterialMode(0)
        state.setDoubleSidedMode(0)
        super().cleanup()


def run(disposable=False):
    test = ViewportCaptureTest(disposable=disposable)
    try:
        test.setup()
        test.colors()
        test.diagnostic()
        test.boundaries()
        return True
    finally:
        test.cleanup()
