# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the documentation guard against representative contract drift."""
from pathlib import Path
import shutil
import tempfile
import unittest

import check_python_documentation as documentation


class DocumentationCheckTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # Copy only public documentation/binding inputs, never the working tree.
        for folder, pattern in (("sources/python", "*.cpp"), ("skills", "**/*.md")):
            for source in (documentation.ROOT / folder).glob(pattern):
                target = self.root / source.relative_to(documentation.ROOT)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
        for relative in ("README.md", "CTest/python/README.md"):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(documentation.ROOT / relative, target)
        # Link targets need to exist, but no implementation/data contents are used.
        for target in ("CTest/python/testbench.py",
                       "skills/scripts/dialogs.py"):
            path = self.root / target
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

    def mutate(self, relative, before, after):
        path = self.root / relative
        contents = path.read_text(encoding="utf-8")
        self.assertIn(before, contents)
        path.write_text(contents.replace(before, after, 1), encoding="utf-8")

    def errors(self):
        return "\n".join(documentation.check(self.root)[2])

    def test_current_contract(self):
        self.assertEqual(self.errors(), "")

    def test_removed_binding(self):
        self.mutate("sources/python/pycommand.cpp", '{ "select_invert",', '{ "select_inverse",')
        errors = self.errors()
        self.assertIn("Undocumented callable: stageviz.command.select_inverse", errors)
        self.assertIn("Unregistered documented callable: stageviz.command.select_invert", errors)

    def test_wrong_signature(self):
        self.mutate("skills/references/python-api.md", "`invokeLater(callable, delay=0) -> None`",
                    "`invokeLater(callable) -> None`")
        self.assertIn("Argument count mismatch: stageviz.Application.invokeLater", self.errors())

    def test_keyword_drift(self):
        self.mutate("sources/python/pycommand.cpp", '"recursive", nullptr', '"recurse", nullptr')
        self.assertIn("Keyword names mismatch: stageviz.command.select_all", self.errors())

    def test_constant_drift(self):
        self.mutate("sources/python/pymodule.cpp", '"PrimsImmediate"', '"PrimsNow"')
        self.assertIn("Module constant table mismatch: PrimsNow", self.errors())

    def test_enum_drift(self):
        self.mutate("sources/python/pyui.cpp", '{ "Sweep",', '{ "SweepNew",')
        self.assertIn("UI enum mismatch: ColorRole.SweepNew", self.errors())

    def test_python_syntax(self):
        self.mutate("skills/references/python-start.md", "import time", "import (")
        self.assertIn("Invalid Python", self.errors())


if __name__ == "__main__":
    unittest.main()
