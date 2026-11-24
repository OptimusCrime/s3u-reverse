#!/usr/bin/env python3
"""Style lint for the reimplementation (docs/coding_style.md, "Checks").

Rules:

  narrowing    (enforced: `python tools/inventory.py check` fails on it)
      `static_cast<E>(x)` where E is an enum class narrower than x
      (A2's SiteKindOf, session 48: a 16-bit building type 270 cast to the
      uint8 BuildingType became Vineyard). coding_style.md: never narrow a
      wider value into an enum; switch on the integer with
      `case Value(E::X):` instead. The width of x is resolved from its
      declaration: an integer literal or constexpr constant, a cast, a
      local variable or parameter of the same file, a struct field of that
      name (any header or source of ours, e.g. layout.h; the widest if
      several structs have one), a GameGlobal, or a function of ours with a
      declared return type. An expression whose width can't be resolved is
      reported too ("width unknown"): give it a typed local, or mark a
      deliberate cast with `narrow-ok` and the reason in a comment on the
      same line (e.g. a value that is known to be < 256), or, in a file
      another batch is converting, list it in KNOWN_NARROWING below. Enums
      of 32 bits are skipped (nothing of ours is wider).

  hex-compare  (report only)
      a hex literal compared with `==`, `!=`, `<`, `<=`, `>`, `>=` or used
      as a `case` label. Decimal is the default for values; hex stays for
      masks, marker words and nibble-coded bytes (coding_style.md section 4),
      so a hit is a candidate, not necessarily wrong. Comparisons of a
      masked value (`(x & 0xf0) == 0x30`) and the marker words 0xff,
      0xffff, 0xffffffff, 0x80000000 and 0x7fffffff, addresses (0x400000
      and up) and lines with static_assert/offsetof/OffsetOf/sizeof (struct
      offsets and sizes) are not reported. One hit per line.

  magic-category  (report only)
      a number compared with a job, settler/building type, good or race:
      `JobOf(s) == 12`, `.job == 0x135`, `TypeOf(b) != 3`, `.art == 5`,
      `GoodOf(p) == 31`, `.good`, `.race`. Use the enums of game/enums/
      (`== SettlerJob::...`, `Value(...)`). `case` labels with a number
      inside a switch on such a value are reported too.

The two report-only rules are not part of `inventory.py check` while the
readability pass converts code (session 49): the other conversion batches
change exactly the lines they flag, so a baseline of today's hits would be
stale after every merge and fail merges for lines nobody touched. Run them
by hand and watch the counts go down; once the pass is done they can become
enforced (with `noqa-style` for the lines that should stay).

Usage:
  python tools/style_lint.py                  counts per rule and file
  python tools/style_lint.py --list [RULE]    every hit (of one rule)
  python tools/style_lint.py --strict         exit code 1 on any hit
  python tools/style_lint.py --self-test      checks the narrowing rule
(Without --strict the exit code is 1 only for hits of the enforced rule.)
A line can opt out with `narrow-ok` (narrowing) or `noqa-style` (the
report-only rules) in a comment.
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ROOT / "src" / "s3re"

RULES = ("narrowing", "hex-compare", "magic-category")
ENFORCED = ("narrowing",)

TYPE_BITS = {
    "bool": 8, "char": 8, "signed char": 8, "unsigned char": 8, "BYTE": 8,
    "uint8_t": 8, "int8_t": 8, "std::uint8_t": 8, "std::int8_t": 8,
    "short": 16, "unsigned short": 16, "WORD": 16, "wchar_t": 16,
    "uint16_t": 16, "int16_t": 16, "std::uint16_t": 16, "std::int16_t": 16,
    "int": 32, "unsigned": 32, "unsigned int": 32, "long": 32, "unsigned long": 32, "DWORD": 32,
    "uint32_t": 32, "int32_t": 32, "std::uint32_t": 32, "std::int32_t": 32, "LONG": 32, "UINT": 32,
    "size_t": 32, "std::size_t": 32, "uintptr_t": 32, "std::uintptr_t": 32, "intptr_t": 32,
    "std::intptr_t": 32, "ptrdiff_t": 32, "std::ptrdiff_t": 32, "float": 32,
    "int64_t": 64, "uint64_t": 64, "std::int64_t": 64, "std::uint64_t": 64, "long long": 64,
    "unsigned long long": 64, "double": 64,
}

ENUM_DECL = re.compile(r"\benum\s+class\s+(\w+)\s*:\s*([\w:]+(?:\s+\w+)?)")
ALIAS = re.compile(r"\busing\s+(\w+)\s*=\s*(?:\w+::)*(\w+)\s*;")
CAST = re.compile(r"\bstatic_cast\s*<\s*(?:const\s+)?((?:\w+::)*\w+)\s*>\s*\(")
TYPE_NAME = (r"(?:const\s+|volatile\s+|static\s+|inline\s+|constexpr\s+)*"
             r"((?:unsigned\s+|signed\s+)?(?:long\s+)?(?:\w+::)*\w+)")
NOT_TYPES = {"return", "case", "else", "new", "delete", "using", "typedef", "goto", "throw"}


def code_of(line: str) -> str:
    """The line without its // comment and with string/char literals blanked."""
    line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(?:\\.|[^'\\])+'", "'c'", line)
    return line.split("//", 1)[0]


def balanced(text: str, start: int) -> str | None:
    """The text from `start` (just after an opening parenthesis) up to the
    matching closing one, or None if it isn't on this line."""
    depth = 1
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start:i]
    return None


class Widths:
    """Bit widths of the types, enums, fields, constants, globals and
    functions of our sources."""

    FIELD = re.compile(r"^\s*" + TYPE_NAME + r"\s+(\w+)\s*(?:\[[^\]]*\]\s*)*(?::\s*\d+\s*)?"
                       r"(?:=[^;]*|\{[^}]*\})?;")
    FUNCTION = re.compile(r"^\s*(?:static\s+|inline\s+|constexpr\s+|virtual\s+)*" + TYPE_NAME
                          + r"\s+(?:__\w+\s+)?(?:\w+::)*(\w+)\s*\([^()]*\)\s*(?:const\s*)?(?:\{|;|$)")
    CONSTANT = re.compile(r"\bconstexpr\s+" + TYPE_NAME + r"\s+(\w+)\s*(?:=|\{)\s*(0x[0-9a-fA-F]+|\d+)\b")
    STRUCT = re.compile(r"\b(?:struct|class)\s+(?:alignas\(\d+\)\s+)?(\w+)\s*(?:final\s*)?(?::[^{;]*)?\{")
    GLOBAL = re.compile(r"GameGlobal<\s*(?:const\s+)?((?:\w+::)*\w+)[^>]*>\s+(\w+)\s*\{")

    def __init__(self, files: dict[str, list[str]]):
        self.enums: dict[str, int] = {}
        for lines in files.values():
            for line in lines:
                match = ENUM_DECL.search(line)
                if match and match.group(2) in TYPE_BITS:
                    self.enums[match.group(1)] = TYPE_BITS[match.group(2)]
        # name -> widths (None: not an integer type we know)
        self.fields: dict[str, set] = collections.defaultdict(set)
        self.functions: dict[str, set] = collections.defaultdict(set)
        self.globals: dict[str, set] = collections.defaultdict(set)
        self.constants: dict[str, set] = collections.defaultdict(set)
        # struct name -> field name -> width, from `struct NAME {` .. its closing brace
        self.structs: dict[str, dict[str, int | None]] = collections.defaultdict(dict)
        for lines in files.values():
            stack: list[tuple[str | None, int]] = []  # (struct name or None, depth it opened at)
            depth = 0
            for line in lines:
                code = code_of(line)
                opened = self.STRUCT.search(code)
                if opened and "{" in code[opened.end() - 1:] and ";" not in code[opened.end():]:
                    stack.append((opened.group(1), depth))
                current = stack[-1][0] if stack and depth == stack[-1][1] + 1 else None
                match = self.FIELD.match(code)
                if current and match and match.group(1) not in NOT_TYPES:
                    self.structs[current][match.group(2)] = self.bits(match.group(1))
                depth += code.count("{") - code.count("}")
                while stack and depth <= stack[-1][1]:
                    stack.pop()
        for lines in files.values():
            for line in lines:
                code = code_of(line)
                for match in self.CONSTANT.finditer(code):
                    self.constants[match.group(2)].add(int(match.group(3), 0))
                match = self.GLOBAL.search(code)
                if match:
                    self.globals[match.group(2)].add(self.bits(match.group(1)))
                    continue
                match = self.FIELD.match(code)
                if match and match.group(1) not in NOT_TYPES:
                    self.fields[match.group(2)].add(self.bits(match.group(1)))
                    continue
                match = self.FUNCTION.match(code)
                if match and match.group(1) not in NOT_TYPES:
                    self.functions[match.group(2)].add(self.bits(match.group(1)))

    def bits(self, type_name: str) -> int | None:
        type_name = re.sub(r"\s+", " ", type_name.strip())
        if type_name in TYPE_BITS:
            return TYPE_BITS[type_name]
        return self.enums.get(type_name.split("::")[-1])


def literal_bits(value: int) -> int:
    value = abs(value)
    return 8 if value < 256 else 16 if value < 65536 else 32


class FileContext:
    """Enum aliases and local declarations of one file."""

    def __init__(self, widths: Widths, lines: list[str]):
        self.widths = widths
        self.lines = lines
        self.aliases: list[tuple[int, str, str]] = []
        for number, line in enumerate(lines):
            for match in ALIAS.finditer(code_of(line)):
                self.aliases.append((number, match.group(1), match.group(2)))

    def enum_bits(self, name: str, number: int) -> int | None:
        """Width of the enum `name` (or an alias of one) at line `number`;
        None if it is not an enum."""
        target = name.split("::")[-1]
        for line, alias, enum in self.aliases:
            if line <= number and alias == name.split("::")[-1]:
                target = enum
        return self.widths.enums.get(target)

    def local_bits(self, name: str, number: int) -> int | None:
        """Width of the nearest earlier declaration of `name` in this file
        (a local or a parameter), or None."""
        declaration = re.compile(TYPE_NAME + r"\s*[&*]?\s*\b" + re.escape(name) + r"\b\s*(?:=|;|,|\)|\[|\{|:)")
        for i in range(number, max(-1, number - 400), -1):
            code = code_of(self.lines[i])
            for match in declaration.finditer(code):
                type_name = match.group(1)
                if type_name == "auto":
                    return self.expression_bits(code[match.end():].split(";")[0], i)
                if type_name in NOT_TYPES:
                    continue
                bits = self.widths.bits(type_name)
                if bits is None:
                    bits = self.enum_bits(type_name, i)
                if bits is not None:
                    return bits
        return None

    def expression_bits(self, expression: str, number: int) -> int | None:
        """Width of an expression, or None when it can't be resolved."""
        expression = expression.strip()
        while expression.startswith("(") and balanced(expression, 1) == expression[1:-1]:
            expression = expression[1:-1].strip()
        literal = re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)[uUlL]*", expression)
        if literal:
            return literal_bits(int(literal.group(1), 0))
        cast = re.match(r"static_cast\s*<\s*(?:const\s+)?((?:\w+::)*\w+)\s*>\s*\(", expression)
        if cast and balanced(expression, cast.end()) == expression[cast.end():-1]:
            bits = self.widths.bits(cast.group(1))
            return bits if bits is not None else self.enum_bits(cast.group(1), number)
        value = re.match(r"Value\s*\(", expression)
        if value and balanced(expression, value.end()) == expression[value.end():-1]:
            enum = re.match(r"((?:\w+::)*\w+)::\w+$", expression[value.end():-1].strip())
            return self.enum_bits(enum.group(1), number) if enum else None
        call = re.fullmatch(r"(?:[\w.\->\[\]]*?(?:\.|->|::))?(\w+)\s*\((.*)\)", expression)
        if call and balanced(expression, expression.index("(") + 1) == call.group(2):
            if call.group(1) in ("Below", "Range"):  # the test RNG: bounded by its (last) argument
                return self.expression_bits(call.group(2).split(",")[-1], number)
            widths = self.widths.functions.get(call.group(1), set()) - {None}
            return max(widths) if widths else None
        compact = expression.replace(" ", "")
        if re.search(r"[+\-*/%|^&<>?~!]", compact.replace("->", ".")):
            return None  # arithmetic: at least int, not resolved
        name = re.fullmatch(r"(?:[\w\[\]().]*?(?:\.|->|::))?(\w+)((?:\[[^\]]*\])*)", compact)
        if not name:
            return None
        last = name.group(1)
        prefix = compact[:name.start(1)]
        is_member = prefix.endswith((".", "->"))
        if not is_member:
            bits = self.local_bits(last, number)
            if bits is not None:
                return bits
            values = self.widths.constants.get(last)
            if values:
                return literal_bits(max(values))
            widths = self.widths.globals.get(last, set()) - {None}
            if widths:
                return max(widths)
        if is_member:
            owner = re.fullmatch(r"(\w+)(?:\[[^\]]*\])*(?:\.|->)", prefix)
            struct = self.declared_type(owner.group(1), number) if owner else None
            if struct in self.widths.structs and last in self.widths.structs[struct]:
                return self.widths.structs[struct][last]
        widths = self.widths.fields.get(last, set()) - {None}
        if widths:
            return max(widths)  # a field of that name in any struct: the widest
        return None

    def declared_type(self, name: str, number: int) -> str | None:
        """The (last component of the) type name of the nearest earlier
        declaration of `name`, e.g. Settler for `layout::Settler& settler`."""
        declaration = re.compile(TYPE_NAME + r"\s*[&*]?\s*(?:const\s*)?[&*]?\s*\b" + re.escape(name)
                                 + r"\b\s*(?:=|;|,|\)|\[|\{|:)")
        for i in range(number, max(-1, number - 400), -1):
            for match in declaration.finditer(code_of(self.lines[i])):
                if match.group(1) not in NOT_TYPES and match.group(1) != "auto":
                    return match.group(1).split("::")[-1]
        return None


HEX_COMPARE = re.compile(r"(==|!=|<=|>=|(?<![<>-])<(?![<=])|(?<![<>-])>(?![>=]))\s*(0x[0-9a-fA-F]+)\b"
                         r"|\b(0x[0-9a-fA-F]+)[uUlL]*\s*(==|!=|<=|>=|<(?![<=])|>(?![>=]))"
                         r"|\bcase\s+(0x[0-9a-fA-F]+)\b")
# Struct offsets and sizes stay hex (coding_style.md section 4).
OFFSET_LINE = re.compile(r"\b(static_assert|offsetof|OffsetOf|sizeof)\b")
MARKERS = {0xFF, 0xFFFF, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF}
CATEGORY = (r"(?:\b(?:JobOf|TypeOf|GoodOf|RaceOf|KindOf|ClassOf)\s*\([^()]*\)"
            r"|(?:\.|->)(?:job|art|good|race)\b)")
NUMBER = r"(?:0x[0-9a-fA-F]+|\d+)\b"
MAGIC = re.compile(CATEGORY + r"\s*(?:==|!=|<=|>=|<|>)\s*" + NUMBER
                   + r"|\b" + NUMBER + r"\s*(?:==|!=|<=|>=|<|>)\s*" + CATEGORY)
SWITCH = re.compile(r"\bswitch\s*\((.*)\)\s*\{?\s*$")
CASE_NUMBER = re.compile(r"\bcase\s+(" + NUMBER + r")\s*:")


# Narrowing casts that are known to be safe, kept here instead of a
# `narrow-ok` comment in files other batches are converting: (file, the cast
# as reported) -> why.
KNOWN_NARROWING = {
    ("src/s3re/game/settlers/jobs/settler_jobs_units.cpp", "static_cast<S>(ctx.type)"):
        "ArriveAtWork returns early for ctx.type > 47 right before the switch (0x4ead38)",
}


def source_files() -> dict[str, list[str]]:
    files = {}
    for path in sorted(SOURCES.rglob("*")):
        if path.suffix in (".cpp", ".h"):
            files[path.relative_to(SOURCES).as_posix()] = path.read_text(
                encoding="utf-8", errors="replace").splitlines()
    return files


def scan(files: dict[str, list[str]] | None = None) -> list[tuple[str, str, int, str]]:
    """(rule, file, line, message) for every hit."""
    if files is None:
        files = source_files()
    widths = Widths(files)
    hits = []
    for relative, lines in files.items():
        context = FileContext(widths, lines)
        where = f"src/s3re/{relative}"
        category_switch_depth = None  # brace depth inside a switch on a category value
        depth = 0
        for number, line in enumerate(lines):
            code = code_of(line)
            # -- narrowing
            if "narrow-ok" not in line:
                for match in CAST.finditer(code):
                    enum_bits = context.enum_bits(match.group(1), number)
                    if enum_bits is None or enum_bits >= 32:
                        continue  # not an enum, or nothing of ours is wider
                    argument = balanced(code, match.end())
                    if argument is None:
                        continue
                    if (where, f"static_cast<{match.group(1)}>({argument.strip()})") in KNOWN_NARROWING:
                        continue
                    bits = context.expression_bits(argument, number)
                    if bits is None:
                        hits.append(("narrowing", where, number + 1,
                                     f"static_cast<{match.group(1)}>({argument.strip()}): width unknown, "
                                     f"{match.group(1)} has {enum_bits} bits"))
                    elif bits > enum_bits:
                        hits.append(("narrowing", where, number + 1,
                                     f"static_cast<{match.group(1)}>({argument.strip()}): {bits}-bit value "
                                     f"into a {enum_bits}-bit enum"))
            if "noqa-style" not in line:
                # -- hex-compare (once per line)
                for match in ([] if OFFSET_LINE.search(code) else HEX_COMPARE.finditer(code)):
                    literal = match.group(2) or match.group(3) or match.group(5)
                    if int(literal, 16) in MARKERS or int(literal, 16) >= 0x400000:
                        continue  # a marker word, or an address of S3.EXE
                    before = code[:match.start()]
                    if re.search(r"&\s*(0x[0-9a-fA-F]+|\w+)\s*\)\s*$", before) or \
                            re.search(r"&\s*0x[0-9a-fA-F]+\s*$", before):
                        continue  # a masked value
                    hits.append(("hex-compare", where, number + 1, code.strip()))
                    break
                # -- magic-category
                if MAGIC.search(code):
                    hits.append(("magic-category", where, number + 1, code.strip()))
                switch = SWITCH.search(code)
                if switch and category_switch_depth is None and re.search(CATEGORY + r"\s*\)?\s*$", switch.group(1)):
                    category_switch_depth = depth + 1
                if category_switch_depth is not None:
                    for match in CASE_NUMBER.finditer(code):
                        hits.append(("magic-category", where, number + 1,
                                     f"case {match.group(1)}: in a switch on a job/type/good/race"))
            depth += code.count("{") - code.count("}")
            if category_switch_depth is not None and depth < category_switch_depth and "{" not in code:
                category_switch_depth = None
    return hits


def enforced_findings() -> list[str]:
    """The hits of the enforced rules, as messages (for inventory.py check)."""
    return [f"{file}:{line}: {message} (style_lint {rule})"
            for rule, file, line, message in scan() if rule in ENFORCED]


SELF_TEST = {
    # file -> (lines, expected narrowing hit lines)
    "game/enums/fake.h": (["enum class Kind : std::uint8_t { A };", "struct Rec {", "    std::uint8_t art;",
                           "    std::uint16_t word;", "};"], []),
    "fake.cpp": ([
        "void F(const Rec& rec, std::uint32_t wide, std::uint8_t narrow) {",
        "    using K = Kind;",
        "    const std::uint16_t type = rec.word;",
        "    auto a = static_cast<Kind>(type);",            # 4: 16 -> 8
        "    auto b = static_cast<Kind>(narrow);",
        "    auto c = static_cast<Kind>(rec.art);",
        "    auto d = static_cast<K>(wide);",               # 7: alias, 32 -> 8
        "    auto e = static_cast<Kind>(rec.word);",        # 8: field 16 -> 8
        "    auto f = static_cast<Kind>(rng.Below(300));",  # 9: bounded by 300
        "    auto g = static_cast<Kind>(rng.Below(47));",
        "    auto h = static_cast<Kind>(Unknown(wide));",   # 11: width unknown
        "    auto i = static_cast<Kind>(wide);  // narrow-ok: checked above",
        "    auto j = static_cast<Kind>(200);",
        "}"], [4, 7, 8, 9, 11]),
}


def self_test() -> int:
    files = {name: lines for name, (lines, _) in SELF_TEST.items()}
    hits = scan(files)
    failed = 0
    for name, (_, expected) in SELF_TEST.items():
        found = sorted(line for rule, file, line, _ in hits if rule == "narrowing" and file == f"src/s3re/{name}")
        if found != expected:
            print(f"self-test FAILED for {name}: narrowing hits at {found}, expected {expected}")
            failed += 1
    print("style lint self-test: " + ("ok" if not failed else "FAILED"))
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Style lint (docs/coding_style.md, Checks).")
    parser.add_argument("--list", nargs="?", const="all", metavar="RULE",
                        help="list every hit (of one rule)")
    parser.add_argument("--strict", action="store_true", help="exit code 1 on any hit")
    parser.add_argument("--self-test", action="store_true", help="check the narrowing rule on built-in cases")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    hits = scan()
    if args.list:
        for rule, file, line, message in hits:
            if args.list in ("all", rule):
                print(f"{file}:{line}: [{rule}] {message}")
    else:
        per_file: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
        for rule, file, _, _ in hits:
            per_file[file][rule] += 1
        print(f"{'file':64}" + "".join(f"{r:>16}" for r in RULES))
        for file in sorted(per_file, key=lambda f: (-sum(per_file[f].values()), f)):
            print(f"{file:64}" + "".join(f"{per_file[file][r]:>16}" for r in RULES))
    totals = collections.Counter(rule for rule, _, _, _ in hits)
    print("style lint: " + ", ".join(f"{rule} {totals[rule]}" + (" (enforced)" if rule in ENFORCED else "")
                                     for rule in RULES))
    if args.strict:
        return 1 if hits else 0
    return 1 if any(totals[rule] for rule in ENFORCED) else 0


if __name__ == "__main__":
    sys.exit(main())
