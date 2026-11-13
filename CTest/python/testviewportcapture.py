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
        occluded = plane("/World/Occluded", -2, False, (0.02, 0.02, 0.9))
        occluded.GetOrderedXformOps()[0].Set(Gf.Vec3d(-2, 0, -1))
        occluded.AddScaleOp().Set(Gf.Vec3f(0.1))
        plane("/World/Offscreen", 100, False, (0.02, 0.02, 0.9))
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
        self.frame_fixture()
        self.activate_mode("Move")
        stageviz.command.select_paths(["/World/Instance/Body/Face"])
        tb.process_events(200)
        self.snapshot("capture_fixture")

    def frame_fixture(self):
        """Use the same front view after stage loads reset the camera."""
        camera = stageviz.session().viewState().camera()
        camera.setCameraUp(1)
        camera.setAxisYaw(0)
        camera.setAxisPitch(0)
        camera.setAxisRoll(0)
        camera.setAspectRatioLocked(False)
        camera.frame(((-3, -1, -0.1), (3, 1, 0.1)))
        camera.distance(1.6)

    def colors(self):
        session = stageviz.session()
        selected = list(session.paths())
        before = session.stage().GetRootLayer().ExportToString()
        self.frame = stageviz.render().capture()
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

    def visible_paths(self):
        """Menu and Python use one tiled scan and one accumulated collection."""
        session = stageviz.session()
        viewport = stageviz.render()
        selected = list(session.paths())
        mask = list(session.mask())
        camera = session.viewState().camera()
        roll = camera.axisRoll()
        before = session.stage().GetRootLayer().ExportToString()
        history = (stageviz.command.can_undo(), stageviz.command.can_redo(), stageviz.command.can_clear())

        def action(name):
            # Resolve the live action each time; stage changes can rebuild UI.
            info = tb.action_info(name)
            assert info is not None and info["enabled"], name
            assert tb.trigger_action(name), name

        try:
            viewport.clearVisibleCapture()
            viewport.clearVisibleCapture()  # Idempotent.
            assert viewport.visibleCapturePaths() == []
            scan = viewport.capturePaths(accumulate=False)
            assert set(scan) == self.expected, scan
            assert viewport.visibleCapturePaths() == [], "one-shot scan does not accumulate"

            # No processEvents between mask setters and capture: both menu and
            # Python must synchronize the latest view before scanning.
            session.setMask(["/World/Front"])
            assert viewport.capturePaths() == ["/World/Front"]
            session.setMask(["/World/Source"])
            scan = viewport.capturePaths(False)
            assert scan == ["/World/Source/Body"], scan
            assert viewport.visibleCapturePaths() == ["/World/Front"]
            action("editSelectVisibleCapture")
            assert set(viewport.visibleCapturePaths()) == {"/World/Front", "/World/Source/Body"}

            session.setMask([])
            assert set(viewport.capturePaths(accumulate=True)) == self.expected
            captured = viewport.visibleCapturePaths()
            assert viewport.capturePaths() == captured, "repeat scans preserve order without duplicates"
            copied = viewport.visibleCapturePaths()
            copied.clear()
            assert viewport.visibleCapturePaths() == captured, "returned list owns its data"
            assert set(stageviz.render().capture().paths()) == self.expected
            assert viewport.visibleCapturePaths() == captured, "image capture leaves collection unchanged"

            # Turn around without pumping events: the smaller rear plane is
            # genuinely occluded from the original view, but visible from behind.
            camera.setAxisRoll(roll + 180)
            rear = viewport.capturePaths(accumulate=False)
            assert "/World/Occluded" in rear and "/World/Offscreen" not in rear, rear
            assert viewport.visibleCapturePaths() == captured
            assert set(viewport.capturePaths()) == self.expected | {"/World/Occluded"}
            camera.setAxisRoll(roll)
            viewport.clearVisibleCapture()
            assert set(viewport.capturePaths()) == self.expected

            assert session.paths() == selected
            assert session.stage().GetRootLayer().ExportToString() == before
            assert history == (stageviz.command.can_undo(), stageviz.command.can_redo(), stageviz.command.can_clear())
            self.snapshot("visible_paths_collected")

            action("editSelectVisibleSelect")
            assert tb.wait_for_commands()
            assert set(session.paths()) == self.expected
            stageviz.command.undo()
            assert tb.wait_for_commands() and session.paths() == selected
            stageviz.command.redo()
            assert tb.wait_for_commands() and set(session.paths()) == self.expected
            action("editSelectVisibleClear")
            assert viewport.visibleCapturePaths() == []
            assert set(session.paths()) == self.expected, "clearing captures leaves selection unchanged"
            self.snapshot("visible_paths_selected_and_cleared")

            # A wrapper acquired on the GUI thread must still reject use by a
            # worker rather than touching Qt/GL off-thread. No Qt processing is
            # required by these immediately rejected calls.
            import threading
            rejected = []

            def worker():
                for method in (viewport.capturePaths, viewport.visibleCapturePaths, viewport.clearVisibleCapture):
                    try:
                        method()
                    except RuntimeError:
                        rejected.append(True)

            thread = threading.Thread(target=worker)
            thread.start()
            thread.join(2)
            assert not thread.is_alive() and len(rejected) == 3, "all collection APIs require the GUI thread"
        finally:
            camera.setAxisRoll(roll)
            session.setMask(mask)
            viewport.clearVisibleCapture()
            stageviz.command.select_paths(selected)
            assert tb.wait_for_commands()
        print("PASS shared tiled path scans, one-shot/accumulation, menu parity, selection undo/redo and thread guards")

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
        # Classify facing direction by hue independently of camera-light
        # specular highlights, which can clip the diagnostic color to white.
        surface.CreateInput("base", Sdf.ValueTypeNames.Float).Set(0.0)
        surface.CreateInput("specular", Sdf.ValueTypeNames.Float).Set(0.0)
        surface.CreateInput("emission", Sdf.ValueTypeNames.Float).Set(1.0)
        surface.CreateInput("emission_color", Sdf.ValueTypeNames.Color3f).ConnectToSource(color.ConnectableAPI(), "out")
        surface.CreateInput("metalness", Sdf.ValueTypeNames.Float).Set(0.0)
        surface.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.68)
        material.CreateSurfaceOutput().ConnectToSource(surface.ConnectableAPI(), "out")
        state = stageviz.session().viewState()
        state.setOverrideMaterial(str(material.GetPath()))
        state.setMaterialMode(2)
        state.setDoubleSidedMode(1)
        tb.process_events(200)
        frame = stageviz.render().capture()
        frame.save(str(self.directory / "capture_normal_direction_clean.png"))
        assert set(frame.pathsByHue(335, tolerance=30, minPixels=8)) == self.red, frame.prims()
        green = set(frame.pathsByHue(120, tolerance=35, minPixels=8))
        assert green == {"/World/Front"}, green
        self.snapshot("capture_normal_direction")
        print("PASS normal-direction diagnostic magenta selects source and proxy backfaces")

    def inspect(self):
        import json
        session = stageviz.session()
        viewport = stageviz.render()
        selected = list(session.paths())
        before = session.stage().GetRootLayer().ExportToString()
        for path in sorted(self.expected):
            report = viewport.inspectPrim(path)
            assert report["usd"]["points"] == 4 and report["usd"]["faces"] == 1
            assert report["usd"]["topologyValid"] and report["usd"]["pointsFinite"]
            assert report["usd"]["visibilityAtViewportTime"] == "inherited"
            assert report["includedByMask"] and report["rprims"]
            assert any(row["present"] and row["visible"] and row["delegateVisible"]
                       and row["drawItems"] for row in report["rprims"]), report
            (self.directory / (path.replace("/", "_") + "_hydra.json")).write_text(json.dumps(report, indent=2))
        assert list(session.paths()) == selected
        assert session.stage().GetRootLayer().ExportToString() == before
        stageviz.command.hide_paths(["/World/Front"])
        tb.process_events(100)
        hidden = viewport.inspectPrim("/World/Front")
        assert hidden["usd"]["visibilityAtViewportTime"] == "invisible"
        assert all(not row["visible"] for row in hidden["rprims"] if row["present"]), hidden
        stageviz.command.undo()
        tb.process_events(100)
        assert viewport.inspectPrim("/World/Front")["usd"]["visibilityAtViewportTime"] == "inherited"
        stageviz.command.redo()
        tb.process_events(100)
        assert viewport.inspectPrim("/World/Front")["usd"]["visibilityAtViewportTime"] == "invisible"
        stageviz.command.undo()
        tb.process_events(100)
        for path, exception in (("relative", ValueError), ("/World/Front.points", ValueError),
                                ("/World", RuntimeError), ("/World/Missing", RuntimeError)):
            try:
                viewport.inspectPrim(path)
            except exception:
                pass
            else:
                raise AssertionError("inspectPrim accepted " + path)
        self.snapshot("hydra_inspection")
        # Build a separate fixture: default visibility can disagree with the
        # numeric time used by the viewport even when the timeline is at zero.
        original_file = session.filename()
        sampled_file = self.directory / "sampled_visibility.usda"
        session.stage().GetRootLayer().Export(str(sampled_file))
        sampled_stage = Usd.Stage.Open(str(sampled_file))
        UsdGeom.Imageable(sampled_stage.GetPrimAtPath("/World/Front")).GetVisibilityAttr().Set(
            UsdGeom.Tokens.invisible, Usd.TimeCode(0))
        sampled_stage.GetRootLayer().Save()
        try:
            assert session.loadFromFile(str(sampled_file))
            tb.process_events(200)
            report = viewport.inspectPrim("/World/Front")
            assert report["usd"]["visibilityDefault"] == "inherited"
            assert report["usd"]["visibilityAtViewportTime"] == "invisible"
            assert report["usd"]["visibilityTimeSamples"] == [0]
            assert all(not row["delegateVisible"] for row in report["rprims"] if row["present"])
        finally:
            assert session.loadFromFile(original_file)
            self.frame_fixture()
            stageviz.command.select_paths(selected)
            tb.process_events(200)
            assert set(viewport.capture().paths()) == self.expected, "reload restores the occluding front view"
        print("PASS read-only USD/Hydra diagnostics, proxy mapping, visibility undo/redo and argument boundaries")

    def boundaries(self):
        session = stageviz.session()
        state = session.viewState()
        state.setMaterialMode(0)
        state.setOverrideMaterial("")
        state.setDoubleSidedMode(2)
        tb.process_events(100)
        frame = stageviz.render().capture()
        assert set(frame.paths()) == {"/World/Front"}, "capture honors back-face culling"
        state.setDoubleSidedMode(1)
        stageviz.command.isolate_paths(["/World/Source"])
        tb.process_events(100)
        frame = stageviz.render().capture()
        assert frame.paths() == ["/World/Source/Body"], "capture honors viewport masks"
        stageviz.command.undo()
        old = self.frame.paths()
        assert session.close()
        assert stageviz.render().visibleCapturePaths() == [], "closing the stage resets menu captures"
        assert stageviz.render().capturePaths() == [], "no rendered stage yields an empty path scan"
        stageviz.render().clearVisibleCapture()
        assert self.frame.paths() == old and set(self.frame.pathsByHue(0, tolerance=20)) == self.red
        try:
            stageviz.render().capture()
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
        test.visible_paths()
        test.inspect()
        test.diagnostic()
        test.boundaries()
        return True
    finally:
        test.cleanup()
