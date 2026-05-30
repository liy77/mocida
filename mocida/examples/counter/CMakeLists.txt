# Standalone Mocida example — see examples/hello/CMakeLists.txt for notes.
#   python build.py --example counter   (add --ios / --simulator for iOS)
cmake_minimum_required(VERSION 3.11)

set(MOCIDA_BUILD_DEMO      OFF CACHE BOOL "" FORCE)
set(MOCIDA_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(MOCIDA_BUILD_INSTALLER OFF CACHE BOOL "" FORCE)

project(mocida_example_counter C)

get_filename_component(MOCIDA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${MOCIDA_ROOT}" "${CMAKE_BINARY_DIR}/mocida")

mocida_add_executable(counter "${CMAKE_CURRENT_SOURCE_DIR}/main.c")
