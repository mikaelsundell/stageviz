# SPDX-License-Identifier: BSD-3-Clause
"""Imported MaterialX USD Preview Surface keeps its shading after a scrub."""
import contextlib
import io
import json
from pathlib import Path
import tempfile

import stageviz
from pxr import Usd, UsdShade, UsdGeom, Sdf
from PySide6 import QtCore, QtGui, QtWidgets
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import testbench as tb


def run(disposable=False):
    assert disposable, 'requires a disposable app'
    directory = Path(tempfile.mkdtemp(prefix='stageviz-materialx-preview-'))
    path = '/Materials/carpaint'
    input_path = path + '/carpaint.inputs:roughness'
    fixture = directory / 'max-style.usda'
    stage = Usd.Stage.CreateNew(str(fixture))
    UsdGeom.Scope.Define(stage, '/Materials')
    material = UsdShade.Material.Define(stage, path)
    shader = UsdShade.Shader.Define(stage, path + '/carpaint')
    shader.CreateIdAttr('ND_UsdPreviewSurface_surfaceshader')
    shader.CreateInput('diffuseColor', Sdf.ValueTypeNames.Color3f).Set((.562, .355, .402))
    shader.CreateInput('roughness', Sdf.ValueTypeNames.Float).Set(.5)
    shader.CreateOutput('surface', Sdf.ValueTypeNames.Token)
    out = shader.CreateOutput('out', Sdf.ValueTypeNames.Token)
    material.CreateSurfaceOutput('mtlx').ConnectToSource(out)
    stage.GetRootLayer().Save()
    del material, shader, stage, out
    session = stageviz.session()
    assert session.load(str(fixture)) and tb.wait_for_commands()
    assert tb.trigger_action('materialEditor')
    assert tb.wait_until(lambda: any(w.objectName() == 'materialDialog' and w.isVisible()
                                    for w in QtWidgets.QApplication.topLevelWidgets()))
    editor = next(w for w in QtWidgets.QApplication.topLevelWidgets()
                  if w.objectName() == 'materialDialog' and w.isVisible())
    browser = editor.findChild(QtWidgets.QWidget, 'browserWidget')
    icons = browser.findChild(QtWidgets.QListWidget, 'icons')

    def image():
        if icons.count() != 1:
            return QtGui.QImage()
        value = icons.item(0).data(int(QtCore.Qt.ItemDataRole.UserRole) + 1)
        return value.copy() if isinstance(value, QtGui.QImage) else QtGui.QImage()

    def ready():
        value = image()
        return not value.isNull() and value.pixelColor(value.width()//2, value.height()//2).alpha() > 240

    def is_shaded(value):
        if value.isNull():
            return False
        samples = [value.pixelColor(int(value.width()*x), int(value.height()*y))
                   for x, y in ((.5,.3),(.4,.4),(.6,.4),(.4,.6),(.6,.6),(.5,.7))]
        return (max(c.red() for c in samples) - min(c.red() for c in samples) > 15
                and any(c.red() > c.green() + 12 for c in samples))

    def shaded():
        large = editor.findChild(QtWidgets.QWidget, 'swatch')
        return (is_shaded(image()) and large is not None
                and is_shaded(large.grab().toImage()))

    def capture(label):
        assert editor.grab().save(str(directory / (label + '.png')))
        with contextlib.redirect_stdout(io.StringIO()):
            data = tb.snapshot_qt_state(label, root=editor)
        (directory / (label + '.json')).write_text(json.dumps(data, indent=2))
        print('MaterialX preview evidence: ' + str(directory), flush=True)

    try:
        assert tb.wait_until(ready, timeout=60)
        icons.item(0).setSelected(True)
        tb.process_events(500)
        capture('initial')
        assert shaded(), 'MaterialX USD Preview Surface must render its network rather than a flat placeholder'
        tree = editor.findChild(QtWidgets.QTreeWidget, 'tree')
        assert tree is not None
        rows = tree.findItems('Roughness', QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)
        assert len(rows) == 1
        row = rows[0]
        ancestors = []
        parent = row.parent()
        while parent:
            ancestors.append(parent)
            parent = parent.parent()
        for parent in reversed(ancestors):
            if not parent.isExpanded():
                parent.setExpanded(True)
        # Expanding a lazy group can rebuild its input rows.
        rows = tree.findItems('Roughness', QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)
        row = rows[0]
        tree.scrollToItem(row)
        control = tree.itemWidget(row, 1)
        spin = control if isinstance(control, QtWidgets.QDoubleSpinBox) else control.findChild(QtWidgets.QDoubleSpinBox)
        line = spin.findChild(QtWidgets.QLineEdit)
        assert line is not None and line.isReadOnly()
        stageviz.command.clear()
        before = session.stage().GetRootLayer().ExportToString()
        # Drive the actual numeric scrub path, including its transient preview.
        def mouse(kind, x, button, buttons):
            local = QtCore.QPointF(x, 10)
            event = QtGui.QMouseEvent(kind, local, QtCore.QPointF(line.mapToGlobal(local.toPoint())),
                                     button, buttons, QtCore.Qt.KeyboardModifier.NoModifier)
            QtWidgets.QApplication.sendEvent(line, event)
        mouse(QtCore.QEvent.Type.MouseButtonPress, 10, QtCore.Qt.MouseButton.LeftButton, QtCore.Qt.MouseButton.LeftButton)
        mouse(QtCore.QEvent.Type.MouseMove, 30, QtCore.Qt.MouseButton.NoButton, QtCore.Qt.MouseButton.LeftButton)
        tb.process_events(300)
        capture('dragging')
        assert shaded()
        assert session.stage().GetRootLayer().ExportToString() == before, 'transient preview must not author USD'
        mouse(QtCore.QEvent.Type.MouseButtonRelease, 30, QtCore.Qt.MouseButton.LeftButton, QtCore.Qt.MouseButton.NoButton)
        assert tb.wait_for_commands()
        tb.process_events(700)
        capture('committed')
        assert shaded(), 'committed swatch must not revert to the placeholder'
        changed = session.stage().GetAttributeAtPath(input_path).Get()
        assert abs(changed - .5) > .001
        stageviz.command.undo()
        assert tb.wait_for_commands()
        tb.process_events(500)
        assert abs(session.stage().GetAttributeAtPath(input_path).Get() - .5) < .001 and shaded()
        stageviz.command.redo()
        assert tb.wait_for_commands()
        tb.process_events(500)
        assert abs(session.stage().GetAttributeAtPath(input_path).Get() - changed) < .001 and shaded()
        editor.hide()
        editor.show()
        tb.process_events(500)
        assert shaded()
        assert session.stage().GetPrimAtPath(path+'/carpaint').GetAttribute('info:id').Get() == 'ND_UsdPreviewSurface_surfaceshader'
        capture('reopened')
        print('PASS: MaterialX preview initial, scrub, commit, undo/redo and reopen', flush=True)
    finally:
        try:
            editor.hide()
        except RuntimeError:
            pass
