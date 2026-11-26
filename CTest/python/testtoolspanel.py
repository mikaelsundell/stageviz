# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
"""View > Tools controls playback and every manipulator status page."""
from pathlib import Path
import runpy

import stageviz

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class ToolsPanelTest(_base['ViewportTest']):
    def action(self):
        return tb.viewer().findChild(tb.qt()[1].QAction, 'viewTools')

    def panel(self):
        return tb.viewer().findChild(tb.qt()[3].QStackedWidget, 'panelWidget')

    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.original_tools = self.action().isChecked()
        assert stageviz.session().newStage()
        stageviz.command.new_prim('/World', 'Cube', 'Cube')
        assert tb.wait_for_commands()
        self.usd_before = stageviz.session().stage().GetRootLayer().ExportToString()
        stageviz.session().viewState().camera().frame(stageviz.session().boundingBox())

    def hidden(self):
        assert tb.wait_until(lambda: self.panel().isHidden() and not self.panel().isVisible())
        assert not self.action().isChecked()
        _, _, _, widgets = tb.qt()
        timeline = tb.find_widget('timelineControls', widgets.QWidget)
        values = tb.find_widget('valueX', widgets.QDoubleSpinBox)
        assert not timeline.isVisible() and not values.isVisible()

    def exercise(self):
        _, gui, _, _ = tb.qt()
        assert self.action().text() == 'Tools' and self.action().isCheckable()
        assert tb.viewer().findChild(gui.QAction, 'viewPlayback') is None
        for mode in ('Select', 'Move', 'Rotate', 'Scale'):
            self.action().setChecked(True)
            self.activate_mode(mode)
            stageviz.command.select_paths(['/World/Cube'])
            assert tb.wait_for_commands()
            expected = 0 if mode == 'Select' else 1
            assert tb.wait_until(lambda: self.panel().isVisible() and self.panel().currentIndex() == expected)
            shown_height = self.viewport().height()
            panel_height = self.panel().height()
            self.snapshot('tools_' + mode + '_shown')
            self.action().setChecked(False)
            self.hidden()
            assert self.viewport().height() >= shown_height + panel_height - 2, 'hidden panel releases its layout space'
            self.snapshot('tools_' + mode + '_hidden')
            stageviz.command.select_paths([])
            assert tb.wait_for_commands()
            self.hidden()
            self.activate_mode('Select')
            self.hidden()
            self.activate_mode('Move')
            stageviz.command.select_paths(['/World/Cube'])
            assert tb.wait_for_commands()
            self.hidden()
        assert stageviz.session().stage().GetRootLayer().ExportToString() == self.usd_before
        self.action().setChecked(True)
        assert tb.wait_until(lambda: self.panel().isVisible() and self.panel().currentIndex() == 1)
        self.snapshot('tools_restored_manipulator')

    def cleanup(self):
        self.action().setChecked(self.original_tools)
        super().cleanup()


def run(disposable=False):
    test = ToolsPanelTest(disposable=disposable)
    try:
        test.setup()
        test.exercise()
    finally:
        test.cleanup()
