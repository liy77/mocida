# Standalone Mocida example: built on its own, NOT with the library.
#
#   python build.py --example hello              # desktop
#   python build.py --ios --example hello        # iOS (.app/.ipa)
#   python build.py --ios --simulator --example hello
#
# This project pulls in the mocida library (library target only — no demo,
# tests or other examples) via add_subdirectory, then builds the app with
# the same mocida_add_executable helper the library uses (so iOS bundling,
# frameworks and the app icon all work). The app name / id / bundled assets
# come from app.bundle next to this file.
cmake_minimum_required(VERSION 3.11)

# Build ONLY the library out of the mocida tree.
set(MOCIDA_BUILD_DEMO     OFF CACHE BOOL "" FORCE)
set(MOCIDA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MOCIDA_BUILD_INSTALLER OFF CACHE BOOL "" FORCE)

# Host project (languages come from the mocida tree's own project() call).
project(mocida_example_hello C)

get_filename_component(MOCIDA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${MOCIDA_ROOT}" "${CMAKE_BINARY_DIR}/mocida")

mocida_add_executable(hello "${CMAKE_CURRENT_SOURCE_DIR}/main.c")
