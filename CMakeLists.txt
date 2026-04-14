# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2024 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

cmake_minimum_required (VERSION 3.27)
set (project_name "stageviz")

project (${project_name})

# packages
set (qt6_modules Core Concurrent Gui OpenGLWidgets Widgets)
find_package (Qt6 COMPONENTS ${qt6_modules} CONFIG REQUIRED)
set (CMAKE_AUTOMOC ON)
set (CMAKE_AUTORCC ON)
set (CMAKE_AUTOUIC ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package (OpenUSD REQUIRED)
find_package (Boost CONFIG COMPONENTS Python REQUIRED)
find_package (Python3 COMPONENTS Interpreter Development REQUIRED)
find_package (pybind11 REQUIRED)

if (WIN32)
    find_package (OpenGL REQUIRED)
endif ()

file (GLOB project_sources
    "sources/*.h"
    "sources/*.cpp"
    "sources/*.qrc"
    "sources/*.ui"
)
list (FILTER project_sources EXCLUDE REGEX "_")

file (GLOB project_resources
    "resources/*.qss"
)

# build
string (TIMESTAMP project_build_date "%y%m%d")
if (CMAKE_CONFIGURATION_TYPES)
    set(project_build_config "$<CONFIG>")
else ()
    set(project_build_config "${CMAKE_BUILD_TYPE}")
endif ()

# source groups
source_group ("Icon Files" FILES ${project_icons})
source_group ("Resource Files" FILES ${project_resources})

set (project_copyright "Copyright 2025-present Contributors to the ${project_name} project")
set (project_identifier "com.github.mikaelsundell.${project_name}")
set (project_long_version "1.3.4")
set (project_short_version "1.3")
set (project_url "https://github.com/mikaelsundell/stageviz")

if (APPLE)
    set (project_bundle
        "${CMAKE_SOURCE_DIR}/resources/MacOSXBundle.plist.in"
    )
    list (APPEND project_resources "resources/AppIcon.icns")
    set (MACOSX_BUNDLE_EXECUTABLE_NAME ${project_name})
    set (MACOSX_BUNDLE_INFO_STRING ${project_name})
    set (MACOSX_BUNDLE_BUNDLE_NAME ${project_name})
    set (MACOSX_BUNDLE_GUI_IDENTIFIER ${project_identifier})
    set (MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set (MACOSX_BUNDLE_LONG_VERSION_STRING ${project_short_version})
    set (MACOSX_BUNDLE_SHORT_VERSION_STRING ${project_short_version})
    set (MACOSX_BUNDLE_BUNDLE_VERSION ${project_long_version})
    set (MACOSX_BUNDLE_COPYRIGHT ${project_copyright})
    set (MACOSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
    set_source_files_properties(${project_resources} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list (APPEND project_sources "sources/os_mac.mm")
    add_executable (${project_name} MACOSX_BUNDLE ${project_sources} ${project_resources})
    set_target_properties (${project_name} PROPERTIES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${project_identifier}"
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${MACOSX_DEPLOYMENT_TARGET}"
        MACOSX_BUNDLE_INFO_PLIST "${project_bundle}"
    )
    set_target_properties (${project_name} PROPERTIES
        OUTPUT_NAME ${project_name}
    )
    target_compile_definitions (${project_name} PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_definitions (${project_name} PRIVATE PXR_PYTHON_DIR="${OpenUSD_PYTHON_DIR}")
    target_link_libraries (${project_name} 
        Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::OpenGLWidgets Qt6::Widgets
        Boost::python
        OpenUSD::OpenUSD
        Python3::Python
        ${LCMS2_LIBRARY}
        "-framework CoreFoundation"
        "-framework AppKit")

elseif (WIN32)
    list (APPEND project_sources "sources/os_win.cpp")
    add_executable (${project_name} WIN32 ${project_sources} ${project_resources})
    target_compile_definitions (${project_name} PRIVATE 
        WIN32
        WINVER=0x0A00
        $<$<CONFIG:Debug>:TBB_USE_DEBUG=1>
    )
    target_compile_definitions (${project_name} PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_definitions (${project_name} PRIVATE PXR_PYTHON_DIR="${OpenUSD_PYTHON_DIR}")
    target_link_libraries (${project_name} 
        Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::OpenGLWidgets Qt6::Widgets
        Boost::python
        OpenGL::GL
        OpenUSD::OpenUSD
        Python3::Python
        ${LCMS2_LIBRARY}
        "User32.lib"
        "Gdi32.lib"
        "Shell32.lib"
    )
    set (project_icon "${CMAKE_SOURCE_DIR}/resources/resources.rc")
    target_sources (${project_name} PRIVATE ${project_icon})

    set (project_debug ${CMAKE_BINARY_DIR}/Debug)
    set (project_release ${CMAKE_BINARY_DIR}/Release)
    foreach(resource_file ${project_resources})
        get_filename_component(resource_name ${resource_file} NAME)
        add_custom_command(TARGET ${project_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<$<CONFIG:Debug>:${project_release}/resources>
                $<$<CONFIG:Release>:${project_release}/resources>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${resource_file}
                $<$<CONFIG:Debug>:${project_debug}/resources/${resource_name}>
                $<$<CONFIG:Release>:${project_release}/resources/${resource_name}>
        )
    endforeach ()
else ()
    message (WARNING "Unknown platform. ${project_name} may not be built correctly.")
endif ()

target_compile_definitions (${project_name} PRIVATE
    -DPROJECT_NAME="${project_name}"
    -DPROJECT_VERSION="${project_long_version}"
    -DPROJECT_COPYRIGHT="${project_copyright}"
    -DPROJECT_IDENTIFIER="${project_identifier}"
    -DPROJECT_CONFIG="${project_build_config}"
    -DPROJECT_DATE="${project_build_date}"
    -DGITHUB_URL="${project_url}"
    -DQT_NO_KEYWORDS # tbb compatibility
)

target_include_directories (${project_name} PRIVATE ${CMAKE_SOURCE_DIR})
