#!/usr/bin/env python3
"""Whole-game evidence: which replacements ran in a whole-game check.

A verified function has unit-level evidence (differential tests, emulator
specs). It only has whole-game evidence if the game actually called our
replacement during a whole-game check that matched the original: a
recorded-game replay (tools/check_replay.py --against-original) or a
savegame run (tools/check_save.py --against-original). The counting thunk
behind every S3RE_HOOK logs "calls <name> <n>" at exit; this module keeps
those counts in re/whole_game_calls.csv.

Columns of re/whole_game_calls.csv (one row per function and source):
  address  0x-prefixed, 8 lowercase hex digits (as in re/functions.csv)
  name     the inventory name at that address
  source   replay:<recording file name> or save:<savegame file name>
  ticks    how many ticks the check ran
  calls    the largest call count measured for this source; 0 = the hook
           was installed and counted, but the game never called it
  commit   the commit the result was recorded with (HEAD when recorded; for
           backfilled rows the commit that wrote the result into the
           inventory notes or re/savegame_checks.md)
  date     date of that commit / recording
  note     free text, e.g. why a count is special

A verified function without any row was never measured (e.g. verified after
the last whole-game check). A 0 count can still hide real runs: the thunk
only counts calls that come from the original code, so a function that is
only called directly by another of our replacements (e.g. PlanSiteLevelling,
called by our CheckBuildingSite) shows 0. `classify` flags such functions
("via") instead of counting them as seen. S3RE_HOOK_ALWAYS hooks are
not counted at all.

Commands:
  backfill   rebuild the CSV from the inventory notes (written by
             tools/record_call_counts.py) and re/savegame_checks.md; only
             for the one-time migration, it overwrites the file
  summary    print the split of verified functions (seen / tests only /
             never measured) and list the tests-only ones
  fix-names  update the name column from re/functions.csv after renames
"""

import collections
import csv
import datetime
import io
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CALLS_CSV = ROOT / "re" / "whole_game_calls.csv"
INVENTORY = ROOT / "re" / "functions.csv"
SOURCE_DIR = ROOT / "src"
COLUMNS = ["address", "name", "source", "ticks", "calls", "commit", "date", "note"]
SOURCE_RE = re.compile(r"(replay|save):\S.*")

# S3RE_HOOK(address, name, FnType, original, replacement) and variants.
HOOK_RE = re.compile(r"S3RE_(HOOK|HOOK_ALWAYS|HOOK_UNVERIFIED)\(\s*(0x[0-9a-fA-F]+)\s*,\s*(\w+)\s*,"
                     r"\s*[^,]+,\s*\w+\s*,\s*([\w:]+)\s*\)")

# Evidence categories of a verified function.
SEEN, TESTS_ONLY, UNMEASURED = "whole_game", "tests_only", "unmeasured"


# ---------------------------------------------------------------- the CSV

def load(text: str | None = None) -> list[dict[str, str]]:
    if text is None:
        if not CALLS_CSV.exists():
            return []
        text = CALLS_CSV.read_text(encoding="utf-8")
    return list(csv.DictReader(io.StringIO(text)))


def save(rows: list[dict[str, str]]) -> None:
    rows = sorted(rows, key=lambda r: (int(r["address"], 16), r["source"]))
    with CALLS_CSV.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows({c: r.get(c, "") for c in COLUMNS} for r in rows)


def head_commit() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT,
                              capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def record(source: str, ticks: int, counts: dict[int, int], note: str = "") -> tuple[int, int]:
    """Merges one whole-game run (address -> calls) into the CSV. Keeps the
    largest count per function and source; the commit/date are replaced when
    the new count is at least as large. Returns (new rows, changed rows)."""
    if not SOURCE_RE.fullmatch(source):
        raise ValueError(f"source must look like replay:NAME or save:NAME, not {source!r}")
    names = inventory_names()
    rows = load()
    index = {(int(r["address"], 16), r["source"]): r for r in rows}
    commit, date = head_commit(), datetime.date.today().isoformat()
    added = changed = 0
    for address, calls in counts.items():
        row = index.get((address, source))
        values = {"address": f"0x{address:08x}", "name": names.get(address, ""),
                  "source": source, "ticks": str(ticks), "calls": str(calls),
                  "commit": commit, "date": date, "note": note}
        if row is None:
            rows.append(values)
            added += 1
        elif calls >= int(row["calls"]):
            if not note:
                values["note"] = row["note"]
            if values != row:
                row.update(values)
                changed += 1
    save(rows)
    return added, changed


def record_named(source: str, ticks: int, counts: dict[str, int], note: str = "") -> tuple[int, int]:
    """Same as record, with the hook names of the "calls <name> <n>" lines."""
    known = hooks()
    return record(source, ticks, {known[n][0]: c for n, c in counts.items() if n in known}, note)


# ---------------------------------------------------------------- sources

def inventory_rows(text: str | None = None) -> dict[int, dict[str, str]]:
    if text is None:
        text = INVENTORY.read_text(encoding="utf-8")
    return {int(r["address"], 16): r for r in csv.DictReader(io.StringIO(text))}


def inventory_names() -> dict[int, str]:
    return {a: r["name"] for a, r in inventory_rows().items()}


def hooks() -> dict[str, tuple[int, str, str]]:
    """Hook name (as logged in "calls <name> <n>") -> (address, kind, replacement)."""
    found = {}
    for path in sorted(SOURCE_DIR.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        for kind, address, name, replacement in HOOK_RE.findall(text):
            found[name] = (int(address, 16), kind, replacement.split("::")[-1])
    return found


DEFINITION_RE = re.compile(r"^(?!(?:using|namespace|struct|class|template|enum|typedef|return|"
                           r"static_assert|S3RE_\w*)\b)(?:[A-Za-z_][^;=(]*?\b)?(\w+)\s*\(")


def call_graph() -> tuple[dict, dict[str, int]]:
    """Calls between functions in our own (non-test) code.

    Returns (edges, replacements): edges maps a caller node to the set of
    nodes it calls directly; a node is the replacement name for a hooked
    function and (file, name) for any other top-level function (helpers).
    replacements maps replacement names to addresses. Tests are the part of
    a file after its "// Tests" line or its first S3RE_TEST. A rough
    parser: a function is a line at column 0 that names it before "(";
    member functions defined inside classes count as part of the function
    before them. Good enough to flag, not to prove."""
    replacements = {}
    for _name, (address, _kind, replacement) in hooks().items():
        replacements.setdefault(replacement, address)
    files = {}
    for path in sorted(SOURCE_DIR.rglob("*.cpp")):
        lines = path.read_text(encoding="utf-8").splitlines()
        end = len(lines)
        for k, line in enumerate(lines):
            if line.strip() == "// Tests" or line.startswith("S3RE_TEST("):
                end = k
                break
        files[path.relative_to(ROOT).as_posix()] = lines[:end]
    defined = {f: {m.group(1) for line in lines if (m := DEFINITION_RE.match(line))}
               for f, lines in files.items()}
    edges = collections.defaultdict(set)
    word = re.compile(r"(?<![\w:&.>])(\w+)\s*\(")
    for f, lines in files.items():
        node = lambda name: name if name in replacements else (f, name)  # noqa: E731
        current = None
        for line in lines:
            m = DEFINITION_RE.match(line)
            if m:
                current = node(m.group(1))
                if not line.rstrip().endswith((";", ",")) or "{" in line:
                    continue
            if current is None or "S3RE_HOOK" in line or line.lstrip().startswith("//"):
                continue
            for call in word.finditer(line):
                name = call.group(1)
                if name in replacements or name in defined[f]:
                    target = node(name)
                    if target != current:
                        edges[current].add(target)
    return edges, replacements


def uncounted_runs(seen: set[int]) -> dict[int, list[str]]:
    """address -> names of seen replacements from which our own code reaches
    our replacement of it directly (through helpers or other replacements).
    Such calls bypass the counting thunk, so a 0 count may hide real runs."""
    edges, replacements = call_graph()
    result = collections.defaultdict(set)
    for start, address in replacements.items():
        if address not in seen:
            continue
        stack, visited = [start], {start}
        while stack:
            for target in edges.get(stack.pop(), ()):
                if target in visited:
                    continue
                visited.add(target)
                stack.append(target)
                if isinstance(target, str) and replacements[target] != address:
                    result[replacements[target]].add(start)
    return {a: sorted(names) for a, names in result.items()}


# ---------------------------------------------------------------- classification

def classify(calls_rows=None, rows=None, with_callers: bool = True) -> dict[int, dict]:
    """For every verified inventory row: {"category": SEEN | TESTS_ONLY |
    UNMEASURED, "calls": largest count or None, "sources": {source: calls},
    "via": [replacement names] (TESTS_ONLY only: 0 counted calls, but our
    own replacements of these seen functions reach it directly, so it may
    have run uncounted), "notes": [notes]}."""
    if calls_rows is None:
        calls_rows = load()
    if rows is None:
        rows = inventory_rows()
    measured = collections.defaultdict(dict)
    notes = collections.defaultdict(list)
    for r in calls_rows:
        address = int(r["address"], 16)
        measured[address][r["source"]] = max(int(r["calls"]), measured[address].get(r["source"], 0))
        if r["note"] and r["note"] not in notes[address]:
            notes[address].append(r["note"])
    result = {}
    for address, row in rows.items():
        if row["status"] != "verified":
            continue
        sources = measured.get(address, {})
        if not sources:
            category, best = UNMEASURED, None
        else:
            best = max(sources.values())
            category = SEEN if best > 0 else TESTS_ONLY
        result[address] = {"category": category, "calls": best, "sources": sources,
                           "via": [], "notes": notes.get(address, [])}
    if with_callers:
        seen = {a for a, e in result.items() if e["category"] == SEEN}
        for address, via in uncounted_runs(seen).items():
            if address in result and result[address]["category"] == TESTS_ONLY:
                result[address]["via"] = via
    return result


# Whole-game sentences in the inventory notes (tools/record_call_counts.py and
# older hand-written variants). Used by backfill and by tools/progress.py for
# commits before re/whole_game_calls.csv existed.
NOTE_PATTERNS = [
    # Whole-game check (scripted-seed127.bin, seed 1234, 3,000 ticks) identical to the original, N calls.
    (re.compile(r"Whole-game check \(([^,)]+)(?:, seed \d+)?, ([\d,]+) ticks\) identical to the "
                r"original, ([\d,]+) calls"), lambda m: (m[1], m[2], m[3], "")),
    # ...identical to the original; the hook counted 0 calls because its only caller ... is replaced
    (re.compile(r"Whole-game check \(([^,)]+)(?:, seed \d+)?, ([\d,]+) ticks\) identical to the "
                r"original; the hook counted ([\d,]+) calls because ([^.]*)"),
     lambda m: (m[1], m[2], m[3], "0 counted, but the inventory notes say it ran inside our "
                "replaced caller (" + m[4].strip() + "); unconfirmed")),
    # Whole-game replay (scripted-seed127, 3,000 ticks) identical to the original; called N times in it.
    (re.compile(r"Whole-game replay \(([^,)]+), ([\d,]+) ticks\) identical to the original; "
                r"called ([\d,]+) times"), lambda m: (m[1], m[2], m[3], "")),
    # Not called in the whole-game check (scripted-seed127.bin, 3,000 ticks), so no whole-game evidence.
    (re.compile(r"Not called in the whole-game check \(([^,)]+), ([\d,]+) ticks\)"),
     lambda m: (m[1], m[2], "0", "")),
    # Not called in the scripted-seed127 game (whole-game check), so ...
    (re.compile(r"Not called in the (scripted-seed\d+) game"), lambda m: (m[1], "3000", "0", "")),
]


def parse_notes(notes: str) -> list[tuple[str, int, int, str]]:
    """[(source, ticks, calls, note)] from an inventory notes field."""
    found = []
    for pattern, values in NOTE_PATTERNS:
        for m in pattern.finditer(notes):
            recording, ticks, calls, note = values(m)
            recording = os.path.basename(recording.strip())
            if not recording.endswith(".bin"):
                recording += ".bin"
            found.append((f"replay:{recording}", int(ticks.replace(",", "")),
                          int(calls.replace(",", "")), note))
    return found


def seen_from_notes(rows: dict[int, dict[str, str]]) -> set[int]:
    """Verified addresses whose notes report calls > 0 (for history points)."""
    return {a for a, r in rows.items() if r.get("status") == "verified"
            and any(calls > 0 for _, _, calls, _ in parse_notes(r.get("notes", "")))}


# ---------------------------------------------------------------- backfill

def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", check=True).stdout


# Savegame checks before this file existed (re/savegame_checks.md).
SAVES = ("slot 1.sav", "slot 2.sav", "slot 3.sav")
SAVE_BATCH27 = "f3f4a64"  # the code checked by the first savegame runs
SAVE_BATCH28 = "a37a88b"  # the repeat with batch 28
SAVE_TICKS = 2000


def savegame_table() -> dict[int, list[int]]:
    """The "New whole-game evidence" table of re/savegame_checks.md:
    address -> calls per save (slot 1, 2, 3)."""
    text = (ROOT / "re" / "savegame_checks.md").read_text(encoding="utf-8")
    table = {}
    for m in re.finditer(r"^\| (0x[0-9a-f]{8}) \| [^|]+ \| (\d+) / (\d+) / (\d+) \|$", text, re.M):
        table[int(m[1], 16)] = [int(m[2]), int(m[3]), int(m[4])]
    return table


def backfill() -> int:
    rows = inventory_rows()
    names = {a: r["name"] for a, r in rows.items()}
    commits = git("log", "--reverse", "--format=%h %cs", "--", "re/functions.csv").split()
    first_seen = {}  # (address, source) -> (commit, date): first inventory version with the result
    for commit, date in zip(commits[::2], commits[1::2]):
        try:
            old = inventory_rows(git("show", f"{commit}:re/functions.csv"))
        except (KeyError, csv.Error):
            continue
        for address, row in old.items():
            for source, *_ in parse_notes(row.get("notes", "")):
                first_seen.setdefault((address, source), (commit, date))

    out = {}
    unmatched = []
    for address, row in rows.items():
        if row["status"] != "verified":
            continue
        found = parse_notes(row["notes"])
        if not found:
            unmatched.append(address)
        for source, ticks, calls, note in found:
            commit, date = first_seen.get((address, source), ("", ""))
            key = (address, source)
            if key not in out or calls > int(out[key]["calls"]):
                out[key] = {"address": f"0x{address:08x}", "name": names[address], "source": source,
                            "ticks": str(ticks), "calls": str(calls), "commit": commit,
                            "date": date, "note": note or "backfilled from the inventory notes"}

    # Savegame runs. The table lists every replacement that was called in a
    # savegame run although the recorded-game check had not called it; every
    # other such replacement installed at batch 27 therefore had 0 calls.
    table = savegame_table()
    dates = {c: git("show", "-s", "--format=%cs", c).strip() for c in (SAVE_BATCH27, SAVE_BATCH28)}
    at27 = inventory_rows(git("show", f"{SAVE_BATCH27}:re/functions.csv"))
    at28 = inventory_rows(git("show", f"{SAVE_BATCH28}:re/functions.csv"))
    replay_zero = {a for (a, s), r in out.items() if s.startswith("replay:") and r["calls"] == "0"}
    note27 = "backfilled from re/savegame_checks.md (batch 27 runs)"
    for address in sorted(replay_zero | set(table)):
        if address not in rows or at27.get(address, {}).get("status") != "verified":
            continue
        counts = table.get(address, [0, 0, 0])
        for save_name, calls in zip(SAVES, counts):
            out[(address, f"save:{save_name}")] = {
                "address": f"0x{address:08x}", "name": names[address], "source": f"save:{save_name}",
                "ticks": str(SAVE_TICKS), "calls": str(calls), "commit": SAVE_BATCH27,
                "date": dates[SAVE_BATCH27],
                "note": note27 if address in table else
                note27 + "; 0 = not in its list of newly called functions"}
    # The batch 28 repeat: CheckBuildingSite 2,690 calls in slot 3 only, the
    # other four batch 28 functions not called.
    new28 = [a for a, r in at28.items() if r["status"] == "verified"
             and at27.get(a, {}).get("status") != "verified" and a != 0x4feb40]
    for address in new28:
        for k, save_name in enumerate(SAVES):
            calls = 2690 if (address == 0x4d99e0 and k == 2) else 0
            out[(address, f"save:{save_name}")] = {
                "address": f"0x{address:08x}", "name": names[address], "source": f"save:{save_name}",
                "ticks": str(SAVE_TICKS), "calls": str(calls), "commit": SAVE_BATCH28,
                "date": dates[SAVE_BATCH28],
                "note": "backfilled from re/savegame_checks.md (batch 28 repeat)"}

    # GameWorld::SeedRandom is an S3RE_HOOK_ALWAYS hook: not counted, but it
    # seeds every game the launcher starts (s3re.log logs each seed).
    if rows.get(0x4feb40, {}).get("status") == "verified":
        out[(0x4feb40, "replay:scripted-seed127.bin")] = {
            "address": "0x004feb40", "name": names[0x4feb40], "source": "replay:scripted-seed127.bin",
            "ticks": "3000", "calls": "1", "commit": "", "date": "",
            "note": "S3RE_HOOK_ALWAYS is not counted; 1 = it seeds every game start "
                    "(s3re.log logs the seed)"}
        unmatched = [a for a in unmatched if a != 0x4feb40]

    save(list(out.values()))
    print(f"wrote {len(out)} rows to {CALLS_CSV.relative_to(ROOT)}")
    for address in unmatched:
        print(f"  no whole-game result in the notes: 0x{address:08x} {names[address]} (never measured)")
    return 0


# ---------------------------------------------------------------- commands

def summary() -> int:
    evidence = classify()
    rows = inventory_rows()
    by = collections.defaultdict(list)
    for address, e in evidence.items():
        by[e["category"]].append(address)
    print(f"verified functions: {len(evidence)}")
    print(f"  seen in a whole-game check:      {len(by[SEEN])}")
    print(f"  tests only (0 calls in every run): {len(by[TESTS_ONLY])} "
          f"({sum(1 for a in by[TESTS_ONLY] if evidence[a]['via'])} of them reachable from seen "
          f"replacements through our own code, so they may have run uncounted)")
    print(f"  never measured:                  {len(by[UNMEASURED])}")
    for title, category in (("tests only", TESTS_ONLY), ("never measured", UNMEASURED)):
        print(f"\n{title}:")
        for address in sorted(by[category]):
            via = evidence[address]["via"]
            print(f"  0x{address:08x} {rows[address]['name']}"
                  + (f"  (may run uncounted inside {', '.join(via[:3])}"
                     + (f" and {len(via) - 3} more" if len(via) > 3 else "") + ")" if via else ""))
    return 0


def fix_names() -> int:
    names = inventory_names()
    rows = load()
    changed = 0
    for r in rows:
        name = names.get(int(r["address"], 16))
        if name and name != r["name"]:
            r["name"] = name
            changed += 1
    save(rows)
    print(f"{changed} name(s) updated")
    return 0


def check_errors() -> list[str]:
    """Problems in re/whole_game_calls.csv (used by inventory.py check)."""
    if not CALLS_CSV.exists():
        return []
    errors = []
    text = CALLS_CSV.read_text(encoding="utf-8")
    header = text.split("\n", 1)[0].strip()
    if header != ",".join(COLUMNS):
        return [f"whole_game_calls.csv: header must be {','.join(COLUMNS)}"]
    rows = inventory_rows()
    seen = set()
    previous = None
    for line, r in enumerate(load(text), start=2):
        where = f"whole_game_calls.csv:{line}"
        if not re.fullmatch(r"0x[0-9a-f]{8}", r["address"] or ""):
            errors.append(f"{where}: bad address '{r['address']}'")
            continue
        address = int(r["address"], 16)
        key = (address, r["source"])
        if key in seen:
            errors.append(f"{where}: duplicate row for {r['address']} {r['source']}")
        if previous is not None and key < previous:
            errors.append(f"{where}: rows must be sorted by address and source")
        seen.add(key)
        previous = key
        row = rows.get(address)
        if row is None:
            errors.append(f"{where}: {r['address']} has no row in functions.csv")
        elif row["name"] != r["name"]:
            errors.append(f"{where}: name '{r['name']}' but functions.csv says '{row['name']}' "
                          "(python tools/whole_game.py fix-names)")
        if not SOURCE_RE.fullmatch(r["source"] or ""):
            errors.append(f"{where}: source must be replay:NAME or save:NAME")
        for column in ("ticks", "calls"):
            if not (r[column] or "").isdigit():
                errors.append(f"{where}: {column} must be a non-negative integer")
    return errors


def main() -> int:
    commands = {"backfill": backfill, "summary": summary, "fix-names": fix_names}
    if len(sys.argv) != 2 or sys.argv[1] not in commands:
        print(__doc__)
        return 2
    return commands[sys.argv[1]]()


if __name__ == "__main__":
    sys.exit(main())
