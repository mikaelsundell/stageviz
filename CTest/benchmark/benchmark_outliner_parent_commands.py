# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""Benchmark Outliner commands applied to a parent with many child prims.

Run this through ``stageviz_run_python`` on a disposable Stageviz process with
MCP enabled. It creates a 5,000-child fixture, expands the Outliner, and times
rename, reparent, and delete of the parent. Each operation waits for deferred
prim updates and the expected Qt row count before recording completion.
"""

import io
import json
import os
import sys
import tempfile
import time
from contextlib import redirect_stdout
from pathlib import Path

import stageviz
TEST_SUPPORT = Path(__file__).resolve().parents[1] / "python"
sys.path.insert(0, str(TEST_SUPPORT))
import testbench


PRIM_COUNT = int(os.environ.get("STAGEVIZ_BENCHMARK_COUNT", "5000"))
FIXTURE_FILE = Path(tempfile.gettempdir()) / "stageviz_outliner_parent_benchmark.usda"
RESULT_FILE = Path(tempfile.gettempdir()) / "stageviz_outliner_parent_benchmark.json"


def _make_fixture(session, count):
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
        raise RuntimeError("Could not load parent benchmark fixture: {}".format(FIXTURE_FILE))
    return (time.perf_counter() - start) * 1000.0


def _snapshot():
    with redirect_stdout(io.StringIO()):
        return testbench.snapshot_qt_state("Outliner parent benchmark")


def _outliner_row_count():
    state = _snapshot()

    def count(rows):
        return sum(1 + count(row["children"]) for row in rows)

    for window in state["windows"]:
        for tree in window["trees"]:
            if tree["objectName"] == "outlinerTree":
                return count(tree["rows"])
    return 0


def _outliner_tree(QtWidgets):
    viewer = testbench.viewer()
    if viewer is None:
        return None
    try:
        return next((widget for widget in viewer.findChildren(QtWidgets.QTreeWidget)
                     if widget.objectName() == "outlinerTree"), None)
    except RuntimeError:
        return None


def _capture_screenshot(name):
    viewer = testbench.viewer()
    path = Path(tempfile.gettempdir()) / ("stageviz_outliner_parent_{}.png".format(name))
    if viewer is None or not viewer.grab().save(str(path)):
        raise RuntimeError("Could not capture the Outliner benchmark screenshot: {}".format(path))
    print("MCP_SCREENSHOT:", path, flush=True)


def _run(session, label, invoke, verify, expected_rows):
    start = time.perf_counter()
    invoke()
    dispatch_ms = (time.perf_counter() - start) * 1000.0
    deadline = time.perf_counter() + 180.0
    stable_checks = 0
    while time.perf_counter() < deadline:
        testbench.process_events()
        complete = (session.primsUpdate() == stageviz.PrimsImmediate
                    and verify() and _outliner_row_count() == expected_rows)
        stable_checks = stable_checks + 1 if complete else 0
        if stable_checks >= 3:
            result = {
                "operation": label,
                "dispatch_ms": dispatch_ms,
                "end_to_end_ms": (time.perf_counter() - start) * 1000.0,
                "outliner_rows": expected_rows,
                "verified": True,
            }
            print("BENCHMARK_RESULT:", json.dumps(result, sort_keys=True), flush=True)
            return result
        time.sleep(0.01)
    raise TimeoutError("{} did not complete with the expected Outliner rows".format(label))


def run_benchmark(count=PRIM_COUNT):
    if not stageviz.isMcpServerRunning():
        print("MCP endpoint:", stageviz.startMcpServer(), flush=True)

    session = testbench.session()
    load_ms = _make_fixture(session, count)
    testbench.release_qt_wrappers()
    stage = session.stage()
    source = "/World/Source"
    target = "/World/Target"
    renamed = "/World/RenamedSource"
    moved = target + "/RenamedSource"
    renamed_children = [renamed + "/Item_{:05d}".format(i) for i in range(count)]
    moved_children = [moved + "/Item_{:05d}".format(i) for i in range(count)]

    _, _, _, QtWidgets = testbench.qt()
    def _expand_outliner():
        tree = _outliner_tree(QtWidgets)
        if tree is None:
            return False
        try:
            tree.expandAll()
            return True
        except RuntimeError:
            return False

    if not testbench.wait_until(_expand_outliner, timeout=60.0):
        raise TimeoutError("Outliner tree was not available to expand")
    if not testbench.wait_until(lambda: _outliner_row_count() >= count + 4, timeout=60.0):
        raise TimeoutError("Outliner did not populate the expanded parent subtree")
    outliner_start_rows = _outliner_row_count()
    session.selection().updatePaths([source])

    results = []
    results.append(_run(
        session, "rename parent with {} children".format(count),
        lambda: stageviz.command.rename_path(source, "RenamedSource"),
        lambda: bool(stage.GetPrimAtPath(renamed))
        and not bool(stage.GetPrimAtPath(source))
        and all(bool(stage.GetPrimAtPath(path)) for path in renamed_children),
        outliner_start_rows,
    ))
    results.append(_run(
        session, "move parent with {} children".format(count),
        lambda: stageviz.command.move_path([renamed], target),
        lambda: bool(stage.GetPrimAtPath(moved))
        and not bool(stage.GetPrimAtPath(renamed))
        and all(bool(stage.GetPrimAtPath(path)) for path in moved_children),
        outliner_start_rows,
    ))
    _capture_screenshot("move_mcp")
    results.append(_run(
        session, "delete parent with {} children".format(count),
        lambda: stageviz.command.delete_paths([moved]),
        lambda: not bool(stage.GetPrimAtPath(moved))
        and all(not bool(stage.GetPrimAtPath(path)) for path in moved_children),
        outliner_start_rows - count - 1,
    ))
    _capture_screenshot("delete_mcp")

    output = {"count": count, "fixture_load_ms": load_ms,
              "outliner_rows_before": outliner_start_rows, "results": results}
    RESULT_FILE.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("BENCHMARK_SUMMARY:", json.dumps(output, sort_keys=True), flush=True)
    print("BENCHMARK_RESULT_FILE:", RESULT_FILE, flush=True)
    return True


_TEST_RESULT = run_benchmark()
