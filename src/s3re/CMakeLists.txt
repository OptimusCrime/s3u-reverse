add_library(s3re SHARED
    dllmain.cpp
    crash_log.cpp
    debug_log.cpp
    log.cpp
    hooks.cpp
    s3re.def

    test/difftest.cpp
    test/harness_selftest.cpp

    msvc6/string.cpp

    trace/determinism.cpp

    replay/command_log.cpp

    game/building_flags.cpp
    game/counting.cpp
    game/id_allocators.cpp
    game/logic_thread.cpp
    game/map.cpp
    game/map_areas.cpp
    game/map_directions.cpp
    game/map_marks.cpp
    game/map_neighbourhood.cpp
    game/net_thread.cpp
    game/players.cpp
    game/random.cpp
    game/settlers.cpp
    game/statistics.cpp
    game/ui_state.cpp
    game/world_search.cpp
    game/winmain.cpp
)

target_include_directories(s3re PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(s3re PRIVATE detours)
target_compile_definitions(s3re PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(s3re PRIVATE /W4 /utf-8)
# Symbol map next to the DLL (s3re.map), for the emulator tests in
# tools/emutest, which call our functions by name.
target_link_options(s3re PRIVATE /MAP)
