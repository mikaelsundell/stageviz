# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""
Stageviz smoke/regression tests.

Run from the Stageviz Python editor.
"""

import os
import tempfile
import time
import traceback

from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade
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


def _disable_preserve_state():
    session = stageviz.session()
    if hasattr(session, "setPreserveState"):
        session.setPreserveState(False)


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


def _run(label, func, args=()):
    print(f"\n--- {label} ---")
    try:
        func(*args)
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


def _root_layer():
    stage = _stage()
    return stage.GetRootLayer() if stage else None


def _root_has_property(path):
    layer = _root_layer()
    if not layer:
        return False
    return bool(layer.GetPropertyAtPath(Sdf.Path(path)))


def _root_has_prim(path):
    layer = _root_layer()
    if not layer:
        return False
    return bool(layer.GetPrimAtPath(Sdf.Path(path)))


def _define_xform(stage, path):
    return UsdGeom.Xform.Define(stage, path).GetPrim()


def _world_transform(path):
    prim = _prim(path)
    if not prim:
        return None
    return UsdGeom.Xformable(prim).ComputeLocalToWorldTransform(Usd.TimeCode.Default())


def _matrix_close(actual, expected, tolerance=1e-8):
    if actual is None or expected is None:
        return False

    for row in range(4):
        for column in range(4):
            if abs(actual[row][column] - expected[row][column]) > tolerance:
                return False

    return True


def _set_translate(path, value):
    prim = _prim(path)
    if not prim:
        return False

    xform = UsdGeom.Xformable(prim)
    xform.ClearXformOpOrder()
    xform.AddTranslateOp().Set(Gf.Vec3d(*value))
    return True


def _set_complex_transform(path, translate, rotate, scale):
    prim = _prim(path)
    if not prim:
        return False

    xform = UsdGeom.Xformable(prim)
    xform.ClearXformOpOrder()
    xform.AddTranslateOp().Set(Gf.Vec3d(*translate))
    xform.AddRotateXYZOp().Set(Gf.Vec3f(*rotate))
    xform.AddScaleOp().Set(Gf.Vec3f(*scale))
    return True


def _has_command(name):
    return hasattr(stageviz.command, name)


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


def _create_external_file(path):
    stage = Usd.Stage.CreateNew(path)
    root = _define_xform(stage, "/ExternalRoot")
    _define_xform(stage, "/ExternalRoot/Child")
    stage.SetDefaultPrim(root)
    stage.GetRootLayer().Save()


def _create_merge_source(path, external):
    stage = Usd.Stage.CreateNew(path)

    world = _define_xform(stage, "/World")
    _define_xform(stage, "/World/Merged")
    stage.SetDefaultPrim(world)

    referenced = _define_xform(stage, "/World/MergedReference")
    referenced.GetReferences().AddReference(
        os.path.basename(external),
        "/ExternalRoot",
    )

    stage.GetRootLayer().Save()


def _create_main_file(path, payload_a, payload_b, external):
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

    referenced = _define_xform(stage, "/World/Referenced")
    referenced.GetReferences().AddReference(os.path.basename(external), "/ExternalRoot")

    stage.GetRootLayer().subLayerPaths.append(os.path.basename(external))

    p1 = _define_xform(stage, "/World/PayloadA")
    p1.GetPayloads().AddPayload(os.path.basename(payload_a), "/PayloadA")

    p2 = _define_xform(stage, "/World/PayloadB")
    p2.GetPayloads().AddPayload(os.path.basename(payload_b), "/PayloadB")

    stage.GetRootLayer().Save()


def create_fixture():
    root = tempfile.mkdtemp(prefix="stageviz_command_tests_")
    payload_a = os.path.join(root, "payload_a.usda")
    payload_b = os.path.join(root, "payload_b.usda")
    external = os.path.join(root, "external.usda")
    main = os.path.join(root, "command_test.usda")
    merge_source = os.path.join(root, "merge_source.usda")
    saved = os.path.join(root, "command_test_saved.usda")

    _create_payload_file(payload_a, "PayloadA")
    _create_payload_file(payload_b, "PayloadB")
    _create_external_file(external)
    _create_main_file(main, payload_a, payload_b, external)
    _create_merge_source(merge_source, external)

    return root, main, merge_source, saved, external


def reload_fixture(main_path):
    _disable_preserve_state()

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

    _wait_until(lambda: _selection() == [], timeout=1.0)
    _wait_until(lambda: _mask() == [], timeout=1.0)


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
    stageviz.command.select_all(recursive=False)

    _assert(
        _wait_until(
            lambda: set(_selection()) == {
                "/ExternalRoot",
                "/World",
                "/OtherRoot",
            }
        ),
        "select_all non-recursive selects root prims only",
    )

    stageviz.command.select_paths([])

    _assert(
        _wait_until(lambda: _selection() == []),
        "selection cleared before recursive select_all",
    )

    expected_recursive = {
        str(prim.GetPath())
        for prim in _stage().Traverse()
        if prim and prim.IsValid()
    }

    _assert(
        "/World/PayloadA" not in expected_recursive
        and "/World/PayloadB" not in expected_recursive,
        "unloaded payload prims are excluded from recursive traversal",
    )

    stageviz.command.select_all(recursive=True)

    _assert(
        _wait_until(
            lambda: set(_selection()) == expected_recursive
        ),
        "select_all recursive selects exactly the traversable prims",
    )

    before = set(_selection())
    stageviz.command.select_invert()

    _assert(
        _wait_until(lambda: set(_selection()) != before),
        "select_invert changes selection domain",
    )

def test_isolate_paths():
    stageviz.command.select_paths(["/World/A/A1"])

    _assert(
        _wait_until(lambda: _selection() == ["/World/A/A1"]),
        "selection prepared for isolate_paths",
    )

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



def test_new_prim():
    _assert(
        _has_command("new_prim"),
        "new_prim command is exposed",
    )

    if not _has_command("new_prim"):
        return

    before_order = _child_names("/World")

    stageviz.command.new_prim("/World", "GenericPrim")

    _assert(
        _wait_until(lambda: _exists("/World/GenericPrim")),
        "new_prim creates untyped prim",
    )

    prim = _prim("/World/GenericPrim")
    _assert(
        bool(prim and not prim.GetTypeName()),
        "new_prim without type creates untyped prim",
    )

    _assert_equal(
        _selection(),
        ["/World/GenericPrim"],
        "new_prim selects created prim",
    )

    _assert_equal(
        _child_names("/World"),
        before_order + ["GenericPrim"],
        "new_prim appends child order",
    )

    if _undo():
        _assert(
            _wait_until(lambda: not _exists("/World/GenericPrim")),
            "undo new_prim removes created prim",
        )

        _assert_equal(
            _child_names("/World"),
            before_order,
            "undo new_prim restores child order",
        )

    if _redo():
        _assert(
            _wait_until(lambda: _exists("/World/GenericPrim")),
            "redo new_prim recreates prim",
        )


def test_new_typed_prim():
    if not _has_command("new_prim"):
        print("[skip] new_prim is not bound")
        return

    stageviz.command.new_prim("/World", "CameraPrim", "Camera")

    _assert(
        _wait_until(lambda: _exists("/World/CameraPrim")),
        "new_prim creates typed prim",
    )

    prim = _prim("/World/CameraPrim")
    _assert_equal(
        str(prim.GetTypeName()) if prim else "",
        "Camera",
        "new_prim preserves requested USD type",
    )


def test_new_prim_unique_name():
    if not _has_command("new_prim"):
        print("[skip] new_prim is not bound")
        return

    stageviz.command.new_prim("/World", "A", "Xform")

    _assert(
        _wait_until(lambda: _exists("/World/A_1") or _exists("/World/A1")),
        "new_prim creates unique sibling name",
    )


def test_new_scope():
    _assert(
        _has_command("new_scope"),
        "new_scope command is exposed",
    )

    if not _has_command("new_scope"):
        return

    stageviz.command.new_scope("/World", "Looks")

    _assert(
        _wait_until(lambda: _exists("/World/Looks")),
        "new_scope creates scope",
    )

    prim = _prim("/World/Looks")
    _assert_equal(
        str(prim.GetTypeName()) if prim else "",
        "Scope",
        "new_scope creates USD Scope type",
    )

    _assert_equal(
        _selection(),
        ["/World/Looks"],
        "new_scope selects created scope",
    )

    if _undo():
        _assert(
            _wait_until(lambda: not _exists("/World/Looks")),
            "undo new_scope removes scope",
        )


def test_new_material():
    _assert(
        _has_command("new_material"),
        "new_material command is exposed",
    )

    if not _has_command("new_material"):
        return

    if not _exists("/World/Looks"):
        UsdGeom.Scope.Define(_stage(), "/World/Looks")

    stageviz.command.new_material("/World/Looks", "TestMaterial")

    material_path = "/World/Looks/TestMaterial"
    shader_path = f"{material_path}/PreviewSurface"

    _assert(
        _wait_until(lambda: _exists(material_path) and _exists(shader_path)),
        "new_material creates material and preview shader",
    )

    material_prim = _prim(material_path)
    shader_prim = _prim(shader_path)

    _assert_equal(
        str(material_prim.GetTypeName()) if material_prim else "",
        "Material",
        "new_material creates UsdShadeMaterial",
    )

    _assert_equal(
        str(shader_prim.GetTypeName()) if shader_prim else "",
        "Shader",
        "new_material creates child Shader",
    )

    shader = UsdShade.Shader(shader_prim) if shader_prim else None
    shader_id = shader.GetIdAttr().Get() if shader else None

    _assert_equal(
        str(shader_id) if shader_id is not None else "",
        "UsdPreviewSurface",
        "new_material assigns UsdPreviewSurface shader id",
    )

    material = UsdShade.Material(material_prim) if material_prim else None
    surface_output = material.GetSurfaceOutput() if material else None
    source = surface_output.GetConnectedSource() if surface_output else None

    _assert(
        bool(source),
        "new_material connects material surface output",
    )

    _assert_equal(
        _selection(),
        [material_path],
        "new_material selects created material",
    )

    if _undo():
        _assert(
            _wait_until(lambda: not _exists(material_path)),
            "undo new_material removes complete material hierarchy",
        )

    if _redo():
        _assert(
            _wait_until(lambda: _exists(material_path) and _exists(shader_path)),
            "redo new_material restores material hierarchy",
        )


def test_new_reference(external):
    _assert(
        _has_command("new_reference"),
        "new_reference command is exposed",
    )

    if not _has_command("new_reference"):
        return

    stageviz.command.new_reference(
        "/World",
        "CreatedReference",
        external,
        "/ExternalRoot",
    )

    path = "/World/CreatedReference"

    _assert(
        _wait_until(lambda: _exists(path) and _exists(f"{path}/Child")),
        "new_reference creates composed reference hierarchy",
    )

    prim = _prim(path)
    references = prim.GetMetadata("references") if prim else None
    has_reference = bool(
        references
        and references.GetAddedOrExplicitItems()
    )

    _assert(
        has_reference,
        "new_reference authors reference arc",
    )

    _assert_equal(
        _selection(),
        [path],
        "new_reference selects created prim",
    )

    if _undo():
        _assert(
            _wait_until(lambda: not _exists(path)),
            "undo new_reference removes reference prim",
        )

    if _redo():
        _assert(
            _wait_until(lambda: _exists(path) and _exists(f"{path}/Child")),
            "redo new_reference restores composed hierarchy",
        )


def test_new_reference_default_prim(external):
    if not _has_command("new_reference"):
        print("[skip] new_reference is not bound")
        return

    stageviz.command.new_reference(
        "/World",
        "DefaultReference",
        external,
    )

    _assert(
        _wait_until(lambda: _exists("/World/DefaultReference/Child")),
        "new_reference empty prim path uses referenced default prim",
    )


def test_new_payload(external):
    _assert(
        _has_command("new_payload"),
        "new_payload command is exposed",
    )

    if not _has_command("new_payload"):
        return

    stageviz.command.new_payload(
        "/World",
        "CreatedPayload",
        external,
        "/ExternalRoot",
    )

    path = "/World/CreatedPayload"

    _assert(
        _wait_until(lambda: bool(_prim(path) and _prim(path).HasPayload())),
        "new_payload authors payload arc",
    )

    _assert(
        _wait_until(lambda: _exists(f"{path}/Child"), timeout=5.0),
        "new_payload composes payload hierarchy",
    )

    _assert_equal(
        _selection(),
        [path],
        "new_payload selects created prim",
    )

    if _undo():
        _assert(
            _wait_until(lambda: not _exists(path)),
            "undo new_payload removes payload prim",
        )

    if _redo():
        _assert(
            _wait_until(lambda: bool(_prim(path) and _prim(path).HasPayload())),
            "redo new_payload restores payload arc",
        )


def test_new_payload_default_prim(external):
    if not _has_command("new_payload"):
        print("[skip] new_payload is not bound")
        return

    stageviz.command.new_payload(
        "/World",
        "DefaultPayload",
        external,
    )

    _assert(
        _wait_until(lambda: _exists("/World/DefaultPayload/Child"), timeout=5.0),
        "new_payload empty prim path uses payload default prim",
    )


def test_duplicate_paths():
    _assert(
        _has_command("duplicate_paths"),
        "duplicate_paths command is exposed",
    )

    if not _has_command("duplicate_paths"):
        return

    before_order = _child_names("/World")

    stageviz.command.duplicate_paths(["/World/A"])

    duplicate_candidates = [
        name for name in _child_names("/World")
        if name not in before_order
    ]

    _assert(
        _wait_until(
            lambda: any(
                _exists(f"/World/{name}")
                for name in _child_names("/World")
                if name not in before_order
            )
        ),
        "duplicate_paths creates sibling prim",
    )

    duplicate_candidates = [
        name for name in _child_names("/World")
        if name not in before_order
    ]
    duplicate_name = duplicate_candidates[0] if duplicate_candidates else ""
    duplicate_path = f"/World/{duplicate_name}" if duplicate_name else ""

    _assert(
        bool(duplicate_path),
        "duplicate_paths resolves created duplicate path",
    )

    if duplicate_path:
        _assert(
            _exists(f"{duplicate_path}/A1") and _exists(f"{duplicate_path}/A2"),
            "duplicate_paths copies descendant specs",
        )

        _assert_equal(
            _selection(),
            [duplicate_path],
            "duplicate_paths selects new duplicate",
        )

    if _undo():
        if duplicate_path:
            _assert(
                _wait_until(lambda: not _exists(duplicate_path)),
                "undo duplicate_paths removes duplicate",
            )

        _assert_equal(
            _child_names("/World"),
            before_order,
            "undo duplicate_paths restores child order",
        )

    if _redo():
        _assert(
            _wait_until(lambda: len(_child_names("/World")) == len(before_order) + 1),
            "redo duplicate_paths restores duplicate",
        )


def test_duplicate_composed_payload_prim():
    if not _has_command("duplicate_paths"):
        print("[skip] duplicate_paths is not bound")
        return

    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(lambda: _exists("/World/PayloadA/Geom"), timeout=5.0),
        "payload prepared for duplicate composed prim",
    )

    before = set(_child_names("/World/PayloadA"))

    stageviz.command.duplicate_paths(["/World/PayloadA/Geom"])

    _assert(
        _wait_until(
            lambda: len(set(_child_names("/World/PayloadA")) - before) == 1
        ),
        "duplicate_paths copies strongest payload-owned prim spec into root layer",
    )

    added = list(set(_child_names("/World/PayloadA")) - before)
    duplicate_path = f"/World/PayloadA/{added[0]}" if added else ""

    _assert(
        bool(duplicate_path and _root_has_prim(duplicate_path)),
        "duplicate of payload-owned prim is authored in root layer",
    )

    if _undo() and duplicate_path:
        _assert(
            _wait_until(lambda: not _exists(duplicate_path)),
            "undo duplicate payload-owned prim removes root-layer copy",
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

    _assert(
        _wait_until(lambda: _selection() == ["/World/A", "/World/A/A1", "/World/B"]),
        "selection prepared for minimal root new_xform",
    )

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
    _assert(
        _wait_until(lambda: _selection() == ["/World/A/A1"]),
        "selection prepared for rename remap",
    )

    stageviz.command.isolate_paths(["/World/A/A2"])
    _assert(
        _wait_until(lambda: _mask() == ["/World/A/A2"]),
        "mask prepared for rename remap",
    )

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
    _assert(
        _wait_until(lambda: _selection() == ["/World/A"]),
        "selection prepared for rename noop",
    )

    stageviz.command.isolate_paths(["/World/B"])
    _assert(
        _wait_until(lambda: _mask() == ["/World/B"]),
        "mask prepared for rename noop",
    )

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
    _assert(
        _wait_until(lambda: _selection() == ["/World/A/A1"]),
        "selection prepared for move remap",
    )

    stageviz.command.isolate_paths(["/World/A/A2"])
    _assert(
        _wait_until(lambda: _mask() == ["/World/A/A2"]),
        "mask prepared for move remap",
    )

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


def test_move_preserve_world_transform():
    _assert(_set_translate("/World/A", (10.0, 2.0, -3.0)), "set source parent transform")
    _assert(_set_translate("/World/A/A1", (4.0, -1.0, 2.0)), "set preserved child transform")
    _assert(_set_translate("/World/A/A2", (4.0, -1.0, 2.0)), "set non-preserved child transform")
    _assert(_set_translate("/World/B", (-7.0, 5.0, 1.0)), "set destination parent transform")

    preserved_before = _world_transform("/World/A/A1")
    changed_before = _world_transform("/World/A/A2")

    _assert(
        preserved_before is not None,
        "capture preserved child world transform before move",
    )

    _assert(
        changed_before is not None,
        "capture non-preserved child world transform before move",
    )

    stageviz.command.move_path(
        ["/World/A/A1"],
        "/World/B",
        insert_index=-1,
        preserve_world_transform=True,
    )

    _assert(
        _wait_until(
            lambda: _exists("/World/B/A1")
            and not _exists("/World/A/A1")
        ),
        "move_path reparents transformed child with preserve_world_transform",
    )

    preserved_after = _world_transform("/World/B/A1")
    _assert(
        _matrix_close(preserved_after, preserved_before),
        "move_path preserves world transform when requested",
    )

    stageviz.command.move_path(
        ["/World/A/A2"],
        "/World/B",
        insert_index=-1,
        preserve_world_transform=False,
    )

    _assert(
        _wait_until(
            lambda: _exists("/World/B/A2")
            and not _exists("/World/A/A2")
        ),
        "move_path reparents transformed child without preserving world transform",
    )

    changed_after = _world_transform("/World/B/A2")
    _assert(
        not _matrix_close(changed_after, changed_before),
        "move_path allows world transform to change when preservation is disabled",
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
    stageviz.command.move_path(["/World/A"], "/World/B", insert_index=-1)

    _assert(
        _wait_until(lambda: not _exists("/World/A") and _exists("/World/B/A")),
        "first move creates destination child",
    )

    before_b = _children("/World/B")

    stageviz.command.move_path(["/World/B/A"], "/World/B", insert_index=-1)

    _assert(
        _wait_until(lambda: _exists("/World/B/A")),
        "move to same parent/path is safe/no-op",
    )

    _assert_equal(
        _children("/World/B"),
        before_b,
        "move to same parent/path preserves destination children",
    )


def test_delete_removes_selection_mask_and_restores_default_prim():
    stageviz.command.select_paths(["/World/A/A1", "/World/B"])
    _assert(
        _wait_until(lambda: _selection() == ["/World/A/A1", "/World/B"]),
        "selection prepared for delete",
    )

    stageviz.command.isolate_paths(["/World/A/A2", "/World/C"])
    _assert(
        _wait_until(lambda: _mask() == ["/World/A/A2", "/World/C"]),
        "mask prepared for delete",
    )

    stageviz.command.set_default_prim("/World")
    _assert(
        _wait_until(lambda: _default_prim_path() == "/World"),
        "default prim prepared",
    )

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

    _assert(
        _wait_until(lambda: _default_prim_path() == "/World"),
        "default prim prepared",
    )

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

    _assert(
        _wait_until(lambda: _default_prim_path() == "/World"),
        "set_default_prim rejects non-root prim",
    )

    stageviz.command.set_default_prim("/OtherRoot")

    _assert(
        _wait_until(lambda: _default_prim_path() == "/OtherRoot"),
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

    _assert(
        _wait_until(lambda: stageviz.session().stageUp() == stageviz.StageUpZ),
        "set_stage_up sets Z again",
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

    stageviz.command.select_paths(["/World/PayloadA/Geom"])
    _assert(
        _wait_until(lambda: _selection() == ["/World/PayloadA/Geom"]),
        "payload child selection prepared",
    )

    stageviz.command.isolate_paths(["/World/PayloadA/Geom"])
    _assert(
        _wait_until(lambda: _mask() == ["/World/PayloadA/Geom"]),
        "payload child mask prepared",
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

    _assert(
        _wait_until(lambda: _selection() == ["/World/PayloadA"]),
        "payload selection prepared for select_invert_payload",
    )

    stageviz.command.select_invert_payload()

    _assert(
        _wait_until(lambda: _selection() == ["/World/PayloadB"]),
        "select_invert_payload selects loaded payloads not already selected",
    )

    if _undo():
        _assert_equal(
            _selection(),
            ["/World/PayloadA"],
            "undo select_invert_payload restores previous payload selection",
        )


def test_root_prim_order_create_move_and_undo():
    before = _child_names("/")

    stageviz.command.new_xform("/", "RootGroup")

    _assert(
        _wait_until(lambda: _exists("/RootGroup")),
        "root-level xform is created",
    )

    _assert(
        _wait_until(lambda: _child_names("/") == before + ["RootGroup"]),
        "root-level create appends root prim order",
    )

    stageviz.command.move_path(["/RootGroup"], "/", insert_index=0)

    _assert(
        _wait_until(
            lambda: bool(_child_names("/"))
            and _child_names("/")[0] == "RootGroup"
        ),
        "root-level move authors requested root prim order",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _child_names("/") == before + ["RootGroup"]),
            "undo root-level move restores root prim order",
        )


def test_root_layer_policy_rejects_sublayer_prim():
    _assert(_exists("/ExternalRoot"), "sublayer prim is composed")
    before_roots = _child_names("/")

    stageviz.command.rename_path("/ExternalRoot", "ExternalRenamed")
    _wait()

    _assert(_exists("/ExternalRoot"), "rename rejects prim authored only in sublayer")
    _assert(not _exists("/ExternalRenamed"), "rejected sublayer rename authors no root override")
    _assert_equal(_child_names("/"), before_roots, "rejected sublayer rename preserves root order")

    stageviz.command.delete_paths(["/ExternalRoot"])
    _wait()
    _assert(_exists("/ExternalRoot"), "delete rejects prim authored only in sublayer")


def test_composition_boundary_rejects_namespace_edit():
    _assert(_exists("/World/Referenced/Child"), "referenced child is composed")

    stageviz.command.move_path(["/World/Referenced/Child"], "/World/B", insert_index=-1)
    _wait()

    _assert(
        _exists("/World/Referenced/Child"),
        "move rejects prim below reference composition arc",
    )
    _assert(
        not _exists("/World/B/Child"),
        "composition-boundary rejection does not author relocates or destination prim",
    )


def test_multi_move_is_transactional_on_collision():
    stage = _stage()
    _define_xform(stage, "/World/B/A2")
    stage.GetRootLayer().Save()

    before_a = _child_names("/World/A")
    before_b = _child_names("/World/B")

    stageviz.command.move_path(["/World/A/A1", "/World/A/A2"], "/World/B", insert_index=0)
    _wait()

    _assert(_exists("/World/A/A1"), "failed batch move keeps first source")
    _assert(_exists("/World/A/A2"), "failed batch move keeps colliding source")
    _assert(not _exists("/World/B/A1"), "failed batch move does not partially move first prim")
    _assert_equal(_child_names("/World/A"), before_a, "failed batch move preserves source order")
    _assert_equal(_child_names("/World/B"), before_b, "failed batch move preserves destination order")


def test_composed_visibility_override_and_undo():
    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(lambda: _exists("/World/PayloadA/Geom")),
        "payload content loaded for visibility override",
    )

    property_path = "/World/PayloadA/Geom.visibility"

    _assert(
        not _root_has_property(property_path),
        "composed visibility has no root-layer opinion initially",
    )

    stageviz.command.hide_paths(["/World/PayloadA/Geom"], recursive=False)

    _assert(
        _wait_until(
            lambda: str(_visibility("/World/PayloadA/Geom")) == "invisible"
        ),
        "hide composed prim authors invisible",
    )

    _assert(
        _root_has_property(property_path),
        "hide composed prim creates root-layer visibility override",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: str(_visibility("/World/PayloadA/Geom")) != "invisible"
            ),
            "undo restores composed visibility",
        )

        _assert(
            not _root_has_property(property_path),
            "undo removes newly-created root-layer visibility override",
        )



def test_reset_overrides():
    if not _has_command("reset_overrides"):
        print("[skip] reset_overrides is not bound")
        return

    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(lambda: _exists("/World/PayloadA/Geom")),
        "payload content loaded for reset overrides",
    )

    path = "/World/PayloadA/Geom"
    visibility_path = f"{path}.visibility"
    translate_path = f"{path}.xformOp:translate"

    prim = _prim(path)
    _assert(bool(prim), "composed prim prepared for reset overrides")

    # Author several direct root-layer overrides on the composed prim.
    UsdGeom.Imageable(prim).GetVisibilityAttr().Set(UsdGeom.Tokens.invisible)

    xform = UsdGeom.Xformable(prim)
    xform.AddTranslateOp().Set(Gf.Vec3d(3.0, 4.0, 5.0))

    prim.SetMetadata("documentation", "Stageviz reset override test")

    _assert(
        _root_has_property(visibility_path),
        "visibility override authored in root layer",
    )
    _assert(
        _root_has_property(translate_path),
        "transform override authored in root layer",
    )
    _assert(
        _root_has_prim(path),
        "root-layer override prim spec exists",
    )

    root_spec = _root_layer().GetPrimAtPath(Sdf.Path(path))
    _assert(
        bool(root_spec and root_spec.HasInfo("documentation")),
        "metadata override authored in root layer",
    )

    # Add a descendant override. Resetting the parent must not remove it.
    child_path = f"{path}/ChildOverride"
    child = UsdGeom.Xform.Define(_stage(), child_path).GetPrim()
    child.SetMetadata("documentation", "Preserve descendant override")

    _assert(
        _root_has_prim(child_path),
        "descendant root-layer opinion prepared",
    )

    stageviz.command.reset_overrides([path])

    _assert(
        _wait_until(
            lambda: not _root_has_property(visibility_path)
            and not _root_has_property(translate_path)
        ),
        "reset_overrides removes direct root-layer properties",
    )

    root_spec = _root_layer().GetPrimAtPath(Sdf.Path(path))
    _assert(
        not root_spec or not root_spec.HasInfo("documentation"),
        "reset_overrides removes direct root-layer metadata",
    )

    _assert(
        _exists(path),
        "reset_overrides preserves composed prim",
    )

    _assert(
        _root_has_prim(child_path),
        "reset_overrides preserves descendant root-layer opinions",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _root_has_property(visibility_path)
                and _root_has_property(translate_path)
            ),
            "undo reset_overrides restores direct root-layer properties",
        )

        root_spec = _root_layer().GetPrimAtPath(Sdf.Path(path))
        _assert(
            bool(root_spec and root_spec.HasInfo("documentation")),
            "undo reset_overrides restores direct root-layer metadata",
        )

        _assert_equal(
            root_spec.GetInfo("documentation") if root_spec else None,
            "Stageviz reset override test",
            "undo reset_overrides restores metadata value",
        )

        _assert(
            _root_has_prim(child_path),
            "undo reset_overrides keeps descendant override intact",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: not _root_has_property(visibility_path)
                and not _root_has_property(translate_path)
            ),
            "redo reset_overrides removes direct root-layer properties again",
        )

        _assert(
            _exists(path),
            "redo reset_overrides still preserves composed prim",
        )


def test_reset_overrides_ignores_root_owned_prim():
    if not _has_command("reset_overrides"):
        print("[skip] reset_overrides is not bound")
        return

    path = "/World/A"
    prim = _prim(path)

    _assert(bool(prim), "root-owned prim prepared for reset override policy")

    prim.SetMetadata("documentation", "Root owned")
    UsdGeom.Imageable(prim).CreatePurposeAttr().Set(UsdGeom.Tokens.render)

    _assert(
        _root_has_prim(path),
        "root-owned prim has root-layer spec",
    )

    stageviz.command.reset_overrides([path])
    _wait()

    _assert(
        _exists(path),
        "reset_overrides never deletes root-owned prim",
    )

    root_spec = _root_layer().GetPrimAtPath(Sdf.Path(path))
    _assert(
        bool(root_spec and root_spec.HasInfo("documentation")),
        "reset_overrides leaves root-owned metadata unchanged",
    )

    _assert(
        _root_has_property(f"{path}.purpose"),
        "reset_overrides leaves root-owned properties unchanged",
    )


def test_reset_attribute_override():
    if not _has_command("reset_attribute_override"):
        print("[skip] reset_attribute_override is not bound")
        return

    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(lambda: _exists("/World/PayloadA/Geom")),
        "payload content loaded for attribute override reset",
    )

    path = "/World/PayloadA/Geom"
    visibility_path = f"{path}.visibility"

    prim = _prim(path)
    _assert(bool(prim), "composed prim prepared for attribute override reset")

    # Author one root-layer visibility override.
    UsdGeom.Imageable(prim).GetVisibilityAttr().Set(UsdGeom.Tokens.invisible)

    _assert(
        _root_has_property(visibility_path),
        "attribute override is authored in root layer",
    )

    _assert_equal(
        str(_visibility(path)),
        "invisible",
        "composed attribute reflects root-layer override",
    )

    stageviz.command.reset_attribute_override(visibility_path)

    _assert(
        _wait_until(lambda: not _root_has_property(visibility_path)),
        "reset_attribute_override removes only the root-layer attribute spec",
    )

    _assert(
        _exists(path),
        "reset_attribute_override preserves composed prim",
    )

    _assert(
        str(_visibility(path)) != "invisible",
        "reset_attribute_override exposes weaker composed attribute value",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _root_has_property(visibility_path)),
            "undo reset_attribute_override restores root-layer attribute spec",
        )

        _assert_equal(
            str(_visibility(path)),
            "invisible",
            "undo reset_attribute_override restores overridden value",
        )

    if _redo():
        _assert(
            _wait_until(lambda: not _root_has_property(visibility_path)),
            "redo reset_attribute_override removes attribute override again",
        )


def test_reset_attribute_override_ignores_root_owned_prim():
    if not _has_command("reset_attribute_override"):
        print("[skip] reset_attribute_override is not bound")
        return

    path = "/World/A"
    attribute_path = f"{path}.purpose"

    prim = _prim(path)
    _assert(bool(prim), "root-owned prim prepared for attribute override policy")

    UsdGeom.Imageable(prim).CreatePurposeAttr().Set(UsdGeom.Tokens.render)

    _assert(
        _root_has_property(attribute_path),
        "root-owned attribute is authored in root layer",
    )

    stageviz.command.reset_attribute_override(attribute_path)
    _wait()

    _assert(
        _root_has_property(attribute_path),
        "reset_attribute_override leaves root-owned attribute unchanged",
    )

    _assert(
        _exists(path),
        "reset_attribute_override never removes root-owned prim",
    )

def test_move_non_xformable_prim():
    stage = _stage()

    UsdGeom.Scope.Define(stage, "/World/Looks")
    UsdGeom.Scope.Define(stage, "/World/LooksDestination")
    UsdShade.Material.Define(stage, "/World/Looks/TestMaterial")

    _assert(
        _exists("/World/Looks/TestMaterial"),
        "material prepared for namespace move",
    )

    stageviz.command.move_path(
        ["/World/Looks/TestMaterial"],
        "/World/LooksDestination",
        insert_index=-1,
        preserve_world_transform=True,
    )

    _assert(
        _wait_until(
            lambda: not _exists("/World/Looks/TestMaterial")
            and _exists("/World/LooksDestination/TestMaterial")
        ),
        "non-xformable material moves with transform preservation enabled",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/Looks/TestMaterial")
                and not _exists("/World/LooksDestination/TestMaterial")
            ),
            "undo restores moved non-xformable material",
        )


def test_move_preserve_complex_world_transform():
    _assert(
        _set_complex_transform(
            "/World/A",
            (13.0, -7.0, 4.0),
            (27.0, -38.0, 16.0),
            (1.7, 0.65, 1.2),
        ),
        "set complex source parent transform",
    )

    _assert(
        _set_complex_transform(
            "/World/A/A1",
            (4.0, 9.0, -3.0),
            (-21.0, 44.0, 33.0),
            (0.8, 1.35, 0.7),
        ),
        "set complex child transform",
    )

    _assert(
        _set_complex_transform(
            "/World/B",
            (-11.0, 18.0, 6.0),
            (31.0, 19.0, -42.0),
            (0.75, 1.4, 1.1),
        ),
        "set complex destination parent transform",
    )

    before = _world_transform("/World/A/A1")

    _assert(
        before is not None,
        "capture complex child world transform before move",
    )

    stageviz.command.move_path(
        ["/World/A/A1"],
        "/World/B",
        insert_index=-1,
        preserve_world_transform=True,
    )

    _assert(
        _wait_until(
            lambda: _exists("/World/B/A1")
            and not _exists("/World/A/A1")
        ),
        "complex transformed prim moved",
    )

    after = _world_transform("/World/B/A1")

    _assert(
        _matrix_close(after, before, tolerance=1e-6),
        "complex reparent preserves full world transform",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/A/A1")
                and not _exists("/World/B/A1")
            ),
            "undo complex reparent restores original hierarchy",
        )

        restored = _world_transform("/World/A/A1")

        _assert(
            _matrix_close(restored, before, tolerance=1e-6),
            "undo complex reparent restores original world transform",
        )


def test_failed_load_leaves_valid_stage():
    _disable_preserve_state()

    missing_path = os.path.join(
        tempfile.gettempdir(),
        "stageviz_this_file_must_not_exist.usda",
    )

    try:
        os.remove(missing_path)
    except FileNotFoundError:
        pass

    loaded = stageviz.session().load(
        missing_path,
        stageviz.LoadNone,
    )

    _assert(
        not loaded,
        "loading missing stage reports failure",
    )

    _assert(
        stageviz.session().stage() is not None,
        "failed load leaves a valid stage object",
    )


def test_payload_descendant_rejects_namespace_edit():
    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(lambda: _exists("/World/PayloadA/Geom")),
        "payload descendant prepared",
    )

    stageviz.command.move_path(
        ["/World/PayloadA/Geom"],
        "/World/B",
        insert_index=-1,
    )

    _wait()

    _assert(
        _exists("/World/PayloadA/Geom"),
        "payload descendant remains at composed path",
    )

    _assert(
        not _exists("/World/B/Geom"),
        "payload descendant move creates no destination override",
    )

    stageviz.command.delete_paths(["/World/PayloadA/Geom"])

    _wait()

    _assert(
        _exists("/World/PayloadA/Geom"),
        "delete rejects payload-owned descendant",
    )


def test_merge_into_stage(merge_source):
    session = stageviz.session()

    _assert(hasattr(session, "merge"), "session.merge is exposed")
    _assert(hasattr(session, "mergeFromFile"), "session.mergeFromFile is exposed")

    before_sublayers = list(_root_layer().subLayerPaths)

    ok = session.merge(merge_source)

    _assert(ok, "merge destructively imports source content")
    _assert(
        _wait_until(lambda: _exists("/World/Merged")),
        "merge imports authored prim",
    )
    _assert_equal(
        list(_root_layer().subLayerPaths),
        before_sublayers,
        "merge does not add the source as a sublayer",
    )


def test_merge_flattened(merge_source):
    session = stageviz.session()

    _assert(
        hasattr(session, "mergeFlattened"),
        "session.mergeFlattened is exposed",
    )

    ok = session.mergeFlattened(merge_source)

    _assert(ok, "mergeFlattened imports composed source content")
    _assert(
        _wait_until(lambda: _exists("/World/MergedReference/Child")),
        "mergeFlattened includes composed referenced content",
    )

    prim = _prim("/World/MergedReference")
    references = prim.GetMetadata("references") if prim else None

    has_reference = bool(
        references
        and references.GetAddedOrExplicitItems()
    )

    _assert(
        not has_reference,
        "mergeFlattened removes the incoming reference arc",
    )


def test_merge_sublayer(merge_source):
    session = stageviz.session()

    _assert(
        hasattr(session, "mergeSublayer"),
        "session.mergeSublayer is exposed",
    )

    before = list(_root_layer().subLayerPaths)

    ok = session.mergeSublayer(merge_source)

    _assert(ok, "mergeSublayer succeeds")
    _assert(
        _wait_until(lambda: _exists("/World/Merged")),
        "sublayer content composes into stage",
    )

    after = list(_root_layer().subLayerPaths)

    _assert_equal(
        len(after),
        len(before) + 1,
        "mergeSublayer adds exactly one dependency",
    )

    ok_again = session.mergeSublayer(merge_source)

    _assert(
        ok_again,
        "merging the same sublayer again is a safe no-op",
    )
    _assert_equal(
        len(_root_layer().subLayerPaths),
        len(after),
        "duplicate sublayer is not merged",
    )


def test_merge_reference(merge_source):
    session = stageviz.session()
    stage = _stage()

    _assert(
        hasattr(session, "mergeReference"),
        "session.mergeReference is exposed",
    )

    _define_xform(stage, "/World/ReferenceTarget")

    ok = session.mergeReference(
        merge_source,
        "/World/ReferenceTarget",
    )

    _assert(ok, "mergeReference succeeds")
    _assert(
        _wait_until(
            lambda: _exists("/World/ReferenceTarget/Merged")
        ),
        "reference composes source default prim below target",
    )


def test_merge_payload(merge_source):
    session = stageviz.session()
    stage = _stage()

    _assert(
        hasattr(session, "mergePayload"),
        "session.mergePayload is exposed",
    )

    _define_xform(stage, "/World/PayloadTarget")

    ok = session.mergePayload(
        merge_source,
        "/World/PayloadTarget",
    )

    _assert(ok, "mergePayload succeeds")

    target = _prim("/World/PayloadTarget")

    _assert(
        bool(target and target.HasPayload()),
        "payload arc is authored on target",
    )


def test_save_reload(saved_path):
    ok = stageviz.session().save(saved_path)
    _assert(ok, "session.save writes test stage")

    reopened = Usd.Stage.Open(saved_path)
    _assert(bool(reopened), "saved stage reopens with USD")
    _assert(bool(reopened.GetPrimAtPath("/World")), "saved stage contains /World")


def run():
    root, main_path, merge_source, saved_path, external = create_fixture()
    print(f"Stageviz command test root: {root}")

    _disable_preserve_state()

    tests = [
        ("select paths", test_select_paths, ()),
        ("select all and invert", test_select_all_and_invert, ()),
        ("isolate paths", test_isolate_paths, ()),
        ("show/hide paths", test_show_hide_paths, ()),
        ("new prim", test_new_prim, ()),
        ("new typed prim", test_new_typed_prim, ()),
        ("new prim unique name", test_new_prim_unique_name, ()),
        ("new scope", test_new_scope, ()),
        ("new material", test_new_material, ()),
        ("new reference", test_new_reference, (external,)),
        ("new reference default prim", test_new_reference_default_prim, (external,)),
        ("new payload", test_new_payload, (external,)),
        ("new payload default prim", test_new_payload_default_prim, (external,)),
        ("duplicate paths", test_duplicate_paths, ()),
        ("duplicate composed payload prim", test_duplicate_composed_payload_prim, ()),
        ("new/rename/move/delete", test_new_rename_move_delete, ()),
        ("new_xform root parent", test_new_xform_root_parent, ()),
        ("new_xform unique name", test_new_xform_unique_name, ()),
        ("new_xform wraps multiple selection", test_new_xform_wraps_multiple_selection, ()),
        ("new_xform minimal root selection", test_new_xform_minimal_root_selection, ()),
        ("rename remaps child selection and mask", test_rename_remaps_child_selection_and_mask, ()),
        ("rename noop is safe", test_rename_noop_is_safe, ()),
        ("move selection/mask and insert order", test_move_selection_mask_and_insert_order, ()),
        ("move preserve world transform", test_move_preserve_world_transform, ()),
        ("move multiple siblings", test_move_multiple_siblings, ()),
        ("move rejects self parenting", test_move_rejects_self_parenting, ()),
        ("move rejects destination collision/no-op", test_move_rejects_destination_collision, ()),
        ("delete selection/mask and default prim", test_delete_removes_selection_mask_and_restores_default_prim, ()),
        ("delete default prim and undo", test_delete_default_prim_and_undo, ()),
        ("default prim validation", test_default_prim_validation, ()),
        ("stage up", test_stage_up, ()),
        ("payload load/unload", test_payload_load_unload, ()),
        ("select invert payload", test_select_invert_payload, ()),
        ("root prim order", test_root_prim_order_create_move_and_undo, ()),
        ("root layer policy", test_root_layer_policy_rejects_sublayer_prim, ()),
        ("composition boundary", test_composition_boundary_rejects_namespace_edit, ()),
        ("transactional multi move", test_multi_move_is_transactional_on_collision, ()),
        ("composed visibility override", test_composed_visibility_override_and_undo, ()),
        ("reset overrides", test_reset_overrides, ()),
        ("reset overrides root-owned policy", test_reset_overrides_ignores_root_owned_prim, ()),
        ("reset attribute override", test_reset_attribute_override, ()),
        ("reset attribute override root-owned policy", test_reset_attribute_override_ignores_root_owned_prim, ()),
        ("move non-xformable prim", test_move_non_xformable_prim, ()),
        ("move preserve complex transform", test_move_preserve_complex_world_transform, ()),
        ("failed load recovery", test_failed_load_leaves_valid_stage, ()),
        ("payload descendant namespace policy", test_payload_descendant_rejects_namespace_edit, ()),
        ("merge into stage", test_merge_into_stage, (merge_source,)),
        ("merge flattened", test_merge_flattened, (merge_source,)),
        ("merge sublayer", test_merge_sublayer, (merge_source,)),
        ("merge reference", test_merge_reference, (merge_source,)),
        ("merge payload", test_merge_payload, (merge_source,)),
        ("save/reopen", test_save_reload, (saved_path,)),
    ]

    for label, func, args in tests:
        reload_fixture(main_path)
        _run(label, func, args)

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
