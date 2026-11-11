# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Edit-layer opinion groups, context-menu presentation and undo isolation."""
from pathlib import Path
import runpy
import stageviz
from pxr import Sdf

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class ResetOpinionsTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.base = self.directory / 'base.usda'
        self.base.write_text('''#usda 1.0
 def Xform "Group" {
    def Sphere "Sphere" { }
 }
''')
        self.fixture = self.directory / 'opinions.usda'
        self.fixture.write_text('''#usda 1.0
 (subLayers = [@base.usda@])
 over "Group" {
    over "Sphere" (displayName = "Retained metadata") {
        double3 xformOp:translate = (3, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        token visibility = "invisible"
        rel material:binding = </Looks/Material>
        double radius = 4
        float primvars:test = 0.5
        string customNote = "retained"
    }
 }
 def Scope "Looks" {
    def Material "Material" { }
 }
 def Sphere "Owned" {
    double radius = 8
 }
''')
        assert stageviz.session().load(str(self.fixture))
        assert tb.wait_for_commands()
        stageviz.command.select_paths(['/Group'])
        assert tb.wait_for_commands()
        self.snapshot('reset_opinions_loaded')

    def menu(self, group=None, locked=False):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        tree = next(w for w in tb.all_widgets()
                    if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(2)
        row = tree.findItems('Group', QtCore.Qt.MatchFlag.MatchExactly
                             | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        position = tree.visualItemRect(row).center()
        result, failures = [], []

        def close():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QMenu) and widget.isVisible():
                    widget.close()

        def inspect():
            try:
                reset = next(w for w in QtWidgets.QApplication.allWidgets()
                             if isinstance(w, QtWidgets.QMenu) and w.objectName() == 'resetOpinionsMenu')
                labels = ['separator' if a.isSeparator() else a.text() for a in reset.actions()]
                assert labels == (['All'] if locked else [
                    'All', 'separator', 'Transforms', 'Visibility', 'Materials',
                    'Geometry', 'Primvars', 'Other']), labels
                assert reset.menuAction().isEnabled() != locked
                result.extend(labels)
                if locked:
                    close()
                    return
                parent = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                              if isinstance(w, QtWidgets.QMenu) and w.isVisible()
                              and any(a.menu() == reset for a in w.actions()))
                parent.setActiveAction(reset.menuAction())
                reset.popup(parent.mapToGlobal(parent.actionGeometry(reset.menuAction()).topRight()))
                self.snapshot('reset_opinions_menu')
                assert reset.grab().save(str(self.directory / 'reset_menu.png'))
                if group is None:
                    close()
                else:
                    action = next(a for a in reset.actions() if a.text() == group)
                    reset.setActiveAction(action)
                    QtCore.QTimer.singleShot(50, lambda: QtTest.QTest.keyClick(reset, QtCore.Qt.Key.Key_Return))
            except Exception as error:
                failures.append(repr(error))
                close()

        watchdog = QtCore.QTimer(tree)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close)
        watchdog.start(5000)
        QtCore.QTimer.singleShot(50, inspect)
        event = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse, position,
                                       tree.viewport().mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(tree.viewport(), event)
        watchdog.stop()
        watchdog.deleteLater()
        assert not failures, failures
        assert result
        assert tb.wait_for_commands()

    def exercise(self):
        session = stageviz.session()
        stage = session.stage()
        root = stage.GetRootLayer()
        base = Sdf.Layer.FindOrOpen(str(self.base))
        original, source = root.ExportToString(), base.ExportToString()
        spec = '/Group/Sphere'
        groups = {'transforms': ['xformOp:translate', 'xformOpOrder'],
                  'visibility': ['visibility'], 'materials': ['material:binding'],
                  'geometry': ['radius'], 'primvars': ['primvars:test'], 'other': ['customNote']}
        self.menu()
        for group, names in groups.items():
            stageviz.command.reset_overrides(['/Group', '/Group/Sphere', '/Owned'], group)
            assert tb.wait_for_commands()
            for name in names:
                assert not root.GetPropertyAtPath(spec + '.' + name)
            assert root.GetPropertyAtPath('/Owned.radius')
            assert stage.GetPrimAtPath(spec).GetDisplayName() == 'Retained metadata'
            assert len(root.GetPrimAtPath(spec).properties) == 7 - len(names)
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert root.ExportToString() == original
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert not root.GetPropertyAtPath(spec + '.' + names[0])
            stageviz.command.undo()
            assert tb.wait_for_commands()
        self.menu('Transforms')
        assert not root.GetPropertyAtPath(spec + '.xformOpOrder')
        assert root.GetPropertyAtPath(spec + '.visibility')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert root.ExportToString() == original
        stageviz.command.reset_overrides(['/Group'])
        assert tb.wait_for_commands()
        assert not list(root.GetPrimAtPath(spec).properties)
        assert stage.GetPrimAtPath(spec).IsDefined()
        self.snapshot('reset_opinions_all')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert root.ExportToString() == original
        assert base.ExportToString() == source
        root.SetPermissionToEdit(False)
        self.menu(locked=True)
        root.SetPermissionToEdit(True)
        try:
            stageviz.command.reset_overrides(['/Group'], 'invalid')
            raise AssertionError('invalid reset group accepted')
        except ValueError:
            pass
        assert list(session.paths()) == ['/Group']
        assert self.fixture.read_text().startswith('#usda 1.0')
        self.snapshot('reset_opinions_restored')
