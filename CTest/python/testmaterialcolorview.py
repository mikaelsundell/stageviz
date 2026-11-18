# SPDX-License-Identifier: BSD-3-Clause
"""Material editor follows OCIO view changes while reusing bounded linear caches."""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile

import stageviz
from pxr import Usd, UsdGeom, UsdShade, Sdf, Gf
from PySide6 import QtCore, QtGui, QtWidgets, QtOpenGLWidgets

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


def run(disposable=False):
    assert disposable, 'material color test requires a disposable process'
    directory = Path(tempfile.mkdtemp(prefix='stageviz-material-color-'))
    fixture = directory / 'material.usda'
    source = Usd.Stage.CreateNew(str(fixture))
    cube = UsdGeom.Cube.Define(source, '/Cube')
    material = UsdShade.Material.Define(source, '/Materials/HDR')
    shader = UsdShade.Shader.Define(source, '/Materials/HDR/Surface')
    shader.CreateIdAttr('UsdPreviewSurface')
    shader.CreateInput('diffuseColor', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(0))
    shader.CreateInput('emissiveColor', Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(4, .18, .02))
    material.CreateSurfaceOutput().ConnectToSource(shader.CreateOutput('surface', Sdf.ValueTypeNames.Token))
    UsdShade.MaterialBindingAPI.Apply(cube.GetPrim()).Bind(material)
    source.GetRootLayer().Save()
    source = None
    session = stageviz.session()
    state = session.viewState()
    original = state.colorView(), state.exposure(), state.gamma()
    assert session.load(str(fixture))
    assert tb.wait_for_commands()
    state.setColorView('Raw')
    state.setExposure(0)
    state.setGamma(1)
    assert tb.trigger_action('materialEditor')
    assert tb.wait_until(lambda: any(w.objectName() == 'materialDialog' and w.isVisible()
                                    for w in QtWidgets.QApplication.topLevelWidgets()))

    def dialog():
        return next(w for w in QtWidgets.QApplication.topLevelWidgets()
                    if w.objectName() == 'materialDialog')

    # Keep the stable parent wrappers alive while querying native child items.
    # The stage and editor widgets are not rebuilt during these view-only checks.
    editor = dialog()
    browser = editor.findChild(QtWidgets.QWidget, 'browserWidget')
    icon_list = browser.findChild(QtWidgets.QListWidget, 'icons')

    def icons():
        return icon_list

    def image():
        if not icons().count():
            return QtGui.QImage()
        value = icons().item(0).data(int(QtCore.Qt.ItemDataRole.UserRole) + 1)
        return value.copy() if isinstance(value, QtGui.QImage) else QtGui.QImage()

    def rendered():
        value = image()
        return not value.isNull() and value.pixelColor(value.width()//2, value.height()//2).alpha() == 255

    def render_count():
        trace = os.environ.get('STAGEVIZ_PERF_LOG')
        if not trace:
            return None
        return sum(json.loads(line).get('phase') == 'material.swatch.linear_render'
                   for line in Path(trace).read_text().splitlines() if line)

    def capture(label):
        assert dialog().grab().save(str(directory / (label + '.png')))
        with contextlib.redirect_stdout(io.StringIO()):
            snapshot = tb.snapshot_qt_state(label)
        (directory / (label + '.json')).write_text(json.dumps(snapshot, indent=2))

    try:
        ready = tb.wait_until(rendered, timeout=60)
        if not ready:
            capture('pending')
            value = image()
            raise AssertionError(dict(count=icons().count(), visible=icons().isVisible(),
                size=(value.width(), value.height()),
                alpha=value.pixelColor(value.width()//2,value.height()//2).alpha() if not value.isNull() else None,
                stored_type=str(type(icons().item(0).data(int(QtCore.Qt.ItemDataRole.UserRole)+1))) if icons().count() else None,
                evidence=str(directory)))
        icons().setCurrentRow(0)
        tb.process_events(100)
        raw = image()
        count = render_count()
        capture('raw')
        state.setColorView('ACES 1.0 - SDR Video')
        assert tb.wait_until(lambda: image() != raw)
        aces = image()
        assert render_count() == count, 'view change rerendered shaderball geometry'
        capture('aces')
        state.setExposure(3)
        state.setGamma(2)
        tb.process_events(100)
        assert image() == aces and render_count() == count, 'viewport inspection adjustments changed swatches'
        state.setColorView('Raw')
        assert tb.wait_until(lambda: image() == raw)
        for _ in range(3):
            state.setColorView('Un-tone-mapped')
            assert tb.wait_until(lambda: image() != raw)
            state.setColorView('Raw')
            assert tb.wait_until(lambda: image() == raw)
        assert render_count() == count
        dialog().hide()
        state.setColorView('ACES 1.0 - SDR Video')
        tb.process_events(100)
        assert image() == raw, 'hidden swatch should refresh lazily'
        dialog().show()
        assert tb.wait_until(lambda: image() == aces)
        assert render_count() == count, 'showing cached swatches rerendered geometry'
        capture('reopened-aces')
        print('PASS testmaterialcolorview: view sync, neutral adjustments, cached transforms and hidden editor refresh')
        print('Material color evidence: ' + str(directory))
    finally:
        state.setColorView(original[0])
        state.setExposure(original[1])
        state.setGamma(original[2])
        dialog().hide()
