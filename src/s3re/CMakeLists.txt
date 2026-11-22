add_library(s3re SHARED
    dllmain.cpp
    crash_log.cpp
    debug_log.cpp
    log.cpp
    hooks.cpp
    s3re.def

    test/difftest.cpp
    test/harness_selftest.cpp

    msvc6/exceptions.cpp
    msvc6/string.cpp

    watcom/ptr_vector.cpp
    watcom/string_vector.cpp

    trace/determinism.cpp

    replay/command_log.cpp

    tooling/random_map_start.cpp
    tooling/fast.cpp

    game/ai_evaland.cpp
    game/ai_helpers.cpp
    game/ai_small_helpers.cpp
    game/area_piles.cpp
    game/area_split.cpp
    game/building_crush.cpp
    game/building_done.cpp
    game/building_flags.cpp
    game/building_sites.cpp
    game/building_goods.cpp
    game/building_tiles.cpp
    game/counting.cpp
    game/directx_surface.cpp
    game/economy.cpp
    game/economy_helpers.cpp
    game/flood_fills.cpp
    game/folded_getters.cpp
    game/framework_helpers.cpp
    game/framework_small.cpp
    game/game_end.cpp
    game/godly_events.cpp
    game/graphics_helpers.cpp
    game/goods_piles.cpp
    game/id_allocators.cpp
    game/landscape_colours.cpp
    game/lobby_memory_block.cpp
    game/logic_helpers.cpp
    game/logic_thread.cpp
    game/map.cpp
    game/map_areas.cpp
    game/map_directions.cpp
    game/map_effect_spawns.cpp
    game/map_effects.cpp
    game/map_generation.cpp
    game/map_marks.cpp
    game/fog_of_war.cpp
    game/map_effect_expiry.cpp
    game/environment.cpp
    game/spell_effects.cpp
    game/map_regions.cpp
    game/map_neighbourhood.cpp
    game/map_terrain.cpp
    game/miner_jobs.cpp
    game/misc_helpers.cpp
    game/net_thread.cpp
    game/notifications.cpp
    game/objectives.cpp
    game/pile_goods_moves.cpp
    game/player_presence.cpp
    game/player_queries.cpp
    game/players.cpp
    game/random.cpp
    game/savegame_start.cpp
    game/screen_element.cpp
    game/settler_idle.cpp
    game/settler_queries.cpp
    game/settler_records.cpp
    game/settlers.cpp
    game/settler_spawn.cpp
    game/string_members.cpp
    game/territory.cpp
    game/terrain_updates.cpp
    game/pathing.cpp
    game/settler_walk.cpp
    game/statistics.cpp
    game/ui_overlays.cpp
    game/ui_static_text.cpp
    game/ui_state.cpp
    game/world_create.cpp
    game/world_search.cpp
    game/win32_classes.cpp
    game/win32_errors.cpp
    game/win32_exceptions.cpp
    game/winmain.cpp
    game/world_tables.cpp
)

target_include_directories(s3re PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(s3re PRIVATE detours)
target_compile_definitions(s3re PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(s3re PRIVATE /W4 /utf-8)
# Symbol map next to the DLL (s3re.map), for the emulator tests in
# tools/emutest, which call our functions by name.
target_link_options(s3re PRIVATE /MAP)
