# SPDX-License-Identifier: BSD-3-Clause
"""Large real-input Shift-sweep checks with an independent ordered-set oracle."""
from pathlib import Path
import runpy
import time
import stageviz
from PySide6 import QtCore,QtGui,QtTest,QtWidgets

base=runpy.run_path(str(Path(__file__).with_name('namespace_workflow.py')),
                   init_globals={'_PIPELINE_AUTORUN':False})


class ModifierWorkflow(base['NamespaceWorkflow']):
    def __init__(self,config):
        self.saved={'empty':[]}
        super().__init__(config)

    def start(self):
        step=self.steps[self.phases[len(self.results)]]
        if step['action']!='sweep':
            super().start()
            if 'expect' in step:self.expected_paths=self.saved[step['expect']]
            return
        step,session,widget=self.begin()
        if 'expect' in step:self.expected_paths=self.saved[step['expect']]
        rectangle=step['rectangle']
        a=QtCore.QPoint(round(widget.width()*rectangle[0]),round(widget.height()*rectangle[1]))
        b=QtCore.QPoint(round(widget.width()*rectangle[2]),round(widget.height()*rectangle[3]))
        modifier=QtCore.Qt.KeyboardModifier.ShiftModifier if step.get('shift') else QtCore.Qt.KeyboardModifier.NoModifier
        QtTest.QTest.mousePress(widget,QtCore.Qt.MouseButton.LeftButton,modifier,pos=a)
        local=QtCore.QPointF(b)
        event=QtGui.QMouseEvent(QtCore.QEvent.Type.MouseMove,local,QtCore.QPointF(widget.mapToGlobal(b)),
                               QtCore.Qt.MouseButton.NoButton,QtCore.Qt.MouseButton.LeftButton,modifier)
        QtWidgets.QApplication.sendEvent(widget,event)
        QtTest.QTest.mouseRelease(widget,QtCore.Qt.MouseButton.LeftButton,modifier,pos=b)
        self.dispatch_end=time.perf_counter()*1000;self.dispatched=True;widget.update()

    def selection_checkpoint(self,step,selected):
        if 'expect' in step:
            assert selected==self.saved[step['expect']],('ordered modifier selection',self.current)
        if 'save' not in step:return
        label=step['save'];self.saved[label]=selected
        assert len(selected)>16,'exercise the large-selection hash path'
        assert len(selected)==len(set(selected)) and set(selected).issubset(self.meshes)
        if label=='full':assert len(selected)>100000,'full viewport sweep exercises a large mesh set'
        if label in ('b','full'):
            a=self.saved['a'];a_set=set(a);hit_set=set(selected)
            self.saved['a_x_'+label]=[p for p in a if p not in hit_set]+[p for p in selected if p not in a_set]


if globals().get('_PIPELINE_AUTORUN',True):
    import json,os
    config=json.loads(Path(os.environ['STAGEVIZ_PIPELINE_PROFILE']).read_text())
    stageviz._modifierWorkflow=ModifierWorkflow(config)
