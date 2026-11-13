# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Run run() in the embedded interpreter to check console file logging."""

from pathlib import Path
import os
import tempfile
import stageviz


def run():
    assert stageviz.consoleOutputFile() is None, "do not replace an active user log"
    with tempfile.TemporaryDirectory(prefix="stageviz-console-") as directory:
        path = Path(directory) / "console.log"
        try:
            stageviz.setConsoleOutputFile(str(path), append=False)
            assert Path(stageviz.consoleOutputFile()) == path.absolute()
            os.write(1, b"native stdout marker\n")
            os.write(2, b"native stderr marker\n")
            try:
                stageviz.setConsoleOutputFile(str(Path(directory) / "missing" / "log.txt"))
            except OSError:
                pass
            else:
                raise AssertionError("invalid destination must raise OSError")
            assert Path(stageviz.consoleOutputFile()) == path.absolute()
            stageviz.setConsoleOutputFile(None)
            original = path.read_bytes()
            assert b"native stdout marker\n" in original and b"native stderr marker\n" in original
            os.write(1, b"console only marker\n")
            assert path.read_bytes() == original
            stageviz.setConsoleOutputFile(str(path))
            os.write(1, b"append marker\n")
            stageviz.setConsoleOutputFile(None)
            assert path.read_bytes() == original + b"append marker\n"
            stageviz.setConsoleOutputFile(str(path), append=False)
            os.write(1, b"replace marker\n")
            stageviz.setConsoleOutputFile(None)
            assert path.read_bytes() == b"replace marker\n"
            assert stageviz.consoleOutputFile() is None
        finally:
            stageviz.setConsoleOutputFile(None)
    print("PASS console file logging, native streams, stop, append, truncate, and failure recovery")
    return True
