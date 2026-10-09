# Copyright (c) 2026 Teirdalin.
# SPDX-License-Identifier: LicenseRef-JDL-1
# Additional plugin-development permission: PLUGIN_API_PERMISSION.md.
cmake_minimum_required(VERSION 3.24)
project(hello_caine LANGUAGES CXX)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Use MSVC with -A Win32")
endif()
set(CAINE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include" CACHE PATH "Folder containing caine/mod_api.h")
add_library(hello_caine SHARED mod.cpp mod.def)
target_include_directories(hello_caine PRIVATE "${CAINE_SDK_DIR}")
set_target_properties(hello_caine PROPERTIES OUTPUT_NAME mod)
