# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Grouped prim menus, native relationship provenance and undoable clearing."""
from pathlib import Path
import json
import runpy
import traceback

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class ContextMenuTest(_base['ViewportTest']):
    child = '/World/Group/Cube'

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        base = Usd.Stage.CreateNew(str(self.directory / 'base.usda'))
        red = UsdShade.Material.Define(base, '/Looks/Red')
        blue = UsdShade.Material.Define(base, '/Looks/Blue')
        group = UsdGeom.Xform.Define(base, '/World/Group').GetPrim()
        cube = UsdGeom.Cube.Define(base, self.child).GetPrim()
        UsdGeom.Cube.Define(base, '/World/Group/Sibling')
        UsdShade.MaterialBindingAPI.Apply(group).Bind(red, UsdShade.Tokens.strongerThanDescendants)
        UsdShade.MaterialBindingAPI.Apply(cube).Bind(blue)
        cube.CreateRelationship('link').SetTargets(['/WeakTarget'])
        UsdGeom.Xform.Define(base, '/WeakTarget')
        UsdGeom.Xform.Define(base, '/LocalTarget')
        inherited = UsdGeom.Xform.Define(base, '/World/Inherited').GetPrim()
        UsdShade.MaterialBindingAPI.Apply(inherited).Bind(red)
        UsdGeom.Cube.Define(base, '/World/Inherited/Cube')
        source = UsdGeom.Xform.Define(base, '/Library/Source').GetPrim()
        UsdGeom.Cube.Define(base, '/Library/Source/Cube')
        UsdShade.MaterialBindingAPI.Apply(source).Bind(red)
        instance = UsdGeom.Xform.Define(base, '/World/Instance').GetPrim()
        instance.GetReferences().AddInternalReference('/Library/Source')
        instance.SetInstanceable(True)
        base.GetRootLayer().Save()
        base = None
        self.fixture = self.directory / 'scene.usda'
        layer = Sdf.Layer.CreateNew(str(self.fixture))
        layer.subLayerPaths = [str(self.directory / 'base.usda')]
        fixture = Usd.Stage.Open(layer)
        child = fixture.OverridePrim(self.child)
        child.GetRelationship('link').SetTargets(['/LocalTarget'])
        child.GetRelationship('link').SetDocumentation('Preserve local relationship metadata')
        child.CreateAttribute('customNote', Sdf.ValueTypeNames.String, custom=True).Set('keep')
        layer.Save()
        del fixture, child, layer
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        self.command(stageviz.command.select_paths, [self.child])
        self.snapshot('context_loaded')

    def command(self, function, *args):
        function(*args)
        assert tb.wait_for_commands()

    def menu(self, selected, action_name=None, target='outliner'):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        self.command(stageviz.command.select_paths, selected)
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(5)
        iterator = QtWidgets.QTreeWidgetItemIterator(tree)
        while iterator.value() and iterator.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) != selected[0]:
            iterator += 1
        assert iterator.value() is not None
        tree.scrollToItem(iterator.value())
        widget = tree.viewport() if target == 'outliner' else self.viewport()
        point = tree.visualItemRect(iterator.value()).center() if target == 'outliner' else widget.rect().center()
        captured, errors, keep, triggered = [], [], [], []

        def close():
            for w in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                    w.close()

        def snapshot(menu):
            actions = menu.actions()
            keep.extend([menu, *actions])
            rows = []
            for action in actions:
                row = {'text': action.text(), 'name': action.objectName(),
                       'enabled': action.isEnabled(), 'separator': action.isSeparator()}
                child = action.menu()
                if child is not None:
                    row['name'] = child.objectName() or row['name']
                    keep.append(child)
                    row['children'] = snapshot(child)
                rows.append(row)
            return rows

        def inspect():
            try:
                root = QtWidgets.QApplication.activePopupWidget()
                assert isinstance(root, QtWidgets.QMenu) and root.objectName() == 'primContextMenu'
                captured.append(snapshot(root))
                suffix = 'editable' if stageviz.session().stage().GetEditTarget().GetLayer().permissionToEdit else 'locked'
                assert root.grab().save(str(self.directory / ('menu-main-' + suffix + '.png')))
                self.snapshot('context_' + str(len(list(self.directory.glob('context_*.json')))))
                if action_name is None:
                    close()
                    return
                def find_route(menu, trail):
                    for action in menu.actions():
                        route = trail + [(menu, action)]
                        if action.objectName() == action_name:
                            return route
                        child = action.menu()
                        if child:
                            keep.append(child)
                            result = find_route(child, route)
                            if result:
                                return result
                    return None
                route = find_route(root, [])
                assert route, 'Missing action ' + action_name
                def open_route(index):
                    try:
                        menu, action = route[index]
                        assert action.isEnabled(), action_name
                        menu.setActiveAction(action)
                        if index == len(route) - 1:
                            action.triggered.connect(lambda: triggered.append(action_name))
                            assert menu.grab().save(str(self.directory / (action_name + '.png')))
                            QtTest.QTest.keyClick(menu, QtCore.Qt.Key.Key_Return)
                            return
                        child = action.menu()
                        child.popup(menu.mapToGlobal(menu.actionGeometry(action).topRight()))
                        QtCore.QTimer.singleShot(50, lambda: open_route(index + 1))
                    except Exception:
                        errors.append(traceback.format_exc())
                        close()
                open_route(0)
            except Exception:
                errors.append(traceback.format_exc())
                close()
        watchdog = QtCore.QTimer(widget)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close)
        watchdog.start(5000)
        QtCore.QTimer.singleShot(50, inspect)
        event = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse, point, widget.mapToGlobal(point))
        QtWidgets.QApplication.sendEvent(widget, event)
        watchdog.stop()
        watchdog.deleteLater()
        assert captured and not errors, errors
        assert action_name is None or triggered == [action_name], ('context action was not triggered', action_name, triggered)
        assert tb.wait_for_commands()
        (self.directory / 'menu.json').write_text(json.dumps(captured[0], indent=2))
        return captured[0]

    @staticmethod
    def find(rows, name):
        for row in rows:
            if row['name'] == name:
                return row
            found = ContextMenuTest.find(row.get('children', []), name)
            if found:
                return found
        return None

    def structure(self):
        rows = self.menu([self.child])
        labels = [r['text'] for r in rows if not r['separator']]
        assert labels == ['Frame', 'Isolate', 'Convert to Mesh', 'Visibility', 'Transform', 'Relationships',
                          'Opinions', 'Composition', 'Copy', 'Group', 'Duplicate', 'Delete'], labels
        assert self.find(rows,'overrideInCurrentLayer') is None
        visibility = self.find(rows, 'primVisibilityMenu')['children']
        assert ['separator' if r['separator'] else r['text'] for r in visibility] == [
            'Show', 'Selected', 'Hierarchy', 'separator', 'Hide', 'Selected', 'Hierarchy']
        assert not visibility[0]['enabled'] and not visibility[4]['enabled']
        opinions = self.find(rows, 'clearOpinionsMenu')['children']
        assert [r['text'] for r in opinions if not r['separator']] == ['Clear', 'All', 'Other']
        assert not opinions[0]['enabled'] and opinions[-2]['separator']
        material = self.find(rows, 'materialBindingMenu')['children']
        assert any(r['text'] == 'Ancestor override: /World/Group' and not r['enabled'] for r in material)
        assert any(r['text'] == 'Layer: base.usda' for r in material)
        assert not self.find(rows, 'materialBindingClear')['enabled']
        assert self.find(rows, 'materialBindingSource')['enabled']
        inherited = self.menu(['/World/Inherited/Cube'])
        assert any(r['text'] == 'Inherited from /World/Inherited' for r in self.find(inherited, 'materialBindingMenu')['children'])
        proxy = self.menu(['/World/Instance/Cube'], target='viewport')
        assert any(r['text'] == 'Instance owner: /World/Instance' for r in self.find(proxy, 'materialBindingMenu')['children'])
        assert self.find(proxy, 'instanceSelectRoots')['enabled']
        multi = self.menu([self.child, '/World/Inherited/Cube'])
        assert any(r['text'] == 'Multiple owners (2)' for r in self.find(multi, 'materialBindingMenu')['children'])

    def actions(self):
        camera = stageviz.session().viewState().camera()
        camera.resetView()
        assert camera.isIdentity()
        self.menu([self.child], 'primFrame')
        assert not camera.isIdentity()
        self.menu([self.child], 'materialBindingSource')
        assert list(stageviz.session().paths()) == ['/Looks/Red']
        self.menu([self.child], 'materialBindingOwner')
        assert list(stageviz.session().paths()) == ['/World/Group'], list(stageviz.session().paths())
        self.menu([self.child], 'visibilityHideSelected')
        stage = stageviz.session().stage()
        assert UsdGeom.Imageable(stage.GetPrimAtPath(self.child)).ComputeVisibility() == 'invisible'
        self.command(stageviz.command.undo)
        assert UsdGeom.Imageable(stage.GetPrimAtPath(self.child)).ComputeVisibility() == 'inherited'
        layer = stage.GetRootLayer()
        original = layer.ExportToString()
        self.menu([self.child], 'relationshipClear_link')
        assert stage.GetPrimAtPath(self.child).GetRelationship('link').GetTargets() == [Sdf.Path('/WeakTarget')]
        assert not layer.GetRelationshipAtPath(self.child + '.link')
        self.command(stageviz.command.undo)
        assert layer.ExportToString() == original
        self.command(stageviz.command.redo)
        assert stage.GetPrimAtPath(self.child).GetRelationship('link').GetTargets() == [Sdf.Path('/WeakTarget')]
        self.command(stageviz.command.undo)
        assert layer.ExportToString() == original
        layer.SetPermissionToEdit(False)
        try:
            rows = self.menu([self.child])
            assert not self.find(rows, 'clearOpinionsMenu')['enabled']
            assert not self.find(rows, 'materialBindingClear')['enabled']
            assert not self.find(rows, 'primTransformMenu')['enabled']
            assert not self.find(rows, 'primVisibilityMenu')['enabled']
            assert not self.find(rows, 'relationshipClear_link')['enabled']
            assert self.find(rows, 'materialBindingSource')['enabled']
        finally:
            layer.SetPermissionToEdit(True)
        print('CONTEXT_CHECKPOINT', json.dumps({'selection':list(stageviz.session().paths()),
              'targets':[str(p) for p in stage.GetPrimAtPath(self.child).GetRelationship('link').GetTargets()]}))
