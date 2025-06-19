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

add_subdirectory(SDL)
add_subdirectory(SDL_image)
add_subdirectory(SDL_ttf)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)

find_package(CURL REQUIRED)

target_link_libraries(uikit PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    CURL::libcurl
)

if(WIN32)
    foreach(dep SDL3 SDL3_ttf SDL3_image)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${dep}::${dep}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endforeach()
endif()
