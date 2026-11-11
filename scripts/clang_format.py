# SPDX-License-Identifier: BSD-3-Clause
"""Format only branch-changed C++ sources and headers, including local edits."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def git(root, *arguments):
    result = subprocess.run(['git', *arguments], cwd=root, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr.decode('utf-8', errors='replace').strip())
    return result.stdout


def branch_base(root, reference=None):
    candidates = (reference,) if reference else ('master', 'origin/master')
    for candidate in candidates:
        try:
            commit = git(root, 'rev-parse', '--verify', '--end-of-options',
                         candidate + '^{commit}').decode().strip()
        except RuntimeError:
            if reference:
                raise
            continue
        return git(root, 'merge-base', 'HEAD', commit).decode().strip()
    raise RuntimeError('Cannot find master or origin/master; pass --base REF.')


def changed_files(root, base):
    names = set()
    for arguments in (
            ('diff', '--name-only', '--diff-filter=ACMR', '-z', base, 'HEAD', '--'),
            ('diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z', '--'),
            ('diff', '--name-only', '--diff-filter=ACMR', '-z', '--'),
            ('ls-files', '--others', '--exclude-standard', '-z')):
        names.update(os.fsdecode(name) for name in git(root, *arguments).split(b'\0') if name)
    selected = []
    for name in sorted(names):
        relative = Path(name)
        path = root / relative
        if (relative.parts[0] in ('sources', 'CTest') and 'investigation' not in relative.parts
                and relative.suffix in ('.cpp', '.h') and path.is_file() and not path.is_symlink()):
            selected.append(name)
    return selected


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', help='Branch base reference (default: master, then origin/master)')
    parser.add_argument('--list', action='store_true', help='List selected files without running clang-format')
    parser.add_argument('--check', action='store_true', help='Check formatting without editing files')
    parser.add_argument('--clang-format', default=os.environ.get('CLANG_FORMAT', 'clang-format'),
                        help='Formatter executable (default: CLANG_FORMAT or clang-format on PATH)')
    args = parser.parse_args(argv)
    try:
        base = branch_base(ROOT, args.base)
        files = changed_files(ROOT, base)
        print('Branch formatting base:', base, flush=True)
        if not files:
            print('No changed C++ files to format.')
            return 0
        for name in files:
            print(name, flush=True)
            if not args.list:
                options = ['--dry-run', '--Werror'] if args.check else ['-i']
                subprocess.run([args.clang_format, *options, '--', name], cwd=ROOT, check=True)
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print('clang-format failed:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
