#!/bin/bash

set -e

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

echo "Running clang-format in project: $project_root"

for directory in "sources" "CTest"; do
    target_dir="$project_root/$directory"

    if [ -d "$target_dir" ]; then
        echo "Formatting: $target_dir"

        find "$target_dir" -type f \
            \( -name '*.cpp' -o -name '*.h' \) \
            -exec clang-format -i {} +
    fi
done

echo "Clang-format completed."
