# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2024 - present Mikael Sundell
# https://github.com/mikaelsundell/stageviz

cmake_minimum_required(VERSION 3.14)

set(OpenUSD_COMPONENTS
  AR
  ARCH
  BOOST
  PYTHON
  CAMERAUTIL
  GF
  GLF
  HD
  HDX
  HGIGL
  PLUG
  SDF
  TF
  USD
  USDGEOM
  USDIMAGING
  USDIMAGINGGL
  VT
  WORK
)

find_path(OpenUSD_INCLUDE_DIR
  NAMES pxr/usd/usd/api.h
  PATH_SUFFIXES include
  PATHS ${CMAKE_PREFIX_PATH}
)

set(OpenUSD_LIBRARIES "")
set(OpenUSD_ALL_FOUND TRUE)
set(OpenUSD_LIBRARY_DIRS "")

foreach(component ${OpenUSD_COMPONENTS})
  string(TOLOWER ${component} component_name)

  find_library(OpenUSD_${component}_LIBRARY
    NAMES usd_${component_name}
    PATH_SUFFIXES lib
    PATHS ${CMAKE_PREFIX_PATH}
  )

  if (NOT OpenUSD_${component}_LIBRARY)
    message(WARNING "OpenUSD: Missing library usd_${component_name}")
    set(OpenUSD_ALL_FOUND FALSE)
  else()
    list(APPEND OpenUSD_LIBRARIES ${OpenUSD_${component}_LIBRARY})
    get_filename_component(_openusd_lib_dir "${OpenUSD_${component}_LIBRARY}" DIRECTORY)
    list(APPEND OpenUSD_LIBRARY_DIRS "${_openusd_lib_dir}")
  endif()
endforeach()

list(REMOVE_DUPLICATES OpenUSD_LIBRARY_DIRS)

set(_openusd_python_hints)

foreach(_lib_dir ${OpenUSD_LIBRARY_DIRS})
  list(APPEND _openusd_python_hints
    "${_lib_dir}/python"
    "${_lib_dir}/../lib/python"
    "${_lib_dir}/../python"
  )
endforeach()

foreach(_prefix ${CMAKE_PREFIX_PATH})
  list(APPEND _openusd_python_hints
    "${_prefix}/lib/python"
    "${_prefix}/python"
  )
endforeach()

find_path(OpenUSD_PYTHON_DIR
  NAMES pxr/__init__.py
  PATHS ${_openusd_python_hints}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenUSD
  REQUIRED_VARS
    OpenUSD_INCLUDE_DIR
    OpenUSD_LIBRARIES
    OpenUSD_PYTHON_DIR
)

if (OpenUSD_FOUND AND OpenUSD_ALL_FOUND)
  set(OpenUSD_INCLUDE_DIRS ${OpenUSD_INCLUDE_DIR})

  add_library(OpenUSD::OpenUSD INTERFACE IMPORTED GLOBAL)
  set_target_properties(OpenUSD::OpenUSD PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${OpenUSD_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${OpenUSD_LIBRARIES}"
  )

  message(STATUS "OpenUSD: Found Python package dir: ${OpenUSD_PYTHON_DIR}")
else()
  message(FATAL_ERROR
    "OpenUSD: Required components missing. "
    "Ensure headers, libraries, and Python package directory are installed."
  )
endif()