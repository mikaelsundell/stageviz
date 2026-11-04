# SPDX-License-Identifier: BSD-3-Clause
"""Real-data sweep/invert/gizmo input benchmark; launch once through external MCP."""
import contextlib,hashlib,io,json,os,runpy,time,random
from pathlib import Path
import stageviz
from pxr import Gf,Sdf,UsdGeom
from PySide6 import QtCore,QtGui,QtTest,QtWidgets

support=runpy.run_path(str(Path(__file__).with_name('benchmark_storm_layers.py')),
                     init_globals={'_STORM_AUTORUN':False})
Base=support['StormLayerBenchmark']

class SelectionBenchmark(Base):
    def __init__(self,config):
        self.original_config=dict(config)
        super().__init__({**config,'phases':['empty','add_front']})
        operations=['sweep_cold','clear_selection','sweep_warm','invert_selection','undo_invert',
                    'move_gizmo','drag_press','drag_step_1','drag_step_2','drag_release',
                    'undo_drag','redo_drag','restore_drag','reload_baseline']
        if config.get('cpuOnly'):operations=['seed_selection','invert_selection','undo_invert','reload_baseline']
        if config.get('cpuTransform'):
            operations=['seed_selection','move_gizmo','cpu_gizmo_prepare','drag_press','drag_step_1','drag_step_2',
                        'drag_release','undo_drag','redo_drag','restore_drag','reload_baseline']
        self.phases += [(name,name) for name in config.get('selectionPhases',operations)]
        self.saved_sweep=None;self.world_before={};self.root_before=None
        self.messages=[]
        for widget in QtWidgets.QApplication.allWidgets():
            if isinstance(widget,QtWidgets.QStatusBar):widget.messageChanged.connect(self.messages.append)

    def mode(self,name):
        for widget in QtWidgets.QApplication.allWidgets():
            if isinstance(widget,QtWidgets.QToolButton) and widget.objectName()==name.lower() and widget.isVisible() and widget.isCheckable():
                if not widget.isChecked():QtTest.QTest.mouseClick(widget,QtCore.Qt.MouseButton.LeftButton,pos=widget.rect().center())
                assert widget.isChecked(),name
                return
        raise AssertionError('Visible toolbar mode missing: '+name)

    def worlds(self,paths):
        stage=stageviz.session().stage();cache=UsdGeom.XformCache()
        return {path:cache.GetLocalToWorldTransform(stage.GetPrimAtPath(path)) for path in paths}

    def mouse_move(self,point,buttons=QtCore.Qt.MouseButton.NoButton):
        widget=self.viewport();local=QtCore.QPointF(point)
        event=QtGui.QMouseEvent(QtCore.QEvent.Type.MouseMove,local,
            QtCore.QPointF(widget.mapToGlobal(local.toPoint())),QtCore.Qt.MouseButton.NoButton,
            buttons,QtCore.Qt.KeyboardModifier.NoModifier)
        QtWidgets.QApplication.sendEvent(widget,event)

    def outliner_snapshot(self,tree):
        # The real stage can materialize hundreds of thousands of rows. Record
        # bounded expanded branches without walking the complete Qt hierarchy.
        model=tree.model();remaining=[256]
        def rows(parent,depth):
            output=[];count=model.rowCount(parent)
            for number in range(min(count,24)):
                if remaining[0]<=0:break
                remaining[0]-=1;index=model.index(number,0,parent)
                expanded=tree.isExpanded(index)
                row={'columns':[str(model.data(model.index(number,column,parent)) or '')
                                for column in range(model.columnCount(parent))],
                     'expanded':expanded,'childCount':model.rowCount(index)}
                if expanded and depth<6:row['children']=rows(index,depth+1)
                output.append(row)
            return output
        result={'objectName':tree.objectName(),'visible':tree.isVisible(),'enabled':tree.isEnabled(),
                'headers':[tree.headerItem().text(column) for column in range(tree.columnCount())],
                'rowLimit':256,'perParentLimit':24,'depthLimit':6}
        result['rows']=rows(QtCore.QModelIndex(),0)
        result['capturedRows']=256-remaining[0]
        return result

    def restored(self):
        stage=stageviz.session().stage()
        return self.signature(stage.GetRootLayer())==self.root_signature and all(
            Gf.IsClose(value,self.world_before[path],1e-7) for path,value in self.worlds(self.world_before).items())

    def signature(self,layer):
        # Empty ancestor over-specs left by undo are tracked separately. They
        # do not excuse residual authored properties, metadata or moved samples.
        result={'__metadata_header__':layer.ExportToString().split('\n\ndef ',1)[0]};stack=[layer.pseudoRoot]
        while stack:
            spec=stack.pop()
            fields={key:('checked in exported metadata header' if key=='subLayerOffsets' else repr(spec.GetInfo(key))) for key in spec.ListInfoKeys()
                    if not (key=='specifier' and isinstance(spec,Sdf.PrimSpec) and spec.specifier==Sdf.SpecifierOver)}
            if fields:result[str(spec.path)]=fields
            if isinstance(spec,Sdf.PrimSpec):
                stack.extend(spec.nameChildren);stack.extend(spec.properties)
        return result

    def start(self):
        next_phase=self.phases[len(self.results)][0]
        if next_phase in ('empty','add_front'):return super().start()
        self.current,self.kind=self.phases[len(self.results)]
        self.frames=[];self.dispatched=False;self.prims_ready_ms=None;self.framing_ms=0
        self.active=True;self.no_render=False;self.tracing=False;self.framed=True
        self.start_ms=time.perf_counter()*1000;self.start_wall_ms=time.time()*1000
        self.write_status('running',start_ms=self.start_ms,start_wall_ms=self.start_wall_ms)
        session=stageviz.session();widget=self.viewport();stage=session.stage()
        phase=self.current
        if phase=='seed_selection':stageviz.command.select_paths(self.seed)
        elif phase=='existing_xform_selection':
            target=None
            for prim in stage.Traverse():
                attr=prim.GetAttribute('xformOp:transform')
                if prim.IsInstance() or not attr or attr.GetTypeName()!=Sdf.ValueTypeNames.Matrix4d:continue
                children=list(prim.GetChildren())
                if 0<len(children)<=8 and any(child.IsA(UsdGeom.Boundable) for child in children):
                    target=prim;break
            assert target,'No existing matrix-transform parent found'
            self.control_target=str(target.GetPath())
            stageviz.command.select_paths([self.control_target])
            cache=UsdGeom.BBoxCache(0,UsdGeom.Imageable.GetOrderedPurposeTokens(),True)
            bounds=cache.ComputeWorldBound(target).ComputeAlignedRange()
            bounds.UnionWith(UsdGeom.XformCache().GetLocalToWorldTransform(target).ExtractTranslation())
            session.viewState().camera().frame((tuple(bounds.GetMin()),tuple(bounds.GetMax())))
        elif phase.startswith('sweep_'):
            self.mode('Select')
            r=self.original_config.get('sweepRect',[0.48,0.48,0.62,0.62])
            a=QtCore.QPoint(round(widget.width()*r[0]),round(widget.height()*r[1]))
            b=QtCore.QPoint(round(widget.width()*r[2]),round(widget.height()*r[3]))
            self.sweep_pixels=[a.x(),a.y(),b.x(),b.y()]
            QtTest.QTest.mousePress(widget,QtCore.Qt.MouseButton.LeftButton,pos=a)
            self.mouse_move(b,QtCore.Qt.MouseButton.LeftButton)
            QtTest.QTest.mouseRelease(widget,QtCore.Qt.MouseButton.LeftButton,pos=b)
        elif phase=='clear_selection':stageviz.command.select_paths([])
        elif phase=='invert_selection':
            self.pre_invert=list(session.paths())
            stageviz.command.select_invert()
        elif phase=='undo_invert':stageviz.command.undo()
        elif phase=='move_gizmo':self.mode('Move')
        elif phase=='cpu_gizmo_prepare':
            tracking=widget.hasMouseTracking()
            widget.setMouseTracking(True)
            self.mouse_move(widget.rect().center())
            widget.setMouseTracking(tracking)
            assert widget.property('transformPivotValid') is True,'CPU pivot projection failed'
        elif phase=='drag_press':
            point=widget.property('transformPivotScreen')
            assert (self.original_config.get('cpuTransform') or widget.property('transformPivotValid')) and point is not None,'Gizmo pivot not ready'
            self.drag_origin=QtCore.QPoint(round(point.x()),round(point.y()))
            assert widget.rect().contains(self.drag_origin),'Gizmo outside viewport'
            QtTest.QTest.mousePress(widget,QtCore.Qt.MouseButton.LeftButton,pos=self.drag_origin)
            assert widget.property('transformDragging') is True,'Drag did not start: '+repr([message[:1200] for message in self.messages[-5:]])
            assert widget.property('transformActiveAxis')==11,'Expected free-translation center handle'
        elif phase.startswith('drag_step_'):
            assert widget.property('transformDragging') is True
            index=int(phase.rsplit('_',1)[1]);self.drag_end=self.drag_origin+QtCore.QPoint(12*index,-4*index)
            self.mouse_move(self.drag_end,QtCore.Qt.MouseButton.LeftButton)
        elif phase=='drag_release':
            QtTest.QTest.mouseRelease(widget,QtCore.Qt.MouseButton.LeftButton,pos=self.drag_end)
        elif phase in ('undo_drag','restore_drag'):stageviz.command.undo()
        elif phase=='redo_drag':stageviz.command.redo()
        elif phase=='reload_baseline':
            assert self.restored(),'Restore edits before reload'
            self.mode('Select');stageviz.command.select_paths([])
            # Reload the original file without clearing the anonymous authoring
            # root that holds the temporary assembly's sublayer list.
            Sdf.Layer.FindOrOpen(self.config['layers'][0]).Reload(True)
            if stage.GetRootLayer().ExportToString()!=self.root_before:
                # Rebuild the disposable assembly to remove inert over-specs
                # retained by existing transform undo behavior.
                session.close();assert session.newStage()
                stageviz.command.add_sublayer(self.config['layers'][0])
            stageviz.command.clear()
        else:raise AssertionError(phase)
        self.dispatch_end_ms=time.perf_counter()*1000;self.dispatched=True
        widget.update()

    def session_ready(self):
        if self.current in ('drag_press','drag_step_1','drag_step_2'):
            return stageviz.session().primsUpdate()==stageviz.PrimsDeferred
        return super().session_ready()

    def expected(self):
        phase=self.current;paths=list(stageviz.session().paths())
        if phase in ('empty','add_front'):return super().expected()
        if phase=='seed_selection':return paths==self.seed
        if phase=='existing_xform_selection':return paths==[self.control_target]
        if phase.startswith('sweep_'):return bool(paths)
        if phase=='clear_selection':return not paths
        if phase=='invert_selection':
            if self.original_config.get('cpuOnly'):
                return set(paths)==self.invert_expected
            return paths!=self.pre_invert
        if phase=='undo_invert':return paths==self.pre_invert
        if phase=='move_gizmo':return self.original_config.get('cpuTransform') or self.viewport().property('transformPivotValid') is True
        if phase=='cpu_gizmo_prepare':return self.viewport().property('transformPivotValid') is True
        if phase in ('drag_press','drag_step_1','drag_step_2'):return self.viewport().property('transformDragging') is True
        if phase=='drag_release':return self.viewport().property('transformDragging') is False and stageviz.command.can_undo()
        if phase in ('undo_drag','restore_drag','reload_baseline'):return self.restored()
        if phase=='redo_drag':return not self.restored()
        return True

    def finish(self):
        # Base captures the elapsed span first. Additional assertions and evidence
        # are outside it; no reentrant MCP calls or Python USD notice callbacks.
        super().finish()
        row=self.results[-1];phase=self.current;session=stageviz.session()
        paths=list(session.paths());widget=self.viewport()
        if phase=='add_front':
            context=widget.context()
            (self.directory/'gl-context.json').write_text(json.dumps({'valid':widget.isValid(),'context':str(context),
                'version':[context.format().majorVersion(),context.format().minorVersion()] if context else None}))
            if not (self.original_config.get('cpuOnly') or self.original_config.get('cpuTransform')):
                assert widget.isValid() and widget.property('renderConverged') is not None,'A valid OpenGL viewport is required for sweep/render timings'
            console=[]
            for obj in QtWidgets.QApplication.allWidgets():
                if isinstance(obj,(QtWidgets.QPlainTextEdit,QtWidgets.QTextEdit)):
                    console.append({'name':obj.objectName(),'text':obj.toPlainText()[-20000:]})
            (self.directory/'console-widgets.json').write_text(json.dumps(console,indent=2))
            self.mode('Select')
            if session.stage().GetEditTarget().GetLayer()!=session.stage().GetRootLayer():
                stageviz.command.set_edit_layer(session.stage().GetRootLayer().identifier)
            assert session.stage().GetEditTarget().GetLayer()==session.stage().GetRootLayer(),'Only author in the disposable root'
            self.root_before=session.stage().GetRootLayer().ExportToString()
            self.root_signature=self.signature(session.stage().GetRootLayer())
            if self.original_config.get('cpuOnly') or self.original_config.get('cpuTransform'):
                domain=[str(prim.GetPath()) for prim in session.stage().Traverse() if not prim.IsA(UsdGeom.Subset)
                        and not any(not child.IsA(UsdGeom.Subset) for child in prim.GetChildren())]
                if self.original_config.get('cpuTransform'):
                    domain=[str(prim.GetPath()) for prim in session.stage().Traverse() if prim.IsA(UsdGeom.Boundable)
                            and UsdGeom.Imageable(prim).ComputeVisibility()!='invisible']
                self.seed=random.Random(20261104).sample(domain,min(self.original_config.get('seedCount',1000),len(domain)))
                self.invert_expected=set(domain)-set(self.seed)
                (self.directory/'selection-domain.json').write_text(json.dumps({'domainCount':len(domain),'seed':self.seed}))
            # Populate one Outliner branch for realistic selection presentation.
            for tree in QtWidgets.QApplication.allWidgets():
                if isinstance(tree,QtWidgets.QTreeWidget) and tree.objectName()=='outlinerTree':
                    for depth in range(1,4):
                        index=QtCore.QModelIndex()
                        for _ in range(depth):index=tree.model().index(0,0,index)
                        if index.isValid():tree.expand(index)
        if phase=='sweep_cold':self.saved_sweep=paths
        if phase=='sweep_warm':assert paths==self.saved_sweep,'Warm sweep differs from cold sweep'
        if phase=='move_gizmo':
            sample=paths[::max(1,len(paths)//64)][:64]
            self.world_before=self.worlds(sample)
            assert session.stage().GetRootLayer().ExportToString()==self.root_before
        if phase in ('drag_step_1','drag_step_2','drag_release','redo_drag'):
            moved=self.worlds(self.world_before)
            assert any(not Gf.IsClose(value,self.world_before[path],1e-7) for path,value in moved.items()),'Drag did not move sampled geometry'
        row.update(selection_count=len(paths),sweep_pixels=getattr(self,'sweep_pixels',None),
                   status_messages=[message[:1200] for message in self.messages[-5:]],
                   edit_target=session.stage().GetEditTarget().GetLayer().identifier,
                   transform_dragging=widget.property('transformDragging'),
                   transform_target_count=widget.property('transformTargetCount'),
                   transform_active_axis=widget.property('transformActiveAxis'),
                   root_restored=session.stage().GetRootLayer().ExportToString()==self.root_before if self.root_before is not None else None,
                   authored_values_restored=self.restored() if self.root_before is not None else None)
        if phase=='reload_baseline':
            assert not stageviz.command.can_undo() and not paths
            assert row['root_restored'],'Cleanup must restore the complete anonymous-root text'
        for tree in QtWidgets.QApplication.allWidgets():
            if isinstance(tree,QtWidgets.QTreeWidget) and tree.objectName()=='outlinerTree':
                state=self.outliner_snapshot(tree)
                state.update(selectionCount=len(paths),selectionSample=paths[:32],
                             selectionSHA256=hashlib.sha256('\n'.join(paths).encode()).hexdigest())
                (self.directory/(phase+'-selection-qt.json')).write_text(json.dumps(state,indent=2))
                break
        (self.directory/'results.json').write_text(json.dumps(self.results,indent=2))
        self.write_status('checkpoint',result=row)

config=json.loads(Path(os.environ['STAGEVIZ_STORM_PROFILE']).read_text())
stageviz._selectionBenchmark=SelectionBenchmark(config)
print('Selection/transform benchmark armed; external controller acknowledges checkpoints')
