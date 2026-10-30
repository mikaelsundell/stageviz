# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""Benchmark hide or delete on a large selection in Stageviz.

Run through ``stageviz_run_python`` on a disposable Stageviz process with MCP
enabled. The generated fixture replaces the active session stage, so use a
disposable app process. The script waits for deferred prim updates and verifies
the final USD state before writing its timing result.
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
OPERATION = os.environ.get("STAGEVIZ_BENCHMARK_OPERATION", "hide")
RESULT_FILE = Path(tempfile.gettempdir()) / "stageviz_hide_delete_benchmark.json"


def _create_fixture(session, operation, count):
    paths = ["/World/Item_{:05d}".format(index) for index in range(count)]
    fixture_path = Path(tempfile.gettempdir()) / "stageviz_{}_{}_prim_fixture.usda".format(OPERATION, count)
    lines = ["#usda 1.0", "(", '    defaultPrim = "World"', ")", 'def Xform "World" {']
    lines.extend('    def Xform "Item_{:05d}" {{}}'.format(index) for index in range(count))
    lines.append("}")
    start = time.perf_counter()
    fixture_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    authored_ms = (time.perf_counter() - start) * 1000.0

    start = time.perf_counter()
    if not session.loadFromFile(str(fixture_path)):
        raise RuntimeError("Could not load benchmark fixture: {}".format(fixture_path))
    load_ms = (time.perf_counter() - start) * 1000.0
    return paths, authored_ms, load_ms


def _capture_screenshot(name):
    viewer = testbench.viewer()
    path = Path(tempfile.gettempdir()) / ("stageviz_hide_delete_{}.png".format(name))
    if viewer is None or not viewer.grab().save(str(path)):
        raise RuntimeError("Could not capture Stageviz viewer screenshot: {}".format(path))
    print("MCP_SCREENSHOT:", path, flush=True)


def _wait_for_completion(session, verify):
    deadline = time.perf_counter() + 180.0
    stable_checks = 0
    while time.perf_counter() < deadline:
        testbench.process_events()
        complete = session.primsUpdate() == stageviz.PrimsImmediate and verify()
        stable_checks = stable_checks + 1 if complete else 0
        if stable_checks >= 3:
            return
        time.sleep(0.01)
    raise TimeoutError("{} did not reach its expected USD state".format(OPERATION))


def run_benchmark(operation=OPERATION, count=PRIM_COUNT):
    if operation not in ("hide", "delete"):
        raise ValueError("operation must be 'hide' or 'delete'")
    if not stageviz.isMcpServerRunning():
        print("MCP endpoint:", stageviz.startMcpServer(), flush=True)

    session = testbench.session()
    paths, authored_ms, load_ms = _create_fixture(session, operation, count)
    stage = session.stage()
    selection_start = time.perf_counter()
    session.selection().updatePaths(paths)
    selection_ms = (time.perf_counter() - selection_start) * 1000.0
    _capture_screenshot("before")

    start = time.perf_counter()
    if operation == "hide":
        stageviz.command.hide_paths(paths)
        verify = lambda: all(
            str(stage.GetPrimAtPath(path).GetAttribute("visibility").Get())
            == str(UsdGeom.Tokens.invisible) for path in paths
        )
    else:
        stageviz.command.delete_paths(paths)
        verify = lambda: all(not bool(stage.GetPrimAtPath(path)) for path in paths)
    dispatch_ms = (time.perf_counter() - start) * 1000.0
    _wait_for_completion(session, verify)
    result = {
        "operation": operation,
        "count": count,
        "fixture_write_ms": authored_ms,
        "fixture_load_ms": load_ms,
        "selection_ms": selection_ms,
        "dispatch_ms": dispatch_ms,
        "end_to_end_ms": (time.perf_counter() - start) * 1000.0,
        "verified": True,
    }
    _capture_screenshot("after")
    RESULT_FILE.write_text(json.dumps([result], indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("BENCHMARK_RESULT:", json.dumps(result, sort_keys=True), flush=True)
    print("BENCHMARK_RESULT_FILE:", RESULT_FILE, flush=True)
    return True


_TEST_RESULT = run_benchmark()
