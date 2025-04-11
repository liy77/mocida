cmake_minimum_required(VERSION 3.11)
project(uikit C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directory for header files
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/headers)

# Find all source files in the src directory
file(GLOB_RECURSE SOURCES_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c")
list(APPEND SOURCES ${SOURCES_RECURSE})

# Add main.c explicitly
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Optional: Install configuration
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Include FetchContent module
include(FetchContent)

# Declare SDL3
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.10  # Use a versão estável mais recente ou ajuste conforme necessário
)

# Make SDL3 available
FetchContent_MakeAvailable(SDL3)

# Link SDL3 to the executable
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

# Copy SDL3.dll to the output directory on Windows
if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying SDL3.dll to output directory"
    )
endif()