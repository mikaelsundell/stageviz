# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Native material assignment promotes proxies and preserves instance geometry."""
import contextlib
import io
import json
from pathlib import Path
import statistics
import sys
import tempfile
import traceback

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade
try:
    from PySide6 import QtCore, QtGui, QtTest, QtWidgets, QtOpenGLWidgets
except ImportError:
    from PyQt6 import QtCore, QtGui, QtTest, QtWidgets, QtOpenGLWidgets
sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


class MaterialAssignmentTest:
    def __init__(self, disposable=False):
        assert disposable, 'material assignment tests require a disposable app'
        self.directory = Path(tempfile.mkdtemp(prefix='stageviz-material-assignment-'))
        self.session = stageviz.session()
        self.a = '/World/A'
        self.b = '/World/B'
        self.left = self.a + '/Group/Left'
        self.right = self.a + '/Group/Right'
        self.paths = [root + '/Group/' + name for root in (self.a, self.b) for name in ('Left', 'Right')]
        self.paths.append('/World/Ordinary')
        self.expected = dict.fromkeys(self.paths, 'Red')

    def command(self, function, *args):
        function(*args)
        assert tb.wait_for_commands()

    def setup(self):
        base = Usd.Stage.CreateNew(str(self.directory / 'base.usda'))
        UsdGeom.SetStageUpAxis(base, UsdGeom.Tokens.y)
        for name, color in [('Red', (1, 0, 0)), ('Green', (0, 1, 0)), ('Blue', (0, 0, 1))]:
            material = UsdShade.Material.Define(base, '/Materials/' + name)
            shader = UsdShade.Shader.Define(base, '/Materials/' + name + '/Shader')
            shader.CreateIdAttr('UsdPreviewSurface')
            shader.CreateInput('diffuseColor', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0))
            shader.CreateInput('emissiveColor', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*color))
            material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
        UsdGeom.Xform.Define(base, '/Library').GetVisibilityAttr().Set('invisible')
        UsdGeom.Xform.Define(base, '/Library/Source/Group')
        red = UsdShade.Material(base.GetPrimAtPath('/Materials/Red'))
        for name, x in [('Left', -1.2), ('Right', 1.2)]:
            cube = UsdGeom.Cube.Define(base, '/Library/Source/Group/' + name)
            cube.CreateSizeAttr(1.8)
            cube.AddTranslateOp().Set(Gf.Vec3d(x, 0, 0))
            UsdShade.MaterialBindingAPI.Apply(cube.GetPrim()).Bind(red)
        for name, x in [('A', -4), ('B', 4)]:
            instance = UsdGeom.Xform.Define(base, '/World/' + name)
            instance.GetPrim().GetReferences().AddInternalReference('/Library/Source')
            instance.GetPrim().SetInstanceable(True)
            instance.AddTranslateOp().Set(Gf.Vec3d(x, 0, 0))
        ordinary = UsdGeom.Cube.Define(base, '/World/Ordinary')
        ordinary.CreateSizeAttr(1.8)
        UsdShade.MaterialBindingAPI.Apply(ordinary.GetPrim()).Bind(red)
        base.GetRootLayer().Save()
        base = None
        look = Sdf.Layer.CreateNew(str(self.directory / 'look.usda'))
        look.Save()
        root = Sdf.Layer.CreateNew(str(self.directory / 'scene.usda'))
        root.subLayerPaths = [look.identifier, str(self.directory / 'base.usda')]
        root.Save()
        root = None
        assert self.session.load(str(self.directory / 'scene.usda')) and tb.wait_for_commands()
        assert self.session.setEditLayer(look.identifier) and tb.wait_for_commands()
        self.look = self.session.stage().GetEditTarget().GetLayer()
        self.original = self.look.ExportToString()
        self.base = Sdf.Layer.FindOrOpen(str(self.directory / 'base.usda'))
        self.base_original = self.base.ExportToString()
        state = self.session.viewState()
        state.setGridEnabled(False)
        state.setRenderMode(stageviz.RenderShaded)
        tb.viewer().resize(1500, 1000)
        state.camera().setCameraView(stageviz.CameraViewFront)
        state.camera().frame(self.session.boundingBox(['/World']))
        state.camera().distance(1.15)
        tb.process_events(150)
        self.snapshot('baseline')

    def snapshot(self, label):
        tb.process_events(100)
        stage = self.session.stage()
        a, b = [stage.GetPrimAtPath(path) for path in (self.a, self.b)]
        assert a.IsInstance() and b.IsInstance() and a.GetPrototype() == b.GetPrototype()
        assert all(stage.GetPrimAtPath(path).IsInstanceProxy() for path in self.paths[:-1])
        resolved = {path: str(UsdShade.MaterialBindingAPI(stage.GetPrimAtPath(path)).ComputeBoundMaterial()[0].GetPath())
                    for path in self.paths}
        assert resolved == {path: '/Materials/' + material for path, material in self.expected.items()}, (label, resolved)
        frame = stageviz.render().capture()
        visible = frame.paths()
        assert set(visible) == set(self.paths), (label, visible)
        ids = memoryview(frame.objectIds()).cast('i')
        rgba = frame.rgba()
        samples = {i: [] for i in range(len(visible))}
        for pixel, index in enumerate(ids):
            if index >= 0:
                samples[index].append(pixel)
        colors = {}
        for i, path in enumerate(visible):
            pixels = samples[i][::max(1, len(samples[i]) // 100)]
            rgb = [statistics.median(rgba[pixel * 4 + channel] for pixel in pixels) for channel in range(3)]
            channel = {'Red': 0, 'Green': 1, 'Blue': 2}[self.expected[path]]
            assert rgb[channel] > 40 and all(rgb[channel] > rgb[c] + 20 for c in range(3) if c != channel), (
                label, path, self.expected[path], rgb)
            colors[path] = rgb
        assert self.base.ExportToString() == self.base_original, 'source asset remains unchanged'
        frame.save(str(self.directory / (label + '-capture.png')))
        assert tb.viewer().grab().save(str(self.directory / (label + '.png')))
        with contextlib.redirect_stdout(io.StringIO()):
            qt = tb.snapshot_qt_state(label)
        (self.directory / (label + '.json')).write_text(json.dumps(dict(qt=qt, resolved=resolved, colors=colors), indent=2))
        print('MATERIAL_ASSIGNMENT_CHECKPOINT', json.dumps(dict(label=label, resolved=resolved, colors=colors)))

    def api(self):
        self.command(stageviz.command.assign_material, [self.a], '/Materials/Blue')
        self.expected.update({self.left: 'Blue', self.right: 'Blue'})
        self.snapshot('parent-assigned')
        assigned = self.look.ExportToString()
        self.command(stageviz.command.undo)
        self.expected = dict.fromkeys(self.paths, 'Red')
        assert self.look.ExportToString() == self.original
        self.snapshot('parent-undo')
        self.command(stageviz.command.redo)
        assert self.look.ExportToString() == assigned
        self.expected.update({self.left: 'Blue', self.right: 'Blue'})
        self.command(stageviz.command.select_paths, [self.left])
        self.command(stageviz.command.assign_material, [self.left], '/Materials/Green')
        assert list(self.session.paths()) == [self.left]
        self.expected.update({self.left: 'Green', self.right: 'Green'})
        assert self.look.GetRelationshipAtPath(self.a + '.material:binding')
        assert not self.look.GetPrimAtPath(self.left)
        self.snapshot('proxy-promoted-to-parent')
        child = self.look.ExportToString()
        self.command(stageviz.command.clear_material, [self.left])
        self.expected.update({self.left: 'Red', self.right: 'Red'})
        self.snapshot('proxy-clear-parent-binding')
        self.command(stageviz.command.undo)
        assert self.look.ExportToString() == child
        self.expected.update({self.left: 'Green', self.right: 'Green'})
        self.snapshot('clear-undo')
        self.command(stageviz.command.redo)
        self.expected.update({self.left: 'Red', self.right: 'Red'})
        self.snapshot('clear-redo')
        self.command(stageviz.command.undo)  # Clear.
        self.command(stageviz.command.undo)  # Child Assign.
        self.command(stageviz.command.undo)  # Selection.
        self.command(stageviz.command.undo)  # Parent Assign.
        self.expected = dict.fromkeys(self.paths, 'Red')
        assert self.look.ExportToString() == self.original
        self.command(stageviz.command.assign_material, [self.left, '/World/Ordinary'], '/Materials/Blue')
        self.expected.update({self.left: 'Blue', self.right: 'Blue', '/World/Ordinary': 'Blue'})
        self.snapshot('mixed-assignment')
        self.command(stageviz.command.undo)
        self.expected = dict.fromkeys(self.paths, 'Red')
        assert self.look.ExportToString() == self.original
        self.command(stageviz.command.assign_material, ['/World'], '/Materials/Green')
        self.expected = dict.fromkeys(self.paths, 'Green')
        self.snapshot('ancestor-group-assigned')
        self.command(stageviz.command.undo)
        self.expected = dict.fromkeys(self.paths, 'Red')
        assert self.look.ExportToString() == self.original

    def dialog(self):
        dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog')
        if not dialog.isVisible():
            action = tb.viewer().findChild(QtGui.QAction, 'materialEditor')
            assert action is not None
            action.trigger()
        assert tb.wait_until(dialog.isVisible)
        return dialog

    def menu(self, target, position, label, submenu=None, keyboard=False, enabled=True):
        observed, errors, menu_objects = [], [], []
        def close():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QMenu) and widget.isVisible():
                    widget.close()
        def choose():
            try:
                menu = QtWidgets.QApplication.activePopupWidget()
                assert isinstance(menu, QtWidgets.QMenu)
                action = next(a for a in menu.actions() if a.text() == label)
                assert action.isEnabled() == enabled
                if submenu == 'Material':
                    assert ['separator' if a.isSeparator() else a.text() for a in menu.actions()] == ['Source', 'separator', 'Clear']
                    assert menu.grab().save(str(self.directory / ('material-menu-' + label + '-' + str(enabled) + '.png')))
                observed.append(label)
                if enabled:
                    menu.setActiveAction(action)
                    QtTest.QTest.keyPress(menu, QtCore.Qt.Key.Key_Return)
                else:
                    close()
            except Exception as error:
                errors.append(traceback.format_exc())
                close()
        def open_submenu():
            try:
                parent = QtWidgets.QApplication.activePopupWidget()
                assert isinstance(parent, QtWidgets.QMenu)
                if submenu == 'Material':
                    labels = [a.text() for a in parent.actions() if not a.isSeparator()]
                    assert labels.index('Material') == labels.index('Transform') + 1, labels
                    actions = parent.actions()
                    menu_objects.extend(actions)
                    material_index = next(i for i, a in enumerate(actions) if a.text() == 'Material')
                    assert actions[material_index - 1].isSeparator()
                    pivot_action = next(a for a in actions if a.text() == 'Pivot')
                    pivot = pivot_action.menu()
                    menu_objects.append(pivot)
                    assert ['separator' if a.isSeparator() else a.text() for a in pivot.actions()] == ['Center', 'separator', 'Reset']
                action = next(a for a in parent.actions() if a.text() == submenu)
                child = action.menu()
                menu_objects.extend([parent, action, child])
                parent.setActiveAction(action)
                child.popup(parent.mapToGlobal(parent.actionGeometry(action).topRight()))
                QtCore.QTimer.singleShot(50, choose)
            except Exception:
                errors.append(traceback.format_exc())
                close()
        watchdog = QtCore.QTimer(target)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close)
        watchdog.start(5000)
        QtCore.QTimer.singleShot(50, open_submenu if submenu else choose)
        reason = QtGui.QContextMenuEvent.Reason.Keyboard if keyboard else QtGui.QContextMenuEvent.Reason.Mouse
        event = QtGui.QContextMenuEvent(reason, position, target.mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(target, event)
        watchdog.stop()
        watchdog.deleteLater()
        assert observed and not errors, errors
        assert tb.wait_for_commands()

    def material_menu(self, material, label):
        dialog = self.dialog()
        icons = dialog.findChild(QtWidgets.QAbstractItemView, 'icons')
        assert tb.wait_until(lambda: icons.model().rowCount() == 3)
        index = next(icons.model().index(row, 0) for row in range(3)
                     if icons.model().index(row, 0).data() == material)
        icons.scrollTo(index)
        icons.setCurrentIndex(index)
        icons.selectionModel().select(index, QtCore.QItemSelectionModel.SelectionFlag.ClearAndSelect
                                      | QtCore.QItemSelectionModel.SelectionFlag.Rows)
        tb.process_events(50)
        assert icons.currentIndex().data() == material
        self.menu(icons.viewport(), icons.visualRect(icons.currentIndex()).center(), label, keyboard=True)

    def ui(self):
        self.command(stageviz.command.select_paths, [self.a])
        self.material_menu('Blue', 'Assign')
        self.expected.update({self.left: 'Blue', self.right: 'Blue'})
        self.snapshot('editor-parent-assign')
        self.material_menu('Blue', 'Select')
        assert set(self.session.paths()) == {self.left, self.right}
        self.command(stageviz.command.select_paths, [self.a])
        dialog = self.dialog()
        select = dialog.findChild(QtWidgets.QToolButton, 'select')
        assert select is not None
        select.click()
        tb.process_events(100)
        icons = dialog.findChild(QtWidgets.QAbstractItemView, 'icons')
        assert {index.data() for index in icons.selectionModel().selectedRows()} == {'Blue'}
        dialog.hide()
        self.command(stageviz.command.select_paths, [self.left])
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        it = QtWidgets.QTreeWidgetItemIterator(tree)
        while it.value() and it.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) != self.left:
            it += 1
        assert it.value() is not None
        item = it.value()
        tree.scrollToItem(item)
        tb.process_events(50)
        self.menu(tree.viewport(), tree.visualItemRect(item).center(), 'Source', submenu='Material')
        assert list(self.session.paths()) == ['/Materials/Blue']
        self.command(stageviz.command.undo)
        assert list(self.session.paths()) == [self.left]
        # Reacquire the row after selection updates.
        it = QtWidgets.QTreeWidgetItemIterator(tree)
        while it.value() and it.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) != self.left:
            it += 1
        assert it.value() is not None
        self.menu(tree.viewport(), tree.visualItemRect(it.value()).center(), 'Clear', submenu='Material')
        self.expected.update({self.left: 'Red', self.right: 'Red'})
        self.snapshot('context-clear-proxy')
        it = QtWidgets.QTreeWidgetItemIterator(tree)
        while it.value() and it.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) != self.left:
            it += 1
        assert it.value() is not None
        self.menu(tree.viewport(), tree.visualItemRect(it.value()).center(), 'Clear', submenu='Material', enabled=False)
        self.command(stageviz.command.undo)
        self.expected.update({self.left: 'Blue', self.right: 'Blue'})
        self.snapshot('context-clear-undo')
        for _ in range(8):
            if self.look.ExportToString() == self.original:
                break
            assert stageviz.command.can_undo()
            self.command(stageviz.command.undo)
        assert self.look.ExportToString() == self.original
        self.expected = dict.fromkeys(self.paths, 'Red')

    def drop(self):
        self.command(stageviz.command.select_paths, [])
        viewport = next(w for w in tb.all_widgets() if w.objectName() == 'imagingGLWidget' and w.isVisible())
        frame = stageviz.render().capture()
        target = self.b + '/Group/Right'
        index = frame.paths().index(target)
        pixels = [pixel for pixel, value in enumerate(memoryview(frame.objectIds()).cast('i')) if value == index]
        assert pixels
        width, _ = frame.size()
        center_x = statistics.median(pixel % width for pixel in pixels)
        center_y = statistics.median(pixel // width for pixel in pixels)
        pixel = min(pixels, key=lambda value: (value % width - center_x) ** 2 + (value // width - center_y) ** 2)
        ratio = viewport.devicePixelRatioF()
        point = QtCore.QPoint(int((pixel % width) / ratio), int((pixel // width) / ratio))
        mime = QtCore.QMimeData()
        mime.setData('application/x-stageviz-material', b'/Materials/Green')
        enter = QtGui.QDragEnterEvent(point, QtCore.Qt.DropAction.CopyAction, mime,
                                     QtCore.Qt.MouseButton.LeftButton, QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(viewport, enter)
        assert enter.isAccepted()
        move = QtGui.QDragMoveEvent(point, QtCore.Qt.DropAction.CopyAction, mime,
                                   QtCore.Qt.MouseButton.LeftButton, QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(viewport, move)
        assert move.isAccepted(), ('drop target is pickable', point.x(), point.y(), frame.pathAt(pixel % width, pixel // width))
        event = QtGui.QDropEvent(QtCore.QPointF(point), QtCore.Qt.DropAction.CopyAction, mime,
                                QtCore.Qt.MouseButton.LeftButton, QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(viewport, event)
        assert event.isAccepted() and tb.wait_for_commands()
        self.expected.update({self.b + '/Group/Left': 'Green', target: 'Green'})
        self.snapshot('viewport-drop')
        self.command(stageviz.command.undo)
        self.expected.update({self.b + '/Group/Left': 'Red', target: 'Red'})
        assert self.look.ExportToString() == self.original
        self.snapshot('drop-undo')

    def cleanup(self):
        assert self.session.newStage() and tb.wait_for_commands()
        stageviz.command.clear()
        print('Material assignment artifacts:', self.directory)


def run(disposable=False):
    test = MaterialAssignmentTest(disposable=disposable)
    try:
        test.setup()
        test.api()
        test.ui()
        test.drop()
    finally:
        test.cleanup()
