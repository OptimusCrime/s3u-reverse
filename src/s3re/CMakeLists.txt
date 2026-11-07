add_library(s3re SHARED
    dllmain.cpp
    log.cpp
    hooks.cpp
    s3re.def

    test/difftest.cpp
    test/harness_selftest.cpp

    msvc6/string.cpp

    game/winmain.cpp
)

target_include_directories(s3re PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(s3re PRIVATE detours)
target_compile_definitions(s3re PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(s3re PRIVATE /W4 /utf-8)
