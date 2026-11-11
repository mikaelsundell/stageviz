# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Empty-group checkbox regression, driven externally through disposable MCP."""
import io
import json
from contextlib import redirect_stdout
from pathlib import Path
import sys
import tempfile

import stageviz
from pxr import Usd, UsdGeom
from PySide6 import QtCore, QtWidgets

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


class PayloadIndicatorTest:
    def __init__(self, disposable=False):
        assert disposable, 'this test replaces the stage'
        self.directory = Path(tempfile.mkdtemp(prefix='stageviz-payload-indicator-test-'))
        self.policy = stageviz.session().loadPolicy()

    def command(self, name, *args):
        getattr(stageviz.command, name)(*args)
        assert tb.wait_for_commands()

    def row(self, path):
        tree = next(w for w in QtWidgets.QApplication.allWidgets()
                    if w.objectName() == 'outlinerTree' and w.isVisible())
        it = QtWidgets.QTreeWidgetItemIterator(tree)
        while it.value():
            item = it.value()
            if item.data(0, QtCore.Qt.ItemDataRole.UserRole + 1) == path:
                state = item.data(0, QtCore.Qt.ItemDataRole.CheckStateRole)
                return state.value if hasattr(state, 'value') else state
            it += 1
        return None

    def snapshot(self, label):
        tree = next(w for w in QtWidgets.QApplication.allWidgets()
                    if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandToDepth(2)
        with redirect_stdout(io.StringIO()):
            state = tb.snapshot_qt_state(label)
        state['selectedPaths'] = list(stageviz.session().paths())
        (self.directory / (label + '.json')).write_text(json.dumps(state, indent=2))
        assert tb.viewer().grab().save(str(self.directory / (label + '.png')))
        print('PAYLOAD_INDICATOR_CHECKPOINT', label, self.row('/'), self.row('/World/Branch'))

    def setup(self):
        asset = Usd.Stage.CreateNew(str(self.directory / 'asset.usda'))
        UsdGeom.Xform.Define(asset, '/Asset')
        UsdGeom.Cube.Define(asset, '/Asset/Cube')
        asset.GetRootLayer().Save()
        child = Usd.Stage.CreateNew(str(self.directory / 'branch.usda'))
        UsdGeom.Xform.Define(child, '/World/Branch')
        gate = UsdGeom.Xform.Define(child, '/World/Branch/Gate').GetPrim()
        assert gate.GetPayloads().AddPayload('asset.usda', '/Asset')
        child.GetRootLayer().Save()
        del asset, child, gate
        assert stageviz.session().newStage(policy=stageviz.LoadNone)
        self.command('new_prim', '/World', 'Branch', 'Xform')
        self.command('add_sublayer', str(self.directory / 'branch.usda'))
        assert stageviz.session().stage().GetPrimAtPath('/World/Branch/Gate')
        self.command('new_payload', '/World', 'Sibling', str(self.directory / 'asset.usda'), '/Asset')
        self.command('load_payloads', ['/World/Branch/Gate', '/World/Sibling'])
        assert self.row('/') == 2
        self.snapshot('loaded')

    def mute(self):
        self.command('set_layer_muted', str(self.directory / 'branch.usda'), True)
        assert not stageviz.session().stage().GetPrimAtPath('/World/Branch/Gate')
        assert self.row('/World/Branch') == 0
        for _ in range(50):
            assert self.row('/World') == self.row('/') == 1
        self.snapshot('muted')
        self.command('undo')
        assert self.row('/') == 2
        self.snapshot('unmuted_undo')
        self.command('redo')
        assert self.row('/') == 1
        self.snapshot('muted_redo')
        self.command('undo')

    def structure(self):
        self.command('new_prim', '/World', 'Target', 'Xform')
        assert self.row('/') == 1
        self.command('move_path', ['/World/Branch'], '/World/Target')
        assert stageviz.session().stage().GetPrimAtPath('/World/Target/Branch/Gate')
        assert self.row('/') == self.row('/World/Target') == 2
        self.snapshot('moved')
        self.command('undo')
        assert self.row('/') == 1
        self.command('undo')
        assert self.row('/') == 2
        self.snapshot('structure_undone')

    def cleanup(self):
        assert tb.wait_for_commands()
        assert stageviz.session().newStage(policy=self.policy)
        stageviz.command.clear()
        self.snapshot('restored')
