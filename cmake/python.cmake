# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2024 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

include_guard(GLOBAL)

function(find_python_runtime out_target)
    if(NOT WIN32)
        message(FATAL_ERROR "find_python_runtime() is intended for WIN32 only")
    endif()

    if(NOT DEFINED Python3_VERSION_MAJOR OR NOT DEFINED Python3_VERSION_MINOR)
        message(FATAL_ERROR "Python3 must be found before calling find_python_runtime()")
    endif()

    if(NOT Python3_INCLUDE_DIRS)
        message(FATAL_ERROR "Python3_INCLUDE_DIRS is empty")
    endif()

    if(NOT Python3_LIBRARY_DIRS)
        message(FATAL_ERROR "Python3_LIBRARY_DIRS is empty")
    endif()

    list(GET Python3_LIBRARY_DIRS 0 python_lib_dir)

    set(python_version_tag "${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
    set(python_release_lib "${python_lib_dir}/python${python_version_tag}.lib")
    set(python_debug_lib "${python_lib_dir}/python${python_version_tag}_d.lib")

    if(NOT EXISTS "${python_release_lib}")
        message(FATAL_ERROR "Python release import lib not found: ${python_release_lib}")
    endif()

    if(NOT EXISTS "${python_debug_lib}")
        message(FATAL_ERROR "Python debug import lib not found: ${python_debug_lib}")
    endif()

    set(target_name "${out_target}")

    if(TARGET "${target_name}")
        message(FATAL_ERROR "Target already exists: ${target_name}")
    endif()

    add_library(${target_name} UNKNOWN IMPORTED)

    set_target_properties(${target_name} PROPERTIES
        IMPORTED_CONFIGURATIONS "Debug;Release;RelWithDebInfo;MinSizeRel"
        IMPORTED_LOCATION_DEBUG "${python_debug_lib}"
        IMPORTED_LOCATION_RELEASE "${python_release_lib}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${python_release_lib}"
        IMPORTED_LOCATION_MINSIZEREL "${python_release_lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${Python3_INCLUDE_DIRS}"
    )

    set(PYTHON_RUNTIME_INCLUDE_DIRS "${Python3_INCLUDE_DIRS}" PARENT_SCOPE)
    set(PYTHON_RUNTIME_LIBRARY_DIRS "${Python3_LIBRARY_DIRS}" PARENT_SCOPE)
    set(PYTHON_RUNTIME_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)

    message(STATUS "Python3_EXECUTABLE: ${Python3_EXECUTABLE}")
    message(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")
    message(STATUS "Python3_LIBRARY_DIRS: ${Python3_LIBRARY_DIRS}")
    message(STATUS "python debug lib: ${python_debug_lib}")
    message(STATUS "python release lib: ${python_release_lib}")
endfunction()