# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Save summary rows, decisions and anonymous/read-only layer cases."""
from pathlib import Path
import json
import runpy
import stageviz
from pxr import Sdf

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class ChangesDialogTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        QtCore, QtGui, _, _ = tb.qt()
        self.original_preserve = tb.viewer().findChild(QtGui.QAction, 'filePreserveState').isChecked()
        self.original_dialog_backend = QtCore.QCoreApplication.testAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs)
        QtCore.QCoreApplication.setAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, True)
        stageviz.session().setPreserveState(True)
        self.first = self.directory / 'first.usda'
        self.second = self.directory / 'second.usda'
        self.named = self.directory / 'saved-root.usda'
        self.first.write_text('#usda 1.0\ndef Xform "First" { }\n')
        self.second.write_text('#usda 1.0\ndef Xform "Second" { }\n')
        self.disk = {str(p): p.read_bytes() for p in (self.first, self.second)}
        assert stageviz.session().newStage()
        for filename, parent in ((self.first, '/First'), (self.second, '/Second')):
            stageviz.command.add_sublayer(filename.as_posix())
            assert tb.wait_for_commands()
            stageviz.command.set_edit_layer(filename.as_posix())
            assert tb.wait_for_commands()
            assert stageviz.session().editLayer() == filename.as_posix()
            stageviz.command.new_prim(parent, 'Edited', 'Xform')
            assert tb.wait_for_commands()
            stageviz.command.set_edit_layer(stageviz.session().stage().GetRootLayer().identifier)
            assert tb.wait_for_commands()
        stageviz.session().viewState().camera().setCameraDistance(7)
        tb.process_events(100)
        self.snapshot('changes_ready')

    def review(self, response='cancel', root_filename=None, blocked_status=None,
               action_name='fileNew', confirmation=False):
        QtCore, QtGui, _, QtWidgets = tb.qt()
        snapshots, failures = [], []
        def inspect():
            try:
                for dialog in QtWidgets.QApplication.topLevelWidgets():
                    if not isinstance(dialog, QtWidgets.QDialog) or not dialog.isVisible():
                        continue
                    if dialog.objectName() == 'changesDialog':
                        if dialog.property('testReviewHandled'):
                            continue
                        dialog.setProperty('testReviewHandled', True)
                        tree = dialog.findChild(QtWidgets.QTreeWidget, 'changesTree')
                        headers = [tree.headerItem().text(i) for i in range(tree.columnCount())]
                        assert headers == ['Name', 'Type', 'Changes', 'Location', 'Status'], headers
                        items = []
                        iterator = QtWidgets.QTreeWidgetItemIterator(tree)
                        while iterator.value():
                            items.append(iterator.value())
                            iterator += 1
                        rows = [[item.text(c) for c in range(5)] for item in items]
                        self.hierarchy = {
                            item.data(0, QtCore.Qt.ItemDataRole.UserRole + 1):
                            item.parent().data(0, QtCore.Qt.ItemDataRole.UserRole + 1) if item.parent() else None
                            for item in items}
                        for item in items:
                            if item.text(1) == 'Sublayer':
                                assert item.parent() and item.parent().isExpanded(), 'sublayers follow expanded layer hierarchy'
                            if Path(item.toolTip(3)).is_absolute():
                                assert item.text(3) == Path(item.toolTip(3)).name
                        assert all(row[1] != 'State' for row in rows), rows
                        snapshots.append(rows)
                        save = dialog.findChild(QtWidgets.QPushButton, 'saveAllButton')
                        assert dialog.findChild(QtWidgets.QPushButton, 'discardButton').isVisible() != confirmation
                        if blocked_status:
                            assert any(row[4] == blocked_status for row in rows), rows
                            assert not save.isEnabled(), rows
                        else:
                            assert save.isEnabled(), rows
                        assert dialog.grab().save(str(self.directory / ('changes_' + response + '.png')))
                        self.snapshot('changes_' + response + '_viewer')
                        if response == 'save':
                            save.click()
                        elif response == 'discard':
                            dialog.findChild(QtWidgets.QPushButton, 'discardButton').click()
                        else:
                            dialog.findChild(QtWidgets.QPushButton, 'cancelButton').click()
                    elif isinstance(dialog, QtWidgets.QFileDialog):
                        assert root_filename is not None, 'unexpected filename dialog'
                        dialog.setDirectory(str(root_filename.parent))
                        edit = dialog.findChild(QtWidgets.QLineEdit, 'fileNameEdit')
                        assert edit is not None
                        edit.setText(root_filename.name)
                        assert [Path(p) for p in dialog.selectedFiles()] == [root_filename], dialog.selectedFiles()
                        dialog.accept()
            except Exception as error:
                failures.append(repr(error))
                for widget in QtWidgets.QApplication.topLevelWidgets():
                    if isinstance(widget, QtWidgets.QDialog) and widget.isVisible():
                        widget.reject()
        timer = QtCore.QTimer()
        timer.timeout.connect(inspect)
        timer.start(75)
        watchdog = QtCore.QTimer()
        watchdog.setSingleShot(True)
        def abort():
            failures.append('changes dialog watchdog expired')
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QDialog) and widget.isVisible():
                    widget.reject()
        watchdog.timeout.connect(abort)
        watchdog.start(15000)
        tb.viewer().findChild(QtGui.QAction, action_name).trigger()
        timer.stop()
        watchdog.stop()
        assert not failures and snapshots, failures
        assert tb.wait_for_commands()
        tb.process_events(100)
        return snapshots

    def ordinary_save_as(self):
        QtCore, QtGui, _, QtWidgets = tb.qt()
        seen, failures = [], []
        root = stageviz.session().stage().GetRootLayer().identifier
        def answer():
            for dialog in QtWidgets.QApplication.topLevelWidgets():
                if not isinstance(dialog, QtWidgets.QDialog) or not dialog.isVisible():
                    continue
                if dialog.objectName() == 'changesDialog':
                    failures.append('ordinary Save layer as must not open the Changes dialog')
                    dialog.reject()
                elif isinstance(dialog, QtWidgets.QFileDialog):
                    seen.append(True)
                    dialog.reject()
        timer = QtCore.QTimer()
        timer.timeout.connect(answer)
        timer.start(25)
        try:
            tb.viewer().findChild(QtGui.QAction, 'fileLayerSaveAs').trigger()
        finally:
            timer.stop()
        assert seen and not failures, failures
        assert stageviz.session().stage().GetRootLayer().identifier == root
        assert all(Path(p).read_bytes() == data for p, data in self.disk.items())

    def anonymous_save(self):
        root_id = stageviz.session().stage().GetRootLayer().identifier
        rows = self.review()[0]
        assert len(rows) == 3, rows
        assert rows[0][0:2] == ['Untitled', 'Root layer'] and rows[0][2].startswith('New layer'), rows
        assert rows[0][4] == 'Choose filename on save'
        assert {row[3] for row in rows if row[1] == 'Sublayer'} == {self.first.name, self.second.name}
        assert stageviz.session().stage().GetRootLayer().identifier == root_id
        assert all(Path(p).read_bytes() == text for p, text in self.disk.items())
        confirmation = self.review('cancel', root_filename=self.named,
                                   action_name='fileLayerSaveAll', confirmation=True)
        assert len(confirmation) == 1 and confirmation[0][0][3] == 'No filename'
        assert not self.named.exists(), 'canceling Save all review must not write files'
        assert stageviz.session().stage().GetRootLayer().identifier == root_id
        snapshots = self.review('save', root_filename=self.named)
        assert len(snapshots) == 1, 'reviewed changes must not cause a second Save As confirmation'
        assert not stageviz.session().filename()
        assert self.named.is_file() and Path(self.named.as_posix() + '.session').is_file()
        assert all(Path(p).read_bytes() != text for p, text in self.disk.items())
        assert stageviz.session().load(self.named.as_posix())
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath('/First/Edited')
        assert stageviz.session().stage().GetPrimAtPath('/Second/Edited')
        self.disk = {str(p): p.read_bytes() for p in (self.first, self.second, self.named)}
        self.snapshot('changes_saved')

    def nested(self):
        session = stageviz.session()
        parent = self.directory / 'parent.usda'
        child = self.directory / 'nested.usda'
        parent.write_text('#usda 1.0\n(subLayers = [@nested.usda@])\ndef Xform "Parent" {}\n')
        child.write_text('#usda 1.0\ndef Xform "Nested" {}\n')
        stageviz.command.add_sublayer(parent.as_posix())
        assert tb.wait_for_commands()
        stageviz.command.set_edit_layer(child.as_posix())
        assert tb.wait_for_commands()
        stageviz.command.new_prim('/Nested', 'Edited', 'Xform')
        assert tb.wait_for_commands()
        stageviz.command.set_edit_layer(session.stage().GetRootLayer().identifier)
        assert tb.wait_for_commands()
        parent_layer = Sdf.Layer.Find(parent.as_posix())
        parent_layer.SetPermissionToSave(False)
        rows = self.review()[0]
        assert self.hierarchy[child.as_posix()] == parent.as_posix()
        assert self.hierarchy[parent.as_posix()] == session.stage().GetRootLayer().identifier
        assert any(row[0] == parent.name and row[4] == 'Unchanged' for row in rows)
        parent_layer.SetPermissionToSave(True)
        assert session.load(self.named.as_posix())
        assert tb.wait_for_commands()

    def readonly(self):
        session = stageviz.session()
        stageviz.command.set_edit_layer(self.first.as_posix())
        assert tb.wait_for_commands()
        stageviz.command.new_prim('/First', 'Unsaved', 'Xform')
        assert tb.wait_for_commands()
        layer = Sdf.Layer.Find(self.first.as_posix())
        layer.SetPermissionToSave(False)
        self.review(blocked_status='Read-only')
        assert Path(session.filename()) == self.named
        assert all(Path(p).read_bytes() == text for p, text in self.disk.items())
        layer.SetPermissionToSave(True)
        session.setPreserveState(False)
        rows = self.review()[0]
        assert any(row[0] == self.first.name and row[2] == 'USD edits' for row in rows)
        assert rows[0][2] == 'No USD edits', rows
        self.review('discard')
        assert not session.filename()
        assert all(Path(p).read_bytes() == text for p, text in self.disk.items())
        session.setPreserveState(True)
        self.snapshot('changes_discarded')

    def unavailable(self):
        session = stageviz.session()
        assert session.newStage()
        anonymous = Sdf.Layer.CreateAnonymous('unsaved-child.usda')
        session.stage().GetRootLayer().subLayerPaths = [anonymous.identifier]
        tb.process_events(100)
        rows = self.review(blocked_status='Save layer as first')[0]
        assert any(row[1] == 'Sublayer' and row[3] == 'No filename' for row in rows)
        assert not session.filename()
        assert session.newStage()
        stage = session.stage()
        stage.SetEditTarget(stage.GetSessionLayer())
        stage.DefinePrim('/SessionOnly', 'Xform')
        stage.SetEditTarget(stage.GetRootLayer())
        tb.process_events(100)
        self.review(blocked_status='Not saved by this operation')
        assert stage.GetPrimAtPath('/SessionOnly'), 'Cancel must retain temporary session opinions'
        stage.GetSessionLayer().Clear()
        self.snapshot('changes_unavailable')

    def checkpoint(self):
        session = stageviz.session()
        stage = session.stage()
        present = [path for path in ('/First/Edited', '/Second/Edited') if stage and stage.GetPrimAtPath(path)]
        print('CHANGES_CHECKPOINT ' + json.dumps({'filename': session.filename(),
            'selection': list(session.paths()), 'present': present}))

    def cleanup(self):
        QtCore = tb.qt()[0]
        QtCore.QCoreApplication.setAttribute(QtCore.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, self.original_dialog_backend)
        stageviz.session().setPreserveState(self.original_preserve)
        super().cleanup()
