# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Instance context-menu conversion and editable child overrides in a disposable viewer."""
from pathlib import Path
import runpy

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class MakeEditableTest(_base['ViewportTest']):
    root = '/World/First'
    child = root + '/Cube'

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.fixture = self.directory / 'instances.usda'
        layer = Sdf.Layer.CreateNew(str(self.fixture))
        source = Sdf.PrimSpec(layer, 'Flattened_Prototype_1', Sdf.SpecifierOver)
        Sdf.PrimSpec(source, 'Cube', Sdf.SpecifierDef, 'Cube')
        Sdf.PrimSpec(source, 'Sphere', Sdf.SpecifierDef, 'Sphere')
        stage = Usd.Stage.Open(layer)
        UsdGeom.Cube(stage.GetPrimAtPath('/Flattened_Prototype_1/Cube')).AddTranslateOp().Set(Gf.Vec3d(0, 3, 0))
        for name, offset in [('First', 0), ('Second', 8)]:
            instance = UsdGeom.Xform.Define(stage, '/World/' + name)
            assert instance.GetPrim().GetReferences().AddInternalReference('/Flattened_Prototype_1')
            instance.GetPrim().SetInstanceable(True)
            instance.AddTranslateOp().Set(Gf.Vec3d(offset, 0, 0))
        external = Usd.Stage.CreateNew(str(self.directory / 'external.usda'))
        UsdGeom.Cube.Define(external, '/Flattened_Prototype_1/Cube')
        asset = UsdGeom.Xform.Define(external, '/Asset').GetPrim()
        assert asset.GetReferences().AddInternalReference('/Flattened_Prototype_1')
        external.GetRootLayer().Save()
        external_instance = UsdGeom.Xform.Define(stage, '/World/External')
        assert external_instance.GetPrim().GetReferences().AddReference(str(self.directory / 'external.usda'), '/Asset')
        external_instance.GetPrim().SetInstanceable(True)
        external_instance.AddTranslateOp().Set(Gf.Vec3d(20, 0, 0))
        assert layer.Save()
        del external, asset, external_instance
        del stage, instance, source, layer
        assert stageviz.session().load(str(self.fixture))
        assert tb.wait_for_commands()
        self.snapshot('make_editable_loaded')

    def menu(self, path, locked=False, action_name='Make editable', from_viewport=False):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(3)
        iterator = QtWidgets.QTreeWidgetItemIterator(tree)
        while iterator.value() and iterator.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) != path:
            iterator += 1
        assert iterator.value() is not None
        tree.scrollToItem(iterator.value())
        target = self.viewport() if from_viewport else tree.viewport()
        position = (QtCore.QPoint(target.width() // 2, target.height() // 2) if from_viewport else
                    tree.visualItemRect(iterator.value()).center())
        observed, failures = [], []

        def close():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QMenu) and widget.isVisible():
                    widget.close()

        def choose():
            try:
                menu = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                            if isinstance(w, QtWidgets.QMenu) and w.isVisible()
                            and any(a.objectName() == 'instanceMakeEditable' for a in w.actions()))
                actions = menu.actions()
                labels = ['separator' if a.isSeparator() else a.text() for a in actions]
                selected = list(stageviz.session().paths())
                proxies = any(stageviz.session().stage().GetPrimAtPath(p).IsInstanceProxy() for p in selected)
                expected = (['Select', 'separator'] if proxies else []) + ['Make editable', 'Source']
                assert labels == expected, labels
                action = next(a for a in actions if a.text() == action_name)
                assert action.isEnabled() != locked
                if action_name == 'Source' and locked:
                    assert next(a for a in actions if a.objectName() == 'instanceMakeEditable').isEnabled()
                observed.append(labels)
                assert menu.grab().save(str(self.directory / ('menu_' + path.rsplit('/', 1)[-1] + '.png')))
                self.snapshot('make_editable_menu_' + path.rsplit('/', 1)[-1])
                if locked:
                    close()
                else:
                    menu.setActiveAction(action)
                    QtTest.QTest.keyClick(menu, QtCore.Qt.Key.Key_Return)
            except Exception as error:
                failures.append(repr(error))
                close()

        def open_instance():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if not isinstance(widget, QtWidgets.QMenu) or not widget.isVisible():
                    continue
                for action in widget.actions():
                    if action.text() == 'Instance' and action.menu():
                        widget.setActiveAction(action)
                        action.menu().popup(widget.mapToGlobal(widget.actionGeometry(action).topRight()))
                        QtCore.QTimer.singleShot(50, choose)
                        return

        watchdog = QtCore.QTimer(tree)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close)
        watchdog.start(5000)
        QtCore.QTimer.singleShot(50, open_instance)
        event = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse, position,
                                       target.mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(target, event)
        watchdog.stop()
        watchdog.deleteLater()
        assert observed and not failures, failures
        assert tb.wait_for_commands()

    def exercise(self, source, path):
        assert stageviz.session().reload()
        assert tb.wait_for_commands()
        stageviz.command.select_paths([path])
        assert tb.wait_for_commands()
        stageviz.command.clear()
        stage = stageviz.session().stage()
        layer = stage.GetRootLayer()
        original = layer.ExportToString()
        cache = UsdGeom.XformCache()
        observed = [self.root, self.child, self.root + '/Sphere', '/World/Second', '/World/Second/Cube']
        before = {p: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(p)) for p in observed}
        if source == 'api':
            stageviz.command.make_editable_paths([path])
            assert tb.wait_for_commands()
        else:
            self.menu(path)
        assert not stage.GetPrimAtPath(self.root).IsInstance()
        assert not stage.GetPrimAtPath(self.child).IsInstanceProxy()
        assert stage.GetPrimAtPath('/World/Second').IsInstance()
        assert stage.GetPrimAtPath(self.root).HasAuthoredReferences()
        assert list(stageviz.session().paths()) == [path]
        converted = layer.ExportToString()
        delta = Gf.Matrix4d(1).SetTranslate(Gf.Vec3d(1, 2, 3))
        rows = lambda m: [list(row) for row in m]
        stageviz.command.set_transforms([self.child], [rows(before[self.child])], [rows(before[self.child] * delta)])
        assert tb.wait_for_commands()
        cache = UsdGeom.XformCache()
        for p, matrix in before.items():
            expected = matrix * delta if p == self.child else matrix
            assert Gf.IsClose(cache.GetLocalToWorldTransform(stage.GetPrimAtPath(p)), expected, 1e-6), p
        self.snapshot('make_editable_child_moved_' + source + '_' + path.rsplit('/', 1)[-1])
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert layer.ExportToString() == converted
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert layer.ExportToString() == original and stage.GetPrimAtPath(self.child).IsInstanceProxy()
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert layer.ExportToString() == converted and not stage.GetPrimAtPath(self.child).IsInstanceProxy()
        assert list(stageviz.session().paths()) == [path]
        self.snapshot('make_editable_redone_' + source + '_' + path.rsplit('/', 1)[-1])

    def multiple(self):
        session = stageviz.session()
        roots = [self.root, '/World/Second']
        for label, selected in [('roots', roots),
                                ('proxies', [self.child, self.root + '/Sphere', '/World/Second/Cube'])]:
            assert session.reload()
            assert tb.wait_for_commands()
            stageviz.command.select_paths(selected)
            assert tb.wait_for_commands()
            stageviz.command.clear()
            layer = session.stage().GetRootLayer()
            original = layer.ExportToString()
            self.menu(selected[0], from_viewport=label == 'proxies')
            assert all(not session.stage().GetPrimAtPath(p).IsInstance() for p in roots)
            assert list(session.paths()) == selected
            converted = layer.ExportToString()
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert layer.ExportToString() == original
            assert all(session.stage().GetPrimAtPath(p).IsInstance() for p in roots)
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert layer.ExportToString() == converted
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert layer.ExportToString() == original
            self.snapshot('multiple_instances_' + label)

            if label == 'proxies':
                self.menu(selected[0], action_name='Select', from_viewport=True)
                assert list(session.paths()) == roots, 'Select deduplicates instance roots'
                stageviz.command.select_paths(selected)
                assert tb.wait_for_commands()
            self.menu(selected[0], action_name='Source', from_viewport=label == 'proxies')
            expected = (['/Flattened_Prototype_1'] if label == 'roots' else
                        ['/Flattened_Prototype_1/Cube', '/Flattened_Prototype_1/Sphere'])
            assert list(session.paths()) == expected, ('Source deduplicates source paths', session.paths())

    def source_availability(self):
        session = stageviz.session()
        assert session.reload()
        assert tb.wait_for_commands()
        original = session.stage().GetRootLayer().ExportToString()
        for paths in ([self.root], [self.child]):
            stageviz.command.select_paths(paths)
            assert tb.wait_for_commands()
            self.menu(paths[0], action_name='Source')
            assert list(session.paths())[0].startswith('/Flattened_Prototype_1')
        for paths in (['/World/External'], ['/World/External/Cube'],
                      [self.root, '/World/External']):
            stageviz.command.select_paths(paths)
            assert tb.wait_for_commands()
            self.menu(paths[0], action_name='Source', locked=True)
            assert list(session.paths()) == paths
        assert session.stage().GetRootLayer().ExportToString() == original

    def locked(self):
        assert stageviz.session().reload()
        assert tb.wait_for_commands()
        stageviz.command.select_paths([self.child])
        assert tb.wait_for_commands()
        layer = stageviz.session().stage().GetRootLayer()
        original = layer.ExportToString()
        layer.SetPermissionToEdit(False)
        try:
            self.menu(self.child, locked=True)
        finally:
            layer.SetPermissionToEdit(True)
        assert layer.ExportToString() == original

    def gizmo(self):
        QtCore, _, QtTest, _ = tb.qt()
        self.activate_mode('Move')
        stage = stageviz.session().stage()
        cache = UsdGeom.XformCache()
        paths = [self.root, self.child, self.root + '/Sphere', '/World/Second/Cube']
        before = {p: cache.GetLocalToWorldTransform(stage.GetPrimAtPath(p)) for p in paths}
        assert Gf.IsClose(self.frame_handle(), before[self.child].ExtractTranslation(), 1e-6)
        layer = stage.GetRootLayer()
        original = layer.ExportToString()
        viewport = self.viewport()
        start = QtCore.QPoint(viewport.width() // 2, viewport.height() // 2)
        end = start + QtCore.QPoint(28, -8)
        QtTest.QTest.mousePress(viewport, QtCore.Qt.MouseButton.LeftButton, pos=start)
        QtTest.QTest.mouseMove(viewport, end, 50)
        QtTest.QTest.mouseRelease(viewport, QtCore.Qt.MouseButton.LeftButton, pos=end)
        assert tb.wait_for_commands()
        cache = UsdGeom.XformCache()
        assert not Gf.IsClose(cache.GetLocalToWorldTransform(stage.GetPrimAtPath(self.child)), before[self.child], 1e-6)
        for path in paths:
            if path != self.child:
                assert Gf.IsClose(cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)), before[path], 1e-6), path
        authored = layer.ExportToString()
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert layer.ExportToString() == original
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert layer.ExportToString() == authored
        self.snapshot('make_editable_gizmo')
