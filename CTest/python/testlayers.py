# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""Automated layer workflow and Qt UI checks for the Stageviz Python editor.

Run this from Stageviz or through the Stageviz MCP Python tool. The script
creates disposable USD fixtures, exercises layer commands and save APIs, and
checks the File menu, window title, Layer Editor, and layer rows through Qt
object names and model roles. UI snapshots are captured at former manual
checkpoints so runs do not wait for user input.
"""

_TESTLAYERS_REVISION = "layer-tree-expansion-save-state-2026-10-27"

import importlib
import os
from pathlib import Path
import sys
import tempfile

from pxr import Sdf, UsdGeom, UsdShade


def _import_testbench():
    probes = [Path.cwd(), Path(sys.executable).resolve().parent]
    if globals().get("__file__"):
        probes.append(Path(__file__).resolve().parent)
    try:
        from PySide6.QtCore import QCoreApplication
        app = QCoreApplication.instance()
        if app is not None:
            probes.append(Path(app.applicationDirPath()))
    except (ImportError, RuntimeError):
        pass
    for probe in probes:
        for parent in (probe, *probe.parents):
            directory = parent if (parent / "testbench.py").is_file() else parent / "CTest" / "python"
            if (directory / "testbench.py").is_file():
                if str(directory) not in sys.path:
                    sys.path.insert(0, str(directory))
                return importlib.reload(importlib.import_module("testbench"))
    raise ImportError("Could not find CTest/python/testbench.py from the Stageviz process paths")


_tb = _import_testbench()
_qt = _tb.qt
_process_events = _tb.process_events
_release_qt_wrappers = _tb.release_qt_wrappers
_wait_until = _tb.wait_until
_viewer = _tb.viewer
_status_message = _tb.status_message
_snapshot_qt_state = _tb.snapshot_qt_state
_progress_state = _tb.progress_state
_action_info = _tb.action_info
_trigger_action = _tb.trigger_action
_session = _tb.session
_command = _tb.command
_layer_flag = _tb.layer_property
_UI_ACTION_FAILURES = []
_LAYER_DIALOG_WRAPPER = None
_LAYER_TREE_WRAPPER = None


def _layer_dialog_visible():
    _, _, _, QtWidgets = _qt()
    if QtWidgets.QApplication.instance() is None:
        return False
    try:
        for widget in QtWidgets.QApplication.allWidgets():
            try:
                if widget.objectName() == "layerDialog" and widget.isVisible():
                    return True
            except RuntimeError:
                continue
    except RuntimeError:
        return False
    return False


def _layer_tree():
    global _LAYER_DIALOG_WRAPPER, _LAYER_TREE_WRAPPER
    _, _, _, QtWidgets = _qt()
    if QtWidgets.QApplication.instance() is None:
        return None

    # Layer Editor is a top-level dialog parented to Viewer. Resolve its live,
    # visible instance directly and retain the PySide wrappers; a viewer-wide
    # findChild can return a wrapper for a dialog/tree that was already replaced.
    for _ in range(3):
        try:
            dialogs = [widget for widget in QtWidgets.QApplication.topLevelWidgets()
                       if widget.objectName() == "layerDialog" and widget.isVisible()]
            for dialog in dialogs:
                tree = dialog.findChild(QtWidgets.QTreeWidget, "layerTree")
                if tree is not None:
                    tree.model()  # Validate the C++ widget before retaining its wrapper.
                    _LAYER_DIALOG_WRAPPER = dialog
                    _LAYER_TREE_WRAPPER = tree
                    return _LAYER_TREE_WRAPPER
        except RuntimeError:
            pass
        _process_events()
    # Keep a hidden dialog discoverable during startup before its first show.
    try:
        for widget in QtWidgets.QApplication.allWidgets():
            if widget.objectName() != "layerDialog":
                continue
            tree = widget.findChild(QtWidgets.QTreeWidget, "layerTree")
            if tree is not None:
                tree.model()
                _LAYER_DIALOG_WRAPPER = widget
                _LAYER_TREE_WRAPPER = tree
                return _LAYER_TREE_WRAPPER
    except RuntimeError:
        pass
    return None


def _layer_button_enabled(name):
    return _tb.button_enabled(name)


def _click_layer_button(name):
    return _tb.click_button(name)


def _open_layer_dialog():
    action_triggered = _trigger_action("fileLayers")
    _require(action_triggered, "File > Layers QAction is available and triggered")
    visible = _wait_until(_layer_dialog_visible)
    if action_triggered and not visible:
        # Keep the layer workflow runnable if the application action is wired
        # incorrectly, while recording that regression for the final result.
        _, _, _, QtWidgets = _qt()
        dialog = _tb.find_widget("layerDialog", QtWidgets.QDialog)
        if dialog is not None:
            try:
                dialog.show()
                dialog.raise_()
            except RuntimeError:
                pass
            dialog = None
            _process_events()
            visible = _wait_until(_layer_dialog_visible)
            if visible:
                message = "File > Layers QAction did not show the Layer Editor; opened it through its QObject instead"
                _UI_ACTION_FAILURES.append(message)
                print("[ FAIL ]", message)
    if not visible:
        _, _, _, QtWidgets = _qt()
        app = QtWidgets.QApplication.instance()
        windows = []
        if app is not None:
            try:
                windows = [
                    _qt_window_state(widget) for widget in QtWidgets.QApplication.topLevelWidgets()
                ]
            except RuntimeError as error:
                windows = [("<inspection failed>", repr(error))]
        print("[ STATE ] fileLayers action:", _action_info("fileLayers"))
        print("[ STATE ] top-level Qt windows:", windows)
        try:
            matching_widgets = [
                _qt_window_state(widget) for widget in QtWidgets.QApplication.allWidgets()
                if widget.objectName() == "layerDialog"
            ] if app is not None else []
        except RuntimeError:
            matching_widgets = [("<deleted during lookup>", "", False, "")]
        print("[ STATE ] layerDialog widgets:", matching_widgets)
    _require(visible, "File > Layers shows the Layer Editor")
    _require(_layer_tree() is not None, "Layer Editor exposes the layerTree QObject")
    _process_events()
    _snapshot_qt_state("Layer Editor open")
    return _layer_tree()


def _qt_window_state(widget):
    try:
        return (
            str(widget.objectName()),
            str(widget.metaObject().className()),
            bool(widget.isVisible()),
            str(widget.windowTitle()),
        )
    except RuntimeError:
        return ("<deleted Qt window>", "", False, "")


def _layer_indexes(tree):
    if tree is None:
        return
    QtCore, _, _, _ = _qt()
    try:
        model = tree.model()
        stack = [(QtCore.QModelIndex(), 0)]
        while stack:
            parent, row = stack.pop()
            if row >= model.rowCount(parent):
                continue
            stack.append((parent, row + 1))
            index = model.index(row, 0, parent)
            yield index
            stack.append((index, 0))
    except RuntimeError:
        return


def _layer_index(identifier, tree=None):
    QtCore, _, _, _ = _qt()
    for candidate in (tree, None):
        live_tree = candidate if candidate is not None else _layer_tree()
        if live_tree is None:
            continue
        try:
            live_tree.model()
            for index in _layer_indexes(live_tree):
                if str(index.data(QtCore.Qt.ItemDataRole.UserRole + 1) or "") == identifier:
                    return index
        except RuntimeError:
            continue
    return QtCore.QModelIndex()


def _current_layer_identifier(tree=None):
    QtCore, _, _, _ = _qt()
    for candidate in (tree, None):
        live_tree = candidate if candidate is not None else _layer_tree()
        if live_tree is None:
            continue
        try:
            return str(live_tree.currentIndex().data(QtCore.Qt.ItemDataRole.UserRole + 1) or "")
        except RuntimeError:
            continue
    return ""


def _layer_row(identifier, tree=None):
    QtCore, _, _, _ = _qt()
    for _ in range(3):
        live_tree = _layer_tree()
        if live_tree is None:
            continue
        try:
            index = _layer_index(identifier, live_tree)
            if not index.isValid():
                return None
            model = live_tree.model()
            font = index.data(QtCore.Qt.ItemDataRole.FontRole)
            visibility_index = model.index(index.row(), 1, index.parent())
            lock_index = model.index(index.row(), 2, index.parent())
            return {
                "name": str(index.data(QtCore.Qt.ItemDataRole.DisplayRole) or ""),
                "bold": bool(font.bold()) if font is not None else False,
                "visible_icon": visibility_index.data(QtCore.Qt.ItemDataRole.DecorationRole),
                "visibility_tooltip": str(visibility_index.data(QtCore.Qt.ItemDataRole.ToolTipRole) or ""),
                "lock_icon": lock_index.data(QtCore.Qt.ItemDataRole.DecorationRole),
                "lock_tooltip": str(lock_index.data(QtCore.Qt.ItemDataRole.ToolTipRole) or ""),
                "opinions_icon": model.index(index.row(), 3, index.parent()).data(QtCore.Qt.ItemDataRole.DecorationRole),
            }
        except RuntimeError:
            _process_events()
    return None


def _layer_tree_rows():
    """Return visible model identifiers and labels for useful failure diagnostics."""
    QtCore, _, _, _ = _qt()
    tree = _layer_tree()
    if tree is None:
        return []
    rows = []
    try:
        for index in _layer_indexes(tree):
            rows.append((
                str(index.data(QtCore.Qt.ItemDataRole.UserRole + 1) or ""),
                str(index.data(QtCore.Qt.ItemDataRole.DisplayRole) or ""),
            ))
    except RuntimeError:
        return rows
    return rows


def _icon_present(icon):
    return icon is not None and not icon.isNull()


def _icon_key(icon):
    if not _icon_present(icon):
        return 0
    sizes = icon.availableSizes()
    pixmap = icon.pixmap(sizes[0]) if sizes else icon.pixmap(24, 24)
    image = pixmap.toImage()
    return tuple(image.pixelColor(x, y).rgba() for y in range(image.height())
                 for x in range(image.width()))


def _row_icon_key(identifier, column):
    row = _layer_row(identifier)
    if row is None:
        return None
    role = {1: "visible_icon", 2: "lock_icon", 3: "opinions_icon"}.get(column)
    return _icon_key(row[role]) if role else None


def _layer_children(identifier, tree=None):
    QtCore, _, _, _ = _qt()
    for candidate in (tree, None):
        live_tree = candidate if candidate is not None else _layer_tree()
        if live_tree is None:
            continue
        try:
            parent = _layer_index(identifier, live_tree)
            if not parent.isValid():
                return []
            model = live_tree.model()
            return [
                str(model.index(row, 0, parent).data(QtCore.Qt.ItemDataRole.UserRole + 1) or "")
                for row in range(model.rowCount(parent))
            ]
        except RuntimeError:
            continue
    return []


def _assert_file_menu(*, save_enabled=None, save_as_enabled=True, copy_enabled=True, save_all_enabled=True):
    expected = {
        "fileOpen": ("Open", None),
        "fileLayers": ("Layers ...", "L"),
        "fileLayerSave": ("Save layer", "Ctrl+S"),
        "fileLayerSaveAs": ("Save layer as ...", "Ctrl+Shift+S"),
        "fileLayerSaveCopyAs": ("Save layer copy as ...", "Ctrl+Alt+S"),
        "fileLayerSaveAll": ("Save all layers", None),
    }
    for object_name, (text, shortcut) in expected.items():
        info = _action_info(object_name)
        _require(info is not None, "File menu has QAction " + object_name)
        if info is None:
            continue
        _require(info["text"].replace("&", "").strip() == text,
                 object_name + " label matches " + repr(text))
        if shortcut is not None:
            _require(info["shortcut"] == shortcut,
                     object_name + " shortcut matches " + shortcut)
    enabled = {
        "fileLayerSave": save_enabled,
        "fileLayerSaveAs": save_as_enabled,
        "fileLayerSaveCopyAs": copy_enabled,
        "fileLayerSaveAll": save_all_enabled,
    }
    for object_name, state in enabled.items():
        if state is not None:
            info = _action_info(object_name)
            _require(info is not None and info["enabled"] == state,
                     object_name + " enabled state is " + str(state))


def _assert_layer_row(identifier, *, name_contains=None, dirty=None, bold=None,
                      visibility=None, locked=None, opinions=None):
    observed = {}

    def find_row():
        observed["row"] = _layer_row(identifier)
        return observed["row"] is not None

    if not _wait_until(find_row, timeout=10.0):
        print("[ STATE ] expected layer row missing:", identifier)
        print("[ STATE ] Layer Editor rows:", _layer_tree_rows())
        try:
            stage = _session().stage()
            root = stage.GetRootLayer() if stage else None
            print("[ STATE ] live stage root:", root.identifier if root else None)
            print("[ STATE ] live layer stack:", [layer.identifier for layer in _tb.layer_stack(stage)[1]])
        except Exception as error:
            print("[ STATE ] could not inspect live layer stack:", repr(error))
    row = observed.get("row")
    _require(row is not None, "Layer Editor contains row " + identifier)
    if row is None:
        return
    checks = (
        (name_contains is None or name_contains in row["name"], "name contains " + repr(name_contains)),
        (dirty is None or row["name"].startswith("*") == dirty, "dirty marker is " + str(dirty)),
        (bold is None or row["bold"] == bold, "bold edit-target state is " + str(bold)),
        (visibility is None or _icon_present(row["visible_icon"]) == visibility,
         "visibility icon presence is " + str(visibility)),
        (locked is None or _icon_present(row["lock_icon"]) == locked, "lock icon presence is " + str(locked)),
        (opinions is None or _icon_present(row["opinions_icon"]) == opinions,
         "opinions icon presence is " + str(opinions)),
    )
    for passed, message in checks:
        if passed:
            print("[ OK ] Layer row " + row["name"] + ": " + message)
        else:
            raise AssertionError("Layer row " + row["name"] + ": expected " + message)


def _assert_window_title(active_layer_name=None, dirty=None):
    viewer = _viewer()
    _require(viewer is not None, "Viewer QMainWindow found by QObject name")
    if viewer is None:
        return
    title = str(viewer.windowTitle())
    if active_layer_name:
        _require(active_layer_name in title, "window title names active layer " + active_layer_name + ": " + title)
    if dirty is not None:
        _require(title.count("*") > 0 if dirty else title.count("*") == 0,
                 "window title dirty marker is " + str(dirty) + ": " + title)


def _checkpoint(title):
    """Capture former interactive checkpoints without blocking an automated run."""
    print("[ AUTO CHECKPOINT ]", title)
    _snapshot_qt_state(title)
    _process_events()


_require = _tb.require


def _make_layer(directory, filename, authored_prim, shared_value=None):
    path = os.path.join(directory, filename)

    def author(stage):
        world = UsdGeom.Xform.Define(stage, "/World")
        if shared_value is not None:
            shared = UsdGeom.Xform.Define(stage, "/World/Shared").GetPrim()
            owner = shared.CreateAttribute("test:layerOwner", Sdf.ValueTypeNames.String, custom=True)
            owner.Set(shared_value)
        if authored_prim:
            UsdGeom.Sphere.Define(stage, "/World/" + authored_prim)
        stage.SetDefaultPrim(world.GetPrim())

    return _tb.create_layer_file(path, author)


def _make_root(directory, filename, sublayers):
    path = os.path.join(directory, filename)

    def author(stage):
        world = UsdGeom.Xform.Define(stage, "/World")
        UsdGeom.Xform.Define(stage, "/World/Shared")
        stage.SetDefaultPrim(world.GetPrim())

    return _tb.create_sublayered_stage(path, sublayers, author)


def _make_fixtures(directory):
    fixtures = {}

    basic = [
        _make_layer(directory, "Geometry.usda", "Geometry", "geometry"),
        _make_layer(directory, "Lighting.usda", "Light", "lighting"),
        _make_layer(directory, "Overrides.usda", "Override", "override"),
    ]
    fixtures["basic"] = _make_root(directory, "01_basic.usda", basic)

    nested_leaf = _make_layer(directory, "NestedDetail.usda", "NestedDetail", "detail")
    nested_parent = _make_layer(directory, "NestedModel.usda", "NestedModel", "model")
    nested_parent_layer = Sdf.Layer.FindOrOpen(nested_parent)
    nested_parent_layer.subLayerPaths = [nested_leaf]
    nested_parent_layer.Save()
    nested_light = _make_layer(directory, "NestedLighting.usda", "NestedLight", "nested-light")
    fixtures["nested"] = _make_root(directory, "02_nested.usda", [nested_parent, nested_light])

    five = [
        _make_layer(directory, "A_Geometry.usda", "A_Geometry", "five-a"),
        _make_layer(directory, "B_Materials.usda", "B_Materials", "five-b"),
        _make_layer(directory, "C_Lighting.usda", "C_Lighting", "five-c"),
        _make_layer(directory, "D_Animation.usda", "D_Animation", "five-d"),
        _make_layer(directory, "E_Overrides.usda", "E_Overrides", "five-e"),
    ]
    fixtures["five"] = _make_root(directory, "03_five_layers.usda", five)
    fixtures["root_only"] = _make_root(directory, "04_root_only.usda", [])
    return fixtures


def _run_command_and_wait(progress_name, callback):
    # The progress tool window is optional and may be reconstructed while a
    # stage is replaced. Commands are synchronous through the Python binding;
    # inspect progress completion when the live QObjects are available, but
    # do not make their transient lifetime a precondition for testing USD edits.
    _, status, tree = _progress_state()
    previous_count = tree.topLevelItemCount() if tree is not None else -1
    callback()

    if status is None or tree is None:
        print("[ INFO ] progress widgets unavailable for", progress_name,
              "; validating the command through the resulting USD and Qt state")
        _process_events()
        return

    def completed():
        if not str(status.text()).startswith("Finished: " + progress_name):
            return False
        if tree.topLevelItemCount() <= previous_count:
            return False
        latest = tree.topLevelItem(0)
        return (latest is not None and str(latest.text(0)).startswith(progress_name)
                and str(latest.text(1)).startswith("Finished ("))

    _require(_wait_until(completed), progress_name + " command completed in the Qt Progress dialog")


def _open_fixture(path, label):
    session = _session()
    print("[ LOAD ] Session.load:", path)
    _release_qt_wrappers()
    _require(bool(session.load(path)), label + " loaded through Session.load")
    _release_qt_wrappers()
    _require(session.isLoaded(), label + " is active")
    _require(_tb.normalize_path(session.filename()) == _tb.normalize_path(path),
             label + " is the active stage")


def _layer_id(path):
    return _tb.open_layer(path).identifier


def _layer_stack():
    return _tb.layer_stack(include_session_layers=False)


def _layer_authors_prim(identifier, path):
    layer = Sdf.Layer.Find(identifier) or Sdf.Layer.FindOrOpen(identifier)
    return bool(layer and layer.GetPrimAtPath(Sdf.Path(path)))


def _material_authored_completely(stage, identifier, material_path):
    if not _layer_authors_prim(identifier, material_path):
        return False
    shader_path = material_path + "/PreviewSurface"
    if not _layer_authors_prim(identifier, shader_path):
        return False
    material = UsdShade.Material.Get(stage, material_path)
    shader = UsdShade.Shader.Get(stage, shader_path)
    if not material or not shader or str(shader.GetIdAttr().Get()) != "UsdPreviewSurface":
        return False
    surface_output = material.GetSurfaceOutput()
    return bool(surface_output and surface_output.GetConnectedSource())


def _bound_material_path(stage, prim_path):
    if not stage:
        return ""
    prim = stage.GetPrimAtPath(prim_path)
    if not prim or not prim.IsValid():
        return ""
    material, _ = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()
    return str(material.GetPath()) if material else ""


def _dirty_identifiers():
    return [layer.identifier for layer in _tb.dirty_layers()]


def _assert_layer_api():
    session = _session()
    command = _command()
    command_methods = (
        "add_sublayer",
        "reorder_sublayers",
        "remove_layer",
        "set_layer_muted",
        "set_layer_permission_to_edit",
        "set_edit_layer",
        "new_prim",
        "new_mesh",
        "new_scope",
        "new_material",
        "bind_material",
    )
    session_methods = (
        "editLayer",
        "editLayers",
        "setEditLayer",
        "load",
        "saveLayer",
        "saveLayerAs",
        "copyLayerToFile",
        "isStateDirty",
        "saveToFile",
    )
    missing = ["command." + name for name in command_methods if not hasattr(command, name)]
    missing += ["session." + name for name in session_methods if not hasattr(session, name)]
    _require(not missing, "all layer command and save APIs are available" + (": " + repr(missing) if missing else ""))


def run_test_layers():
    print("[ INFO ] testlayers.py revision:", _TESTLAYERS_REVISION)
    print("\n=== Stageviz interactive layer workflow ===")
    print("The existing open stage will be replaced when the script loads its first fixture.")
    print("The fixtures are disposable and will be kept here for review:")
    directory = tempfile.mkdtemp(prefix="stageviz-layer-workflow-")
    print(directory)

    session = _session()
    if session.isLoaded():
        print("Current stage:", session.filename() or "Untitled")
    _assert_layer_api()
    fixtures = _make_fixtures(directory)
    for name, path in fixtures.items():
        print("[ FIXTURE ]", name, path)

    _open_fixture(fixtures["basic"], "basic three-layer stage")
    stage, layers = _layer_stack()
    root = stage.GetRootLayer()
    _require(len(layers) == 4, "basic stage has root plus three sublayers")
    _require(len(root.subLayerPaths) == 3, "root lists all three direct sublayers")
    tree = _open_layer_dialog()
    _assert_file_menu(save_enabled=False)
    _assert_window_title(dirty=False)
    def root_is_expanded():
        live_tree = _layer_tree()
        if live_tree is None:
            return False
        index = _layer_index(root.identifier, live_tree)
        try:
            return index.isValid() and live_tree.isExpanded(index)
        except RuntimeError:
            return False

    root_expanded = _wait_until(root_is_expanded, timeout=2.0)
    tree = _layer_tree()
    root_index = _layer_index(root.identifier, tree)
    if not root_expanded:
        _snapshot_qt_state("Layer Editor root expansion failure")
        try:
            print("[ STATE ] root index valid:", root_index.isValid())
            print("[ STATE ] root identifier:", root.identifier)
            live_tree = _layer_tree()
            if live_tree is None:
                print("[ STATE ] no live Layer Editor tree was found")
            else:
                print("[ STATE ] Layer Editor tree object:", live_tree.objectName(), live_tree.metaObject().className())
                print("[ STATE ] root row details:", _layer_row(root.identifier, live_tree))
            print("[ STATE ] Layer Editor tree rows:", _layer_tree_rows())
        except RuntimeError as error:
            print("[ STATE ] root expansion diagnostics failed:", repr(error))
    _require(root_expanded, "Layer Editor keeps the root row expanded")
    if root_index.isValid():
        tree.collapse(root_index)
        _process_events()
        _require(tree.isExpanded(_layer_index(root.identifier)), "collapsing the root row immediately re-expands it")
    _assert_layer_row(root.identifier, name_contains="01_basic.usda", dirty=False, bold=True,
                      visibility=False, locked=False, opinions=False)

    geometry_id = _layer_id(os.path.join(directory, "Geometry.usda"))
    lighting_id = _layer_id(os.path.join(directory, "Lighting.usda"))
    override_id = _layer_id(os.path.join(directory, "Overrides.usda"))
    command = _command()

    command.set_edit_layer(lighting_id)
    _run_command_and_wait("New prim", lambda: command.new_prim("/", "LightingSphere", "Sphere"))
    _require(_wait_until(lambda: _layer_authors_prim(lighting_id, "/LightingSphere")),
             "new_prim authors LightingSphere into the Lighting layer")
    _require(_wait_until(lambda: _session().editLayer() == lighting_id and lighting_id in _dirty_identifiers()),
             "edit target and authored dirty state update")
    _require(_session().editLayer() == lighting_id, "command.set_edit_layer changes the active layer")
    _require(lighting_id in _dirty_identifiers(), "new_prim authors into the active layer and marks it dirty")
    _require(_session().stage().GetPrimAtPath("/LightingSphere").IsValid(),
             "LightingSphere is composed into the current stage")
    _assert_layer_row(lighting_id, name_contains="Lighting.usda", dirty=True, bold=True,
                      visibility=True, locked=True, opinions=True)
    _assert_window_title(active_layer_name="Lighting.usda", dirty=True)
    _assert_file_menu(save_enabled=True)
    save_info = _action_info("fileLayerSave")
    _require(save_info is not None and save_info["enabled"],
             "Save Layer QAction is enabled for the dirty edit layer")
    _require(_trigger_action("fileLayerSave"), "Save Layer QAction triggers for the active edit layer")
    _require(_wait_until(lambda: lighting_id not in _dirty_identifiers()), "Save Layer QAction saves the active layer")
    _require(lighting_id not in _dirty_identifiers(), "File > Save Layer cleared the active layer's dirty state")
    _require(_layer_authors_prim(lighting_id, "/LightingSphere"),
             "saved Lighting layer retains its authored sphere")
    _require(_wait_until(lambda: "*" not in _viewer().windowTitle()),
             "window title clears its dirty marker after saving the active layer")
    _assert_layer_row(lighting_id, name_contains="Lighting.usda", dirty=False, bold=True,
                      visibility=True, locked=True, opinions=True)
    _assert_window_title(active_layer_name="Lighting.usda", dirty=False)
    _assert_file_menu(save_enabled=False)

    command.set_edit_layer(_session().stage().GetRootLayer().identifier)
    root_id = _session().stage().GetRootLayer().identifier
    _run_command_and_wait("New prim", lambda: command.new_prim("/", "RootSphere", "Sphere"))
    _require(_wait_until(lambda: _layer_authors_prim(root_id, "/RootSphere")),
             "new_prim authors RootSphere into the root layer")
    _run_command_and_wait("New prim", lambda: command.new_scope("/", "RootLooks"))
    _require(_wait_until(lambda: _layer_authors_prim(root_id, "/RootLooks")),
             "new_scope authors a root-layer material scope")
    _run_command_and_wait("New material", lambda: command.new_material("/RootLooks", "RootMaterial"))
    root_material_path = "/RootLooks/RootMaterial"
    _require(_wait_until(lambda: _material_authored_completely(stage, root_id, root_material_path)),
             "root layer material has its authored PreviewSurface shader and surface connection")

    command.set_edit_layer(geometry_id)
    _run_command_and_wait("New mesh", lambda: command.new_mesh("/", "GeometryMesh"))
    _require(_wait_until(lambda: _layer_authors_prim(geometry_id, "/GeometryMesh")),
             "new_mesh authors GeometryMesh into the Geometry layer")

    command.set_edit_layer(override_id)
    _run_command_and_wait("New prim", lambda: command.new_prim("/", "OverrideSphere", "Sphere"))
    _require(_wait_until(lambda: _layer_authors_prim(override_id, "/OverrideSphere")),
             "new_prim authors OverrideSphere into the Overrides layer")
    _run_command_and_wait("New prim", lambda: command.new_scope("/", "OverrideLooks"))
    _require(_wait_until(lambda: _layer_authors_prim(override_id, "/OverrideLooks")),
             "new_scope authors an Overrides-layer material scope")
    _run_command_and_wait("New material", lambda: command.new_material("/OverrideLooks", "OverrideMaterial"))
    override_material_path = "/OverrideLooks/OverrideMaterial"
    _require(_wait_until(lambda: _material_authored_completely(stage, override_id, override_material_path)),
             "Overrides material has its authored PreviewSurface shader and surface connection")

    command.set_edit_layer(geometry_id)
    _run_command_and_wait("Bind material", lambda: command.bind_material(["/GeometryMesh"], override_material_path))
    stage, _ = _layer_stack()
    _require(_wait_until(lambda: _bound_material_path(stage, "/GeometryMesh") == override_material_path),
             "bind_material authors a binding in Geometry to a material from Overrides")
    binding = stage.GetPrimAtPath("/GeometryMesh").GetRelationship("material:binding")
    binding_specs = binding.GetPropertyStack() if binding else []
    _require(any(spec.layer.identifier == geometry_id for spec in binding_specs),
             "Geometry layer contains the material:binding relationship opinion")

    _require(_wait_until(lambda: len(_dirty_identifiers()) >= 2),
             "authoring commands across the layer stack update dirty state")
    dirty = _dirty_identifiers()
    _require({root_id, geometry_id, override_id}.issubset(set(dirty)),
             "root, Geometry, and Overrides layers contain unsaved edits")
    print("[ STATE ] dirty layers before Save All:", dirty)
    _assert_layer_row(root_id, name_contains="01_basic.usda", dirty=True, bold=False,
                      visibility=False, locked=False, opinions=False)
    _assert_layer_row(geometry_id, name_contains="Geometry.usda", dirty=True, bold=True,
                      visibility=True, locked=True, opinions=True)
    _assert_layer_row(lighting_id, name_contains="Lighting.usda", dirty=False, bold=False,
                      visibility=True, locked=True, opinions=True)
    _assert_layer_row(override_id, name_contains="Overrides.usda", dirty=True, bold=False,
                      visibility=True, locked=True, opinions=True)
    _assert_window_title(active_layer_name="Geometry.usda", dirty=True)
    _assert_file_menu(save_enabled=True)
    save_all_info = _action_info("fileLayerSaveAll")
    _require(save_all_info is not None and save_all_info["enabled"], "Save All Layers QAction is enabled")
    _, _, _, QtWidgets = _qt()
    status_bar = _viewer().findChild(QtWidgets.QStatusBar, "statusbar")
    _require(status_bar is not None, "Viewer exposes its statusbar QObject")
    status_messages = []
    status_message_slot = lambda message: status_messages.append(str(message))
    status_bar.messageChanged.connect(status_message_slot)
    _require(_trigger_action("fileLayerSaveAll"), "Save All Layers QAction triggers")
    _require(_wait_until(lambda: not _dirty_identifiers()), "Save All Layers QAction saves every dirty layer")
    _require(not _dirty_identifiers(), "File > Save All Layers cleared all modified file-backed layers")
    _require(_wait_until(lambda: any("Saved all layers for" in message for message in status_messages)),
             "Save All Layers reports the saved stage and file count in the status bar")
    _require(_wait_until(lambda: "*" not in _viewer().windowTitle()),
             "window title clears its dirty marker after saving all layers")
    try:
        status_bar.messageChanged.disconnect(status_message_slot)
    except RuntimeError:
        # A stage/title refresh may replace the Viewer status bar after save.
        pass
    saved_status = next((message for message in reversed(status_messages)
                         if "Saved all layers for" in message), "")
    print("[ STATE ] Save All status bar:", saved_status)
    _assert_layer_row(root.identifier, name_contains="01_basic.usda", dirty=False, bold=False,
                      visibility=False, locked=False, opinions=False)
    _assert_layer_row(geometry_id, name_contains="Geometry.usda", dirty=False, bold=True,
                      visibility=True, locked=True, opinions=True)
    _assert_layer_row(override_id, name_contains="Overrides.usda", dirty=False,
                      visibility=True, locked=True, opinions=True)
    _require(_bound_material_path(_session().stage(), "/GeometryMesh") == override_material_path,
             "cross-layer material binding survives Save All Layers")
    _assert_window_title(active_layer_name="Geometry.usda", dirty=False)

    command.set_edit_layer(geometry_id)
    renamed_path = os.path.join(directory, "Geometry_Renamed.usda")
    _require(_session().saveLayerAs(geometry_id, renamed_path),
             "Session.saveLayerAs retargets the active Geometry layer without a file dialog")
    _require(os.path.isfile(renamed_path), "Session.saveLayerAs wrote the requested layer file")
    renamed_path_key = os.path.normcase(os.path.realpath(renamed_path))
    _, live_layers = _layer_stack()
    renamed_layers = [layer for layer in live_layers
                      if layer.realPath and os.path.normcase(os.path.realpath(layer.realPath)) == renamed_path_key]
    print("[ STATE ] edit layer after Save Layer As:", _session().editLayer())
    print("[ STATE ] live layer identifiers:", [layer.identifier for layer in live_layers])
    _require(len(renamed_layers) == 1,
             "Save Layer As retargeted the live stage to the requested file; live identifiers: "
             + repr([layer.identifier for layer in live_layers]))
    renamed_id = renamed_layers[0].identifier
    _require(_session().editLayer() == renamed_id, "Save Layer As kept the renamed layer as the edit target")
    _require(_wait_until(lambda: _layer_index(renamed_id).isValid()), "Layer Editor refreshes after Save Layer As")
    _require(_wait_until(lambda: (_action_info("fileLayerSave") or {}).get("enabled") is False),
             "Save Layer action state refreshes for the clean renamed edit layer")
    _assert_layer_row(renamed_id, name_contains="Geometry_Renamed.usda", dirty=False, bold=True,
                      visibility=True, locked=True, opinions=True)
    _assert_window_title(active_layer_name="Geometry_Renamed.usda", dirty=True)
    _assert_layer_row(root.identifier, name_contains="01_basic.usda", dirty=True, bold=False,
                      visibility=False, locked=False, opinions=False)
    _assert_file_menu(save_enabled=False)
    _require(_trigger_action("fileLayerSaveAll"), "Save All Layers clears the dirty root layer")
    _require(_wait_until(lambda: not _dirty_identifiers()), "Save All Layers saves the dirty parent layer")
    _require(_wait_until(lambda: "*" not in _viewer().windowTitle()),
             "window title clears after all dirty layers are saved")
    _assert_window_title(active_layer_name="Geometry_Renamed.usda", dirty=False)

    copy_path = os.path.join(directory, "Lighting_Copy.usda")
    command.set_edit_layer(lighting_id)
    _require(_session().copyLayerToFile(lighting_id, copy_path),
             "Session.copyLayerToFile writes a copy without a file dialog")
    _require(os.path.isfile(copy_path), "Session.copyLayerToFile wrote the requested copy")
    copy_id = _layer_id(copy_path)
    _require(_layer_authors_prim(copy_id, "/LightingSphere"),
             "Session.copyLayerToFile preserved the source layer's authored prims")
    _require(_session().editLayer() == lighting_id, "copying a layer left the active edit layer unchanged")
    _require(not _layer_index(copy_id).isValid(), "copying a layer does not add it to Layer Editor")
    _assert_layer_row(lighting_id, name_contains="Lighting.usda", bold=True)

    command.set_edit_layer(_session().stage().GetRootLayer().identifier)
    visible_icon_key = _row_icon_key(override_id, 1)
    print("[ STATE ] captured the contributing icon before muting the layer")
    command.set_layer_muted(override_id, True)
    stage, _ = _layer_stack()
    _require(_wait_until(lambda: stage.IsLayerMuted(override_id)), "mute notice reaches the live stage")
    _require(stage.IsLayerMuted(override_id), "set_layer_muted muted the selected sublayer")
    _require(_wait_until(lambda: "muted" in (_layer_row(override_id) or {}).get("visibility_tooltip", "").lower()),
             "Layer Editor refreshes the muted layer state")
    _snapshot_qt_state("Override layer muted")
    _require(_session().isStateDirty(), "muting a layer marks preserved stage state dirty")
    _require(_wait_until(lambda: "*" in _viewer().windowTitle()),
             "muting a layer marks the stage window title dirty")
    muted_row = _layer_row(override_id)
    _assert_layer_row(override_id, name_contains="Overrides.usda", visibility=True, locked=True, opinions=True)
    _require(muted_row is not None and _icon_present(muted_row["visible_icon"])
             and "muted" in muted_row["visibility_tooltip"].lower()
             and _icon_key(muted_row["visible_icon"]) != visible_icon_key,
             "muted layer displays the hidden-eye icon and tooltip")
    command.set_layer_muted(override_id, False)
    _require(_wait_until(lambda: not stage.IsLayerMuted(override_id)), "set_layer_muted can unmute the same layer")
    _require(_wait_until(lambda: "contributing" in (_layer_row(override_id) or {}).get("visibility_tooltip", "").lower()),
             "Layer Editor restores the contributing state after unmuting")
    unmuted_row = _layer_row(override_id)
    _require(unmuted_row is not None and _icon_present(unmuted_row["visible_icon"])
             and "contributing" in unmuted_row["visibility_tooltip"].lower()
             and _icon_key(unmuted_row["visible_icon"]) == visible_icon_key,
             "unmuting restores the visible-eye icon and tooltip")
    _require(_trigger_action("fileLayerSaveAll"), "Save All Layers saves preserved mute state")
    _require(_wait_until(lambda: not _session().isStateDirty()), "saving all layers clears preserved state dirtiness")
    _require(_wait_until(lambda: "*" not in _viewer().windowTitle()),
             "saving preserved layer state clears the stage title marker")

    unlocked_icon_key = _row_icon_key(lighting_id, 2)
    command.set_layer_permission_to_edit(lighting_id, False)
    lighting_layer = Sdf.Layer.Find(lighting_id)
    _require(_wait_until(lambda: not _layer_flag(lighting_layer, "permissionToEdit", "PermissionToEdit")),
             "lock command updates USD layer permission")
    _require(not _layer_flag(lighting_layer, "permissionToEdit", "PermissionToEdit"),
             "set_layer_permission_to_edit locks a sublayer")
    _require(_wait_until(lambda: _row_icon_key(lighting_id, 2) != unlocked_icon_key),
             "Layer Editor refreshes the closed-lock icon")
    locked_row = _layer_row(lighting_id)
    _assert_layer_row(lighting_id, name_contains="Lighting.usda", locked=True)
    _require(locked_row is not None and _icon_key(locked_row["lock_icon"]) != unlocked_icon_key,
             "locked layer displays the closed-lock icon")
    command.set_edit_layer(lighting_id)
    stage, _ = _layer_stack()
    command.new_xform("/World/Light", "LockedWriteProbe")
    _require(not stage.GetPrimAtPath("/World/Light/LockedWriteProbe"), "a locked active layer rejects a new prim edit")
    command.set_layer_permission_to_edit(lighting_id, True)
    _require(_wait_until(lambda: _layer_flag(lighting_layer, "permissionToEdit", "PermissionToEdit")),
             "unlock command updates USD layer permission")
    _require(_layer_flag(lighting_layer, "permissionToEdit", "PermissionToEdit"),
             "set_layer_permission_to_edit unlocks a sublayer")
    _require(_wait_until(lambda: _row_icon_key(lighting_id, 2) == unlocked_icon_key),
             "Layer Editor restores the open-lock icon")

    command.set_edit_layer(_session().stage().GetRootLayer().identifier)
    tree = _layer_tree()
    override_index = _layer_index(override_id, tree)
    _require(override_index.isValid(), "Layer Editor exposes Overrides.usda for selection")
    if override_index.isValid():
        tree.setCurrentIndex(override_index)
        _process_events()
    _, _, _, QtWidgets = _qt()
    layer_dialog = tree.window()
    select_button = layer_dialog.findChild(QtWidgets.QAbstractButton, "select")
    _require(select_button is not None and select_button.isEnabled(),
             "active Layer Editor Select button is enabled and has QObject name select")
    select_triggered = select_button is not None and select_button.isEnabled()
    if select_button is not None and select_button.isEnabled():
        select_button.click()
        select_button = None
    _process_events()
    _require(select_triggered, "active Layer Editor Select button triggers")
    _require(_wait_until(lambda: "/World/Override" in [str(path) for path in _session().paths()]),
             "Layer Editor Select button selects prims authored by Overrides.usda")
    _require(_session().editLayer() == _session().stage().GetRootLayer().identifier,
             "selecting layer opinions does not change the active edit target")

    tree = None
    _open_fixture(fixtures["nested"], "nested stage")
    stage, layers = _layer_stack()
    _require(len(layers) == 4, "nested stage composes root, parent, nested detail, and lighting layers")
    tree = _layer_tree()
    nested_parent_id = _layer_id(os.path.join(directory, "NestedModel.usda"))
    nested_detail_id = _layer_id(os.path.join(directory, "NestedDetail.usda"))
    _assert_layer_row(stage.GetRootLayer().identifier, name_contains="02_nested.usda", visibility=False,
                      locked=False, opinions=False)
    _assert_layer_row(nested_parent_id, name_contains="NestedModel.usda", visibility=True,
                      locked=True, opinions=True)
    parent_index = _layer_index(nested_parent_id, tree)
    detail_index = _layer_index(nested_detail_id, tree)
    _require(detail_index.isValid() and detail_index.parent() == parent_index,
             "Layer Editor represents nested sublayers in the correct parent-child hierarchy")
    tree.setExpanded(parent_index, True)
    tree.setCurrentIndex(parent_index)
    _process_events()
    _require(tree.isExpanded(_layer_index(nested_parent_id, tree)),
             "nested layer parent is expanded before saving")
    _require(_current_layer_identifier(tree) == nested_parent_id,
             "Layer Editor current row is the nested parent before saving")
    command = _command()
    command.set_edit_layer(nested_detail_id)
    _run_command_and_wait("New prim", lambda: command.new_scope("/", "SavedNestedScope"))
    _require(_layer_authors_prim(nested_detail_id, "/SavedNestedScope"),
             "nested-layer authoring creates a saveable opinion")
    _require(_trigger_action("fileLayerSave"), "File > Save Layer saves the nested edit layer")
    _require(_wait_until(lambda: nested_detail_id not in _dirty_identifiers()),
             "nested save action completes while the layer tree refreshes")
    _process_events()
    parent_index = _layer_index(nested_parent_id, tree)
    _require(parent_index.isValid() and tree.isExpanded(parent_index),
             "Layer Editor preserves nested expansion during Save Layer refresh")
    _require(_current_layer_identifier(tree) == nested_parent_id,
             "Layer Editor preserves its current row during Save Layer refresh")
    _require(_session().editLayer() == nested_detail_id,
             "saving the nested layer leaves it as the active edit layer")
    parent_eye = _icon_key(_layer_row(nested_parent_id)["visible_icon"])
    detail_eye = _icon_key(_layer_row(nested_detail_id)["visible_icon"])
    command = _command()
    command.set_layer_muted(nested_parent_id, True)
    _require(_wait_until(lambda: stage.IsLayerMuted(nested_parent_id)), "mute notice updates nested layer tree")
    _require(stage.IsLayerMuted(nested_parent_id), "muting a parent layer records the muted state")
    _require(_wait_until(lambda: _row_icon_key(nested_parent_id, 1) != parent_eye
                         and _row_icon_key(nested_detail_id, 1) != detail_eye),
             "Layer Editor refreshes hidden-eye styling on the muted parent and descendant")
    _require(_layer_index(nested_parent_id).isValid(), "muted parent remains visible in the Layer Editor")
    _require(_layer_index(nested_detail_id).isValid(), "child of muted parent remains visible in the Layer Editor")
    _require(_icon_key(_layer_row(nested_parent_id)["visible_icon"]) != parent_eye,
             "muted parent row displays the hidden-eye icon")
    _require(_icon_key(_layer_row(nested_detail_id)["visible_icon"]) != detail_eye,
             "muted descendant row displays inherited hidden-eye styling")
    command.set_layer_muted(nested_parent_id, False)
    _require(_wait_until(lambda: not stage.IsLayerMuted(nested_parent_id)), "unmute notice updates nested layer tree")
    _require(not stage.IsLayerMuted(nested_parent_id), "unmuting the ancestor restores its subtree")
    _require(not stage.IsLayerMuted(nested_detail_id), "the nested child was not independently muted")

    tree = None
    _open_fixture(fixtures["five"], "five-layer stage")
    stage, layers = _layer_stack()
    root = stage.GetRootLayer()
    _require(len(layers) == 6 and len(root.subLayerPaths) == 5, "five-layer fixture exposes five reorderable siblings")
    _require(_layer_children(root.identifier) == [
        _layer_id(os.path.join(directory, filename))
        for filename in ("A_Geometry.usda", "B_Materials.usda", "C_Lighting.usda", "D_Animation.usda", "E_Overrides.usda")
    ], "Qt Layer Editor displays all five sibling layers in USD strength order")
    python_added_path = os.path.join(directory, "Added_From_Python.usda")
    python_added_layer = Sdf.Layer.CreateNew(python_added_path)
    _require(bool(python_added_layer), "Sdf.Layer.CreateNew creates a USD layer file")
    _require(bool(python_added_layer.Save()), "new Python-created layer file is writable")
    command = _command()
    command.set_edit_layer(root.identifier)
    command.add_sublayer(python_added_path)
    python_added_id = _layer_id(python_added_path)
    _require(python_added_id in _session().editLayers(), "command.add_sublayer adds a new file under the edit layer")
    _require(len(root.subLayerPaths) == 6, "Python-added layer appears in the root sublayer list")
    original_order = list(root.subLayerPaths)
    a_id = _layer_id(os.path.join(directory, "A_Geometry.usda"))
    b_id = _layer_id(os.path.join(directory, "B_Materials.usda"))
    command.reorder_sublayers(root.identifier, b_id, a_id, False)
    first_layer = Sdf.Layer.FindRelativeToLayer(root, root.subLayerPaths[0])
    _require(first_layer and first_layer.identifier == b_id, "reorder_sublayers moves B before A")
    _require(_wait_until(lambda: _layer_children(root.identifier)[:2] == [b_id, a_id]),
             "Qt Layer Editor row order follows USD sublayer reorder")
    _checkpoint("Checkpoint 10 - sibling reorder state")
    command.reorder_sublayers(root.identifier, a_id, b_id, False)
    _require(root.subLayerPaths == original_order, "reorder_sublayers restores the original sibling order")

    command.set_edit_layer(root.identifier)
    remove_id = _layer_id(os.path.join(directory, "E_Overrides.usda"))
    command.remove_layer(remove_id)
    _require(_wait_until(lambda: remove_id not in _session().editLayers()),
             "remove_layer command refreshes the live stage")
    _require(remove_id not in _session().editLayers(), "remove_layer removes a sublayer from the stage")
    _require(os.path.isfile(os.path.join(directory, "E_Overrides.usda")), "remove_layer keeps the USD file on disk")
    _require(not _layer_index(remove_id).isValid(), "removed layer row disappears from the Qt Layer Editor")
    _checkpoint("Checkpoint 11 - removed layer state")

    tree = None
    _open_fixture(fixtures["root_only"], "root-only stage")
    stage, layers = _layer_stack()
    _require(len(layers) == 1 and layers[0] == stage.GetRootLayer(), "root-only stage shows exactly one root row")
    _assert_layer_row(stage.GetRootLayer().identifier, name_contains="04_root_only.usda", dirty=False,
                      bold=True, visibility=False, locked=False, opinions=False)
    _assert_file_menu(save_enabled=False)

    dialog_layer_path = os.path.join(directory, "Created_From_Layer_Editor.usda")
    _require(_layer_button_enabled("newMaterial"),
             "Layer Editor + button is enabled and has QObject name newMaterial")
    _checkpoint("Checkpoint 13 - create-layer destination")
    created_layer = Sdf.Layer.CreateNew(dialog_layer_path)
    _require(bool(created_layer) and bool(created_layer.Save()),
             "Layer Editor create workflow has a writable USD destination")
    _command().add_sublayer(dialog_layer_path)
    _require(os.path.isfile(dialog_layer_path), "new-layer workflow created the requested USD file")
    dialog_layer_id = _layer_id(dialog_layer_path)
    _require(dialog_layer_id in _session().editLayers(), "new-layer workflow added the created file to the stage")
    _require(_session().editLayer() == stage.GetRootLayer().identifier,
             "creating a layer did not change the active edit layer")
    _assert_layer_row(dialog_layer_id, name_contains="Created_From_Layer_Editor.usda", bold=False,
                      visibility=True, locked=True, opinions=False)

    existing_layer_path = _make_layer(directory, "Existing_From_Disk.usda", "ExistingDisk", "existing")
    _require(_layer_button_enabled("load"),
             "Layer Editor folder button is enabled and has QObject name load")
    _checkpoint("Checkpoint 14 - existing-layer source")
    _command().add_sublayer(existing_layer_path)
    existing_layer_id = _layer_id(existing_layer_path)
    _require(existing_layer_id in _session().editLayers(), "existing-layer workflow added the source file to the stage")
    _assert_layer_row(existing_layer_id, name_contains="Existing_From_Disk.usda", bold=False,
                      visibility=True, locked=True, opinions=True)

    _require(_session().saveToFile(_session().filename()), "Python Session.saveToFile saves all fixture layers")
    tree = None
    _release_qt_wrappers()
    _require(_session().newStage(), "Session.newStage creates the final anonymous-stage scenario")
    command = _command()
    command.add_sublayer(existing_layer_path)
    command.new_xform("/", "UnsavedTestPrim")
    stage, layers = _layer_stack()
    root_id = stage.GetRootLayer().identifier
    _require(len(layers) == 2 and root_id.startswith("anon:"),
             "new unsaved stage has an anonymous root and a child layer")
    _require(_wait_until(lambda: _layer_index(stage.GetRootLayer().identifier).isValid()),
             "Qt Layer Editor refreshes for an anonymous stage")
    tree = _layer_tree()
    root_index = _layer_index(root_id, tree)
    tree.setExpanded(root_index, True)
    tree.setCurrentIndex(root_index)
    _process_events()
    _require(tree.isExpanded(root_index), "anonymous root is expanded before the first save")
    _require(_current_layer_identifier(tree) == root_id, "anonymous root row is selected before the first save")
    _assert_layer_row(root_id, name_contains="Untitled", dirty=True, bold=True,
                      visibility=False, locked=False, opinions=False)
    _assert_layer_row(existing_layer_id, name_contains="Existing_From_Disk.usda", bold=False,
                      visibility=True, locked=True, opinions=True)
    _assert_window_title(active_layer_name="Untitled", dirty=True)
    _assert_file_menu(save_enabled=True)
    unsaved_path = os.path.join(directory, "Untitled_Saved.usda")
    _checkpoint("Checkpoint 15 - anonymous-root save state")
    _require(_session().saveLayerAs(root_id, unsaved_path),
             "Save Layer As saves the anonymous root through the Session API")
    _require(os.path.isfile(unsaved_path), "anonymous root layer was saved to the chosen file")
    _require(not _dirty_identifiers(), "saving the anonymous root cleared the dirty marker")
    _require(os.path.basename(_session().filename()) == os.path.basename(unsaved_path),
             "saving the anonymous root established the stage filename")
    saved_root = _session().stage().GetRootLayer()
    saved_root_id = saved_root.identifier
    _require(_wait_until(lambda: _layer_index(saved_root_id).isValid()),
             "Layer Editor maps the root row to its saved file identifier")
    tree = _layer_tree()
    saved_root_index = _layer_index(saved_root_id)
    _require(tree is not None and saved_root_index.isValid() and tree.isExpanded(saved_root_index),
             "Layer Editor preserves root expansion across anonymous-to-file-backed save")
    _require(_current_layer_identifier(tree) == saved_root_id,
             "Layer Editor preserves the current root row across anonymous-to-file-backed save")
    _assert_layer_row(saved_root.identifier, name_contains="Untitled_Saved.usda", dirty=False, bold=True,
                      visibility=False, locked=False, opinions=False)
    _assert_window_title(dirty=False)
    _assert_file_menu(save_enabled=False)

    print("\n=== Layer workflow and Qt UI checks finished ===")
    print("Fixture directory:", directory)
    print("Former interactive checkpoints ran automatically through Qt snapshots and Session/command APIs.")
    if _UI_ACTION_FAILURES:
        print("Layer UI action failures:", _UI_ACTION_FAILURES)
        return False
    return True


# Stageviz's embedded editor may execute scripts with a non-__main__ namespace.
# Keep this as a direct-run script; test.py also executes it through runpy.
_TEST_RESULT = bool(run_test_layers())
