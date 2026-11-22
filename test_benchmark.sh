#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [[ $(uname -s) == Darwin ]]; then
    export DYLD_IMAGE_SUFFIX=_debug
fi
exec python3 "$script_dir/CTest/benchmark/run_stageviz_benchmark.py" "$@"
