# Microsoft Detours (MIT license): process creation with DLL injection and
# function hooking with trampolines. Detours ships an nmake build, so we
# compile its sources directly into a static library.

include(FetchContent)

FetchContent_Declare(detours
    GIT_REPOSITORY https://github.com/microsoft/Detours.git
    GIT_TAG        1dbd4ea281d73d478aa8d09a8b83fbd265f715f7
)
FetchContent_MakeAvailable(detours)

set(_detours_src ${detours_SOURCE_DIR}/src)

add_library(detours STATIC
    ${_detours_src}/detours.cpp
    ${_detours_src}/modules.cpp
    ${_detours_src}/disasm.cpp
    ${_detours_src}/image.cpp
    ${_detours_src}/creatwth.cpp
    ${_detours_src}/disolx86.cpp
    ${_detours_src}/disolx64.cpp
    ${_detours_src}/disolia64.cpp
    ${_detours_src}/disolarm.cpp
    ${_detours_src}/disolarm64.cpp
)
target_include_directories(detours SYSTEM PUBLIC ${_detours_src})
target_compile_options(detours PRIVATE /W0)
