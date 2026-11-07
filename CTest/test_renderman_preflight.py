# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Standalone synthetic tests; no RenderMan, Qt, or library loading required."""

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_spec = importlib.util.spec_from_file_location(
    "renderman_preflight", Path(__file__).resolve().parents[1] / "scripts" / "renderman_preflight.py")
preflight = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(preflight)


class PreflightTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def binary(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def thin(self, cpu=0x0100000C):
        return b"\xcf\xfa\xed\xfe" + struct.pack("<I", cpu)

    def test_thin_architectures(self):
        for cpu, name in ((0x0100000C, "arm64"), (0x01000007, "x86_64")):
            self.assertEqual(preflight.macho_architectures(self.binary(name, self.thin(cpu))), [name])

    def test_universal_architectures(self):
        for endian, magic in ((">", b"\xca\xfe\xba\xbe"), ("<", b"\xbe\xba\xfe\xca")):
            content = magic + struct.pack(endian + "I", 2)
            content += b"".join(struct.pack(endian + "IIIII", cpu, 0, 0, 0, 0)
                                for cpu in (0x01000007, 0x0100000C))
            self.assertEqual(preflight.macho_architectures(self.binary("fat", content)), ["arm64", "x86_64"])

    def test_universal_64(self):
        content = b"\xca\xfe\xba\xbf" + struct.pack(">I", 1)
        content += struct.pack(">IIQQII", 0x0100000C, 0, 0, 0, 0, 0)
        self.assertEqual(preflight.macho_architectures(self.binary("fat64", content)), ["arm64"])

    def test_malformed_and_missing(self):
        for content in (b"", b"not macho", b"\xcf\xfa\xed\xfe",
                        b"\xca\xfe\xba\xbe" + struct.pack(">I", 65),
                        b"\xca\xfe\xba\xbe" + struct.pack(">I", 1)):
            self.assertIsNotNone(preflight.inspect_binary(self.binary("bad", content))["error"])
        self.assertIsNotNone(preflight.inspect_binary(self.root / "missing")["error"])

    def fixture(self, cpu=0x0100000C):
        loader = self.binary("RenderManForMaya-99/lib/2026/usd_plugins_2511/hdPrmanLoader.dylib", self.thin(cpu))
        self.binary(str(loader.parent.relative_to(self.root) / "hdPrman.dylib"), self.thin(cpu))
        self.binary("RenderManProServer-99/lib/libprman.dylib", self.thin(cpu))
        return loader, self.root / "RenderManProServer-99"

    @staticmethod
    def compatible_output(command):
        if command[0].endswith("nm"):
            return "__ZN34pxrInternal_v0_25_11__pxrReserved__"
        return "@executable_path/../Frameworks/Python.framework/Python (current version 3.9.0)"

    def test_matching_initial_checks(self):
        self.fixture()
        with patch.object(preflight, "tool_output", side_effect=self.compatible_output):
            report = preflight.inspect_installation(self.root, "arm64", "25.11", "3.9")
        self.assertTrue(report["passes_initial_checks"])
        self.assertEqual(len(report["candidates"]), 1)

    def test_architecture_mismatch(self):
        loader, runtime = self.fixture(0x01000007)
        with patch.object(preflight, "tool_output", side_effect=self.compatible_output):
            report = preflight.inspect_delegate(loader, runtime, "arm64", "25.11", "3.9")
        self.assertFalse(report["passes_initial_checks"])
        self.assertEqual(len(report["problems"]), 3)

    def test_abi_mismatch(self):
        loader, runtime = self.fixture()
        def incompatible(command):
            return self.compatible_output(command).replace("25_11", "25_5").replace("3.9.0", "3.11.0")
        with patch.object(preflight, "tool_output", side_effect=incompatible):
            report = preflight.inspect_delegate(loader, runtime, "arm64", "25.11", "3.9")
        self.assertFalse(report["passes_initial_checks"])
        self.assertEqual(report["usd_versions"], ["25.5"])
        self.assertEqual(report["python_versions"], ["3.11"])

    def test_unknown_abi_fails_closed(self):
        loader, runtime = self.fixture()
        with patch.object(preflight, "tool_output", return_value=""):
            report = preflight.inspect_delegate(loader, runtime, "arm64", "25.11", "3.9")
        self.assertFalse(report["passes_initial_checks"])
        self.assertEqual(len(report["problems"]), 2)

    def test_tool_failure(self):
        loader, runtime = self.fixture()
        with patch.object(preflight, "tool_output", side_effect=RuntimeError("inspection unavailable")):
            report = preflight.inspect_delegate(loader, runtime, "arm64", "25.11", "3.9")
        self.assertFalse(report["passes_initial_checks"])

    def test_missing_installation_and_release_mismatch(self):
        self.assertFalse(preflight.inspect_installation(self.root, "arm64", "25.11", "3.9")["passes_initial_checks"])
        self.fixture()
        report = preflight.inspect_installation(self.root, "arm64", "25.11", "3.9",
                                                self.root / "RenderManProServer-98")
        self.assertFalse(report["candidates"])


if __name__ == "__main__":
    unittest.main()
