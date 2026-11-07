# SPDX-License-Identifier: BSD-3-Clause
"""Async namespace, tree, property and render checks in an owned benchmark app."""
import hashlib
import json
from pathlib import Path
import random
import runpy
import time
import contextlib
import io
import stageviz
from pxr import Gf, Sdf, UsdGeom, UsdShade
from PySide6 import QtCore, QtGui, QtTest, QtWidgets
import shiboken6

base = runpy.run_path(str(Path(__file__).resolve().parents[1]/'sweep_scale/embedded.py'), init_globals={'_SCALE_AUTORUN':False})
ScaleBenchmark = base['ScaleBenchmark']
tb = base['support']['support']['tb']
PATH_ROLE = int(QtCore.Qt.ItemDataRole.UserRole)+1
# Reuse the non-owning row proxy from the existing UI suite.
TreeIndexRef = runpy.run_path(str(Path(__file__).resolve().parents[2]/'python/testui.py'))['TreeIndexRef']


class NamespaceWorkflow(ScaleBenchmark):
    def __init__(self, config):
        self.steps = {step['name']:step for step in config['steps']}
        config['phases'] = list(self.steps)
        super().__init__(config)
        self.qt_root = stageviz.application().window()
        property_width=config.get('propertyPaneWidth',0)
        if property_width:
            splitter=self.qt_root.findChild(QtWidgets.QSplitter,'splitter')
            assert splitter is not None
            splitter.setSizes([240,max(400,splitter.width()-240-property_width),property_width])
        self.meshes = []
        self.sampled = []
        self.source = config['branch']
        self.renamed = str(Sdf.Path(self.source).GetParentPath().AppendChild(Sdf.Path(self.source).name+'_Audit'))
        self.moved = '/AuditTarget/'+Sdf.Path(self.source).name
        self.root = config['root']
        self.renamed_root = self.root+'_Audit'
        self.moved_root = '/AuditTarget/'+Sdf.Path(self.root).name
        self.timer.setInterval(25)

    def tree(self):
        return tb.find_widget('outlinerTree',QtWidgets.QTreeWidget,tb.viewer())

    def item(self,path,expand=False):
        tree=self.tree();model=tree.model();parent=QtCore.QModelIndex()
        wanted=['/']+[str(p) for p in Sdf.Path(path).GetPrefixes() if str(p)!='/']
        for prefix in wanted:
            found=next((model.index(i,0,parent) for i in range(model.rowCount(parent))
                        if model.index(i,0,parent).data(PATH_ROLE)==prefix),None)
            if found is None:return None
            if expand:tree.setExpanded(found,True)
            parent=found
        return TreeIndexRef(tree,parent)

    def world(self,path):
        prim=stageviz.session().stage().GetPrimAtPath(path)
        return UsdGeom.XformCache().GetLocalToWorldTransform(prim)

    def remap(self,path,old,new):
        return str(Sdf.Path(path).ReplacePrefix(Sdf.Path(old),Sdf.Path(new)))

    def drop(self,paths,target):
        tree=self.tree()
        self.item(str(Sdf.Path(target).GetParentPath()),True)
        item=self.item(target)
        assert item is not None,target
        tree.scrollTo(item.index);tree.setFocus()
        pos=tree.visualRect(item.index).center()
        assert tree.viewport().rect().contains(pos)
        mime=QtCore.QMimeData();mime.setData('application/x-stageviz-prim-path','\n'.join(paths).encode())
        enter=QtGui.QDragEnterEvent(pos,QtCore.Qt.DropAction.MoveAction,mime,QtCore.Qt.MouseButton.LeftButton,QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(tree.viewport(),enter)
        move=QtGui.QDragMoveEvent(pos,QtCore.Qt.DropAction.MoveAction,mime,QtCore.Qt.MouseButton.LeftButton,QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(tree.viewport(),move)
        drop=QtGui.QDropEvent(QtCore.QPointF(pos),QtCore.Qt.DropAction.MoveAction,mime,QtCore.Qt.MouseButton.LeftButton,QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(tree.viewport(),drop)
        self.drop_accepted=drop.isAccepted()

    def begin(self):
        self.current=self.phases[len(self.results)]
        self.frames=[];self.ready_at=None;self.responsive_at=None
        self.active=True;self.dispatched=False
        self.start_ms=time.perf_counter()*1000;self.start_wall=time.time()*1000
        self.status('running',start_wall_ms=self.start_wall)
        self.expected_paths=None
        return self.steps[self.current],stageviz.session(),self.viewport()

    def start(self):
        step,session,widget=self.begin();action=step['action'];command=stageviz.command
        if action=='load':
            session.setPreserveState(False)
            assert session.loadFromFile(self.config['fixture'])
            view=session.viewState();view.setSceneMaterialsEnabled(False);view.setSceneStatsEnabled(True)
            view.setPerformanceStatsEnabled(False);view.setRenderMode(stageviz.RenderShaded)
            camera=view.camera();camera.setAspectRatioLocked(False);camera.setAxisYaw(45);camera.setAxisPitch(-35);camera.setAxisRoll(0)
            self.mode('Select')
        elif action=='frame':
            session.viewState().camera().frame(session.boundingBox());session.viewState().camera().distance(1.1)
        elif action=='prepare':
            command.new_xform('/','AuditTarget')
            assert tb.wait_until(lambda: bool(session.stage().GetPrimAtPath('/AuditTarget')))
            command.new_xform('/AuditTarget','ExistingA');command.new_xform('/AuditTarget','ExistingB')
            assert tb.wait_until(lambda: bool(session.stage().GetPrimAtPath('/AuditTarget/ExistingB')))
            delta=Gf.Matrix4d(1).SetScale(Gf.Vec3d(1.2,.8,1.1))*Gf.Matrix4d(1).SetRotate(Gf.Rotation(Gf.Vec3d(1,2,3),23));delta.SetTranslateOnly(Gf.Vec3d(3,4,5))
            command.set_transforms(['/AuditTarget'],[[list(row) for row in Gf.Matrix4d(1)]],[[list(row) for row in delta]])
            assert tb.wait_until(lambda: Gf.IsClose(self.world('/AuditTarget'),delta,1e-6))
            self.item(self.source,True)
            command.select_paths([self.source])
        elif action=='select':
            count=min(step.get('count',1),len(self.meshes))
            rng=random.Random(20261106);paths=rng.sample(self.meshes,count)
            self.expected_paths=paths
            command.select_paths(paths)
        elif action=='repeat_selection':
            self.expected_paths=list(session.paths());command.select_paths(self.expected_paths)
        elif action=='property_refresh':
            command.hide_paths(list(session.paths()))
        elif action=='restore_visibility':
            command.undo()
        elif action in ('expand_properties','collapse_properties'):
            tree=tb.find_widget('propertyTree',QtWidgets.QTreeWidget,tb.viewer())
            assert tree is not None and tree.isVisible(),'Property Editor is visible'
            model=tree.model();self.property_expanded={}
            for i in range(model.rowCount()):
                index=model.index(i,0);expanded=action=='expand_properties'
                tree.setExpanded(index,expanded)
                self.property_expanded[str(index.data())]=expanded
            assert 'Attributes' in self.property_expanded
        elif action=='select_parent_child':
            leaf=next(p for p in self.meshes if Sdf.Path(p).HasPrefix(Sdf.Path(self.source)))
            self.expected_paths=[leaf,self.source];command.select_paths(self.expected_paths)
        elif action=='select_branch':
            self.expected_paths=[self.source];command.select_paths(self.expected_paths)
        elif action=='rename_editor':
            tree=self.tree();item=self.item(self.source,True)
            assert item is not None
            tree.setCurrentIndex(item.index);tree.setFocus();tree.edit(item.index)
            editor=tree.focusWidget()
            assert isinstance(editor,QtWidgets.QLineEdit),'inline tree editor is active'
            editor.setText(Sdf.Path(self.renamed).name)
            QtTest.QTest.keyClick(editor,QtCore.Qt.Key.Key_Return)
        elif action=='move_drop':
            self.item('/AuditTarget').setExpanded(False)
            self.before_move=self.world(self.source)
            self.drop([self.source],'/AuditTarget')
            assert self.drop_accepted,'native Outliner accepts a valid parent drop'
        elif action=='multi_prepare':
            available=[p for p in self.meshes if Sdf.Path(p).HasPrefix(Sdf.Path(self.source))]
            self.multi=available[:3]
            assert len(self.multi)==3
            self.before_multi=[self.world(p) for p in self.multi]
            command.select_paths(self.multi)
        elif action=='multi_drop':
            self.drop(self.multi,'/AuditTarget')
            assert self.drop_accepted
        elif action=='default_guard':
            self.protected_default=bool(session.stage().GetDefaultPrim()) and str(session.stage().GetDefaultPrim().GetPath())==self.root
            if self.protected_default:command.rename_path(self.root,Sdf.Path(self.renamed_root).name)
        elif action=='clear_default':command.clear_default_prim()
        elif action=='root_rename':
            command.rename_path(self.root,Sdf.Path(self.renamed_root).name)
        elif action=='root_move':
            command.move_path([self.root],'/AuditTarget')
        elif action=='invalid_drop':
            command.clear();self.drop([self.source],self.multi[0])
            assert not self.drop_accepted,'dropping an ancestor inside itself is rejected'
        elif action=='undo':command.undo()
        elif action=='redo':command.redo()
        elif action=='materials':session.viewState().setSceneMaterialsEnabled(step['enabled'])
        elif action=='wire':session.viewState().setRenderMode(stageviz.RenderWireframe)
        elif action=='shaded':session.viewState().setRenderMode(stageviz.RenderShaded)
        elif action=='cleanup':
            command.clear();assert session.reload();command.select_paths([]);command.clear()
        else:raise AssertionError(action)
        self.dispatch_end=time.perf_counter()*1000;self.dispatched=True;widget.update()

    def expected(self):
        session=stageviz.session();stage=session.stage();step=self.steps[self.current]
        if session.primsUpdate()!=stageviz.PrimsImmediate or not stage:return False
        for path in step.get('exists',[]):
            if not stage.GetPrimAtPath(path):return False
        for path in step.get('absent',[]):
            if stage.GetPrimAtPath(path):return False
        if self.expected_paths is not None and list(session.paths())!=self.expected_paths:return False
        return True

    def finish(self):
        end=time.perf_counter()*1000;self.active=False
        step=self.steps[self.current];action=step['action'];session=stageviz.session();stage=session.stage()
        selected=list(session.paths())
        self.selection_checkpoint(step,selected)
        if action=='load':
            self.meshes=[str(p.GetPath()) for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
            assert self.meshes
            self.sampled=random.Random(6106).sample(self.meshes,min(64,len(self.meshes)))
            self.original_worlds={p:self.world(p) for p in self.sampled}
            self.original_default=str(stage.GetDefaultPrim().GetPath()) if stage.GetDefaultPrim() else ''
            self.original_count=sum(1 for _ in stage.Traverse())
        if action=='prepare':
            self.original_order=[str(p.GetPath()) for p in stage.GetPrimAtPath('/AuditTarget').GetChildren()]
            self.unaffected_index=QtCore.QPersistentModelIndex(self.item(self.root).index)
            stageviz.command.clear()
        if action=='move_drop':
            assert Gf.IsClose(self.world(self.moved),self.before_move,1e-6),'parent drop preserves world transform'
        if action=='multi_drop':
            assert len(selected)==len(self.multi) and all(Sdf.Path(p).GetParentPath()==Sdf.Path('/AuditTarget') for p in selected)
            assert all(Gf.IsClose(self.world(p),matrix,1e-6) for p,matrix in zip(selected,self.before_multi))
        if action=='invalid_drop':assert not stageviz.command.can_undo(),'rejected drop does not create history'
        old,new=step.get('sample_remap',[None,None])
        for path,matrix in self.original_worlds.items():
            current=self.remap(path,old,new) if old else path
            # Multi-leaf moves are validated separately through selection mapping.
            if action=='multi_drop' and path in self.multi:continue
            if stage.GetPrimAtPath(current):
                assert Gf.IsClose(self.world(current),matrix,1e-5),current+' preserves world pose'
        for path in step.get('tree_exists',[]):assert self.item(path,True) is not None,('Qt row missing',path)
        if action=='cleanup':
            assert sum(1 for _ in stage.Traverse())==self.original_count
            assert str(stage.GetDefaultPrim().GetPath())==self.original_default if stage.GetDefaultPrim() else self.original_default==''
            assert not stage.GetRootLayer().dirty and not selected and not stageviz.command.can_undo()
        captured=[]
        if action in ('frame','rename_editor','move_drop','root_rename','root_move'):
            capture=stageviz.render().capture()
            captured=list(capture.paths())
            assert captured,'document capture contains visible geometry'
            assert all(bool(stage.GetPrimAtPath(path)) for path in captured),'rendered IDs resolve to current composed prims'
        prop_rows=None;property_expansion=None;property_details=None
        for widget in QtWidgets.QApplication.allWidgets():
            try:
                if widget.objectName()!='propertyTree' or not isinstance(widget,QtWidgets.QTreeWidget):continue
                model=widget.model()
                prop_rows=[(str(model.data(model.index(i,0))),str(model.data(model.index(i,1)))) for i in range(model.rowCount())]
                property_expansion={str(model.index(i,0).data()):widget.isExpanded(model.index(i,0)) for i in range(model.rowCount())}
                if self.config.get('propertyPaneWidth'):
                    property_details=[]
                    for i in range(model.rowCount()):
                        index=model.index(i,0)
                        if not widget.isExpanded(index):continue
                        for j in range(min(50,model.rowCount(index))):
                            property_details.append([str(index.data()),str(model.index(j,0,index).data()),str(model.index(j,1,index).data())])
                break
            except RuntimeError:
                continue
        assert prop_rows is not None,'live Property Editor model is available'
        if action in ('property_refresh','restore_visibility') and hasattr(self,'property_expanded'):
            for label,expanded in self.property_expanded.items():
                if label in property_expansion:
                    assert property_expansion[label]==expanded,('property expansion survives refresh',label)
        row=dict(phase=self.current,action=action,start_wall_ms=self.start_wall,dispatch_ms=self.dispatch_end-self.start_ms,
                 responsive_ms=self.responsive_at-self.start_ms,end_to_end_ms=end-self.start_ms,frames=len(self.frames),
                 selection_count=len(selected),selection_sha256=hashlib.sha256('\n'.join(selected).encode()).hexdigest(),selected_sample=selected[:8],
                 widget_size=[self.viewport().width(),self.viewport().height()],device_pixel_ratio=self.viewport().devicePixelRatioF(),view_state=self.view_state(),
                 property_sections=prop_rows,property_sections_expanded=property_expansion,property_rows=property_details,root_index_survived=self.unaffected_index.isValid() if hasattr(self,'unaffected_index') else None,action_state={name:tb.action_info(name) for name in ('editInstanceSelected','editSelectParent')},captured_count=len(captured),captured_sample=captured[:16],source_dirty=stage.GetRootLayer().dirty,verified=True,
                 target_children=[str(p.GetPath()) for p in stage.GetPrimAtPath('/AuditTarget').GetChildren()] if stage.GetPrimAtPath('/AuditTarget') else [])
        if action in ('prepare','rename_editor','move_drop','multi_drop','root_rename','root_move','materials','cleanup','expand_properties','collapse_properties','property_refresh','restore_visibility') or self.current in self.config.get('screenshotPhases',[]):
            assert tb.viewer().grab().save(str(self.directory/(self.current+'-viewer.png')))
            with contextlib.redirect_stdout(io.StringIO()):snapshot=tb.snapshot_qt_state(self.current)
            (self.directory/(self.current+'-qt.json')).write_text(json.dumps(snapshot,indent=2))
        self.results.append(row);(self.directory/'results.json').write_text(json.dumps(self.results,indent=2))
        self.waiting=self.current;self.status('checkpoint',result=row)

    def selection_checkpoint(self,step,selected):
        """Optional derived-workflow oracle, outside the recorded timing spans."""
        pass


if globals().get('_PIPELINE_AUTORUN',True):
    import os
    config=json.loads(Path(os.environ['STAGEVIZ_PIPELINE_PROFILE']).read_text())
    stageviz._namespaceWorkflow=NamespaceWorkflow(config)
