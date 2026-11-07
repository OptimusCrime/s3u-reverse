#!/usr/bin/env python3
"""Draw the reimplementation progress as a treemap.

Every known function in S3.EXE is one box, sized by its code size in bytes
and coloured by its status in re/functions.csv. Boxes are grouped by
category (game, C runtime, Watcom, ...) and then by class. Functions we have
not identified yet are grouped by address range.

Writes:
  docs/progress.svg   the treemap (shown in README.md; GitHub renders it)
  docs/progress.html  the same with hover details, totals and history

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
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402  (shares the file locations and baseline loader)

ROOT = inventory.ROOT
OUT_DIR = ROOT / "docs"

# Where the statically linked C runtime starts (see CLAUDE.md). Unnamed
# functions after it are counted as runtime code, not game code.
CRT_START = 0x61E4EF
BUCKET = 0x10000  # address range per group of unidentified functions

# status -> (label, colour). Order = legend order, worst to best.
STATUS_STYLE = {
    "unknown": ("Not looked at", "#c9ccd1"),
    "classified": ("Classified (class/role known)", "#e8a39b"),
    "identified": ("Identified (purpose known)", "#f2c86b"),
    "reimplemented": ("Reimplemented, not verified", "#a9d18e"),
    "verified": ("Reimplemented and verified", "#3d9a50"),
}
CATEGORY_LABEL = {
    "game": "Game code (Blue Byte)",
    "unidentified": "Not yet identified (probably game code)",
    "crt": "C runtime",
    "watcom": "Watcom container library",
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


def build_tree(rows, sizes, baseline):
    """category -> group -> list of (address, name, status, size)."""
    tree = collections.defaultdict(lambda: collections.defaultdict(list))
    for address, size in sizes.items():
        row = rows.get(address)
        if row:
            category, status, name = row["category"], row["status"], row["name"]
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
    """(date, commit, verified count, verified bytes, understood bytes) for
    every commit that changed the inventory. Uses today's sizes."""
    log = subprocess.run(
        ["git", "log", "--reverse", "--format=%H %cs", "--", "re/functions.csv"],
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
        points.append((date, commit[:7], len(verified),
                       sum(sizes[a] for a in verified), sum(sizes[a] for a in understood)))
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
        done = sum(f[3] for f in functions_in_cat if f[2] == "verified")
        header = 18 if ch > 40 and cw > 80 else 0
        label = f"{CATEGORY_LABEL.get(category, category)} — {100 * done / cat_sizes[categories.index((category, groups))]:.1f}% verified"
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


def history_svg(points, total_bytes, width=760, height=220) -> str:
    if len(points) < 2:
        return "<p>Not enough history yet.</p>"
    left, right, top, bottom = 50, 10, 10, 30
    pw, ph = width - left - right, height - top - bottom
    peak = max(p[4] for p in points) or 1
    ymax = peak * 1.1
    n = len(points)
    px = lambda i: left + pw * i / (n - 1)  # noqa: E731
    py = lambda v: top + ph * (1 - v / ymax)  # noqa: E731
    out = [f'<svg viewBox="0 0 {width} {height}" class="history" role="img" '
           f'aria-label="Verified and understood bytes per inventory commit">']
    for k in range(5):
        v = ymax * k / 4
        out.append(f'<line x1="{left}" x2="{width - right}" y1="{py(v):.1f}" y2="{py(v):.1f}" class="grid"/>'
                   f'<text x="{left - 6}" y="{py(v) + 4:.1f}" text-anchor="end" class="axis">'
                   f'{100 * v / total_bytes:.1f}%</text>')
    for key, cls in ((4, "understood"), (3, "verified")):
        path = " ".join(f"{px(i):.1f},{py(p[key]):.1f}" for i, p in enumerate(points))
        out.append(f'<polyline points="{path}" class="{cls}"/>')
        for i, p in enumerate(points):
            out.append(f'<circle cx="{px(i):.1f}" cy="{py(p[key]):.1f}" r="3" class="{cls}">'
                       f'<title>{p[0]} {p[1]}: {p[key]:,} bytes ({100 * p[key] / total_bytes:.2f}%)'
                       f'{f", {p[2]} functions" if key == 3 else ""}</title></circle>')
    for i in (0, n - 1):
        out.append(f'<text x="{px(i):.1f}" y="{height - 8}" text-anchor="{"start" if i == 0 else "end"}" '
                   f'class="axis">{points[i][0]}</text>')
    out.append("</svg>")
    return "\n".join(out)


def page(tree, treemap, points) -> str:
    count, size = totals(tree)
    total_count, total_bytes = sum(count.values()), sum(size.values())
    game_bytes = sum(f[3] for c in ("game", "unidentified") for g in tree.get(c, {}).values() for f in g)
    game_verified = sum(f[3] for c in ("game", "unidentified") for g in tree.get(c, {}).values()
                        for f in g if f[2] == "verified")
    bar = "".join(f'<span style="width:{100 * size[s] / total_bytes:.3f}%;background:{c}" '
                  f'title="{l}: {size[s]:,} bytes"></span>' for s, (l, c) in STATUS_STYLE.items())
    legend_rows = "".join(
        f'<tr><td><i style="background:{c}"></i>{l}</td><td>{count[s]:,}</td>'
        f'<td>{size[s]:,}</td><td>{100 * size[s] / total_bytes:.2f}%</td></tr>'
        for s, (l, c) in STATUS_STYLE.items())
    generated = datetime.date.today().isoformat()
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>S3 Reimplementation Progress</title>
<style>
:root {{ --bg:#f7f7f8; --fg:#1d2026; --muted:#5d6470; --card:#ffffff; --line:#e1e3e7; }}
@media (prefers-color-scheme: dark) {{
  :root {{ --bg:#16181c; --fg:#e7e9ec; --muted:#9aa1ab; --card:#1f2228; --line:#30343b; }}
}}
body {{ margin:0; padding:24px 16px; background:var(--bg); color:var(--fg);
       font:15px/1.5 "Segoe UI", Helvetica, Arial, sans-serif; }}
main {{ max-width:1600px; margin:0 auto; }}
h1 {{ font-size:24px; margin:0 0 4px; }} h2 {{ font-size:17px; margin:28px 0 8px; }}
.muted {{ color:var(--muted); }}
.stats {{ display:flex; flex-wrap:wrap; gap:12px; margin:16px 0; }}
.stat {{ background:var(--card); border:1px solid var(--line); border-radius:8px; padding:10px 16px; }}
.stat b {{ display:block; font-size:22px; font-variant-numeric:tabular-nums; }}
.bar {{ display:flex; height:16px; border-radius:4px; overflow:hidden; border:1px solid var(--line); }}
.bar span {{ display:block; height:100%; }}
table {{ border-collapse:collapse; margin-top:10px; font-variant-numeric:tabular-nums; }}
td, th {{ padding:3px 14px 3px 0; text-align:right; }} td:first-child, th:first-child {{ text-align:left; }}
td i {{ display:inline-block; width:12px; height:12px; border-radius:2px; margin-right:8px; vertical-align:-1px; }}
.map {{ overflow-x:auto; background:var(--card); border:1px solid var(--line); border-radius:8px; padding:6px; }}
.map svg {{ display:block; width:100%; height:auto; min-width:900px; }}
.history {{ width:100%; max-width:760px; height:auto; }}
.history .grid {{ stroke:var(--line); }} .history .axis {{ fill:var(--muted); font-size:11px; }}
.history polyline {{ fill:none; stroke-width:2; }}
.history .verified {{ stroke:#3d9a50; fill:#3d9a50; }} .history .understood {{ stroke:#d9a53a; fill:#d9a53a; }}
.history polyline.verified, .history polyline.understood {{ fill:none; }}
</style></head><body><main>
<h1>Settlers 3 reimplementation progress</h1>
<div class="muted">Generated {generated} from <code>re/functions.csv</code> by <code>tools/progress.py</code>.
Each box is one function of S3.EXE, sized by its code size. Hover a box for details.</div>
<div class="stats">
  <div class="stat"><b>{100 * size["verified"] / total_bytes:.2f}%</b>of all code verified</div>
  <div class="stat"><b>{100 * game_verified / game_bytes:.2f}%</b>of game code verified</div>
  <div class="stat"><b>{count["verified"]:,} / {total_count:,}</b>functions verified</div>
  <div class="stat"><b>{total_count - count["unknown"]:,}</b>functions named</div>
</div>
<div class="bar">{bar}</div>
<table><tr><th>Status</th><th>Functions</th><th>Bytes</th><th>Share</th></tr>{legend_rows}</table>
<h2>Treemap</h2>
<div class="map">{treemap}</div>
<h2>History</h2>
<div class="muted">Share of all code bytes, per commit that changed the inventory.
<span style="color:#3d9a50">●</span> verified &nbsp; <span style="color:#d9a53a">●</span> identified or better</div>
{history_svg(points, total_bytes) if points else "<p>History skipped.</p>"}
</main></body></html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-history", action="store_true", help="do not read git history")
    args = parser.parse_args()

    baseline = inventory.load_baseline()
    sizes = function_sizes()
    tree = build_tree(load_rows(), sizes, baseline)
    treemap = svg_treemap(tree)
    points = [] if args.no_history else history(sizes, baseline)

    OUT_DIR.mkdir(exist_ok=True)
    (OUT_DIR / "progress.svg").write_text(treemap, encoding="utf-8", newline="\n")
    (OUT_DIR / "progress.html").write_text(page(tree, treemap, points), encoding="utf-8", newline="\n")

    count, size = totals(tree)
    total = sum(size.values())
    print(f"verified {count['verified']} functions, {size['verified']:,} of {total:,} bytes "
          f"({100 * size['verified'] / total:.2f}%)")
    print(f"wrote {OUT_DIR / 'progress.svg'} and {OUT_DIR / 'progress.html'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
