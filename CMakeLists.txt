cmake_minimum_required(VERSION 3.25)

project(s3re LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "s3re must be built as 32-bit (x86) to load into S3.EXE. "
        "Use the 'x86' configure preset: cmake --preset x86")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C++ runtime statically so the DLL injected into the game does not
# depend on any runtime DLLs being installed on the system.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Launcher and DLL end up side by side in build/<preset>/bin/<Config>/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(S3RE_GAME_DIR "${CMAKE_SOURCE_DIR}/Settlers3Ultimate" CACHE PATH
    "Default location of the original game installation (contains S3.EXE)")

include(cmake/Detours.cmake)

add_subdirectory(src/launcher)
add_subdirectory(src/s3re)
