# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Packaged material source labels and whole-mesh face-subset assignment."""
from pathlib import Path
import json
import runpy
import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade, UsdUtils

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN':False})
tb = _base['tb']


class PackagedMaterialAssignmentTest(_base['ViewportTest']):
    target='/Asset/Mesh'

    def setup(self):
        assert self.disposable
        self.original_filename=stageviz.session().filename()
        self.original_selection=list(stageviz.session().paths())
        geometry=self.directory/'geometry.usdc'
        stage=Usd.Stage.CreateNew(str(geometry))
        mesh=UsdGeom.Mesh.Define(stage,'/Geometry')
        mesh.CreatePointsAttr().Set([(-2,-2,0),(2,-2,0),(2,2,0),(-2,2,0)])
        mesh.CreateFaceVertexCountsAttr().Set([3,3])
        mesh.CreateFaceVertexIndicesAttr().Set([0,1,2,0,2,3])
        mesh.CreateSubdivisionSchemeAttr().Set('none')
        for name,index,color in [('Left',0,(1,0,0)),('Right',1,(0,0,1))]:
            subset=UsdGeom.Subset.CreateGeomSubset(mesh,name,'face',[index],'materialBind')
            material=self.material(stage,str(subset.GetPath())+'/Paint',color)
            UsdShade.MaterialBindingAPI.Apply(subset.GetPrim()).Bind(material)
        stage.GetRootLayer().Save()
        root=self.directory/'asset.usda'
        stage=Usd.Stage.CreateNew(str(root))
        UsdGeom.Xform.Define(stage,'/Asset')
        prim=stage.DefinePrim(self.target,'Mesh')
        prim.GetReferences().AddReference(str(geometry),'/Geometry')
        stage.GetRootLayer().Save()
        package=self.directory/'scene.usdz'
        assert UsdUtils.CreateNewUsdzPackage(Sdf.AssetPath(str(root)),str(package))
        external=self.directory/'external.usda'
        stage=Usd.Stage.CreateNew(str(external))
        self.material(stage,'/External',(1,1,0))
        stage.GetRootLayer().Save()
        self.fixture=self.directory/'overlay.usda'
        stage=Usd.Stage.CreateNew(str(self.fixture))
        stage.GetRootLayer().subLayerPaths=[str(package)]
        self.material(stage,'/Looks/Assigned',(0,1,0))
        self.material(stage,'/Looks/Blue',(0,0,1))
        stage.DefinePrim('/Looks/Referenced','Material').GetReferences().AddReference(str(external),'/External')
        stage.GetRootLayer().Save()
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
        self.initial=self.resolved()
        assert len(set(self.initial.values()))==2
        self.weak={l.identifier:l.ExportToString() for l in stageviz.session().stage().GetUsedLayers() if '.usdz[' in l.identifier}
        assert self.weak
        camera=stageviz.session().viewState().camera()
        camera.setFocusPoint(0,0,0)
        camera.setCameraDistance(12)
        camera.setCameraView(stageviz.CameraViewFront)
        assert tb.trigger_action('materialEditor')
        self.check(False,'package-loaded')

    @staticmethod
    def material(stage,path,color):
        material=UsdShade.Material.Define(stage,path)
        shader=UsdShade.Shader.Define(stage,path+'/Surface')
        shader.CreateIdAttr('UsdPreviewSurface')
        shader.CreateInput('diffuseColor',Sdf.ValueTypeNames.Color3f).Set(color)
        shader.CreateInput('roughness',Sdf.ValueTypeNames.Float).Set(.7)
        material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface',Sdf.ValueTypeNames.Token))
        return material

    def resolved(self):
        stage=stageviz.session().stage()
        return {str(v.GetPath()):str(UsdShade.MaterialBindingAPI(v.GetPrim()).ComputeBoundMaterial()[0].GetPath())
                for v in UsdShade.MaterialBindingAPI(stage.GetPrimAtPath(self.target)).GetMaterialBindSubsets()}

    def check(self,assigned,label,colors=None):
        values=self.resolved()
        expected=assigned if isinstance(assigned,dict) else dict.fromkeys(self.initial,'/Looks/Assigned') if assigned else self.initial
        assert values==expected, (values,expected)
        for identifier,before in self.weak.items():
            assert Sdf.Layer.Find(identifier).ExportToString()==before
        def colors_match():
            stageviz.render().capture()
            image=self.viewport().grab().toImage()
            counts=[0,0,0]
            for y in range(image.height()//10,image.height()*9//10,8):
                for x in range(image.width()//10,image.width()*9//10,8):
                    c=image.pixelColor(x,y)
                    rgb=(c.red(),c.green(),c.blue())
                    for i in range(3):
                        # Exclude the orange selection outline from material colors.
                        if rgb[i]>60 and max(rgb[(i+1)%3],rgb[(i+2)%3])<rgb[i]*.4:
                            counts[i]+=1
            visible=colors if colors is not None else [1] if assigned else [0,2]
            return all(counts[i]>15 if i in visible else counts[i]<10 for i in range(3))
        matches=tb.wait_until(colors_match,timeout=12)
        assert self.viewport().grab().save(str(self.directory/(label+'-viewport.png')))
        assert matches, (label,self.resolved(),str(self.directory))
        self.snapshot(label)
        print('PACKAGE_ASSIGNMENT_CHECKPOINT',json.dumps(values))

    def menu(self):
        QtCore,_,_,QtWidgets=tb.qt()
        assert tb.wait_until(lambda:any(w.objectName()=='materialDialog' and w.isVisible() for w in tb.all_widgets()))
        dialog=next(w for w in tb.all_widgets() if w.objectName()=='materialDialog' and w.isVisible())
        button=dialog.findChild(QtWidgets.QToolButton,'display')
        seen=[]
        def inspect():
            menu=QtWidgets.QApplication.activePopupWidget()
            assert isinstance(menu,QtWidgets.QMenu)
            actions=menu.actions()
            assert actions[0].text()=='Sources'
            sources=menu.findChild(QtWidgets.QMenu,'displaySourcesMenu')
            assert sources is not None
            source_actions=sources.actions()
            sources.popup(menu.mapToGlobal(menu.actionGeometry(actions[0]).topRight()))
            groups=[sources.findChild(QtWidgets.QMenu,name) for name in
                    ['displayLayersMenu','displayReferencedAssetsMenu','displayPackagesMenu']]
            assert [g.title() for g in groups]==['Layers','Referenced assets','Packages']
            layers,references,packages=groups
            assert any(a.text()=='overlay.usda' for a in layers.actions())
            assert any(a.text()=='external.usda' for a in references.actions())
            assert all('.usdz[' not in str(a.data() or '') for a in references.actions())
            package_actions=packages.actions()
            assert len(package_actions)==1 and package_actions[0].text()=='scene.usdz'
            package=package_actions[0].menu()
            assert package is not None
            for action in package.actions():
                identifier=str(action.data() or '')
                assert '.usdz[' in identifier
                assert action.text()=='geometry.usdc', action.text()
                assert action.toolTip()==identifier
                seen.append(identifier)
            assert len(seen)==len(set(seen))==1
            package.popup(packages.mapToGlobal(packages.actionGeometry(package_actions[0]).topRight()))
            assert package.grab().save(str(self.directory/'package-members-menu.png'))
            action=package.actions()[0]
            action.setChecked(False)
            assert button.isChecked()
            action.setChecked(True)
            assert sources.grab().save(str(self.directory/'package-layers-menu.png'))
            menu.close()
        QtCore.QTimer.singleShot(0,inspect)
        button.click()
        assert seen
        assert not button.isChecked()
        # The same source hierarchy is reused by the Outliner, whose other
        # display controls remain outside Sources.
        tree=next(w for w in tb.all_widgets() if w.objectName()=='outlinerTree' and w.isVisible())
        view=tree.parentWidget()
        while view and view.findChild(QtWidgets.QToolButton,'display') is None:
            view=view.parentWidget()
        outliner_button=view.findChild(QtWidgets.QToolButton,'display')
        def inspect_outliner():
            menu=QtWidgets.QApplication.activePopupWidget()
            actions=menu.actions()
            assert actions[0].text()=='Sources'
            assert any(a.text()=='Prim types' for a in actions) and any(a.text()=='Purpose' for a in actions)
            sources=menu.findChild(QtWidgets.QMenu,'displaySourcesMenu')
            groups=[sources.findChild(QtWidgets.QMenu,name) for name in
                    ['displayLayersMenu','displayReferencedAssetsMenu','displayPackagesMenu']]
            assert [g.title() for g in groups]==['Layers','Referenced assets','Packages']
            package_actions=groups[2].actions()
            assert len(package_actions)==1
            package=package_actions[0].menu()
            entries=package.actions()
            identifiers=[str(a.data() or '') for a in entries]
            assert set(seen).issubset(identifiers) and len(identifiers)==len(set(identifiers))
            menu.close()
        QtCore.QTimer.singleShot(0,inspect_outliner)
        outliner_button.click()
        self.check(False,'package-labels')

    def assignment(self):
        stageviz.command.assign_material([self.target],'/Looks/Assigned')
        assert tb.wait_for_commands()
        self.check(True,'mesh-assigned')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.check(False,'assign-undo')
        stageviz.command.redo()
        assert tb.wait_for_commands()
        self.check(True,'assign-redo')
        stageviz.command.clear_material([self.target])
        assert tb.wait_for_commands()
        self.check(False,'mesh-cleared')
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.check(True,'clear-undo')

    def subsets(self):
        QtCore,QtGui,QtTest,QtWidgets=tb.qt()
        session=stageviz.session()
        before=session.stage().GetRootLayer().ExportToString()
        stageviz.command.select_paths([self.target])
        assert tb.wait_for_commands()
        dialog=next(w for w in tb.all_widgets() if w.objectName()=='materialDialog' and w.isVisible())
        icons=dialog.findChild(QtWidgets.QAbstractItemView,'icons')
        index=next(icons.model().index(row,0) for row in range(icons.model().rowCount())
                   if icons.model().index(row,0).data()=='Blue')
        icons.scrollTo(index)
        icons.setCurrentIndex(index)
        tb.process_events(50)
        errors=[]
        seen=[]
        menus=[]
        def close():
            for menu in reversed(menus):
                menu.close()
        def choose():
            try:
                faces=menus[-1]
                actions=faces.actions()
                assert [a.objectName() for a in actions]==['assignFaceSubset_Left','assignFaceSubset_Right']
                assert all(a.text().endswith('Assigned') for a in actions), [a.text() for a in actions]
                assert actions[0].data()==self.target+'/Left'
                assert '/Looks/Assigned' in actions[0].toolTip()
                assert faces.grab().save(str(self.directory/'face-subsets-menu.png'))
                seen.extend(a.text() for a in actions)
                faces.setActiveAction(actions[0])
                QtTest.QTest.keyClick(faces,QtCore.Qt.Key.Key_Return)
            except Exception as error:
                errors.append(str(error))
                close()
        def open_faces():
            try:
                root=QtWidgets.QApplication.activePopupWidget()
                assert root.objectName()=='materialContextMenu'
                menus.append(root)
                assign=root.findChild(QtWidgets.QMenu,'assignMaterialMenu')
                assert assign is not None
                assert assign.findChild(QtGui.QAction,'assignWholeObject').isEnabled()
                faces=assign.findChild(QtWidgets.QMenu,'assignFaceSubsetsMenu')
                menus.extend([assign,faces])
                root.setActiveAction(assign.menuAction())
                assign.popup(root.mapToGlobal(root.actionGeometry(assign.menuAction()).topRight()))
                assign.setActiveAction(faces.menuAction())
                faces.popup(assign.mapToGlobal(assign.actionGeometry(faces.menuAction()).topRight()))
                QtCore.QTimer.singleShot(50,choose)
            except Exception as error:
                errors.append(str(error))
                close()
        watchdog=QtCore.QTimer(icons)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close)
        watchdog.start(5000)
        QtCore.QTimer.singleShot(50,open_faces)
        position=icons.visualRect(index).center()
        event=QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Keyboard,position,
                                     icons.viewport().mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(icons.viewport(),event)
        watchdog.stop()
        watchdog.deleteLater()
        assert seen and not errors, errors
        assert tb.wait_for_commands()
        expected={self.target+'/Left':'/Looks/Blue',self.target+'/Right':'/Looks/Assigned'}
        self.check(expected,'subset-assigned',[1,2])
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert session.stage().GetRootLayer().ExportToString()==before
        self.check(True,'subset-undo')
        stageviz.command.redo()
        assert tb.wait_for_commands()
        self.check(expected,'subset-redo',[1,2])
        stageviz.command.assign_material([self.target+'/Right'],'/Looks/Blue')
        assert tb.wait_for_commands()
        self.check(dict.fromkeys(expected,'/Looks/Blue'),'second-subset',[2])
        stageviz.command.undo()
        assert tb.wait_for_commands()
        self.check(expected,'second-subset-undo',[1,2])

    def follow(self):
        QtCore,QtGui,QtTest,QtWidgets=tb.qt()
        session=stageviz.session()
        before=session.stage().GetRootLayer().ExportToString()
        dialog=next(w for w in tb.all_widgets() if w.objectName()=='materialDialog' and w.isVisible())
        browser=dialog.findChild(QtWidgets.QWidget,'browserWidget')
        button=browser.findChild(QtWidgets.QToolButton,'follow')
        icons=browser.findChild(QtWidgets.QListWidget,'icons')
        assert button.isCheckable() and not button.isChecked() and not button.icon().isNull()
        layout=browser.findChild(QtWidgets.QHBoxLayout,'filterLayout')
        assert layout.indexOf(button)==layout.indexOf(browser.findChild(QtWidgets.QToolButton,'clear'))+1
        assert layout.indexOf(button)+1==layout.indexOf(browser.findChild(QtWidgets.QToolButton,'display'))
        stageviz.command.select_paths([self.target])
        assert tb.wait_for_commands()
        initial_selection=[item.text() for item in icons.selectedItems()]
        button.click()
        assert [item.text() for item in icons.selectedItems()]==initial_selection
        assert list(session.paths())==[self.target]
        tree=next(w for w in tb.all_widgets() if w.objectName()=='propertyTree' and w.isVisible())
        def binding_row():
            iterator=QtWidgets.QTreeWidgetItemIterator(tree)
            while iterator.value():
                item=iterator.value()
                if item.text(0)=='material:binding' and item.text(1)=='/Looks/Assigned':
                    return item
                iterator+=1
        row=binding_row()
        assert row is not None
        branch=[]
        parent=row.parent()
        while parent:
            branch.append(parent.text(0))
            parent=parent.parent()
        for label in reversed(branch):
            items=tree.findItems(label,QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive,0)
            assert items, label
            items[0].setExpanded(True)
            tb.process_events(25)
        row=binding_row()
        assert row is not None
        tree.scrollToItem(row)
        grid=icons.gridSize()
        icons.setGridSize(QtCore.QSize(icons.viewport().width()-1,icons.iconSize().height()+40))
        icons.scrollToBottom()
        tb.process_events(50)
        errors=[]
        seen=[]
        def choose():
            menu=QtWidgets.QApplication.activePopupWidget()
            try:
                action=next(a for a in menu.actions() if a.text()=='Select')
                seen.append(action.text())
                menu.setActiveAction(action)
                QtTest.QTest.keyClick(menu,QtCore.Qt.Key.Key_Return)
            except Exception as error:
                errors.append(str(error))
                if menu:
                    menu.close()
        QtCore.QTimer.singleShot(50,choose)
        position=tree.visualItemRect(binding_row()).center()
        event=QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Keyboard,position,
                                     tree.viewport().mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(tree.viewport(),event)
        assert seen and not errors, errors
        assert tb.wait_for_commands()
        def selected(name):
            return [item.text() for item in icons.selectedItems()]==[name]
        def visible(name):
            item=next(icons.item(i) for i in range(icons.count()) if icons.item(i).text()==name)
            return icons.viewport().rect().intersects(icons.visualItemRect(item))
        assert list(session.paths())==['/Looks/Assigned']
        assert tb.wait_until(lambda:selected('Assigned') and visible('Assigned'))
        assert browser.grab().save(str(self.directory/'material-follow-browser.png'))
        self.snapshot('property-select-follow')
        stageviz.command.undo()
        assert tb.wait_for_commands() and list(session.paths())==[self.target]
        assert selected('Assigned')
        stageviz.command.redo()
        assert tb.wait_for_commands() and selected('Assigned')
        dialog.hide()
        stageviz.command.select_paths(['/Looks/Blue'])
        assert tb.wait_for_commands() and selected('Assigned')
        dialog.show()
        assert tb.wait_until(lambda:selected('Blue') and visible('Blue'))
        assert browser.findChild(QtWidgets.QWidget,'widget').grab().save(str(self.directory/'material-follow-toolbar.png'))
        self.snapshot('follow-on-open')
        button.click()
        stageviz.command.select_paths(['/Looks/Assigned'])
        assert tb.wait_for_commands() and selected('Blue')
        icons.setGridSize(grid)
        assert session.stage().GetRootLayer().ExportToString()==before
