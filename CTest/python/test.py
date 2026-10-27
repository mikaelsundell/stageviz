# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

"""Run all embedded Stageviz Python test suites from the Python editor."""

import runpy
import sys
import traceback
from pathlib import Path


def _test_directory():
    probes = [Path.cwd(), Path(sys.executable).resolve().parent]
    if globals().get("__file__"):
        probes.insert(0, Path(__file__).resolve().parent)
    try:
        from PySide6.QtCore import QCoreApplication
        app = QCoreApplication.instance()
        if app is not None:
            probes.append(Path(app.applicationDirPath()))
    except (ImportError, RuntimeError):
        pass

    for probe in probes:
        for parent in (probe, *probe.parents):
            candidate = parent if (parent / "testapi.py").is_file() else parent / "CTest" / "python"
            if (candidate / "testapi.py").is_file() and (candidate / "testui.py").is_file():
                return candidate
    raise RuntimeError("Could not locate CTest/python from the Stageviz process paths")


def run_all():
    directory = _test_directory()
    suites = (
        ("USD commands and session API", "testapi.py"),
        ("Outliner and editor UI", "testui.py"),
        ("Layer editor workflow", "testlayers.py"),
    )
    results = []

    print("=== Stageviz Python test bench ===")
    print("Test directory:", directory)
    print("Suites run in order: command/session API, Outliner/editor UI, layer editor.")

    for label, filename in suites:
        path = directory / filename
        print("\n=== Running", label, "(" + filename + ") ===")
        if not path.is_file():
            print("[ FAIL ] Missing test script:", path)
            results.append((label, False, "missing script"))
            continue

        try:
            namespace = runpy.run_path(str(path), run_name="__main__")
            passed = namespace.get("_TEST_RESULT")
            if passed is None:
                results.append((label, False, "script did not report _TEST_RESULT"))
                print("[ FAIL ] Script completed without reporting _TEST_RESULT")
            elif bool(passed):
                results.append((label, True, ""))
                print("[ PASS ]", label)
            else:
                results.append((label, False, "suite returned failure"))
                print("[ FAIL ]", label)
        except Exception as error:
            results.append((label, False, repr(error)))
            print("[ FAIL ]", label, "raised", repr(error))
            traceback.print_exc()

    failures = [entry for entry in results if not entry[1]]
    print("\n=== Stageviz Python test summary ===")
    for label, passed, detail in results:
        print(("[ PASS ] " if passed else "[ FAIL ] ") + label + (": " + detail if detail else ""))
    if failures:
        print("FAILED:", len(failures), "of", len(results), "suites")
        return False

    print("PASSED:", len(results), "suites")
    return True


_TEST_RESULT = run_all()
