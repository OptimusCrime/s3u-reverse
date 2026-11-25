add_library(s3re SHARED
    # src/s3re/: DLL main, hooks, logs
    dllmain.cpp
    crash_log.cpp
    debug_log.cpp
    hooks.cpp
    log.cpp
    s3re.def

    # test/: the differential test harness
    test/difftest.cpp
    test/harness_selftest.cpp

    # msvc6/: MSVC 6.0 library code (std::string, exceptions)
    msvc6/exceptions.cpp
    msvc6/string.cpp

    # watcom/: Watcom container library code
    watcom/ptr_vector.cpp
    watcom/string_vector.cpp

    # tooling/: launcher features: --fast, --random-map, --load-save
    tooling/fast.cpp
    tooling/random_map_start.cpp
    tooling/savegame_start.cpp

    # tooling/trace/: the determinism trace
    tooling/trace/determinism.cpp

    # tooling/replay/: command recorder and replay
    tooling/replay/command_log.cpp

    # game/world/: CGameData basics: RNG, players, statistics, counters, tables
    game/world/counting.cpp
    game/world/finish_initialising.cpp
    game/world/id_allocators.cpp
    game/world/notifications.cpp
    game/world/player_presence.cpp
    game/world/player_queries.cpp
    game/world/players.cpp
    game/world/random.cpp
    game/world/statistics.cpp
    game/world/world_search.cpp
    game/world/world_tables.cpp

    # game/map/: tiles, directions, areas and regions, territory, fog of war, minimap
    game/map/area_split.cpp
    game/map/flood_fills.cpp
    game/map/fog_of_war.cpp
    game/map/load_map.cpp
    game/map/load_map_tests.cpp
    game/map/map.cpp
    game/map/map_areas.cpp
    game/map/map_directions.cpp
    game/map/map_marks.cpp
    game/map/map_neighbourhood.cpp
    game/map/map_regions.cpp
    game/map/map_terrain.cpp
    game/map/minimap.cpp
    game/map/player_towns.cpp
    game/map/player_towns_tests.cpp
    game/map/terrain_updates.cpp
    game/map/territory.cpp

    # game/mapgen/: the random map generator
    game/mapgen/map_generation.cpp
    game/mapgen/world_create.cpp

    # game/environment/: UpdateEnvironment and the map effects
    game/environment/environment.cpp
    game/environment/map_effect_expiry.cpp
    game/environment/map_effect_queries.cpp
    game/environment/map_effect_spawns.cpp
    game/environment/map_effects.cpp

    # game/economy/: UpdateEconomy, goods piles and their moves
    game/economy/area_piles.cpp
    game/economy/economy.cpp
    game/economy/economy_helpers.cpp
    game/economy/goods_piles.cpp
    game/economy/pile_goods_moves.cpp
    game/economy/update_economy.cpp

    # game/buildings/: building sites, construction, crushing, flags, goods
    game/buildings/building_crush.cpp
    game/buildings/building_done.cpp
    game/buildings/building_flags.cpp
    game/buildings/building_goods.cpp
    game/buildings/building_sites.cpp
    game/buildings/building_tiles.cpp

    # game/settlers/: UpdateSettlers, settler records, walking, soldiers
    game/settlers/miner_jobs.cpp
    game/settlers/settler_idle.cpp
    game/settlers/settler_queries.cpp
    game/settlers/settler_records.cpp
    game/settlers/settler_spawn.cpp
    game/settlers/settler_walk.cpp
    game/settlers/settlers.cpp
    game/settlers/soldier.cpp
    game/settlers/update_settlers.cpp

    # game/settlers/jobs/: the job handlers of UpdateSettlers
    game/settlers/jobs/settler_jobs_building.cpp
    game/settlers/jobs/settler_jobs_drinks.cpp
    game/settlers/jobs/settler_jobs_food.cpp
    game/settlers/jobs/settler_jobs_metal.cpp
    game/settlers/jobs/settler_jobs_special.cpp
    game/settlers/jobs/settler_jobs_units.cpp

    # game/pathing/: the pathfinder (Dirk/Pathing.cpp)
    game/pathing/pathing.cpp

    # game/spells/: the god events (Volker/GodlyEvents.cpp)
    game/spells/godly_events.cpp
    game/spells/spell_effects.cpp
    game/spells/spell_goods.cpp
    game/spells/spell_units.cpp
    game/spells/spells.cpp
    game/spells/spells_land.cpp

    # game/logic/: CLogicThread: Step, game end, victory conditions, UI state in the world
    game/logic/game_end.cpp
    game/logic/game_end_check.cpp
    game/logic/load_save_game.cpp
    game/logic/load_save_game_tests.cpp
    game/logic/logic_helpers.cpp
    game/logic/logic_thread.cpp
    game/logic/logic_thread_members.cpp
    game/logic/objectives.cpp
    game/logic/ui_overlays.cpp
    game/logic/ui_state.cpp
    game/logic/victory_conditions.cpp

    # game/logic/commands/: ExecuteCommands and its parts
    game/logic/commands/command_helpers.cpp
    game/logic/commands/execute_commands.cpp
    game/logic/commands/execute_commands_areas.cpp
    game/logic/commands/execute_commands_net.cpp
    game/logic/commands/execute_commands_trade.cpp
    game/logic/commands/execute_commands_units.cpp

    # game/ai/: computer players (Rainer/*.cpp)
    game/ai/ai_agents.cpp
    game/ai/ai_army.cpp
    game/ai/ai_army_attack.cpp
    game/ai/ai_army_groups.cpp
    game/ai/ai_army_orders.cpp
    game/ai/ai_battle_spells.cpp
    game/ai/ai_building_orders.cpp
    game/ai/ai_building_queries.cpp
    game/ai/ai_cell_resources.cpp
    game/ai/ai_census.cpp
    game/ai/ai_census_settlers.cpp
    game/ai/ai_commands.cpp
    game/ai/ai_computer_player.cpp
    game/ai/ai_economy_actions.cpp
    game/ai/ai_economy_conditions.cpp
    game/ai/ai_economy_snapshot.cpp
    game/ai/ai_enemy_search.cpp
    game/ai/ai_evaland.cpp
    game/ai/ai_evaland_cells.cpp
    game/ai/ai_ground_request.cpp
    game/ai/ai_helpers.cpp
    game/ai/ai_land_queries.cpp
    game/ai/ai_map_queries.cpp
    game/ai/ai_player_nodes.cpp
    game/ai/ai_request_notes.cpp
    game/ai/ai_safety_agent.cpp
    game/ai/ai_site_notes.cpp
    game/ai/ai_site_queries.cpp
    game/ai/ai_small_helpers.cpp
    game/ai/ai_state.cpp
    game/ai/ai_wrapped_pos.cpp

    # game/fileformats/: the map and savegame container (Dirk::CMapFile)
    game/fileformats/map_file.cpp
    game/fileformats/map_file_io.cpp
    game/fileformats/map_file_io_tests.cpp
    game/fileformats/map_file_stream.cpp
    game/fileformats/map_file_stream_tests.cpp
    game/fileformats/map_file_tests.cpp

    # game/ui/: CScreenElement and the controls, menu and settings layouts
    game/ui/game_settings.cpp
    game/ui/screen_element.cpp
    game/ui/ui_static_text.cpp

    # game/graphics/: graphics data, landscape colours, DirectDraw surface
    game/graphics/directx_surface.cpp
    game/graphics/drawer_queues.cpp
    game/graphics/graphics_helpers.cpp
    game/graphics/landscape_colours.cpp
    game/graphics/screen_layout.cpp

    # game/net/: CNetThread, lobby protocol buffer
    game/net/lobby_memory_block.cpp
    game/net/net_thread.cpp

    # game/framework/: WinMain, Win32/DirectX framework classes, exceptions, strings
    game/framework/folded_getters.cpp
    game/framework/framework_helpers.cpp
    game/framework/framework_small.cpp
    game/framework/misc_helpers.cpp
    game/framework/string_members.cpp
    game/framework/win32_classes.cpp
    game/framework/win32_errors.cpp
    game/framework/win32_exceptions.cpp
    game/framework/winmain.cpp
)

target_include_directories(s3re PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(s3re PRIVATE detours)
target_compile_definitions(s3re PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(s3re PRIVATE /W4 /utf-8)
# Symbol map next to the DLL (s3re.map), for the emulator tests in
# tools/emutest, which call our functions by name.
target_link_options(s3re PRIVATE /MAP)
# Optimized builds keep every function under its own name for the emulator
# (run.py --build Release finds our replacements by name in s3re.map):
# identical-code folding (/OPT:ICF, the linker's default with
# optimizations) would merge identical functions and keep one name, and
# /OPT:REF would drop a replacement whose only use (the counting thunk of
# its hook) inlined it.
target_link_options(s3re PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:NOICF>
                    $<$<NOT:$<CONFIG:Debug>>:/OPT:NOREF>)
