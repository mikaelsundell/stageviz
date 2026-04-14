#!/bin/bash
##  Copyright 2022-present Contributors to the stageviz project.
##  SPDX-License-Identifier: BSD-3-Clause
##  https://github.com/mikaelsundell/stageviz

# usage
usage()
{
cat << EOF
deployplugin.sh -- Deploy a usd plugin

usage: $0 [options]

Options:
   -h, --help              Print help message
   -v, --verbose           Print verbose output
   -d, --dylib             Path to the dylib (required)
   -p, --prefix            Path to the prefix (required)
EOF
}

overwrite=0
processed_paths=""

# parse arguments
i=0; argv=()
for ARG in "$@"; do
    argv[$i]="${ARG}"
    i=$((i + 1))
done

i=0
while test $i -lt $# ; do
    ARG="${argv[$i]}"
    case "${ARG}" in
        -h|--help) 
            usage
            exit;;
        -v|--verbose)
            verbose=1;;
        -d|--dylib) 
            i=$((i + 1)) 
            dylib=${argv[$i]};;
        -p|--prefix) 
            i=$((i + 1)) 
            prefix=${argv[$i]};;
        *) 
            echo "Unknown argument '${ARG}'"
            exit 1;;
    esac
    i=$((i + 1))
done

# test arguments
if [ -z "${dylib}" ]; then
    usage
    exit 1
fi

if [ ! -f "${dylib}" ]; then
    echo "Dylib path '${dylib}' is not a file."
    exit 1
fi

function deploy_plugin() {
    local deploy_path="${1}"
    local prefix_path="${2}"
    local dependency_paths=$(otool -L "${deploy_path}" | tail -n+2 | awk '{print $1}')
    local dependency_path
    echo "Deploy plugin '${deploy_path}'"
    for dependency_path in ${dependency_paths[@]}; do
        if [[ "$dependency_path" == "$prefix"* ]]; then
            local depedency_name=$(basename "${dependency_path}")
            echo "- Changing name of dependency: $dependency_path"
            install_name_tool -change "${dependency_path}" "@rpath/${depedency_name}" "${deploy_path}"
        fi
    done
}

# main
function main {
    deploy_plugin "${dylib}" "${prefix}"
}

main
