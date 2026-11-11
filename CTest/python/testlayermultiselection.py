# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Native multi-layer selection, batch context actions and grouped history."""
from pathlib import Path
import json
import runpy
import stageviz
from pxr import Sdf

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class LayerMultiSelectionTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        QtCore = tb.qt()[0]
        self.original_non_native_dialogs = QtCore.QCoreApplication.testAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs)
        QtCore.QCoreApplication.setAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, True)
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.child = self.directory / 'child.usda'
        self.first = self.directory / 'first.usda'
        self.second = self.directory / 'second.usda'
        self.fixture = self.directory / 'scene.usda'
        self.child.write_text('#usda 1.0\ndef Sphere "Child" { }\n')
        self.first.write_text('''#usda 1.0
 (subLayers = [@child.usda@])
 def Xform "First" { }
 def Xform "Shared" { }
''')
        self.second.write_text('#usda 1.0\ndef Xform "Second" { }\nover "Shared" { }\n')
        self.fixture.write_text('#usda 1.0\n(subLayers = [@first.usda@, @second.usda@])\n')
        self.saved_files = {str(p): p.read_text() for p in (self.child, self.first, self.second, self.fixture)}
        assert stageviz.session().load(str(self.fixture))
        assert tb.wait_for_commands()
        _, _, _, QtWidgets = tb.qt()
        dialog = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                      if w.objectName() == 'layerDialog')
        if not dialog.isVisible():
            assert tb.trigger_action('fileLayers')
        assert tb.wait_until(dialog.isVisible)
        self.tree().expandAll()
        self.snapshot('layer_multi_loaded')

    def tree(self):
        _, _, _, QtWidgets = tb.qt()
        dialog = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                      if w.objectName() == 'layerDialog' and w.isVisible())
        tree = dialog.findChild(QtWidgets.QTreeWidget, 'layerTree')
        tree.model()
        return tree

    def row(self, path):
        QtCore, _, _, QtWidgets = tb.qt()
        it = QtWidgets.QTreeWidgetItemIterator(self.tree())
        while it.value():
            if str(it.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1)) == str(path):
                return it.value()
            it += 1
        raise AssertionError('missing layer row ' + str(path))

    def selected(self):
        QtCore, _, _, _ = tb.qt()
        return {str(row.data(0, QtCore.Qt.ItemDataRole.UserRole + 1)) for row in self.tree().selectedItems()}

    def click(self, path, modifiers=None):
        QtCore, _, QtTest, _ = tb.qt()
        tree = self.tree()
        row = self.row(path)
        tree.scrollToItem(row)
        rect = tree.visualItemRect(row)
        point = QtCore.QPoint(rect.left() + 75, rect.center().y())
        QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.MouseButton.LeftButton,
                               modifiers or QtCore.Qt.KeyboardModifier.NoModifier, point)
        tb.process_events(20)

    def menu(self, action_name=None, path=None, color=None):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        tree = self.tree()
        row = self.row(path or self.first)
        point = tree.visualItemRect(row).center()
        observed, failures = {}, []

        def close():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QMenu) and widget.isVisible():
                    widget.close()

        def inspect():
            try:
                menu = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                            if isinstance(w, QtWidgets.QMenu) and w.isVisible()
                            and any(a.objectName() == 'layerSetEdit' for a in w.actions()))
                for action in menu.actions():
                    if action.objectName():
                        observed[action.objectName()] = {'text': action.text(), 'enabled': action.isEnabled()}
                assert menu.grab().save(str(self.directory / 'layer_multi_context.png'))
                assert self.tree().grab().save(str(self.directory / 'layer_multi_tree.png'))
                self.snapshot('layer_multi_menu')
                if action_name:
                    action = next(a for a in menu.actions() if a.objectName() == action_name)
                    assert action.isEnabled(), observed
                    menu.setActiveAction(action)
                    if color:
                        attempts = [0]
                        def choose_color():
                            try:
                                dialogs = [w for w in QtWidgets.QApplication.allWidgets()
                                           if isinstance(w, QtWidgets.QColorDialog)]
                                if dialogs:
                                    dialogs[0].setCurrentColor(QtGui.QColor(color))
                                    dialogs[0].accept()
                                elif attempts[0] < 80:
                                    attempts[0] += 1
                                    QtCore.QTimer.singleShot(50, choose_color)
                                else:
                                    failures.append('no color dialog appeared')
                                    for widget in QtWidgets.QApplication.allWidgets():
                                        if isinstance(widget, QtWidgets.QColorDialog):
                                            widget.reject()
                            except Exception as error:
                                failures.append(repr(error))
                        QtCore.QTimer.singleShot(50, choose_color)
                    QtTest.QTest.keyClick(menu, QtCore.Qt.Key.Key_Return)
                else:
                    close()
            except Exception as error:
                failures.append(repr(error))
                close()

        watchdog = QtCore.QTimer(tree)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close)
        watchdog.start(5000)
        QtCore.QTimer.singleShot(50, inspect)
        event = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse, point,
                                       tree.viewport().mapToGlobal(point))
        QtWidgets.QApplication.sendEvent(tree.viewport(), event)
        watchdog.stop()
        watchdog.deleteLater()
        assert not failures, failures
        assert observed
        assert tb.wait_for_commands()
        return observed

    def selection(self):
        QtCore, _, _, QtWidgets = tb.qt()
        assert self.tree().selectionMode() == QtWidgets.QAbstractItemView.SelectionMode.ExtendedSelection
        self.click(self.first)
        self.click(self.second, QtCore.Qt.KeyboardModifier.ControlModifier)
        assert self.selected() == {str(self.first), str(self.second)}, self.selected()
        self.click(self.first)
        self.click(self.second, QtCore.Qt.KeyboardModifier.ShiftModifier)
        assert self.selected() == {str(self.first), str(self.child), str(self.second)}, self.selected()
        self.click(self.child, QtCore.Qt.KeyboardModifier.ControlModifier)
        assert self.selected() == {str(self.first), str(self.second)}
        stageviz.command.set_edit_layer(str(self.second))
        assert tb.wait_for_commands()
        assert self.selected() == {str(self.first), str(self.second)}
        stageviz.command.set_edit_layer(str(self.fixture))
        assert tb.wait_for_commands()
        menu = self.menu('layerSelect')
        assert not menu['layerSetEdit']['enabled']
        assert menu['layerMute']['text'] == 'Mute layers'
        assert set(stageviz.session().paths()) == {'/First', '/Shared', '/Second'}
        assert self.selected() == {str(self.first), str(self.second)}
        self.snapshot('layer_multi_selected')
        # Right-clicking an unselected layer replaces the previous selection.
        self.menu(path=self.child)
        assert self.selected() == {str(self.child)}
        self.click(self.first)
        self.click(self.second, QtCore.Qt.KeyboardModifier.ControlModifier)

    def actions(self):
        session = stageviz.session()
        stage = session.stage()
        first = Sdf.Layer.Find(str(self.first))
        second = Sdf.Layer.Find(str(self.second))
        child = Sdf.Layer.Find(str(self.child))
        for action, changed, restored in (
            ('layerLock', lambda: not first.permissionToEdit and not second.permissionToEdit,
             lambda: first.permissionToEdit and second.permissionToEdit),
            ('layerMute', lambda: stage.IsLayerMuted(str(self.first)) and stage.IsLayerMuted(str(self.second)),
             lambda: not stage.IsLayerMuted(str(self.first)) and not stage.IsLayerMuted(str(self.second)))):
            self.menu(action)
            assert changed()
            assert self.selected() == {str(self.first), str(self.second)}
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert restored()
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert changed()
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert restored()
        state_file = self.directory / 'colors.session'
        self.menu('layerColor', color='#38b75e')
        assert session.saveState(str(state_file))
        colors = json.loads(state_file.read_text())['layerColors']
        assert colors == {str(self.first): '#ff38b75e', str(self.second): '#ff38b75e'}, colors
        self.menu('layerClearColor')
        assert session.saveState(str(state_file))
        assert not json.loads(state_file.read_text()).get('layerColors')
        # Save all selected dirty file-backed layers.
        first.documentation = 'multi-save first'
        second.documentation = 'multi-save second'
        self.menu('layerSave')
        assert not first.dirty and not second.dirty
        self.menu('layerRemove')
        assert not stage.HasLocalLayer(first) and not stage.HasLocalLayer(second)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert stage.HasLocalLayer(first) and stage.HasLocalLayer(second) and stage.HasLocalLayer(child)
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert not stage.HasLocalLayer(first)
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.snapshot('layer_multi_restored')
        # Reload and restore disposable fixture files before cleanup.
        for filename, text in self.saved_files.items():
            Path(filename).write_text(text)

    def batches(self):
        stage = stageviz.session().stage()
        ids = [str(self.first), str(self.child), str(self.second)]
        first, child, second = [Sdf.Layer.Find(identifier) for identifier in ids]
        for command, args, changed in (
            (stageviz.command.set_layer_muted, (ids, True), lambda: all(stage.IsLayerMuted(i) for i in ids)),
            (stageviz.command.set_layer_permission_to_edit, (ids, False), lambda: all(not layer.permissionToEdit for layer in (first, child, second))),
            (stageviz.command.remove_layer, (ids,), lambda: not stage.GetRootLayer().subLayerPaths)):
            command(*args)
            assert tb.wait_for_commands()
            assert changed()
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert all(stage.HasLocalLayer(layer) and layer.permissionToEdit for layer in (first, child, second))
            assert not any(stage.IsLayerMuted(i) for i in ids)
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert changed()
            stageviz.command.undo()
            assert tb.wait_for_commands()

    def cleanup(self):
        QtCore = tb.qt()[0]
        QtCore.QCoreApplication.setAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, self.original_non_native_dialogs)
        super().cleanup()
