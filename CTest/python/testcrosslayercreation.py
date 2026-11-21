# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Outliner child creation under a parent defined in a stronger layer."""
from pathlib import Path
import runpy

import stageviz
from pxr import Sdf, Usd, UsdGeom

_helpers=runpy.run_path(str(Path(__file__).with_name('testcontextmenu.py')))
tb=_helpers['tb']


class CrossLayerCreationTest(_helpers['ContextMenuTest']):
    def setup(self):
        assert self.disposable
        self.original_filename=stageviz.session().filename()
        self.original_selection=list(stageviz.session().paths())
        self.look_path=self.directory/'children.usda'
        look=Sdf.Layer.CreateNew(str(self.look_path))
        look.Save()
        self.fixture=self.directory/'root.usda'
        stage=Usd.Stage.CreateNew(str(self.fixture))
        stage.GetRootLayer().subLayerPaths=[str(self.look_path)]
        UsdGeom.Xform.Define(stage,'/World')
        UsdGeom.Cube.Define(stage,'/World/Existing')
        stage.GetRootLayer().Save()
        del stage,look
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        assert stageviz.session().setEditLayer(str(self.look_path)) and tb.wait_for_commands()
        stage=stageviz.session().stage()
        self.root_before=stage.GetRootLayer().ExportToString()
        self.look_before=stage.GetEditTarget().GetLayer().ExportToString()
        self.snapshot('cross-layer-loaded')

    def check(self,created):
        stage=stageviz.session().stage()
        look=Sdf.Layer.Find(str(self.look_path))
        assert bool(stage.GetPrimAtPath('/World/Xform'))==created
        assert stage.GetRootLayer().ExportToString()==self.root_before
        assert stage.GetPrimAtPath('/World/Existing')
        if created:
            assert look.GetPrimAtPath('/World').specifier==Sdf.SpecifierOver
            assert look.GetPrimAtPath('/World/Xform').specifier==Sdf.SpecifierDef
            assert stage.GetPrimAtPath('/World/Xform').GetTypeName()=='Xform'
        else:
            assert look.ExportToString()==self.look_before

    def create(self):
        self.menu(['/World'],'createChildXform')
        self.check(True)
        assert list(stageviz.session().paths())==['/World/Xform']
        QtCore,_,_,QtWidgets=tb.qt()
        tree=next(w for w in tb.all_widgets() if w.objectName()=='outlinerTree' and w.isVisible())
        tree.expandToDepth(2)
        iterator=QtWidgets.QTreeWidgetItemIterator(tree)
        rows=[]
        while iterator.value():
            rows.append(str(iterator.value().data(0,QtCore.Qt.ItemDataRole.UserRole+1)))
            iterator+=1
        assert '/World/Xform' in rows
        self.snapshot('outliner-child-created')

    def history(self):
        # UI history is scoped to an edit target. The native regression separately
        # exercises command objects after a direct target switch.
        self.command(stageviz.command.undo)
        self.check(False)
        assert list(stageviz.session().paths())==['/World']
        self.command(stageviz.command.redo)
        self.check(True)
        self.snapshot('child-redo-original-layer')
        self.command(stageviz.command.undo)
        self.check(False)
