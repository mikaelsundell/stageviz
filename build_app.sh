#!/bin/bash
##  Copyright 2022-present Contributors to the stageviz project.
##  SPDX-License-Identifier: BSD-3-Clause
##  https://github.com/mikaelsundell/stageviz

set -e

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
machine_arch=$(uname -m)
macos_version=$(sw_vers -productVersion)
major_version=$(echo "$macos_version" | cut -d '.' -f 1)
app_name="Stageviz"
pkg_name="stageviz"

# signing
sign_code=OFF
developerid_identity=""

# globals
github=OFF
build_type=""

# permissions
permission_app() {
    local bundle_path="$1"
    find "$bundle_path" -type f \( -name "*.dylib" -o -name "*.so" -o -name "*.bundle" -o -perm -111 \) | while read -r file; do
        echo "setting permissions for $file..."
        chmod o+r "$file"
    done
}

parse_args() {
    while [[ "$#" -gt 0 ]]; do
        case $1 in
            --target=*)
                major_version="${1#*=}"
                ;;
            --sign)
                sign_code=ON
                ;;
            --github)
                github=ON
                ;;
            *)
                build_type="$1"
                ;;
        esac
        shift
    done
}
parse_args "$@"

# target
if [ -z "$major_version" ]; then
    macos_version=$(sw_vers -productVersion)
    major_version=$(echo "$macos_version" | cut -d '.' -f 1)
fi
export MACOSX_DEPLOYMENT_TARGET=$major_version
export CMAKE_OSX_DEPLOYMENT_TARGET=$major_version

clear

if [ "$build_type" != "debug" ] && [ "$build_type" != "release" ] && [ "$build_type" != "all" ]; then
    echo "invalid build type: $build_type (use 'debug', 'release', or 'all')"
    exit 1
fi

echo "Building stageviz for $build_type"
echo "---------------------------------"

if [ "$sign_code" == "ON" ]; then
    default_developerid_identity=${DEVELOPERID_IDENTITY:-}
    read -p "enter Developer ID certificate identity [$default_developerid_identity]: " input_developerid_identity
    developerid_identity=${input_developerid_identity:-$default_developerid_identity}

    if [[ ! "$developerid_identity" == *"Developer ID"* ]]; then
        echo "Developer ID certificate identity must contain 'Developer ID'."
    fi
    echo ""
fi

# check cmake
if ! command -v cmake &> /dev/null; then
    echo "cmake not found in PATH, trying /Applications/CMake.app/Contents/bin"
    export PATH=$PATH:/Applications/CMake.app/Contents/bin
    if ! command -v cmake &> /dev/null; then
        echo "cmake could not be found, please make sure it's installed"
        exit 1
    fi
fi

# check version
if ! [[ $(cmake --version | grep -o '[0-9]\+\(\.[0-9]\+\)*' | head -n1) < "3.28.0" ]]; then
    echo "cmake version is not compatible with Qt, must be before 3.28.0 for multi configuration"
    exit 1
fi

copy_xcode_framework() {
    local xcode_root="$1"
    local framework_name="$2"
    local bundle_path="$3"

    local src_framework="${xcode_root}/${framework_name}"
    local dst_framework_dir="${bundle_path}/Contents/Frameworks"
    local dst_framework="${dst_framework_dir}/${framework_name}"
    local framework_base="${framework_name%.framework}"

    if [ ! -d "$src_framework" ]; then
        echo "Xcode framework not found: $src_framework"
        return 1
    fi

    mkdir -p "$dst_framework_dir"
    rm -rf "$dst_framework"

    echo "Copy xcode framework:"
    echo "  from: $src_framework"
    echo "  to:   $dst_framework"

    local version_folder
    version_folder=$(ls -1 "$src_framework/Versions" | grep -E '^[0-9]+(\.[0-9]+)?$' | head -n 1)
    if [ -z "$version_folder" ]; then
        echo "Error: No versioned folder found in $src_framework/Versions"
        return 1
    fi

    local src_version="${src_framework}/Versions/${version_folder}"
    local dst_version="${dst_framework}/Versions/${version_folder}"

    mkdir -p "${dst_version}/Resources"

    cp "${src_version}/${framework_base}" "${dst_version}/"
    cp "${src_version}/Resources/Info.plist" "${dst_version}/Resources/"

    ln -sfn "${version_folder}" "${dst_framework}/Versions/Current"
    ln -sfn "Versions/Current/${framework_base}" "${dst_framework}/${framework_base}"
    ln -sfn "Versions/Current/Resources" "${dst_framework}/Resources"
}

copy_extra_libs() {
    local dst_dir="$1"
    shift

    mkdir -p "$dst_dir"

    local src_lib
    for src_lib in "$@"; do
        if [ ! -f "$src_lib" ]; then
            echo "Extra library not found: $src_lib"
            return 1
        fi

        local base
        base="$(basename "$src_lib")"

        echo "Copy extra library:"
        echo "  from: $src_lib"
        echo "  to:   $dst_dir/$base"

        cp -f "$src_lib" "$dst_dir/"
    done
}

build_stageviz() {
    local build_type="$1"

    export PATH=$PATH:/Applications/CMake.app/Contents/bin

    cd "$script_dir"

    local build_dir="$script_dir/build.$build_type"
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi

    mkdir -p "$build_dir"
    cd "$build_dir"

    if ! [ -d "$THIRDPARTY_DIR" ]; then
        echo "could not find 3rdparty project in: $THIRDPARTY_DIR"
        exit 1
    fi
    local prefix="$THIRDPARTY_DIR"

    local xcode_type
    xcode_type=$(echo "$build_type" | awk '{ print toupper(substr($0, 1, 1)) tolower(substr($0, 2)) }')

    cmake .. \
        -DCMAKE_MODULE_PATH="$script_dir/modules" \
        -DCMAKE_PREFIX_PATH="$prefix" \
        -DDEPLOY_BUILD=ON \
        -G Xcode

    cmake --build . --config "$xcode_type" --parallel

    if [ "$github" == "ON" ]; then
        local app_bundle="$xcode_type/${app_name}.app"
        local dmg_file="$script_dir/${pkg_name}_macOS${major_version}_${machine_arch}_${build_type}.dmg"

        if [ -f "$dmg_file" ]; then
            rm -f "$dmg_file"
        fi

        local debug=""
        if [ "$build_type" = "debug" ]; then
            debug="-no-strip -verbose=1"
        fi

        echo "Run macdeployqt"
        "$prefix/bin/macdeployqt" "$app_bundle" ${debug}

        echo "Copy xcode Python framework"
        copy_xcode_framework \
            "/Applications/Xcode.app/Contents/Developer/Library/Frameworks" \
            "Python3.framework" \
            "$app_bundle"

        echo "Deploy extra runtime dylibs"
        copy_extra_libs \
            "$app_bundle/Contents/Frameworks" \
            "$prefix/lib/libshiboken6.abi3.6.10.dylib" \
            "$prefix/lib/libpyside6.abi3.6.10.dylib" \
            "$prefix/lib/libusd_usdSkel.dylib" \
            "$prefix/lib/libusd_usdSkelImaging.dylib" \
            "$prefix/lib/libusd_hdGp.dylib"

        echo "Deploy usd plugin info"
        local plugininfo="$prefix/lib/usd"
        local frameworks="$app_bundle/Contents/Frameworks"
        if [ -d "$plugininfo" ]; then
            echo "Copy usd plugin info to bundle $frameworks"
            rm -rf "$frameworks/usd"
            cp -R "$plugininfo" "$frameworks"
        else
            echo "Could not copy usd plugin info to Frameworks, will be skipped."
        fi

        echo "Deploy usd plugin"
        local plugin="$prefix/plugin/usd"
        local plugins_dir="$app_bundle/Contents/PlugIns"
        if [ -d "$plugin" ]; then
            echo "Copy usd plugin to bundle $plugins_dir"
            mkdir -p "$plugins_dir"
            rm -rf "$plugins_dir/usd"
            cp -R "$plugin" "$plugins_dir"
        else
            echo "Could not copy usd plugins to Contents/PlugIns, will be skipped."
        fi

        echo "Deploy python bindings"
        local site_packages_dst="$app_bundle/Contents/Frameworks/site-packages"
        mkdir -p "$site_packages_dst"

        local python_projects=(
            pxr
            PySide6
            shiboken6
            MaterialX
            OpenImageIO
        )

        local python_search_roots=(
            "$prefix/site-packages"
            "$prefix/lib/python"
            "$prefix/lib/python3.9/site-packages"
            "$prefix/python/lib/python3.9/site-packages"
        )

        find_python_package() {
            local package_name="$1"
            local root
            for root in "${python_search_roots[@]}"; do
                if [ -d "$root/$package_name" ]; then
                    echo "$root/$package_name"
                    return 0
                fi
            done
            return 1
        }

        local package_name
        for package_name in "${python_projects[@]}"; do
            local package_src
            package_src="$(find_python_package "$package_name" || true)"

            if [ -n "$package_src" ] && [ -d "$package_src" ]; then
                local package_dst="$site_packages_dst/$package_name"

                echo "Copy python package '$package_name'"
                echo "  from: $package_src"
                echo "  to:   $package_dst"

                rm -rf "$package_dst"
                cp -R "$package_src" "$package_dst"
            else
                echo "Python package '$package_name' not found in known search roots, skipping."
            fi
        done

        echo "Set permissions"
        permission_app "$app_bundle"

        echo "Run final deploymac"
        deploymac_args=(
            --bundle "$app_bundle"
            --prefix "$prefix"
            -v
        )
        if [ "$sign_code" == "ON" ]; then
            deploymac_args+=(--sign)
        fi
        if [ -n "$developerid_identity" ]; then
            deploymac_args+=(--identity "$developerid_identity")
        fi
        "$script_dir/scripts/deploymac.sh" "${deploymac_args[@]}"

        #echo "Deploy dmg"
        #"$script_dir/scripts/deploydmg.sh" -b "$app_bundle" -d "$dmg_file"
    fi
}

if [ "$build_type" == "all" ]; then
    build_stageviz "debug"
    build_stageviz "release"
else
    build_stageviz "$build_type"
fi