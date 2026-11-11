#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required to select changed files for clang-format." >&2
    exit 1
fi
exec python3 "$script_dir/clang_format.py" "$@"
