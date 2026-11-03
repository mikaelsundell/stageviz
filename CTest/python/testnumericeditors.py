# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Numeric typing and repeated property scrubs in a disposable viewer."""

from pathlib import Path
import runpy
import stageviz

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


class NumericEditorTest(_base["ViewportTest"]):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        assert stageviz.session().newStage()
        stageviz.command.new_light("/World", "Light", type="rect")
        assert tb.wait_until(lambda: list(stageviz.session().paths()) == ["/World/Light"])

    def tree(self):
        QtWidgets = tb.qt()[3]
        for widget in tb.all_widgets():
            try:
                if widget.objectName() == "propertyTree" and isinstance(widget, QtWidgets.QTreeWidget) and widget.isVisible():
                    widget.model()
                    return widget
            except RuntimeError:
                continue
        raise AssertionError("visible propertyTree")

    def editor(self):
        QtCore, _, _, QtWidgets = tb.qt()
        tree = self.tree()
        attributes = tree.findItems("Attributes", QtCore.Qt.MatchFlag.MatchExactly, 0)[0]
        tree.expandItem(attributes)
        row = tree.findItems("inputs:specular", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        tree.scrollToItem(row)
        tree.setCurrentItem(row, 1)
        tree.editItem(row, 1)
        tb.process_events(50)
        spin = next(w for w in tree.findChildren(QtWidgets.QDoubleSpinBox) if w.isVisible()
                    and w.metaObject().className() == "stageviz::SpinBox")
        line = spin.findChild(QtWidgets.QLineEdit)
        tree.window().raise_()
        tree.window().activateWindow()
        QtWidgets.QApplication.setActiveWindow(tree.window())
        line.setFocus()
        tb.process_events(50)
        assert line.hasFocus(), "numeric editor has keyboard focus"
        return tree, spin, line

    def exercise(self):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        tree, spin, line = self.editor()
        assert line.isReadOnly()
        decimal = spin.locale().decimalPoint()
        QtTest.QTest.keyClicks(line, "0" + decimal + "25")
        assert abs(spin.value() - .25) < 1e-8 and not line.isReadOnly(), spin.value()
        assert tree.currentItem().text(0) == "inputs:specular", "typing must not jump to another row"
        QtTest.QTest.keyClick(line, QtCore.Qt.Key.Key_Return)
        attr = lambda: stageviz.session().stage().GetAttributeAtPath("/World/Light.inputs:specular")
        assert tb.wait_until(lambda: abs(attr().Get() - .25) < 1e-6)
        tree, spin, line = self.editor()
        # The same delegate editor must survive both asynchronous USD updates.
        import shiboken6
        identity = shiboken6.getCppPointer(spin)[0]
        stageviz.command.clear()
        values = [attr().Get()]
        for _ in range(2):
            start = QtCore.QPoint(max(4, line.width()//2), line.height()//2)
            end = start + QtCore.QPoint(30, 0)
            QtTest.QTest.mousePress(line, QtCore.Qt.MouseButton.LeftButton, pos=start)
            event = QtGui.QMouseEvent(QtCore.QEvent.Type.MouseMove, QtCore.QPointF(end),
                                     QtCore.QPointF(line.mapToGlobal(end)), QtCore.Qt.MouseButton.NoButton,
                                     QtCore.Qt.MouseButton.LeftButton, QtCore.Qt.KeyboardModifier.NoModifier)
            QtWidgets.QApplication.sendEvent(line, event)
            expected = spin.value()
            QtTest.QTest.mouseRelease(line, QtCore.Qt.MouseButton.LeftButton, pos=end)
            assert tb.wait_until(lambda: abs(attr().Get() - expected) < 1e-6)
            assert spin.isVisible() and shiboken6.getCppPointer(spin)[0] == identity
            assert line.hasFocus(), "scrub release preserves the editable field"
            assert expected > values[-1], (expected, values)
            values.append(expected)
        assert spin.isVisible(), "before undo"
        stageviz.command.undo()
        assert tb.wait_until(lambda: abs(attr().Get() - values[1]) < 1e-6)
        assert abs(spin.value() - values[1]) < 1e-6, "undo refreshes focused numeric editor"
        stageviz.command.undo()
        assert tb.wait_until(lambda: abs(attr().Get() - values[0]) < 1e-6)
        stageviz.command.redo()
        assert tb.wait_until(lambda: abs(attr().Get() - values[1]) < 1e-6)
        # Type directly after scrubbing; the first digit replaces the old value.
        QtTest.QTest.keyClicks(line, "0" + decimal + "5")
        assert abs(spin.value() - .5) < 1e-8
        QtTest.QTest.keyClick(line, QtCore.Qt.Key.Key_Return)
        assert tb.wait_until(lambda: abs(attr().Get() - .5) < 1e-6)
        self.tree().setFocus()
        tb.process_events(100)
        self.snapshot("numeric_entry_complete")
        print("PASS numeric replacement, row focus, repeated scrub editor lifetime, undo/redo and keyboard commit")


def run(disposable=False):
    test = NumericEditorTest(disposable=disposable)
    try:
        test.setup()
        test.exercise()
    finally:
        test.cleanup()
    return str(test.directory)
