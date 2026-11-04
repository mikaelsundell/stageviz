# SPDX-License-Identifier: BSD-3-Clause
"""Asynchronous two-layer benchmark. Start once via external MCP; never call MCP here."""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import time
import traceback

import stageviz
from pxr import Sdf, Trace, UsdGeom
from PySide6 import QtCore, QtWidgets, QtOpenGLWidgets

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'python'))
import testbench as tb


class StormLayerBenchmark:
    def __init__(self, config):
        self.config = config
        self.directory = Path(config['output'])
        self.results = []
        self.current = None
        self.waiting = None
        self.front_roots = []
        self.frames = []
        self.dispatched = False
        self.active = False
        self.phases = [('empty','empty'),('add_front','add'),('add_axle','add')]
        for index in range(1,3):
            self.phases += [(f'mute_front_{index}','mute'),(f'unmute_front_{index}','unmute')]
        self.phases += [('mute_axle_1','mute'),('unmute_axle_1','unmute'),
                        ('hide_front_roots','hide'),('undo_visibility','undo'),
                        ('mute_front_no_render','mute'),('unmute_front_no_render','unmute'),
                        ('render_after_unmute','render')]
        if config.get('smoke'):self.phases=[('empty','empty')]
        elif config.get('short'):self.phases=self.phases[:5]
        if config.get('phases'):
            available=dict(self.phases)
            available.update(settle_after_mute='settle',settle_after_unmute='settle')
            self.phases=[(name,available[name]) for name in config['phases']]
        self.timer = QtCore.QTimer()
        self.timer.setInterval(50)
        self.timer.timeout.connect(self.poll)
        self.timer.start()
        self.viewport().frameSwapped.connect(self.frame)
        view = stageviz.session().viewState()
        view.setSceneMaterialsEnabled(config.get('materials',True))
        view.setSceneStatsEnabled(config.get('sceneStats',True))
        view.setDoubleSidedMode(1)
        self.write_status('ready')

    def viewport(self):
        for widget in QtWidgets.QApplication.allWidgets():
            try:
                if widget.objectName()=='imagingGLWidget' and widget.isVisible():
                    return widget
            except RuntimeError:
                pass
        raise RuntimeError('Visible viewport missing')

    def layer_dialog(self):
        # Direct enumeration avoids transient parent wrappers from findChild
        # invalidating an unregistered native dialog wrapper in this PySide build.
        for widget in QtWidgets.QApplication.allWidgets():
            try:
                if widget.objectName()=='layerDialog':
                    widget.isVisible()
                    return widget
            except RuntimeError:pass
        return None

    def write_status(self, state, **extra):
        value={'state':state,'completed':len(self.results),'phase':self.current,**extra}
        target=self.directory/'status.json'
        # The external Windows reader may hold a handle without delete sharing.
        # Update in place; the controller already retries incomplete JSON reads.
        target.write_text(json.dumps(value,indent=2))

    def frame(self):
        if self.active and self.dispatched:
            self.frames.append(time.perf_counter()*1000)

    def start(self):
        self.current,self.kind=self.phases[len(self.results)]
        self.frames=[]
        self.dispatched=False
        self.prims_ready_ms=None
        self.framing_ms=0
        self.active=True
        self.start_ms=time.perf_counter()*1000
        self.start_wall_ms=time.time()*1000
        self.no_render='no_render' in self.current
        self.write_status('running',start_ms=self.start_ms,start_wall_ms=self.start_wall_ms)
        self.tracing=self.current==self.config.get('traceOperation')
        if self.tracing:
            Trace.Collector().Clear()
            Trace.Reporter.globalReporter.ClearTree()
            Trace.Collector().enabled=True
        session=stageviz.session()
        if self.no_render:
            self.viewport().setUpdatesEnabled(False)
        else:
            self.viewport().setUpdatesEnabled(True)
        if self.kind=='empty':
            assert session.newStage()
            dialog=self.layer_dialog()
            if dialog:dialog.setVisible(self.config.get('layerEditor',True))
        elif self.kind=='add':
            index=0 if self.current=='add_front' else 1
            stageviz.command.add_sublayer(self.config['layers'][index])
        elif self.kind in ('mute','unmute'):
            index=1 if 'axle' in self.current else 0
            stageviz.command.set_layer_muted(self.config['layers'][index],self.kind=='mute')
        elif self.kind=='hide':
            self.before_visibility={path:str(UsdGeom.Imageable(session.stage().GetPrimAtPath(path)).ComputeVisibility(0)) for path in self.front_roots}
            if self.front_roots:stageviz.command.hide_paths(self.front_roots)
        elif self.kind=='undo':
            if self.front_roots:stageviz.command.undo()
        self.dispatch_end_ms=time.perf_counter()*1000
        self.dispatched=True
        self.framed=False
        if not self.no_render:
            self.viewport().update()

    def expected(self):
        stage=stageviz.session().stage()
        if self.kind=='empty':return len(stage.GetRootLayer().subLayerPaths)==0
        if self.kind=='add':
            index=0 if self.current=='add_front' else 1
            return any(Path(layer.realPath)==Path(self.config['layers'][index]) for layer in stage.GetLayerStack())
        if self.kind in ('mute','unmute'):
            index=1 if 'axle' in self.current else 0
            return stage.IsLayerMuted(self.config['layers'][index])==(self.kind=='mute')
        if self.kind=='hide':
            return all(UsdGeom.Imageable(stage.GetPrimAtPath(path)).ComputeVisibility(0)=='invisible' for path in self.front_roots)
        if self.kind=='undo':
            return all(str(UsdGeom.Imageable(stage.GetPrimAtPath(path)).ComputeVisibility(0))==value for path,value in self.before_visibility.items())
        return True

    def safe_start(self):
        try:self.start()
        except Exception:
            self.timer.stop()
            self.write_status('failed',error=traceback.format_exc())

    def session_ready(self):
        return stageviz.session().primsUpdate()==stageviz.PrimsImmediate

    def finish(self):
        end=time.perf_counter()*1000
        self.active=False
        if self.tracing:
            Trace.Collector().enabled=False
            Trace.Reporter.globalReporter.Report(str(self.directory/(self.current+'-usd-trace.txt')))
            if self.config.get('traceChrome',False):
                Trace.Reporter.globalReporter.ReportChromeTracingToFile(str(self.directory/(self.current+'-usd-trace.json')))
            Trace.Collector().Clear()
            Trace.Reporter.globalReporter.ClearTree()
        session=stageviz.session()
        stage=session.stage()
        assert session.viewState().sceneMaterialsEnabled()==self.config.get('materials',True)
        assert session.viewState().sceneStatsEnabled()==self.config.get('sceneStats',True)
        if self.current=='add_front':
            layer=Sdf.Layer.Find(self.config['layers'][0])
            self.front_roots=[str(spec.path) for spec in layer.rootPrims if UsdGeom.Imageable(stage.GetPrimAtPath(spec.path))]
            # Assemblies can share a top-level container. Prefer the front
            # assembly branch below a chain of single-child containers.
            roots=[]
            for path in self.front_roots:
                prim=stage.GetPrimAtPath(path)
                while len(prim.GetChildren())==1:
                    child=prim.GetChildren()[0]
                    if not UsdGeom.Imageable(child):break
                    prim=child
                roots.append(str(prim.GetPath()))
            self.front_roots=roots
        result={'phase':self.current,'kind':self.kind,'start_ms':self.start_ms,
                'start_wall_ms':self.start_wall_ms,'end_wall_ms':time.time()*1000,
                'dispatch_end_ms':self.dispatch_end_ms,'end_ms':end,
                'dispatch_ms':self.dispatch_end_ms-self.start_ms,
                'prims_ready_ms':self.prims_ready_ms-self.start_ms if self.prims_ready_ms else None,
                'explicit_framing_ms':self.framing_ms,
                'end_to_end_ms':end-self.start_ms,
                'first_present_after_dispatch_ms':self.frames[0]-self.dispatch_end_ms if self.frames else None,
                'frames_ms':[stamp-self.start_ms for stamp in self.frames],
                'viewport_updates':not self.no_render,'trace_enabled':self.tracing,
                'verified':self.expected(),'prims':sum(1 for _ in stage.Traverse()),
                'muted':list(stage.GetMutedLayers()),'front_roots':self.front_roots,
                'dirty_file_layers':[layer.identifier for layer in stage.GetUsedLayers() if not layer.anonymous and layer.dirty],
                'layer_stack':[layer.identifier for layer in stage.GetLayerStack()],
                'materials_enabled':session.viewState().sceneMaterialsEnabled(),
                'scene_statistics_enabled':session.viewState().sceneStatsEnabled(),
                'render_converged':self.viewport().property('renderConverged'),
                'camera_light_enabled':session.viewState().defaultCameraLightEnabled(),
                'dome_light_enabled':session.viewState().defaultDomeLightEnabled(),
                'selection':list(session.paths())}
        assert not result['dirty_file_layers'], result['dirty_file_layers']
        # Checkpoints are captured after the measured span; grabbing can repaint.
        if not self.no_render:
            assert tb.viewer().grab().save(str(self.directory/(self.current+'-viewer.png')))
            assert self.viewport().grab().save(str(self.directory/(self.current+'-viewport.png')))
        with contextlib.redirect_stdout(io.StringIO()):
            qt=tb.snapshot_qt_state(self.current,root=self.viewport())
        qt['topLevel']=[{'name':w.objectName(),'visible':w.isVisible(),'enabled':w.isEnabled()} for w in tb.top_level_widgets()]
        dialog=self.layer_dialog()
        if dialog and dialog.isVisible():
            dialog.grab().save(str(self.directory/(self.current+'-layers.png')))
            with contextlib.redirect_stdout(io.StringIO()):qt['layers']=tb.snapshot_qt_state(self.current,root=dialog)
        (self.directory/(self.current+'-qt.json')).write_text(json.dumps(qt,indent=2))
        self.results.append(result)
        (self.directory/'results.json').write_text(json.dumps(self.results,indent=2))
        self.waiting=self.current
        self.write_status('checkpoint',result=result)

    def poll(self):
        try:
            if self.waiting:
                ack=self.directory/'ack.txt'
                if ack.exists() and ack.read_text()==self.waiting:
                    self.waiting=None
                    if len(self.results)==len(self.phases):
                        self.timer.stop()
                        self.write_status('complete')
                    else:QtCore.QTimer.singleShot(0,self.safe_start)
                return
            if not self.active:
                if not self.results:QtCore.QTimer.singleShot(0,self.safe_start)
                return
            if not self.dispatched:return
            if (time.perf_counter()*1000-self.start_ms)>self.config.get('operationTimeoutSeconds',1800)*1000:
                raise TimeoutError(self.current)
            ready=self.session_ready() and self.expected()
            if self.kind=='settle':
                ready=ready and self.viewport().property('renderConverged') is True
            if ready and self.prims_ready_ms is None:self.prims_ready_ms=time.perf_counter()*1000
            if ready and not self.framed and self.kind=='add':
                framing_start=time.perf_counter()*1000
                stageviz.session().viewState().camera().frame(stageviz.session().boundingBox())
                self.framing_ms=time.perf_counter()*1000-framing_start
                self.frames=[]
                self.framed=True
            if ready and (self.no_render or len(self.frames)>=3):
                self.finish()
            elif not self.no_render:self.viewport().update()
        except Exception:
            self.timer.stop()
            self.write_status('failed',error=traceback.format_exc())


if globals().get('_STORM_AUTORUN',True):
    config=json.loads(Path(os.environ['STAGEVIZ_STORM_PROFILE']).read_text())
    stageviz._stormBenchmark=StormLayerBenchmark(config)
    print('Asynchronous Storm layer benchmark started; external controller acknowledges checkpoints')
