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
    test/fake_ddraw.cpp
    test/fake_dsound.cpp
    test/fake_gdi.cpp
    test/fake_win32.cpp
    test/harness_selftest.cpp

    # msvc6/: MSVC 6.0 library code (std::string, exceptions)
    msvc6/catch_match.cpp
    msvc6/static_string_inits.cpp
    msvc6/static_stubs.cpp
    msvc6/static_stub_atexit.cpp
    msvc6/static_stub_concat.cpp
    msvc6/static_stub_dtors.cpp
    msvc6/static_stub_entries.cpp
    msvc6/static_stub_floats.cpp
    msvc6/static_stub_guards.cpp
    msvc6/static_stub_strings.cpp
    msvc6/deleting_destructors.cpp
    msvc6/exceptions.cpp
    msvc6/old_iostream.cpp
    msvc6/string.cpp
    msvc6/string_search.cpp
    msvc6/wstring.cpp

    # watcom/: Watcom container library code
    watcom/bare_vectors.cpp
    watcom/element_vectors.cpp
    watcom/slist_destructors.cpp
    watcom/isv_slist.cpp
    watcom/list_iterators.cpp
    watcom/skiplist_base.cpp
    watcom/skiplist_dicts.cpp
    watcom/skiplist_nodes.cpp
    watcom/skiplist_nodes2.cpp
    watcom/sorted_find.cpp
    watcom/ptr_vector.cpp
    watcom/string_vector.cpp
    watcom/ushort_vector.cpp
    watcom/vector_callbacks.cpp
    watcom/vector_init.cpp
    watcom/vector_insert.cpp

    # thirdparty/: libpng 1.0.2, zlib 1.1.3 and PaintLib (the PNG decoder)
    thirdparty/paintlib.cpp
    thirdparty/png_core.cpp
    thirdparty/png_error.cpp
    thirdparty/png_mem.cpp
    thirdparty/png_read_destroy.cpp
    thirdparty/png_rtran_rows.cpp
    thirdparty/png_rutil_chunks.cpp
    thirdparty/png_rutil_chunks2.cpp
    thirdparty/png_read.cpp
    thirdparty/paintlib2.cpp
    thirdparty/png_rtran.cpp
    thirdparty/png_rutil_rows.cpp
    thirdparty/png_set.cpp
    thirdparty/zlib_checksums.cpp
    thirdparty/zlib_inflate.cpp
    thirdparty/zlib_inflate_setup.cpp
    thirdparty/zlib_inflate_tests.cpp

    # tooling/: launcher features: --fast, --random-map, --load-save
    tooling/fast.cpp
    tooling/random_map_start.cpp
    tooling/savegame_start.cpp

    # tooling/trace/: the determinism trace
    tooling/trace/determinism.cpp

    # tooling/replay/: command recorder and replay
    tooling/replay/command_log.cpp

    # game/world/: CGameData basics: RNG, players, statistics, counters, tables
    game/world/game_data_ctor.cpp
    game/world/counting.cpp
    game/world/finish_initialising.cpp
    game/world/id_allocators.cpp
    game/world/notifications.cpp
    game/world/player_presence.cpp
    game/world/player_queries.cpp
    game/world/player_info.cpp
    game/world/player_info_tests.cpp
    game/world/players.cpp
    game/world/random.cpp
    game/world/statistics.cpp
    game/world/world_search.cpp
    game/world/init_after_map_load.cpp
    game/world/offset_table.cpp
    game/world/world_object.cpp
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
    game/pathing/reciprocals.cpp

    # game/spells/: the god events (Volker/GodlyEvents.cpp)
    game/spells/godly_events.cpp
    game/spells/spell_effects.cpp
    game/spells/spell_goods.cpp
    game/spells/spell_units.cpp
    game/spells/spells.cpp
    game/spells/spells_land.cpp

    # game/logic/: CLogicThread: Step, game end, victory conditions, UI state in the world
    game/logic/finish_game.cpp
    game/logic/game_end.cpp
    game/logic/game_end_check.cpp
    game/logic/load_save_game.cpp
    game/logic/load_save_game_tests.cpp
    game/logic/save_game.cpp
    game/logic/save_game_tests.cpp
    game/logic/save_game_to_slot.cpp
    game/logic/draw_data.cpp
    game/logic/draw_data_publish.cpp
    game/logic/logic_helpers.cpp
    game/logic/logic_music.cpp
    game/logic/logic_thread.cpp
    game/logic/logic_thread_dtor.cpp
    game/logic/message_lines.cpp
    game/logic/logic_thread_members.cpp
    game/logic/objectives.cpp
    game/logic/ui_overlays.cpp
    game/logic/ui_state.cpp
    game/logic/victory_conditions.cpp
    game/logic/message_queue.cpp

    # game/logic/commands/: ExecuteCommands and its parts
    game/logic/commands/command_helpers.cpp
    game/logic/commands/execute_commands.cpp
    game/logic/commands/execute_commands_areas.cpp
    game/logic/commands/execute_commands_net.cpp
    game/logic/commands/execute_commands_trade.cpp
    game/logic/commands/execute_commands_units.cpp

    # game/ai/: computer players (Rainer/*.cpp)
    game/ai/ai_agent_factory.cpp
    game/ai/ai_agent_slots.cpp
    game/ai/ai_agents.cpp
    game/ai/ai_area_events.cpp
    game/ai/ai_area_records.cpp
    game/ai/ai_area_site_lists.cpp
    game/ai/ai_army.cpp
    game/ai/ai_army_attack.cpp
    game/ai/ai_army_groups.cpp
    game/ai/ai_army_orders.cpp
    game/ai/ai_battle_spells.cpp
    game/ai/ai_building_finished.cpp
    game/ai/ai_building_orders.cpp
    game/ai/ai_building_queries.cpp
    game/ai/ai_cell_resources.cpp
    game/ai/ai_census.cpp
    game/ai/ai_centrum_check.cpp
    game/ai/ai_census_settlers.cpp
    game/ai/ai_commands.cpp
    game/ai/ai_computer_player.cpp
    game/ai/ai_computer_player_init.cpp
    game/ai/ai_computer_player_step.cpp
    game/ai/ai_economy_actions.cpp
    game/ai/ai_economy_conditions.cpp
    game/ai/ai_economy_consider.cpp
    game/ai/ai_economy_snapshot.cpp
    game/ai/ai_enemy_search.cpp
    game/ai/ai_evaland.cpp
    game/ai/ai_expand_empire_agent.cpp
    game/ai/ai_evaland_cells.cpp
    game/ai/ai_force_production.cpp
    game/ai/ai_ground_request.cpp
    game/ai/ai_helpers.cpp
    game/ai/ai_land_queries.cpp
    game/ai/ai_map_queries.cpp
    game/ai/ai_map_worker_agent.cpp
    game/ai/ai_military.cpp
    game/ai/ai_player_nodes.cpp
    game/ai/ai_request_agents.cpp
    game/ai/ai_request_execute.cpp
    game/ai/ai_request_notes.cpp
    game/ai/ai_safety_agent.cpp
    game/ai/ai_site_tree.cpp
    game/ai/ai_site_notes.cpp
    game/ai/ai_site_queries.cpp
    game/ai/ai_small_helpers.cpp
    game/ai/ai_soldier_shares.cpp
    game/ai/ai_state.cpp
    game/ai/ai_work_areas.cpp
    game/ai/ai_work_area_finders.cpp
    game/ai/ai_world_events.cpp
    game/ai/ai_wrapped_pos.cpp

    # game/fileformats/: the map and savegame container (Dirk::CMapFile)
    game/fileformats/map_file.cpp
    game/fileformats/map_file_io.cpp
    game/fileformats/map_file_io_tests.cpp
    game/fileformats/map_file_stream.cpp
    game/fileformats/map_file_stream_tests.cpp
    game/fileformats/map_file_tests.cpp

    # game/ui/: CScreenElement and the controls, menu and settings layouts
    game/ui/controls.cpp
    game/ui/credits_debug.cpp
    game/ui/game_settings.cpp
    game/ui/game_settings_players.cpp
    game/ui/game_settings_players_tests.cpp
    game/ui/game_settings_load.cpp
    game/ui/game_settings_load_tests.cpp
    game/ui/game_settings_local_player.cpp
    game/ui/game_settings_vectors.cpp
    game/ui/game_teams.cpp
    game/ui/screen_element.cpp
    game/ui/ui_button_scroll.cpp
    game/ui/ui_control_destructors.cpp
    game/ui/ui_control_members.cpp
    game/ui/ui_gdi_members.cpp
    game/ui/ui_group_members.cpp
    game/ui/ui_static_text.cpp
    game/ui/ui_text_getters.cpp
    game/ui/ui_updown.cpp

    # game/graphics/: graphics data, landscape colours, DirectDraw surface
    game/graphics/building_slots.cpp
    game/graphics/draw_thread.cpp
    game/graphics/drawer_display_mode.cpp
    game/graphics/drawer_lifetime.cpp
    game/graphics/draw_thread_steps.cpp
    game/graphics/drawer_surfaces.cpp
    game/graphics/directx_surface.cpp
    game/graphics/drawer_buildings.cpp
    game/graphics/drawer_settlers.cpp
    game/graphics/drawer_landscape.cpp
    game/graphics/drawer_objects.cpp
    game/graphics/drawer_queues.cpp
    game/graphics/gfx_draw.cpp
    game/graphics/gfx_files.cpp
    game/graphics/gfx_images.cpp
    game/graphics/gfx_loader.cpp
    game/graphics/gfx_loader_tests.cpp
    game/graphics/gfx_rle.cpp
    game/graphics/gfx_spans.cpp
    game/graphics/graphics_helpers.cpp
    game/graphics/panel_build_menu.cpp
    game/graphics/panel_building_info.cpp
    game/graphics/panel_drawer.cpp
    game/graphics/panel_drawer_tests.cpp
    game/graphics/panel_settlers.cpp
    game/graphics/panel_soldiers.cpp
    game/graphics/panel_statistics.cpp
    game/graphics/text_panels.cpp
    game/graphics/text_panels_tests.cpp
    game/graphics/text_surface.cpp
    game/graphics/text_surface_tests.cpp
    game/graphics/value_bar.cpp
    game/graphics/landscape_blit.cpp
    game/graphics/landscape_blit_tests.cpp
    game/graphics/landscape_colours.cpp
    game/graphics/mouse_surface.cpp
    game/graphics/screen_layout.cpp

    # game/net/: CNetThread, lobby protocol buffer
    game/net/lobby_memory_block.cpp
    game/net/net_thread.cpp

    # game/sound/: the sound data (CSoundData, the SND files)
    game/sound/sound_data.cpp
    game/sound/sound_data_tests.cpp
    game/sound/sound_effects.cpp
    game/sound/sound_effects_tests.cpp
    # game/framework/: WinMain, Win32/DirectX framework classes, exceptions, strings
    game/framework/win32_locks.cpp
    game/framework/directx_draw.cpp
    game/framework/directx_draw_tests.cpp
    game/framework/directx_draw_thread.cpp
    game/framework/directx_drawer.cpp
    game/framework/directx_drawer_lifetime.cpp
    game/framework/directx_drawer_tests.cpp
    game/framework/directx_sound.cpp
    game/framework/directx_sound_tests.cpp
    game/framework/directx_sound_effect.cpp
    game/framework/directx_sound_effect_tests.cpp
    game/framework/directx_sound_files.cpp
    game/framework/directx_sound_files_tests.cpp
    game/framework/folded_getters.cpp
    game/framework/framework_helpers.cpp
    game/framework/framework_small.cpp
    game/framework/game_config.cpp
    game/framework/game_config_fields.cpp
    game/framework/misc_helpers.cpp
    game/framework/string_members.cpp
    game/framework/wait_times.cpp
    game/framework/win32_thread_destructors.cpp
    game/framework/win32_app_members.cpp
    game/framework/wide_strings.cpp
    game/framework/win32_cdaudio.cpp
    game/framework/win32_classes.cpp
    game/framework/win32_detach.cpp
    game/framework/win32_errors.cpp
    game/framework/win32_exceptions.cpp
    game/framework/win32_mapping.cpp
    game/framework/win32_mapping_tests.cpp
    game/framework/win32_synchronize.cpp
    game/framework/win32_synchronize_tests.cpp
    game/framework/win32_registry.cpp
    game/framework/win32_registry_tests.cpp
    game/framework/win32_throwers.cpp
    game/framework/win32_throwers_tests.cpp
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
