#!/bin/bash
##  Copyright 2022-present Contributors to the stageviz project.
##  SPDX-License-Identifier: BSD-3-Clause
##  https://github.com/mikaelsundell/stageviz

set -euo pipefail

usage() {
cat << EOF
deploymac.sh -- Final macOS bundle sanitize/sign pass

Usage:
  $0 --bundle <app_bundle> --prefix <thirdparty_prefix> [options]

Options:
  -h, --help                 Show help
  -v, --verbose              Verbose output
  -b, --bundle <path>        Path to .app bundle
  -p, --prefix <path>        Path to 3rdparty prefix
  --sign                     Sign binaries and bundle
  --identity <name>          Signing identity (ad-hoc if omitted)
EOF
}

verbose=0
bundle=""
prefix=""
sign_code=0
identity=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -v|--verbose)
            verbose=1
            shift
            ;;
        -b|--bundle)
            if [[ $# -lt 2 ]]; then
                echo "Error: missing value for --bundle"
                exit 1
            fi
            bundle="$2"
            shift 2
            ;;
        -p|--prefix)
            if [[ $# -lt 2 ]]; then
                echo "Error: missing value for --prefix"
                exit 1
            fi
            prefix="$2"
            shift 2
            ;;
        --sign)
            sign_code=1
            shift
            ;;
        --identity)
            if [[ $# -lt 2 ]]; then
                echo "Error: missing value for --identity"
                exit 1
            fi
            identity="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1"
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$bundle" || -z "$prefix" ]]; then
    echo "Error: --bundle and --prefix are required."
    exit 1
fi

if [[ ! -d "$bundle" ]]; then
    echo "Error: bundle does not exist: $bundle"
    exit 1
fi

if [[ ! -d "$prefix" ]]; then
    echo "Error: prefix does not exist: $prefix"
    exit 1
fi

bundle="$(cd "$bundle" && pwd)"
prefix="$(cd "$prefix" && pwd)"

log() {
    if [[ $verbose -eq 1 ]]; then
        echo "$@"
    fi
}

debug_file_state() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in debug_file_state"
        return 1
    fi

    echo "---- DEBUG FILE STATE ----"
    echo "file: $path"
    echo "install id:"
    otool -D "$path" 2>/dev/null || true
    echo "rpaths:"
    otool -l "$path" 2>/dev/null | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print "  " $2 }
    ' | sort -u || true
    echo "deps:"
    otool -L "$path" 2>/dev/null || true
    echo "--------------------------"
}

print_expected_rpaths() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in print_expected_rpaths"
        return 1
    fi

    case "$path" in
        */Contents/MacOS/*)
            echo "  @executable_path/../Frameworks"
            ;;
        */Contents/Frameworks/Python3.framework/Versions/*/Python3)
            echo "  @loader_path/../../../../"
            echo "  @executable_path/../Frameworks"
            ;;
        */Contents/PlugIns/usd/*.dylib)
            echo "  @loader_path/../../Frameworks"
            echo "  @executable_path/../Frameworks"
            ;;
        */Contents/Frameworks/site-packages/*.so|*/Contents/Frameworks/site-packages/*.dylib|*/Contents/Frameworks/site-packages/*/*.so|*/Contents/Frameworks/site-packages/*/*.dylib|*/Contents/Frameworks/site-packages/*/*/*.so|*/Contents/Frameworks/site-packages/*/*/*.dylib)
            echo "  @loader_path/../../"
            echo "  @executable_path/../Frameworks"
            ;;
        *)
            echo "  <no enforced set>"
            ;;
    esac
}

print_rpath_analysis() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in print_rpath_analysis"
        return 1
    fi

    echo "==== RPATH ANALYSIS ===="
    echo "file: $path"
    echo "current rpaths:"
    list_rpaths "$path" | sed 's/^/  /' || true
    echo "desired rpaths:"
    print_expected_rpaths "$path"
    echo "dependencies:"
    list_deps "$path" | sed 's/^/  /' || true
    echo "========================"
}

is_macho() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in is_macho"
        return 1
    fi
    file "$path" | grep -q "Mach-O"
}

list_rpaths() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in list_rpaths"
        return 1
    fi
    otool -l "$path" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }
    ' | sort -u
}

list_deps() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in list_deps"
        return 1
    fi
    otool -L "$path" | tail -n +2 | awk '{print $1}' | sort -u
}

current_install_id() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in current_install_id"
        return 1
    fi
    otool -D "$path" 2>/dev/null | tail -n +2 | head -n 1 || true
}

path_contains_bad_rpath() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in path_contains_bad_rpath"
        return 1
    fi

    local found=1
    while IFS= read -r rpath; do
        [[ -z "$rpath" ]] && continue
        case "$rpath" in
            "$prefix"/*|/Applications/Xcode.app/*)
                echo "$rpath"
                found=0
                ;;
        esac
    done < <(list_rpaths "$path")
    return $found
}

path_contains_bad_dep() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in path_contains_bad_dep"
        return 1
    fi

    local found=1
    while IFS= read -r dep; do
        [[ -z "$dep" ]] && continue
        case "$dep" in
            "$prefix"/*|/Applications/Xcode.app/*)
                echo "$dep"
                found=0
                ;;
        esac
    done < <(list_deps "$path")
    return $found
}

path_has_rpath() {
    local path="${1:-}"
    local wanted="${2:-}"
    if [[ -z "$path" || -z "$wanted" ]]; then
        echo "Error: missing argument in path_has_rpath"
        return 1
    fi
    list_rpaths "$path" | grep -Fxq "$wanted"
}

delete_bad_rpaths() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in delete_bad_rpaths"
        return 1
    fi

    local rpath
    while IFS= read -r rpath; do
        [[ -z "$rpath" ]] && continue
        case "$rpath" in
            "$prefix"/*|/Applications/Xcode.app/*)
                echo "Delete bad rpath:"
                echo "  file:  $path"
                echo "  rpath: $rpath"
                install_name_tool -delete_rpath "$rpath" "$path"
                ;;
        esac
    done < <(list_rpaths "$path")
}

delete_all_rpaths() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in delete_all_rpaths"
        return 1
    fi

    local rpath
    while IFS= read -r rpath; do
        [[ -z "$rpath" ]] && continue
        echo "Delete rpath:"
        echo "  file:  $path"
        echo "  rpath: $rpath"
        install_name_tool -delete_rpath "$rpath" "$path"
    done < <(list_rpaths "$path")
}

add_rpath_if_missing() {
    local path="${1:-}"
    local rpath="${2:-}"

    if [[ -z "$path" || -z "$rpath" ]]; then
        echo "Error: missing argument in add_rpath_if_missing"
        return 1
    fi

    if ! path_has_rpath "$path" "$rpath"; then
        echo "Add rpath:"
        echo "  file:  $path"
        echo "  rpath: $rpath"
        install_name_tool -add_rpath "$rpath" "$path"
    else
        log "Keep existing rpath in $path: $rpath"
    fi
}

rewrite_dependency() {
    local path="${1:-}"
    local old_dep="${2:-}"
    local new_dep="${3:-}"

    if [[ -z "$path" || -z "$old_dep" || -z "$new_dep" ]]; then
        echo "Error: missing argument in rewrite_dependency"
        return 1
    fi

    if list_deps "$path" | grep -Fxq "$old_dep"; then
        echo "Rewrite dependency:"
        echo "  file: $path"
        echo "  from: $old_dep"
        echo "  to:   $new_dep"
        install_name_tool -change "$old_dep" "$new_dep" "$path"
    fi
}

rewrite_bad_dependencies() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in rewrite_bad_dependencies"
        return 1
    fi

    local dep
    while IFS= read -r dep; do
        [[ -z "$dep" ]] && continue

        case "$dep" in
            "$prefix"/lib/lib*.dylib)
                rewrite_dependency "$path" "$dep" "@rpath/$(basename "$dep")"
                ;;
            "$prefix"/plugin/usd/*.dylib)
                rewrite_dependency "$path" "$dep" "@rpath/$(basename "$dep")"
                ;;
            "$prefix"/lib/*.framework/Versions/Current/*)
                rewrite_dependency "$path" "$dep" "@rpath/${dep#"$prefix"/lib/}"
                ;;
            "$prefix"/lib/*.so)
                rewrite_dependency "$path" "$dep" "@rpath/$(basename "$dep")"
                ;;
            "$prefix"/lib/python/*.so|"$prefix"/lib/python/*.dylib)
                rewrite_dependency "$path" "$dep" "@rpath/$(basename "$dep")"
                ;;
            "$prefix"/lib/python3.9/site-packages/*.so|"$prefix"/lib/python3.9/site-packages/*.dylib)
                rewrite_dependency "$path" "$dep" "@rpath/$(basename "$dep")"
                ;;
            /Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/*/Python3)
                rewrite_dependency "$path" "$dep" "@rpath/Python3.framework/Versions/3.9/Python3"
                ;;
        esac
    done < <(list_deps "$path")
}

desired_install_id() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in desired_install_id"
        return 1
    fi

    local base
    base="$(basename "$path")"

    case "$path" in
        */Contents/Frameworks/*.dylib)
            echo "@rpath/$base"
            ;;
        */Contents/PlugIns/usd/*.dylib)
            echo "@rpath/$base"
            ;;
        */Contents/Frameworks/site-packages/*.so|*/Contents/Frameworks/site-packages/*/*.so|*/Contents/Frameworks/site-packages/*/*/*.so)
            echo "@rpath/$base"
            ;;
        *)
            echo ""
            ;;
    esac
}

set_install_id_if_needed() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in set_install_id_if_needed"
        return 1
    fi

    local current_id desired_id
    current_id="$(current_install_id "$path")"
    desired_id="$(desired_install_id "$path")"

    if [[ -n "$desired_id" && "$current_id" != "$desired_id" ]]; then
        echo "Set install id:"
        echo "  file: $path"
        echo "  from: ${current_id:-<empty>}"
        echo "  to:   $desired_id"
        install_name_tool -id "$desired_id" "$path"
    else
        log "Keep install id for $path: ${current_id:-<empty>}"
    fi
}

needs_rpath_enforcement() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in needs_rpath_enforcement"
        return 1
    fi

    case "$path" in
        */Contents/MacOS/*)
            return 0
            ;;
        */Contents/Frameworks/Python3.framework/Versions/*/Python3)
            return 0
            ;;
        */Contents/PlugIns/usd/*.dylib)
            return 0
            ;;
        */Contents/Frameworks/site-packages/*.so|*/Contents/Frameworks/site-packages/*.dylib|*/Contents/Frameworks/site-packages/*/*.so|*/Contents/Frameworks/site-packages/*/*.dylib|*/Contents/Frameworks/site-packages/*/*/*.so|*/Contents/Frameworks/site-packages/*/*/*.dylib)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

verify_expected_rpaths_exact() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in verify_expected_rpaths_exact"
        return 1
    fi

    local current_sorted desired_sorted
    current_sorted="$(list_rpaths "$path" | sort || true)"

    case "$path" in
        */Contents/MacOS/*)
            desired_sorted="$(printf '%s\n' '@executable_path/../Frameworks' | sort)"
            ;;
        */Contents/Frameworks/Python3.framework/Versions/*/Python3)
            desired_sorted="$(printf '%s\n' \
                '@loader_path/../../../../' \
                '@executable_path/../Frameworks' | sort)"
            ;;
        */Contents/PlugIns/usd/*.dylib)
            desired_sorted="$(printf '%s\n' \
                '@loader_path/../../Frameworks' \
                '@executable_path/../Frameworks' | sort)"
            ;;
        */Contents/Frameworks/site-packages/*.so|*/Contents/Frameworks/site-packages/*.dylib|*/Contents/Frameworks/site-packages/*/*.so|*/Contents/Frameworks/site-packages/*/*.dylib|*/Contents/Frameworks/site-packages/*/*/*.so|*/Contents/Frameworks/site-packages/*/*/*.dylib)
            desired_sorted="$(printf '%s\n' \
                '@loader_path/../../' \
                '@executable_path/../Frameworks' | sort)"
            ;;
        *)
            return 0
            ;;
    esac

    [[ "$current_sorted" == "$desired_sorted" ]]
}

enforce_required_rpaths() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in enforce_required_rpaths"
        return 1
    fi

    case "$path" in
        */Contents/MacOS/*)
            add_rpath_if_missing "$path" "@executable_path/../Frameworks"
            ;;
        */Contents/Frameworks/Python3.framework/Versions/*/Python3)
            add_rpath_if_missing "$path" "@loader_path/../../../../"
            add_rpath_if_missing "$path" "@executable_path/../Frameworks"
            ;;
        */Contents/PlugIns/usd/*.dylib)
            add_rpath_if_missing "$path" "@loader_path/../../Frameworks"
            add_rpath_if_missing "$path" "@executable_path/../Frameworks"
            ;;
        */Contents/Frameworks/site-packages/*.so|*/Contents/Frameworks/site-packages/*.dylib|*/Contents/Frameworks/site-packages/*/*.so|*/Contents/Frameworks/site-packages/*/*.dylib|*/Contents/Frameworks/site-packages/*/*/*.so|*/Contents/Frameworks/site-packages/*/*/*.dylib)
            add_rpath_if_missing "$path" "@loader_path/../../"
            add_rpath_if_missing "$path" "@executable_path/../Frameworks"
            ;;
    esac
}

file_needs_changes() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in file_needs_changes"
        return 1
    fi

    local desired_id current_id

    if path_contains_bad_rpath "$path" >/dev/null; then
        log "Needs change because bad rpath exists: $path"
        return 0
    fi

    if path_contains_bad_dep "$path" >/dev/null; then
        log "Needs change because bad dependency exists: $path"
        return 0
    fi

    desired_id="$(desired_install_id "$path")"
    if [[ -n "$desired_id" ]]; then
        current_id="$(current_install_id "$path")"
        if [[ "$current_id" != "$desired_id" ]]; then
            log "Needs change because install id differs: $path"
            return 0
        fi
    fi

    if needs_rpath_enforcement "$path"; then
        if ! verify_expected_rpaths_exact "$path"; then
            log "Needs change because rpath set differs: $path"
            return 0
        fi
    fi

    return 1
}

sanitize_file() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in sanitize_file"
        return 1
    fi

    [[ -f "$path" ]] || return 0
    is_macho "$path" || return 0

    if ! file_needs_changes "$path"; then
        log "Skip (no changes needed): $path"
        return 0
    fi

    echo "Sanitize: $path"
    if [[ $verbose -eq 1 ]]; then
        debug_file_state "$path"
        print_rpath_analysis "$path"
    fi

    if needs_rpath_enforcement "$path" && ! verify_expected_rpaths_exact "$path"; then
        echo "RPATH set differs from desired set, clearing all rpaths first:"
        echo "  file: $path"
        delete_all_rpaths "$path"
    else
        delete_bad_rpaths "$path"
    fi

    rewrite_bad_dependencies "$path"
    set_install_id_if_needed "$path"
    enforce_required_rpaths "$path"

    if [[ $verbose -eq 1 ]]; then
        echo "After sanitize:"
        debug_file_state "$path"
        print_rpath_analysis "$path"
    fi
}

verify_no_bad_refs() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in verify_no_bad_refs"
        return 1
    fi

    if list_rpaths "$path" | grep -Fq "$prefix"; then
        echo "Error: bad LC_RPATH still references prefix in:"
        echo "  $path"
        echo "Current LC_RPATH state:"
        list_rpaths "$path" | sed 's/^/  /'
        return 1
    fi

    if list_rpaths "$path" | grep -Fq "/Applications/Xcode.app"; then
        echo "Error: bad LC_RPATH still references Xcode in:"
        echo "  $path"
        echo "Current LC_RPATH state:"
        list_rpaths "$path" | sed 's/^/  /'
        return 1
    fi

    if list_deps "$path" | grep -Fq "$prefix"; then
        echo "Error: bad dependency still references prefix in:"
        echo "  $path"
        echo "Current dependencies:"
        list_deps "$path" | sed 's/^/  /'
        return 1
    fi

    if list_deps "$path" | grep -Fq "/Applications/Xcode.app"; then
        echo "Error: bad dependency still references Xcode in:"
        echo "  $path"
        echo "Current dependencies:"
        list_deps "$path" | sed 's/^/  /'
        return 1
    fi

    if needs_rpath_enforcement "$path" && ! verify_expected_rpaths_exact "$path"; then
        echo "Error: unexpected LC_RPATH set in:"
        echo "  $path"
        print_rpath_analysis "$path"
        return 1
    fi
}

sign_file() {
    local path="${1:-}"
    if [[ -z "$path" ]]; then
        echo "Error: missing path argument in sign_file"
        return 1
    fi

    if [[ $sign_code -ne 1 ]]; then
        return 0
    fi

    if [[ -n "$identity" ]]; then
        echo "Sign: $path"
        codesign --force --sign "$identity" --timestamp --options runtime "$path"
    else
        echo "Ad-hoc sign: $path"
        codesign --force --sign - "$path"
    fi
}

macho_files=()
while IFS= read -r -d '' f; do
    macho_files+=("$f")
done < <(
    find "$bundle/Contents" \
        -type f \
        \( -name "*.dylib" -o -name "*.so" -o -name "*.bundle" -o -perm -111 \) \
        -print0
)

echo "Found ${#macho_files[@]} candidate Mach-O files"

for f in "${macho_files[@]}"; do
    sanitize_file "$f"
done

verify_failures=0
for f in "${macho_files[@]}"; do
    if ! verify_no_bad_refs "$f"; then
        verify_failures=$((verify_failures + 1))
    fi
done

if [[ $sign_code -eq 1 ]]; then
    echo "Signing modified bundle with explicit signing enabled"

    for f in "${macho_files[@]}"; do
        sign_file "$f"
    done

    if [[ -n "$identity" ]]; then
        echo "Sign app bundle: $bundle"
        codesign --force --deep --sign "$identity" --timestamp --options runtime "$bundle"
    else
        echo "Ad-hoc sign app bundle: $bundle"
        codesign --force --deep --sign - "$bundle"
    fi
else
    echo "No explicit signing requested, apply final ad-hoc deep sign to restore valid bundle signatures"
    codesign --force --deep --sign - "$bundle"
fi

if [[ $verify_failures -ne 0 ]]; then
    echo "deploymac completed with $verify_failures verification failure(s)"
    exit 1
fi

echo "deploymac complete"