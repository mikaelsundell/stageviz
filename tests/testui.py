# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""
StageTree interactive UI diagnostic.

Run from the Stageviz Python editor.

Policy assumptions:
    - StageTree never displays payload contents.
    - With payload policy enabled:
        payload rows show load-state checkboxes.
    - With policy All / payload policy disabled:
        payload rows do not show checkboxes.

The test proceeds one step at a time and stops on the first failure.

Important:
    - USD paths are treated as stable test handles.
    - QTreeWidgetItem wrappers are reacquired after tree updates so the
      diagnostic does not retain Python wrappers to deleted C++ items.
"""

import os
import tempfile
import time
import gc

from pxr import Gf, Kind, Sdf, Usd, UsdGeom

import stageviz

from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QLineEdit, QTreeWidget
from PySide6.QtCore import QModelIndex, QItemSelectionModel
from PySide6.QtWidgets import QAbstractItemView


# ----------------------------------------------------------------------
# Basic helpers
# ----------------------------------------------------------------------

def process_events():
    app = QApplication.instance()

    if app:
        app.processEvents()

    QTest.qWait(30)

    if app:
        app.processEvents()



def release_qt_wrappers():
    # The diagnostic uses QModelIndex rather than QTreeWidgetItem wrappers.
    # Keep this as a small event/GC barrier around full session reloads.
    process_events()
    gc.collect()
    process_events()



def wait_until(predicate, timeout=3.0, interval_ms=25):
    end = time.time() + timeout

    while time.time() < end:
        process_events()

        try:
            if predicate():
                process_events()
                return True
        except Exception:
            pass

        QTest.qWait(interval_ms)

    return False


def stage():
    return stageviz.session().stage()


def prim(path):
    current_stage = stage()

    if not current_stage:
        return None

    return current_stage.GetPrimAtPath(path)


def exists(path):
    value = prim(path)
    return bool(value and value.IsValid())


def divider():
    print()
    print("=" * 72)


def step(number, title, expected):
    divider()
    print(f"STEP {number} - {title}")
    print()
    print("EXPECTED UI:")
    print(expected)
    print()


def passed(message):
    print(f"[ OK ] {message}")


def stop(message):
    print()
    print(f"[FAIL] {message}")
    print()
    print("STOPPING HERE.")
    print("Compare the StageTree with the EXPECTED UI above.")
    print()

    raise RuntimeError(message)


def require(condition, message):
    if not condition:
        stop(message)

    passed(message)


TIMINGS = []


def timed_wait(label, predicate, timeout=5.0):
    """Measure from immediately after an action until USD + StageTree are stable."""
    started = time.perf_counter()
    success = wait_until(
        predicate,
        timeout=timeout,
    )
    elapsed_ms = (time.perf_counter() - started) * 1000.0

    TIMINGS.append(
        (
            label,
            elapsed_ms,
            success,
        )
    )

    status = "OK" if success else "TIMEOUT"
    print(
        f"[TIME] {label}: "
        f"{elapsed_ms:.2f} ms "
        f"({status})"
    )

    return success, elapsed_ms


def require_timed(label, predicate, message, timeout=5.0):
    success, elapsed_ms = timed_wait(
        label,
        predicate,
        timeout=timeout,
    )

    require(
        success,
        message,
    )

    return elapsed_ms


def tree_item_count(tree):
    return sum(
        1
        for _ in _walk_model_indexes(tree)
    )


def snapshot_tree_state(tree, paths):
    """
    Snapshot UI state for stable paths.

    Checkbox state is captured as the actual Qt::CheckStateRole value so
    namespace edits can be checked for accidental checkbox changes.
    """
    snapshot = {}

    for path in paths:
        item = find_item_by_path(
            tree,
            path,
        )

        require(
            item is not None,
            f"{path} exists before UI-state snapshot",
        )

        snapshot[path] = {
            "expanded": item.isExpanded(),
            "selected": item.isSelected(),
            "checkbox": checkbox_state_for_path(
                tree,
                path,
            ),
        }

    return snapshot


def require_tree_state_preserved(
    tree,
    snapshot,
    path_remap=None,
    check_selection=True,
):
    path_remap = path_remap or {}

    for old_path, expected in snapshot.items():
        path = path_remap.get(
            old_path,
            old_path,
        )

        item = find_item_by_path(
            tree,
            path,
        )

        require(
            item is not None,
            f"{path} exists after UI-state restore",
        )

        require(
            item.isExpanded()
            == expected["expanded"],
            f"{path} preserves expanded={expected['expanded']}",
        )

        if check_selection:
            require(
                item.isSelected()
                == expected["selected"],
                f"{path} preserves selected={expected['selected']}",
            )

        require(
            checkbox_state_for_path(
                tree,
                path,
            )
            == expected["checkbox"],
            f"{path} preserves checkbox state",
        )


def print_timing_summary():
    divider()
    print("TIMING SUMMARY")
    print()

    if not TIMINGS:
        print("No timed operations recorded.")
        return

    for label, elapsed_ms, success in TIMINGS:
        status = "OK" if success else "TIMEOUT"
        print(
            f"{label:<46} "
            f"{elapsed_ms:10.2f} ms  "
            f"{status}"
        )


# ----------------------------------------------------------------------
# Fixture
# ----------------------------------------------------------------------

def define_xform(usd_stage, path):
    return UsdGeom.Xform.Define(
        usd_stage,
        path,
    ).GetPrim()


def create_payload_file(filename, root_name):
    usd_stage = Usd.Stage.CreateNew(filename)

    root = define_xform(
        usd_stage,
        f"/{root_name}",
    )

    Usd.ModelAPI(root).SetKind(
        Kind.Tokens.component
    )

    cube = UsdGeom.Cube.Define(
        usd_stage,
        f"/{root_name}/Geom",
    )

    cube.CreateSizeAttr(2.0)

    define_xform(
        usd_stage,
        f"/{root_name}/Geom/Detail",
    )

    usd_stage.SetDefaultPrim(root)
    usd_stage.GetRootLayer().Save()


def create_fixture():
    root = tempfile.mkdtemp(
        prefix="stageviz_stagetree_ui_"
    )

    payload_a = os.path.join(
        root,
        "payload_a.usda",
    )

    payload_b = os.path.join(
        root,
        "payload_b.usda",
    )

    payload_c = os.path.join(
        root,
        "payload_c.usda",
    )

    main = os.path.join(
        root,
        "stagetree_test.usda",
    )

    create_payload_file(
        payload_a,
        "PayloadA",
    )

    create_payload_file(
        payload_b,
        "PayloadB",
    )

    create_payload_file(
        payload_c,
        "PayloadC",
    )

    usd_stage = Usd.Stage.CreateNew(main)

    world = define_xform(
        usd_stage,
        "/World",
    )

    usd_stage.SetDefaultPrim(world)

    define_xform(
        usd_stage,
        "/World/Assembly",
    )

    define_xform(
        usd_stage,
        "/World/Assembly/Door",
    )

    define_xform(
        usd_stage,
        "/World/Assembly/Door/Handle",
    )

    define_xform(
        usd_stage,
        "/World/Assembly/Wheel",
    )

    payload = define_xform(
        usd_stage,
        "/World/PayloadA",
    )

    payload.GetPayloads().AddPayload(
        os.path.basename(payload_a),
        "/PayloadA",
    )

    payload = define_xform(
        usd_stage,
        "/World/PayloadB",
    )

    payload.GetPayloads().AddPayload(
        os.path.basename(payload_b),
        "/PayloadB",
    )

    UsdGeom.Xformable(payload).AddTranslateOp().Set(
        Gf.Vec3d(
            5.0,
            0.0,
            0.0,
        )
    )

    payload = define_xform(
        usd_stage,
        "/World/PayloadC",
    )

    payload.GetPayloads().AddPayload(
        os.path.basename(payload_c),
        "/PayloadC",
    )

    UsdGeom.Xformable(payload).AddTranslateOp().Set(
        Gf.Vec3d(
            10.0,
            0.0,
            0.0,
        )
    )

    usd_stage.GetRootLayer().Save()

    return root, main


def create_performance_fixture(
    root,
    branch_count=80,
    children_per_branch=15,
):
    """
    Create a moderately large normal hierarchy specifically for namespace
    performance/state testing. It is separate from the functional fixture so
    the normal diagnostic remains easy to inspect visually.
    """
    main = os.path.join(
        root,
        "stagetree_performance_test.usda",
    )

    usd_stage = Usd.Stage.CreateNew(main)

    world = define_xform(
        usd_stage,
        "/World",
    )

    usd_stage.SetDefaultPrim(world)

    define_xform(
        usd_stage,
        "/World/Source",
    )

    define_xform(
        usd_stage,
        "/World/Target",
    )

    define_xform(
        usd_stage,
        "/World/Source/Moving",
    )

    define_xform(
        usd_stage,
        "/World/Source/Moving/Child",
    )

    for branch_index in range(branch_count):
        branch_path = (
            f"/World/Branch_{branch_index:03d}"
        )

        define_xform(
            usd_stage,
            branch_path,
        )

        for child_index in range(children_per_branch):
            define_xform(
                usd_stage,
                (
                    f"{branch_path}/"
                    f"Item_{child_index:03d}"
                ),
            )

    usd_stage.GetRootLayer().Save()

    return main



def create_25k_benchmark_fixture(
    root,
    target_prim_count=25000,
    branch_count=100,
):
    """
    Create a fresh, non-payload USD layer containing exactly
    target_prim_count authored prims below /World.

    Use Sdf directly rather than repeatedly calling UsdStage.DefinePrim().
    This avoids forcing USD stage recomposition thousands of times while the
    benchmark fixture itself is being authored.
    """
    main = os.path.join(
        root,
        "stagetree_25k_benchmark.usda",
    )

    started = time.perf_counter()

    layer = Sdf.Layer.CreateNew(main)

    def define_xform_spec(path):
        spec = Sdf.CreatePrimInLayer(
            layer,
            path,
        )
        spec.specifier = Sdf.SpecifierDef
        spec.typeName = "Xform"
        return spec

    define_xform_spec("/World")
    layer.defaultPrim = "World"

    define_xform_spec("/World/Source")
    define_xform_spec("/World/Source/Moving")
    define_xform_spec("/World/Source/Moving/Child")
    define_xform_spec("/World/Target")

    fixed_count = 5
    remaining = max(
        0,
        int(target_prim_count) - fixed_count,
    )

    branch_count = max(
        1,
        min(
            int(branch_count),
            remaining if remaining else 1,
        ),
    )

    leaf_budget = max(
        0,
        remaining - branch_count,
    )

    base_children = (
        leaf_budget // branch_count
        if branch_count
        else 0
    )

    extra_children = (
        leaf_budget % branch_count
        if branch_count
        else 0
    )

    last_child_paths = []

    for branch_index in range(branch_count):
        branch_path = (
            f"/World/Branch_{branch_index:03d}"
        )

        define_xform_spec(
            branch_path,
        )

        child_count = (
            base_children
            + (
                1
                if branch_index < extra_children
                else 0
            )
        )

        last_child_path = ""

        for child_index in range(child_count):
            last_child_path = (
                f"{branch_path}/"
                f"Item_{child_index:03d}"
            )

            define_xform_spec(
                last_child_path,
            )

        last_child_paths.append(
            last_child_path
        )

    layer.Save()

    elapsed = (
        time.perf_counter()
        - started
    )

    return (
        main,
        elapsed,
        last_child_paths,
    )


def find_stage_tree():
    app = QApplication.instance()

    if app is None:
        raise RuntimeError(
            "No QApplication instance"
        )

    # StageTree is exposed through PySide as its QTreeWidget base wrapper,
    # so do not rely on __class__.__name__ == "StageTree".
    for widget in app.allWidgets():
        try:
            if (
                isinstance(widget, QTreeWidget)
                and widget.objectName() == "stageTree"
            ):
                return widget
        except RuntimeError:
            continue

    # Fallback: identify a QTreeWidget whose model contains /World.
    # This stays QModelIndex-based and does not create QTreeWidgetItem wrappers.
    for widget in app.allWidgets():
        try:
            if not isinstance(widget, QTreeWidget):
                continue

            model = widget.model()
            if model is None:
                continue

            stack = [QModelIndex()]

            while stack:
                parent = stack.pop()
                row_count = model.rowCount(parent)

                for row in range(row_count):
                    index = model.index(
                        row,
                        0,
                        parent,
                    )

                    if not index.isValid():
                        continue

                    if _path_from_index(index) == "/World":
                        return widget

                    stack.append(index)

        except RuntimeError:
            continue

    return None



class TreeIndexRef:
    """Lightweight, non-owning wrapper around a QModelIndex."""

    def __init__(self, tree, index):
        self.tree = tree
        self.index = index

    def data(self, column, role):
        if not self.index.isValid():
            return None

        index = self.index
        if column != index.column():
            index = index.sibling(index.row(), column)

        return index.data(role)

    def text(self, column):
        value = self.data(column, Qt.DisplayRole)
        return "" if value is None else str(value)

    def childCount(self):
        if not self.index.isValid():
            return 0
        return self.tree.model().rowCount(self.index)

    def child(self, row):
        if not self.index.isValid():
            return None

        index = self.tree.model().index(row, 0, self.index)
        if not index.isValid():
            return None

        return TreeIndexRef(self.tree, index)

    def isExpanded(self):
        return bool(self.index.isValid() and self.tree.isExpanded(self.index))

    def setExpanded(self, expanded):
        if self.index.isValid():
            self.tree.setExpanded(self.index, expanded)

    def isSelected(self):
        selection_model = self.tree.selectionModel()
        return bool(
            self.index.isValid()
            and selection_model
            and selection_model.isSelected(self.index)
        )

    def setSelected(self, selected):
        selection_model = self.tree.selectionModel()
        if not self.index.isValid() or not selection_model:
            return

        flags = (
            QItemSelectionModel.Select
            if selected
            else QItemSelectionModel.Deselect
        ) | QItemSelectionModel.Rows

        selection_model.select(self.index, flags)

    def checkState(self, column):
        value = self.data(column, Qt.CheckStateRole)
        if value is None:
            return Qt.Unchecked

        try:
            return Qt.CheckState(value)
        except Exception:
            return value


def _path_from_index(index):
    if not index.isValid():
        return ""

    for role in range(
        int(Qt.UserRole),
        int(Qt.UserRole) + 2048,
    ):
        try:
            value = index.data(role)
        except Exception:
            continue

        if (
            isinstance(value, str)
            and value.startswith("/")
        ):
            return value

    return ""


def item_path(item):
    if item is None:
        return ""

    if isinstance(item, TreeIndexRef):
        return _path_from_index(item.index)

    # Do not intentionally create QTreeWidgetItem wrappers in this diagnostic.
    # This fallback only keeps the helper tolerant of an unexpected caller.
    try:
        for role in range(
            int(Qt.UserRole),
            int(Qt.UserRole) + 2048,
        ):
            value = item.data(
                0,
                role,
            )

            if (
                isinstance(value, str)
                and value.startswith("/")
            ):
                return value
    except Exception:
        pass

    return ""


def _walk_model_indexes(tree):
    model = tree.model()
    if model is None:
        return

    def walk(parent_index):
        row_count = model.rowCount(parent_index)

        for row in range(row_count):
            index = model.index(
                row,
                0,
                parent_index,
            )

            if not index.isValid():
                continue

            yield index
            yield from walk(index)

    yield from walk(QModelIndex())


def walk_items(tree):
    return tuple(
        TreeIndexRef(
            tree,
            index,
        )
        for index in _walk_model_indexes(tree)
    )


def find_item_by_path(tree, path):
    for index in _walk_model_indexes(tree):
        if _path_from_index(index) == path:
            return TreeIndexRef(
                tree,
                index,
            )

    return None


def find_item_by_name(tree, name):
    for index in _walk_model_indexes(tree):
        value = index.data(Qt.DisplayRole)

        if value is not None and str(value) == name:
            return TreeIndexRef(
                tree,
                index,
            )

    return None


def path_exists_in_tree(tree, path):
    return find_item_by_path(
        tree,
        path,
    ) is not None


def path_is_expanded(tree, path):
    item = find_item_by_path(
        tree,
        path,
    )

    return bool(
        item
        and item.isExpanded()
    )


def path_is_selected(tree, path):
    item = find_item_by_path(
        tree,
        path,
    )

    return bool(
        item
        and item.isSelected()
    )


def set_path_expanded(tree, path, expanded=True):
    def apply():
        item = find_item_by_path(
            tree,
            path,
        )

        if item is None:
            return False

        item.setExpanded(expanded)

        return item.isExpanded() == expanded

    require(
        wait_until(
            apply,
            timeout=3.0,
        ),
        f"{path} expansion state set to {expanded}",
    )


def current_path(tree):
    index = tree.currentIndex()

    if not index.isValid():
        return ""

    return _path_from_index(index)


def select_path(tree, path):
    def apply_selection():
        item = find_item_by_path(
            tree,
            path,
        )

        if item is None:
            return False

        index = item.index
        selection_model = tree.selectionModel()

        if selection_model is None:
            return False

        selection_model.clearSelection()
        selection_model.select(
            index,
            QItemSelectionModel.ClearAndSelect
            | QItemSelectionModel.Rows,
        )

        tree.setCurrentIndex(index)
        tree.scrollTo(
            index,
            QAbstractItemView.PositionAtCenter,
        )
        tree.setFocus()

        # Selection can trigger a StageTree refresh. Process that refresh here,
        # then reacquire the live index before deciding the selection is stable.
        process_events()

        item = find_item_by_path(
            tree,
            path,
        )

        if item is None:
            return False

        index = item.index
        selection_model = tree.selectionModel()

        return bool(
            selection_model
            and selection_model.isSelected(index)
            and current_path(tree) == path
        )

    require(
        wait_until(
            apply_selection,
            timeout=3.0,
        ),
        f"{path} becomes selected/current",
    )


def has_checkbox(item):
    if item is None:
        return False

    return item.data(
        0,
        Qt.CheckStateRole,
    ) is not None


def has_checkbox_for_path(tree, path):
    item = find_item_by_path(
        tree,
        path,
    )

    return bool(
        item
        and has_checkbox(item)
    )


def checkbox_state_for_path(tree, path):
    item = find_item_by_path(
        tree,
        path,
    )

    if item is None:
        return None

    value = item.data(
        0,
        Qt.CheckStateRole,
    )

    if value is None:
        return None

    try:
        return Qt.CheckState(value)
    except Exception:
        return value


def require_checkbox(tree, path, expected=True, message=None):
    if message is None:
        message = (
            f"{path} has checkbox"
            if expected
            else f"{path} has no checkbox"
        )

    require(
        wait_until(
            lambda: (
                has_checkbox_for_path(
                    tree,
                    path,
                )
                == expected
            ),
            timeout=3.0,
        ),
        message,
    )


def require_check_state(tree, path, expected, message):
    require_checkbox(
        tree,
        path,
        True,
        f"{path} has checkbox before checking state",
    )

    require(
        wait_until(
            lambda: (
                checkbox_state_for_path(
                    tree,
                    path,
                )
                == expected
            ),
            timeout=3.0,
        ),
        message,
    )


def require_no_checkboxes_anywhere(tree, message):
    def none_present():
        for index in _walk_model_indexes(tree):
            if index.data(Qt.CheckStateRole) is not None:
                return False

        return True

    require(
        wait_until(
            none_present,
            timeout=3.0,
        ),
        message,
    )


def dump_tree(tree):
    print()
    print("ACTUAL TREE:")
    print()

    model = tree.model()
    selection_model = tree.selectionModel()

    if model is None:
        return

    def walk(parent_index, depth):
        row_count = model.rowCount(parent_index)

        for row in range(row_count):
            index = model.index(
                row,
                0,
                parent_index,
            )

            if not index.isValid():
                continue

            path = _path_from_index(index)
            value = index.data(Qt.DisplayRole)
            name = "" if value is None else str(value)

            expanded = (
                "+"
                if tree.isExpanded(index)
                else "-"
            )

            selected = (
                "*"
                if (
                    selection_model
                    and selection_model.isSelected(index)
                )
                else " "
            )

            check = ""
            check_value = index.data(Qt.CheckStateRole)

            if check_value is not None:
                try:
                    state = Qt.CheckState(check_value)
                except Exception:
                    state = check_value

                if state == Qt.Checked:
                    check = "[x] "
                elif state == Qt.PartiallyChecked:
                    check = "[-] "
                else:
                    check = "[ ] "

            print(
                f"{selected}"
                f"{'  ' * depth}"
                f"{expanded} "
                f"{check}"
                f"{name} "
                f"{path}"
            )

            walk(
                index,
                depth + 1,
            )

    walk(
        QModelIndex(),
        0,
    )


# ----------------------------------------------------------------------
# Policy helpers
# ----------------------------------------------------------------------

def payload_policy_enabled(tree):
    item = find_item_by_path(
        tree,
        "/World/PayloadA",
    )

    if not item:
        return False

    return has_checkbox(item)


def policy_name(tree):
    if payload_policy_enabled(tree):
        return "Payload"
    return "All"


def verify_payload_is_leaf(tree, path):
    require(
        path_exists_in_tree(
            tree,
            path,
        ),
        f"{path} exists in StageTree",
    )

    require(
        wait_until(
            lambda: (
                (
                    find_item_by_path(
                        tree,
                        path,
                    )
                ) is not None
                and find_item_by_path(
                    tree,
                    path,
                ).childCount() == 0
            ),
            timeout=3.0,
        ),
        f"{path} remains a leaf in StageTree",
    )



def undo_if_available():
    for name in ("undo", "undo_command"):
        if hasattr(stageviz.command, name):
            getattr(stageviz.command, name)()
            process_events()
            return True

    stack = getattr(stageviz, "command_stack", None)
    if stack is not None and hasattr(stack, "undo"):
        stack.undo()
        process_events()
        return True

    print("[skip] undo is not bound in this build")
    return False


def reload_fixture(main, policy):
    # Drop all temporary PySide wrappers before Session::load() causes the
    # previous StageTree items to be destroyed.
    release_qt_wrappers()

    session = stageviz.session()

    require(
        session.load(
            main,
            policy,
        ),
        "fixture reloaded",
    )

    # Let the queued StageTree/session updates finish before creating any new
    # wrappers for the rebuilt tree.
    process_events()
    release_qt_wrappers()

    require(
        wait_until(
            lambda: bool(
                stage()
                and exists("/World")
            ),
            timeout=5.0,
        ),
        "reloaded stage is available",
    )

    tree = find_stage_tree()

    require(
        tree is not None,
        "StageTree found after reload",
    )

    process_events()
    return tree



def test_initial_tree(tree):
    step(
        1,
        "Initial tree",
        """
  World
    Assembly
      Door
        Handle
      Wheel
    PayloadA
    PayloadB
    PayloadC

If policy is Payload:
    PayloadA/B/C are StageTree leaf rows with unchecked checkboxes.
    Payload contents are not traversed in the tree.

If policy is All:
    PayloadA/B/C have no checkboxes.
    Loaded payload contents are traversed and shown below the payload rows.

All three payloads should initially be unloaded in this LoadNone fixture.
""",
    )

    dump_tree(tree)

    require(
        find_item_by_path(
            tree,
            "/World",
        ) is not None,
        "World exists in StageTree",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly",
        ) is not None,
        "Assembly exists in StageTree",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/Door",
        ) is not None,
        "Door exists in StageTree",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/Door/Handle",
        ) is not None,
        "Handle exists in StageTree",
    )

    current_policy = policy_name(tree)

    print()
    print("Detected StageTree policy:")
    print(current_policy)
    print()

    if current_policy == "Payload":
        for path in (
            "/",
            "/World",
            "/World/Assembly",
            "/World/Assembly/Door",
            "/World/Assembly/Door/Handle",
            "/World/Assembly/Wheel",
            "/World/PayloadA",
            "/World/PayloadB",
            "/World/PayloadC",
        ):
            require_checkbox(
                tree,
                path,
                True,
                f"{path} has checkbox in Payload policy",
            )

    for path in (
        "/World/PayloadA",
        "/World/PayloadB",
        "/World/PayloadC",
    ):
        verify_payload_is_leaf(
            tree,
            path,
        )

        value = prim(path)

        require(
            bool(
                value
                and not value.IsLoaded()
            ),
            f"{path} starts unloaded",
        )

        if current_policy == "Payload":
            require(
                checkbox_state_for_path(tree, path) is not None,
                f"{path} has checkbox in Payload policy",
            )

            require(
                checkbox_state_for_path(tree, path) == Qt.Unchecked,
                f"{path} checkbox starts unchecked",
            )

        else:
            require(
                checkbox_state_for_path(tree, path) is None,
                f"{path} has no checkbox in All policy",
            )


def test_expansion(tree):
    step(
        2,
        "Expand normal hierarchy",
        """
Expected visible hierarchy:

  World
    Assembly
      Door
        Handle
      Wheel

World, Assembly and Door should be expanded.

In Payload policy, payload rows remain StageTree leaf items even when loaded.
""",
    )

    require(
        find_item_by_path(
            tree,
            "/World",
        ) is not None,
        "World found",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly",
        ) is not None,
        "Assembly found",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/Door",
        ) is not None,
        "Door found",
    )

    find_item_by_path(
        tree,
        "/World",
    ).setExpanded(True)

    find_item_by_path(
        tree,
        "/World/Assembly",
    ).setExpanded(True)

    find_item_by_path(
        tree,
        "/World/Assembly/Door",
    ).setExpanded(True)

    process_events()

    dump_tree(tree)

    require(
        find_item_by_path(
            tree,
            "/World",
        ).isExpanded(),
        "World is expanded",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly",
        ).isExpanded(),
        "Assembly is expanded",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/Door",
        ).isExpanded(),
        "Door is expanded",
    )


def test_payload_a_load(tree):
    current_policy = policy_name(tree)

    step(
        3,
        "Load PayloadA",
        f"""
Current policy:
    {current_policy}

Before load:
    PayloadA

After load:
    PayloadA

USD should contain:
    /World/PayloadA/Geom
    /World/PayloadA/Geom/Detail

If policy is Payload:
    PayloadA remains a StageTree leaf.
    PayloadA/Geom and Detail stay hidden from StageTree.
    PayloadA checkbox becomes checked.

If policy is All:
    PayloadA has no checkbox.
    PayloadA/Geom and Detail are traversed and shown.
""",
    )

    print("ACTION: loading /World/PayloadA")
    print()

    stageviz.command.load_payloads(
        ["/World/PayloadA"]
    )

    require(
        wait_until(
            lambda: bool(
                prim("/World/PayloadA")
                and prim(
                    "/World/PayloadA"
                ).IsLoaded()
            ),
            timeout=5.0,
        ),
        "USD PayloadA is loaded",
    )

    require(
        exists(
            "/World/PayloadA/Geom"
        ),
        "USD contains PayloadA/Geom",
    )

    require(
        exists(
            "/World/PayloadA/Geom/Detail"
        ),
        "USD contains PayloadA/Geom/Detail",
    )

    process_events()
    dump_tree(tree)

    if current_policy == "Payload":
        verify_payload_is_leaf(
            tree,
            "/World/PayloadA",
        )

        require(
            find_item_by_path(
                tree,
                "/World/PayloadA/Geom",
            ) is None,
            "PayloadA/Geom is intentionally hidden from StageTree in Payload policy",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadA",
            ) == Qt.Checked,
            "PayloadA checkbox becomes checked",
        )

        require_check_state(
            tree,
            "/World",
            Qt.PartiallyChecked,
            "World becomes partially checked when only some payloads are loaded",
        )

        require_check_state(
            tree,
            "/",
            Qt.PartiallyChecked,
            "root becomes partially checked when only some payloads are loaded",
        )

    else:
        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadA",
            ) is None,
            "PayloadA still has no checkbox in All policy",
        )

        require(
            path_exists_in_tree(tree, "/World/PayloadA/Geom"),
            "PayloadA/Geom is visible in All policy",
        )


def test_payload_b_load(tree):
    current_policy = policy_name(tree)

    step(
        4,
        "Load PayloadB",
        f"""
Current policy:
    {current_policy}

Expected StageTree:

    PayloadA
    PayloadB
    PayloadC

PayloadA and PayloadB are loaded in USD.

If policy is Payload:
    PayloadA/B/C remain StageTree leaf rows.
    PayloadA = checked
    PayloadB = checked
    PayloadC = unchecked

If policy is All:
    payload rows have no checkboxes and loaded payload contents are traversed.
""",
    )

    print("ACTION: loading /World/PayloadB")
    print()

    stageviz.command.load_payloads(
        ["/World/PayloadB"]
    )

    require(
        wait_until(
            lambda: bool(
                prim("/World/PayloadB")
                and prim(
                    "/World/PayloadB"
                ).IsLoaded()
            ),
            timeout=5.0,
        ),
        "USD PayloadB is loaded",
    )

    require(
        exists(
            "/World/PayloadB/Geom"
        ),
        "USD contains PayloadB/Geom",
    )

    process_events()
    dump_tree(tree)

    if current_policy == "Payload":
        verify_payload_is_leaf(
            tree,
            "/World/PayloadA",
        )

        verify_payload_is_leaf(
            tree,
            "/World/PayloadB",
        )

        verify_payload_is_leaf(
            tree,
            "/World/PayloadC",
        )

        require(
            find_item_by_path(
                tree,
                "/World/PayloadB/Geom",
            ) is None,
            "PayloadB/Geom is intentionally hidden from StageTree in Payload policy",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadA",
            ) == Qt.Checked,
            "PayloadA remains checked",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadB",
            ) == Qt.Checked,
            "PayloadB becomes checked",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadC",
            ) == Qt.Unchecked,
            "PayloadC remains unchecked",
        )

        require_check_state(
            tree,
            "/World",
            Qt.PartiallyChecked,
            "World remains partially checked while PayloadC is unloaded",
        )

        require_check_state(
            tree,
            "/",
            Qt.PartiallyChecked,
            "root remains partially checked while PayloadC is unloaded",
        )

    else:
        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadA",
            ) is None
            and checkbox_state_for_path(
                tree,
                "/World/PayloadB",
            ) is None
            and checkbox_state_for_path(
                tree,
                "/World/PayloadC",
            ) is None,
            "All policy keeps payload checkboxes hidden",
        )


def test_payload_a_unload(tree):
    current_policy = policy_name(tree)

    step(
        5,
        "Unload PayloadA",
        f"""
Current policy:
    {current_policy}

After unload:

    PayloadA
    PayloadB
    PayloadC

USD:
    PayloadA = unloaded
    PayloadB = loaded
    PayloadC = unloaded

If policy is Payload:
    PayloadA/B/C remain StageTree leaf rows.
    PayloadA = unchecked
    PayloadB = checked
    PayloadC = unchecked

If policy is All:
    no checkboxes are visible and loaded payload contents are traversed.
""",
    )

    print("ACTION: unloading /World/PayloadA")
    print()

    stageviz.command.unload_payloads(
        ["/World/PayloadA"]
    )

    require(
        wait_until(
            lambda: bool(
                prim("/World/PayloadA")
                and not prim(
                    "/World/PayloadA"
                ).IsLoaded()
            ),
            timeout=5.0,
        ),
        "USD PayloadA is unloaded",
    )

    require(
        not exists(
            "/World/PayloadA/Geom"
        ),
        "PayloadA composed contents disappear from USD traversal",
    )

    process_events()
    dump_tree(tree)

    if current_policy == "Payload":
        verify_payload_is_leaf(
            tree,
            "/World/PayloadA",
        )

        verify_payload_is_leaf(
            tree,
            "/World/PayloadB",
        )

        verify_payload_is_leaf(
            tree,
            "/World/PayloadC",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadA",
            ) == Qt.Unchecked,
            "PayloadA becomes unchecked",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadB",
            ) == Qt.Checked,
            "PayloadB remains checked",
        )

        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadC",
            ) == Qt.Unchecked,
            "PayloadC remains unchecked",
        )

    else:
        require(
            checkbox_state_for_path(
                tree,
                "/World/PayloadA",
            ) is None
            and checkbox_state_for_path(
                tree,
                "/World/PayloadB",
            ) is None
            and checkbox_state_for_path(
                tree,
                "/World/PayloadC",
            ) is None,
            "All policy still shows no payload checkboxes",
        )


def test_tab_rename(tree):
    step(
        6,
        "Tab starts inline rename",
        """
Before pressing Tab:

  World
    Assembly
      Door
        Handle

Door should be selected.

After pressing Tab:

The Door name should become an inline text editor.

Approximately:

      [ Door ]

The hierarchy must remain expanded.
Focus must stay in the inline editor.
""",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/Door",
        ) is not None,
        "Door tree item exists",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly",
        ) is not None,
        "Assembly tree item exists",
    )

    find_item_by_path(
        tree,
        "/World/Assembly",
    ).setExpanded(True)

    find_item_by_path(
        tree,
        "/World/Assembly/Door",
    ).setExpanded(True)

    select_path(
        tree,
        "/World/Assembly/Door",
    )

    dump_tree(tree)

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/Door",
        ).isSelected(),
        "Door is selected before Tab",
    )

    print()
    print("ACTION: pressing Tab")
    print()

    QTest.keyClick(
        tree,
        Qt.Key_Tab,
    )

    process_events()

    editor = tree.focusWidget()

    print(
        "Focus widget after Tab:",
        type(editor).__name__
        if editor
        else None,
    )

    if not isinstance(
        editor,
        QLineEdit,
    ):
        stop(
            "Tab did not open a QLineEdit inline editor. "
            "Expected Door to become editable in place."
        )

    passed(
        "Tab opened inline rename editor"
    )


def test_complete_rename(tree):
    step(
        7,
        "Complete Door rename",
        """
Type:

    DoorRenamed

Then press Return.

Expected:

  World
    Assembly
      DoorRenamed
        Handle

Assembly remains expanded.
DoorRenamed remains expanded.
Handle remains visible.
DoorRenamed remains selected.
""",
    )

    editor = tree.focusWidget()

    require(
        isinstance(
            editor,
            QLineEdit,
        ),
        "inline editor is active",
    )

    editor.selectAll()

    QTest.keyClicks(
        editor,
        "DoorRenamed",
    )

    preserved_state = snapshot_tree_state(
        tree,
        (
            "/",
            "/World",
            "/World/Assembly",
            "/World/Assembly/Door",
            "/World/Assembly/Wheel",
            "/World/PayloadA",
            "/World/PayloadB",
            "/World/PayloadC",
        ),
    )

    QTest.keyClick(
        editor,
        Qt.Key_Return,
    )

    require_timed(
        "rename Door -> DoorRenamed (USD + tree stable)",
        lambda: (
            not exists(
                "/World/Assembly/Door"
            )
            and exists(
                "/World/Assembly/DoorRenamed"
            )
            and path_exists_in_tree(
                tree,
                "/World/Assembly/DoorRenamed",
            )
            and path_exists_in_tree(
                tree,
                "/World/Assembly/DoorRenamed/Handle",
            )
            and current_path(tree)
            == "/World/Assembly/DoorRenamed"
        ),
        "USD Door was renamed and StageTree became stable",
        timeout=5.0,
    )

    require_tree_state_preserved(
        tree,
        preserved_state,
        path_remap={
            "/World/Assembly/Door":
                "/World/Assembly/DoorRenamed",
        },
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorRenamed",
        ) is not None,
        "DoorRenamed appears in StageTree",
    )

    dump_tree(tree)

    require(
        find_item_by_path(
            tree,
            "/World/Assembly",
        ).isExpanded(),
        "Assembly remains expanded",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorRenamed",
        ).isExpanded(),
        "DoorRenamed remains expanded",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorRenamed/Handle",
        ) is not None,
        "Handle remains under DoorRenamed",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorRenamed",
        ).isSelected(),
        "DoorRenamed remains selected",
    )

    require(
        current_path(tree) == "/World/Assembly/DoorRenamed",
        "DoorRenamed remains current item",
    )


def test_tab_commit(tree):
    step(
        8,
        "Tab commits rename without moving to next item",
        """
DoorRenamed should be selected.

Press Tab once:
    DoorRenamed enters inline rename mode.

Change the text to:
    DoorFinal

Press Tab again.

Expected:
    - rename is committed
    - editor closes
    - DoorFinal remains selected/current
    - selection does NOT move to Handle or Wheel
    - hierarchy remains expanded
""",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorRenamed",
        ) is not None,
        "DoorRenamed exists before Tab commit test",
    )

    select_path(
        tree,
        "/World/Assembly/DoorRenamed",
    )

    find_item_by_path(
        tree,
        "/World/Assembly",
    ).setExpanded(True)

    find_item_by_path(
        tree,
        "/World/Assembly/DoorRenamed",
    ).setExpanded(True)

    QTest.keyClick(
        tree,
        Qt.Key_Tab,
    )

    process_events()

    editor = tree.focusWidget()

    require(
        isinstance(
            editor,
            QLineEdit,
        ),
        "Tab opens editor for DoorRenamed",
    )

    editor.selectAll()

    QTest.keyClicks(
        editor,
        "DoorFinal",
    )

    print()
    print("ACTION: pressing Tab while inline editor is active")
    print()

    preserved_state = snapshot_tree_state(
        tree,
        (
            "/",
            "/World",
            "/World/Assembly",
            "/World/Assembly/DoorRenamed",
            "/World/Assembly/Wheel",
            "/World/PayloadA",
            "/World/PayloadB",
            "/World/PayloadC",
        ),
    )

    QTest.keyClick(
        editor,
        Qt.Key_Tab,
    )

    require_timed(
        "Tab rename DoorRenamed -> DoorFinal (USD + tree stable)",
        lambda: (
            not exists(
                "/World/Assembly/DoorRenamed"
            )
            and exists(
                "/World/Assembly/DoorFinal"
            )
            and path_exists_in_tree(
                tree,
                "/World/Assembly/DoorFinal",
            )
            and current_path(tree)
            == "/World/Assembly/DoorFinal"
        ),
        "Tab commits DoorFinal rename and StageTree becomes stable",
        timeout=5.0,
    )

    require_tree_state_preserved(
        tree,
        preserved_state,
        path_remap={
            "/World/Assembly/DoorRenamed":
                "/World/Assembly/DoorFinal",
        },
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorFinal",
        ) is not None,
        "DoorFinal appears in StageTree",
    )

    dump_tree(tree)

    require(
        current_path(tree) == "/World/Assembly/DoorFinal",
        "Tab commit does not move current item to another row",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorFinal",
        ).isSelected(),
        "DoorFinal remains selected after Tab commit",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorFinal",
        ).isExpanded(),
        "DoorFinal remains expanded after Tab commit",
    )

    require(
        find_item_by_path(
            tree,
            "/World/Assembly/DoorFinal/Handle",
        ) is not None,
        "Handle remains under DoorFinal",
    )



def test_usd_sanitized_rename(tree):
    step(
        9,
        "USD sanitized rename",
        """
DoorFinal should be selected and expanded.

Press Tab once:
    DoorFinal enters inline rename mode.

Enter:
    123

Press Return.

Expected USD/StageTree result:
    _23

This verifies the case where the authored input is not a valid USD identifier
and the rename command normalizes the resulting prim name.

Expected:
    - /World/Assembly/DoorFinal disappears
    - /World/Assembly/_23 exists
    - _23 remains selected/current
    - _23 remains expanded
    - Handle remains visible below _23
""",
    )

    old_path = "/World/Assembly/DoorFinal"
    expected_path = "/World/Assembly/_23"

    require(
        path_exists_in_tree(
            tree,
            old_path,
        ),
        "DoorFinal exists before sanitized rename",
    )

    set_path_expanded(
        tree,
        "/World/Assembly",
        True,
    )

    set_path_expanded(
        tree,
        old_path,
        True,
    )

    select_path(
        tree,
        old_path,
    )

    require(
        path_is_selected(
            tree,
            old_path,
        ),
        "DoorFinal is selected before sanitized rename",
    )

    QTest.keyClick(
        tree,
        Qt.Key_Tab,
    )

    process_events()

    editor = tree.focusWidget()

    require(
        isinstance(
            editor,
            QLineEdit,
        ),
        "Tab opens editor for DoorFinal",
    )

    editor.selectAll()

    QTest.keyClicks(
        editor,
        "123",
    )

    preserved_state = snapshot_tree_state(
        tree,
        (
            "/",
            "/World",
            "/World/Assembly",
            old_path,
            "/World/Assembly/Wheel",
            "/World/PayloadA",
            "/World/PayloadB",
            "/World/PayloadC",
        ),
    )

    QTest.keyClick(
        editor,
        Qt.Key_Return,
    )

    require_timed(
        "sanitized rename DoorFinal -> _23 (USD + tree stable)",
        lambda: (
            not exists(old_path)
            and exists(expected_path)
            and path_exists_in_tree(
                tree,
                expected_path,
            )
            and path_exists_in_tree(
                tree,
                expected_path + "/Handle",
            )
            and current_path(tree)
            == expected_path
        ),
        "USD sanitizes 123 to _23 and StageTree becomes stable",
        timeout=5.0,
    )

    require_tree_state_preserved(
        tree,
        preserved_state,
        path_remap={
            old_path: expected_path,
        },
    )

    require(
        wait_until(
            lambda: path_exists_in_tree(
                tree,
                expected_path,
            ),
            timeout=5.0,
        ),
        "_23 appears in StageTree",
    )

    dump_tree(tree)

    require(
        not path_exists_in_tree(
            tree,
            old_path,
        ),
        "DoorFinal no longer exists in StageTree",
    )

    require(
        path_is_expanded(
            tree,
            "/World/Assembly",
        ),
        "Assembly remains expanded after sanitized rename",
    )

    require(
        path_is_expanded(
            tree,
            expected_path,
        ),
        "_23 remains expanded after sanitized rename",
    )

    require(
        path_exists_in_tree(
            tree,
            expected_path + "/Handle",
        ),
        "Handle remains under _23",
    )

    require(
        path_is_selected(
            tree,
            expected_path,
        ),
        "_23 remains selected",
    )

    require(
        current_path(tree) == expected_path,
        "_23 remains current item",
    )


def test_reparent_subtree(main):
    tree = reload_fixture(
        main,
        stageviz.LoadNone,
    )

    step(
        10,
        "Reparent expanded subtree",
        """
Start from the original hierarchy:

  World
    Assembly
      Door
        Handle
      Wheel

Expand Assembly, Door and Wheel.

Move Door below Wheel using stageviz.command.move_path().

Expected:

  World
    Assembly
      Wheel
        Door
          Handle

Expected UI state:
    - Door is not recreated as a collapsed row
    - Door remains expanded
    - Handle remains below Door
    - Wheel remains expanded
    - Door remains selected/current
    - old /World/Assembly/Door path disappears
""",
    )

    for path in (
        "/World",
        "/World/Assembly",
        "/World/Assembly/Door",
        "/World/Assembly/Wheel",
    ):
        require(
            path_exists_in_tree(tree, path),
            f"{path} exists before move",
        )
        set_path_expanded(tree, path, True)

    select_path(
        tree,
        "/World/Assembly/Door",
    )

    require(
        path_is_expanded(tree, "/World/Assembly/Door"),
        "Door is expanded before move",
    )

    preserved_state = snapshot_tree_state(
        tree,
        (
            "/",
            "/World",
            "/World/Assembly",
            "/World/Assembly/Door",
            "/World/Assembly/Wheel",
            "/World/PayloadA",
            "/World/PayloadB",
            "/World/PayloadC",
        ),
    )

    print()
    print("ACTION: moving Door below Wheel")
    print()

    stageviz.command.move_path(
        ["/World/Assembly/Door"],
        "/World/Assembly/Wheel",
        insert_index=0,
        preserve_world_transform=True,
    )

    new_path = "/World/Assembly/Wheel/Door"

    require_timed(
        "reparent Door -> Wheel (USD + tree stable)",
        lambda: (
            not exists("/World/Assembly/Door")
            and exists(new_path)
            and exists(new_path + "/Handle")
            and path_exists_in_tree(
                tree,
                new_path,
            )
            and path_exists_in_tree(
                tree,
                new_path + "/Handle",
            )
            and current_path(tree)
            == new_path
        ),
        "USD reparents Door subtree and StageTree becomes stable",
        timeout=5.0,
    )

    require_tree_state_preserved(
        tree,
        preserved_state,
        path_remap={
            "/World/Assembly/Door": new_path,
        },
    )

    dump_tree(tree)

    require(
        not path_exists_in_tree(tree, "/World/Assembly/Door"),
        "old Door tree path disappears after reparent",
    )

    require(
        path_exists_in_tree(tree, new_path + "/Handle"),
        "Handle remains below reparented Door",
    )

    require(
        path_is_expanded(tree, "/World/Assembly"),
        "Assembly remains expanded after reparent",
    )

    require(
        path_is_expanded(tree, "/World/Assembly/Wheel"),
        "Wheel remains expanded after receiving Door",
    )

    require(
        path_is_expanded(tree, new_path),
        "reparented Door remains expanded",
    )

    require(
        path_is_selected(tree, new_path),
        "reparented Door remains selected",
    )

    require(
        current_path(tree) == new_path,
        "reparented Door remains current item",
    )

    if undo_if_available():
        require(
            wait_until(
                lambda: (
                    exists("/World/Assembly/Door")
                    and exists("/World/Assembly/Door/Handle")
                    and not exists(new_path)
                ),
                timeout=5.0,
            ),
            "undo restores Door to Assembly",
        )

        require(
            wait_until(
                lambda: path_exists_in_tree(
                    tree,
                    "/World/Assembly/Door",
                ),
                timeout=5.0,
            ),
            "undo restores Door row in StageTree",
        )

        dump_tree(tree)

        require(
            path_exists_in_tree(
                tree,
                "/World/Assembly/Door/Handle",
            ),
            "undo restores Handle below Door",
        )

        require(
            path_is_selected(
                tree,
                "/World/Assembly/Door",
            ),
            "undo restores Door selection",
        )

        require(
            current_path(tree) == "/World/Assembly/Door",
            "undo restores Door as current item",
        )

    release_qt_wrappers()


def test_delete_subtree(main):
    tree = reload_fixture(
        main,
        stageviz.LoadNone,
    )

    step(
        11,
        "Delete subtree",
        """
Start from the original hierarchy and expand:

  World
    Assembly
      Door
        Handle
      Wheel

Delete Door.

Expected:

  World
    Assembly
      Wheel

Expected:
    - Door disappears from USD and StageTree
    - Handle disappears with Door
    - Assembly remains expanded
    - no stale current-item path points at Door/Handle

If undo is available, Door and Handle should return.
""",
    )

    set_path_expanded(tree, "/World", True)
    set_path_expanded(tree, "/World/Assembly", True)
    set_path_expanded(tree, "/World/Assembly/Door", True)

    select_path(
        tree,
        "/World/Assembly/Door",
    )

    print()
    print("ACTION: deleting /World/Assembly/Door")
    print()

    stageviz.command.delete_paths(
        ["/World/Assembly/Door"]
    )

    require(
        wait_until(
            lambda: (
                not exists("/World/Assembly/Door")
                and not exists("/World/Assembly/Door/Handle")
            ),
            timeout=5.0,
        ),
        "USD deletes Door subtree",
    )

    require(
        wait_until(
            lambda: not path_exists_in_tree(
                tree,
                "/World/Assembly/Door",
            ),
            timeout=5.0,
        ),
        "Door disappears from StageTree",
    )

    dump_tree(tree)

    require(
        not path_exists_in_tree(
            tree,
            "/World/Assembly/Door/Handle",
        ),
        "Handle disappears from StageTree with Door",
    )

    require(
        path_is_expanded(
            tree,
            "/World/Assembly",
        ),
        "Assembly remains expanded after delete",
    )

    require(
        current_path(tree)
        not in (
            "/World/Assembly/Door",
            "/World/Assembly/Door/Handle",
        ),
        "current item does not reference deleted subtree",
    )

    if undo_if_available():
        require(
            wait_until(
                lambda: (
                    exists("/World/Assembly/Door")
                    and exists("/World/Assembly/Door/Handle")
                ),
                timeout=5.0,
            ),
            "undo restores deleted Door subtree",
        )

        require(
            wait_until(
                lambda: path_exists_in_tree(
                    tree,
                    "/World/Assembly/Door/Handle",
                ),
                timeout=5.0,
            ),
            "undo restores Door and Handle in StageTree",
        )

        dump_tree(tree)

    release_qt_wrappers()


def test_payload_policy_after_reload(main):
    tree = reload_fixture(
        main,
        stageviz.LoadNone,
    )

    step(
        12,
        "Payload policy reload",
        """
Reload the fixture with LoadNone.

Expected StageTree policy:
    Payload

Expected:
    - PayloadA/B/C are unloaded
    - payload rows are StageTree leaves
    - payload rows show unchecked checkboxes
    - loading/unloading changes checkbox state only
    - payload contents remain hidden from StageTree
    - normal expanded hierarchy remains stable
""",
    )

    require(
        policy_name(tree) == "Payload",
        "LoadNone produces Payload StageTree policy",
    )

    set_path_expanded(tree, "/World", True)
    set_path_expanded(tree, "/World/Assembly", True)
    set_path_expanded(tree, "/World/Assembly/Door", True)

    for path in (
        "/",
        "/World",
        "/World/Assembly",
        "/World/Assembly/Door",
        "/World/Assembly/Door/Handle",
        "/World/Assembly/Wheel",
        "/World/PayloadA",
        "/World/PayloadB",
        "/World/PayloadC",
    ):
        require_checkbox(
            tree,
            path,
            True,
            f"{path} has checkbox after Payload-policy reload",
        )

    for path in (
        "/World/PayloadA",
        "/World/PayloadB",
        "/World/PayloadC",
    ):
        verify_payload_is_leaf(tree, path)

        require(
            has_checkbox_for_path(tree, path),
            f"{path} has checkbox in Payload policy",
        )

        require(
            checkbox_state_for_path(tree, path) == Qt.Unchecked,
            f"{path} begins unchecked after LoadNone reload",
        )

    stageviz.command.load_payloads(
        [
            "/World/PayloadA",
            "/World/PayloadC",
        ]
    )

    require(
        wait_until(
            lambda: (
                prim("/World/PayloadA").IsLoaded()
                and prim("/World/PayloadC").IsLoaded()
            ),
            timeout=5.0,
        ),
        "PayloadA and PayloadC load",
    )

    process_events()
    dump_tree(tree)

    require(
        checkbox_state_for_path(
            tree,
            "/World/PayloadA",
        ) == Qt.Checked,
        "PayloadA checkbox becomes checked",
    )

    require(
        checkbox_state_for_path(
            tree,
            "/World/PayloadB",
        ) == Qt.Unchecked,
        "PayloadB checkbox remains unchecked",
    )

    require(
        checkbox_state_for_path(
            tree,
            "/World/PayloadC",
        ) == Qt.Checked,
        "PayloadC checkbox becomes checked",
    )

    require(
        not path_exists_in_tree(
            tree,
            "/World/PayloadA/Geom",
        ),
        "PayloadA contents remain hidden in Payload policy",
    )

    require(
        not path_exists_in_tree(
            tree,
            "/World/PayloadC/Geom",
        ),
        "PayloadC contents remain hidden in Payload policy",
    )

    require_check_state(
        tree,
        "/World",
        Qt.PartiallyChecked,
        "World is partially checked while PayloadB remains unloaded",
    )

    require_check_state(
        tree,
        "/",
        Qt.PartiallyChecked,
        "root is partially checked while PayloadB remains unloaded",
    )

    require(
        path_is_expanded(
            tree,
            "/World/Assembly/Door",
        ),
        "unrelated Door expansion survives payload changes",
    )

    release_qt_wrappers()


def test_all_policy(main):
    tree = reload_fixture(
        main,
        stageviz.LoadAll,
    )

    step(
        13,
        "All policy",
        """
Reload the fixture using LoadAll.

Expected StageTree policy:
    All

Expected:
    - PayloadA/B/C are loaded in USD
    - payload rows have NO checkboxes
    - loaded payload contents ARE traversed and shown
    - PayloadA/Geom and PayloadA/Geom/Detail are visible in StageTree

Then unload PayloadA programmatically.

Expected:
    - PayloadA remains in StageTree
    - PayloadA still has no checkbox
    - composed PayloadA contents disappear from USD
    - PayloadA/Geom and Detail disappear from StageTree

Then load PayloadA again.

Expected:
    - PayloadA/Geom and Detail return
    - no checkbox appears
    - unrelated normal hierarchy expansion remains stable
""",
    )

    require(
        policy_name(tree) == "All",
        "LoadAll produces All StageTree policy",
    )

    require_no_checkboxes_anywhere(
        tree,
        "All policy has no checkboxes anywhere in StageTree",
    )

    set_path_expanded(tree, "/World", True)
    set_path_expanded(tree, "/World/Assembly", True)
    set_path_expanded(tree, "/World/Assembly/Door", True)

    for path in (
        "/World/PayloadA",
        "/World/PayloadB",
        "/World/PayloadC",
    ):
        require(
            bool(
                prim(path)
                and prim(path).IsLoaded()
            ),
            f"{path} is loaded under LoadAll",
        )

        require(
            path_exists_in_tree(tree, path),
            f"{path} exists in StageTree",
        )

        require(
            not has_checkbox_for_path(tree, path),
            f"{path} has no checkbox in All policy",
        )

    require(
        path_exists_in_tree(tree, "/World/PayloadA/Geom"),
        "PayloadA/Geom is visible in All policy",
    )

    require(
        path_exists_in_tree(tree, "/World/PayloadA/Geom/Detail"),
        "PayloadA/Geom/Detail is visible in All policy",
    )

    require(
        path_exists_in_tree(tree, "/World/PayloadB/Geom"),
        "PayloadB/Geom is visible in All policy",
    )

    require(
        path_exists_in_tree(tree, "/World/PayloadC/Geom"),
        "PayloadC/Geom is visible in All policy",
    )

    print()
    print("ACTION: unloading PayloadA while StageTree policy is All")
    print()

    stageviz.command.unload_payloads(
        ["/World/PayloadA"]
    )

    require(
        wait_until(
            lambda: bool(
                prim("/World/PayloadA")
                and not prim("/World/PayloadA").IsLoaded()
            ),
            timeout=5.0,
        ),
        "PayloadA unloads in All policy",
    )

    require(
        wait_until(
            lambda: (
                not exists("/World/PayloadA/Geom")
                and not path_exists_in_tree(tree, "/World/PayloadA/Geom")
            ),
            timeout=5.0,
        ),
        "PayloadA composed contents disappear from USD and StageTree",
    )

    process_events()
    dump_tree(tree)

    require(
        path_exists_in_tree(tree, "/World/PayloadA"),
        "PayloadA row remains after unload in All policy",
    )

    require(
        not has_checkbox_for_path(tree, "/World/PayloadA"),
        "PayloadA still has no checkbox after unload in All policy",
    )

    require_no_checkboxes_anywhere(
        tree,
        "All policy still has no checkboxes anywhere after payload unload",
    )

    require(
        not path_exists_in_tree(tree, "/World/PayloadA/Geom/Detail"),
        "PayloadA/Geom/Detail disappears after unload",
    )

    require(
        path_is_expanded(tree, "/World/Assembly/Door"),
        "Door expansion survives payload unload in All policy",
    )

    print()
    print("ACTION: loading PayloadA again in All policy")
    print()

    stageviz.command.load_payloads(
        ["/World/PayloadA"]
    )

    require(
        wait_until(
            lambda: bool(
                prim("/World/PayloadA")
                and prim("/World/PayloadA").IsLoaded()
            ),
            timeout=5.0,
        ),
        "PayloadA reloads in All policy",
    )

    require(
        wait_until(
            lambda: (
                exists("/World/PayloadA/Geom")
                and exists("/World/PayloadA/Geom/Detail")
                and path_exists_in_tree(tree, "/World/PayloadA/Geom")
                and path_exists_in_tree(tree, "/World/PayloadA/Geom/Detail")
            ),
            timeout=5.0,
        ),
        "PayloadA contents return to USD and StageTree",
    )

    process_events()
    dump_tree(tree)

    require(
        not has_checkbox_for_path(tree, "/World/PayloadA"),
        "PayloadA remains checkbox-free after reload in All policy",
    )

    require_no_checkboxes_anywhere(
        tree,
        "All policy still has no checkboxes anywhere after payload reload",
    )

    require(
        path_is_expanded(tree, "/World/Assembly/Door"),
        "Door expansion survives payload reload in All policy",
    )

    release_qt_wrappers()


def test_invalid_move_keeps_tree_stable(main):
    tree = reload_fixture(
        main,
        stageviz.LoadNone,
    )

    step(
        14,
        "Rejected namespace move",
        """
Attempt to move Assembly below its own descendant Door.

USD should reject the operation.

Expected StageTree:
    - hierarchy remains unchanged
    - Assembly remains expanded
    - Door remains expanded
    - Handle remains present
    - no duplicate rows appear
""",
    )

    set_path_expanded(tree, "/World", True)
    set_path_expanded(tree, "/World/Assembly", True)
    set_path_expanded(tree, "/World/Assembly/Door", True)

    before_current = current_path(tree)

    stageviz.command.move_path(
        ["/World/Assembly"],
        "/World/Assembly/Door",
        insert_index=-1,
        preserve_world_transform=True,
    )

    process_events()

    require(
        exists("/World/Assembly"),
        "rejected move leaves Assembly in USD",
    )

    require(
        exists("/World/Assembly/Door/Handle"),
        "rejected move leaves Door hierarchy in USD",
    )

    dump_tree(tree)

    require(
        path_exists_in_tree(
            tree,
            "/World/Assembly/Door/Handle",
        ),
        "rejected move leaves Handle in StageTree",
    )

    require(
        path_is_expanded(
            tree,
            "/World/Assembly",
        ),
        "Assembly remains expanded after rejected move",
    )

    require(
        path_is_expanded(
            tree,
            "/World/Assembly/Door",
        ),
        "Door remains expanded after rejected move",
    )

    release_qt_wrappers()


def test_large_tree_namespace_performance(root):
    main = create_performance_fixture(
        root,
    )

    tree = reload_fixture(
        main,
        stageviz.LoadNone,
    )

    step(
        15,
        "Large-tree namespace timing and state preservation",
        """
A separate hierarchy with roughly 1,200 normal prim rows is loaded.

Source/Moving/Child is expanded and Moving is selected/current.
Several unrelated branches are deliberately given a mixture of expanded and
collapsed states.

Move:

    /World/Source/Moving

to:

    /World/Target/Moving

Expected:
    - the moved subtree stays expanded
    - selection/current follows the new path
    - unrelated branch expansion states are unchanged
    - the operation prints command-to-stable timing
    - the timing measures both USD completion and the final StageTree state,
      not merely how long the Python command call takes

This is intended to expose accidental O(N^2) StageTree state restoration.
There is intentionally no strict millisecond pass/fail threshold because UI
timing varies substantially between Debug/Release and machines.
""",
    )

    require(
        path_exists_in_tree(
            tree,
            "/World/Source/Moving",
        ),
        "Moving subtree exists in large fixture",
    )

    set_path_expanded(
        tree,
        "/World",
        True,
    )

    set_path_expanded(
        tree,
        "/World/Source",
        True,
    )

    set_path_expanded(
        tree,
        "/World/Source/Moving",
        True,
    )

    set_path_expanded(
        tree,
        "/World/Target",
        True,
    )

    tracked_branches = (
        "/World/Branch_000",
        "/World/Branch_010",
        "/World/Branch_020",
        "/World/Branch_030",
        "/World/Branch_040",
        "/World/Branch_050",
        "/World/Branch_060",
        "/World/Branch_070",
    )

    for index, path in enumerate(
        tracked_branches,
    ):
        set_path_expanded(
            tree,
            path,
            index % 2 == 0,
        )

    select_path(
        tree,
        "/World/Source/Moving",
    )

    state_paths = (
        "/World",
        "/World/Source",
        "/World/Source/Moving",
        "/World/Target",
    ) + tracked_branches

    preserved_state = snapshot_tree_state(
        tree,
        state_paths,
    )

    item_count = tree_item_count(
        tree,
    )

    print()
    print(
        "Large-tree StageTree rows:",
        item_count,
    )
    print()

    old_path = "/World/Source/Moving"
    new_path = "/World/Target/Moving"

    print(
        "ACTION: moving large-fixture subtree"
    )
    print()

    command_started = time.perf_counter()

    stageviz.command.move_path(
        [old_path],
        "/World/Target",
        insert_index=0,
        preserve_world_transform=True,
    )

    python_call_ms = (
        time.perf_counter()
        - command_started
    ) * 1000.0

    print(
        f"[TIME] move_path Python call returned: "
        f"{python_call_ms:.2f} ms"
    )

    elapsed_ms = require_timed(
        (
            "large-tree reparent "
            f"({item_count} rows, USD + tree stable)"
        ),
        lambda: (
            not exists(old_path)
            and exists(new_path)
            and exists(new_path + "/Child")
            and not path_exists_in_tree(
                tree,
                old_path,
            )
            and path_exists_in_tree(
                tree,
                new_path,
            )
            and path_exists_in_tree(
                tree,
                new_path + "/Child",
            )
            and path_is_expanded(
                tree,
                new_path,
            )
            and path_is_selected(
                tree,
                new_path,
            )
            and current_path(tree)
            == new_path
        ),
        "large-tree reparent reaches stable USD and StageTree state",
        timeout=10.0,
    )

    require_tree_state_preserved(
        tree,
        preserved_state,
        path_remap={
            old_path: new_path,
        },
    )

    require(
        path_exists_in_tree(
            tree,
            new_path + "/Child",
        ),
        "large-tree moved child remains present",
    )

    print()
    print(
        "[PERF] large-tree namespace result: "
        f"{elapsed_ms:.2f} ms stable latency "
        f"for {item_count} StageTree rows"
    )
    print()

    release_qt_wrappers()



def test_25k_namespace_benchmark(root):
    target_prim_count = 25000

    step(
        16,
        "Fresh 25k StageTree namespace benchmark",
        """
A completely new USD stage is authored at the end of the UI suite.

Target:
    approximately 25,000 authored prims

The stage contains:
    /World
      /Source
        /Moving
          /Child
      /Target
      /Branch_000 ...
      /Branch_099 ...

The broad unrelated hierarchy is intentional: rename/reparent only touches a
very small subtree, while StageTree must preserve state correctly across the
full visible tree.

Measurements:
    1. USD fixture creation/save time
    2. StageViz session.load() -> StageTree fully populated
    3. rename Moving -> MovingRenamed
    4. reparent MovingRenamed -> Target

Correctness checks:
    - moved/renamed subtree remains expanded
    - selection/current follows the namespace edit
    - representative unrelated branches keep mixed expanded/collapsed states
    - representative checkbox state is preserved
    - old paths disappear and descendants remain at their remapped paths

No hard millisecond threshold is applied yet. This produces a stable reference
measurement for comparing builds and detecting scaling regressions.
""",
    )

    print()
    print(
        f"Creating fresh benchmark stage with target "
        f"{target_prim_count:,} prims..."
    )
    print()

    (
        main,
        create_seconds,
        last_child_paths,
    ) = create_25k_benchmark_fixture(
        root,
        target_prim_count=target_prim_count,
        branch_count=100,
    )

    expected_last_path = last_child_paths[-1]

    print(
        f"[TIME] 25k fixture create/save: "
        f"{create_seconds * 1000.0:.2f} ms"
    )

    release_qt_wrappers()

    load_started = time.perf_counter()

    require(
        stageviz.session().load(
            main,
            stageviz.LoadNone,
        ),
        "25k benchmark stage loads",
    )

    require(
        wait_until(
            lambda: bool(
                stage()
                and exists("/World")
            ),
            timeout=30.0,
        ),
        "25k benchmark USD stage becomes available",
    )

    tree = find_stage_tree()

    require(
        tree is not None,
        "StageTree found for 25k benchmark",
    )

    print(
        "25k tree population sentinel:",
        expected_last_path,
    )

    require(
        wait_until(
            lambda: (
                path_exists_in_tree(
                    tree,
                    "/World/Branch_099",
                )
                and path_exists_in_tree(
                    tree,
                    expected_last_path,
                )
            ),
            timeout=30.0,
        ),
        "25k benchmark StageTree is fully populated",
    )

    load_ms = (
        time.perf_counter()
        - load_started
    ) * 1000.0

    item_count = tree_item_count(
        tree,
    )

    print(
        f"[TIME] 25k session.load -> tree stable: "
        f"{load_ms:.2f} ms"
    )
    print(
        f"[PERF] 25k StageTree rows: "
        f"{item_count:,}"
    )
    print()

    # Establish deliberately mixed view state across distant branches.
    set_path_expanded(
        tree,
        "/World",
        True,
    )
    set_path_expanded(
        tree,
        "/World/Source",
        True,
    )
    set_path_expanded(
        tree,
        "/World/Source/Moving",
        True,
    )
    set_path_expanded(
        tree,
        "/World/Target",
        True,
    )

    tracked_branches = (
        "/World/Branch_000",
        "/World/Branch_011",
        "/World/Branch_022",
        "/World/Branch_033",
        "/World/Branch_044",
        "/World/Branch_055",
        "/World/Branch_066",
        "/World/Branch_077",
        "/World/Branch_088",
        "/World/Branch_099",
    )

    for index, path in enumerate(
        tracked_branches,
    ):
        set_path_expanded(
            tree,
            path,
            index % 2 == 0,
        )

    original_path = "/World/Source/Moving"
    renamed_path = "/World/Source/MovingRenamed"
    moved_path = "/World/Target/MovingRenamed"

    select_path(
        tree,
        original_path,
    )

    tracked_state_paths = (
        "/",
        "/World",
        "/World/Source",
        original_path,
        "/World/Target",
    ) + tracked_branches

    rename_state = snapshot_tree_state(
        tree,
        tracked_state_paths,
    )

    print()
    print(
        "ACTION: 25k rename "
        "/World/Source/Moving -> MovingRenamed"
    )
    print()

    command_started = time.perf_counter()

    stageviz.command.rename_path(
        original_path,
        "MovingRenamed",
    )

    rename_python_ms = (
        time.perf_counter()
        - command_started
    ) * 1000.0

    print(
        f"[TIME] 25k rename_path Python call returned: "
        f"{rename_python_ms:.2f} ms"
    )

    rename_ms = require_timed(
        (
            f"25k rename "
            f"({item_count} rows, USD + tree stable)"
        ),
        lambda: (
            not exists(original_path)
            and exists(renamed_path)
            and exists(renamed_path + "/Child")
            and not path_exists_in_tree(
                tree,
                original_path,
            )
            and path_exists_in_tree(
                tree,
                renamed_path,
            )
            and path_exists_in_tree(
                tree,
                renamed_path + "/Child",
            )
            and path_is_expanded(
                tree,
                renamed_path,
            )
            and path_is_selected(
                tree,
                renamed_path,
            )
            and current_path(tree)
            == renamed_path
        ),
        "25k rename reaches stable USD and StageTree state",
        timeout=30.0,
    )

    require_tree_state_preserved(
        tree,
        rename_state,
        path_remap={
            original_path: renamed_path,
        },
    )

    # Snapshot again after rename so reparent is independently validated.
    move_state_paths = (
        "/",
        "/World",
        "/World/Source",
        renamed_path,
        "/World/Target",
    ) + tracked_branches

    move_state = snapshot_tree_state(
        tree,
        move_state_paths,
    )

    print()
    print(
        "ACTION: 25k reparent "
        "/World/Source/MovingRenamed -> /World/Target"
    )
    print()

    command_started = time.perf_counter()

    stageviz.command.move_path(
        [renamed_path],
        "/World/Target",
        insert_index=0,
        preserve_world_transform=True,
    )

    move_python_ms = (
        time.perf_counter()
        - command_started
    ) * 1000.0

    print(
        f"[TIME] 25k move_path Python call returned: "
        f"{move_python_ms:.2f} ms"
    )

    move_ms = require_timed(
        (
            f"25k reparent "
            f"({item_count} rows, USD + tree stable)"
        ),
        lambda: (
            not exists(renamed_path)
            and exists(moved_path)
            and exists(moved_path + "/Child")
            and not path_exists_in_tree(
                tree,
                renamed_path,
            )
            and path_exists_in_tree(
                tree,
                moved_path,
            )
            and path_exists_in_tree(
                tree,
                moved_path + "/Child",
            )
            and path_is_expanded(
                tree,
                moved_path,
            )
            and path_is_selected(
                tree,
                moved_path,
            )
            and current_path(tree)
            == moved_path
        ),
        "25k reparent reaches stable USD and StageTree state",
        timeout=30.0,
    )

    require_tree_state_preserved(
        tree,
        move_state,
        path_remap={
            renamed_path: moved_path,
        },
    )

    print()
    print("25K BENCHMARK SUMMARY")
    print()
    print(
        f"Fixture create/save:                  "
        f"{create_seconds * 1000.0:10.2f} ms"
    )
    print(
        f"Session load -> tree stable:           "
        f"{load_ms:10.2f} ms"
    )
    print(
        f"Rename command call:                   "
        f"{rename_python_ms:10.2f} ms"
    )
    print(
        f"Rename -> USD + tree stable:           "
        f"{rename_ms:10.2f} ms"
    )
    print(
        f"Reparent command call:                 "
        f"{move_python_ms:10.2f} ms"
    )
    print(
        f"Reparent -> USD + tree stable:         "
        f"{move_ms:10.2f} ms"
    )
    print(
        f"StageTree rows:                        "
        f"{item_count:10,d}"
    )
    print()

    release_qt_wrappers()


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------

def run():
    divider()

    print("StageTree interactive UI diagnostic")

    try:
        root, main = create_fixture()

        print()
        print("Fixture:")
        print(root)

        print()
        print("Loading:")
        print(main)

        session = stageviz.session()

        if hasattr(
            session,
            "setPreserveState",
        ):
            session.setPreserveState(False)

        require(
            session.load(
                main,
                stageviz.LoadNone,
            ),
            "fixture loaded",
        )

        require(
            wait_until(
                lambda: bool(
                    stage()
                    and exists("/World")
                ),
                timeout=5.0,
            ),
            "stage is available",
        )

        tree = find_stage_tree()

        require(
            tree is not None,
            "StageTree found",
        )

        print()
        print("Detected policy:")
        print(policy_name(tree))

        test_initial_tree(tree)
        test_expansion(tree)
        test_payload_a_load(tree)
        test_payload_b_load(tree)
        test_payload_a_unload(tree)
        test_tab_rename(tree)
        test_complete_rename(tree)
        test_tab_commit(tree)
        test_usd_sanitized_rename(tree)

        # Structural and policy regressions use fresh fixture reloads so a
        # failure is attributable to one operation rather than accumulated
        # changes from the rename sequence above.
        test_reparent_subtree(main)
        test_delete_subtree(main)
        test_payload_policy_after_reload(main)
        test_all_policy(main)
        test_invalid_move_keeps_tree_stable(main)
        test_large_tree_namespace_performance(root)
        test_25k_namespace_benchmark(root)

        print_timing_summary()

        divider()

        print("ALL STAGETREE UI STEPS PASSED")
        print()

        return True

    except Exception as exc:
        print()
        print("Diagnostic stopped.")
        print()
        print("Reason:")
        print(str(exc))
        print()

        return False


if __name__ == "__main__":
    run()
