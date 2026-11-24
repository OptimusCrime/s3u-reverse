"""Apply function-name proposals to re/functions.csv.

Usage:
  python tools/apply_names.py [--dry-run] [--tag TAG] PROPOSALS.tsv [...]

Each proposals file is tab-separated with a header line and the columns
  address  old_name  new_name  confidence  evidence
(address as 0x00xxxxxx; confidence high or medium).

For every proposal whose row still carries old_name:
- the name becomes new_name (a name already used by another row gets the
  address appended: Name_4a57b0);
- a `classified` row becomes `identified` when the confidence is high
  (medium proposals keep their status; their note says "probably");
- the note gets "Named (TAG, CONF): EVIDENCE Was OLD." in front of the
  existing note.
Rows that are verified or hooked keep their status. A new name starting
with StaticInit_/StaticDtorReg_ moves an unhooked row to category compiler. Old names are then
replaced as whole words in src/, re/*.md and TODO.md (survey names such
as AI_sub_550a30 are unique, so this is safe). Run afterwards:
  python tools/whole_game.py fix-names
  python tools/inventory.py check
"""

import argparse
import csv
import io
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INVENTORY = ROOT / "re" / "functions.csv"
NAME_RE = re.compile(r"^[A-Za-z_~][A-Za-z0-9_:~]*$")


def read_proposals(paths):
    proposals = {}
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for i, line in enumerate(f):
                line = line.rstrip("\r\n")
                if i == 0 or not line.strip():
                    continue
                cols = line.split("\t")
                if len(cols) < 5:
                    print(f"{path}:{i + 1}: too few columns, skipped", file=sys.stderr)
                    continue
                address, old, new, conf, evidence = (c.strip() for c in cols[:5])
                address = "0x%08x" % int(address, 16)
                if conf not in ("high", "medium"):
                    print(f"{path}:{i + 1}: confidence {conf!r}, skipped", file=sys.stderr)
                    continue
                if not NAME_RE.match(new):
                    print(f"{path}:{i + 1}: bad name {new!r}, skipped", file=sys.stderr)
                    continue
                if address in proposals:
                    print(f"{path}:{i + 1}: {address} proposed twice, first kept",
                          file=sys.stderr)
                    continue
                proposals[address] = (old, new, conf, evidence)
    return proposals


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--tag", default="naming pass")
    args = parser.parse_args()

    proposals = read_proposals(args.files)
    text = INVENTORY.read_text(encoding="utf-8")
    rows = list(csv.DictReader(io.StringIO(text)))
    columns = list(rows[0])
    used = {r["name"] for r in rows}
    renames = {}
    applied = skipped = 0
    for row in rows:
        p = proposals.get(row["address"])
        if not p:
            continue
        old, new, conf, evidence = p
        if row["name"] != old:
            print(f"{row['address']}: name is {row['name']!r}, not {old!r}; skipped",
                  file=sys.stderr)
            skipped += 1
            continue
        if new != old and new in used:
            new = f"{new}_{int(row['address'], 16):x}"
            if new in used:
                print(f"{row['address']}: {new} taken; skipped", file=sys.stderr)
                skipped += 1
                continue
        used.discard(old)
        used.add(new)
        if new != old:
            renames[old] = new
        row["name"] = new
        if conf == "high" and row["status"] == "classified":
            row["status"] = "identified"
        # Static initializers of globals are compiler-generated code.
        if new.startswith(("StaticInit_", "StaticDtorReg_")) and row["hook"] == "none":
            row["category"] = "compiler"
        evidence = evidence.rstrip()
        if evidence and evidence[-1] not in ".!?":
            evidence += "."
        prefix = f"Named ({args.tag}, {conf}): {evidence}"
        if new != old:
            prefix += f" Was {old}."
        row["notes"] = prefix + (" " + row["notes"] if row["notes"] else "")
        applied += 1
    print(f"applied {applied}, skipped {skipped}, renamed {len(renames)}")
    if args.dry_run:
        return

    buf = io.StringIO()
    writer = csv.DictWriter(buf, fieldnames=columns, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    INVENTORY.write_text(buf.getvalue(), encoding="utf-8", newline="\n")

    if not renames:
        return
    pattern = re.compile(r"(?<![A-Za-z0-9_:])(" + "|".join(
        re.escape(o) for o in sorted(renames, key=len, reverse=True)) + r")(?![A-Za-z0-9_])")
    targets = [p for p in (ROOT / "src").rglob("*") if p.suffix in (".cpp", ".h")]
    targets += list((ROOT / "re").glob("*.md")) + [ROOT / "TODO.md"]
    for path in targets:
        old_text = path.read_text(encoding="utf-8")
        new_text = pattern.sub(lambda m: renames[m.group(1)], old_text)
        if new_text != old_text:
            path.write_text(new_text, encoding="utf-8", newline="\n")
            print(f"updated references in {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
