# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Xform New and Parent through both menus in a disposable viewer."""

from pathlib import Path
import runpy
import stageviz

_base = runpy.run_path(str(Path(__file__).with_name("testviewport.py")),
                      init_globals={"_VIEWPORT_AUTORUN": False})
tb = _base["tb"]


class XformGroupingTest(_base["ViewportTest"]):
    def setup(self):
        assert self.disposable, "use a disposable viewer"
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())

    def trigger(self, source, label):
        QtCore, QtGui, QtTest, QtWidgets = tb.qt()
        if source == "edit":
            assert tb.trigger_action("createXform" if label == "New" else "parentXform")
            return
        assert source == "context"
        tree = None
        for widget in tb.all_widgets():
            try:
                if (widget.objectName() == "outlinerTree" and widget.isVisible()
                        and isinstance(widget, QtWidgets.QTreeWidget)):
                    widget.model()
                    tree = widget
                    break
            except RuntimeError:
                continue
        assert tree is not None, "visible Outliner tree"
        row = tree.findItems("A", QtCore.Qt.MatchFlag.MatchExactly | QtCore.Qt.MatchFlag.MatchRecursive, 0)[0]
        tree.scrollToItem(row)
        position = tree.visualItemRect(row).center()
        chosen = []

        def choose_action():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if not isinstance(widget, QtWidgets.QMenu) or not widget.isVisible():
                    continue
                for action in widget.actions():
                    if action.text() == label and action.isEnabled():
                        widget.setActiveAction(action)
                        chosen.append(True)
                        QtTest.QTest.keyClick(widget, QtCore.Qt.Key.Key_Return)
                        return

        def open_xform():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if not isinstance(widget, QtWidgets.QMenu) or not widget.isVisible():
                    continue
                for action in widget.actions():
                    if action.text() == "Xform" and action.menu():
                        assert [item.text() for item in action.menu().actions()] == ["New", "Parent"]
                        widget.setActiveAction(action)
                        action.menu().popup(widget.mapToGlobal(widget.actionGeometry(action).topRight()))
                        QtCore.QTimer.singleShot(50, choose_action)
                        return

        def close_menu():
            for widget in QtWidgets.QApplication.topLevelWidgets():
                if isinstance(widget, QtWidgets.QMenu) and widget.isVisible():
                    widget.close()

        watchdog = QtCore.QTimer(tree)
        watchdog.setSingleShot(True)
        watchdog.timeout.connect(close_menu)
        watchdog.start(3000)
        QtCore.QTimer.singleShot(50, open_xform)
        event = QtGui.QContextMenuEvent(QtGui.QContextMenuEvent.Reason.Mouse, position,
                                        tree.viewport().mapToGlobal(position))
        QtWidgets.QApplication.sendEvent(tree.viewport(), event)
        watchdog.stop()
        watchdog.deleteLater()
        assert chosen, "context menu Xform > " + label + " was chosen automatically"

    def exercise(self, source):
        _, _, _, QtWidgets = tb.qt()
        parent_action = tb.action_info("parentXform")
        new_action = tb.action_info("createXform")
        assert new_action["text"] == "New" and new_action["shortcut"]
        assert parent_action["text"] == "Parent" and not parent_action["shortcut"]
        menus = {}
        for menu in tb.all_widgets():
            try:
                if (isinstance(menu, QtWidgets.QMenu)
                        and menu.objectName() in ("menuEdit", "menuCreate", "menuXform", "menuCreateXform")):
                    menus[menu.objectName()] = menu
            except RuntimeError:
                continue
        assert len(menus) == 4
        for parent_name, submenu_name in (("menuEdit", "menuXform"), ("menuCreate", "menuCreateXform")):
            submenu = menus[submenu_name]
            assert submenu.parent() is menus[parent_name]
            assert submenu.menuAction() in menus[parent_name].actions()
            assert [action.objectName() for action in submenu.actions()] == ["createXform", "parentXform"]
        assert menus["menuXform"] is not menus["menuCreateXform"]
        # Native macOS menus require a separate QMenu for every attachment.
        # Sharing ordinary QAction objects between those menus remains safe.
        owners = {}
        graph = {}
        for key, menu in menus.items():
            graph[key] = []
            for action in menu.actions():
                child = action.menu()
                if child is None:
                    continue
                child_key = child.objectName()
                assert child_key not in owners, "submenu must have only one attachment: " + child_key
                owners[child_key] = key
                graph[key].append(child_key)
        def check_menu(key, ancestors):
            assert key not in ancestors, "menu graph must not contain a cycle"
            for child in graph.get(key, []):
                check_menu(child, ancestors + [key])
        for key in graph:
            check_menu(key, [])
        for label in ("New", "Parent"):
            for multiple in (False, True):
                assert stageviz.session().newStage()
                assert not tb.action_info("parentXform")["enabled"]
                for name in ("A", "B"):
                    stageviz.command.new_prim("/World", name, type_name="Xform")
                    assert tb.wait_for_commands()
                stageviz.command.new_prim("/World/A", "Child", type_name="Cube")
                assert tb.wait_for_commands()
                stageviz.command.select_paths(["/"])
                assert tb.wait_for_commands()
                assert not tb.action_info("parentXform")["enabled"]
                selection = ["/World/A", "/World/B"] if multiple else ["/World/A"]
                stageviz.command.select_paths(selection)
                assert tb.wait_for_commands()
                assert tb.action_info("parentXform")["enabled"]
                checkpoint = source + "_" + label.lower() + ("_multiple" if multiple else "_single")
                self.snapshot(checkpoint + "_before")
                self.trigger(source, label)
                assert tb.wait_for_commands()
                stage = lambda: stageviz.session().stage()
                if label == "New":
                    created = "/World/Xform" if multiple else "/World/A/Xform"
                    assert stage().GetPrimAtPath(created)
                    assert not list(stage().GetPrimAtPath(created).GetAllChildren())
                    assert stage().GetPrimAtPath("/World/A/Child")
                    assert stage().GetPrimAtPath("/World/B")
                else:
                    created = "/World/Xform"
                    assert stage().GetPrimAtPath(created + "/A/Child")
                    assert not stage().GetPrimAtPath("/World/A")
                    assert bool(stage().GetPrimAtPath(created + "/B")) == multiple
                    assert bool(stage().GetPrimAtPath("/World/B")) != multiple
                    assert list(stageviz.session().paths()) == [created]
                self.snapshot(checkpoint + "_after")
                stageviz.command.undo()
                assert tb.wait_for_commands()
                assert stage().GetPrimAtPath("/World/A/Child")
                assert not stage().GetPrimAtPath(created)
                assert list(stageviz.session().paths()) == selection
                stageviz.command.redo()
                assert tb.wait_for_commands()
                assert stage().GetPrimAtPath(created)
                self.snapshot(checkpoint + "_redone")
                print("PASS Xform >", label, source, "multiple=" + str(multiple))


def run(disposable=False):
    test = XformGroupingTest(disposable=disposable)
    try:
        test.setup()
        test.exercise("edit")
        test.exercise("context")
    finally:
        test.cleanup()
    return str(test.directory)
