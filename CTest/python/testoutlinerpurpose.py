# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Outliner-only purpose choices, inherited rows and unchanged viewport content."""
from pathlib import Path
import json
import runpy
import traceback

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class OutlinerPurposeTest(_base['ViewportTest']):
    purposes = ['default', 'render', 'proxy', 'guide']

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.fixture = self.directory / 'purposes.usda'
        stage = Usd.Stage.CreateNew(str(self.fixture))
        UsdGeom.Xform.Define(stage, '/World')
        for i, purpose in enumerate(self.purposes):
            group = UsdGeom.Xform.Define(stage, '/World/' + purpose.title())
            group.CreatePurposeAttr().Set(purpose)
            group.AddTranslateOp().Set((i * 3, 0, 0))
            UsdGeom.Cube.Define(stage, str(group.GetPath()) + '/Cube')
        stage.DefinePrim('/World/Render/Bridge')
        UsdGeom.Cube.Define(stage, '/World/Render/Bridge/Child')
        UsdGeom.Cube.Define(stage, '/World/Render/Default').CreatePurposeAttr().Set('default')
        source = UsdGeom.Xform.Define(stage, '/Library/Source')
        UsdGeom.Cube.Define(stage, '/Library/Source/Cube')
        instance = UsdGeom.Xform.Define(stage, '/World/Instance')
        instance.GetPrim().GetReferences().AddInternalReference('/Library/Source')
        instance.GetPrim().SetInstanceable(True)
        instance.CreatePurposeAttr().Set('proxy')
        UsdShade.Material.Define(stage, '/Looks/Material')
        stage.GetRootLayer().Save()
        del stage, group, source, instance
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        self.command(stageviz.command.select_paths, ['/World/Render/Cube'])
        self.original = stageviz.session().stage().GetRootLayer().ExportToString()
        self.viewport_paths = set(stageviz.render().capture().paths())
        self.snapshot('purpose_loaded')

    def command(self, function, *args):
        function(*args)
        assert tb.wait_for_commands()

    def visible(self, path):
        QtCore, _, _, QtWidgets = tb.qt()
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        it = QtWidgets.QTreeWidgetItemIterator(tree)
        while it.value():
            if it.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) == path:
                row = it.value()
                while row:
                    if row.isHidden():
                        return False
                    row = row.parent()
                return True
            it += 1
        raise AssertionError('Missing row ' + path)

    def menu(self, chosen=None):
        _, _, _, QtWidgets = tb.qt()
        QtCore, _, _, _ = tb.qt()
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        view = tree.parentWidget()
        while view and view.findChild(QtWidgets.QToolButton, 'display') is None:
            view = view.parentWidget()
        button = view.findChild(QtWidgets.QToolButton, 'display')
        observed, errors = [], []
        def inspect():
            menu = QtWidgets.QApplication.activePopupWidget()
            try:
                assert isinstance(menu, QtWidgets.QMenu)
                actions = menu.actions()
                assert actions[-6].isSeparator() and actions[-5].text() == 'Purpose'
                assert not actions[-5].isEnabled()
                choices = actions[-4:]
                assert [a.objectName() for a in choices] == ['outlinerPurpose_' + p for p in self.purposes]
                if chosen is not None:
                    for action, purpose in zip(choices, self.purposes):
                        if action.isChecked() != (purpose in chosen):
                            action.trigger()
                observed.extend(p for action, p in zip(choices, self.purposes) if action.isChecked())
                assert menu.grab().save(str(self.directory / ('purpose-menu-' + ('-'.join(observed) or 'none') + '.png')))
            except Exception:
                errors.append(traceback.format_exc())
            finally:
                if menu:
                    menu.close()
        QtCore.QTimer.singleShot(50, inspect)
        button.click()
        assert button.isChecked() == (set(observed) != set(self.purposes)), "Outliner highlight follows deviations from default purposes with Untyped hidden"
        assert not errors, errors
        if chosen is not None:
            assert set(observed) == set(chosen)
        return observed

    def exercise(self):
        assert self.menu() == self.purposes
        stage = stageviz.session().stage()
        geometry = [p for p in stage.Traverse(Usd.TraverseInstanceProxies()) if p.IsA(UsdGeom.Gprim)]
        for purpose in self.purposes:
            self.menu([purpose])
            for prim in geometry:
                assert self.visible(str(prim.GetPath())) == (UsdGeom.Imageable(prim).ComputePurpose() == purpose), str(prim.GetPath())
            self.snapshot('purpose_' + purpose)
        self.menu([])
        assert self.menu() == []
        assert not any(self.visible(str(p.GetPath())) for p in geometry)
        assert self.visible('/Looks/Material')
        assert set(stageviz.render().capture().paths()) == self.viewport_paths
        assert stage.GetRootLayer().ExportToString() == self.original
        assert list(stageviz.session().paths()) == ['/World/Render/Cube']
        self.snapshot('purpose_none')
        assert stageviz.session().reload() and tb.wait_for_commands()
        assert self.menu() == []
        assert not self.visible('/World/Render/Cube')
        self.menu(['render'])
        assert self.visible('/World') and self.visible('/World/Render/Bridge') and self.visible('/World/Render/Bridge/Child')
        self.menu(self.purposes)
        self.snapshot('purpose_restored')
        print('PURPOSE_CHECKPOINT', json.dumps({'selection':list(stageviz.session().paths()), 'filename':stageviz.session().filename()}))
