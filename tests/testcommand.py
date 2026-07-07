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

from pxr import Usd, UsdGeom
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


def _path_list(values):
    return [str(v) for v in values]


def _process_events():
    """
    Flush Qt events for the embedded test script, but avoid unrestricted
    processEvents(), which can make command tests more re-entrant than needed.
    """
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


def _define_xform(stage, path):
    return UsdGeom.Xform.Define(stage, path).GetPrim()


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
    _define_xform(stage, "/World/B")
    _define_xform(stage, "/World/B/B1")
    _define_xform(stage, "/World/C")

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


def test_select_paths():
    stageviz.command.select_paths(["/World/A", "/World/B"])

    _assert(
        _wait_until(lambda: _selection() == ["/World/A", "/World/B"]),
        "select_paths updates selection",
    )


def test_select_all_and_invert():
    stageviz.command.select_all()

    _assert(
        _wait_until(
            lambda: "/World/A/A1" in set(_selection())
            and "/World/B/B1" in set(_selection())
        ),
        "select_all includes expected leaf paths",
    )

    before = set(_selection())
    stageviz.command.select_invert()

    _assert(
        _wait_until(lambda: set(_selection()) != before),
        "select_invert changes selection domain",
    )


def test_isolate_paths():
    stageviz.command.isolate_paths(["/World/A"])

    _assert(
        _wait_until(lambda: _mask() == ["/World/A"]),
        "isolate_paths sets mask",
    )


def test_show_hide_paths():
    stageviz.command.hide_paths(["/World/A"], recursive=False)

    _assert(
        _wait_until(lambda: str(_visibility("/World/A")) == "invisible"),
        "hide_paths authors invisible",
    )

    stageviz.command.show_paths(["/World/A"], recursive=False)

    _assert(
        _wait_until(lambda: str(_visibility("/World/A")) != "invisible"),
        "show_paths restores visible/inherited",
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

    stageviz.command.move_path("/World/Renamed", "/World/B", insert_index=-1)

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


def test_default_prim():
    stageviz.command.set_default_prim("/World")

    _assert(
        _wait_until(
            lambda: bool(_stage().GetDefaultPrim())
            and str(_stage().GetDefaultPrim().GetPath()) == "/World"
        ),
        "set_default_prim sets /World",
    )


def test_stage_up():
    stageviz.command.set_stage_up(stageviz.StageUpZ)

    _assert(
        _wait_until(lambda: stageviz.session().stageUp() == stageviz.StageUpZ),
        "set_stage_up sets Z",
    )

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


def test_save_reload(saved_path):
    ok = stageviz.session().save(saved_path)
    _assert(ok, "session.save writes test stage")

    reopened = Usd.Stage.Open(saved_path)
    _assert(bool(reopened), "saved stage reopens with USD")
    _assert(bool(reopened.GetPrimAtPath("/World")), "saved stage contains /World")


def run():
    root, main_path, saved_path = create_fixture()
    print(f"Stageviz command test root: {root}")

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

    tests = [
        ("select paths", test_select_paths),
        ("select all and invert", test_select_all_and_invert),
        ("isolate paths", test_isolate_paths),
        ("show/hide paths", test_show_hide_paths),
        ("new/rename/move/delete", test_new_rename_move_delete),
        ("default prim", test_default_prim),
        ("stage up", test_stage_up),
        ("payload load/unload", test_payload_load_unload),
        ("save/reopen", lambda: test_save_reload(saved_path)),
    ]

    for label, func in tests:
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