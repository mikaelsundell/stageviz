# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Light commands, Create actions and viewport helpers in a disposable viewer."""

from pathlib import Path
import runpy
import stageviz
from pxr import Gf, Usd, UsdGeom, UsdLux

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]
KINDS = ("point", "spot", "distant", "rect", "disk", "cylinder", "dome")
SCHEMAS = ("SphereLight", "SphereLight", "DistantLight", "RectLight", "DiskLight", "CylinderLight", "DomeLight")


class LightsTest(_base["ViewportTest"]):
    def setup(self):
        assert self.disposable, "use a disposable viewer"
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        assert stageviz.session().newStage()
        self.paths = []

    def menu(self):
        for kind, schema in zip(KINDS, SCHEMAS):
            stageviz.command.select_paths(["/World"])
            stageviz.command.clear()
            tb.trigger_action("create" + kind.title() + "Light")
            path = "/World/" + kind.title() + "Light"
            assert tb.wait_until(lambda: list(stageviz.session().paths()) == [path]), kind
            prim = stageviz.session().stage().GetPrimAtPath(path)
            assert prim.GetTypeName() == schema and UsdLux.LightAPI(prim)
            if kind == "dome":
                texture = UsdLux.DomeLight(prim).GetTextureFileAttr().Get()
                assert Path(texture.path).name == "scandinavia_1024x512.exr" and Path(texture.path).is_file()
            assert prim.HasAPI(UsdLux.ShapingAPI) == (kind == "spot")
            stageviz.command.undo()
            assert tb.wait_until(lambda: not stageviz.session().stage().GetPrimAtPath(path))
            assert list(stageviz.session().paths()) == ["/World"]
            stageviz.command.redo()
            assert tb.wait_until(lambda: list(stageviz.session().paths()) == [path])
            if kind == "dome":
                restored = stageviz.session().stage().GetPrimAtPath(path)
                assert UsdLux.DomeLight(restored).GetTextureFileAttr().Get() == texture
            stageviz.command.undo()
            assert tb.wait_until(lambda: not stageviz.session().stage().GetPrimAtPath(path))
        print("PASS all Create light actions, USD schemas, selection, undo/redo")

    def guides(self):
        for i, (kind, schema) in enumerate(zip(KINDS, SCHEMAS)):
            path = "/World/" + kind.title()
            stageviz.command.new_light("/World", kind.title(), type=kind)
            assert tb.wait_until(lambda: list(stageviz.session().paths()) == [path])
            prim = stageviz.session().stage().GetPrimAtPath(path)
            assert prim.GetTypeName() == schema
            matrix = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d((i-3)*3, 0, 0))
            stageviz.command.set_transforms([path], [Gf.Matrix4d(1)], [matrix])
            assert tb.wait_until(lambda: Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(prim), matrix, 1e-6))
            self.paths.append(path)
        state = stageviz.session().viewState()
        state.setDefaultDomeLightEnabled(False)
        state.setDomeLightCameraVisibility(False)
        state.setSceneLightsEnabled(True)
        state.setGridEnabled(True)
        camera = state.camera()
        camera.setCameraUp(1)
        camera.setAxisYaw(0)
        camera.setAxisPitch(0)
        camera.setAxisRoll(0)
        camera.setAspectRatioLocked(False)
        camera.frame(((-12, -3, -3), (12, 3, 3)))
        camera.distance(1.5)
        stageviz.command.select_paths([])
        self.activate_mode("Select")
        tb.process_events(200)
        self.snapshot("light_guides")
        # All guides are editor-only: no geometry in the document or capture.
        stage = stageviz.session().stage()
        assert not any(prim.IsA(UsdGeom.Gprim) for prim in stage.Traverse())
        assert stageviz.render().capture().paths() == []
        QtCore, _, QtTest, _ = tb.qt()
        widget = self.viewport()
        center = QtCore.QPoint(widget.width()//2, widget.height()//2)
        QtTest.QTest.mouseClick(widget, QtCore.Qt.MouseButton.LeftButton, pos=center)
        assert tb.wait_until(lambda: list(stageviz.session().paths()) == ["/World/Rect"]), stageviz.session().paths()
        self.snapshot("light_picked")
        self.activate_mode("Move")
        stageviz.command.clear()
        before = UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath("/World/Rect"))
        end = center + QtCore.QPoint(24, -12)
        widget = self.viewport()
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=center)
        QtTest.QTest.mouseMove(widget, end, 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(100)
        after = UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath("/World/Rect"))
        assert not Gf.IsClose(before, after, 1e-6), "Move gizmo transforms the light"
        stageviz.command.undo()
        assert tb.wait_until(lambda: Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath("/World/Rect")), before, 1e-6))
        stageviz.command.redo()
        assert tb.wait_until(lambda: Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath("/World/Rect")), after, 1e-6))
        stageviz.command.undo()
        tb.process_events(100)
        self.activate_mode("Select")
        stageviz.command.select_paths([])
        widget = self.viewport()
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=QtCore.QPoint(8,8))
        QtTest.QTest.mouseMove(widget, QtCore.QPoint(widget.width()-8,widget.height()-8), 50)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton,
                                 pos=QtCore.QPoint(widget.width()-8,widget.height()-8))
        assert tb.wait_until(lambda: set(stageviz.session().paths()) == set(self.paths)), stageviz.session().paths()
        self.snapshot("lights_swept")
        try:
            stageviz.command.new_light("/World", "Invalid", type="volume")
        except ValueError:
            pass
        else:
            raise AssertionError("invalid USD light kind accepted")
        print("PASS Python light creation, helper drawing, click/sweep selection, Move undo/redo, clean capture")

    def illumination(self):
        stageviz.command.new_prim("/World", "Ball", type_name="Sphere")
        assert tb.wait_until(lambda: bool(stageviz.session().stage().GetPrimAtPath("/World/Ball")))
        state = stageviz.session().viewState()
        state.setMaterialMode(0)
        state.setSceneMaterialsEnabled(True)
        state.setDefaultCameraLightEnabled(False)
        state.setDefaultDomeLightEnabled(False)
        state.setDomeLightCameraVisibility(False)
        state.camera().frame(((-2,-2,-2),(2,2,2)))
        state.camera().distance(1.8)
        stageviz.command.select_paths([])
        images = []
        for enabled in (False, True):
            state.setSceneLightsEnabled(enabled)
            tb.process_events(150)
            frame = stageviz.render().capture()
            assert "/World/Ball" in frame.paths()
            frame.save(str(self.directory / f"scene_lights_{enabled}.png"))
            images.append(frame.rgba())
        assert images[0] != images[1], "authored USD lights illuminate the document"
        self.snapshot("authored_lighting")
        print("PASS authored USD light illumination follows Scene Lights")

    def distant(self):
        """Isolate a default Distant light so other sources cannot mask a bad preset."""
        assert stageviz.session().newStage()
        stageviz.command.new_prim("/World", "Cube", type_name="Cube")
        assert tb.wait_until(lambda: bool(stageviz.session().stage().GetPrimAtPath("/World/Cube")))
        stageviz.command.new_light("/World", "Sun", type="distant")
        assert tb.wait_until(lambda: list(stageviz.session().paths()) == ["/World/Sun"])
        stage = stageviz.session().stage()
        sun = stage.GetPrimAtPath("/World/Sun")
        assert UsdLux.LightAPI(sun).GetNormalizeAttr().Get(), "normalized Distant preset is visible at intensity 1"
        state = stageviz.session().viewState()
        state.setMaterialMode(0)
        state.setSceneMaterialsEnabled(True)
        state.setSceneLightsEnabled(True)
        state.setDefaultCameraLightEnabled(False)
        state.setDefaultDomeLightEnabled(False)
        state.setDomeLightCameraVisibility(False)
        state.setAmbientOcclusionEnabled(False)
        state.setRenderMode(0)
        camera = state.camera()
        camera.setCameraUp(1)
        camera.setAxisYaw(0)
        camera.setAxisPitch(0)
        camera.setAxisRoll(0)
        camera.setAspectRatioLocked(False)
        camera.frame(((-2,-2,-2),(2,2,2)))
        camera.distance(1.8)
        stageviz.command.select_paths([])
        def brightness(label):
            tb.process_events(100)
            frame = stageviz.render().capture()
            frame.save(str(self.directory / (label + ".png")))
            width, height = frame.size()
            rgba = frame.rgba()
            samples = [rgba[(y*width+x)*4+c] for y in range(height//2-8, height//2+8)
                       for x in range(width//2-8, width//2+8) for c in range(3)]
            return sum(samples)/len(samples)
        forward = brightness("distant_forward")
        normalize_path = "/World/Sun.inputs:normalize"
        stageviz.command.set_attribute_value(normalize_path, False)
        assert tb.wait_until(lambda: not UsdLux.LightAPI(sun).GetNormalizeAttr().Get())
        dim = brightness("distant_unnormalized")
        stageviz.command.set_attribute_value(normalize_path, True)
        assert tb.wait_until(lambda: UsdLux.LightAPI(sun).GetNormalizeAttr().Get())
        assert forward > dim + 10, ("unnormalized small-angle light is too dim for the preset", forward, dim)
        stageviz.command.clear()
        translated = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(10,-2,3))
        stageviz.command.set_transforms(["/World/Sun"], [Gf.Matrix4d(1)], [translated])
        assert tb.wait_until(lambda: Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(sun),translated,1e-6))
        moved = brightness("distant_translated")
        assert abs(forward-moved) < 1, "Distant light position does not affect illumination"
        rotated = Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d(0,1,0),180))
        rotated.SetTranslateOnly(Gf.Vec3d(10,-2,3))
        stageviz.command.set_transforms(["/World/Sun"], [translated], [rotated])
        assert tb.wait_until(lambda: Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(sun),rotated,1e-6))
        backward = brightness("distant_backward")
        assert forward > backward+10, ("default Distant light must visibly illuminate the front face", forward, backward)
        stageviz.command.undo()
        assert tb.wait_until(lambda: Gf.IsClose(UsdGeom.XformCache().GetLocalToWorldTransform(sun),translated,1e-6))
        assert abs(brightness("distant_undo")-forward) < 1
        self.snapshot("distant_verified")
        print("PASS isolated Distant light: forward/unnormalized/translated/backward brightness", forward, dim, moved, backward)


def run(disposable=False):
    test = LightsTest(disposable=disposable)
    try:
        test.setup()
        test.menu()
        test.guides()
        test.illumination()
        test.distant()
    finally:
        test.cleanup()
    return str(test.directory)
