# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""
Stageviz command smoke/regression tests.

Run from the Stageviz Python console after pycommand.cpp has been registered as
stageviz.command.
"""

import os
import tempfile
import time
import traceback

from pxr import Sdf, Usd, UsdGeom
import stageviz


_FAILURES = []


def _fail(message):
    _FAILURES.append(message)
    print(f"[FAIL] {message}")


def _ok(message):
    print(f"[ OK ] {message}")


def _assert(condition, message):
    if condition:
        _ok(message)
    else:
        _fail(message)


def _assert_equal(actual, expected, message):
    if actual == expected:
        _ok(f"{message}: {actual!r}")
    else:
        _fail(f"{message}: expected {expected!r}, got {actual!r}")


def _assert_set_equal(actual, expected, message):
    actual_set = set(actual)
    expected_set = set(expected)
    if actual_set == expected_set:
        _ok(f"{message}: {actual!r}")
    else:
        _fail(f"{message}: expected set {sorted(expected_set)!r}, got {sorted(actual_set)!r}")


def _path_list(values):
    return [str(v) for v in values]


def _process_events():
    try:
        from PySide6 import QtWidgets, QtCore
        qt_app = QtWidgets.QApplication.instance()
        if qt_app:
            flags = (
                QtCore.QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents
                | QtCore.QEventLoop.ProcessEventsFlag.ExcludeSocketNotifiers
            )
            qt_app.processEvents(flags)
            return
    except Exception:
        pass

    try:
        from PyQt6 import QtWidgets, QtCore
        qt_app = QtWidgets.QApplication.instance()
        if qt_app:
            flags = (
                QtCore.QEventLoop.ProcessEventsFlag.ExcludeUserInputEvents
                | QtCore.QEventLoop.ProcessEventsFlag.ExcludeSocketNotifiers
            )
            qt_app.processEvents(flags)
            return
    except Exception:
        pass


def _wait(ms=250):
    end = time.time() + (ms / 1000.0)
    while time.time() < end:
        _process_events()
        time.sleep(0.01)


def _wait_until(predicate, timeout=3.0, interval=0.02):
    end = time.time() + timeout
    last_error = None

    while time.time() < end:
        _process_events()

        try:
            if predicate():
                _process_events()
                return True
        except Exception as exc:
            last_error = exc

        time.sleep(interval)

    if last_error:
        print(f"[wait] last predicate exception: {last_error}")

    return False


def _run(label, func):
    print(f"\n--- {label} ---")
    try:
        func()
    except Exception as exc:
        _fail(f"{label}: exception: {exc}")
        traceback.print_exc()


def _stage():
    return stageviz.session().stage()


def _selection():
    return _path_list(stageviz.session().paths())


def _mask():
    return _path_list(stageviz.session().mask())


def _prim(path):
    stage = _stage()
    if not stage:
        return None
    return stage.GetPrimAtPath(path)


def _exists(path):
    prim = _prim(path)
    return bool(prim and prim.IsValid())


def _visibility(path):
    prim = _prim(path)
    if not prim:
        return None
    return UsdGeom.Imageable(prim).GetVisibilityAttr().Get()


def _children(path):
    prim = _prim(path)
    if not prim:
        return []
    return [str(child.GetPath()) for child in prim.GetChildren()]


def _child_names(path):
    prim = _prim(path)
    if not prim:
        return []
    return [child.GetName() for child in prim.GetChildren()]


def _default_prim_path():
    stage = _stage()
    if not stage:
        return ""
    prim = stage.GetDefaultPrim()
    if not prim:
        return ""
    return str(prim.GetPath())


def _define_xform(stage, path):
    return UsdGeom.Xform.Define(stage, path).GetPrim()


def _has_command(name):
    return hasattr(stageviz.command, name)


def _try_command(name, *args, **kwargs):
    if not _has_command(name):
        print(f"[skip] stageviz.command.{name} is not bound")
        return False

    getattr(stageviz.command, name)(*args, **kwargs)
    _wait()
    return True


def _undo():
    for name in ("undo", "undo_command"):
        if _has_command(name):
            getattr(stageviz.command, name)()
            _wait()
            return True

    stack = getattr(stageviz, "command_stack", None)
    if stack and hasattr(stack, "undo"):
        stack.undo()
        _wait()
        return True

    print("[skip] undo is not bound")
    return False


def _redo():
    for name in ("redo", "redo_command"):
        if _has_command(name):
            getattr(stageviz.command, name)()
            _wait()
            return True

    stack = getattr(stageviz, "command_stack", None)
    if stack and hasattr(stack, "redo"):
        stack.redo()
        _wait()
        return True

    print("[skip] redo is not bound")
    return False


def _create_payload_file(path, name):
    stage = Usd.Stage.CreateNew(path)
    _define_xform(stage, f"/{name}")
    _define_xform(stage, f"/{name}/Geom")
    stage.SetDefaultPrim(stage.GetPrimAtPath(f"/{name}"))
    stage.GetRootLayer().Save()


def _create_main_file(path, payload_a, payload_b):
    stage = Usd.Stage.CreateNew(path)

    world = _define_xform(stage, "/World")
    stage.SetDefaultPrim(world)

    _define_xform(stage, "/World/A")
    _define_xform(stage, "/World/A/A1")
    _define_xform(stage, "/World/A/A2")

    _define_xform(stage, "/World/B")
    _define_xform(stage, "/World/B/B1")
    _define_xform(stage, "/World/B/B2")

    _define_xform(stage, "/World/C")
    _define_xform(stage, "/World/D")

    _define_xform(stage, "/OtherRoot")

    p1 = _define_xform(stage, "/World/PayloadA")
    p1.GetPayloads().AddPayload(os.path.basename(payload_a), "/PayloadA")

    p2 = _define_xform(stage, "/World/PayloadB")
    p2.GetPayloads().AddPayload(os.path.basename(payload_b), "/PayloadB")

    stage.GetRootLayer().Save()


def create_fixture():
    root = tempfile.mkdtemp(prefix="stageviz_command_tests_")
    payload_a = os.path.join(root, "payload_a.usda")
    payload_b = os.path.join(root, "payload_b.usda")
    main = os.path.join(root, "command_test.usda")
    saved = os.path.join(root, "command_test_saved.usda")

    _create_payload_file(payload_a, "PayloadA")
    _create_payload_file(payload_b, "PayloadB")
    _create_main_file(main, payload_a, payload_b)

    return root, main, saved


def reload_fixture(main_path):
    loaded = stageviz.session().load(main_path, stageviz.LoadNone)
    _assert(loaded, "session.load fixture with LoadNone")

    _assert(
        _wait_until(
            lambda: bool(
                stageviz.session().stage()
                and stageviz.session().stage().GetPrimAtPath("/World")
            )
        ),
        "fixture stage is available",
    )


def test_select_paths():
    stageviz.command.select_paths(["/World/A", "/World/B"])

    _assert(
        _wait_until(lambda: _selection() == ["/World/A", "/World/B"]),
        "select_paths updates selection",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _selection() == []),
            "undo select_paths restores previous empty selection",
        )

    if _redo():
        _assert(
            _wait_until(lambda: _selection() == ["/World/A", "/World/B"]),
            "redo select_paths restores selected paths",
        )


def test_select_all_and_invert():
    stageviz.command.isolate_paths(["/World/A"])
    _assert(
        _wait_until(lambda: _mask() == ["/World/A"]),
        "mask prepared for select_all",
    )

    stageviz.command.select_all()

    _assert_set_equal(
        _selection(),
        ["/World/A/A1", "/World/A/A2"],
        "select_all respects mask and selects leaf paths",
    )

    before = set(_selection())
    stageviz.command.select_invert()

    _assert(
        _wait_until(lambda: set(_selection()) != before),
        "select_invert changes selection domain",
    )

    stageviz.command.isolate_paths([])


def test_isolate_paths():
    stageviz.command.select_paths(["/World/A/A1"])
    stageviz.command.isolate_paths(["/World/A"])

    _assert(
        _wait_until(lambda: _mask() == ["/World/A"]),
        "isolate_paths sets mask",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _mask() == []),
            "undo isolate_paths restores previous mask",
        )


def test_show_hide_paths():
    stageviz.command.hide_paths(["/World/A"], recursive=False)

    _assert(
        _wait_until(lambda: str(_visibility("/World/A")) == "invisible"),
        "hide_paths authors invisible",
    )

    _assert(
        str(_visibility("/World/A/A1")) != "invisible",
        "non-recursive hide does not author invisible on child",
    )

    if _undo():
        _assert(
            _wait_until(lambda: str(_visibility("/World/A")) != "invisible"),
            "undo hide_paths restores previous visibility",
        )

    stageviz.command.hide_paths(["/World/A"], recursive=True)

    _assert(
        _wait_until(lambda: str(_visibility("/World/A/A1")) == "invisible"),
        "recursive hide authors invisible on child",
    )

    stageviz.command.show_paths(["/World/A"], recursive=True)

    _assert(
        _wait_until(lambda: str(_visibility("/World/A/A1")) != "invisible"),
        "recursive show restores visible/inherited on child",
    )


def test_new_rename_move_delete():
    stageviz.command.new_xform("/World", "Created")

    _assert(
        _wait_until(lambda: _exists("/World/Created")),
        "new_xform creates prim",
    )
    _assert(
        _wait_until(lambda: _selection() == ["/World/Created"]),
        "new_xform selects created prim",
    )

    stageviz.command.rename_path("/World/Created", "Renamed")

    _assert(
        _wait_until(lambda: not _exists("/World/Created") and _exists("/World/Renamed")),
        "rename_path renames prim",
    )
    _assert(
        _wait_until(lambda: _selection() == ["/World/Renamed"]),
        "rename_path remaps selection",
    )

    stageviz.command.move_path(["/World/Renamed"], "/World/B", insert_index=-1)

    _assert(
        _wait_until(lambda: not _exists("/World/Renamed") and _exists("/World/B/Renamed")),
        "move_path reparents prim",
    )
    _assert(
        _wait_until(lambda: _selection() == ["/World/B/Renamed"]),
        "move_path remaps selection",
    )

    stageviz.command.delete_paths(["/World/B/Renamed"])

    _assert(
        _wait_until(lambda: not _exists("/World/B/Renamed")),
        "delete_paths removes prim",
    )


def test_new_xform_root_parent():
    stageviz.command.new_xform("/", "RootGroup")

    _assert(
        _wait_until(lambda: _exists("/RootGroup")),
        "new_xform can create below absolute root",
    )

    _assert(
        _wait_until(lambda: _selection() == ["/RootGroup"]),
        "new_xform below root selects created root prim",
    )


def test_new_xform_unique_name():
    stageviz.command.new_xform("/World", "A")

    _assert(
        _wait_until(lambda: _exists("/World/A_1") or _exists("/World/A1")),
        "new_xform creates unique name when sibling exists",
    )


def test_new_xform_wraps_multiple_selection():
    stageviz.command.select_paths(["/World/A", "/World/B"])

    _assert(
        _wait_until(lambda: _selection() == ["/World/A", "/World/B"]),
        "selection prepared for multi-new_xform",
    )

    before_order = _child_names("/World")

    stageviz.command.new_xform("/World", "Group")

    _assert(
        _wait_until(
            lambda: _exists("/World/Group")
            and _exists("/World/Group/A")
            and _exists("/World/Group/B")
            and not _exists("/World/A")
            and not _exists("/World/B")
        ),
        "new_xform wraps selected siblings",
    )

    _assert(
        _wait_until(lambda: _selection() == ["/World/Group"]),
        "new_xform selects wrapper prim",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/A")
                and _exists("/World/B")
                and not _exists("/World/Group")
            ),
            "undo new_xform wrapper restores moved prims",
        )

        _assert_equal(
            _selection(),
            ["/World/A", "/World/B"],
            "undo new_xform restores previous selection",
        )

        _assert_equal(
            _child_names("/World"),
            before_order,
            "undo new_xform restores child order",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: _exists("/World/Group")
                and _exists("/World/Group/A")
                and _exists("/World/Group/B")
            ),
            "redo new_xform wrapper reapplies move",
        )


def test_new_xform_minimal_root_selection():
    stageviz.command.select_paths(["/World/A", "/World/A/A1", "/World/B"])

    stageviz.command.new_xform("/World", "MinimalGroup")

    _assert(
        _wait_until(
            lambda: _exists("/World/MinimalGroup/A")
            and _exists("/World/MinimalGroup/A/A1")
            and _exists("/World/MinimalGroup/B")
            and not _exists("/World/A")
            and not _exists("/World/B")
        ),
        "new_xform wraps only minimal root paths from selection",
    )

    _assert(
        not _exists("/World/MinimalGroup/A/A1/A1"),
        "new_xform does not duplicate descendant selection",
    )


def test_rename_remaps_child_selection_and_mask():
    stageviz.command.select_paths(["/World/A/A1"])
    stageviz.command.isolate_paths(["/World/A/A2"])

    before_order = _child_names("/World")

    stageviz.command.rename_path("/World/A", "ARenamed")

    _assert(
        _wait_until(
            lambda: _exists("/World/ARenamed")
            and _exists("/World/ARenamed/A1")
            and not _exists("/World/A")
        ),
        "rename_path renames parent with children",
    )

    _assert_equal(
        _selection(),
        ["/World/ARenamed/A1"],
        "rename_path remaps child selection",
    )

    _assert_equal(
        _mask(),
        ["/World/ARenamed/A2"],
        "rename_path remaps child mask",
    )

    expected_order = ["ARenamed" if name == "A" else name for name in before_order]
    _assert_equal(
        _child_names("/World"),
        expected_order,
        "rename_path preserves child order with renamed token",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _exists("/World/A") and not _exists("/World/ARenamed")),
            "undo rename_path restores old prim path",
        )

        _assert_equal(
            _selection(),
            ["/World/A/A1"],
            "undo rename_path restores previous selection",
        )

        _assert_equal(
            _mask(),
            ["/World/A/A2"],
            "undo rename_path restores previous mask",
        )

        _assert_equal(
            _child_names("/World"),
            before_order,
            "undo rename_path restores child order",
        )


def test_rename_noop_is_safe():
    stageviz.command.select_paths(["/World/A"])
    stageviz.command.isolate_paths(["/World/B"])

    before_selection = _selection()
    before_mask = _mask()
    before_order = _child_names("/World")

    stageviz.command.rename_path("/World/A", "A")

    _assert(
        _wait_until(lambda: _exists("/World/A")),
        "rename noop leaves prim valid",
    )

    _assert_equal(
        _selection(),
        before_selection,
        "rename noop preserves selection",
    )

    _assert_equal(
        _mask(),
        before_mask,
        "rename noop preserves mask",
    )

    _assert_equal(
        _child_names("/World"),
        before_order,
        "rename noop preserves child order",
    )


def test_move_selection_mask_and_insert_order():
    stageviz.command.select_paths(["/World/A/A1"])
    stageviz.command.isolate_paths(["/World/A/A2"])

    before_world_order = _child_names("/World")
    before_b_order = _child_names("/World/B")

    stageviz.command.move_path(["/World/A"], "/World/B", insert_index=0)

    _assert(
        _wait_until(
            lambda: _exists("/World/B/A")
            and _exists("/World/B/A/A1")
            and not _exists("/World/A")
        ),
        "move_path moves parent and children",
    )

    _assert_equal(
        _selection(),
        ["/World/B/A/A1"],
        "move_path remaps child selection",
    )

    _assert_equal(
        _mask(),
        ["/World/B/A/A2"],
        "move_path remaps child mask",
    )

    _assert_equal(
        _child_names("/World/B")[0],
        "A",
        "move_path insert_index inserts moved name at requested position",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/A")
                and _exists("/World/A/A1")
                and not _exists("/World/B/A")
            ),
            "undo move_path restores old path",
        )

        _assert_equal(
            _selection(),
            ["/World/A/A1"],
            "undo move_path restores previous selection",
        )

        _assert_equal(
            _mask(),
            ["/World/A/A2"],
            "undo move_path restores previous mask",
        )

        _assert_equal(
            _child_names("/World"),
            before_world_order,
            "undo move_path restores source parent order",
        )

        _assert_equal(
            _child_names("/World/B"),
            before_b_order,
            "undo move_path restores destination parent order",
        )


def test_move_multiple_siblings():
    stageviz.command.move_path(["/World/A/A1", "/World/A/A2"], "/World/B", insert_index=1)

    _assert(
        _wait_until(
            lambda: _exists("/World/B/A1")
            and _exists("/World/B/A2")
            and not _exists("/World/A/A1")
            and not _exists("/World/A/A2")
        ),
        "move_path moves multiple siblings",
    )

    names = _child_names("/World/B")
    _assert(
        names.index("A1") < names.index("A2"),
        "move_path preserves moved item order",
    )


def test_move_rejects_self_parenting():
    before = _children("/World/A")

    stageviz.command.move_path(["/World/A"], "/World/A/A1", insert_index=-1)

    _assert(
        _wait_until(lambda: _exists("/World/A") and _exists("/World/A/A1")),
        "invalid move below itself leaves source intact",
    )

    _assert_equal(
        _children("/World/A"),
        before,
        "invalid move below itself preserves children",
    )


def test_move_rejects_destination_collision():
    before_world = _children("/World")
    before_b = _children("/World/B")

    stageviz.command.move_path(["/World/A"], "/World/B", insert_index=-1)

    _assert(
        _wait_until(lambda: _exists("/World/A") and _exists("/World/B/A")),
        "first move creates collision fixture",
    )

    stageviz.command.move_path(["/World/C"], "/World/B", insert_index=-1)
    _assert(
        _wait_until(lambda: _exists("/World/B/C")),
        "control move without collision succeeds",
    )

    stageviz.command.move_path(["/World/D"], "/World/B", insert_index=-1)
    _assert(
        _wait_until(lambda: _exists("/World/B/D")),
        "second control move succeeds",
    )

    stageviz.command.move_path(["/World/B/C"], "/World/B", insert_index=-1)

    _assert(
        _wait_until(lambda: _exists("/World/B/C")),
        "move to same parent/path is safe/no-op",
    )


def test_delete_removes_selection_mask_and_restores_default_prim():
    stageviz.command.select_paths(["/World/A/A1", "/World/B"])
    stageviz.command.isolate_paths(["/World/A/A2", "/World/C"])

    stageviz.command.set_default_prim("/World")
    _assert_equal(_default_prim_path(), "/World", "default prim prepared")

    before_world_order = _child_names("/World")

    stageviz.command.delete_paths(["/World/A"])

    _assert(
        _wait_until(lambda: not _exists("/World/A")),
        "delete_paths removes parent prim",
    )

    _assert_equal(
        _selection(),
        ["/World/B"],
        "delete_paths removes affected selected child paths",
    )

    _assert_equal(
        _mask(),
        ["/World/C"],
        "delete_paths removes affected masked child paths",
    )

    _assert_equal(
        _default_prim_path(),
        "/World",
        "delete non-default prim keeps default prim",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _exists("/World/A") and _exists("/World/A/A1")),
            "undo delete_paths restores deleted prim hierarchy",
        )

        _assert_equal(
            _selection(),
            ["/World/A/A1", "/World/B"],
            "undo delete_paths restores previous selection",
        )

        _assert_equal(
            _mask(),
            ["/World/A/A2", "/World/C"],
            "undo delete_paths restores previous mask",
        )

        _assert_equal(
            _child_names("/World"),
            before_world_order,
            "undo delete_paths restores child order",
        )


def test_delete_default_prim_and_undo():
    stageviz.command.set_default_prim("/World")
    _assert_equal(_default_prim_path(), "/World", "default prim prepared")

    stageviz.command.delete_paths(["/World"])

    _assert(
        _wait_until(lambda: not _exists("/World")),
        "delete_paths removes default prim",
    )

    _assert_equal(
        _default_prim_path(),
        "",
        "delete_paths clears default prim when deleting it",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _exists("/World")),
            "undo delete default prim restores /World",
        )

        _assert_equal(
            _default_prim_path(),
            "/World",
            "undo delete default prim restores default prim",
        )


def test_default_prim_validation():
    stageviz.command.set_default_prim("/World/A")

    _assert_equal(
        _default_prim_path(),
        "/World",
        "set_default_prim rejects non-root prim",
    )

    stageviz.command.set_default_prim("/OtherRoot")

    _assert_equal(
        _default_prim_path(),
        "/OtherRoot",
        "set_default_prim accepts root prim",
    )

    if _undo():
        _assert_equal(
            _default_prim_path(),
            "/World",
            "undo set_default_prim restores previous default prim",
        )


def test_stage_up():
    stageviz.command.set_stage_up(stageviz.StageUpZ)

    _assert(
        _wait_until(lambda: stageviz.session().stageUp() == stageviz.StageUpZ),
        "set_stage_up sets Z",
    )

    if _undo():
        _assert(
            _wait_until(lambda: stageviz.session().stageUp() == stageviz.StageUpY),
            "undo set_stage_up restores Y",
        )

    stageviz.command.set_stage_up(stageviz.StageUpZ)
    stageviz.command.set_stage_up(stageviz.StageUpY)

    _assert(
        _wait_until(lambda: stageviz.session().stageUp() == stageviz.StageUpY),
        "set_stage_up sets Y",
    )


def test_payload_load_unload():
    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(
            lambda: bool(
                _prim("/World/PayloadA")
                and _prim("/World/PayloadA").IsLoaded()
            ),
            timeout=5.0,
        ),
        "load_payloads loads payload",
    )

    stageviz.command.select_paths(["/World/PayloadA/Geom"])
    stageviz.command.isolate_paths(["/World/PayloadA/Geom"])

    stageviz.command.unload_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(
            lambda: bool(
                _prim("/World/PayloadA")
                and not _prim("/World/PayloadA").IsLoaded()
            ),
            timeout=5.0,
        ),
        "unload_payloads unloads payload",
    )

    _assert_equal(
        _selection(),
        [],
        "unload_payloads removes affected payload child selection",
    )

    _assert_equal(
        _mask(),
        [],
        "unload_payloads removes affected payload child mask",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: bool(
                    _prim("/World/PayloadA")
                    and _prim("/World/PayloadA").IsLoaded()
                ),
                timeout=5.0,
            ),
            "undo unload_payloads reloads payload",
        )

        _assert_equal(
            _selection(),
            ["/World/PayloadA/Geom"],
            "undo unload_payloads restores previous selection",
        )

        _assert_equal(
            _mask(),
            ["/World/PayloadA/Geom"],
            "undo unload_payloads restores previous mask",
        )


def test_select_invert_payload():
    if not _has_command("select_invert_payload"):
        print("[skip] select_invert_payload is not bound")
        return

    stageviz.command.load_payloads(["/World/PayloadA", "/World/PayloadB"])

    _assert(
        _wait_until(
            lambda: bool(
                _prim("/World/PayloadA")
                and _prim("/World/PayloadA").IsLoaded()
                and _prim("/World/PayloadB")
                and _prim("/World/PayloadB").IsLoaded()
            ),
            timeout=5.0,
        ),
        "payloads prepared for select_invert_payload",
    )

    stageviz.command.select_paths(["/World/PayloadA"])
    stageviz.command.select_invert_payload()

    _assert_equal(
        _selection(),
        ["/World/PayloadB"],
        "select_invert_payload selects loaded payloads not already selected",
    )

    if _undo():
        _assert_equal(
            _selection(),
            ["/World/PayloadA"],
            "undo select_invert_payload restores previous payload selection",
        )


def test_save_reload(saved_path):
    ok = stageviz.session().save(saved_path)
    _assert(ok, "session.save writes test stage")

    reopened = Usd.Stage.Open(saved_path)
    _assert(bool(reopened), "saved stage reopens with USD")
    _assert(bool(reopened.GetPrimAtPath("/World")), "saved stage contains /World")


def run():
    root, main_path, saved_path = create_fixture()
    print(f"Stageviz command test root: {root}")

    if hasattr(stageviz.session(), "setPreserveState"):
        stageviz.session().setPreserveState(False)

    tests = [
        ("select paths", test_select_paths),
        ("select all and invert", test_select_all_and_invert),
        ("isolate paths", test_isolate_paths),
        ("show/hide paths", test_show_hide_paths),
        ("new/rename/move/delete", test_new_rename_move_delete),
        ("new_xform root parent", test_new_xform_root_parent),
        ("new_xform unique name", test_new_xform_unique_name),
        ("new_xform wraps multiple selection", test_new_xform_wraps_multiple_selection),
        ("new_xform minimal root selection", test_new_xform_minimal_root_selection),
        ("rename remaps child selection and mask", test_rename_remaps_child_selection_and_mask),
        ("rename noop is safe", test_rename_noop_is_safe),
        ("move selection/mask and insert order", test_move_selection_mask_and_insert_order),
        ("move multiple siblings", test_move_multiple_siblings),
        ("move rejects self parenting", test_move_rejects_self_parenting),
        ("move rejects destination collision/no-op", test_move_rejects_destination_collision),
        ("delete selection/mask and default prim", test_delete_removes_selection_mask_and_restores_default_prim),
        ("delete default prim and undo", test_delete_default_prim_and_undo),
        ("default prim validation", test_default_prim_validation),
        ("stage up", test_stage_up),
        ("payload load/unload", test_payload_load_unload),
        ("select invert payload", test_select_invert_payload),
        ("save/reopen", lambda: test_save_reload(saved_path)),
    ]

    for label, func in tests:
        reload_fixture(main_path)
        _run(label, func)

    print("\n=== Stageviz command test summary ===")
    if _FAILURES:
        print(f"FAILED: {len(_FAILURES)} failure(s)")
        for failure in _FAILURES:
            print(f" - {failure}")
        return False

    print("PASSED")
    return True


if __name__ == "__main__":
    run()