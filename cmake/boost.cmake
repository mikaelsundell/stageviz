# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2024 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

include_guard(GLOBAL)

set(BOOST_VERSION "" CACHE STRING
    "Force a Boost version tag like 1_89. Empty means auto-detect newest under CMAKE_PREFIX_PATH."
)

function(find_boost_python out_target)
    if(NOT DEFINED Python3_VERSION_MAJOR OR NOT DEFINED Python3_VERSION_MINOR)
        message(FATAL_ERROR "Python3 must be found before calling find_boost_python()")
    endif()

    set(boost_prefix "")
    set(boost_include_dir "")
    set(boost_library_dir "")
    set(boost_version_tag "")

    foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
        if(NOT IS_DIRECTORY "${prefix}")
            continue()
        endif()

        if(BOOST_VERSION)
            set(candidate_include_dir "${prefix}/include/boost-${BOOST_VERSION}")
            if(EXISTS "${candidate_include_dir}/boost/python.hpp" AND EXISTS "${prefix}/lib")
                set(boost_prefix "${prefix}")
                set(boost_include_dir "${candidate_include_dir}")
                set(boost_library_dir "${prefix}/lib")
                set(boost_version_tag "${BOOST_VERSION}")
                break()
            endif()
        else()
            file(GLOB boost_include_candidates LIST_DIRECTORIES true
                "${prefix}/include/boost-*"
            )

            if(boost_include_candidates)
                list(SORT boost_include_candidates COMPARE NATURAL ORDER DESCENDING)

                foreach(candidate_include_dir IN LISTS boost_include_candidates)
                    if(EXISTS "${candidate_include_dir}/boost/python.hpp" AND EXISTS "${prefix}/lib")
                        get_filename_component(candidate_name "${candidate_include_dir}" NAME)
                        string(REGEX REPLACE "^boost-" "" candidate_version_tag "${candidate_name}")

                        set(boost_prefix "${prefix}")
                        set(boost_include_dir "${candidate_include_dir}")
                        set(boost_library_dir "${prefix}/lib")
                        set(boost_version_tag "${candidate_version_tag}")
                        break()
                    endif()
                endforeach()

                if(boost_prefix)
                    break()
                endif()
            endif()
        endif()
    endforeach()

    if(NOT boost_prefix)
        message(FATAL_ERROR
            "Failed to locate Boost under CMAKE_PREFIX_PATH.\n"
            "Checked prefixes:\n  ${CMAKE_PREFIX_PATH}\n"
            "Expected layout like <prefix>/include/boost-<version>/boost/python.hpp and <prefix>/lib"
        )
    endif()

    set(boost_python_version_tag "${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")

    set(boost_arch_tag "")
    if(WIN32)
        if(CMAKE_VS_PLATFORM_NAME)
            string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" boost_arch_tag)
        elseif(CMAKE_GENERATOR_PLATFORM)
            string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" boost_arch_tag)
        else()
            set(boost_arch_tag "x64")
        endif()
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64|aarch64")
            set(boost_arch_tag "a64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(boost_arch_tag "a64")
        else()
            set(boost_arch_tag "x64")
        endif()
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(boost_arch_tag "x64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(boost_arch_tag "a64")
        endif()
    endif()

    set(boost_python_lib_debug "")
    set(boost_python_lib_release "")

    if(WIN32)
        file(GLOB boost_python_debug_candidates
            "${boost_library_dir}/boost_python${boost_python_version_tag}-*-gyd-${boost_arch_tag}-${boost_version_tag}.lib"
            "${boost_library_dir}/boost_python${boost_python_version_tag}-*-gd-${boost_arch_tag}-${boost_version_tag}.lib"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}-*-gyd-${boost_arch_tag}-${boost_version_tag}.lib"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}-*-gd-${boost_arch_tag}-${boost_version_tag}.lib"
        )

        file(GLOB boost_python_release_candidates
            "${boost_library_dir}/boost_python${boost_python_version_tag}-*-${boost_arch_tag}-${boost_version_tag}.lib"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}-*-${boost_arch_tag}-${boost_version_tag}.lib"
        )

        list(FILTER boost_python_release_candidates EXCLUDE REGEX "-gd-")
        list(FILTER boost_python_release_candidates EXCLUDE REGEX "-gyd-")
    else()
        file(GLOB boost_python_debug_candidates
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*-mt-d-${boost_arch_tag}.dylib"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*-mt-d-${boost_arch_tag}.a"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*-mt-d-${boost_arch_tag}.so"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*-d*.dylib"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*-d*.a"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*-d*.so"
        )

        file(GLOB boost_python_release_candidates
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*.dylib"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*.a"
            "${boost_library_dir}/libboost_python${boost_python_version_tag}*.so"
        )

        list(FILTER boost_python_release_candidates EXCLUDE REGEX "-d")
    endif()

    list(SORT boost_python_debug_candidates COMPARE NATURAL ORDER ASCENDING)
    list(SORT boost_python_release_candidates COMPARE NATURAL ORDER ASCENDING)

    if(boost_python_debug_candidates)
        list(GET boost_python_debug_candidates 0 boost_python_lib_debug)
    endif()

    if(boost_python_release_candidates)
        list(GET boost_python_release_candidates 0 boost_python_lib_release)
    endif()

    set(config_name "")
    if(CMAKE_CONFIGURATION_TYPES)
        if(CMAKE_BUILD_TYPE)
            set(config_name "${CMAKE_BUILD_TYPE}")
        else()
            get_filename_component(config_name "${boost_prefix}" NAME)
        endif()
    else()
        set(config_name "${CMAKE_BUILD_TYPE}")
    endif()

    string(TOLOWER "${config_name}" config_name_lower)

    set(require_debug_lib OFF)
    if(config_name_lower STREQUAL "debug")
        set(require_debug_lib ON)
    endif()

    if(require_debug_lib)
        if(NOT boost_python_lib_debug)
            message(FATAL_ERROR
                "Boost.Python debug library not found.\n"
                "Prefix: ${boost_prefix}\n"
                "Include: ${boost_include_dir}\n"
                "Lib dir: ${boost_library_dir}\n"
                "Python tag: ${boost_python_version_tag}\n"
                "Arch tag: ${boost_arch_tag}\n"
                "Boost version: ${boost_version_tag}"
            )
        endif()
    else()
        if(NOT boost_python_lib_release)
            message(FATAL_ERROR
                "Boost.Python release library not found.\n"
                "Prefix: ${boost_prefix}\n"
                "Include: ${boost_include_dir}\n"
                "Lib dir: ${boost_library_dir}\n"
                "Python tag: ${boost_python_version_tag}\n"
                "Arch tag: ${boost_arch_tag}\n"
                "Boost version: ${boost_version_tag}"
            )
        endif()
    endif()

    set(target_name "${out_target}")

    if(TARGET "${target_name}")
        message(FATAL_ERROR "Target already exists: ${target_name}")
    endif()

    add_library(${target_name} UNKNOWN IMPORTED)

    set_target_properties(${target_name} PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
        INTERFACE_INCLUDE_DIRECTORIES "${boost_include_dir}"
    )

    if(boost_python_lib_debug)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION_DEBUG "${boost_python_lib_debug}"
        )
    elseif(boost_python_lib_release)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION_DEBUG "${boost_python_lib_release}"
        )
    endif()

    if(boost_python_lib_release)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION_RELEASE "${boost_python_lib_release}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${boost_python_lib_release}"
            IMPORTED_LOCATION_MINSIZEREL "${boost_python_lib_release}"
        )
    elseif(boost_python_lib_debug)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_LOCATION_RELEASE "${boost_python_lib_debug}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${boost_python_lib_debug}"
            IMPORTED_LOCATION_MINSIZEREL "${boost_python_lib_debug}"
        )
    endif()

    message(STATUS "Boost prefix: ${boost_prefix}")
    message(STATUS "Boost include dir: ${boost_include_dir}")
    message(STATUS "Boost lib dir: ${boost_library_dir}")
    message(STATUS "Boost version tag: ${boost_version_tag}")
    message(STATUS "Boost arch tag: ${boost_arch_tag}")

    if(boost_python_lib_debug)
        message(STATUS "Boost.Python debug lib: ${boost_python_lib_debug}")
    else()
        message(STATUS "Boost.Python debug lib: <not found>")
    endif()

    if(boost_python_lib_release)
        message(STATUS "Boost.Python release lib: ${boost_python_lib_release}")
    else()
        message(STATUS "Boost.Python release lib: <not found>")
    endif()
endfunction()