# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Rich prim, override and material tooltips in a disposable app."""
from pathlib import Path
import json
import runpy

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class TooltipsTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        source = Usd.Stage.CreateNew(str(self.directory / 'base.usda'))
        UsdGeom.Xform.Define(source, '/World/Group')
        UsdGeom.Cube.Define(source, '/World/Group/Shape')
        source.GetRootLayer().Save()
        del source
        self.fixture = self.directory / 'tooltips.usda'
        layer = Sdf.Layer.CreateNew(str(self.fixture))
        layer.subLayerPaths = [str(self.directory / 'base.usda')]
        fixture = Usd.Stage.Open(layer)
        material = UsdShade.Material.Define(fixture, '/Materials/Paint')
        shader = UsdShade.Shader.Define(fixture, '/Materials/Paint/Surface')
        shader.CreateIdAttr('UsdPreviewSurface')
        material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
        group = fixture.GetPrimAtPath('/World/Group')
        UsdShade.MaterialBindingAPI.Apply(group).Bind(material, UsdShade.Tokens.strongerThanDescendants)
        UsdGeom.Imageable(fixture.GetPrimAtPath('/World/Group/Shape')).CreateVisibilityAttr().Set('invisible')
        instance = UsdGeom.Xform.Define(fixture, '/World/Instance').GetPrim()
        instance.GetReferences().AddInternalReference('/World/Group')
        instance.SetInstanceable(True)
        layer.Save()
        del fixture, layer, material, shader, group, instance
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        self.snapshot('tooltips_loaded')

    def row(self, path):
        QtCore, _, _, QtWidgets = tb.qt()
        tree = next(w for w in tb.all_widgets() if w.objectName() == 'outlinerTree' and w.isVisible())
        tree.expandAll()
        it = QtWidgets.QTreeWidgetItemIterator(tree)
        while it.value():
            if it.value().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) == path:
                tree.scrollToItem(it.value())
                return tree, it.value()
            it += 1
        raise AssertionError('Missing tooltip row ' + path)

    def capture_tooltip(self, widget, point, expected, label):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        QtWidgets.QToolTip.hideText()
        tb.process_events(100)
        QtTest.QTest.mouseMove(widget, point)
        event = QtGui.QHelpEvent(QtCore.QEvent.Type.ToolTip, point, widget.mapToGlobal(point))
        QtWidgets.QApplication.sendEvent(widget, event)
        def visible():
            for w in QtWidgets.QApplication.topLevelWidgets():
                if w.windowType() == QtCore.Qt.WindowType.ToolTip and w.isVisible() and isinstance(w, QtWidgets.QLabel):
                    if w.text() == expected:
                        return True
            return False
        assert tb.wait_until(visible), 'actual Qt tooltip: ' + label
        tooltip = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                       if w.windowType() == QtCore.Qt.WindowType.ToolTip and w.isVisible()
                       and isinstance(w, QtWidgets.QLabel) and w.text() == expected)
        assert tooltip.width() > 30 and tooltip.height() > 20
        assert tooltip.grab().save(str(self.directory / (label + '-popup.png')))
        self.snapshot(label)
        QtWidgets.QToolTip.hideText()
        tb.process_events(100)

    def exercise(self):
        QtCore, QtGui, _, QtWidgets = tb.qt()
        tree, row = self.row('/World/Group/Shape')
        name = row.data(0, QtCore.Qt.ItemDataRole.ToolTipRole)
        assert name.startswith('<qt>') and '<b>Shape</b>' in name and 'Type' in name and 'Path' in name
        rect = tree.visualItemRect(row)
        self.capture_tooltip(tree.viewport(), QtCore.QPoint(rect.left() + 70, rect.center().y()), name, 'prim-tooltip')
        tree, row = self.row('/World/Group')
        column = tree.columnCount() - 1
        override = row.data(column, QtCore.Qt.ItemDataRole.ToolTipRole)
        assert all(text in override for text in ['Edit-layer overrides', 'Layer', 'This prim', 'material:binding',
                                                '/Materials/Paint', 'Stronger than descendants', 'Descendants (1)', 'visibility'])
        rect = tree.visualItemRect(row)
        point = QtCore.QPoint(tree.header().sectionViewportPosition(column) + tree.columnWidth(column) // 2,
                             rect.center().y())
        self.capture_tooltip(tree.viewport(), point, override, 'override-tooltip')
        tree, row = self.row('/World/Instance/Shape')
        proxy = row.data(0, QtCore.Qt.ItemDataRole.ToolTipRole)
        assert 'Proxy (read-only)' in proxy
        assert tb.trigger_action('materialEditor')
        assert tb.wait_until(lambda: any(w.objectName() == 'materialDialog' and w.isVisible() for w in tb.all_widgets()))
        dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog' and w.isVisible())
        icons = dialog.findChild(QtWidgets.QListWidget, 'icons')
        assert tb.wait_until(lambda: icons.count() == 1)
        item = icons.item(0)
        swatch = item.toolTip()
        assert all(text in swatch for text in ['<b>Paint</b>', 'Shader type', 'USD Preview Surface', '/Materials/Paint'])
        self.capture_tooltip(icons.viewport(), icons.visualItemRect(item).center(), swatch, 'swatch-tooltip')
        dialog.hide()
        stageviz.command.clear_relationships(['/World/Group.material:binding'])
        assert tb.wait_for_commands()
        tree, row = self.row('/World/Group')
        # The group no longer has its own property override; descendant opinions remain.
        cleared = row.data(tree.columnCount() - 1, QtCore.Qt.ItemDataRole.ToolTipRole)
        assert 'material:binding' not in cleared
        stageviz.command.undo()
        assert tb.wait_for_commands()
        tree, row = self.row('/World/Group')
        assert 'material:binding' in row.data(tree.columnCount() - 1, QtCore.Qt.ItemDataRole.ToolTipRole)
        print('TOOLTIP_CHECKPOINT', json.dumps({'selection':list(stageviz.session().paths()), 'filename':stageviz.session().filename()}))
