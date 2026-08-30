# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""
Stageviz smoke/regression tests.

Run from the Stageviz Python editor.
"""

import json
import os
import platform
import statistics
import sys
import tempfile
import time
import traceback

from pxr import Gf, Kind, Sdf, Usd, UsdGeom, UsdShade
import stageviz


_FAILURES = []
_PERF_RESULTS = {}
_PERF_FIXTURE_RELOADS = []
_PERF_FAILURES = []
_PERF_WARNINGS = []

_PERF_ENABLED = True
_PERF_BASELINE_RESET = False
_PERF_BASELINE_VERSION = 1
_PERF_BASELINE_PATH = os.path.join(
    os.path.expanduser("~"),
    ".stageviz",
    "testapi_performance.json",
)

_PERF_WARN_PERCENT = 35.0
_PERF_FAIL_PERCENT = 100.0
_PERF_WARN_SECONDS = 0.050
_PERF_FAIL_SECONDS = 0.200


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
    start = time.perf_counter()

    try:
        func(*args)
    except Exception as exc:
        _fail(f"{label}: exception: {exc}")
        traceback.print_exc()
    finally:
        elapsed = time.perf_counter() - start
        _PERF_RESULTS[label] = elapsed
        print(f"[TIME] {label}: {elapsed:.4f}s")


def _performance_environment():
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python": sys.version.split()[0],
    }


def _load_performance_baseline():
    if not _PERF_ENABLED or not os.path.exists(_PERF_BASELINE_PATH):
        return None

    try:
        with open(_PERF_BASELINE_PATH, "r", encoding="utf-8") as file:
            baseline = json.load(file)
    except Exception as exc:
        print(f"[PERF WARN] could not read baseline: {exc}")
        return None

    if baseline.get("version") != _PERF_BASELINE_VERSION:
        print(
            "[PERF WARN] baseline version mismatch: "
            f"expected {_PERF_BASELINE_VERSION}, got {baseline.get('version')!r}"
        )
        return None

    return baseline


def _save_performance_baseline(fixture_create, fixture_reload_median, suite_total):
    directory = os.path.dirname(_PERF_BASELINE_PATH)
    os.makedirs(directory, exist_ok=True)

    data = {
        "version": _PERF_BASELINE_VERSION,
        "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "environment": _performance_environment(),
        "thresholds": {
            "warn_percent": _PERF_WARN_PERCENT,
            "fail_percent": _PERF_FAIL_PERCENT,
            "warn_seconds": _PERF_WARN_SECONDS,
            "fail_seconds": _PERF_FAIL_SECONDS,
        },
        "tests": dict(_PERF_RESULTS),
        "fixture": {
            "create": fixture_create,
            "reload_median": fixture_reload_median,
        },
        "suite": {
            "total": suite_total,
        },
    }

    temp_path = _PERF_BASELINE_PATH + ".tmp"

    with open(temp_path, "w", encoding="utf-8") as file:
        json.dump(data, file, indent=2, sort_keys=True)
        file.write("\n")

    os.replace(temp_path, _PERF_BASELINE_PATH)


def _performance_level(current, baseline):
    if baseline is None or baseline < 0.0:
        return "ok", current, None

    delta = current - baseline
    percent = None

    if baseline > 1e-9:
        percent = (delta / baseline) * 100.0

    fail_delta = max(
        _PERF_FAIL_SECONDS,
        baseline * (_PERF_FAIL_PERCENT / 100.0),
    )
    warn_delta = max(
        _PERF_WARN_SECONDS,
        baseline * (_PERF_WARN_PERCENT / 100.0),
    )

    if delta >= fail_delta:
        return "fail", delta, percent

    if delta >= warn_delta:
        return "warn", delta, percent

    return "ok", delta, percent


def _format_performance_delta(delta, percent):
    if percent is None:
        return f"{delta:+.4f}s"
    return f"{percent:+.1f}% ({delta:+.4f}s)"


def _compare_performance_metric(label, current, baseline):
    level, delta, percent = _performance_level(current, baseline)

    if level == "fail":
        message = (
            f"{label}: baseline {baseline:.4f}s, current {current:.4f}s, "
            f"delta {_format_performance_delta(delta, percent)}"
        )
        _PERF_FAILURES.append(message)
        return

    if level == "warn":
        message = (
            f"{label}: baseline {baseline:.4f}s, current {current:.4f}s, "
            f"delta {_format_performance_delta(delta, percent)}"
        )
        _PERF_WARNINGS.append(message)


def _compare_performance(baseline, fixture_create, fixture_reload_median, suite_total):
    baseline_tests = baseline.get("tests", {})

    for label, current in _PERF_RESULTS.items():
        previous = baseline_tests.get(label)
        if previous is None:
            _PERF_WARNINGS.append(
                f"{label}: new test has no performance baseline"
            )
            continue

        _compare_performance_metric(label, current, float(previous))

    missing_tests = sorted(set(baseline_tests) - set(_PERF_RESULTS))
    for label in missing_tests:
        _PERF_WARNINGS.append(
            f"{label}: baseline test was not run in the current suite"
        )

    baseline_fixture = baseline.get("fixture", {})
    if "create" in baseline_fixture:
        _compare_performance_metric(
            "fixture create",
            fixture_create,
            float(baseline_fixture["create"]),
        )

    if "reload_median" in baseline_fixture:
        _compare_performance_metric(
            "fixture reload median",
            fixture_reload_median,
            float(baseline_fixture["reload_median"]),
        )

    baseline_suite = baseline.get("suite", {})
    if "total" in baseline_suite:
        _compare_performance_metric(
            "suite total",
            suite_total,
            float(baseline_suite["total"]),
        )


def _print_performance_summary(
    baseline,
    fixture_create,
    fixture_reload_median,
    suite_total,
    functional_passed,
):
    print("\n=== Stageviz performance summary ===")
    print(f"Baseline: {_PERF_BASELINE_PATH}")
    print(f"Fixture create: {fixture_create:.4f}s")
    print(f"Fixture reload median: {fixture_reload_median:.4f}s")
    print(f"Suite total: {suite_total:.4f}s")

    if not _PERF_ENABLED:
        print("Performance regression checks are disabled.")
        return True

    if _PERF_BASELINE_RESET:
        if not functional_passed:
            print("[PERF WARN] baseline was not reset because functional tests failed.")
            return True

        try:
            _save_performance_baseline(
                fixture_create,
                fixture_reload_median,
                suite_total,
            )
            print("Performance baseline replaced.")
        except Exception as exc:
            _PERF_FAILURES.append(f"could not replace performance baseline: {exc}")
            print(f"[PERF FAIL] {_PERF_FAILURES[-1]}")
            return False

        return True

    if baseline is None:
        if not functional_passed:
            print("[PERF WARN] baseline was not created because functional tests failed.")
            return True

        try:
            _save_performance_baseline(
                fixture_create,
                fixture_reload_median,
                suite_total,
            )
            print("Performance baseline created.")
        except Exception as exc:
            _PERF_FAILURES.append(f"could not create performance baseline: {exc}")
            print(f"[PERF FAIL] {_PERF_FAILURES[-1]}")
            return False

        return True

    baseline_environment = baseline.get("environment", {})
    current_environment = _performance_environment()

    if baseline_environment and baseline_environment != current_environment:
        print("[PERF WARN] baseline environment differs from current environment.")
        print(f"  baseline: {baseline_environment}")
        print(f"  current:  {current_environment}")

    _compare_performance(
        baseline,
        fixture_create,
        fixture_reload_median,
        suite_total,
    )

    if _PERF_FAILURES:
        print(f"[PERF FAIL] {len(_PERF_FAILURES)} performance regression(s)")
        for message in _PERF_FAILURES:
            print(f" - {message}")

    if _PERF_WARNINGS:
        print(f"[PERF WARN] {len(_PERF_WARNINGS)} performance warning(s)")
        for message in _PERF_WARNINGS:
            print(f" - {message}")

    if not _PERF_FAILURES and not _PERF_WARNINGS:
        print(f"[ OK ] {len(_PERF_RESULTS)} tests within performance tolerance")

    return not _PERF_FAILURES


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


def _edit_layer_identifier():
    session = stageviz.session()
    if not hasattr(session, "editLayer"):
        return ""
    return session.editLayer()


def _edit_layer():
    stage = _stage()
    identifier = _edit_layer_identifier()

    if not stage or not identifier:
        return None

    for layer in stage.GetLayerStack(False):
        if layer.identifier == identifier or layer.realPath == identifier:
            return layer

    return None


def _edit_has_property(path):
    layer = _edit_layer()
    if not layer:
        return False
    return bool(layer.GetPropertyAtPath(Sdf.Path(path)))


def _edit_has_prim(path):
    layer = _edit_layer()
    if not layer:
        return False
    return bool(layer.GetPrimAtPath(Sdf.Path(path)))


def _layer_has_default(layer, path):
    if not layer:
        return False

    prop = layer.GetPropertyAtPath(Sdf.Path(path))
    return bool(prop and prop.HasInfo("default"))


def _ensure_cube(path, size=2.0):
    stage = _stage()
    if not stage:
        return None

    cube = UsdGeom.Cube.Define(stage, path)
    cube.CreateSizeAttr(float(size))
    return cube.GetPrim()


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


def _transform_property_names(path):
    layer = _edit_layer()
    if not layer:
        return []

    spec = layer.GetPrimAtPath(Sdf.Path(path))
    if not spec:
        return []

    names = []
    for prop in spec.properties:
        name = prop.name
        if name == "xformOpOrder" or name.startswith("xformOp:"):
            names.append(name)

    return sorted(names)


def _local_transform(path):
    prim = _prim(path)
    if not prim:
        return None

    xformable = UsdGeom.Xformable(prim)
    if not xformable:
        return None

    value = xformable.GetLocalTransformation(Usd.TimeCode.Default())
    if isinstance(value, tuple):
        value = value[0]

    return Gf.Matrix4d(value)


def _identity_matrix():
    return Gf.Matrix4d(1.0)


def _xform_order(path):
    prim = _prim(path)
    if not prim:
        return []

    attr = UsdGeom.Xformable(prim).GetXformOpOrderAttr()
    value = attr.Get() if attr else None
    return [str(token) for token in value] if value is not None else []


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

    root = _define_xform(stage, f"/{name}")
    Usd.ModelAPI(root).SetKind(Kind.Tokens.component)

    geom = UsdGeom.Cube.Define(stage, f"/{name}/Geom")
    geom.CreateSizeAttr(2.0)

    stage.SetDefaultPrim(root)
    stage.GetRootLayer().Save()


def _create_external_file(path):
    stage = Usd.Stage.CreateNew(path)
    root = _define_xform(stage, "/ExternalRoot")
    _define_xform(stage, "/ExternalRoot/Child")
    stage.SetDefaultPrim(root)
    stage.GetRootLayer().Save()


def _create_merge_source(path, external):
    stage = Usd.Stage.CreateNew(path)

    root = _define_xform(stage, "/MergeSource")
    _define_xform(stage, "/MergeSource/Merged")
    stage.SetDefaultPrim(root)

    referenced = _define_xform(stage, "/MergeSource/MergedReference")
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

    # Properly applied UsdGeomModelAPI source.
    p1 = _define_xform(stage, "/World/PayloadA")
    p1.GetPayloads().AddPayload(os.path.basename(payload_a), "/PayloadA")
    Usd.ModelAPI(p1).SetKind(Kind.Tokens.component)
    UsdGeom.ModelAPI.Apply(p1).SetExtentsHint([
        Gf.Vec3f(-5.0, -5.0, -5.0),
        Gf.Vec3f(5.0, 5.0, 5.0),
    ])

    # Authored extentsHint without applying UsdGeomModelAPI.
    p2 = _define_xform(stage, "/World/PayloadB")
    p2.GetPayloads().AddPayload(os.path.basename(payload_b), "/PayloadB")
    Usd.ModelAPI(p2).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(p2).AddTranslateOp().Set(Gf.Vec3d(6.0, 0.0, 0.0))
    p2.CreateAttribute("extentsHint", Sdf.ValueTypeNames.Float3Array).Set([
        Gf.Vec3f(-2.0, -2.0, -2.0),
        Gf.Vec3f(2.0, 2.0, 2.0),
    ])

    payload_selected_near = _define_xform(stage, "/World/PayloadSelectedNear")
    payload_selected_near.GetPayloads().AddPayload(
        os.path.basename(payload_b),
        "/PayloadB",
    )
    Usd.ModelAPI(payload_selected_near).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(payload_selected_near).AddTranslateOp().Set(
        Gf.Vec3d(1.0, 0.0, 0.0)
    )
    payload_selected_near.CreateAttribute(
        "extentsHint",
        Sdf.ValueTypeNames.Float3Array,
    ).Set([
        Gf.Vec3f(-0.25, -0.25, -0.25),
        Gf.Vec3f(0.25, 0.25, 0.25),
    ])

    payload_far = _define_xform(stage, "/World/PayloadFar")
    payload_far.GetPayloads().AddPayload(os.path.basename(payload_b), "/PayloadB")
    Usd.ModelAPI(payload_far).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(payload_far).AddTranslateOp().Set(Gf.Vec3d(100.0, 0.0, 0.0))
    payload_far.CreateAttribute("extentsHint", Sdf.ValueTypeNames.Float3Array).Set([
        Gf.Vec3f(-2.0, -2.0, -2.0),
        Gf.Vec3f(2.0, 2.0, 2.0),
    ])

    payload_wide = _define_xform(stage, "/World/PayloadWide")
    payload_wide.GetPayloads().AddPayload(os.path.basename(payload_b), "/PayloadB")
    Usd.ModelAPI(payload_wide).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(payload_wide).AddTranslateOp().Set(Gf.Vec3d(80.0, 0.0, 0.0))
    payload_wide.CreateAttribute("extentsHint", Sdf.ValueTypeNames.Float3Array).Set([
        Gf.Vec3f(-100.0, -2.0, -2.0),
        Gf.Vec3f(100.0, 2.0, 2.0),
    ])

    # Reverse coverage: raw extentsHint source with an applied ModelAPI neighbor.
    fallback_source = _define_xform(stage, "/World/PayloadFallbackSource")
    fallback_source.GetPayloads().AddPayload(os.path.basename(payload_a), "/PayloadA")
    Usd.ModelAPI(fallback_source).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(fallback_source).AddTranslateOp().Set(Gf.Vec3d(50.0, 0.0, 0.0))
    fallback_source.CreateAttribute("extentsHint", Sdf.ValueTypeNames.Float3Array).Set([
        Gf.Vec3f(-5.0, -5.0, -5.0),
        Gf.Vec3f(5.0, 5.0, 5.0),
    ])

    model_neighbor = _define_xform(stage, "/World/PayloadModelNeighbor")
    model_neighbor.GetPayloads().AddPayload(os.path.basename(payload_b), "/PayloadB")
    Usd.ModelAPI(model_neighbor).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(model_neighbor).AddTranslateOp().Set(Gf.Vec3d(56.0, 0.0, 0.0))
    UsdGeom.ModelAPI.Apply(model_neighbor).SetExtentsHint([
        Gf.Vec3f(-2.0, -2.0, -2.0),
        Gf.Vec3f(2.0, 2.0, 2.0),
    ])

    model_selected_near = _define_xform(stage, "/World/PayloadModelSelectedNear")
    model_selected_near.GetPayloads().AddPayload(
        os.path.basename(payload_b),
        "/PayloadB",
    )
    Usd.ModelAPI(model_selected_near).SetKind(Kind.Tokens.component)
    UsdGeom.Xformable(model_selected_near).AddTranslateOp().Set(
        Gf.Vec3d(51.0, 0.0, 0.0)
    )
    UsdGeom.ModelAPI.Apply(model_selected_near).SetExtentsHint([
        Gf.Vec3f(-0.25, -0.25, -0.25),
        Gf.Vec3f(0.25, 0.25, 0.25),
    ])

    nested_outer = _define_xform(stage, "/World/NestedOuter")
    nested_outer.GetPayloads().AddPayload(
        os.path.basename(payload_a),
        "/PayloadA",
    )
    Usd.ModelAPI(nested_outer).SetKind(Kind.Tokens.component)

    nested_inner = _define_xform(stage, "/World/NestedOuter/NestedInner")
    nested_inner.GetPayloads().AddPayload(
        os.path.basename(payload_b),
        "/PayloadB",
    )
    Usd.ModelAPI(nested_inner).SetKind(Kind.Tokens.component)

    variant_payload = _define_xform(stage, "/World/VariantPayload")
    Usd.ModelAPI(variant_payload).SetKind(Kind.Tokens.component)
    variant_set = variant_payload.GetVariantSets().AddVariantSet("model")
    variant_set.AddVariant("A")
    variant_set.AddVariant("B")
    variant_set.SetVariantSelection("A")
    with variant_set.GetVariantEditContext():
        variant_payload.GetPayloads().AddPayload(
            os.path.basename(payload_a),
            "/PayloadA",
        )
    variant_set.SetVariantSelection("B")
    with variant_set.GetVariantEditContext():
        variant_payload.GetPayloads().AddPayload(
            os.path.basename(payload_b),
            "/PayloadB",
        )
    variant_set.SetVariantSelection("A")

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


def test_command_api():
    expected = [
        "select_paths",
        "select_all",
        "select_invert",
        "select_payload",
        "select_invert_payload",
        "isolate_paths",
        "show_paths",
        "hide_paths",
        "load_payloads",
        "load_neighbor_payloads",
        "unload_payloads",
        "set_stage_up",
        "set_default_prim",
        "delete_paths",
        "duplicate_paths",
        "new_prim",
        "new_scope",
        "new_material",
        "new_reference",
        "new_payload",
        "rename_path",
        "new_xform",
        "move_path",
        "set_attribute_value",
        "set_attribute_values",
        "reset_attribute_values",
        "reset_attribute_override",
        "reset_attribute_overrides",
        "center_pivots",
        "reset_pivots",
        "reset_transforms",
        "identity_transforms",
        "bind_material",
    ]

    # reset_overrides was added after the original Python command surface.
    if _has_command("reset_overrides"):
        expected.append("reset_overrides")

    missing = [name for name in expected if not _has_command(name)]

    _assert(
        not missing,
        "expected command API is exposed"
        + (f": missing {missing}" if missing else ""),
    )


def test_session_edit_layer_api():
    session = stageviz.session()

    required = ("editLayer", "editLayers", "setEditLayer")
    missing = [name for name in required if not hasattr(session, name)]

    _assert(
        not missing,
        "session edit-layer API is exposed"
        + (f": missing {missing}" if missing else ""),
    )

    if missing:
        return

    root = _root_layer()
    _assert(bool(root), "root layer exists for edit-layer test")

    if not root:
        return

    _assert_equal(
        session.editLayer(),
        root.identifier,
        "edit layer defaults to root layer",
    )

    layers = list(session.editLayers())

    _assert(
        root.identifier in layers,
        "editLayers includes root layer",
    )

    sublayers = [
        identifier
        for identifier in layers
        if identifier != root.identifier
    ]

    _assert(
        bool(sublayers),
        "fixture exposes at least one local sublayer",
    )

    if not sublayers:
        return

    sublayer = sublayers[0]

    _assert(
        bool(session.setEditLayer(sublayer)),
        "setEditLayer accepts local sublayer",
    )

    _assert_equal(
        session.editLayer(),
        sublayer,
        "session switches active edit layer",
    )

    stage_target = _stage().GetEditTarget().GetLayer()
    _assert(
        bool(stage_target),
        "USD stage has active edit target",
    )

    if stage_target:
        _assert_equal(
            stage_target.identifier,
            sublayer,
            "USD stage edit target follows Session edit layer",
        )

    rejected_missing = False
    try:
        rejected_missing = not bool(
            session.setEditLayer("__stageviz_missing_layer__.usda")
        )
    except ValueError:
        rejected_missing = True

    _assert(
        rejected_missing,
        "setEditLayer rejects unknown layer",
    )

    _assert(
        bool(session.setEditLayer(root.identifier)),
        "setEditLayer switches back to root",
    )

    _assert_equal(
        session.editLayer(),
        root.identifier,
        "edit layer restored to root",
    )


def test_property_edit_uses_selected_sublayer():
    session = stageviz.session()

    if not all(hasattr(session, name) for name in ("editLayer", "editLayers", "setEditLayer")):
        print("[skip] session edit-layer API is not bound")
        return

    root = _root_layer()
    layers = list(session.editLayers())
    sublayers = [
        identifier
        for identifier in layers
        if root and identifier != root.identifier
    ]

    if not sublayers:
        print("[skip] no local sublayer available")
        return

    sublayer = sublayers[0]

    _assert(
        bool(session.setEditLayer(sublayer)),
        "select sublayer for property authoring",
    )

    property_path = "/ExternalRoot.visibility"

    _assert(
        not _edit_has_property(property_path),
        "selected edit layer has no visibility opinion initially",
    )

    _assert(
        not _root_has_property(property_path),
        "root layer has no visibility opinion initially",
    )

    stageviz.command.hide_paths(["/ExternalRoot"], recursive=False)

    _assert(
        _wait_until(lambda: str(_visibility("/ExternalRoot")) == "invisible"),
        "hide_paths edits composed sublayer prim",
    )

    _assert(
        _edit_has_property(property_path),
        "visibility is authored in selected edit layer",
    )

    _assert(
        not _root_has_property(property_path),
        "visibility is not accidentally authored in root layer",
    )

    session.setEditLayer(root.identifier)


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


def test_bind_material():
    if not _has_command("bind_material"):
        print("[skip] bind_material is not bound")
        return

    stage = _stage()

    if not _exists("/World/Looks"):
        UsdGeom.Scope.Define(stage, "/World/Looks")

    material_path = "/World/Looks/BoundMaterial"

    if not _exists(material_path):
        material = UsdShade.Material.Define(stage, material_path)
        shader = UsdShade.Shader.Define(stage, f"{material_path}/PreviewSurface")
        shader.CreateIdAttr("UsdPreviewSurface")
        material.CreateSurfaceOutput().ConnectToSource(
            shader.ConnectableAPI(),
            "surface",
        )

    stageviz.command.bind_material(
        ["/World/A", "/World/B"],
        material_path,
    )

    def bound_material(path):
        prim = _prim(path)
        if not prim:
            return ""
        bound, _ = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()
        return str(bound.GetPath()) if bound else ""

    _assert(
        _wait_until(
            lambda: bound_material("/World/A") == material_path
            and bound_material("/World/B") == material_path
        ),
        "bind_material binds material to all requested prims",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: bound_material("/World/A") != material_path
                and bound_material("/World/B") != material_path
            ),
            "undo bind_material restores previous bindings",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: bound_material("/World/A") == material_path
                and bound_material("/World/B") == material_path
            ),
            "redo bind_material restores bindings",
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


def test_move_preserve_world_transform_keeps_pivot_stack():
    if not _has_command("center_pivots"):
        print("[skip] center_pivots is not bound")
        return

    path = "/World/A/A1"
    _ensure_cube(f"{path}/PivotGeom", size=3.0)

    _assert(
        _set_complex_transform(
            "/World/A",
            (8.0, -2.0, 3.0),
            (12.0, 19.0, -7.0),
            (1.1, 0.9, 1.2),
        ),
        "set source parent transform for pivot-preserving move",
    )

    _assert(
        _set_complex_transform(
            path,
            (4.0, 1.0, -2.0),
            (21.0, -16.0, 9.0),
            (1.3, 0.8, 1.1),
        ),
        "set child transform for pivot-preserving move",
    )

    _assert(
        _set_complex_transform(
            "/World/B",
            (-5.0, 7.0, 2.0),
            (-8.0, 14.0, 23.0),
            (0.9, 1.2, 1.0),
        ),
        "set destination parent transform for pivot-preserving move",
    )

    stageviz.command.center_pivots([path])

    expected_order = [
        "xformOp:translate:pivot",
        "xformOp:transform",
        "!invert!xformOp:translate:pivot",
    ]

    _assert(
        _wait_until(lambda: _xform_order(path) == expected_order),
        "center_pivots creates canonical paired pivot stack before move",
    )

    pivot_before = _prim(path).GetAttribute("xformOp:translate:pivot").Get()
    world_before = _world_transform(path)

    stageviz.command.move_path(
        [path],
        "/World/B",
        insert_index=-1,
        preserve_world_transform=True,
    )

    moved_path = "/World/B/A1"

    _assert(
        _wait_until(lambda: _exists(moved_path) and not _exists(path)),
        "pivoted prim moved to destination parent",
    )

    _assert(
        _matrix_close(_world_transform(moved_path), world_before),
        "pivoted prim preserves world transform during move",
    )

    _assert_equal(
        _xform_order(moved_path),
        expected_order,
        "world-transform authoring preserves pivot/matrix/inverse-pivot order",
    )

    pivot_after = _prim(moved_path).GetAttribute("xformOp:translate:pivot").Get()

    _assert(
        pivot_after is not None and pivot_before is not None
        and all(abs(float(pivot_after[i]) - float(pivot_before[i])) < 1e-8 for i in range(3)),
        "world-transform authoring preserves pivot value",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _exists(path) and not _exists(moved_path)),
            "undo pivot-preserving move restores original path",
        )

        _assert_equal(
            _xform_order(path),
            expected_order,
            "undo pivot-preserving move restores canonical pivot order",
        )

        _assert(
            _matrix_close(_world_transform(path), world_before),
            "undo pivot-preserving move restores world transform",
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



def test_payload_variant_session_state(root):
    session = stageviz.session()
    required = ("saveState", "loadState")
    missing = [name for name in required if not hasattr(session, name)]
    _assert(
        not missing,
        "session state API is exposed"
        + (f": missing {missing}" if missing else ""),
    )
    if missing:
        return

    path = "/World/VariantPayload"
    prim = _prim(path)
    _assert(bool(prim and prim.HasPayload()), "variant payload is composed")
    if not prim:
        return

    variant_set = prim.GetVariantSet("model")
    _assert(bool(variant_set and variant_set.IsValid()), "variant payload has model variant set")
    if not variant_set or not variant_set.IsValid():
        return

    _assert_equal(
        variant_set.GetVariantSelection(),
        "A",
        "variant payload fixture starts on variant A",
    )

    variant_set.SetVariantSelection("B")
    stageviz.command.load_payloads([path])

    _assert(
        _wait_until(
            lambda: bool(
                _prim(path)
                and _prim(path).IsLoaded()
                and _prim(path).GetVariantSet("model").GetVariantSelection() == "B"
            ),
            timeout=5.0,
        ),
        "variant B payload is loaded before saving session state",
    )

    state_path = os.path.join(root, "variant_payload.session")
    _assert(bool(session.saveState(state_path)), "session state saves variant payload state")

    try:
        with open(state_path, "r", encoding="utf-8") as file:
            state = json.load(file)
    except Exception as exc:
        _fail(f"read saved variant payload session state: {exc}")
        return

    _assert(
        path in state.get("loadedPayloads", []),
        "saved session records variant payload as loaded",
    )
    _assert(
        {
            "path": path,
            "set": "model",
            "value": "B",
        }
        in state.get("payloadVariants", []),
        "saved session records payload variant selection",
    )

    stageviz.command.unload_payloads([path])
    variant_set = _prim(path).GetVariantSet("model")
    variant_set.SetVariantSelection("A")

    _assert(
        _wait_until(
            lambda: bool(
                _prim(path)
                and not _prim(path).IsLoaded()
                and _prim(path).GetVariantSet("model").GetVariantSelection() == "A"
            ),
            timeout=5.0,
        ),
        "variant payload state changed before session restore",
    )

    _assert(bool(session.loadState(state_path)), "session state restores variant payload state")

    _assert(
        _wait_until(
            lambda: bool(
                _prim(path)
                and _prim(path).IsLoaded()
                and _prim(path).GetVariantSet("model").GetVariantSelection() == "B"
            ),
            timeout=5.0,
        ),
        "session restore selects variant B before reloading payload",
    )


def _prim_world_bounds(path):
    prim = _prim(path)
    if not prim or not prim.IsA(UsdGeom.Imageable):
        return None

    cache = UsdGeom.BBoxCache(
        Usd.TimeCode.Default(),
        UsdGeom.Imageable.GetOrderedPurposeTokens(),
        useExtentsHint=True,
    )
    bounds = cache.ComputeWorldBound(prim).ComputeAlignedRange()
    return None if bounds.IsEmpty() else bounds


def _expanded_bounds(bounds, scale=1.5):
    if bounds is None or bounds.IsEmpty():
        return None

    center = bounds.GetMidpoint()
    half_size = bounds.GetSize() * 0.5 * scale
    return Gf.Range3d(center - half_size, center + half_size)


def _payload_world_bounds(path):
    prim = _prim(path)
    if not prim:
        return None

    attribute = prim.GetAttribute("extentsHint")
    extents = attribute.Get() if attribute else None
    if not extents or len(extents) < 2 or (len(extents) % 2) != 0:
        return None

    world = UsdGeom.XformCache(
        Usd.TimeCode.Default()
    ).GetLocalToWorldTransform(prim)

    bounds = Gf.Range3d()

    for index in range(0, len(extents), 2):
        minimum = Gf.Vec3d(extents[index])
        maximum = Gf.Vec3d(extents[index + 1])

        for x in range(2):
            for y in range(2):
                for z in range(2):
                    corner = Gf.Vec3d(
                        maximum[0] if x else minimum[0],
                        maximum[1] if y else minimum[1],
                        maximum[2] if z else minimum[2],
                    )
                    bounds.UnionWith(world.Transform(corner))

    return None if bounds.IsEmpty() else bounds


def _point_inside(bounds, point):
    if bounds is None or bounds.IsEmpty():
        return False

    minimum = bounds.GetMin()
    maximum = bounds.GetMax()

    return all(
        minimum[axis] <= point[axis] <= maximum[axis]
        for axis in range(3)
    )


def test_load_neighbor_payloads():
    if not _has_command("load_neighbor_payloads"):
        print("[skip] load_neighbor_payloads is not bound")
        return

    source_path = "/World/PayloadA"
    source_child_path = "/World/PayloadA/Geom"
    selected_near_path = "/World/PayloadSelectedNear"
    old_root_near_path = "/World/PayloadB"
    far_path = "/World/PayloadFar"
    wide_path = "/World/PayloadWide"

    source_prim = _prim(source_path)
    old_root_near_prim = _prim(old_root_near_path)

    _assert(
        bool(source_prim and UsdGeom.ModelAPI(source_prim)),
        "neighbor source payload has applied UsdGeomModelAPI",
    )
    _assert(
        bool(old_root_near_prim and not UsdGeom.ModelAPI(old_root_near_prim)),
        "neighbor candidate exercises raw extentsHint fallback",
    )

    payload_bounds = _payload_world_bounds(source_path)

    stageviz.command.load_payloads([source_path])

    _assert(
        _wait_until(
            lambda: bool(
                _prim(source_path)
                and _prim(source_path).IsLoaded()
                and _exists(source_child_path)
            ),
            timeout=5.0,
        ),
        "source payload prepared for selected-prim neighbor search",
    )

    selected_bounds = _prim_world_bounds(source_child_path)
    selected_near_bounds = _payload_world_bounds(selected_near_path)
    old_root_near_bounds = _payload_world_bounds(old_root_near_path)
    far_bounds = _payload_world_bounds(far_path)
    wide_bounds = _payload_world_bounds(wide_path)

    for bounds, label in (
        (selected_bounds, "selected child world bounds are available"),
        (payload_bounds, "owning payload extentsHint bounds are available"),
        (selected_near_bounds, "selected-near payload bounds are available"),
        (old_root_near_bounds, "old root-near payload bounds are available"),
        (far_bounds, "far payload bounds are available"),
        (wide_bounds, "wide payload bounds are available"),
    ):
        _assert(bounds is not None, label)

    if any(
        bounds is None
        for bounds in (
            selected_bounds,
            payload_bounds,
            selected_near_bounds,
            old_root_near_bounds,
            far_bounds,
            wide_bounds,
        )
    ):
        return

    selected_search = _expanded_bounds(selected_bounds)
    old_payload_search = _expanded_bounds(payload_bounds)

    _assert(
        _point_inside(selected_search, selected_near_bounds.GetMidpoint()),
        "selected-near payload center is inside 1.5x selected child bounds",
    )
    _assert(
        not _point_inside(selected_search, old_root_near_bounds.GetMidpoint()),
        "old root-near payload is outside selected child search bounds",
    )
    _assert(
        _point_inside(old_payload_search, old_root_near_bounds.GetMidpoint()),
        "old root-near payload would match the former payload-root bounds",
    )
    _assert(
        not _point_inside(selected_search, far_bounds.GetMidpoint()),
        "far payload center is outside selected child search bounds",
    )
    _assert(
        not _point_inside(selected_search, wide_bounds.GetMidpoint()),
        "wide payload center is outside selected child search bounds",
    )

    stageviz.command.select_paths([source_child_path])
    _assert(
        _wait_until(lambda: _selection() == [source_child_path]),
        "payload descendant selected for neighbor search",
    )

    stageviz.command.load_neighbor_payloads([source_child_path])

    _assert(
        _wait_until(
            lambda: bool(
                _prim(selected_near_path)
                and _prim(selected_near_path).IsLoaded()
            ),
            timeout=5.0,
        ),
        "neighbor search loads top-most payload inside selected child bounds",
    )
    _assert(
        bool(
            _prim(old_root_near_path)
            and not _prim(old_root_near_path).IsLoaded()
        ),
        "neighbor search does not use owning payload bounds as source bounds",
    )
    _assert(
        bool(_prim(far_path) and not _prim(far_path).IsLoaded()),
        "neighbor search rejects far top-most payload",
    )
    _assert(
        bool(_prim(wide_path) and not _prim(wide_path).IsLoaded()),
        "neighbor search tests candidate center rather than bbox intersection",
    )
    _assert(
        bool(_prim(source_path) and _prim(source_path).IsLoaded()),
        "selected prim owning payload stays the source and is not a neighbor candidate",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: bool(
                    _prim(selected_near_path)
                    and not _prim(selected_near_path).IsLoaded()
                ),
                timeout=5.0,
            ),
            "undo neighbor search restores selected-near payload load state",
        )
        _assert_equal(
            _selection(),
            [source_child_path],
            "undo neighbor search restores previous selection",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: bool(
                    _prim(selected_near_path)
                    and _prim(selected_near_path).IsLoaded()
                ),
                timeout=5.0,
            ),
            "redo neighbor search reloads selected-near top-most payload",
        )


def test_load_neighbor_payloads_raw_source():
    if not _has_command("load_neighbor_payloads"):
        print("[skip] load_neighbor_payloads is not bound")
        return

    source_path = "/World/PayloadFallbackSource"
    source_child_path = "/World/PayloadFallbackSource/Geom"
    selected_near_path = "/World/PayloadModelSelectedNear"
    old_root_near_path = "/World/PayloadModelNeighbor"

    source_prim = _prim(source_path)
    old_root_near_prim = _prim(old_root_near_path)

    _assert(
        bool(source_prim and not UsdGeom.ModelAPI(source_prim)),
        "raw-extents source payload exercises direct extentsHint fallback",
    )
    _assert(
        bool(old_root_near_prim and UsdGeom.ModelAPI(old_root_near_prim)),
        "ModelAPI candidate is available",
    )

    payload_bounds = _payload_world_bounds(source_path)

    stageviz.command.load_payloads([source_path])

    _assert(
        _wait_until(
            lambda: bool(
                _prim(source_path)
                and _prim(source_path).IsLoaded()
                and _exists(source_child_path)
            ),
            timeout=5.0,
        ),
        "raw-extents source prepared for selected-prim neighbor search",
    )

    selected_bounds = _prim_world_bounds(source_child_path)
    selected_near_bounds = _payload_world_bounds(selected_near_path)
    old_root_near_bounds = _payload_world_bounds(old_root_near_path)

    for bounds, label in (
        (selected_bounds, "raw-source selected child world bounds are available"),
        (payload_bounds, "raw-source payload extentsHint bounds are available"),
        (selected_near_bounds, "selected-near ModelAPI candidate bounds are available"),
        (old_root_near_bounds, "old root-near ModelAPI candidate bounds are available"),
    ):
        _assert(bounds is not None, label)

    if any(
        bounds is None
        for bounds in (
            selected_bounds,
            payload_bounds,
            selected_near_bounds,
            old_root_near_bounds,
        )
    ):
        return

    selected_search = _expanded_bounds(selected_bounds)
    old_payload_search = _expanded_bounds(payload_bounds)

    _assert(
        _point_inside(selected_search, selected_near_bounds.GetMidpoint()),
        "ModelAPI candidate center is inside selected child search bounds",
    )
    _assert(
        not _point_inside(selected_search, old_root_near_bounds.GetMidpoint()),
        "old ModelAPI near candidate is outside selected child search bounds",
    )
    _assert(
        _point_inside(old_payload_search, old_root_near_bounds.GetMidpoint()),
        "old ModelAPI near candidate would match payload-root bounds",
    )

    stageviz.command.select_paths([source_child_path])
    _assert(
        _wait_until(lambda: _selection() == [source_child_path]),
        "raw-source child selected for neighbor search",
    )

    stageviz.command.load_neighbor_payloads([source_child_path])

    _assert(
        _wait_until(
            lambda: bool(
                _prim(selected_near_path)
                and _prim(selected_near_path).IsLoaded()
            ),
            timeout=5.0,
        ),
        "raw source loads top-most ModelAPI payload near selected child",
    )
    _assert(
        bool(
            _prim(old_root_near_path)
            and not _prim(old_root_near_path).IsLoaded()
        ),
        "raw source does not load candidate only near payload-root bounds",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: bool(
                    _prim(selected_near_path)
                    and not _prim(selected_near_path).IsLoaded()
                ),
                timeout=5.0,
            ),
            "undo raw-source neighbor search restores selected-near payload",
        )
        _assert_equal(
            _selection(),
            [source_child_path],
            "undo raw-source neighbor search restores previous selection",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: bool(
                    _prim(selected_near_path)
                    and _prim(selected_near_path).IsLoaded()
                ),
                timeout=5.0,
            ),
            "redo raw-source neighbor search reloads selected-near payload",
        )


def test_select_payload():
    if not _has_command("select_payload"):
        print("[skip] select_payload is not bound")
        return

    stageviz.command.load_payloads(["/World/PayloadA", "/World/PayloadB"])

    _assert(
        _wait_until(
            lambda: bool(
                _exists("/World/PayloadA/Geom")
                and _exists("/World/PayloadB/Geom")
            ),
            timeout=5.0,
        ),
        "payload descendants prepared for select_payload",
    )

    stageviz.command.select_paths(
        ["/World/PayloadA/Geom", "/World/PayloadB/Geom"]
    )

    _assert(
        _wait_until(
            lambda: _selection()
            == ["/World/PayloadA/Geom", "/World/PayloadB/Geom"]
        ),
        "payload descendants selected for select_payload",
    )

    stageviz.command.select_payload()

    _assert(
        _wait_until(
            lambda: _selection()
            == ["/World/PayloadA", "/World/PayloadB"]
        ),
        "select_payload selects each nearest owning payload",
    )

    if _undo():
        _assert_equal(
            _selection(),
            ["/World/PayloadA/Geom", "/World/PayloadB/Geom"],
            "undo select_payload restores previous descendant selection",
        )

    stageviz.command.select_paths(
        ["/World/PayloadA/Geom", "/World/A"]
    )

    _assert(
        _wait_until(
            lambda: _selection()
            == ["/World/PayloadA/Geom", "/World/A"]
        ),
        "mixed payload/non-payload selection prepared",
    )

    stageviz.command.select_payload()

    _assert(
        _wait_until(lambda: _selection() == ["/World/PayloadA"]),
        "select_payload keeps only selections with an owning payload",
    )

    stageviz.command.load_payloads(
        ["/World/NestedOuter", "/World/NestedOuter/NestedInner"]
    )

    _assert(
        _wait_until(
            lambda: bool(
                _prim("/World/NestedOuter")
                and _prim("/World/NestedOuter").IsLoaded()
                and _prim("/World/NestedOuter/NestedInner")
                and _prim("/World/NestedOuter/NestedInner").IsLoaded()
                and _exists("/World/NestedOuter/NestedInner/Geom")
            ),
            timeout=5.0,
        ),
        "nested payload hierarchy prepared for select_payload",
    )

    stageviz.command.select_paths(
        ["/World/NestedOuter/NestedInner/Geom"]
    )
    _assert(
        _wait_until(
            lambda: _selection()
            == ["/World/NestedOuter/NestedInner/Geom"]
        ),
        "nested payload descendant selected",
    )

    stageviz.command.select_payload()

    _assert(
        _wait_until(
            lambda: _selection()
            == ["/World/NestedOuter/NestedInner"]
        ),
        "select_payload chooses nearest owning payload, not top-most payload",
    )

    if _undo():
        _assert_equal(
            _selection(),
            ["/World/NestedOuter/NestedInner/Geom"],
            "undo nearest select_payload restores nested descendant selection",
        )

    stageviz.command.select_paths(["/World/A"])

    _assert(
        _wait_until(lambda: _selection() == ["/World/A"]),
        "non-payload selection prepared for select_payload",
    )

    stageviz.command.select_payload()

    _wait(100)
    _assert_equal(
        _selection(),
        ["/World/A"],
        "select_payload leaves selection unchanged when no owning payload exists",
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

    stageviz.command.load_payloads(
        ["/World/NestedOuter", "/World/NestedOuter/NestedInner"]
    )

    _assert(
        _wait_until(
            lambda: bool(
                _prim("/World/NestedOuter")
                and _prim("/World/NestedOuter").IsLoaded()
                and _prim("/World/NestedOuter/NestedInner")
                and _prim("/World/NestedOuter/NestedInner").IsLoaded()
                and _exists("/World/NestedOuter/NestedInner/Geom")
            ),
            timeout=5.0,
        ),
        "nested payload hierarchy prepared for payload inversion",
    )

    stageviz.command.select_paths(
        ["/World/NestedOuter/NestedInner/Geom"]
    )
    _assert(
        _wait_until(
            lambda: _selection()
            == ["/World/NestedOuter/NestedInner/Geom"]
        ),
        "nested payload descendant selected for inversion",
    )

    stageviz.command.select_invert_payload()

    _assert(
        _wait_until(
            lambda: "/World/NestedOuter" not in _selection()
            and "/World/NestedOuter/NestedInner" not in _selection()
        ),
        "select_invert_payload treats nested selection as its top-most payload",
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
        not _edit_has_property(property_path),
        "composed visibility has no edit-layer opinion initially",
    )

    stageviz.command.hide_paths(["/World/PayloadA/Geom"], recursive=False)

    _assert(
        _wait_until(
            lambda: str(_visibility("/World/PayloadA/Geom")) == "invisible"
        ),
        "hide composed prim authors invisible",
    )

    _assert(
        _edit_has_property(property_path),
        "hide composed prim creates edit-layer visibility override",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: str(_visibility("/World/PayloadA/Geom")) != "invisible"
            ),
            "undo restores composed visibility",
        )

        _assert(
            not _edit_has_property(property_path),
            "undo removes newly-created edit-layer visibility override",
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

    payload_path = "/World/PayloadA"
    child_path = "/World/PayloadA/Geom"
    visibility_path = f"{child_path}.visibility"
    payload_translate_path = f"{payload_path}.xformOp:translate"

    payload_prim = _prim(payload_path)
    child_prim = _prim(child_path)

    _assert(
        bool(payload_prim and payload_prim.HasPayload() and payload_prim.IsLoaded()),
        "payload root prepared for reset overrides",
    )
    _assert(
        bool(child_prim),
        "payload child prepared for reset overrides",
    )

    # The payload root is owned by the root layer. Its authored transform is
    # stage content, not an override, and must survive Reset > Overrides.
    _assert(
        _set_translate(payload_path, (8.0, -3.0, 2.0)),
        "payload root transform authored",
    )

    payload_world_before = _world_transform(payload_path)

    _assert(
        _root_has_property(payload_translate_path),
        "payload root transform exists in root layer",
    )

    # Hide a composed child exactly as the UI does. This creates a root-layer
    # over containing only the visibility opinion.
    stageviz.command.hide_paths([child_path], recursive=False)

    _assert(
        _wait_until(lambda: str(_visibility(child_path)) == "invisible"),
        "payload child hidden",
    )

    _assert(
        _root_has_property(visibility_path),
        "hide creates descendant root-layer visibility override",
    )

    _assert(
        bool(_prim(payload_path) and _prim(payload_path).HasPayload() and _prim(payload_path).IsLoaded()),
        "payload remains loaded before reset",
    )

    # Resetting the payload root must recurse into composed descendants and
    # remove their override properties without touching the payload root def.
    stageviz.command.reset_overrides([payload_path])

    _assert(
        _wait_until(lambda: not _root_has_property(visibility_path)),
        "reset_overrides recursively removes descendant visibility override",
    )

    _assert(
        str(_visibility(child_path)) != "invisible",
        "reset_overrides exposes weaker payload visibility",
    )

    _assert(
        _exists(child_path),
        "reset_overrides preserves payload child",
    )

    payload_prim = _prim(payload_path)
    _assert(
        bool(payload_prim and payload_prim.HasPayload() and payload_prim.IsLoaded()),
        "reset_overrides preserves payload arc and load state",
    )

    _assert(
        _root_has_property(payload_translate_path),
        "reset_overrides preserves root-owned payload transform",
    )

    _assert(
        _matrix_close(_world_transform(payload_path), payload_world_before, tolerance=1e-8),
        "reset_overrides preserves payload root world transform",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _root_has_property(visibility_path)),
            "undo reset_overrides restores descendant visibility override",
        )

        _assert_equal(
            str(_visibility(child_path)),
            "invisible",
            "undo reset_overrides restores hidden child",
        )

        payload_prim = _prim(payload_path)
        _assert(
            bool(payload_prim and payload_prim.HasPayload() and payload_prim.IsLoaded()),
            "undo reset_overrides keeps payload loaded",
        )

        _assert(
            _matrix_close(_world_transform(payload_path), payload_world_before, tolerance=1e-8),
            "undo reset_overrides keeps payload root transform unchanged",
        )

    if _redo():
        _assert(
            _wait_until(lambda: not _root_has_property(visibility_path)),
            "redo reset_overrides removes descendant visibility override again",
        )

        payload_prim = _prim(payload_path)
        _assert(
            bool(payload_prim and payload_prim.HasPayload() and payload_prim.IsLoaded()),
            "redo reset_overrides keeps payload loaded",
        )

        _assert(
            _root_has_property(payload_translate_path),
            "redo reset_overrides still preserves payload root transform",
        )

        _assert(
            _matrix_close(_world_transform(payload_path), payload_world_before, tolerance=1e-8),
            "redo reset_overrides preserves payload root world transform",
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
        _edit_has_property(visibility_path),
        "attribute override is authored in edit layer",
    )

    _assert_equal(
        str(_visibility(path)),
        "invisible",
        "composed attribute reflects edit-layer override",
    )

    stageviz.command.reset_attribute_override(visibility_path)

    _assert(
        _wait_until(lambda: not _edit_has_property(visibility_path)),
        "reset_attribute_override removes only the edit-layer attribute spec",
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
            _wait_until(lambda: _edit_has_property(visibility_path)),
            "undo reset_attribute_override restores edit-layer attribute spec",
        )

        _assert_equal(
            str(_visibility(path)),
            "invisible",
            "undo reset_attribute_override restores overridden value",
        )

    if _redo():
        _assert(
            _wait_until(lambda: not _edit_has_property(visibility_path)),
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



def test_pivot_attribute_edit_preserves_local_transform():
    if not _has_command("set_attribute_value"):
        print("[skip] set_attribute_value is not bound")
        return

    path = "/World/A"
    _ensure_cube(f"{path}/PivotGeom", size=4.0)

    _assert(
        _set_complex_transform(
            path,
            (18.0, 0.0, 0.0),
            (27.0, -38.0, 16.0),
            (1.7, 0.65, 1.2),
        ),
        "complex transform prepared for pivot edit",
    )

    prim = _prim(path)
    xformable = UsdGeom.Xformable(prim)

    before = _local_transform(path)

    # Create the Stageviz canonical pivot stack first.
    stageviz.command.center_pivots([path])

    _assert(
        _wait_until(
            lambda: bool(
                _prim(path).GetAttribute("xformOp:translate:pivot")
            )
        ),
        "pivot created before direct pivot edit",
    )

    before_edit = _local_transform(path)

    _assert(
        _matrix_close(before_edit, before, tolerance=1e-8),
        "centering pivot preserves local transform",
    )

    stageviz.command.set_attribute_value(
        f"{path}.xformOp:translate:pivot",
        (3.0, -2.0, 7.0),
    )

    _assert(
        _wait_until(
            lambda: _matrix_close(
                _local_transform(path),
                before_edit,
                tolerance=1e-8,
            )
        ),
        "editing pivot attribute preserves local transform",
    )

    pivot = _prim(path).GetAttribute("xformOp:translate:pivot").Get()

    _assert(
        pivot is not None
        and abs(float(pivot[0]) - 3.0) < 1e-8
        and abs(float(pivot[1]) + 2.0) < 1e-8
        and abs(float(pivot[2]) - 7.0) < 1e-8,
        "pivot attribute receives requested value",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _matrix_close(
                    _local_transform(path),
                    before_edit,
                    tolerance=1e-8,
                )
            ),
            "undo pivot edit preserves local transform",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: _matrix_close(
                    _local_transform(path),
                    before_edit,
                    tolerance=1e-8,
                )
            ),
            "redo pivot edit preserves local transform",
        )


def test_reset_pivots_removes_attribute_and_preserves_transform():
    if not _has_command("center_pivots") or not _has_command("reset_pivots"):
        print("[skip] pivot commands are not fully bound")
        return

    path = "/World/A"
    _ensure_cube(f"{path}/PivotGeom", size=4.0)

    _assert(
        _set_complex_transform(
            path,
            (11.0, -3.0, 5.0),
            (23.0, -31.0, 14.0),
            (1.4, 0.8, 1.2),
        ),
        "complex transform prepared for reset pivot",
    )

    before = _local_transform(path)

    stageviz.command.center_pivots([path])

    pivot_path = f"{path}.xformOp:translate:pivot"

    _assert(
        _wait_until(lambda: _edit_has_property(pivot_path)),
        "center_pivots authors pivot attribute",
    )

    centered = _local_transform(path)

    _assert(
        _matrix_close(centered, before, tolerance=1e-8),
        "center_pivots preserves local transform before reset",
    )

    stageviz.command.reset_pivots([path])

    _assert(
        _wait_until(lambda: not _edit_has_property(pivot_path)),
        "reset_pivots removes pivot attribute from edit layer",
    )

    _assert(
        _matrix_close(_local_transform(path), before, tolerance=1e-8),
        "reset_pivots preserves local transform",
    )

    properties = _transform_property_names(path)

    _assert(
        "xformOp:translate:pivot" not in properties,
        "reset_pivots leaves no pivot property",
    )

    if _undo():
        _assert(
            _wait_until(lambda: _edit_has_property(pivot_path)),
            "undo reset_pivots restores pivot attribute",
        )

        _assert(
            _matrix_close(_local_transform(path), before, tolerance=1e-8),
            "undo reset_pivots preserves local transform",
        )

    if _redo():
        _assert(
            _wait_until(lambda: not _edit_has_property(pivot_path)),
            "redo reset_pivots removes pivot attribute again",
        )

        _assert(
            _matrix_close(_local_transform(path), before, tolerance=1e-8),
            "redo reset_pivots preserves local transform",
        )

def test_identity_transforms_removes_existing_ops():
    if not _has_command("identity_transforms"):
        print("[skip] identity_transforms is not bound")
        return

    path = "/World/A"

    _assert(
        _set_complex_transform(
            path,
            (6.0, -4.0, 2.0),
            (17.0, 29.0, -33.0),
            (1.8, 0.65, 1.25),
        ),
        "complex transform prepared for identity",
    )

    before = _local_transform(path)

    _assert(
        before is not None and not _matrix_close(before, _identity_matrix()),
        "prepared transform is not identity",
    )

    before_properties = _transform_property_names(path)

    _assert(
        "xformOp:translate" in before_properties,
        "translate op exists before identity",
    )

    _assert(
        "xformOp:rotateXYZ" in before_properties,
        "rotate op exists before identity",
    )

    _assert(
        "xformOp:scale" in before_properties,
        "scale op exists before identity",
    )

    stageviz.command.identity_transforms([path])

    _assert(
        _wait_until(
            lambda: _matrix_close(
                _local_transform(path),
                _identity_matrix(),
                tolerance=1e-8,
            )
        ),
        "identity_transforms evaluates to local identity",
    )

    properties = _transform_property_names(path)

    _assert_equal(
        properties,
        ["xformOp:transform", "xformOpOrder"],
        "identity_transforms removes stale translate/rotate/scale ops",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _matrix_close(
                    _local_transform(path),
                    before,
                    tolerance=1e-8,
                )
            ),
            "undo identity_transforms restores original local transform",
        )

        _assert_equal(
            _transform_property_names(path),
            before_properties,
            "undo identity_transforms restores original transform properties",
        )

    if _redo():
        _assert(
            _wait_until(
                lambda: _matrix_close(
                    _local_transform(path),
                    _identity_matrix(),
                    tolerance=1e-8,
                )
            ),
            "redo identity_transforms returns to identity",
        )

        _assert_equal(
            _transform_property_names(path),
            ["xformOp:transform", "xformOpOrder"],
            "redo identity_transforms removes stale transform ops again",
        )


def test_reset_transforms_removes_existing_ops():
    if not _has_command("reset_transforms"):
        print("[skip] reset_transforms is not bound")
        return

    path = "/World/A"

    _assert(
        _set_complex_transform(
            path,
            (9.0, 3.0, -2.0),
            (-11.0, 37.0, 21.0),
            (0.7, 1.6, 1.2),
        ),
        "complex transform prepared for reset_transforms",
    )

    stageviz.command.reset_transforms([path])

    _assert(
        _wait_until(
            lambda: _matrix_close(
                _local_transform(path),
                _identity_matrix(),
                tolerance=1e-8,
            )
        ),
        "reset_transforms evaluates to local identity",
    )

    _assert_equal(
        _transform_property_names(path),
        ["xformOp:transform", "xformOpOrder"],
        "reset_transforms leaves only canonical identity matrix op",
    )


def test_identity_transforms_exposes_weaker_transform():
    if not _has_command("identity_transforms"):
        print("[skip] identity_transforms is not bound")
        return

    session = stageviz.session()

    if not all(hasattr(session, name) for name in ("editLayer", "editLayers", "setEditLayer")):
        print("[skip] session edit-layer API is not bound")
        return

    root = _root_layer()
    layers = list(session.editLayers())
    sublayers = [
        identifier
        for identifier in layers
        if root and identifier != root.identifier
    ]

    if not root or not sublayers:
        print("[skip] no local sublayer available")
        return

    path = "/ExternalRoot"
    weaker_layer = sublayers[0]

    _assert(
        bool(session.setEditLayer(weaker_layer)),
        "select local sublayer for weaker transform",
    )

    _assert(
        _set_translate(path, (4.0, 2.0, -1.0)),
        "author weaker transform in local sublayer",
    )

    weaker = _local_transform(path)

    _assert(
        bool(session.setEditLayer(root.identifier)),
        "switch back to root edit layer",
    )

    _assert(
        _set_complex_transform(
            path,
            (20.0, 0.0, 0.0),
            (17.0, -9.0, 6.0),
            (2.0, 2.0, 2.0),
        ),
        "author stronger root-layer transform",
    )

    stronger = _local_transform(path)

    _assert(
        not _matrix_close(stronger, weaker),
        "stronger edit-layer transform overrides weaker sublayer transform",
    )

    stageviz.command.identity_transforms([path])

    _assert(
        _wait_until(
            lambda: _matrix_close(
                _local_transform(path),
                weaker,
                tolerance=1e-8,
            )
        ),
        "identity_transforms exposes weaker composed transform",
    )

    _assert_equal(
        _transform_property_names(path),
        [],
        "identity_transforms removes all root edit-layer transform properties when weaker transform exists",
    )


def test_attribute_value_commands():
    required = (
        "set_attribute_value",
        "set_attribute_values",
        "reset_attribute_values",
    )

    if not all(_has_command(name) for name in required):
        print("[skip] attribute value commands are not fully bound")
        return

    stage = _stage()
    prim_a = _prim("/World/A")
    prim_b = _prim("/World/B")

    attr_a = prim_a.CreateAttribute(
        "stageviz:testValue",
        Sdf.ValueTypeNames.Double,
    )
    attr_b = prim_b.CreateAttribute(
        "stageviz:testValue",
        Sdf.ValueTypeNames.Double,
    )

    attr_a.Set(1.0)
    attr_b.Set(2.0)

    path_a = "/World/A.stageviz:testValue"
    path_b = "/World/B.stageviz:testValue"

    stageviz.command.set_attribute_value(path_a, 4.25)

    _assert(
        _wait_until(
            lambda: abs(float(attr_a.Get()) - 4.25) < 1e-8
        ),
        "set_attribute_value authors one numeric value",
    )

    stageviz.command.set_attribute_values(
        [path_a, path_b],
        7.5,
    )

    _assert(
        _wait_until(
            lambda: abs(float(attr_a.Get()) - 7.5) < 1e-8
            and abs(float(attr_b.Get()) - 7.5) < 1e-8
        ),
        "set_attribute_values authors one numeric value to several attributes",
    )

    stageviz.command.reset_attribute_values([path_a, path_b])

    _assert(
        _wait_until(
            lambda: not _layer_has_default(_edit_layer(), path_a)
            and not _layer_has_default(_edit_layer(), path_b)
        ),
        "reset_attribute_values clears authored defaults",
    )


def test_reset_attribute_overrides_batch():
    if not _has_command("reset_attribute_overrides"):
        print("[skip] reset_attribute_overrides is not bound")
        return

    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(lambda: _exists("/World/PayloadA/Geom"), timeout=5.0),
        "payload child prepared for batch override reset",
    )

    path = "/World/PayloadA/Geom"
    imageable = UsdGeom.Imageable(_prim(path))

    imageable.CreateVisibilityAttr().Set(UsdGeom.Tokens.invisible)
    imageable.CreatePurposeAttr().Set(UsdGeom.Tokens.render)

    visibility = f"{path}.visibility"
    purpose = f"{path}.purpose"

    _assert(
        _edit_has_property(visibility) and _edit_has_property(purpose),
        "two composed attribute overrides prepared",
    )

    stageviz.command.reset_attribute_overrides([visibility, purpose])

    _assert(
        _wait_until(
            lambda: not _edit_has_property(visibility)
            and not _edit_has_property(purpose)
        ),
        "reset_attribute_overrides removes several overrides in one command",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _edit_has_property(visibility)
                and _edit_has_property(purpose)
            ),
            "undo reset_attribute_overrides restores all removed overrides",
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


def test_stage_load_resets_edit_layer(main_path):
    session = stageviz.session()

    if not all(hasattr(session, name) for name in ("editLayer", "editLayers", "setEditLayer")):
        print("[skip] session edit-layer API is not bound")
        return

    root = _root_layer()
    layers = list(session.editLayers())
    sublayers = [
        identifier
        for identifier in layers
        if root and identifier != root.identifier
    ]

    if not sublayers:
        print("[skip] no local sublayer available")
        return

    _assert(
        bool(session.setEditLayer(sublayers[0])),
        "sublayer selected before stage reload",
    )

    loaded = session.load(main_path, stageviz.LoadNone)

    _assert(
        loaded,
        "stage reload succeeds for edit-layer reset test",
    )

    new_root = _root_layer()

    _assert(
        bool(new_root),
        "reloaded stage has root layer",
    )

    if new_root:
        _assert_equal(
            session.editLayer(),
            new_root.identifier,
            "loading stage resets edit layer to root",
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



def test_move_payload_root_is_allowed():
    stageviz.command.load_payloads(["/World/PayloadA"])

    _assert(
        _wait_until(
            lambda: bool(
                _prim("/World/PayloadA")
                and _prim("/World/PayloadA").IsLoaded()
                and _exists("/World/PayloadA/Geom")
            ),
            timeout=5.0,
        ),
        "payload root prepared for namespace move",
    )

    _assert(_set_translate("/World/PayloadA", (8.0, -3.0, 2.0)), "payload root transform prepared")
    _assert(_set_translate("/World/B", (-4.0, 7.0, 1.0)), "payload destination transform prepared")

    before = _world_transform("/World/PayloadA")

    stageviz.command.move_path(
        ["/World/PayloadA"],
        "/World/B",
        insert_index=-1,
        preserve_world_transform=True,
    )

    _assert(
        _wait_until(
            lambda: not _exists("/World/PayloadA")
            and _exists("/World/B/PayloadA")
            and bool(_prim("/World/B/PayloadA").HasPayload())
        ),
        "move_path allows moving a root-layer payload root",
    )

    _assert(
        _wait_until(lambda: _exists("/World/B/PayloadA/Geom"), timeout=5.0),
        "moved payload root keeps composed payload content",
    )

    after = _world_transform("/World/B/PayloadA")
    _assert(
        _matrix_close(after, before, tolerance=1e-6),
        "moving payload root preserves world transform",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/PayloadA")
                and not _exists("/World/B/PayloadA")
                and bool(_prim("/World/PayloadA").HasPayload())
            ),
            "undo restores moved payload root",
        )

        _assert(
            _wait_until(lambda: _exists("/World/PayloadA/Geom"), timeout=5.0),
            "undo restores payload composition below original root",
        )

        restored = _world_transform("/World/PayloadA")
        _assert(
            _matrix_close(restored, before, tolerance=1e-6),
            "undo payload-root move restores world transform",
        )


def test_move_reference_root_is_allowed():
    _assert(
        _exists("/World/Referenced/Child"),
        "reference root prepared for namespace move",
    )

    before = _world_transform("/World/Referenced")

    stageviz.command.move_path(
        ["/World/Referenced"],
        "/World/B",
        insert_index=-1,
        preserve_world_transform=True,
    )

    _assert(
        _wait_until(
            lambda: not _exists("/World/Referenced")
            and _exists("/World/B/Referenced")
            and _exists("/World/B/Referenced/Child")
        ),
        "move_path allows moving a root-layer reference root",
    )

    moved = _world_transform("/World/B/Referenced")
    _assert(
        _matrix_close(moved, before, tolerance=1e-6),
        "moving reference root preserves world transform",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/Referenced")
                and _exists("/World/Referenced/Child")
                and not _exists("/World/B/Referenced")
            ),
            "undo restores moved reference root",
        )


def test_new_xform_wraps_payload_roots():
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
        "payload roots prepared for new_xform wrapping",
    )

    stageviz.command.select_paths(["/World/PayloadA", "/World/PayloadB"])

    _assert(
        _wait_until(lambda: _selection() == ["/World/PayloadA", "/World/PayloadB"]),
        "payload roots selected for new_xform wrapping",
    )

    stageviz.command.new_xform("/World", "PayloadGroup")

    _assert(
        _wait_until(
            lambda: _exists("/World/PayloadGroup")
            and _exists("/World/PayloadGroup/PayloadA")
            and _exists("/World/PayloadGroup/PayloadB")
            and not _exists("/World/PayloadA")
            and not _exists("/World/PayloadB")
        ),
        "new_xform can wrap root-layer payload roots",
    )

    _assert(
        bool(_prim("/World/PayloadGroup/PayloadA").HasPayload())
        and bool(_prim("/World/PayloadGroup/PayloadB").HasPayload()),
        "wrapped payload roots retain payload arcs",
    )

    if _undo():
        _assert(
            _wait_until(
                lambda: _exists("/World/PayloadA")
                and _exists("/World/PayloadB")
                and not _exists("/World/PayloadGroup")
            ),
            "undo new_xform restores payload roots",
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
        _wait_until(
            lambda: _exists("/MergeSource")
            and _exists("/MergeSource/Merged")
        ),
        "merge imports authored source hierarchy",
    )
    _assert_equal(
        list(_root_layer().subLayerPaths),
        before_sublayers,
        "merge does not add the source as a sublayer",
    )

    ok_again = session.merge(merge_source)

    _assert(
        ok_again,
        "merging the same file into the stage again succeeds",
    )
    _assert(
        _wait_until(
            lambda: _exists("/MergeSource_1")
            and _exists("/MergeSource_1/Merged")
        ),
        "second destructive merge uses unique root identifier",
    )

    ok_third = session.mergeFromFile(merge_source)

    _assert(
        ok_third,
        "mergeFromFile accepts the same file a third time",
    )
    _assert(
        _wait_until(
            lambda: _exists("/MergeSource_2")
            and _exists("/MergeSource_2/Merged")
        ),
        "third destructive merge increments unique root identifier",
    )

    _assert_set_equal(
        [
            name
            for name in _child_names("/")
            if name.startswith("MergeSource")
        ],
        ["MergeSource", "MergeSource_1", "MergeSource_2"],
        "destructive merge creates exactly the expected unique root identifiers",
    )


def test_merge_flattened(merge_source):
    session = stageviz.session()

    _assert(
        hasattr(session, "mergeFlattened"),
        "session.mergeFlattened is exposed",
    )
    _assert(
        hasattr(session, "mergeFlattenedFromFile"),
        "session.mergeFlattenedFromFile is exposed",
    )

    ok = session.mergeFlattened(merge_source)

    _assert(ok, "mergeFlattened imports composed source content")
    _assert(
        _wait_until(
            lambda: _exists("/MergeSource/MergedReference/Child")
        ),
        "mergeFlattened includes composed referenced content",
    )

    prim = _prim("/MergeSource/MergedReference")
    references = prim.GetMetadata("references") if prim else None

    has_reference = bool(
        references
        and references.GetAddedOrExplicitItems()
    )

    _assert(
        not has_reference,
        "mergeFlattened removes the incoming reference arc",
    )

    ok_again = session.mergeFlattened(merge_source)

    _assert(
        ok_again,
        "merging the same file flattened again succeeds",
    )
    _assert(
        _wait_until(
            lambda: _exists("/MergeSource_1")
            and _exists("/MergeSource_1/MergedReference/Child")
        ),
        "second flattened merge uses unique root identifier",
    )

    prim_again = _prim("/MergeSource_1/MergedReference")
    references_again = prim_again.GetMetadata("references") if prim_again else None
    has_reference_again = bool(
        references_again
        and references_again.GetAddedOrExplicitItems()
    )

    _assert(
        not has_reference_again,
        "second flattened merge also contains no incoming reference arc",
    )

    ok_third = session.mergeFlattenedFromFile(merge_source)

    _assert(
        ok_third,
        "mergeFlattenedFromFile accepts the same file a third time",
    )
    _assert(
        _wait_until(
            lambda: _exists("/MergeSource_2")
            and _exists("/MergeSource_2/MergedReference/Child")
        ),
        "third flattened merge increments unique root identifier",
    )

    _assert_set_equal(
        [
            name
            for name in _child_names("/")
            if name.startswith("MergeSource")
        ],
        ["MergeSource", "MergeSource_1", "MergeSource_2"],
        "flattened merge creates exactly the expected unique root identifiers",
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
        _wait_until(lambda: _exists("/MergeSource/Merged")),
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
    _assert(
        hasattr(session, "mergeReferenceFromFile"),
        "session.mergeReferenceFromFile is exposed",
    )

    _define_xform(stage, "/World/ReferenceTarget")
    _define_xform(stage, "/World/ReferenceTarget_1")

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

    ok_again = session.mergeReferenceFromFile(
        merge_source,
        "/World/ReferenceTarget_1",
    )

    _assert(
        ok_again,
        "mergeReferenceFromFile accepts the same source on another target",
    )
    _assert(
        _wait_until(
            lambda: _exists("/World/ReferenceTarget_1/Merged")
        ),
        "second reference composes the same source below another target",
    )


def test_merge_payload(merge_source):
    session = stageviz.session()
    stage = _stage()

    _assert(
        hasattr(session, "mergePayload"),
        "session.mergePayload is exposed",
    )
    _assert(
        hasattr(session, "mergePayloadFromFile"),
        "session.mergePayloadFromFile is exposed",
    )

    _define_xform(stage, "/World/PayloadTarget")
    _define_xform(stage, "/World/PayloadTarget_1")

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
    _assert(
        bool(target and not target.IsLoaded()),
        "payload respects LoadNone and remains unloaded",
    )

    stageviz.command.load_payloads(["/World/PayloadTarget"])

    _assert(
        _wait_until(
            lambda: _exists("/World/PayloadTarget/Merged"),
            timeout=5.0,
        ),
        "loading merged payload composes source default prim below target",
    )

    ok_again = session.mergePayloadFromFile(
        merge_source,
        "/World/PayloadTarget_1",
    )

    _assert(
        ok_again,
        "mergePayloadFromFile accepts the same source on another target",
    )

    target_again = _prim("/World/PayloadTarget_1")
    _assert(
        bool(target_again and target_again.HasPayload()),
        "second payload arc is authored on another target",
    )
    _assert(
        bool(target_again and not target_again.IsLoaded()),
        "second payload also respects LoadNone",
    )

    stageviz.command.load_payloads(["/World/PayloadTarget_1"])

    _assert(
        _wait_until(
            lambda: _exists("/World/PayloadTarget_1/Merged"),
            timeout=5.0,
        ),
        "loading second payload composes the same source below another target",
    )


def test_save_reload(saved_path):
    ok = stageviz.session().save(saved_path)
    _assert(ok, "session.save writes test stage")

    reopened = Usd.Stage.Open(saved_path)
    _assert(bool(reopened), "saved stage reopens with USD")
    _assert(bool(reopened.GetPrimAtPath("/World")), "saved stage contains /World")


def run():
    _FAILURES.clear()
    _PERF_RESULTS.clear()
    _PERF_FIXTURE_RELOADS.clear()
    _PERF_FAILURES.clear()
    _PERF_WARNINGS.clear()

    suite_start = time.perf_counter()
    baseline = _load_performance_baseline()

    fixture_start = time.perf_counter()
    root, main_path, merge_source, saved_path, external = create_fixture()
    fixture_create = time.perf_counter() - fixture_start

    print(f"Stageviz command test root: {root}")
    print(f"Performance baseline: {_PERF_BASELINE_PATH}")

    if _PERF_ENABLED:
        if _PERF_BASELINE_RESET:
            print("Performance baseline reset requested.")
        elif baseline is None:
            print("No performance baseline found; this successful run will create one.")
        else:
            print("Performance regression comparison enabled.")

    _disable_preserve_state()

    tests = [
        ("command API", test_command_api, ()),
        ("session edit layer API", test_session_edit_layer_api, ()),
        ("property edit uses selected sublayer", test_property_edit_uses_selected_sublayer, ()),
        ("select paths", test_select_paths, ()),
        ("select all and invert", test_select_all_and_invert, ()),
        ("isolate paths", test_isolate_paths, ()),
        ("show/hide paths", test_show_hide_paths, ()),
        ("new prim", test_new_prim, ()),
        ("new typed prim", test_new_typed_prim, ()),
        ("new prim unique name", test_new_prim_unique_name, ()),
        ("new scope", test_new_scope, ()),
        ("new material", test_new_material, ()),
        ("bind material", test_bind_material, ()),
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
        ("move preserve world transform keeps pivot stack", test_move_preserve_world_transform_keeps_pivot_stack, ()),
        ("move multiple siblings", test_move_multiple_siblings, ()),
        ("move rejects self parenting", test_move_rejects_self_parenting, ()),
        ("move rejects destination collision/no-op", test_move_rejects_destination_collision, ()),
        ("delete selection/mask and default prim", test_delete_removes_selection_mask_and_restores_default_prim, ()),
        ("delete default prim and undo", test_delete_default_prim_and_undo, ()),
        ("default prim validation", test_default_prim_validation, ()),
        ("stage up", test_stage_up, ()),
        ("payload load/unload", test_payload_load_unload, ()),
        ("payload variant session state", test_payload_variant_session_state, (root,)),
        ("load neighbor payloads", test_load_neighbor_payloads, ()),
        ("load neighbor payloads raw source", test_load_neighbor_payloads_raw_source, ()),
        ("select payload", test_select_payload, ()),
        ("select invert payload", test_select_invert_payload, ()),
        ("root prim order", test_root_prim_order_create_move_and_undo, ()),
        ("root layer policy", test_root_layer_policy_rejects_sublayer_prim, ()),
        ("composition boundary", test_composition_boundary_rejects_namespace_edit, ()),
        ("transactional multi move", test_multi_move_is_transactional_on_collision, ()),
        ("composed visibility override", test_composed_visibility_override_and_undo, ()),
        ("reset overrides", test_reset_overrides, ()),
        ("reset overrides root-owned policy", test_reset_overrides_ignores_root_owned_prim, ()),
        ("reset attribute override", test_reset_attribute_override, ()),
        ("reset attribute overrides batch", test_reset_attribute_overrides_batch, ()),
        ("reset attribute override root-owned policy", test_reset_attribute_override_ignores_root_owned_prim, ()),
        ("attribute value commands", test_attribute_value_commands, ()),
        ("pivot attribute edit preserves local transform", test_pivot_attribute_edit_preserves_local_transform, ()),
        ("reset pivots removes attribute", test_reset_pivots_removes_attribute_and_preserves_transform, ()),
        ("identity transforms removes existing ops", test_identity_transforms_removes_existing_ops, ()),
        ("reset transforms removes existing ops", test_reset_transforms_removes_existing_ops, ()),
        ("identity transforms exposes weaker transform", test_identity_transforms_exposes_weaker_transform, ()),
        ("move non-xformable prim", test_move_non_xformable_prim, ()),
        ("move preserve complex transform", test_move_preserve_complex_world_transform, ()),
        ("stage load resets edit layer", test_stage_load_resets_edit_layer, (main_path,)),
        ("failed load recovery", test_failed_load_leaves_valid_stage, ()),
        ("move payload root", test_move_payload_root_is_allowed, ()),
        ("move reference root", test_move_reference_root_is_allowed, ()),
        ("new_xform wraps payload roots", test_new_xform_wraps_payload_roots, ()),
        ("payload descendant namespace policy", test_payload_descendant_rejects_namespace_edit, ()),
        ("merge into stage", test_merge_into_stage, (merge_source,)),
        ("merge flattened", test_merge_flattened, (merge_source,)),
        ("merge sublayer", test_merge_sublayer, (merge_source,)),
        ("merge reference", test_merge_reference, (merge_source,)),
        ("merge payload", test_merge_payload, (merge_source,)),
        ("save/reopen", test_save_reload, (saved_path,)),
    ]

    for label, func, args in tests:
        reload_start = time.perf_counter()
        reload_fixture(main_path)
        reload_elapsed = time.perf_counter() - reload_start
        _PERF_FIXTURE_RELOADS.append(reload_elapsed)
        print(f"[TIME] fixture reload: {reload_elapsed:.4f}s")

        _run(label, func, args)

    suite_total = time.perf_counter() - suite_start
    fixture_reload_median = (
        statistics.median(_PERF_FIXTURE_RELOADS)
        if _PERF_FIXTURE_RELOADS
        else 0.0
    )

    functional_passed = not _FAILURES

    print("\n=== Stageviz command test summary ===")
    if _FAILURES:
        print(f"FAILED: {len(_FAILURES)} failure(s)")
        for failure in _FAILURES:
            print(f" - {failure}")
    else:
        print("PASSED")

    performance_passed = _print_performance_summary(
        baseline,
        fixture_create,
        fixture_reload_median,
        suite_total,
        functional_passed,
    )

    print("\n=== Stageviz final result ===")

    if not functional_passed:
        print("FAILED: functional regression(s) detected")
        return False

    if not performance_passed:
        print("FAILED: performance regression(s) detected")
        return False

    if _PERF_WARNINGS:
        print("PASSED WITH PERFORMANCE WARNINGS")
    else:
        print("PASSED")

    return True


if __name__ == "__main__":
    run()
