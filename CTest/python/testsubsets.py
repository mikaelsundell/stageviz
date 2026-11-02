# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Verify geometry subsets as owner properties in a disposable live viewer.

Use _SUBSETS_AUTORUN=False to run phases from an external MCP controller.
Set _VIEWPORT_DISPOSABLE=True only in a disposable process. The controller
must issue its read-only MCP observations outside the embedded interpreter.
"""

from pathlib import Path
import runpy
import sys

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade

_directory = Path(__file__).resolve().parent
if str(_directory) not in sys.path:
    sys.path.insert(0, str(_directory))
import testbench as tb

_Base = runpy.run_path(str(_directory / "testviewport.py"),
                      init_globals={"_VIEWPORT_AUTORUN": False})["ViewportTest"]


class SubsetTest(_Base):
    owners = ("/World/Plain/Mesh", "/World/Instance/Mesh")

    def tree(self, name):
        QtWidgets = tb.qt()[3]
        for widget in tb.all_widgets():
            try:
                if widget.objectName() == name and isinstance(widget, QtWidgets.QTreeWidget) and widget.isVisible():
                    return widget
            except RuntimeError:
                continue
        raise AssertionError("visible " + name)

    def setup(self):
        session = stageviz.session()
        original = session.stage()
        assert self.disposable or not tb.dirty_layers(original), "use a disposable viewer or a clean saved stage"
        self.original_filename = session.filename()
        self.original_selection = list(session.paths())
        path = self.directory / "subsets.usda"
        fixture = Usd.Stage.CreateNew(str(path))
        world = UsdGeom.Xform.Define(fixture, "/World")
        fixture.SetDefaultPrim(world.GetPrim())
        UsdGeom.SetStageUpAxis(fixture, UsdGeom.Tokens.y)
        plain = UsdGeom.Xform.Define(fixture, "/World/Plain")
        plain.AddTranslateOp().Set(Gf.Vec3d(-2, 0, 0))
        mesh = UsdGeom.Mesh.Define(fixture, self.owners[0])
        mesh.CreatePointsAttr([(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)])
        mesh.CreateFaceVertexCountsAttr([3, 3])
        mesh.CreateFaceVertexIndicesAttr([0, 1, 2, 0, 2, 3])
        mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        mesh.CreateDoubleSidedAttr(True)
        for name, indices, color in (("Front", [0], (0.8, 0.02, 0.02)), ("Back", [1], (0.02, 0.04, 0.8))):
            subset = UsdGeom.Subset.CreateGeomSubset(mesh, name, UsdGeom.Tokens.face, indices, "materialBind")
            material = UsdShade.Material.Define(fixture, "/World/Plain/Looks/" + name)
            shader = UsdShade.Shader.Define(fixture, str(material.GetPath()) + "/Surface")
            shader.CreateIdAttr("UsdPreviewSurface")
            shader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*color))
            shader.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.5)
            material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), "surface")
            UsdShade.MaterialBindingAPI.Apply(subset.GetPrim()).Bind(material)
        UsdGeom.Subset.SetFamilyType(mesh, "materialBind", UsdGeom.Tokens.partition)
        instance = UsdGeom.Xform.Define(fixture, "/World/Instance")
        instance.GetPrim().GetReferences().AddInternalReference("/World/Plain")
        instance.GetPrim().SetInstanceable(True)
        instance.GetPrim().GetAttribute("xformOp:translate").Set(Gf.Vec3d(2, 0, 0))
        fixture.GetRootLayer().Save()
        self.fixture_path = str(path)
        assert session.loadFromFile(self.fixture_path)
        tb.process_events(200)
        self.activate_mode("Select")
        state = session.viewState()
        state.setMaterialMode(0)
        state.setSceneMaterialsEnabled(True)
        state.setGridEnabled(False)
        camera = state.camera()
        camera.setCameraUp(1)
        camera.setAxisYaw(0)
        camera.setAxisPitch(0)
        camera.setAxisRoll(0)
        camera.setProjectionMode(0)
        camera.setAspectRatioLocked(False)
        camera.frame(((-3, -1, 0), (3, 1, 0)))
        camera.distance(1.6)
        tb.process_events(250)
        self.snapshot("subsets_loaded")

    def point(self, owner, local):
        widget = self.viewport()
        camera = stageviz.session().viewState().camera()
        cache = UsdGeom.XformCache()
        world = cache.GetLocalToWorldTransform(stageviz.session().stage().GetPrimAtPath(owner)).Transform(Gf.Vec3d(*local))
        focus = camera.focusPoint()
        scale = camera.mapToFrustumHeight(widget.height())
        return (round(widget.width() / 2 + (world[0] - focus[0]) / scale),
                round(widget.height() / 2 - (world[1] - focus[1]) / scale))

    def check_properties(self, owner):
        assert tb.wait_until(lambda: self._check_properties(owner)), "owner properties settle after queued notices"

    def _check_properties(self, owner):
        stageviz.command.select_paths([owner])
        tb.process_events(80)
        tree = self.tree("propertyTree")
        match = tb.qt()[0].Qt.MatchFlag
        sections = tree.findItems("subsets", match.MatchExactly | match.MatchRecursive, 0)
        attributes = tree.findItems("Attributes", match.MatchExactly, 0)[0]
        assert len(sections) == 1 and sections[0].childCount() == 2
        assert sections[0].text(1) == "2 values"
        assert any(attributes.child(i) == sections[0] for i in range(attributes.childCount()))
        tree.expandItem(attributes)
        tree.expandItem(sections[0])
        for index in range(2):
            group = sections[0].child(index)
            name = group.text(0)
            assert name in ("Front", "Back")
            tree.expandItem(group)
            rows = {group.child(i).text(0): group.child(i) for i in range(group.childCount())}
            assert rows["indices"].text(1) == "1 values"
            assert rows["material:binding"].text(1) == owner.rsplit("/", 1)[0] + "/Looks/" + name
            tree.expandItem(rows["indices"])
            assert rows["indices"].child(0).text(1) == ("0" if name == "Front" else "1")
        for item in self.rows(self.tree("outlinerTree").invisibleRootItem()):
            path = item.data(0, int(tb.qt()[0].Qt.ItemDataRole.UserRole) + 1)
            if path:
                prim = stageviz.session().stage().GetPrimAtPath(str(path))
                assert not prim or not prim.IsA(UsdGeom.Subset), "subsets are absent from the Outliner"
        tree.scrollToItem(sections[0], tb.qt()[3].QAbstractItemView.ScrollHint.PositionAtTop)
        self.snapshot("properties_" + owner.split("/")[2].lower())
        return True

    @staticmethod
    def rows(parent):
        for index in range(parent.childCount()):
            child = parent.child(index)
            yield child
            yield from SubsetTest.rows(child)

    def clicks_and_rendering(self):
        QtCore, _, QtTest, _ = tb.qt()
        for owner in self.owners:
            for local in ((0.35, -0.35, 0), (-0.35, 0.35, 0)):
                point = QtCore.QPoint(*self.point(owner, local))
                QtTest.QTest.mouseClick(self.viewport(), QtCore.Qt.MouseButton.LeftButton, pos=point)
                tb.process_events(100)
                assert list(stageviz.session().paths()) == [owner], "face clicks select only their owning geometry"
            image = self.viewport().grabFramebuffer()
            ratio = image.width() / self.viewport().width()
            red_point = self.point(owner, (0.35, -0.35, 0))
            blue_point = self.point(owner, (-0.35, 0.35, 0))
            red = image.pixelColor(round(red_point[0] * ratio), round(red_point[1] * ratio))
            blue = image.pixelColor(round(blue_point[0] * ratio), round(blue_point[1] * ratio))
            assert red.red() > red.blue() * 1.5, "front faces retain their red material"
            assert blue.blue() > blue.red() * 1.5, "back faces retain their blue material"
        self.snapshot("owner_picking")

    def binding_undo_redo(self):
        owner = self.owners[0]
        stageviz.command.select_paths([owner])
        command = stageviz.command
        command.clear()
        subset_path = owner + "/Front"
        red_path = "/World/Plain/Looks/Front"
        blue_path = "/World/Plain/Looks/Back"

        def binding():
            material, _ = UsdShade.MaterialBindingAPI(stageviz.session().stage().GetPrimAtPath(subset_path)).ComputeBoundMaterial()
            return str(material.GetPath())

        command.bind_material([subset_path], blue_path)
        assert tb.wait_until(lambda: binding() == blue_path)
        command.undo()
        assert tb.wait_until(lambda: binding() == red_path)
        command.redo()
        assert tb.wait_until(lambda: binding() == blue_path)
        assert list(stageviz.session().paths()) == [owner]
        self.snapshot("binding_redone")
        command.undo()
        assert tb.wait_until(lambda: binding() == red_path)
        # Selection APIs preserve both exact subset paths.
        selected = [owner + "/Front", owner + "/Back"]
        command.select_paths(selected)
        tb.process_events(80)
        assert list(stageviz.session().paths()) == selected
        self.check_properties(owner)

    def subset_selection(self, owner):
        QtCore, _, _, _ = tb.qt()
        stageviz.command.select_paths([])
        tb.process_events(100)
        unselected = self.viewport().grabFramebuffer()
        stageviz.command.select_paths([owner])
        outliner = self.tree("outlinerTree")
        outliner.expandAll()
        tb.process_events(100)
        parent_outline = self.viewport().grabFramebuffer()
        assert parent_outline != unselected, "the parent mesh has a visible selection outline"
        parent_rows = outliner.grab().toImage()
        subset = owner + "/Front"
        stageviz.command.select_paths([subset])
        tb.process_events(100)
        assert list(stageviz.session().paths()) == [subset], "Session retains the exact selected subset"
        assert self.viewport().grabFramebuffer() == parent_outline, "subset selection outlines the parent mesh"
        assert outliner.grab().toImage() == parent_rows, "subset selection highlights the parent Outliner row"
        tree = self.tree("propertyTree")
        prim = tree.findItems("Prim", QtCore.Qt.MatchFlag.MatchExactly, 0)[0]
        paths = [prim.child(i).text(1) for i in range(prim.childCount()) if prim.child(i).text(0) == "Path"]
        assert paths == [subset], "PropertyTree inspects the selected subset itself"
        attributes = tree.findItems("Attributes", QtCore.Qt.MatchFlag.MatchExactly, 0)[0]
        assert any(attributes.child(i).text(0) == "indices" for i in range(attributes.childCount()))
        tree.scrollToItem(prim)
        self.snapshot("selected_subset_" + owner.split("/")[2].lower())
        assert list(stageviz.session().paths()) == [subset], "display projection never writes back to Session"

    def sweep(self):
        QtCore, _, QtTest, _ = tb.qt()
        self.activate_mode("Select")
        widget = self.viewport()
        QtTest.QTest.mousePress(widget, QtCore.Qt.MouseButton.LeftButton, pos=QtCore.QPoint(8, 8))
        end = QtCore.QPoint(widget.width() - 8, widget.height() - 8)
        QtTest.QTest.mouseMove(widget, end, 30)
        QtTest.QTest.mouseRelease(widget, QtCore.Qt.MouseButton.LeftButton, pos=end)
        tb.process_events(100)
        assert set(stageviz.session().paths()) == set(self.owners)
        tree = self.tree("propertyTree")
        attributes = tree.findItems("Attributes", QtCore.Qt.MatchFlag.MatchExactly, 0)[0]
        tree.expandItem(attributes)
        section = tree.findItems("subsets", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        tree.expandItem(section)
        assert section.childCount() == 2, "multi-selection groups subsets by owning geometry"
        # Descendant edits must refresh an already-expanded multi-owner section.
        owner = self.owners[0]
        stageviz.command.bind_material([owner + "/Front"], owner.rsplit("/", 1)[0] + "/Looks/Back")
        tb.process_events(100)
        section = tree.findItems("subsets", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        assert section.isExpanded() and section.childCount() == 2
        group = next(section.child(i) for i in range(section.childCount()) if section.child(i).text(0) == owner)
        front = next(group.child(i) for i in range(group.childCount()) if group.child(i).text(0) == "Front")
        assert front.text(1) == owner.rsplit("/", 1)[0] + "/Looks/Back"
        stageviz.command.undo()
        tb.process_events(100)
        self.snapshot("subset_sweep")


def run(disposable=False):
    test = SubsetTest(disposable=disposable)
    try:
        test.setup()
        for owner in test.owners:
            test.check_properties(owner)
        test.clicks_and_rendering()
        test.binding_undo_redo()
        for owner in test.owners:
            test.subset_selection(owner)
        test.sweep()
        return True
    finally:
        test.cleanup()


if globals().get("_SUBSETS_AUTORUN", True):
    _TEST_RESULT = run(disposable=globals().get("_VIEWPORT_DISPOSABLE", False))
