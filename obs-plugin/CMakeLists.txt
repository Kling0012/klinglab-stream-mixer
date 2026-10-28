# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
project(klinglab-stream-mixer-source VERSION 0.1.0 LANGUAGES C)
if(NOT APPLE)
    message(FATAL_ERROR "This bundle targets macOS")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(OBS_SOURCE_DIR "" CACHE PATH "Matching official OBS source tree")
set(OBS_LIBRARY "" CACHE FILEPATH "Universal libobs from the matching official OBS bundles")
if(NOT EXISTS "${OBS_SOURCE_DIR}/libobs/obs-module.h" OR NOT EXISTS "${OBS_LIBRARY}")
    message(FATAL_ERROR "Set OBS_SOURCE_DIR and OBS_LIBRARY, or use scripts/obs-build.sh")
endif()
set(OBS_RELEASE_CANDIDATE 0)
set(OBS_BETA 0)
configure_file("${OBS_SOURCE_DIR}/libobs/obsconfig.h.in" generated/obsconfig.h @ONLY)
add_library(libobs SHARED IMPORTED)
set_target_properties(libobs PROPERTIES IMPORTED_LOCATION "${OBS_LIBRARY}")
add_library(klinglab-stream-mixer-source MODULE src/ksm-source.c)
target_include_directories(klinglab-stream-mixer-source PRIVATE include)
target_include_directories(klinglab-stream-mixer-source SYSTEM PRIVATE
    "${OBS_SOURCE_DIR}/libobs" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(klinglab-stream-mixer-source PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(klinglab-stream-mixer-source PRIVATE libobs)
set_target_properties(klinglab-stream-mixer-source PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    PREFIX ""
    OUTPUT_NAME "klinglab-stream-mixer-source"
    C_VISIBILITY_PRESET hidden
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks")
set_source_files_properties(LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(klinglab-stream-mixer-source PRIVATE LICENSE)
add_executable(ksm-shm-test tools/ksm-shm-test.c)
target_include_directories(ksm-shm-test PRIVATE include)
target_compile_options(ksm-shm-test PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS klinglab-stream-mixer-source LIBRARY DESTINATION .)
install(TARGETS ksm-shm-test RUNTIME DESTINATION tools)
