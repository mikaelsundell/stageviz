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
        assert line.cursor().shape() == QtCore.Qt.CursorShape.SizeHorCursor
        attr = lambda: stageviz.session().stage().GetAttributeAtPath("/World/Light.inputs:specular")
        baseline = attr().Get()
        stageviz.command.clear()
        activated = []
        shortcuts = [QtGui.QShortcut(QtGui.QKeySequence(key), tree.window()) for key in ("5", "A")]
        for shortcut in shortcuts:
            shortcut.activated.connect(lambda: activated.append(True))
        try:
            self.snapshot("numeric_before_double_click")
            tree, spin, line = self.editor()
            QtTest.QTest.mouseDClick(line, QtCore.Qt.MouseButton.LeftButton, pos=line.rect().center())
            assert line.hasFocus() and not line.isReadOnly()
            assert line.cursor().shape() == QtCore.Qt.CursorShape.IBeamCursor
            assert line.selectedText() == line.text(), "double-click selects the whole value"
            QtTest.QTest.keyClicks(line, "5")
            assert spin.value() == 5.0, "first digit replaces the selected value"
            QtTest.QTest.keyClicks(line, "a")
            assert not activated, "application shortcuts must not run during numeric entry"
            assert spin.value() == 5.0, "invalid numeric input preserves the value"
            QtTest.QTest.keyClick(line, QtCore.Qt.Key.Key_Return)
            assert tb.wait_for_commands()
            assert abs(attr().Get() - 5.0) < 1e-6
            self.snapshot("numeric_double_click_committed")
            stageviz.command.undo()
            assert tb.wait_for_commands() and abs(attr().Get() - baseline) < 1e-6
            stageviz.command.redo()
            assert tb.wait_for_commands() and abs(attr().Get() - 5.0) < 1e-6
        finally:
            for shortcut in shortcuts:
                shortcut.setEnabled(False)
                shortcut.deleteLater()
        tree, spin, line = self.editor()
        decimal = spin.locale().decimalPoint()
        QtTest.QTest.keyClicks(line, "0" + decimal + "25")
        assert abs(spin.value() - .25) < 1e-8 and not line.isReadOnly(), spin.value()
        assert tree.currentItem().text(0) == "inputs:specular", "typing must not jump to another row"
        QtTest.QTest.keyClick(line, QtCore.Qt.Key.Key_Return)
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
            assert line.cursor().shape() == QtCore.Qt.CursorShape.SizeHorCursor
            assert spin.cursor().shape() == QtCore.Qt.CursorShape.SizeHorCursor
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

    def exercise_gizmo_panel(self):
        QtCore, _, QtTest, QtWidgets = tb.qt()
        root = tb.viewer()
        stageviz.command.new_prim("/World", "TestSphere", type_name="Sphere")
        path = "/World/TestSphere"
        assert tb.wait_until(lambda: bool(stageviz.session().stage().GetPrimAtPath(path)))
        self.activate_mode("Move")
        tb.process_events(100)

        panel = tb.find_widget("panelWidget", QtWidgets.QStackedWidget, root)
        frame = tb.find_widget("frame", QtWidgets.QFrame, root)
        fields = [tb.find_widget("value" + axis, QtWidgets.QDoubleSpinBox, root) for axis in "XYZ"]
        assert panel.currentIndex() == 1 and frame.isVisible(), "selected object shows Move panel"

        fields[0].setValue(5.0)
        tb.process_events(75)
        matrix = lambda: stageviz.session().stage().GetPrimAtPath(path).GetAttribute("xformOp:transform").Get()
        assert abs(matrix()[3][0] - 5.0) < 1e-6, "numeric Move authors its preview"
        fields[0].setValue(8.0)
        tb.process_events(75)
        assert abs(matrix()[3][0] - 8.0) < 1e-6, "successive numeric values replace the preview"
        assert not frame.isHidden(), "gizmo panel remains visible while editing values"
        self.snapshot("gizmo_move_numeric")

        # Exercise the viewport's normal click-to-deselect path as well as the
        # command API; this is where a stale operation page used to linger.
        viewport = self.viewport()
        QtTest.QTest.mouseClick(viewport, QtCore.Qt.MouseButton.LeftButton, pos=QtCore.QPoint(4, 4))
        assert tb.wait_until(lambda: not stageviz.session().paths())
        assert panel.currentIndex() == 0 and not frame.isVisible(), (
            f"deselect hides the gizmo panel (page={panel.currentIndex()}, visible={frame.isVisible()})")
        assert [field.value() for field in fields] == [0.0, 0.0, 0.0], "deselect resets Move values"

        stageviz.command.select_paths([path])
        tb.process_events(75)
        self.activate_mode("Scale")
        tb.process_events(75)
        assert panel.currentIndex() == 1 and frame.isVisible(), "selected object shows Scale panel"
        assert [field.value() for field in fields] == [1.0, 1.0, 1.0], "Scale starts at neutral factors"
        fields[0].setValue(2.0)
        tb.process_events(75)
        fields[0].setValue(3.0)
        tb.process_events(75)
        scaled = matrix()
        assert abs(scaled[0][0] - 3.0) < 1e-6, "successive numeric Scale values replace the preview"
        assert abs(scaled[3][0] - 8.0) < 1e-6, "Scale preserves the translated pivot"
        stageviz.command.select_paths([])
        tb.process_events(75)
        assert panel.currentIndex() == 0 and not frame.isVisible(), "deselect hides Scale panel"
        assert [field.value() for field in fields] == [1.0, 1.0, 1.0], "deselect resets Scale values"
        self.snapshot("gizmo_scale_deselected")

        # Switching selection must refresh the numeric baseline before a
        # multi-object edit. Otherwise a stale value from the prior object is
        # treated as the lead object's starting TRS and the delta jumps.
        self.activate_mode("Move")
        stageviz.command.select_paths([path])
        tb.process_events(75)
        fields[0].setValue(8.0)
        tb.process_events(75)

        second_path = "/World/SecondSphere"
        stageviz.command.new_prim("/World", "SecondSphere", type_name="Sphere")
        assert tb.wait_until(lambda: list(stageviz.session().paths()) == [second_path])
        assert tb.wait_until(lambda: abs(fields[0].value()) < 1e-6), (
            "new selection refreshes the panel from the new object's transform")

        stageviz.command.select_paths([path, second_path])
        tb.process_events(75)
        assert list(stageviz.session().paths()) == [path, second_path]
        assert abs(fields[0].value()) < 1e-6, "multi-selection panel shows the most recently selected object's X value"
        fields[0].setValue(1.0)
        tb.process_events(75)
        assert abs(matrix()[3][0] - 9.0) < 1e-6, "numeric delta moves the first selected sphere by one unit"
        second_matrix = lambda: stageviz.session().stage().GetPrimAtPath(second_path).GetAttribute(
            "xformOp:transform").Get()
        assert abs(second_matrix()[3][0] - 1.0) < 1e-6, "numeric delta moves the active sphere by one unit"
        self.snapshot("gizmo_multiselection_numeric")

        # Match the Outliner workflow: select World, then add a sibling prim.
        # The clicked Cube row must become the active selection even though Qt
        # reports selected rows in visual tree order.
        cube_path = "/Cube"
        stageviz.command.new_prim("/", "Cube", type_name="Cube")
        assert tb.wait_until(lambda: list(stageviz.session().paths()) == [cube_path])
        fields[0].setValue(17.0)
        tb.process_events(75)
        tree = tb.find_widget("outlinerTree", QtWidgets.QTreeWidget, root)
        world_item = tree.findItems("World", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        cube_item = tree.findItems("Cube", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.MouseButton.LeftButton,
                                pos=tree.visualItemRect(world_item).center())
        tb.process_events(75)
        QtTest.QTest.mouseClick(tree.viewport(), QtCore.Qt.MouseButton.LeftButton,
                                QtCore.Qt.KeyboardModifier.ControlModifier,
                                tree.visualItemRect(cube_item).center())
        tb.process_events(75)
        assert list(stageviz.session().paths()) == ["/World", cube_path], (
            "the Outliner keeps its current selected row last")
        assert tree.currentItem() is cube_item, "the clicked Cube row is the current Outliner item"
        assert abs(fields[0].value() - 17.0) < 1e-6, "the active Cube supplies the panel TRS values"
        self.snapshot("gizmo_outliner_active_prim")
        print("PASS gizmo panel selection refresh, numeric multi-selection delta, Move/Scale and deselection")


def run(disposable=False):
    test = NumericEditorTest(disposable=disposable)
    try:
        test.setup()
        test.exercise()
        test.exercise_gizmo_panel()
    finally:
        test.cleanup()
    return str(test.directory)
