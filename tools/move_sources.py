"""Move the files of src/s3re into the subsystem folders (docs/source_layout_plan.md).

The record of the source tree move of 2026-11-23 (session 48). The table
below is the plan's appendix plus the files added after it was written
(the computer-player files of A2/A3). The script:

- checks that every file in src/s3re is mapped and that the .cpp base
  names stay unique (tools/emutest/changed.py keys objects by base name);
- runs `git mv` for every file that moves;
- rewrites the source list of src/s3re/CMakeLists.txt, grouped by folder;
- rewrites full paths (src/s3re/game/pathing.cpp) and paths relative to
  src/s3re (game/pathing.h, as in #include lines and notes) in src/, in
  CLAUDE.md, README.md, TODO.md, docs/*.md, re/* and tools/**/*.py.
  LOGBOOK.md (history) and docs/source_layout_plan.md (the plan, whose
  appendix lists the old paths) are left alone. Bare file names
  (pathing.cpp) stay valid: no file is renamed.

Usage: python tools/move_sources.py [--dry-run]
Running it again after the move finds nothing to do.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "s3re"

# New folder (relative to src/s3re) -> (comment for CMakeLists.txt, files
# by their current path relative to src/s3re). Files not listed don't move.
MOVES = [
    ("tooling", "launcher features of original mode (--load-save)", [
        "game/savegame_start.cpp"]),
    ("tooling/trace", "the determinism trace", [
        "trace/determinism.cpp", "trace/determinism.h"]),
    ("tooling/replay", "command recorder and replay", [
        "replay/command_log.cpp", "replay/command_log.h"]),
    ("game/world", "CGameData basics: RNG, players, statistics, counters, tables", [
        "game/random.cpp", "game/random.h", "game/players.cpp", "game/player_queries.cpp",
        "game/player_presence.cpp", "game/statistics.cpp", "game/counting.cpp",
        "game/world_tables.cpp", "game/id_allocators.cpp", "game/world_search.cpp",
        "game/notifications.cpp"]),
    ("game/map", "tiles, directions, areas and regions, territory, fog of war, minimap", [
        "game/map.cpp", "game/map_directions.cpp", "game/map_neighbourhood.cpp",
        "game/map_marks.cpp", "game/map_terrain.cpp", "game/map_areas.cpp",
        "game/map_regions.cpp", "game/area_split.cpp", "game/flood_fills.cpp",
        "game/territory.cpp", "game/terrain_updates.cpp", "game/fog_of_war.cpp",
        "game/minimap.cpp"]),
    ("game/mapgen", "the random map generator", [
        "game/world_create.cpp", "game/map_generation.cpp"]),
    ("game/environment", "UpdateEnvironment and the map effects", [
        "game/environment.cpp", "game/map_effects.cpp", "game/map_effect_spawns.cpp",
        "game/map_effect_expiry.cpp"]),
    ("game/economy", "UpdateEconomy, goods piles and their moves", [
        "game/update_economy.cpp", "game/economy.cpp", "game/economy_helpers.cpp",
        "game/goods_piles.cpp", "game/area_piles.cpp", "game/pile_goods_moves.cpp"]),
    ("game/buildings", "building sites, construction, crushing, flags, goods", [
        "game/building_sites.cpp", "game/building_done.cpp", "game/building_crush.cpp",
        "game/building_flags.cpp", "game/building_goods.cpp", "game/building_tiles.cpp"]),
    ("game/settlers", "UpdateSettlers, settler records, walking, soldiers", [
        "game/update_settlers.cpp", "game/settlers.cpp", "game/settler_records.cpp",
        "game/settler_queries.cpp", "game/settler_spawn.cpp", "game/settler_idle.cpp",
        "game/settler_walk.cpp", "game/soldier.cpp", "game/miner_jobs.cpp",
        "game/settler_groups.h"]),
    ("game/settlers/jobs", "the job handlers of UpdateSettlers", [
        "game/settler_jobs.h", "game/settler_jobs_test.h", "game/settler_jobs_units.cpp",
        "game/settler_jobs_building.cpp", "game/settler_jobs_metal.cpp",
        "game/settler_jobs_food.cpp", "game/settler_jobs_drinks.cpp",
        "game/settler_jobs_special.cpp"]),
    ("game/pathing", "the pathfinder (Dirk/Pathing.cpp)", [
        "game/pathing.cpp", "game/pathing.h"]),
    ("game/spells", "the god events (Volker/GodlyEvents.cpp)", [
        "game/godly_events.cpp", "game/spells.cpp", "game/spells_land.cpp",
        "game/spell_units.cpp", "game/spell_goods.cpp", "game/spell_effects.cpp"]),
    ("game/logic", "CLogicThread: Step, game end, victory conditions, UI state in the world", [
        "game/logic_thread.cpp", "game/logic_thread_layout.h", "game/logic_helpers.cpp",
        "game/game_end_check.cpp", "game/game_end.cpp", "game/victory_conditions.cpp",
        "game/objectives.cpp", "game/ui_state.cpp", "game/ui_overlays.cpp"]),
    ("game/logic/commands", "ExecuteCommands and its parts", [
        "game/execute_commands.cpp", "game/execute_commands.h",
        "game/execute_commands_test.h", "game/execute_commands_areas.cpp",
        "game/execute_commands_net.cpp", "game/execute_commands_trade.cpp",
        "game/execute_commands_units.cpp", "game/command_helpers.cpp"]),
    ("game/ai", "computer players (Rainer/*.cpp)", [
        "game/ai_layout.h", "game/ai_computer_player.cpp", "game/ai_agents.cpp",
        "game/ai_census.cpp", "game/ai_evaland.cpp", "game/ai_helpers.cpp",
        "game/ai_small_helpers.cpp",
        # Added after the plan was written (computer players A2, A3):
        "game/ai_economy_conditions.cpp", "game/ai_map_queries.cpp",
        "game/ai_request_notes.cpp", "game/ai_site_queries.cpp"]),
    ("game/ui", "CScreenElement and the controls, menu and settings layouts", [
        "game/screen_element.cpp", "game/screen_element_layout.h",
        "game/ui_static_text.cpp", "game/menus_layout.h", "game/game_settings_layout.h"]),
    ("game/graphics", "graphics data, landscape colours, DirectDraw surface", [
        "game/graphics_helpers.cpp", "game/gfx_layout.h", "game/landscape_colours.cpp",
        "game/directx_surface.cpp"]),
    ("game/net", "CNetThread, lobby protocol buffer", [
        "game/net_thread.cpp", "game/lobby_memory_block.cpp"]),
    ("game/framework", "WinMain, Win32/DirectX framework classes, exceptions, strings", [
        "game/winmain.cpp", "game/win32_classes.cpp", "game/win32_errors.cpp",
        "game/win32_exceptions.cpp", "game/framework_helpers.cpp",
        "game/framework_small.cpp", "game/framework_layout.h", "game/folded_getters.cpp",
        "game/string_members.cpp", "game/misc_helpers.cpp"]),
]

# Folders that stay (CMakeLists.txt order and comments). "" is src/s3re itself.
KEPT = [
    ("", "DLL main, hooks, logs"),
    ("test", "the differential test harness"),
    ("msvc6", "MSVC 6.0 library code (std::string, exceptions)"),
    ("watcom", "Watcom container library code"),
    ("tooling", None),  # merged with the moved tooling group
]

# Whole folders whose path changes (for text like "src/s3re/trace/").
FOLDER_MOVES = {"trace/": "tooling/trace/", "replay/": "tooling/replay/"}

# Files whose text is rewritten (relative to the repository).
TEXT_GLOBS = ["src/**/*.cpp", "src/**/*.h", "CLAUDE.md", "README.md", "TODO.md",
              "docs/*.md", "re/*.md", "re/*.csv", "re/*.h", "tools/**/*.py"]
TEXT_SKIP = {"LOGBOOK.md", "docs/source_layout_plan.md", "tools/move_sources.py"}


def mapping():
    """Old path -> new path, both relative to src/s3re."""
    moves = {}
    for folder, _, files in MOVES:
        for old in files:
            if old in moves:
                sys.exit(f"mapped twice: {old}")
            moves[old] = f"{folder}/{old.rsplit('/', 1)[-1]}"
    return moves


def current_files():
    out = subprocess.run(["git", "ls-files", "src/s3re"], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout.split()
    return [p[len("src/s3re/"):] for p in out]


def check(files, moves):
    kept = {f for f, _ in KEPT}
    arrived = set(moves.values())  # already moved (a second run)
    problems = []
    final = []
    for f in files:
        new = moves.get(f, f)
        folder = new.rsplit("/", 1)[0] if "/" in new else ""
        if f in arrived:
            pass
        elif f not in moves and folder not in kept and folder != "game":
            problems.append(f"not mapped: {f}")
        if f not in moves and folder == "game" and f.endswith(".cpp"):
            problems.append(f"a .cpp left in game/: {f}")
        final.append(new)
    for old in moves:
        if old not in files and moves[old] not in files:
            problems.append(f"mapped but missing: {old}")
    names = {}
    for p in final:
        if p.endswith(".cpp"):
            names.setdefault(p.rsplit("/", 1)[-1].lower(), []).append(p)
    problems += [f".cpp base name not unique: {v}" for v in names.values() if len(v) > 1]
    if problems:
        sys.exit("\n".join(problems))
    return final


def path_regex(moves):
    """One regex over every old path (longest first), with or without the
    src/s3re/ prefix, not inside a longer path."""
    olds = sorted(list(moves) + list(FOLDER_MOVES), key=len, reverse=True)
    alt = "|".join(re.escape(o) for o in olds)
    return re.compile(r"(?<![\w./\\-])(src/s3re/)?(" + alt + r")(?![\w-])")


def rewrite_text(text, regex, moves, is_src):
    def repl(m):
        prefix, old = m.group(1) or "", m.group(2)
        if old in FOLDER_MOVES:
            # A bare "trace/" or "replay/" is too vague; only with src/s3re/.
            if not prefix:
                return m.group(0)
            return prefix + FOLDER_MOVES[old]
        return prefix + moves[old]
    return regex.sub(repl, text)


def cmake_sources(final):
    groups = {}
    for p in final:
        if not p.endswith((".cpp", ".def")):
            continue
        folder = p.rsplit("/", 1)[0] if "/" in p else ""
        groups.setdefault(folder, []).append(p)
    order = [f for f, _ in KEPT] + [f for f, _, _ in MOVES if f != "tooling"]
    comments = {f: c for f, c in KEPT if c}
    comments.update({f: c for f, c, _ in MOVES if f != "tooling"})
    comments["tooling"] = "launcher features: --fast, --random-map, --load-save"
    lines = []
    seen = set()
    for folder in order:
        if folder in seen or folder not in groups:
            continue
        seen.add(folder)
        if lines:
            lines.append("")
        lines.append(f"    # {folder + '/' if folder else 'src/s3re/'}: {comments[folder]}")
        top = [p for p in groups[folder] if p.endswith(".cpp")]
        top.sort(key=lambda p: (p != "dllmain.cpp", p))
        top += [p for p in groups[folder] if p.endswith(".def")]
        lines += [f"    {p}" for p in top]
    missing = set(groups) - seen
    if missing:
        sys.exit(f"no CMake group for {sorted(missing)}")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true", help="print what would change")
    args = ap.parse_args()

    moves = mapping()
    files = current_files()
    final = check(files, moves)
    todo = [(o, n) for o, n in moves.items() if o in files]

    for old, new in todo:
        print(f"git mv src/s3re/{old} src/s3re/{new}")
        if not args.dry_run:
            (SRC / new).parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "mv", f"src/s3re/{old}", f"src/s3re/{new}"],
                           cwd=ROOT, check=True)

    regex = path_regex(moves)
    changed = 0
    seen = set()
    for pattern in TEXT_GLOBS:
        for path in sorted(ROOT.glob(pattern)):
            rel = path.relative_to(ROOT).as_posix()
            if rel in TEXT_SKIP or rel in seen or not path.is_file():
                continue
            seen.add(rel)
            raw = path.read_bytes()
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                # e.g. re/update_settlers_jobs.csv (Latin-1 German strings)
                text = raw.decode("latin-1")
                if regex.search(text):
                    sys.exit(f"{rel} is not UTF-8 and has paths to rewrite")
                continue
            new = rewrite_text(text, regex, moves, rel.startswith("src/"))
            if new != text:
                count = sum(1 for _ in regex.finditer(text))
                print(f"rewrite {rel} ({count} paths)")
                changed += 1
                if not args.dry_run:
                    path.write_bytes(new.encode("utf-8"))

    cmake = SRC / "CMakeLists.txt"
    text = cmake.read_text(encoding="utf-8")
    start = text.index("add_library(s3re SHARED\n") + len("add_library(s3re SHARED\n")
    end = text.index(")\n", start)
    new = text[:start] + cmake_sources(final) + text[end:]
    if new != text:
        print("rewrite src/s3re/CMakeLists.txt (source list)")
        if args.dry_run:
            print(cmake_sources(final))
        else:
            cmake.write_text(new, encoding="utf-8", newline="\n")
    print(f"{len(todo)} files moved, {changed} files rewritten"
          + (" (dry run)" if args.dry_run else ""))


if __name__ == "__main__":
    main()
