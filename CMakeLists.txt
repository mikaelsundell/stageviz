# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2024 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

cmake_minimum_required(VERSION 3.27)

set(project_name "stageviz")
set(project_output_name "Stageviz")

project(${project_name})

# options
# DEPLOY_BUILD must be set explicitly by the build script for packaged builds.
# Local development builds should keep this OFF and reuse the discovered runtime paths.
option(DEPLOY_BUILD "Use deployed app bundle runtime paths" OFF)

# packages
set(qt6_modules Core Concurrent Gui Network OpenGLWidgets Widgets)
find_package(Qt6 COMPONENTS ${qt6_modules} CONFIG REQUIRED)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenUSD REQUIRED)
find_package(MaterialX CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

if(WIN32)
    find_package(TBB CONFIG REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(python)
include(boost)

if(WIN32)
    set(Python3_FIND_ABI OFF ANY ANY)
    find_package(Python3 COMPONENTS Interpreter Development.Embed REQUIRED)
    find_python_runtime(python)
    find_boost_python(boost_python)
else()
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
endif()

function(find_python_package_root out_python_dir out_package_dir prefix package_name module_glob)
    set(found_python_dir "")
    set(found_package_dir "")

    # preserve the existing mac/Linux search locations and add the Windows
    # layout used by this 3rdparty build, e.g.:
    #   <prefix>/python/MaterialX/PyMaterialXCore*.pyd
    set(candidates
        "${prefix}/${package_name}"
        "${prefix}/python/${package_name}"
        "${prefix}/Python/${package_name}"
        "${prefix}/site-packages/${package_name}"
        "${prefix}/lib/site-packages/${package_name}"
        "${prefix}/Lib/site-packages/${package_name}"
        "${prefix}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/${package_name}"
        "${prefix}/Lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/${package_name}"
        "${prefix}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/${package_name}"
        "${prefix}/Python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/${package_name}"
    )

    message(STATUS "Searching Python runtime for ${package_name} under: ${prefix}")

    foreach(candidate IN LISTS candidates)
        message(STATUS "  checking: ${candidate}")

        file(GLOB found_modules
            "${candidate}/${module_glob}.so"
            "${candidate}/${module_glob}.pyd"
            "${candidate}/${module_glob}.dll"
        )

        if(found_modules)
            list(GET found_modules 0 first_match)
            message(STATUS "  found ${package_name} module: ${first_match}")

            set(found_package_dir "${candidate}")
            get_filename_component(found_python_dir "${candidate}" DIRECTORY)
            break()
        endif()
    endforeach()

    set(${out_python_dir} "${found_python_dir}" PARENT_SCOPE)
    set(${out_package_dir} "${found_package_dir}" PARENT_SCOPE)
endfunction()

function(require_python_package_root python_dir_var package_name prefix)
    if(NOT DEFINED ${python_dir_var} OR "${${python_dir_var}}" STREQUAL "")
        message(FATAL_ERROR "Failed to locate ${package_name} Python runtime under ${prefix}")
    endif()
endfunction()

function(append_existing_paths out_var)
    set(result "${${out_var}}")
    foreach(path IN LISTS ARGN)
        if(EXISTS "${path}")
            list(APPEND result "${path}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES result)
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

get_filename_component(materialx_prefix "${MaterialX_DIR}/../../.." ABSOLUTE)
get_filename_component(openimageio_prefix "${OpenImageIO_DIR}/../../.." ABSOLUTE)
get_filename_component(qt6_prefix "${Qt6_DIR}/../../.." ABSOLUTE)

find_python_package_root(
    MaterialX_PYTHON_DIR
    MaterialX_PACKAGE_DIR
    "${materialx_prefix}"
    "MaterialX"
    "PyMaterialXCore*"
)

find_python_package_root(
    OpenImageIO_PYTHON_DIR
    OpenImageIO_PACKAGE_DIR
    "${openimageio_prefix}"
    "OpenImageIO"
    "*"
)

# avoid find_package(PySide6 CONFIG REQUIRED) here because the current
# PySide6Config may fail on missing development-side typesystem paths even when
# the runtime Python package itself is present.
find_python_package_root(
    PySide6_PYTHON_DIR
    PySide6_PACKAGE_DIR
    "${qt6_prefix}"
    "PySide6"
    "QtCore*"
)

find_python_package_root(
    Shiboken6_PYTHON_DIR
    Shiboken6_PACKAGE_DIR
    "${qt6_prefix}"
    "shiboken6"
    "Shiboken*"
)

if(NOT OpenUSD_PYTHON_DIR)
    message(FATAL_ERROR "OpenUSD_PYTHON_DIR is not set")
endif()

require_python_package_root(MaterialX_PYTHON_DIR "MaterialX" "${materialx_prefix}")
require_python_package_root(OpenImageIO_PYTHON_DIR "OpenImageIO" "${openimageio_prefix}")
require_python_package_root(PySide6_PYTHON_DIR "PySide6" "${qt6_prefix}")
require_python_package_root(Shiboken6_PYTHON_DIR "shiboken6" "${qt6_prefix}")

# python import paths (sys.path)
set(python_module_paths
    "${OpenUSD_PYTHON_DIR}"
    "${MaterialX_PYTHON_DIR}"
    "${OpenImageIO_PYTHON_DIR}"
    "${PySide6_PYTHON_DIR}"
    "${Shiboken6_PYTHON_DIR}"
)
list(REMOVE_DUPLICATES python_module_paths)
list(JOIN python_module_paths ";" PYTHON_MODULE_PATHS_STR)

# native runtime library paths (DLL/.so/.dylib lookup)
set(python_runtime_library_paths)

append_existing_paths(python_runtime_library_paths
    "${materialx_prefix}/bin"
    "${materialx_prefix}/lib"
    "${materialx_prefix}/lib64"
    "${openimageio_prefix}/bin"
    "${openimageio_prefix}/lib"
    "${openimageio_prefix}/lib64"
    "${qt6_prefix}/bin"
    "${qt6_prefix}/lib"
)

list(REMOVE_DUPLICATES python_runtime_library_paths)
list(JOIN python_runtime_library_paths ";" PYTHON_RUNTIME_LIBRARY_PATHS_STR)

message(STATUS "python_module_paths:")
foreach(path IN LISTS python_module_paths)
    message(STATUS "  ${path}")
endforeach()

message(STATUS "python_runtime_library_paths:")
foreach(path IN LISTS python_runtime_library_paths)
    message(STATUS "  ${path}")
endforeach()

configure_file(
    "${CMAKE_SOURCE_DIR}/sources/config.h.in"
    "${CMAKE_BINARY_DIR}/config.h"
    @ONLY
)

if(WIN32)
    find_package(OpenGL REQUIRED)
endif()

file(GLOB project_sources
    "sources/*.h"
    "sources/*.cpp"
    "sources/*.qrc"
    "sources/*.ui"
)
list(FILTER project_sources EXCLUDE REGEX "_")

file(GLOB project_resources
    "resources/*.qss"
)

# build
string(TIMESTAMP project_build_date "%y%m%d")
if(CMAKE_CONFIGURATION_TYPES)
    set(project_build_config "$<CONFIG>")
else()
    set(project_build_config "${CMAKE_BUILD_TYPE}")
endif()

# source groups
source_group("Resource Files" FILES ${project_resources})

set(project_copyright "Copyright 2025-present Contributors to the ${project_name} project")
set(project_identifier "com.github.mikaelsundell.${project_name}")
set(project_long_version "0.2.22")
set(project_short_version "0.2")
set(project_url "https://github.com/mikaelsundell/stageviz")

if(APPLE)
    set(project_bundle
        "${CMAKE_SOURCE_DIR}/resources/MacOSXBundle.plist.in"
    )
    list(APPEND project_resources "resources/AppIcon.icns")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME ${project_name})
    set(MACOSX_BUNDLE_INFO_STRING ${project_output_name})
    set(MACOSX_BUNDLE_BUNDLE_NAME ${project_output_name})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER ${project_identifier})
    set(MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set(MACOSX_BUNDLE_LONG_VERSION_STRING ${project_short_version})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${project_short_version})
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${project_long_version})
    set(MACOSX_BUNDLE_COPYRIGHT ${project_copyright})
    set(MACOSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})

    set_source_files_properties(${project_resources} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND project_sources "sources/os_mac.mm")

    add_executable(${project_name} MACOSX_BUNDLE ${project_sources} ${project_resources})
    set_target_properties(${project_name} PROPERTIES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${project_identifier}"
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${MACOSX_DEPLOYMENT_TARGET}"
        MACOSX_BUNDLE_INFO_PLIST "${project_bundle}"
        OUTPUT_NAME "${project_name}"
    )

    target_compile_definitions(${project_name} PRIVATE
        GL_SILENCE_DEPRECATION
    )

    if(DEPLOY_BUILD)
        target_compile_definitions(${project_name} PRIVATE DEPLOY_BUILD=1)
    else()
        target_compile_definitions(${project_name} PRIVATE DEPLOY_BUILD=0)
    endif()

    find_package(Boost CONFIG REQUIRED COMPONENTS python39)

    target_link_libraries(${project_name}
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        Qt6::OpenGLWidgets
        Qt6::Widgets
        Boost::python39
        OpenUSD::OpenUSD
        Python3::Python
        "-framework CoreFoundation"
        "-framework AppKit"
    )

elseif(WIN32)
    list(APPEND project_sources "sources/os_win.cpp")

    add_executable(${project_name} WIN32 ${project_sources} ${project_resources})
    set_target_properties(${project_name} PROPERTIES
        OUTPUT_NAME "${project_output_name}"
    )

    target_compile_definitions(${project_name} PRIVATE
        WIN32
        WINVER=0x0A00
        GL_SILENCE_DEPRECATION
        $<$<CONFIG:Debug>:TBB_USE_DEBUG=1>
        $<$<CONFIG:Debug>:BOOST_DEBUG_PYTHON=1>
    )

    if(DEPLOY_BUILD)
        target_compile_definitions(${project_name} PRIVATE DEPLOY_BUILD=1)
    else()
        target_compile_definitions(${project_name} PRIVATE DEPLOY_BUILD=0)
    endif()

    target_link_libraries(${project_name}
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        Qt6::OpenGLWidgets
        Qt6::Widgets
        boost_python
        OpenGL::GL
        OpenUSD::OpenUSD
        python
        TBB::tbb
        "User32.lib"
        "Gdi32.lib"
        "Shell32.lib"
    )

    set(project_icon "${CMAKE_SOURCE_DIR}/resources/resources.rc")
    target_sources(${project_name} PRIVATE ${project_icon})

    set(project_debug ${CMAKE_BINARY_DIR}/Debug)
    set(project_release ${CMAKE_BINARY_DIR}/Release)
    foreach(resource_file ${project_resources})
        get_filename_component(resource_name ${resource_file} NAME)
        add_custom_command(TARGET ${project_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<$<CONFIG:Debug>:${project_debug}/resources>
                $<$<CONFIG:Release>:${project_release}/resources>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${resource_file}
                $<$<CONFIG:Debug>:${project_debug}/resources/${resource_name}>
                $<$<CONFIG:Release>:${project_release}/resources/${resource_name}>
        )
    endforeach()
else()
    message(WARNING "Unknown platform. ${project_name} may not be built correctly.")
endif()

target_compile_definitions(${project_name} PRIVATE
    -DPROJECT_NAME="${project_output_name}"
    -DPROJECT_VERSION="${project_long_version}"
    -DPROJECT_COPYRIGHT="${project_copyright}"
    -DPROJECT_IDENTIFIER="${project_identifier}"
    -DPROJECT_CONFIG="${project_build_config}"
    -DPROJECT_DATE="${project_build_date}"
    -DGITHUB_URL="${project_url}"
    -DQT_NO_KEYWORDS
)

target_include_directories(${project_name} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)