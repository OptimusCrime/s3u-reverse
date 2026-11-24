#!/usr/bin/env python3
"""Draw the reimplementation progress as a treemap.

Every known function in S3.EXE is one box, sized by its code size in bytes
and coloured by its status in re/functions.csv. Verified functions are split
by their whole-game evidence (re/whole_game_calls.csv, tools/whole_game.py):
seen in a whole-game check, tests only (0 calls in every check), or not
measured yet. Boxes are grouped by category (game, C runtime, Watcom, ...)
and then by class. Functions we have not identified yet are grouped by
address range.

Writes:
  docs/progress-data.js  the data for docs/progress.html, a static page that
                         draws the interactive treemap, totals and history
                         in the browser (open it, or publish both files)
  docs/progress.svg      a static picture of the treemap for README.md

Usage:  python tools/progress.py            (regenerate both files)
        python tools/progress.py --no-history  (skip reading git history)

No dependencies beyond the standard library. Run it whenever the inventory
changes and commit the result together with the batch.
"""

import argparse
import collections
import csv
import datetime
import html
import io
import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402  (shares the file locations and baseline loader)
import whole_game  # noqa: E402  (whole-game evidence of verified functions)

ROOT = inventory.ROOT
OUT_DIR = ROOT / "docs"

# Where the statically linked C runtime starts (see CLAUDE.md). Unnamed
# functions after it are counted as runtime code, not game code.
CRT_START = 0x61E4EF
BUCKET = 0x10000  # address range per group of unidentified functions

# status -> (label, colour). Order = legend order, worst to best. The three
# verified_* statuses split inventory status "verified" by whole-game evidence.
STATUS_STYLE = {
    "unknown": ("Not looked at", "#c9ccd1"),
    "classified": ("Classified (class/role known)", "#e8a39b"),
    "identified": ("Identified (purpose known)", "#f2c86b"),
    "reimplemented": ("Reimplemented, not verified", "#a9d18e"),
    "verified_unmeasured": ("Verified by tests, no whole-game check yet", "#a8c8e6"),
    "verified_tests": ("Verified by tests only (never called in a whole-game check)", "#4f8fc0"),
    "verified_game": ("Verified, and identical in a whole-game check", "#3d9a50"),
}
VERIFIED_STATUSES = ("verified_unmeasured", "verified_tests", "verified_game")
EVIDENCE_STATUS = {whole_game.UNMEASURED: "verified_unmeasured",
                   whole_game.TESTS_ONLY: "verified_tests",
                   whole_game.SEEN: "verified_game"}
CATEGORY_LABEL = {
    "game": "Game code (Blue Byte)",
    "unidentified": "Not yet identified (probably game code)",
    "crt": "C runtime",
    "watcom": "Watcom container library",
    "thirdparty": "Third-party libraries (zlib, libpng)",
    "compiler": "Compiler-generated",
    "stl": "C++ standard library",
    "mfc": "MFC",
}


# ---------------------------------------------------------------- data

def function_sizes() -> dict[int, int]:
    """Size of every known function = distance to the next start of anything
    (function or exception funclet)."""
    starts = set(inventory.load_baseline())
    every_start = set(starts)
    with inventory.BASELINE.open(encoding="utf-8") as f:
        for line in f:
            if line.startswith("0x"):
                every_start.add(int(line.split("\t", 1)[0], 16))
    ordered = sorted(every_start)
    following = {a: b for a, b in zip(ordered, ordered[1:])}
    return {a: max(1, following.get(a, a + 16) - a) for a in starts}


def load_rows(text: str | None = None) -> dict[int, dict[str, str]]:
    if text is None:
        text = inventory.INVENTORY.read_text(encoding="utf-8")
    return {int(r["address"], 16): r for r in csv.DictReader(io.StringIO(text))}


def build_tree(rows, sizes, baseline, evidence):
    """category -> group -> list of (address, name, status, size)."""
    tree = collections.defaultdict(lambda: collections.defaultdict(list))
    for address, size in sizes.items():
        row = rows.get(address)
        if row:
            category, status, name = row["category"], row["status"], row["name"]
            if status == "verified":
                status = EVIDENCE_STATUS[evidence[address]["category"]]
            group = name.rsplit("::", 1)[0] if "::" in name else "(free functions)"
        else:
            kind, name = baseline[address]
            status = "unknown"
            if kind == "library" or address >= CRT_START:
                category, group = "crt", "(unnamed runtime functions)"
            else:
                category = "unidentified"
                start = address // BUCKET * BUCKET
                group = f"0x{start:06x}-0x{start + BUCKET - 1:06x}"
        tree[category][group].append((address, name, status, size))
    return tree


def totals(tree):
    count = collections.Counter()
    size = collections.Counter()
    for groups in tree.values():
        for functions in groups.values():
            for _, _, status, s in functions:
                count[status] += 1
                size[status] += s
    return count, size


def history(sizes, baseline):
    """(date, commit, verified count, verified bytes, understood bytes,
    whole-game seen bytes, whole-game seen count) for every commit that
    changed the inventory or the whole-game calls. Uses today's sizes.
    Before re/whole_game_calls.csv existed, whole-game results come from the
    inventory notes."""
    log = subprocess.run(
        ["git", "log", "--reverse", "--format=%H %cs", "--", "re/functions.csv",
         "re/whole_game_calls.csv"],
        cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    points = []
    for commit, date in zip(log[::2], log[1::2]):
        text = subprocess.run(["git", "show", f"{commit}:re/functions.csv"],
                              cwd=ROOT, capture_output=True, text=True,
                              encoding="utf-8").stdout
        try:
            rows = load_rows(text)
        except (KeyError, csv.Error):
            continue
        verified = [a for a, r in rows.items() if r.get("status") == "verified" and a in sizes]
        understood = [a for a, r in rows.items()
                      if r.get("status") in ("identified", "reimplemented", "verified") and a in sizes]
        calls = subprocess.run(["git", "show", f"{commit}:re/whole_game_calls.csv"],
                               cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
        if calls.returncode == 0:
            called = {int(r["address"], 16) for r in whole_game.load(calls.stdout)
                      if int(r["calls"]) > 0}
        else:
            called = whole_game.seen_from_notes(rows)
        seen = [a for a in verified if a in called]
        points.append((date, commit[:7], len(verified),
                       sum(sizes[a] for a in verified), sum(sizes[a] for a in understood),
                       sum(sizes[a] for a in seen), len(seen)))
    return points


# ---------------------------------------------------------------- layout

def squarify(values, x, y, w, h):
    """Squarified treemap (Bruls, Huizing, van Wijk). values sorted descending.
    Returns one (x, y, w, h) per value."""
    total = sum(values)
    if total <= 0 or w <= 0 or h <= 0:
        return [(x, y, 0, 0)] * len(values)
    scale = w * h / total
    areas = [v * scale for v in values]
    rects = []
    i = 0
    while i < len(areas):
        short = min(w, h)
        row = [areas[i]]
        i += 1

        def worst(r):
            s = sum(r)
            return max(max(s * s / (short * short * a), short * short * a / (s * s)) for a in r)

        while i < len(areas) and worst(row + [areas[i]]) <= worst(row):
            row.append(areas[i])
            i += 1
        s = sum(row)
        if w >= h:  # lay the row out as a column on the left
            cw = s / h
            cy = y
            for a in row:
                rects.append((x, cy, cw, a / cw))
                cy += a / cw
            x += cw
            w -= cw
        else:  # as a row on top
            rh = s / w
            cx = x
            for a in row:
                rects.append((cx, y, a / rh, rh))
                cx += a / rh
            y += rh
            h -= rh
    return rects


# ---------------------------------------------------------------- drawing

def svg_treemap(tree, width=1600, height=1000) -> str:
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
           f'width="{width}" height="{height}" font-family="Segoe UI, Helvetica, Arial, sans-serif">',
           f'<rect width="{width}" height="{height}" fill="#ffffff"/>']
    esc = lambda s: html.escape(s, quote=True)  # noqa: E731
    fmt = lambda v: f"{v:.1f}".rstrip("0").rstrip(".")  # noqa: E731

    categories = sorted(tree.items(), key=lambda kv: -sum(f[3] for g in kv[1].values() for f in g))
    cat_sizes = [sum(f[3] for g in groups.values() for f in g) for _, groups in categories]
    for (category, groups), (cx, cy, cw, ch) in zip(categories, squarify(cat_sizes, 0, 0, width, height)):
        functions_in_cat = [f for g in groups.values() for f in g]
        cat_size = cat_sizes[categories.index((category, groups))]
        done = sum(f[3] for f in functions_in_cat if f[2] in VERIFIED_STATUSES)
        game = sum(f[3] for f in functions_in_cat if f[2] == "verified_game")
        header = 18 if ch > 40 and cw > 80 else 0
        label = (f"{CATEGORY_LABEL.get(category, category)} — {100 * done / cat_size:.1f}% verified "
                 f"({100 * game / cat_size:.1f}% in a whole game)")
        out.append(f'<g><rect x="{fmt(cx)}" y="{fmt(cy)}" width="{fmt(cw)}" height="{fmt(ch)}" '
                   f'fill="#2b2f36" stroke="#ffffff" stroke-width="2"/>')
        if header:
            out.append(f'<text x="{fmt(cx + 5)}" y="{fmt(cy + 13)}" font-size="12" font-weight="600" '
                       f'fill="#ffffff">{esc(label)}</text>')
        ordered = sorted(groups.items(), key=lambda kv: -sum(f[3] for f in kv[1]))
        group_sizes = [sum(f[3] for f in fs) for _, fs in ordered]
        inner = squarify(group_sizes, cx + 2, cy + header + 1, cw - 4, ch - header - 3)
        for (group, functions), (gx, gy, gw, gh) in zip(ordered, inner):
            gheader = 11 if gh > 30 and gw > 60 else 0
            out.append(f'<g><title>{esc(group)}: {len(functions)} functions</title>'
                       f'<rect x="{fmt(gx)}" y="{fmt(gy)}" width="{fmt(gw)}" height="{fmt(gh)}" '
                       f'fill="#5b616b" stroke="#2b2f36" stroke-width="0.6"/>')
            if gheader:
                max_chars = int(gw / 5.2)
                name = group if len(group) <= max_chars else group[:max(1, max_chars - 1)] + "…"
                out.append(f'<text x="{fmt(gx + 2)}" y="{fmt(gy + 8.5)}" font-size="8.5" '
                           f'fill="#ffffff">{esc(name)}</text>')
            functions = sorted(functions, key=lambda f: -f[3])
            leaves = squarify([f[3] for f in functions], gx + 0.5, gy + gheader + 0.5, gw - 1, gh - gheader - 1)
            for (address, name, status, size), (lx, ly, lw, lh) in zip(functions, leaves):
                if lw < 0.05 or lh < 0.05:
                    continue
                stroke = ' stroke="#ffffff" stroke-width="0.3"' if lw > 2 and lh > 2 else ""
                out.append(f'<rect x="{fmt(lx)}" y="{fmt(ly)}" width="{fmt(lw)}" height="{fmt(lh)}" '
                           f'fill="{STATUS_STYLE[status][1]}"{stroke}><title>{esc(name)} @ 0x{address:08x}'
                           f'\n{size} bytes — {STATUS_STYLE[status][0]}</title></rect>')
            out.append("</g>")
        out.append("</g>")
    out.append("</svg>")
    return "\n".join(out)


STATUS_CODE = {s: i for i, s in enumerate(STATUS_STYLE)}


def data_js(tree, points, evidence) -> str:
    """The data for docs/progress.html, as a script that sets
    window.PROGRESS_DATA (a script file, unlike JSON, also loads from file://).
    "evidence" has one entry per verified function: [address, largest
    whole-game call count or null if never measured, {source: calls},
    [seen replacements it may run uncounted inside]]."""
    groups, functions = [], []
    for category, category_groups in sorted(tree.items()):
        for group, members in sorted(category_groups.items()):
            groups.append([category, group])
            for address, name, status, size in members:
                functions.append([address, size, STATUS_CODE[status], len(groups) - 1,
                                  None if status == "unknown" else name])
    functions.sort()
    ev = [[address, e["calls"], e["sources"], e["via"]] for address, e in sorted(evidence.items())]
    data = {"generated": datetime.date.today().isoformat(), "statuses": list(STATUS_STYLE),
            "groups": groups, "functions": functions, "evidence": ev,
            "history": [list(p) for p in points]}
    body = json.dumps(data, separators=(",", ":"))
    body = body.replace("],[", "],\n[")  # one function per line keeps git diffs readable
    return ("// Generated by tools/progress.py from re/functions.csv. Do not edit.\n"
            f"window.PROGRESS_DATA = {body};\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-history", action="store_true", help="do not read git history")
    args = parser.parse_args()

    baseline = inventory.load_baseline()
    sizes = function_sizes()
    rows = load_rows()
    evidence = whole_game.classify(rows=rows)
    tree = build_tree(rows, sizes, baseline, evidence)
    treemap = svg_treemap(tree)
    points = [] if args.no_history else history(sizes, baseline)

    OUT_DIR.mkdir(exist_ok=True)
    (OUT_DIR / "progress.svg").write_text(treemap, encoding="utf-8", newline="\n")
    (OUT_DIR / "progress-data.js").write_text(data_js(tree, points, evidence), encoding="utf-8", newline="\n")

    count, size = totals(tree)
    total = sum(size.values())
    verified = sum(count[s] for s in VERIFIED_STATUSES)
    verified_size = sum(size[s] for s in VERIFIED_STATUSES)
    print(f"verified {verified} functions, {verified_size:,} of {total:,} bytes "
          f"({100 * verified_size / total:.2f}%)")
    print(f"  identical in a whole-game check: {count['verified_game']} functions "
          f"({size['verified_game']:,} bytes, {100 * size['verified_game'] / total:.2f}%); "
          f"tests only: {count['verified_tests']} "
          f"({sum(1 for e in evidence.values() if e['via'])} of them may run uncounted inside "
          f"our replacements); no whole-game check yet: {count['verified_unmeasured']}")
    print(f"wrote {OUT_DIR / 'progress.svg'} and {OUT_DIR / 'progress-data.js'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
