# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Root companion state, save prompts and round trips in a disposable viewer."""
from pathlib import Path
import json
import runpy
import stageviz
from pxr import Sdf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class PreserveStateTest(_base['ViewportTest']):
    def action(self, name):
        _, QtGui, _, _ = tb.qt()
        action = tb.viewer().findChild(QtGui.QAction, name)
        assert action is not None, name
        return action

    def settled(self):
        assert tb.wait_for_commands()
        tb.process_events(100)

    def setup(self):
        assert self.disposable
        session = stageviz.session()
        self.original_filename = session.filename()
        self.original_selection = list(session.paths())
        self.original_preserve = self.action('filePreserveState').isChecked()
        QtCore = tb.qt()[0]
        self.original_non_native_dialogs = QtCore.QCoreApplication.testAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs)
        self.fixture = self.directory / 'root.usda'
        self.sidecar = Path(str(self.fixture) + '.session')
        self.layer = self.directory / 'edit.usda'
        self.asset = self.directory / 'asset.usda'
        self.layer.write_text('#usda 1.0\ndef Xform "LayerPrim" { }\n')
        asset = Usd.Stage.CreateNew(str(self.asset))
        asset.SetDefaultPrim(UsdGeom.Xform.Define(asset, '/Asset').GetPrim())
        UsdGeom.Sphere.Define(asset, '/Asset/Sphere')
        assert asset.GetRootLayer().Save()
        root = Usd.Stage.CreateNew(str(self.fixture))
        root.GetRootLayer().subLayerPaths = [str(self.layer)]
        root.SetDefaultPrim(UsdGeom.Xform.Define(root, '/World').GetPrim())
        assert root.DefinePrim('/Payload', 'Xform').GetPayloads().AddPayload(str(self.asset))
        assert root.GetRootLayer().Save()
        del root, asset
        session.setPreserveState(True)
        assert session.load(str(self.fixture))
        self.settled()
        self.usd_before = {str(p): p.read_bytes() for p in (self.fixture, self.layer, self.asset)}
        assert self.action('fileStateSave').isVisible()
        assert self.action('fileStateSave').isEnabled(), 'can create a missing root companion'
        self.save()
        self.snapshot('state_baseline')

    def assert_usd_unchanged(self):
        assert all(Path(p).read_bytes() == data for p, data in self.usd_before.items())

    def save(self):
        self.settled()
        action = self.action('fileStateSave')
        assert action.isVisible() and action.isEnabled()
        action.trigger()
        self.settled()
        assert self.sidecar.is_file()
        assert not stageviz.session().isStateDirty()
        assert not self.action('fileStateSave').isEnabled()
        assert '*' not in tb.viewer().windowTitle()
        self.assert_usd_unchanged()

    def camera(self):
        session = stageviz.session()
        camera = session.viewState().camera()
        camera.setCameraDistance(camera.cameraDistance() * 1.2 + 1)
        self.settled()
        assert session.isStateDirty() and self.action('fileStateSave').isEnabled()
        assert '*' in tb.viewer().windowTitle()
        distance = camera.cameraDistance()
        self.save()
        assert abs(json.loads(self.sidecar.read_text())['viewCamera']['cameraDistance'] - distance) < 1e-6
        camera.setCameraDistance(distance + 10)
        assert session.reload()
        self.settled()
        assert abs(session.viewState().camera().cameraDistance() - distance) < 1e-6
        assert not session.isStateDirty()
        # View state belongs to root even with a sublayer as edit target.
        stageviz.command.set_edit_layer(str(self.layer))
        self.settled()
        camera.setAxisYaw(camera.axisYaw() + 13)
        self.settled()
        assert self.action('fileLayerSave').isEnabled()
        self.action('fileLayerSave').trigger()
        self.settled()
        assert not session.isStateDirty()
        self.assert_usd_unchanged()
        assert not Path(str(self.layer) + '.session').exists()
        assert json.loads(self.sidecar.read_text())['stageFile'] == str(self.fixture)
        self.snapshot('state_camera_sublayer')

    def views(self):
        session = stageviz.session()
        view = session.viewState()
        camera = view.camera()
        mutations = [
            (lambda: view.setGridEnabled(not view.gridEnabled()), lambda: view.gridEnabled()),
            (lambda: view.setBackgroundColor((0.17, 0.21, 0.25)), lambda: view.backgroundColor()),
            (lambda: view.setSceneLightsEnabled(not view.sceneLightsEnabled()), lambda: view.sceneLightsEnabled()),
            (lambda: view.setSceneMaterialsEnabled(not view.sceneMaterialsEnabled()), lambda: view.sceneMaterialsEnabled()),
            (lambda: view.setAmbientOcclusionEnabled(not view.ambientOcclusionEnabled()), lambda: view.ambientOcclusionEnabled()),
            (lambda: view.setAmbientOcclusionContactRadius(19), lambda: view.ambientOcclusionContactRadius()),
            (lambda: camera.setLetterboxEnabled(not camera.letterboxEnabled()), lambda: camera.letterboxEnabled()),
            (lambda: camera.setFov(51), lambda: camera.fov()),
        ]
        for change, read in mutations:
            change()
            self.settled()
            assert session.isStateDirty()
            expected = read()
            self.save()
            assert session.loadState(str(self.sidecar))
            self.settled()
            actual = read()
            if isinstance(expected, tuple):
                assert all(abs(a - b) <= 1 / 255 + 1e-6 for a, b in zip(actual, expected)), (expected, actual)
            else:
                assert actual == expected, (expected, actual)
            assert not session.isStateDirty()
        for mode in (stageviz.ProjectionOrthographic, stageviz.ProjectionPhysical, stageviz.ProjectionFieldOfView):
            camera.setProjectionMode(mode)
            camera.distance(1.3)
            self.save()
            expected = dict(json.loads(self.sidecar.read_text())['viewCamera'])
            camera.setFov(33)
            camera.distance(1.6)
            assert session.reload()
            self.settled()
            assert camera.projectionMode() == mode and not session.isStateDirty()
            check = self.directory / 'roundtrip.session'
            assert session.saveState(str(check))
            actual = json.loads(check.read_text())['viewCamera']
            for name in ('projectionMode', 'focusPoint', 'cameraDistance', 'orthographicHeight'):
                if name in expected:
                    if isinstance(expected[name], float):
                        assert abs(actual[name] - expected[name]) < 1e-4, (name, expected, actual)
                    else:
                        assert actual[name] == expected[name], (name, expected, actual)
        self.snapshot('state_views_roundtrip')

    def payloads(self):
        session = stageviz.session()
        stageviz.command.set_edit_layer(str(self.fixture))
        stageviz.command.unload_payloads(['/Payload'])
        self.settled()
        assert session.isStateDirty() and not session.stage().GetPrimAtPath('/Payload').IsLoaded()
        self.save()
        stageviz.command.undo()
        self.settled()
        assert session.isStateDirty() and session.stage().GetPrimAtPath('/Payload').IsLoaded()
        assert session.reload()
        self.settled()
        assert not session.stage().GetPrimAtPath('/Payload').IsLoaded() and not session.isStateDirty()
        stageviz.command.set_layer_muted(str(self.layer), True)
        self.settled()
        assert session.isStateDirty()
        self.save()
        assert session.reload()
        self.settled()
        assert session.stage().IsLayerMuted(str(self.layer)) and not session.isStateDirty()
        state = json.loads(self.sidecar.read_text())
        state['layerColors'] = {'$root': '#ffdf3f80', str(self.layer): '#ff5840d0'}
        self.sidecar.write_text(json.dumps(state))
        assert session.loadState(str(self.sidecar))
        self.settled()
        session.viewState().camera().setAxisPitch(23)
        self.save()
        assert json.loads(self.sidecar.read_text())['layerColors'] == state['layerColors']
        assert session.reload()
        self.settled()
        assert not session.isStateDirty()
        self.snapshot('state_payload_mute_colors')

    def layer_controls(self):
        session = stageviz.session()
        stageviz.command.set_layer_muted(str(self.layer), False)
        self.settled()
        self.save()
        usd_before = self.fixture.read_bytes(), self.layer.read_bytes()
        stageviz.command.set_edit_layer(str(self.layer))
        self.settled()
        assert session.isStateDirty()
        self.save()
        assert session.reload()
        self.settled()
        assert session.stage().GetEditTarget().GetLayer().identifier == str(self.layer)
        assert not session.isStateDirty()
        stageviz.command.set_edit_layer(str(self.fixture))
        self.settled()
        self.save()
        stageviz.command.set_layer_permission_to_edit(str(self.layer), False)
        self.settled()
        assert session.isStateDirty() and not Sdf.Layer.Find(str(self.layer)).permissionToEdit
        self.save()
        stageviz.command.undo()
        self.settled()
        assert session.isStateDirty() and Sdf.Layer.Find(str(self.layer)).permissionToEdit
        stageviz.command.redo()
        self.settled()
        assert not Sdf.Layer.Find(str(self.layer)).permissionToEdit
        stageviz.command.set_layer_muted(str(self.layer), True)
        self.settled()
        self.save()
        assert json.loads(self.sidecar.read_text())['lockedLayers'] == [str(self.layer)]
        assert session.reload()
        self.settled()
        assert not session.isStateDirty()
        assert session.stage().IsLayerMuted(str(self.layer))
        assert not Sdf.Layer.Find(str(self.layer)).permissionToEdit
        assert tb.trigger_action('fileLayers')
        self.settled()
        QtCore, _, _, QtWidgets = tb.qt()
        tree = next(w for w in QtWidgets.QApplication.allWidgets()
                    if w.objectName() == 'layerTree' and w.isVisible())
        model = tree.model()
        root = model.index(0, 0)
        role = QtCore.Qt.ItemDataRole
        assert model.data(model.index(0, 0, root), role.DisplayRole) == self.layer.name
        assert model.data(model.index(0, 1, root), role.ToolTipRole).startswith('Layer is muted.')
        assert model.data(model.index(0, 2, root), role.ToolTipRole).startswith('Layer is locked.')
        assert not model.data(model.index(0, 1, root), role.DecorationRole).isNull()
        assert not model.data(model.index(0, 2, root), role.DecorationRole).isNull()
        del model, root
        tree.expandAll()
        assert tree.grab().save(str(self.directory / 'state_layer_controls_tree.png'))
        self.snapshot('state_layer_controls')
        next(w for w in QtWidgets.QApplication.topLevelWidgets()
             if w.objectName() == 'layerDialog').hide()
        stageviz.command.set_layer_permission_to_edit(str(self.layer), True)
        self.settled()
        self.save()
        assert (self.fixture.read_bytes(), self.layer.read_bytes()) == usd_before

    def prompt(self, action_name, response, expect_state=True):
        QtCore, _, _, QtWidgets = tb.qt()
        seen, failures = [], []
        def answer():
            try:
                dialog = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                              if isinstance(w, QtWidgets.QDialog) and w.isVisible()
                              and w.objectName() == 'changesDialog')
                tree = dialog.findChild(QtWidgets.QTreeWidget, 'changesTree')
                rows = [[tree.topLevelItem(i).text(column) for column in range(tree.columnCount())]
                        for i in range(tree.topLevelItemCount())]
                assert [tree.headerItem().text(i) for i in range(5)] == ['Name', 'Type', 'Changes', 'Location', 'Status']
                assert any(row[1] == 'State' and row[3] == self.sidecar.name for row in rows), rows
                assert any(tree.topLevelItem(i).text(1) == 'State'
                           and tree.topLevelItem(i).toolTip(3) == str(self.sidecar)
                           for i in range(tree.topLevelItemCount()))
                assert (len(rows) == 1) == expect_state, rows
                seen.append(rows)
                assert dialog.grab().save(str(self.directory / ('state_prompt_' + response + '_dialog.png')))
                self.snapshot('state_prompt_' + response)
                if response == 'cancel':
                    dialog.reject()
                else:
                    dialog.findChild(QtWidgets.QPushButton, 'saveAllButton' if response == 'save' else 'discardButton').click()
            except Exception as error:
                failures.append(repr(error))
                for widget in QtWidgets.QApplication.topLevelWidgets():
                    if isinstance(widget, QtWidgets.QDialog) and widget.isVisible():
                        widget.reject()
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(answer)
        timer.start(75)
        self.action(action_name).trigger()
        timer.stop()
        assert seen and not failures, failures
        self.settled()

    def prompts(self):
        session = stageviz.session()
        session.viewState().camera().setAxisYaw(47)
        self.settled()
        self.prompt('fileNew', 'cancel')
        assert session.filename() == str(self.fixture) and session.isStateDirty()
        self.prompt('fileReload', 'cancel')
        assert session.filename() == str(self.fixture) and session.isStateDirty()
        self.prompt('fileClose', 'cancel')
        assert session.isLoaded() and session.isStateDirty()
        distance = session.viewState().camera().cameraDistance()
        self.prompt('fileNew', 'save')
        assert not session.filename()
        self.assert_usd_unchanged()
        assert session.load(str(self.fixture))
        self.settled()
        assert not session.isStateDirty()
        assert abs(session.viewState().camera().cameraDistance() - distance) < 1e-6
        before = self.sidecar.read_bytes()
        session.viewState().camera().setAxisYaw(54)
        self.settled()
        self.prompt('fileNew', 'discard')
        assert not session.filename() and self.sidecar.read_bytes() == before
        assert session.load(str(self.fixture))
        self.settled()
        # Mixed USD edits and state edits share the ordinary Save changes prompt.
        stageviz.command.new_prim('/World', 'SavedWithState', 'Xform')
        self.settled()
        session.viewState().camera().setAxisYaw(63)
        self.settled()
        self.prompt('fileNew', 'save', expect_state=False)
        assert session.load(str(self.fixture))
        self.settled()
        assert session.stage().GetPrimAtPath('/World/SavedWithState')
        assert abs(session.viewState().camera().axisYaw() - 63) < 1e-6
        self.usd_before[str(self.fixture)] = self.fixture.read_bytes()
        self.snapshot('state_prompt_saved')

    def disabled_and_failures(self):
        session = stageviz.session()
        baseline = self.sidecar.read_bytes()
        session.setPreserveState(False)
        session.viewState().camera().setAxisYaw(77)
        self.settled()
        assert self.action('fileStateSave').isVisible() and not self.action('fileStateSave').isEnabled()
        assert '*' not in tb.viewer().windowTitle()
        # No state-only question when preservation is disabled.
        self.action('fileNew').trigger()
        self.settled()
        assert not session.filename() and self.sidecar.read_bytes() == baseline
        assert session.load(str(self.fixture))
        self.settled()
        session.setPreserveState(True)
        session.viewState().camera().setAxisYaw(81)
        self.settled()
        self.sidecar.unlink()
        self.sidecar.mkdir()
        self.action('fileStateSave').trigger()
        self.settled()
        assert session.isStateDirty() and self.action('fileStateSave').isEnabled()
        self.prompt('fileNew', 'save')
        assert session.filename() == str(self.fixture), 'failed state save must stop document replacement'
        assert session.isStateDirty()
        self.sidecar.rmdir()
        self.sidecar.write_bytes(baseline)
        self.save()
        bad = self.directory / 'bad.session'
        bad.write_text('{ invalid json')
        camera = session.viewState().camera()
        before = camera.axisYaw()
        camera.setAxisYaw(before + 1)
        assert not session.loadState(str(bad)) and session.isStateDirty()
        assert camera.axisYaw() == before + 1
        self.save()
        self.snapshot('state_failure_recovered')

    def save_as_and_copy(self):
        session = stageviz.session()
        camera = session.viewState().camera()
        camera.setAxisYaw(92)
        copy = self.directory / 'copy.usda'
        assert session.copyToFile(str(copy)) and session.isStateDirty()
        assert json.loads(Path(str(copy) + '.session').read_text())['stageFile'] == str(copy)
        stageviz.command.set_layer_muted(str(self.layer), False)
        self.settled()
        stageviz.command.set_edit_layer(str(self.layer))
        self.settled()
        assert session.stage().GetEditTarget().GetLayer().identifier == str(self.layer)
        renamed = self.directory / 'renamed-root.usda'
        assert session.saveToFile(str(renamed))
        self.settled()
        assert not session.isStateDirty()
        assert json.loads(Path(str(renamed) + '.session').read_text())['stageFile'] == str(renamed)
        assert not Path(str(self.layer) + '.session').exists()
        assert session.reload()
        self.settled()
        assert abs(camera.axisYaw() - 92) < 1e-6
        self.snapshot('state_save_as')

    def pristine(self):
        session = stageviz.session()
        core, _, _, widgets = tb.qt()
        assert session.newStage()
        session.viewState().camera().setCameraDistance(12)
        session.viewState().camera().tumble(15, 8)
        self.settled()
        assert not session.isStateDirty() and not self.action('fileStateSave').isEnabled()
        assert '*' not in tb.viewer().windowTitle()
        reviewed = []
        def reject_unexpected():
            for dialog in widgets.QApplication.topLevelWidgets():
                if dialog.objectName() == 'changesDialog' and dialog.isVisible():
                    reviewed.append(True)
                    dialog.reject()
        core.QTimer.singleShot(0, reject_unexpected)
        self.action('fileNew').trigger()
        self.settled()
        assert not reviewed, 'New after camera-only changes must not ask to save'
        assert not session.isStateDirty()
        stageviz.command.new_prim('/World', 'Authored', type_name='Sphere')
        self.settled()
        session.viewState().camera().setCameraDistance(13)
        self.settled()
        assert session.isStateDirty() and self.action('fileStateSave').isEnabled()
        assert '*' in tb.viewer().windowTitle()
        self.snapshot('state_pristine_then_authored')
        assert session.load(str(self.fixture))
        self.settled()

    def anonymous(self):
        session = stageviz.session()
        QtCore, _, _, QtWidgets = tb.qt()
        # Exercise file-dialog Save/Cancel paths deterministically through Qt.
        # Production continues to use the platform-native picker.
        QtCore.QCoreApplication.setAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, True)
        assert session.newStage()
        stageviz.command.add_sublayer(str(self.layer))
        self.settled()
        stageviz.command.set_edit_layer(str(self.layer))
        self.settled()
        session.viewState().camera().setCameraDistance(7)
        self.settled()
        named = self.directory / 'anonymous-root.usda'
        def run_dialog(cancel):
            seen, failures = [], []
            attempts = [0]
            def respond():
                try:
                    dialogs = [w for w in QtWidgets.QApplication.allWidgets()
                               if isinstance(w, QtWidgets.QFileDialog) and w.isVisible()]
                    if not dialogs:
                        attempts[0] += 1
                        assert attempts[0] < 80, 'save dialog not found'
                        QtCore.QTimer.singleShot(50, respond)
                        return
                    dialog = dialogs[0]
                    seen.append(dialog.windowTitle())
                    if cancel:
                        dialog.reject()
                    else:
                        dialog.setDirectory(str(named.parent))
                        def choose_file():
                            try:
                                edit = dialog.findChild(QtWidgets.QLineEdit, 'fileNameEdit')
                                assert edit is not None
                                edit.setText(named.name)
                                assert dialog.selectedFiles() == [str(named)], dialog.selectedFiles()
                                dialog.accept()
                            except Exception as error:
                                failures.append(repr(error))
                                dialog.reject()
                        QtCore.QTimer.singleShot(50, choose_file)
                except Exception as error:
                    failures.append(repr(error))
                    for widget in QtWidgets.QApplication.allWidgets():
                        if isinstance(widget, QtWidgets.QFileDialog):
                            widget.reject()
            QtCore.QTimer.singleShot(50, respond)
            self.action('fileStateSave').trigger()
            self.settled()
            assert seen and not failures, failures
        run_dialog(True)
        assert not session.filename() and session.isStateDirty(), 'cancel keeps unnamed root state unsaved'
        run_dialog(False)
        assert session.filename() == str(named)
        assert not session.isStateDirty()
        companion = Path(str(named) + '.session')
        assert companion.is_file() and json.loads(companion.read_text())['stageFile'] == str(named)
        assert session.stage().GetEditTarget().GetLayer().identifier == str(self.layer)
        assert not Path(str(self.layer) + '.session').exists()
        self.snapshot('state_anonymous_named_root')

    def checkpoint(self):
        session = stageviz.session()
        stage = session.stage()
        payload = stage.GetPrimAtPath('/Payload') if stage else None
        print('STATE_CHECKPOINT ' + json.dumps({'filename': session.filename(),
            'selection': list(session.paths()), 'dirty': session.isStateDirty(),
            'payloadLoaded': payload.IsLoaded() if payload else None}))

    def cleanup(self):
        QtCore = tb.qt()[0]
        QtCore.QCoreApplication.setAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, self.original_non_native_dialogs)
        stageviz.session().setPreserveState(self.original_preserve)
        super().cleanup()
