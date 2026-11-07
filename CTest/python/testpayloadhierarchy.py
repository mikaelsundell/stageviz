# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Layered, four-level payload and Outliner checks in a disposable viewer.

Drive PayloadHierarchyTest phases through an external MCP controller. All USD
fixture stages are released before session commands edit/load their layers.
"""
import hashlib
import json
import random
from pathlib import Path
import sys
import tempfile

import stageviz
from pxr import Gf, Sdf, Usd, UsdGeom

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


def make_fixture(directory):
    """Return only plain fixture metadata; retain no USD stages or Qt wrappers."""
    directory = Path(directory)

    def save(name, root, author):
        stage = Usd.Stage.CreateNew(str(directory / name))
        prim = UsdGeom.Xform.Define(stage, root).GetPrim()
        stage.SetDefaultPrim(prim)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        author(stage)
        stage.GetRootLayer().Save()

    def geometry(stage, root, count):
        for i in range(count):
            cube = UsdGeom.Cube.Define(stage, root + '/Cube_%03d' % i)
            cube.GetSizeAttr().Set(0.12)
            cube.AddTranslateOp().Set(Gf.Vec3d((i % 16) * .18, (i // 16) * .18, 0))

    def payload(stage, path, file, source):
        UsdGeom.Xform.Define(stage, path).GetPrim().GetPayloads().AddPayload(file, source)

    save('bolts.usda', '/Bolts', lambda s: geometry(s, '/Bolts', 160))
    save('panels.usda', '/Panels', lambda s: geometry(s, '/Panels', 32))
    save('badges.usda', '/Badges', lambda s: geometry(s, '/Badges', 12))

    def detail(stage):
        payload(stage, '/Detail/Bolts', 'bolts.usda', '/Bolts')
        payload(stage, '/Detail/Panels', 'panels.usda', '/Panels')
        UsdGeom.Xform.Define(stage, '/Detail/Labels').GetPrim().GetReferences().AddReference('badges.usda', '/Badges')
    save('detail.usda', '/Detail', detail)

    for number in range(3):
        def module(stage, number=number):
            for name in ('PartA', 'PartB'):
                payload(stage, '/Module/' + name, 'detail.usda', '/Detail')
        save('module_%d.usda' % number, '/Module', module)

    def assembly(stage):
        for number, name in enumerate(('ModuleA', 'ModuleB', 'ModuleC')):
            payload(stage, '/Asset/' + name, 'module_%d.usda' % number, '/Module')
            UsdGeom.Xformable(stage.GetPrimAtPath('/Asset/' + name)).AddTranslateOp().Set(Gf.Vec3d(number * 4, 0, 0))
    save('assembly.usda', '/Asset', assembly)

    def catalog(stage):
        payload(stage, '/Catalog/Equipment', 'assembly.usda', '/Asset')
        for i, name in enumerate(('InstanceA', 'InstanceB')):
            prim = UsdGeom.Xform.Define(stage, '/Catalog/' + name).GetPrim()
            prim.GetReferences().AddReference('module_0.usda', '/Module')
            prim.SetInstanceable(True)
            UsdGeom.Xformable(prim).AddTranslateOp().Set(Gf.Vec3d(i * 4, 0, 5))
    save('catalog.usda', '/Catalog', catalog)

    for number, region in enumerate(('RegionA', 'RegionB', 'RegionC')):
        def layout(stage, number=number, region=region):
            if number == 0:
                UsdGeom.Xform.Define(stage, '/World/RegionA/MoveTarget')
            for i in range(2):
                path = '/World/' + region + '/Vehicle%02d' % (number * 2 + i + 1)
                payload(stage, path, 'assembly.usda', '/Asset')
                UsdGeom.ModelAPI(stage.GetPrimAtPath(path)).SetExtentsHint([Gf.Vec3f(-.1), Gf.Vec3f(10.8, 1.7, .1)])
                UsdGeom.Xformable(stage.GetPrimAtPath(path)).AddTranslateOp().Set(Gf.Vec3d(i * 8, 0, number * 40))
        save('layout_%d.usda' % number, '/World', layout)

    save('references.usda', '/World', lambda s: UsdGeom.Xform.Define(s, '/World/Depot').GetPrim().GetReferences().AddReference('catalog.usda', '/Catalog'))
    save('always_visible.usda', '/World', lambda s: UsdGeom.Cube.Define(s, '/World/AlwaysVisible'))
    scene = Usd.Stage.CreateNew(str(directory / 'scene.usda'))
    scene.GetRootLayer().subLayerPaths = ['references.usda', 'always_visible.usda'] + ['layout_%d.usda' % i for i in range(3)]
    scene.SetDefaultPrim(scene.GetPrimAtPath('/World'))
    UsdGeom.SetStageUpAxis(scene, UsdGeom.Tokens.y)
    scene.GetRootLayer().Save()
    count = sum(1 for _ in Usd.PrimRange(scene.GetPseudoRoot(), Usd.TraverseInstanceProxies(Usd.PrimAllPrimsPredicate))) - 1
    return dict(scene=str(directory / 'scene.usda'), fully_composed_prims=count,
                layers={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.glob('*.usda')})


class PayloadHierarchyTest:
    first = '/World/RegionA/Vehicle01'
    second = '/World/RegionA/Vehicle02'
    nested = first + '/ModuleA/PartA/Bolts'
    sibling = first + '/ModuleA/PartB/Bolts'
    proxy = '/World/Depot/InstanceA/PartA'
    other_proxy = '/World/Depot/InstanceB/PartA'

    def __init__(self, disposable=False):
        assert disposable, 'payload hierarchy tests replace the current stage'
        self.original_policy = stageviz.session().loadPolicy()
        self.directory = Path(tempfile.mkdtemp(prefix='stageviz-payload-hierarchy-'))
        self.fixture = make_fixture(self.directory)
        self.checkpoints = []

    def tree(self):
        return tb.find_widget('outlinerTree', tb.qt()[3].QTreeWidget, tb.viewer())

    def index(self, path):
        QtCore = tb.qt()[0]
        model = self.tree().model()
        parent = QtCore.QModelIndex()
        for part in ['/'] + [str(p) for p in Sdf.Path(path).GetPrefixes()]:
            found = next((model.index(i, 0, parent) for i in range(model.rowCount(parent))
                          if model.index(i, 0, parent).data(int(QtCore.Qt.ItemDataRole.UserRole) + 1) == part), None)
            if found is None:
                return None
            parent = found
        return parent

    def row(self, path):
        index = self.index(path)
        if index is None:
            return None
        QtCore = tb.qt()[0]
        value = index.data(QtCore.Qt.ItemDataRole.CheckStateRole)
        if hasattr(value, 'value'):
            value = value.value
        return dict(state=value, children=self.tree().model().rowCount(index),
                    checkable=bool(index.flags() & QtCore.Qt.ItemFlag.ItemIsUserCheckable))

    def loaded(self, path):
        prim = stageviz.session().stage().GetPrimAtPath(path)
        return bool(prim) and prim.IsLoaded()

    def rules(self):
        rules = stageviz.session().stage().GetLoadRules()
        rules.Minimize()  # Ignore redundant encodings of the same inherited rule.
        return [(str(path), str(rule)) for path, rule in rules.GetRules()]

    def command(self, operation, paths):
        getattr(stageviz.command, operation)(paths)
        assert tb.wait_for_commands(timeout=30), operation + ' completion'

    def undo(self):
        assert stageviz.command.can_undo()
        stageviz.command.undo()
        assert tb.wait_for_commands(timeout=30)

    def redo(self):
        assert stageviz.command.can_redo()
        stageviz.command.redo()
        assert tb.wait_for_commands(timeout=30)

    def baseline(self):
        assert tb.wait_for_commands(timeout=30)
        assert stageviz.session().loadFromFile(self.fixture['scene'], policy=stageviz.LoadNone)
        tb.process_events(100)
        stageviz.command.clear()
        assert stageviz.session().loadPolicy() == stageviz.LoadNone
        assert self.row(self.first)['state'] == 0 and not self.loaded(self.first)

    def checkpoint(self, label):
        assert tb.wait_for_commands(timeout=30)
        self.tree().expandAll()
        tb.process_events()
        QtCore = tb.qt()[0]
        model = self.tree().model()
        stage = stageviz.session().stage()
        gates = {}
        groups = {}
        def visit(parent):
            states = []
            for i in range(model.rowCount(parent)):
                index = model.index(i, 0, parent)
                path = index.data(int(QtCore.Qt.ItemDataRole.UserRole) + 1)
                prim = stage.GetPrimAtPath(path)
                assert prim, ('stale Outliner path', path)
                info = self.row(path)
                if prim.HasPayload():
                    expected = 2 if prim.IsLoaded() else 0
                    assert info['state'] == expected, (path, info, prim.IsLoaded())
                    assert info['children'] == 0, ('Payloads mode stops at payload gates', path)
                    gates[path] = dict(info, loaded=prim.IsLoaded())
                    states.append(expected)
                else:
                    children = visit(index)
                    expected = (children[0] if all(s == children[0] for s in children) else 1) if children else 0
                    assert info['state'] == expected, ('group/ordinary row', path, info, expected)
                    groups[path] = dict(info, expected=expected)
                    if children:
                        states.append(expected)
            return states
        visit(QtCore.QModelIndex())
        rows = {p: self.row(p) for p in ('/', '/World', '/World/RegionA', self.first, self.second, self.proxy, self.other_proxy)}
        state = dict(label=label, rows=rows, payload_gates=gates, groups=groups, rules=self.rules(), selection=list(stageviz.session().paths()),
                     loaded={p: self.loaded(p) for p in (self.first, self.second, self.nested, self.sibling, self.proxy, self.other_proxy, self.proxy + "/Bolts")})
        self.checkpoints.append(state)
        (self.directory / 'states.json').write_text(json.dumps(self.checkpoints, indent=2))
        (self.directory / (label + '-qt.json')).write_text(json.dumps(tb.snapshot_qt_state(label), default=str, indent=2))
        assert tb.viewer().grab().save(str(self.directory / (label + '.png')))
        assert self.fixture['layers'] == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.directory.glob('*.usda')}
        print('PAYLOAD_CHECKPOINT', json.dumps(state))

    def click_checkbox(self, path):
        """Send one actual checkbox click without pumping queued command timers."""
        QtCore, _, QtTest, _ = tb.qt()
        tree = self.tree()
        index = self.index(path)
        assert index is not None and self.row(path)['checkable'], path
        tree.scrollTo(index)
        rect = tree.visualRect(index)
        size = stageviz.ui().iconSize(stageviz.ui().UIScale.Small)
        point = QtCore.QPoint(rect.left() + 18 + size // 2, rect.center().y() + 2)
        assert tree.viewport().rect().contains(point)
        QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.MouseButton.LeftButton, pos=point)

    def toggle(self, path, checked):
        tree = self.tree()
        tree.expandAll()
        tree.setCurrentIndex(self.index(path))
        tree.setFocus()
        tb.process_events()
        before_selection = list(stageviz.session().paths())
        stageviz.command.clear()
        self.click_checkbox(path)
        assert tb.wait_for_commands(timeout=30)
        expected = 2 if checked else 0
        assert tb.wait_until(lambda: self.row(path)['state'] == expected), (path, self.row(path))
        assert list(stageviz.session().paths()) == before_selection, 'checkbox click preserves selection'

    def assert_group(self, path, expected):
        assert self.row(path)['state'] == expected, (path, self.row(path), expected)

    def root_checkbox(self):
        self.baseline()
        self.toggle('/', True)
        self.checkpoint('root_all_loaded')
        assert all(g['loaded'] for g in self.checkpoints[-1]['payload_gates'].values())
        self.undo()
        self.checkpoint('root_load_undone')
        self.assert_group('/', 0)
        self.assert_group('/World', 0)
        self.redo()
        self.assert_group('/', 2)
        self.toggle('/', False)
        self.undo()
        self.checkpoint('root_unload_undone')
        self.assert_group('/', 2)
        self.redo()
        self.assert_group('/', 0)
        self.checkpoint('root_unload_redone')

    def menu_actions(self):
        self.baseline()
        self.command('select_paths', [self.first])
        _, QtGui, _, _ = tb.qt()
        def action(name):
            control = tb.viewer().findChild(QtGui.QAction, name)
            assert control is not None and control.isEnabled(), name
            control.trigger()
            assert tb.wait_for_commands(timeout=30), name
        action('editPayloadLoad')
        assert self.loaded(self.nested) and not self.loaded(self.second)
        self.checkpoint('menu_load')
        self.undo()
        assert not self.loaded(self.first)
        self.redo()
        action('editPayloadUnload')
        assert not self.loaded(self.first)
        self.undo()
        assert self.loaded(self.first)
        action('editPayloadLoadNeighbors')
        assert self.loaded(self.second)
        self.checkpoint('menu_neighbors')
        self.undo()
        assert not self.loaded(self.second) and self.loaded(self.first)
        self.checkpoint('menu_neighbors_undone')

    def filtered_collapsed(self):
        self.baseline()
        tb.find_widget('filter', tb.qt()[3].QLineEdit, self.tree().parentWidget()).setText('Vehicle01')
        tb.process_events()
        assert self.tree().isRowHidden(self.index(self.second).row(), self.index('/World/RegionA'))
        self.toggle('/World/RegionA', True)
        assert self.loaded(self.first) and self.loaded(self.second), 'hidden sibling follows group click'
        self.checkpoint('filtered_group_loaded')
        tb.find_widget('filter', tb.qt()[3].QLineEdit, self.tree().parentWidget()).setText('')
        self.tree().collapseAll()
        self.command('unload_payloads', [self.first])
        self.assert_group('/World/RegionA', 1)
        self.undo()
        self.assert_group('/World/RegionA', 2)
        self.checkpoint('collapsed_command_undone')

    def rapid_clicks(self):
        for count in (2, 3, 4):
            self.baseline()
            self.tree().expandAll()
            tb.process_events()
            for _ in range(count):
                self.click_checkbox(self.first)
            assert tb.wait_for_commands(timeout=30)
            expected = bool(count % 2)
            self.checkpoint('rapid_%d_clicks' % count)
            assert self.loaded(self.first) == expected, (count, self.loaded(self.first), expected)
            assert self.row(self.first)['state'] == (2 if expected else 0)

    def randomized_commands(self):
        self.baseline()
        rng = random.Random(61107)
        targets = [self.first, self.second, '/World/RegionB/Vehicle03', self.proxy, self.other_proxy]
        for step in range(16):
            selected = rng.sample(targets, rng.randint(1, 3))
            operation = rng.choice(('load_payloads', 'unload_payloads'))
            before = self.rules()
            self.command(operation, selected)
            after = self.rules()
            self.checkpoint('random_%02d_%s' % (step, operation))
            self.undo()
            assert self.rules() == before, (step, 'undo', before, self.rules())
            self.redo()
            assert self.rules() == after, (step, 'redo', after, self.rules())
            self.checkpoint('random_%02d_redone' % step)

    def mixed_clicks(self):
        self.baseline()
        self.toggle(self.first, True)
        self.tree().expandAll()
        tb.process_events()
        # Opposite requests on independent branches must survive the same batch.
        self.click_checkbox(self.first)
        self.click_checkbox(self.second)
        assert tb.wait_for_commands(timeout=30)
        assert not self.loaded(self.first) and self.loaded(self.second)
        self.checkpoint('mixed_queued_clicks')
        # A root click after a child click replaces all outstanding gate requests.
        self.baseline()
        self.tree().expandAll()
        tb.process_events()
        self.click_checkbox(self.first)
        self.click_checkbox('/')  # Partial root -> Unchecked in TreeWidget.
        assert tb.wait_for_commands(timeout=30)
        self.checkpoint('queued_child_then_root')
        assert not any(g['loaded'] for g in self.checkpoints[-1]['payload_gates'].values())

    def selection_clicks(self):
        self.baseline()
        QtCore, _, QtTest, _ = tb.qt()
        self.tree().expandAll()
        tb.process_events()
        for path in (self.first, self.second, self.proxy, '/World/RegionA'):
            tree = self.tree()
            index = self.index(path)
            tree.scrollTo(index)
            rect = tree.visualRect(index)
            size = stageviz.ui().iconSize(stageviz.ui().UIScale.Small)
            point = QtCore.QPoint(rect.left() + 18 + size * 2 + 25, rect.center().y() + 2)
            QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.MouseButton.LeftButton, pos=point)
            assert tb.wait_until(lambda: list(stageviz.session().paths()) == [path]), path
            assert not self.loaded(self.first) and not self.loaded(self.second)
        self.checkpoint('row_selection_clicks')

    def namespace_commands(self):
        self.baseline()
        self.command('load_payloads', [self.first, self.proxy])
        renamed = '/World/RegionA/VehicleRenamed'
        moved = '/World/RegionA/MoveTarget/VehicleRenamed'
        self.command('set_edit_layer', str(self.directory / 'layout_0.usda'))
        stageviz.command.rename_path(self.first, 'VehicleRenamed')
        assert tb.wait_for_commands(timeout=30)
        assert self.index(self.first) is None and self.loaded(renamed)
        assert self.row(renamed)['state'] == 2
        self.undo()
        self.checkpoint('rename_payload_undone')
        self.redo()
        stageviz.command.move_path([renamed], '/World/RegionA/MoveTarget')
        assert tb.wait_for_commands(timeout=30)
        assert self.index(renamed) is None and self.loaded(moved)
        assert self.row(moved)['state'] == 2
        self.assert_group('/World/RegionA', 1)
        self.assert_group('/World/RegionA/MoveTarget', 2)
        self.undo()
        assert self.row(renamed)['state'] == 2
        self.redo()
        assert self.row(moved)['state'] == 2
        self.undo()
        self.undo()
        self.checkpoint('rename_and_move_restored')
        assert self.loaded(self.first) and self.loaded(self.proxy)
        self.command('delete_paths', [self.first])
        assert self.index(self.first) is None and not self.loaded(self.first)
        self.assert_group('/World/RegionA', 0)
        self.undo()
        assert self.loaded(self.first) and self.row(self.first)['state'] == 2
        self.redo()
        assert self.index(self.first) is None
        self.undo()
        self.checkpoint('delete_payload_restored')

    def layer_invert_unload(self):
        self.baseline()
        outside = '/World/RegionB/Vehicle03'
        self.command('load_payloads', [self.first, self.second, self.proxy, outside])
        self.command('set_edit_layer', str(self.directory / 'layout_0.usda'))
        leaves = [self.nested + '/Cube_000', self.sibling + '/Cube_001', self.proxy + '/Bolts/Cube_000']
        self.command('select_paths', leaves)
        _, QtGui, _, _ = tb.qt()
        def action(name):
            control = tb.viewer().findChild(QtGui.QAction, name)
            assert control is not None and control.isEnabled(), name
            if name == 'editLayerSelect':
                assert control.text() == 'Select in Layer', control.text()
            control.trigger()
            assert tb.wait_for_commands(timeout=30), name
        action('editLayerInvert')
        direct = list(stageviz.session().paths())
        assert direct == [self.second], direct
        self.undo()
        assert list(stageviz.session().paths()) == leaves
        action('editLayerSelect')
        assert list(stageviz.session().paths()) == [self.first]
        action('editLayerInvert')
        assert list(stageviz.session().paths()) == direct
        action('editPayloadUnload')
        assert not self.loaded(self.second)
        assert all(self.loaded(p) for p in (self.first, self.proxy, outside))
        self.checkpoint('layer_inverse_unloaded')
        self.undo()
        assert self.loaded(self.second)
        self.redo()
        assert not self.loaded(self.second)
        assert self.loaded(self.first) and self.loaded(self.proxy) and self.loaded(outside)
        self.checkpoint('layer_inverse_redone')
        self.undo()
        self.checkpoint('layer_inverse_restored')

    def load_policy(self):
        self.baseline()
        assert stageviz.session().loadFromFile(self.fixture['scene'], policy=stageviz.LoadAll)
        tb.process_events()
        assert self.loaded(self.nested)
        assert self.row(self.first)['children'] > 0
        assert not self.row(self.first)['checkable'] and self.row(self.first)['state'] is None
        self.baseline()
        self.checkpoint('all_to_payloads_policy')

    def setup(self):
        self.baseline()
        assert len(self.fixture['layers']) >= 14 and self.fixture['fully_composed_prims'] > 8000, self.fixture
        assert self.row(self.proxy)['state'] == 0
        assert self.row(self.first)['children'] == 0
        assert self.index(self.nested) is None
        self.checkpoint('initial_payloads_mode')

    def leaf_checkbox(self):
        self.baseline()
        self.toggle(self.first, True)
        assert all(self.loaded(p) for p in (self.first, self.nested, self.sibling))
        assert not self.loaded(self.second) and self.row(self.second)['state'] == 0
        assert self.row('/World/RegionA')['state'] == 1
        assert self.row(self.first)['children'] == 0, 'Payloads mode keeps gates as leaves'
        self.checkpoint('leaf_loaded')
        self.undo()
        assert not self.loaded(self.first) and self.row(self.first)['state'] == 0
        self.redo()
        assert self.loaded(self.nested) and self.row(self.first)['state'] == 2
        self.toggle(self.first, False)
        assert not self.loaded(self.first)
        self.undo()
        assert self.loaded(self.nested) and self.row(self.first)['state'] == 2
        self.checkpoint('leaf_undo_redo')
        self.toggle('/World/RegionA', False)
        assert not self.loaded(self.first) and not self.loaded(self.second)
        self.undo()
        assert self.loaded(self.first) and not self.loaded(self.second)
        assert self.row('/World/RegionA')['state'] == 1
        self.checkpoint('partial_group_click_undo')

    def group_checkbox(self):
        self.baseline()
        self.toggle('/World/RegionA', True)
        assert self.loaded(self.first) and self.loaded(self.second)
        assert not self.loaded('/World/RegionB/Vehicle03')
        assert self.row('/World/RegionA')['state'] == 2 and self.row('/World')['state'] == 1
        self.checkpoint('group_loaded')
        self.undo()
        assert not self.loaded(self.first) and not self.loaded(self.second)
        self.redo()
        assert self.loaded(self.first) and self.loaded(self.second)
        self.toggle('/World/RegionA', False)
        assert not self.loaded(self.first) and not self.loaded(self.second)
        self.checkpoint('group_unloaded')

    def nested_rules(self):
        self.baseline()
        self.command('load_payloads', [self.first])
        self.command('unload_payloads', [self.nested])
        assert self.loaded(self.first) and not self.loaded(self.nested) and self.loaded(self.sibling)
        assert self.row(self.first)['state'] == 2 and self.index(self.nested) is None
        original = self.rules()
        stageviz.command.clear()
        self.checkpoint('nested_excluded')
        self.command('unload_payloads', [self.first])
        self.undo()
        assert self.rules() == original, ('parent unload undo must preserve nested exclusions', original, self.rules())
        assert self.loaded(self.first) and not self.loaded(self.nested) and self.loaded(self.sibling)
        self.redo()
        assert not self.loaded(self.first) and self.row(self.first)['state'] == 0
        self.undo()
        assert self.rules() == original
        self.checkpoint('nested_parent_undo_redo')

    def instance_payloads(self):
        self.baseline()
        assert stageviz.session().stage().GetPrimAtPath(self.proxy).IsInstanceProxy()
        self.toggle(self.proxy, True)
        assert self.loaded(self.proxy) and self.row(self.proxy)['state'] == 2
        assert not self.loaded(self.other_proxy) and self.row(self.other_proxy)['state'] == 0
        self.undo()
        assert not self.loaded(self.proxy) and self.row(self.proxy)['state'] == 0
        self.redo()
        assert self.loaded(self.proxy) and not self.loaded(self.other_proxy)
        self.command('unload_payloads', [self.proxy + '/Bolts'])
        original = self.rules()
        stageviz.command.clear()
        self.command('unload_payloads', [self.proxy])
        self.undo()
        assert self.rules() == original and not self.loaded(self.proxy + '/Bolts')
        assert self.loaded(self.proxy) and not self.loaded(self.other_proxy)
        assert self.row(self.proxy)['state'] == 2 and self.row(self.other_proxy)['state'] == 0
        self.checkpoint('instance_sibling_independence')

    def overlapping_batch(self):
        self.baseline()
        self.command('load_payloads', [self.first, self.first + '/ModuleA', self.nested, self.second, self.first])
        assert self.loaded(self.nested) and self.loaded(self.second)
        self.checkpoint('overlapping_batch_loaded')
        self.undo()
        assert not self.loaded(self.first) and not self.loaded(self.second)
        assert self.row(self.first)['state'] == 0
        self.redo()
        assert self.loaded(self.nested) and self.loaded(self.second)
        self.checkpoint('overlapping_batch_undo_redo')

    def repeated_toggle(self):
        self.baseline()
        for _ in range(4):
            self.toggle('/World/RegionA', True)
            assert self.loaded(self.nested) and self.loaded(self.second)
            assert self.row('/World/RegionA')['state'] == 2
            self.toggle('/World/RegionA', False)
            assert not self.loaded(self.first) and not self.loaded(self.second)
            self.undo()
            assert self.loaded(self.nested) and self.loaded(self.second)
            self.redo()
            assert not self.loaded(self.first) and self.row('/World/RegionA')['state'] == 0
        self.checkpoint('repeated_group_toggle')

    def reload(self):
        self.baseline()
        self.command('load_payloads', [self.first, self.proxy])
        assert stageviz.session().reload()
        tb.process_events(100)
        assert stageviz.session().loadPolicy() == stageviz.LoadNone
        assert not self.loaded(self.first) and not self.loaded(self.proxy)
        assert self.row(self.first)['state'] == self.row(self.proxy)['state'] == 0
        self.checkpoint('reload_payloads_mode')

    def cleanup(self):
        assert tb.wait_for_commands(timeout=30)
        self.checkpoint('final_fixture_unchanged')
        assert stageviz.session().newStage(policy=self.original_policy)
        print('PAYLOAD_FIXTURE', json.dumps(self.fixture))
        print('PAYLOAD_ARTIFACTS', str(self.directory))
