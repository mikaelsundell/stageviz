# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""Benchmark large selection visibility and namespace commands in Stageviz.

Run this through ``stageviz_run_python`` on a disposable Stageviz process with
MCP enabled. It loads a generated fixture into the active session, measures
hide/show/rename/move/delete, waits for deferred prim updates to finish, and
checks the resulting USD state after each operation.
"""

import json
import os
import sys
import tempfile
import time
from pathlib import Path

import stageviz
from pxr import UsdGeom

TEST_SUPPORT = Path(__file__).resolve().parents[1] / "python"
sys.path.insert(0, str(TEST_SUPPORT))
import testbench


PRIM_COUNT = int(os.environ.get("STAGEVIZ_BENCHMARK_COUNT", "5000"))
RESULT_FILE = Path(tempfile.gettempdir()) / "stageviz_prim_command_benchmark.json"
FIXTURE_FILE = Path(tempfile.gettempdir()) / "stageviz_prim_command_benchmark.usda"


def _create_fixture(session, count=PRIM_COUNT):
    lines = [
        "#usda 1.0",
        "(",
        '    defaultPrim = "World"',
        ")",
        'def Xform "World" {',
        '    def Xform "Source" {',
    ]
    lines.extend('        def Xform "Item_{:05d}" {{}}'.format(i) for i in range(count))
    lines.extend(['    }', '    def Xform "Target" {}', '}'])
    FIXTURE_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")

    start = time.perf_counter()
    if not session.loadFromFile(str(FIXTURE_FILE)):
        raise RuntimeError("Could not load benchmark fixture: {}".format(FIXTURE_FILE))
    load_ms = (time.perf_counter() - start) * 1000.0
    print("FIXTURE: {} prims, load/notice/UI={:.3f} ms".format(count + 3, load_ms), flush=True)
    return load_ms


def _capture_screenshot(name):
    viewer = testbench.viewer()
    path = Path(tempfile.gettempdir()) / ("stageviz_prim_benchmark_{}.png".format(name))
    if viewer is None or not viewer.grab().save(str(path)):
        raise RuntimeError("Could not capture Stageviz viewer screenshot: {}".format(path))
    print("MCP_SCREENSHOT:", path, flush=True)
    return str(path)


def _run_command(session, label, invoke, verify, count):
    start = time.perf_counter()
    invoke()
    dispatch_ms = (time.perf_counter() - start) * 1000.0

    deadline = time.perf_counter() + 180.0
    stable_checks = 0
    while time.perf_counter() < deadline:
        testbench.process_events()
        complete = session.primsUpdate() == stageviz.PrimsImmediate and verify()
        stable_checks = stable_checks + 1 if complete else 0
        if stable_checks >= 3:
            result = {
                "operation": label,
                "count": count,
                "dispatch_ms": dispatch_ms,
                "end_to_end_ms": (time.perf_counter() - start) * 1000.0,
                "verified": True,
            }
            print("BENCHMARK_RESULT:", json.dumps(result, sort_keys=True), flush=True)
            return result
        time.sleep(0.01)

    raise TimeoutError("{} did not finish with the expected USD state".format(label))


def run_benchmark(count=PRIM_COUNT):
    if not stageviz.isMcpServerRunning():
        print("MCP endpoint:", stageviz.startMcpServer(), flush=True)
    else:
        print("MCP endpoint: already running", flush=True)

    session = testbench.session()
    fixture_load_ms = _create_fixture(session, count)
    stage = session.stage()
    source = "/World/Source"
    target = "/World/Target"
    paths = ["{}/Item_{:05d}".format(source, i) for i in range(count)]
    selection_start = time.perf_counter()
    session.selection().updatePaths(paths)
    selection_ms = (time.perf_counter() - selection_start) * 1000.0
    print("SELECTION: {} paths, update={:.3f} ms".format(count, selection_ms), flush=True)
    _capture_screenshot("before")

    results = []
    results.append(_run_command(
        session, "hide",
        lambda: stageviz.command.hide_paths(paths),
        lambda: all(str(stage.GetPrimAtPath(path).GetAttribute("visibility").Get())
                    == str(UsdGeom.Tokens.invisible) for path in paths),
        count,
    ))
    results.append(_run_command(
        session, "show",
        lambda: stageviz.command.show_paths(paths),
        lambda: all(str(stage.GetPrimAtPath(path).GetAttribute("visibility").Get())
                    == str(UsdGeom.Tokens.inherited) for path in paths),
        count,
    ))
    _capture_screenshot("visibility")

    renamed_source = "/World/RenamedSource"
    renamed_paths = [path.replace(source, renamed_source, 1) for path in paths]
    results.append(_run_command(
        session, "rename subtree",
        lambda: stageviz.command.rename_path(source, "RenamedSource"),
        lambda: bool(stage.GetPrimAtPath(renamed_source))
        and not bool(stage.GetPrimAtPath(source))
        and all(bool(stage.GetPrimAtPath(path)) for path in renamed_paths),
        count,
    ))
    moved_paths = [path.replace(renamed_source, target, 1) for path in renamed_paths]
    results.append(_run_command(
        session, "move selected prims",
        lambda: stageviz.command.move_path(renamed_paths, target),
        lambda: all(bool(stage.GetPrimAtPath(path)) for path in moved_paths)
        and all(not bool(stage.GetPrimAtPath(path)) for path in renamed_paths),
        count,
    ))
    results.append(_run_command(
        session, "delete selected prims",
        lambda: stageviz.command.delete_paths(moved_paths),
        lambda: all(not bool(stage.GetPrimAtPath(path)) for path in moved_paths),
        count,
    ))

    output = {
        "count": count,
        "fixture_load_ms": fixture_load_ms,
        "selection_update_ms": selection_ms,
        "results": results,
        "screenshots": ["stageviz_prim_benchmark_before.png",
                        "stageviz_prim_benchmark_visibility.png"],
    }
    RESULT_FILE.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("BENCHMARK_SUMMARY:", json.dumps(output, sort_keys=True), flush=True)
    print("BENCHMARK_RESULT_FILE:", RESULT_FILE, flush=True)
    return True


_TEST_RESULT = run_benchmark()
