# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Reload selected layers through the Layer Editor, retaining undo and state."""
from pathlib import Path
import json
import runpy

import stageviz
from pxr import Sdf

_base = runpy.run_path(str(Path(__file__).with_name('testlayermultiselection.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class LayerUpdateTest(_base['LayerMultiSelectionTest']):
    def checkpoint(self):
        session = stageviz.session()
        print('LAYER_UPDATE_CHECKPOINT ' + json.dumps({
            'filename': session.filename(), 'selection': list(session.paths()),
            'present': [str(prim.GetPath()) for prim in session.stage().Traverse()]}))

    def update(self):
        stage = stageviz.session().stage()
        stageviz.command.set_edit_layer(str(self.first))
        tb.wait_for_commands()
        stageviz.command.new_prim('/', 'MemoryOnly', type_name='Cube')
        tb.wait_for_commands()
        layer = Sdf.Layer.Find(str(self.first))
        before = layer.ExportToString()
        self.first.write_text('#usda 1.0\n(subLayers = [@child.usda@])\ndef Sphere "FirstUpdated" {}\n')
        self.child.write_text('#usda 1.0\ndef Sphere "ChildExternal" {}\n')
        stageviz.command.clear()
        camera = stageviz.session().viewState().camera()
        camera_state = (camera.focusPoint(), camera.cameraDistance(), camera.axisRoll(), camera.axisPitch())
        self.click(self.first)
        observed = self.menu('layerUpdate')
        names = list(observed)
        assert names[names.index('layerSave') + 1] == 'layerUpdate'
        assert observed['layerUpdate']['text'] == 'Update layer'
        assert stage.GetPrimAtPath('/FirstUpdated') and not stage.GetPrimAtPath('/MemoryOnly')
        assert stage.GetPrimAtPath('/Child') and not stage.GetPrimAtPath('/ChildExternal'), 'unselected child is not reloaded'
        assert camera_state == (camera.focusPoint(), camera.cameraDistance(), camera.axisRoll(), camera.axisPitch())
        updated = layer.ExportToString()
        self.first.write_text('#usda 1.0\ndef Sphere "LaterDiskChange" {}\n')
        stageviz.command.undo()
        tb.wait_for_commands()
        assert layer.ExportToString() == before
        stageviz.command.redo()
        tb.wait_for_commands()
        assert layer.ExportToString() == updated and not stage.GetPrimAtPath('/LaterDiskChange')
        stageviz.command.undo()
        tb.wait_for_commands()
        stageviz.command.set_edit_layer(str(self.fixture))
        tb.wait_for_commands()
        stageviz.command.clear()
        self.snapshot('layer_updated_single')

    def batch(self):
        core = tb.qt()[0]
        stage = stageviz.session().stage()
        layers = [Sdf.Layer.Find(str(path)) for path in (self.first, self.second)]
        before = [layer.ExportToString() for layer in layers]
        self.first.write_text('#usda 1.0\n(subLayers = [@child.usda@])\ndef Sphere "FirstUpdated" {}\n')
        self.second.write_text('#usda 1.0\ndef Cube "SecondUpdated" {}\n')
        self.click(self.first)
        self.click(self.second, core.Qt.KeyboardModifier.ControlModifier)
        observed = self.menu('layerUpdate')
        assert observed['layerUpdate']['text'] == 'Update layers'
        assert stage.GetPrimAtPath('/FirstUpdated') and stage.GetPrimAtPath('/SecondUpdated')
        assert stage.GetPrimAtPath('/Child') and not stage.GetPrimAtPath('/ChildExternal')
        stageviz.command.undo()
        tb.wait_for_commands()
        assert [layer.ExportToString() for layer in layers] == before, 'one undo restores both layers'
        assert not stageviz.command.can_undo()
        stageviz.command.redo()
        tb.wait_for_commands()
        assert stage.GetPrimAtPath('/FirstUpdated') and stage.GetPrimAtPath('/SecondUpdated')
        stageviz.command.undo()
        tb.wait_for_commands()
        stageviz.command.clear()
        stageviz.command.update_layer([str(self.first), str(self.second), str(self.first)])
        tb.wait_for_commands()
        assert stage.GetPrimAtPath('/FirstUpdated') and stage.GetPrimAtPath('/SecondUpdated')
        stageviz.command.undo()
        tb.wait_for_commands()
        assert [layer.ExportToString() for layer in layers] == before
        self.snapshot('layer_updated_batch')

    def availability(self):
        self.click(self.first)
        stageviz.command.set_layer_permission_to_edit(str(self.first), False)
        tb.wait_for_commands()
        assert not self.menu()['layerUpdate']['enabled']
        stageviz.command.undo()
        tb.wait_for_commands()
        assert self.menu()['layerUpdate']['enabled']
        stageviz.command.set_layer_muted(str(self.first), True)
        tb.wait_for_commands()
        assert not self.menu()['layerUpdate']['enabled']
        stageviz.command.undo()
        tb.wait_for_commands()
        assert stageviz.session().newStage()
        tb.process_events(50)
        root_id = stageviz.session().stage().GetRootLayer().identifier
        assert not self.menu(path=root_id)['layerUpdate']['enabled'], 'anonymous root cannot be reloaded'
        self.snapshot('layer_update_unavailable')


def run(disposable=False):
    test = LayerUpdateTest(disposable=disposable)
    try:
        test.setup()
        test.update()
        test.batch()
        test.availability()
    finally:
        test.cleanup()


if globals().get('_VIEWPORT_AUTORUN', True):
    run(disposable=globals().get('_VIEWPORT_DISPOSABLE', False))
