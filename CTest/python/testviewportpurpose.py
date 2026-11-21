# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Native viewport purpose visibility, sidebar menu, isolation and saved state."""
from pathlib import Path
import json
import runpy
import stageviz
from pxr import Gf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class ViewportPurposeTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.fixture = self.directory/'purposes.usda'
        stage = Usd.Stage.CreateNew(str(self.fixture))
        self.shapes = {}
        for purpose,x in zip(('default','render','proxy','guide'),(-4.5,-1.5,1.5,4.5)):
            parent = UsdGeom.Xform.Define(stage,'/World/'+purpose.capitalize())
            parent.CreatePurposeAttr().Set(purpose)
            parent.AddTranslateOp().Set(Gf.Vec3d(x,0,0))
            path = str(parent.GetPath())+'/Shape'
            UsdGeom.Sphere.Define(stage,path)
            self.shapes[purpose] = path
        stage.GetRootLayer().Save()
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        state = stageviz.session().viewState()
        state.camera().setFocusPoint(0,0,0)
        state.camera().setCameraDistance(25)
        state.camera().setCameraView(stageviz.CameraViewFront)
        self.before = stageviz.session().stage().GetRootLayer().ExportToString()
        self.check(set(self.shapes),'initial')

    def check(self,purposes,label):
        expected = {self.shapes[p] for p in purposes}
        assert tb.wait_until(lambda:set(stageviz.render().capture().paths())==expected,timeout=10), (label,stageviz.render().capture().paths(),expected)
        assert stageviz.session().stage().GetRootLayer().ExportToString()==self.before
        self.snapshot(label)
        print('VIEWPORT_PURPOSE_CHECKPOINT',json.dumps({'paths':sorted(expected),'mask':list(stageviz.session().mask())}))

    def menu(self):
        QtCore,QtGui,QtTest,QtWidgets = tb.qt()
        button = next(w for w in tb.all_widgets() if w.objectName()=='viewportDisplay' and w.isVisible())
        wireframe = next(w for w in tb.all_widgets() if w.objectName()=='wireframe' and w.isVisible())
        assert button.size()==wireframe.size() and button.iconSize()==wireframe.iconSize()
        assert not button.isChecked(), 'All purposes are included by default'
        def open_menu(right=False,toggle=None):
            observed = []
            def inspect():
                menus=[w for w in QtWidgets.QApplication.topLevelWidgets() if isinstance(w,QtWidgets.QMenu) and w.objectName()=='viewportDisplayMenu' and w.isVisible()]
                assert len(menus)==1
                menu=menus[0]
                assert button.isChecked()
                assert [a.text() for a in menu.actions()]==['Purposes','Default','Render','Proxy','Guide']
                default=menu.findChild(QtGui.QAction,'viewportPurposeDefault')
                assert default.isChecked() and not default.isEnabled()
                if toggle:
                    menu.findChild(QtGui.QAction,toggle).trigger()
                observed.append(True)
                menu.close()
            QtCore.QTimer.singleShot(0,inspect)
            if right:
                event=QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse,button.rect().center(),button.mapToGlobal(button.rect().center()))
                QtWidgets.QApplication.sendEvent(button,event)
            else:
                button.click()
            assert observed
        open_menu(toggle='viewportPurposeGuide')
        assert button.isChecked()
        self.check({'default','render','proxy'},'guide-hidden')
        open_menu(toggle='viewportPurposeGuide')
        assert not button.isChecked()
        self.check(set(self.shapes),'all-purposes')
        open_menu(right=True,toggle='viewportPurposeRender')
        assert button.isChecked()
        self.check({'default','proxy','guide'},'render-hidden')

    def isolation(self):
        state=stageviz.session().viewState()
        state.setShowProxy(False)
        state.setShowGuides(False)
        self.check({'default'},'default-only')
        stageviz.command.isolate_paths(['/World/Render'])
        assert tb.wait_for_commands()
        self.check(set(),'isolated-render-hidden')
        state.setShowRender(True)
        self.check({'render'},'isolated-render-shown')
        assert list(stageviz.session().mask())==['/World/Render']
        stageviz.command.isolate_paths([])
        assert tb.wait_for_commands()
        self.check({'default','render'},'full-render-shown')

    def persistence(self):
        state=stageviz.session().viewState()
        state.setShowGuides(True)
        state.setShowRender(False)
        state.setShowProxy(True)
        filename=str(self.directory/'purpose.session')
        assert stageviz.session().saveState(filename)
        state.setShowGuides(False)
        state.setShowRender(True)
        state.setShowProxy(False)
        assert stageviz.session().loadState(filename)
        assert state.showGuides() and state.showProxy() and not state.showRender()
        self.check({'default','proxy','guide'},'state-restored')
