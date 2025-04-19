cmake_minimum_required(VERSION 3.11)
project(uikit C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Otimizações para Release (MSVC / Clang / MinGW)
set(CMAKE_C_FLAGS_RELEASE "/O2 /GL /Gw /Gy /GF /DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/headers)

file(GLOB_RECURSE SOURCES_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c")
list(APPEND SOURCES ${SOURCES_RECURSE})
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

add_executable(${PROJECT_NAME} ${SOURCES})

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

include(FetchContent)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.10  
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
  SDL3_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG release-3.2.2 
)
set(SDL3TTF_VENDORED ON CACHE BOOL "Use vendored dependencies for building SDL3_ttf")
set(SDL3TTF_SAMPLES OFF CACHE BOOL "Build the SDL3_ttf sample programs")
FetchContent_MakeAvailable(SDL3_ttf)

target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)

if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying SDL3 and SDL3_ttf DLLs to output directory"
    )
endif()
