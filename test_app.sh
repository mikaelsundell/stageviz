#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required to run Stageviz tests." >&2
    exit 1
fi
exec python3 "$script_dir/CTest/run_all.py" "$@"
