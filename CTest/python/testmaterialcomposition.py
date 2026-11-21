# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""Layer composition, material caches, inert opinions and switching latency."""
from pathlib import Path
import json
import runpy
import time

import stageviz
from pxr import Sdf, Usd, UsdGeom, UsdShade

_base = runpy.run_path(str(Path(__file__).with_name('testtooltips.py')))
tb = _base['tb']


class MaterialCompositionTest(_base['TooltipsTest']):
    path = '/Materials/Material1'

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.layers = [self.directory / 'materialx.usda', self.directory / 'preview.usda']
        for filename, node, shader_id, context, color in (
                (self.layers[0], 'StandardSurface', 'ND_standard_surface_surfaceshader', 'mtlx', (.5,.5,.5)),
                (self.layers[1], 'PreviewSurface', 'UsdPreviewSurface', '', (.978,.547,.121))):
            stage = Usd.Stage.CreateNew(str(filename))
            UsdGeom.Scope.Define(stage, '/Materials')
            material = UsdShade.Material.Define(stage, self.path)
            shader = UsdShade.Shader.Define(stage, self.path + '/' + node)
            shader.CreateIdAttr(shader_id)
            shader.CreateInput('base_color' if context else 'diffuseColor', Sdf.ValueTypeNames.Color3f).Set(color)
            material.CreateSurfaceOutput(context).ConnectToSource(
                shader.CreateOutput('out' if context else 'surface', Sdf.ValueTypeNames.Token))
            stage.GetRootLayer().Save()
        self.fixture = self.directory / 'root.usda'
        root = Sdf.Layer.CreateNew(str(self.fixture))
        root.subLayerPaths = [str(self.layers[1]), str(self.layers[0])]
        stage = Usd.Stage.Open(root)
        UsdGeom.Xform.Define(stage, '/World')
        Sdf.CreatePrimInLayer(root, self.path)
        root.Save()
        del root, stage, material, shader
        self.references = {}
        self.failures = []
        self.timings = []
        self.checkpoints = []
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()

    def widgets(self):
        _, _, _, QtWidgets = tb.qt()
        if not hasattr(self, 'dialog'):
            self.dialog = next(w for w in tb.all_widgets() if w.objectName() == 'materialDialog' and w.isVisible())
            self.icons = self.dialog.findChild(QtWidgets.QListWidget, 'icons')
        return self.dialog, self.icons

    def image(self):
        QtCore, _, _, _ = tb.qt()
        _, icons = self.widgets()
        if icons.count() != 1:
            return None
        image = icons.item(0).data(QtCore.Qt.ItemDataRole.UserRole + 1)
        if image is None or image.isNull() or image.pixelColor(image.width()//2, image.height()//2).alpha() < 200:
            return None
        return image

    def samples(self):
        image = self.image()
        assert image is not None
        return [image.pixelColor(int(image.width()*x), int(image.height()*y)).getRgb()[:3]
                for x,y in ((.5,.5),(.4,.4),(.6,.4),(.4,.6),(.6,.6),(.5,.3))]

    def switch(self, state, label):
        QtCore, _, _, _ = tb.qt()
        gaps = []
        previous = [time.monotonic()]
        def tick():
            now = time.monotonic()
            gaps.append(now-previous[0])
            previous[0] = now
        timer = QtCore.QTimer()
        timer.timeout.connect(tick)
        timer.start(10)
        started = time.monotonic()
        try:
            for layer, muted in zip(self.layers, state):
                stageviz.command.set_layer_muted(str(layer), muted)
                assert tb.wait_for_commands()
            assert tb.wait_until(lambda: self.widgets()[1].count() == (0 if all(state) else 1), timeout=10), (state, self.widgets()[1].count(), [str(p.GetPath()) for p in stageviz.session().stage().Traverse()])
            if not all(state):
                assert tb.wait_until(lambda: self.image() is not None, timeout=15)
            tb.process_events(700)
        finally:
            timer.stop()
        elapsed = (time.monotonic()-started)*1000
        self.timings.append({'label':label,'elapsed_ms':elapsed,'max_event_gap_ms':max(gaps,default=0)*1000})
        stage = stageviz.session().stage()
        material = stage.GetPrimAtPath(self.path)
        expected = []
        if not state[0]: expected.append('outputs:mtlx:surface')
        if not state[1]: expected.append('outputs:surface')
        actual = [a.GetName() for a in material.GetAttributes() if a.GetConnections()] if material else []
        assert sorted(actual) == sorted(expected), (actual,expected)
        print('COMPOSITION_CHECKPOINT',json.dumps({'path':self.path,'present':not all(state), 'outputs':expected}))
        if not all(state):
            dialog, icons = self.widgets()
            icons.setCurrentRow(0)
            tb.process_events(100)
            # Inspect the rendered large preview, not only the browser's cached image.
            _, _, _, QtWidgets = tb.qt()
            tree = dialog.findChild(QtWidgets.QTreeWidget, 'tree')
            tree.expandAll()
            assert tree.topLevelItemCount() > 0, ('material properties vanished', state)
            it = QtWidgets.QTreeWidgetItemIterator(tree)
            authored_inputs = []
            while it.value():
                input_path = it.value().data(0, QtCore.Qt.ItemDataRole.UserRole)
                if isinstance(input_path, str) and '.inputs:' in input_path:
                    authored_inputs.append(input_path)
                    assert stage.GetPrimAtPath(Sdf.Path(input_path).GetPrimPath()), input_path
                it += 1
            assert authored_inputs, ('missing composed shader inputs',state)
            preview = dialog.findChild(QtWidgets.QWidget, 'swatch').grab().toImage()
            source = self.image()
            a = preview.pixelColor(preview.width()//2, preview.height()//2).getRgb()[:3]
            b = source.pixelColor(source.width()//2, source.height()//2).getRgb()[:3]
            assert max(abs(x-y) for x,y in zip(a,b)) <= 12, ('large preview differs',a,b)
            dialog.grab().save(str(self.directory/(label+'-materials.png')))
        self.snapshot(label)
        return self.samples() if not all(state) else None

    def references_phase(self):
        assert tb.trigger_action('materialEditor')
        assert tb.wait_until(lambda: any(w.objectName()=='materialDialog' and w.isVisible() for w in tb.all_widgets()))
        self.widgets()
        for state in ((False,False),(True,False),(False,True),(True,True)):
            # Flush browser rows as well as renderer caches: these are independent cold references.
            assert stageviz.session().newStage() and tb.wait_for_commands()
            assert tb.wait_until(lambda: self.widgets()[1].count() == 0)
            self.dialog.hide()
            assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()
            for layer, muted in zip(self.layers, state):
                stageviz.command.set_layer_muted(str(layer), muted)
                assert tb.wait_for_commands()
            self.dialog.show()
            key = str(state)
            self.references[key] = self.switch(state, 'reference-'+str(len(self.references)))
        print('REFERENCE_SAMPLES',json.dumps(self.references))

    def switching(self):
        for index, state in enumerate(((False,True),(False,False),(True,False),(False,False),
                                       (True,True),(False,True),(False,False),(True,False))):
            actual = self.switch(state, 'switch-'+str(index))
            expected = self.references[str(state)]
            if actual is not None:
                difference = max(abs(a-b) for pixel,ref in zip(actual,expected) for a,b in zip(pixel,ref))
                if difference > 8:
                    self.failures.append({'state':state,'difference':difference,'actual':actual,'expected':expected})
        (self.directory/'composition-results.json').write_text(json.dumps({'timings':self.timings,'failures':self.failures},indent=2))
        print('COMPOSITION_RESULTS',json.dumps({'timings':self.timings,'failures':self.failures}))
        assert not self.failures, self.failures

    def reload_composition(self):
        self.switch((False,False), 'before-document-switch')
        alternate = self.directory / 'preview-only.usda'
        layer = Sdf.Layer.CreateNew(str(alternate))
        layer.subLayerPaths = [str(self.layers[1])]
        layer.Save()
        del layer
        assert stageviz.session().load(str(alternate)) and tb.wait_for_commands()
        assert tb.wait_until(lambda: self.image() is not None, timeout=15)
        tb.process_events(700)
        actual = self.samples()
        expected = self.references[str((True,False))]
        assert max(abs(a-b) for p,r in zip(actual,expected) for a,b in zip(p,r)) <= 8, (actual,expected)
        self.snapshot('document-network-reloaded')
        assert stageviz.session().load(str(self.fixture)) and tb.wait_for_commands()

    def opinions(self):
        stage = stageviz.session().stage()
        root = stage.GetRootLayer()
        before = root.ExportToString()
        stageviz.command.clear_opinions(['/Materials'])
        assert tb.wait_for_commands()
        assert root.GetPrimAtPath('/Materials') is None, root.ExportToString()
        assert stage.GetPrimAtPath(self.path).IsDefined()
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert root.ExportToString() == before
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert root.GetPrimAtPath('/Materials') is None
        self.snapshot('empty-opinions-cleared')
