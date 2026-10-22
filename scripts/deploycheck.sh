#!/bin/bash
set -euo pipefail

app="${1:-}"
if [[ -z "$app" ]]; then
    echo "usage: $0 /path/to/stageviz.app"
    exit 1
fi

if [[ ! -d "$app" ]]; then
    echo "error: app not found: $app"
    exit 1
fi

app="$(cd "$(dirname "$app")" && pwd)/$(basename "$app")"
contents="$app/Contents"

echo "Scanning app:"
echo "  $app"
echo

has_issue=0

is_system_path() {
    local path="$1"
    [[ "$path" == /System/Library/* ]] || [[ "$path" == /usr/lib/* ]]
}

is_allowed_dynamic_ref() {
    local path="$1"

    is_system_path "$path" && return 0

    [[ "$path" == @executable_path/* ]] && return 0
    [[ "$path" == @loader_path/* ]] && return 0
    [[ "$path" == @rpath/* ]] && return 0

    [[ "$path" == "$app"/* ]] && return 0

    return 1
}

check_deps() {
    local file="$1"
    local found=0

    while IFS= read -r line; do
        local dep
        dep="$(echo "$line" | sed 's/^[[:space:]]*//' | awk '{print $1}')"

        [[ -z "$dep" ]] && continue
        [[ "$dep" == "$file" ]] && continue

        if ! is_allowed_dynamic_ref "$dep"; then
            if [[ $found -eq 0 ]]; then
                echo "BAD LINK REFERENCES:"
                echo "  $file"
                found=1
                has_issue=1
            fi
            echo "    -> $dep"
        fi
    done < <(otool -L "$file" | tail -n +2)
}

check_rpaths() {
    local file="$1"
    local found=0

    while IFS= read -r rpath; do
        [[ -z "$rpath" ]] && continue

        if is_system_path "$rpath"; then
            continue
        fi

        if [[ "$rpath" == @executable_path/* ]] || [[ "$rpath" == @loader_path/* ]] || [[ "$rpath" == @rpath/* ]]; then
            continue
        fi

        if [[ "$rpath" == "$app"/* ]]; then
            continue
        fi

        if [[ $found -eq 0 ]]; then
            echo "BAD RPATHS:"
            echo "  $file"
            found=1
            has_issue=1
        fi
        echo "    -> $rpath"
    done < <(
        otool -l "$file" | awk '
            $1 == "cmd" && $2 == "LC_RPATH" { in_rpath=1; next }
            in_rpath && $1 == "path" { print $2; in_rpath=0 }
        '
    )
}

while IFS= read -r -d '' file; do
    check_deps "$file"
    check_rpaths "$file"
done < <(
    find "$contents" \
        \( -path "$contents/MacOS/*" -o -path "$contents/Frameworks/*" -o -path "$contents/PlugIns/*" \) \
        \( -type f -perm -111 -o -name "*.dylib" -o -name "*.so" -o -name "*.bundle" \) \
        -print0
)

echo
if [[ $has_issue -eq 0 ]]; then
    echo "No obvious absolute external references found."
else
    echo "Found external references."
    exit 2
fi