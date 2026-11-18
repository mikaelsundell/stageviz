# SPDX-License-Identifier: BSD-3-Clause
"""OCIO import, view controls, remembered state and visible outline regression."""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile

import stageviz
import PyOpenColorIO as OCIO
from pxr import Usd, UsdGeom
from PySide6 import QtCore, QtGui, QtWidgets, QtOpenGLWidgets

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


def run(disposable=False):
    assert disposable, 'this suite replaces the stage; use a disposable process'
    directory = Path(tempfile.mkdtemp(prefix='stageviz-color-management-'))
    session = stageviz.session()
    state = session.viewState()
    original = (state.exposure(), state.gamma(), state.colorView())
    selection_color = stageviz.ui().color(stageviz.UI.ColorRole.Selection)
    fixture = directory / 'color.usda'
    stage = Usd.Stage.CreateNew(str(fixture))
    UsdGeom.Cube.Define(stage, '/Cube')
    stage.GetRootLayer().Save()
    stage = None
    assert session.load(str(fixture))
    assert tb.wait_for_commands()
    state.setGridEnabled(False)
    state.setSceneStatsEnabled(False)
    stageviz.command.select_paths(['/Cube'])
    assert tb.wait_for_commands()
    viewer = tb.viewer()
    state.camera().frame(session.boundingBox())
    assert tb.wait_until(lambda: any(w.objectName() == 'imagingGLWidget' and w.isVisible()
                                    for w in QtWidgets.QApplication.allWidgets()))
    viewport = lambda: next(w for w in QtWidgets.QApplication.allWidgets()
                           if w.objectName() == 'imagingGLWidget' and w.isVisible())
    stageviz.ui().setColor(stageviz.UI.ColorRole.Selection, 255, 0, 255)
    assert OCIO.GetVersion()
    config = OCIO.Config.CreateFromBuiltinConfig('studio-config-v2.2.0_aces-v1.3_ocio-v2.4')
    assert state.colorViews() == list(config.getViews('sRGB - Display'))
    menu = viewer.findChild(QtWidgets.QMenu, 'displayColor')
    assert menu.title() == 'Color'
    assert [a.text() for a in menu.actions() if not a.isSeparator()] == ['Exposure', 'Gamma', 'View']
    exposure = viewer.findChild(QtWidgets.QMenu, 'displayExposure')
    gamma = viewer.findChild(QtWidgets.QMenu, 'displayGamma')
    assert [None if a.isSeparator() else a.text() for a in exposure.actions()] == [
        '+3', '+2', '+1', None, '0', None, '-1', '-2', '-3']
    assert [None if a.isSeparator() else a.text() for a in gamma.actions()] == [
        '2.0', '1.5', '1.2', None, '1.0', None, '0.8', '0.6', '0.5']

    def checked(menu):
        return [round(a.data(), 5) for a in menu.actions() if not a.isSeparator() and a.isChecked()]

    def choose(menu, value):
        next(a for a in menu.actions() if not a.isSeparator() and abs(a.data()-value)<1e-5).trigger()
        assert checked(menu) == [value]
    views = viewer.findChild(QtWidgets.QMenu, 'displayColorView')
    assert [a.text() for a in views.actions()] == state.colorViews()

    def image(label):
        viewport().repaint()
        assert tb.wait_until(lambda: viewport().isValid())
        result = viewport().grabFramebuffer()
        assert not result.isNull()
        assert viewer.grab().save(str(directory / (label + '.png')))
        rgba = result.convertToFormat(result.Format.Format_RGBA8888)
        data = bytes(rgba.constBits())
        assert sum(r > 220 and g < 70 and b > 220 for r,g,b in
                   zip(data[0::4], data[1::4], data[2::4])) > 8, 'selection color survives view changes'
        return data

    try:
        state.setExposure(0)
        state.setGamma(1)
        state.setColorView('Un-tone-mapped')
        assert checked(exposure) == [0] and checked(gamma) == [1]
        for preset in [3, 2, 1, 0, -1, -2, -3, -3, 0]:
            choose(exposure, preset)
            assert state.exposure() == preset
        for preset in [2, 1.5, 1.2, 1, .8, .6, .5, .5, 1]:
            choose(gamma, preset)
            assert abs(state.gamma()-preset)<1e-5
        state.setExposure(1.25)
        state.setGamma(1.1)
        assert checked(exposure) == [] and checked(gamma) == []
        choose(exposure, 0)
        choose(gamma, 1)
        normal = image('srgb')
        choose(exposure, 2)
        assert state.exposure() == 2
        brighter = image('exposure')
        assert normal != brighter
        state.setExposure(0)
        choose(gamma, 2)
        assert state.gamma() == 2
        assert image('gamma') != normal
        state.setGamma(1)
        for view in state.colorViews():
            next(a for a in views.actions() if a.data() == view).trigger()
            assert state.colorView() == view
            image('view-' + str(state.colorViews().index(view)))
            assert next(a for a in views.actions() if a.data() == view).isChecked()
        trace = os.environ.get('STAGEVIZ_PERF_LOG')
        def program_count():
            if not trace:
                return None
            return sum(json.loads(line).get('phase') == 'color_management.program.create'
                       for line in Path(trace).read_text().splitlines() if line)
        compiled = program_count()
        for view in state.colorViews():
            state.setColorView(view)
            state.setExposure(0.5)
            state.setGamma(1.1)
            image('cached-' + str(state.colorViews().index(view)))
        assert program_count() == compiled, 'revisiting views or changing exposure/gamma recompiled OCIO'
        for getter, setter in [(state.exposure, state.setExposure), (state.gamma, state.setGamma)]:
            before = getter()
            for value in [float('nan'), float('inf'), -float('inf')]:
                try:
                    setter(value)
                except ValueError:
                    pass
                else:
                    raise AssertionError('nonfinite adjustment accepted')
            assert getter() == before
        try:
            state.setColorView('not-a-view')
        except ValueError:
            pass
        else:
            raise AssertionError('unsupported view accepted')
        state.setExposure(1.25)
        state.setGamma(1.2)
        state.setColorView('ACES 1.0 - SDR Video')
        saved = directory / 'remembered.json'
        assert session.saveState(str(saved))
        state.setExposure(0)
        state.setGamma(1)
        state.setColorView('Raw')
        assert session.loadState(str(saved))
        assert abs(state.exposure()-1.25)<1e-5 and abs(state.gamma()-1.2)<1e-5
        assert state.colorView() == 'ACES 1.0 - SDR Video'
        assert checked(exposure) == [] and checked(gamma) == [1.2]
        assert session.paths() == ['/Cube']
        with contextlib.redirect_stdout(io.StringIO()):
            snapshot = tb.snapshot_qt_state('color-management')
        (directory / 'qt.json').write_text(json.dumps(snapshot, indent=2))
        for label, submenu in [('exposure-menu', exposure), ('gamma-menu', gamma)]:
            submenu.popup(viewer.mapToGlobal(QtCore.QPoint(250, 120)))
            assert tb.wait_until(lambda: submenu.isVisible())
            assert submenu.grab().save(str(directory / (label + '.png')))
            submenu.hide()
        print('PASS testcolormanagement: OCIO import, controls, views, outlines and remembered state')
        print('Color management evidence: ' + str(directory))
    finally:
        state.setExposure(original[0])
        state.setGamma(original[1])
        state.setColorView(original[2])
        stageviz.ui().setColor(stageviz.UI.ColorRole.Selection, *selection_color)
