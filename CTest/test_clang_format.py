# SPDX-License-Identifier: BSD-3-Clause
"""Verify branch-scoped formatting without touching unrelated files."""
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import clang_format


class ChangedFormatting(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='stageviz format ')
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.git('init', '-b', 'master')
        self.git('config', 'user.name', 'Stageviz Test')
        self.git('config', 'user.email', 'test@example.invalid')
        for path in ('sources/committed.cpp', 'sources/staged.h', 'sources/unstaged.cpp',
                     'sources/unchanged.cpp', 'sources/deleted.h', 'sources/old.h',
                     'sources/flip.cpp', 'CTest/check.cpp'):
            self.write(path, 'original\n')
        self.write('.gitignore', 'ignored.cpp\ninvestigation/\n')
        self.git('add', '.')
        self.commit()
        self.base = clang_format.branch_base(self.root)
        self.git('checkout', '-b', 'work')

    def git(self, *args):
        return clang_format.git(self.root, *args)

    def commit(self):
        self.git('-c', 'commit.gpgsign=false', 'commit', '-sm', 'test fixture')

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8')

    def test_all_change_types_and_exclusions(self):
        self.write('sources/committed.cpp', 'branch\n')
        self.git('mv', 'sources/old.h', 'sources/renamed header.h')
        self.git('rm', 'sources/deleted.h')
        self.git('add', '.')
        self.commit()
        self.write('sources/staged.h', 'staged\n')
        self.write('sources/flip.cpp', 'staged\n')
        self.git('add', '.')
        self.write('sources/flip.cpp', 'original\n')
        self.write('sources/unstaged.cpp', 'working\n')
        self.write('CTest/check.cpp', 'working\n')
        self.write('sources/new header.h', 'new\n')
        self.write('sources/ignored.cpp', 'ignored\n')
        self.write('sources/readme.md', 'prose\n')
        self.write('outside.h', 'outside scope\n')
        self.assertEqual(clang_format.changed_files(self.root, self.base), sorted([
            'sources/committed.cpp', 'sources/staged.h', 'sources/flip.cpp',
            'sources/unstaged.cpp', 'CTest/check.cpp', 'sources/new header.h',
            'sources/renamed header.h']))

    def test_merge_base_excludes_new_master_changes(self):
        self.write('sources/committed.cpp', 'branch\n')
        self.git('add', '.')
        self.commit()
        self.git('checkout', 'master')
        self.write('sources/unchanged.cpp', 'master only\n')
        self.git('add', '.')
        self.commit()
        self.git('checkout', 'work')
        self.assertEqual(clang_format.branch_base(self.root), self.base)
        self.assertEqual(clang_format.changed_files(self.root, self.base), ['sources/committed.cpp'])

    def test_clean_branch_does_not_invoke_formatter(self):
        with mock.patch.object(clang_format, 'ROOT', self.root), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(clang_format.main(['--clang-format', 'missing-formatter']), 0)

    def test_remote_master_fallback(self):
        self.git('branch', '-m', 'master', 'main')
        self.git('update-ref', 'refs/remotes/origin/master', self.base)
        self.assertEqual(clang_format.branch_base(self.root), self.base)

    def test_list_and_check_do_not_modify_files(self):
        self.write('sources/changed header.h', 'unformatted\n')
        with mock.patch.object(clang_format, 'ROOT', self.root), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(clang_format.main(['--list', '--clang-format', 'missing-formatter']), 0)
        self.assertIn('sources/changed header.h', output.getvalue())
        original_run = subprocess.run
        calls = []
        def run(command, **kwargs):
            if command[0] != 'formatter':
                return original_run(command, **kwargs)
            calls.append(command)
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(clang_format, 'ROOT', self.root), \
                mock.patch.object(clang_format.subprocess, 'run', side_effect=run), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(clang_format.main(['--check', '--clang-format', 'formatter']), 0)
        self.assertEqual(calls, [['formatter', '--dry-run', '--Werror', '--', 'sources/changed header.h']])
        self.assertEqual((self.root / 'sources/changed header.h').read_text(), 'unformatted\n')

    def test_only_selected_files_are_modified(self):
        self.write('sources/changed header.h', 'unformatted\n')
        original_run = subprocess.run
        calls = []
        def run(command, **kwargs):
            if command[0] != 'formatter with spaces':
                return original_run(command, **kwargs)
            calls.append(command)
            self.write(command[-1], 'formatted\n')
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(clang_format, 'ROOT', self.root), \
                mock.patch.object(clang_format.subprocess, 'run', side_effect=run), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(clang_format.main(['--clang-format', 'formatter with spaces']), 0)
        self.assertEqual(calls, [['formatter with spaces', '-i', '--', 'sources/changed header.h']])
        self.assertEqual((self.root / 'sources/unchanged.cpp').read_text(), 'original\n')
        self.assertEqual((self.root / 'sources/changed header.h').read_text(), 'formatted\n')

    def test_invalid_base_fails(self):
        with mock.patch.object(clang_format, 'ROOT', self.root), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(clang_format.main(['--base', 'missing-reference']), 1)


if __name__ == '__main__':
    unittest.main()
