# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Layer color state, menu controls and Outliner strips in a disposable viewer."""

import json
from pathlib import Path
import runpy
import stageviz
from pxr import Gf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


class LayerColorsTest(_base["ViewportTest"]):
    def setup(self):
        assert self.disposable, "use a disposable viewer"
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        _, _, _, QtWidgets = tb.qt()
        assert stageviz.session().newStage()
        self.layer_dialog = next(widget for widget in QtWidgets.QApplication.topLevelWidgets()
                                 if widget.objectName() == "layerDialog")
        assert tb.trigger_action("fileLayers")
        assert tb.wait_until(self.layer_dialog.isVisible)
        self.snapshot("layer_color_before")

    def menu_state(self):
        QtCore, QtGui, _, QtWidgets = tb.qt()
        tree = None
        for widget in tb.all_widgets():
            try:
                if (widget.objectName() == "layerTree" and widget.isVisible()
                        and isinstance(widget, QtWidgets.QTreeWidget)):
                    widget.model()
                    tree = widget
                    break
            except RuntimeError:
                continue
        assert tree is not None
        row = tree.topLevelItem(0)
        tree.setCurrentItem(row)
        position = tree.visualItemRect(row).center()
        state = {}
        def inspect_menu():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QMenu) and widget.isVisible():
                    for action in widget.actions():
                        if action.objectName() in ("layerColor", "layerClearColor"):
                            state[action.objectName()] = action.isEnabled()
                    widget.close()
        QtCore.QTimer.singleShot(50, inspect_menu)
        event = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse, position,
                                       tree.viewport().mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(tree.viewport(), event)
        assert "layerColor" in state and "layerClearColor" in state
        return state

    def layer_strip(self, label):
        _, _, _, QtWidgets = tb.qt()
        for widget in tb.all_widgets():
            try:
                if widget.objectName() != "layerTree" or not widget.isVisible():
                    continue
                row = widget.topLevelItem(0)
                widget.setCurrentItem(row)
                image = widget.viewport().grab().toImage()
                assert image.save(str(self.directory / (label + ".png")))
                y = widget.visualItemRect(row).center().y()
                return image.pixelColor(round(image.devicePixelRatio()), round(y * image.devicePixelRatio()))
            except RuntimeError:
                continue
        raise AssertionError("visible Layer Editor tree")

    def exercise(self):
        QtCore, QtGui, _, QtWidgets = tb.qt()
        assert self.menu_state() == {"layerColor": True, "layerClearColor": False}
        state_file = self.directory / "colors.session"
        state_file.write_text(json.dumps({"version": 7, "layerColors": {"$root": "#ffd04030"}}))
        assert stageviz.session().loadState(str(state_file))
        assert self.menu_state() == {"layerColor": True, "layerClearColor": True}
        stageviz.command.select_paths(["/World"])
        assert tb.wait_for_commands()
        tree = next(widget for widget in tb.all_widgets()
                    if widget.objectName() == "outlinerTree" and widget.isVisible()
                    and isinstance(widget, QtWidgets.QTreeWidget))
        row = tree.findItems("World", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        image = tree.viewport().grab().toImage()
        point = tree.visualItemRect(row).center()
        assert image.pixelColor(round(image.devicePixelRatio()), round(point.y() * image.devicePixelRatio())) == QtGui.QColor("#d04030")
        assert self.layer_strip("layer_editor_color") == QtGui.QColor("#d04030")
        self.snapshot("layer_color_selected")
        assert stageviz.session().saveState(str(state_file))
        assert json.loads(state_file.read_text())["layerColors"]["$root"] == "#ffd04030"
        state_file.write_text(json.dumps({"version": 7, "layerColors": {}}))
        assert stageviz.session().loadState(str(state_file))
        assert self.menu_state() == {"layerColor": True, "layerClearColor": False}
        assert self.layer_strip("layer_editor_cleared") != QtGui.QColor("#d04030")
        self.snapshot("layer_color_cleared")

    def cleanup(self):
        _, _, _, QtWidgets = tb.qt()
        for widget in QtWidgets.QApplication.topLevelWidgets():
            try:
                if widget.objectName() == "layerDialog":
                    widget.hide()
            except RuntimeError:
                continue
        super().cleanup()

    def composition(self):
        QtCore, QtGui, _, QtWidgets = tb.qt()
        base_file = self.directory / 'base-colors.usda'
        fixture = Usd.Stage.CreateNew(str(base_file))
        UsdGeom.Sphere.Define(fixture, '/Sphere')
        assert fixture.GetRootLayer().Save()
        del fixture
        stageviz.command.add_sublayer(str(base_file))
        assert tb.wait_for_commands()
        stage = stageviz.session().stage()
        base_id = stage.GetPrimAtPath('/Sphere').GetPrimStack()[0].layer.identifier
        state_file = self.directory / 'composition.session'

        def palette(root=True, base='#fff046b8'):
            colors = {base_id: base}
            if root:
                colors['$root'] = '#ff6450ff'
            state_file.write_text(json.dumps({'version': 7, 'layerColors': colors}))
            assert stageviz.session().loadState(str(state_file))

        def strips():
            tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
            row = tree.findItems('Sphere', QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
            tree.scrollToItem(row)
            image = tree.viewport().grab().toImage()
            ratio = image.devicePixelRatio()
            y = round(tree.visualItemRect(row).center().y() * ratio)
            return [image.pixelColor(round(x * ratio), y) for x in (1, 4)]

        pink, purple = QtGui.QColor('#f046b8'), QtGui.QColor('#6450ff')
        palette()
        stageviz.command.select_paths(['/Sphere'])
        assert tb.wait_for_commands()
        assert strips()[0] == pink and strips()[1] != purple
        self.snapshot('composition_base')
        before = UsdGeom.XformCache().GetLocalToWorldTransform(stage.GetPrimAtPath('/Sphere'))
        rows = lambda matrix: [list(row) for row in matrix]
        stageviz.command.set_transforms(['/Sphere'], [rows(before)],
                                       [rows(before * Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(1, 2, 3)))])
        assert tb.wait_for_commands()
        for _ in range(25):
            assert strips() == [pink, purple]
        assert [s.layer.identifier for s in stage.GetPrimAtPath('/Sphere').GetPrimStack()] == [
            stage.GetRootLayer().identifier, base_id]
        self.snapshot('composition_root_override')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert strips()[0] == pink and strips()[1] != purple
        self.snapshot('composition_undo')
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert strips() == [pink, purple]
        palette(root=False)
        assert strips()[0] == pink and strips()[1] != purple
        self.snapshot('composition_clear_root_color')
        palette(base='#ff40b080')
        assert strips() == [QtGui.QColor('#40b080'), purple]
        assert list(stageviz.session().paths()) == ['/Sphere']
        assert stage.GetPrimAtPath('/Sphere').GetPrimStack()[-1].layer.ExportToString() == base_file.read_text()
        self.snapshot('composition_recolored')


def run(disposable=False):
    test = LayerColorsTest(disposable=disposable)
    try:
        test.setup()
        test.exercise()
    finally:
        test.cleanup()
    return str(test.directory)
