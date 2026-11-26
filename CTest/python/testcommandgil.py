# SPDX-License-Identifier: BSD-3-Clause
"""Exercise command/history dispatch with Python notices on USD worker threads."""
from pathlib import Path
import runpy
import threading
import time

import stageviz
from pxr import Tf, Usd

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class CommandGilTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        assert stageviz.session().newStage()
        # Retain the Python-owned stage throughout worker edits, as clients do.
        self.stage = stageviz.session().stage()
        assert self.stage.GetPrimAtPath('/World')
        self.notice_threads = []
        self.main_thread = threading.get_ident()
        self.notice_key = Tf.Notice.Register(Usd.Notice.ObjectsChanged, self.changed, self.stage)
        self.snapshot('command_gil_setup')

    def changed(self, notice, sender):
        self.notice_threads.append(threading.get_ident())
        if threading.get_ident() != self.main_thread:
            # USD holds the stage write lock while delivering notices. Sleeping
            # here releases/reacquires the GIL, widening the previous inversion
            # with native command-stack signals refreshing the Layer Tree.
            time.sleep(0.005)

    def roundtrip(self):
        for index in range(8):
            name = 'NoticePrim' + str(index)
            path = '/World/' + name
            stageviz.command.new_prim('/World', name, 'Xform')
            assert tb.wait_for_commands()
            assert self.stage.GetPrimAtPath(path)
            assert stageviz.command.can_undo()
            stageviz.command.undo()
            assert tb.wait_for_commands()
            assert not self.stage.GetPrimAtPath(path), (index, stageviz.command.can_undo(),
                                                      stageviz.command.can_redo(), self.notice_threads)
            assert stageviz.command.can_redo()
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert self.stage.GetPrimAtPath(path)
            stageviz.command.clear()
            assert not stageviz.command.can_clear()
        assert any(thread != self.main_thread for thread in self.notice_threads), 'USD notices exercised a worker'
        self.snapshot('command_gil_roundtrip')

    def cleanup(self):
        self.notice_key.Revoke()
        self.stage = None
        super().cleanup()

    def menu_roundtrip(self):
        def trigger(name):
            action = tb.viewer().findChild(tb.qt()[1].QAction, name)
            assert action is not None and action.isEnabled(), name
            action.trigger()
            assert tb.wait_for_commands()

        for index in range(8):
            stageviz.command.select_paths(['/World'])
            assert tb.wait_for_commands()
            parent = self.stage.GetPrimAtPath('/World')
            before = {str(prim.GetPath()) for prim in parent.GetChildren()}
            # These actions enter CommandStack directly through Qt, bypassing
            # stageviz.command. They must release the GIL at the native boundary.
            trigger('createXform')
            created = {str(prim.GetPath()) for prim in parent.GetChildren()} - before
            assert len(created) == 1, created
            path = created.pop()
            trigger('editUndo')
            assert not self.stage.GetPrimAtPath(path), ('menu undo', index, path)
            trigger('editRedo')
            assert self.stage.GetPrimAtPath(path)
            trigger('editClear')
            assert not stageviz.command.can_clear()
        self.snapshot('command_gil_menu_roundtrip')


def run(disposable=False):
    test = CommandGilTest(disposable=disposable)
    test.setup()
    try:
        test.roundtrip()
        test.menu_roundtrip()
    finally:
        test.cleanup()
    return True
