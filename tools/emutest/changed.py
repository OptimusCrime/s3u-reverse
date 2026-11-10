"""Which emulator specs a change can affect (run.py --changed).

A spec is selected when
  - a source file that one of the objects behind its replacement was
    compiled from changed (the .cpp or any header it includes), or
  - its definition in specs.py changed (the Spec(...) entry or any top-level
    helper, constant or generator it uses, transitively; comments don't
    count), or
  - the harness or the build setup changed (tools/emutest/emu.py, run.py,
    this file, tools/requirements.txt, CMake files, the function lists in
    re/ that emu.py reads): then every spec is selected and the self-test
    should run too.

"The objects behind a replacement" come from the build, not from guessing:
s3re.map (from the build that is tested) names the object file of every
symbol; starting at our function, every symbol its code refers to (direct
calls and jumps, addresses in operands, function pointers in data it refers
to) is followed, so a replacement that calls a helper or reads a table in
another .cpp depends on that file too. The headers each object includes
come from MSBuild's compiler tracking log (build/x86/src/s3re/s3re.dir/
<config>/s3re.tlog/CL.read.*.tlog, UTF-16, one "^source" line per
object followed by every file it read). Source files under src/s3re that no
object read (e.g. a new file not built yet) select everything.

"Changed" means: changed between REF and the working tree (git diff REF,
plus untracked files). Without --since, REF is the commit of the last full
clean run (build/emutest_last_full.json, written by run.py after a full run
with 0 mismatches; specs with incomplete coverage are listed there); files that were uncommitted at that
run count as changed, and specs.py is compared with the copy that run used.
"""

from __future__ import annotations

import ast
import bisect
import datetime
import glob
import hashlib
import json
import os
import re
import subprocess

import capstone
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LAST_FULL_PATH = os.path.join(ROOT, "build", "emutest_last_full.json")
SPECS_PATH = "tools/emutest/specs.py"

# Changes to these select every spec (and call for the self-test).
HARNESS_FILES = {
    "tools/emutest/emu.py", "tools/emutest/run.py", "tools/emutest/changed.py",
    "tools/requirements.txt", "CMakeLists.txt", "CMakePresets.json",
    "src/s3re/CMakeLists.txt", "re/ghidra_functions.tsv", "re/extra_functions.tsv",
}
HARNESS_PREFIXES = ("cmake/",)


# -- git -----------------------------------------------------------------------

def _git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", check=True).stdout


def head_commit() -> str:
    return _git("rev-parse", "HEAD").strip()


def files_changed_since(ref: str) -> set[str]:
    """Paths (relative, with /) that differ between ref and the working tree,
    plus untracked files."""
    changed = set(_git("diff", "--name-only", ref).split())
    changed |= set(_git("ls-files", "--others", "--exclude-standard").split())
    return changed


def dirty_files() -> list[str]:
    return sorted(files_changed_since("HEAD"))


# -- specs.py units ------------------------------------------------------------

def _definitions(tree: ast.Module) -> dict[str, list[ast.stmt]]:
    """Top-level name -> the statements that define (or extend) it."""
    defined: dict[str, list[ast.stmt]] = {}
    for statement in tree.body:
        names = []
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            names = [statement.name]
        elif isinstance(statement, (ast.Assign, ast.AnnAssign, ast.AugAssign)):
            targets = statement.targets if isinstance(statement, ast.Assign) else [statement.target]
            for target in targets:
                names += [n.id for n in ast.walk(target) if isinstance(n, ast.Name)]
        elif isinstance(statement, (ast.Import, ast.ImportFrom)):
            names = [(a.asname or a.name).split(".")[0] for a in statement.names]
        for name in names:
            defined.setdefault(name, []).append(statement)
    return defined


def spec_units(source: str) -> list[tuple[ast.expr, str]]:
    """The entries of SPECS (each element of SPECS = [...] / SPECS += [...]),
    with a fingerprint: the entry's code and the code of every top-level
    definition it uses, transitively (without comments)."""
    tree = ast.parse(source)
    defined = _definitions(tree)
    memo: dict[str, str] = {}

    def closure(node: ast.AST) -> set[str]:
        seen: set[str] = set()
        todo = [n.id for n in ast.walk(node) if isinstance(n, ast.Name)]
        while todo:
            name = todo.pop()
            if name in seen or name in ("SPECS", "SELF_TEST") or name not in defined:
                continue
            seen.add(name)
            for statement in defined[name]:
                todo += [n.id for n in ast.walk(statement) if isinstance(n, ast.Name)]
        return seen

    def text(name: str) -> str:
        if name not in memo:
            memo[name] = "\n".join(ast.unparse(s) for s in defined[name])
        return memo[name]

    units = []
    for statement in tree.body:
        if isinstance(statement, (ast.Assign, ast.AugAssign)) and isinstance(statement.value, ast.List):
            targets = statement.targets if isinstance(statement, ast.Assign) else [statement.target]
            if not any(isinstance(t, ast.Name) and t.id == "SPECS" for t in targets):
                continue
            for element in statement.value.elts:
                parts = [ast.unparse(element)] + [text(n) for n in sorted(closure(element))]
                units.append((element, hashlib.sha1("\n\0".join(parts).encode()).hexdigest()))
    return units


def unit_fingerprints(source: str) -> list[str]:
    return [fingerprint for _, fingerprint in spec_units(source)]


def changed_spec_names(specs_module, old_fingerprints: list[str]) -> set[str]:
    """`ours` names of the current specs whose entry in specs.py is new or
    changed (its fingerprint is not among old_fingerprints)."""
    old: dict[str, int] = {}
    for fingerprint in old_fingerprints:
        old[fingerprint] = old.get(fingerprint, 0) + 1
    names: set[str] = set()
    namespace = vars(specs_module)
    current = open(os.path.join(ROOT, SPECS_PATH), encoding="utf-8").read()
    for element, fingerprint in spec_units(current):
        if old.get(fingerprint, 0) > 0:
            old[fingerprint] -= 1
            continue
        # Which specs this entry makes (a Spec(...) or a *[... for ...]).
        value = element.value if isinstance(element, ast.Starred) else element
        produced = eval(compile(ast.Expression(value), SPECS_PATH, "eval"), namespace)  # noqa: S307
        for spec in (list(produced) if isinstance(element, ast.Starred) else [produced]):
            names.add(spec.ours)
    return names


# -- objects behind each replacement --------------------------------------------

_MAP_LINE = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$",
                       re.I)


class DllReferences:
    """The symbols of s3re.dll and what each one refers to."""

    def __init__(self, dll_path: str, map_path: str):
        pe = pefile.PE(dll_path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.image = pe.get_memory_mapped_image()
        self.text = [(self.base + s.VirtualAddress, self.base + s.VirtualAddress + s.Misc_VirtualSize)
                     for s in pe.sections if s.Characteristics & 0x20000000]  # executable
        symbols = {}
        functions = set()
        for line in open(map_path, encoding="utf-8", errors="replace"):
            match = _MAP_LINE.match(line)
            if match:
                address = int(match.group(2), 16)
                if address >= self.base:
                    symbols.setdefault(address, (match.group(1), match.group(3).lower()))
                    if re.search(r"\s[0-9a-f]{8}\s+f\s", line, re.I):
                        functions.add(address)
        self.addresses = sorted(symbols)
        self.symbols = symbols
        self.functions = functions
        self.disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.disassembler.detail = True
        self._refs: dict[int, set[int]] = {}

    def symbol_at(self, address: int) -> int | None:
        i = bisect.bisect_right(self.addresses, address) - 1
        return self.addresses[i] if i >= 0 else None

    def object_of(self, symbol: int) -> str:
        return self.symbols[symbol][1]

    def _in_image(self, value: int) -> bool:
        return self.base <= value < self.base + len(self.image)

    def _in_text(self, value: int) -> bool:
        return any(low <= value < high for low, high in self.text)

    def _bytes(self, symbol: int, limit: int = 0x10000) -> bytes:
        i = bisect.bisect_right(self.addresses, symbol)
        end = self.addresses[i] if i < len(self.addresses) else self.base + len(self.image)
        return self.image[symbol - self.base:min(end, symbol + limit) - self.base]

    def _resolve(self, value: int) -> int | None:
        """The symbol `value` points into; an incremental-linking jump thunk
        (jmp rel32 at a non-symbol address) is followed to its target."""
        if not self._in_image(value):
            return None
        if value not in self.symbols and self._in_text(value) and self.image[value - self.base] == 0xE9:
            value = (value + 5 + int.from_bytes(self.image[value - self.base + 1:value - self.base + 5],
                                                "little", signed=True)) & 0xFFFFFFFF
            if not self._in_image(value):
                return None
        return self.symbol_at(value)

    def references(self, symbol: int) -> set[int]:
        if symbol in self._refs:
            return self._refs[symbol]
        found: set[int] = set()
        code = self._bytes(symbol)
        if symbol in self.functions or self._in_text(symbol):
            for ins in self.disassembler.disasm(code, symbol):
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_IMM:
                        target = self._resolve(op.imm & 0xFFFFFFFF)
                    elif op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0:
                        target = self._resolve(op.mem.disp & 0xFFFFFFFF)
                    else:
                        continue
                    if target is not None:
                        found.add(target)
                if ins.mnemonic in ("ret", "int3") and ins.address + ins.size >= symbol + len(code):
                    break
        else:  # data: pointers it holds (tables of functions, strings, ...)
            for offset in range(0, len(code) - 3, 4):
                target = self._resolve(int.from_bytes(code[offset:offset + 4], "little"))
                if target is not None:
                    found.add(target)
        found.discard(symbol)
        self._refs[symbol] = found
        return found

    def objects_reached(self, start: int, own_objects: set[str]) -> set[str]:
        """Our object files (in own_objects) whose code or data the function
        at `start` can reach. Library objects (the C runtime) are not
        followed."""
        start_symbol = self.symbol_at(start)
        todo = [start_symbol]
        seen = {start_symbol}
        objects = set()
        while todo:
            symbol = todo.pop()
            obj = self.object_of(symbol)
            if obj not in own_objects:
                continue
            objects.add(obj)
            for target in self.references(symbol):
                if target not in seen:
                    seen.add(target)
                    todo.append(target)
        return objects


def object_sources(bin_dir: str) -> dict[str, set[str]]:
    """Object file name (lower case) -> the repository files (relative, with
    /, lower case) it was compiled from, headers included."""
    config = os.path.basename(os.path.normpath(bin_dir))
    build_root = os.path.dirname(os.path.dirname(os.path.normpath(bin_dir)))  # build/x86
    logs = glob.glob(os.path.join(build_root, "src", "s3re", "s3re.dir", config, "s3re.tlog",
                                  "CL.read.*.tlog"))
    if not logs:
        raise FileNotFoundError(f"no compiler tracking log for {bin_dir} (build with the "
                                "Visual Studio generator first)")
    objects: dict[str, set[str]] = {}
    source_root = None
    for log in logs:
        current = None
        for line in open(log, encoding="utf-16", errors="replace").read().splitlines():
            line = line.strip().lstrip("﻿")
            if not line:
                continue
            if line.startswith("^"):
                path = line[1:].split("|")[0]
                if source_root is None and "\\SRC\\" in path.upper():
                    source_root = path.upper()[:path.upper().index("\\SRC\\") + 1]
                current = objects.setdefault(os.path.splitext(os.path.basename(path))[0].lower()
                                             + ".obj", set())
                current.add(path.upper())
            elif current is not None:
                current.add(line.upper())
    result = {}
    for obj, paths in objects.items():
        result[obj] = {p[len(source_root):].replace("\\", "/").lower()
                       for p in paths if source_root and p.startswith(source_root)}
    return result


# -- selection -------------------------------------------------------------------

def record_full_pass(build: str, incomplete: list[str]) -> None:
    """Called by run.py after a full run with no mismatches. `incomplete`:
    the specs whose coverage was incomplete (kept for the record; their
    coverage only changes when their inputs do)."""
    try:
        data = {"commit": head_commit(), "dirty": dirty_files(), "build": build,
                "incomplete_coverage": incomplete,
                "time": datetime.datetime.now().isoformat(timespec="seconds"),
                "specs_units": unit_fingerprints(
                    open(os.path.join(ROOT, SPECS_PATH), encoding="utf-8").read())}
        os.makedirs(os.path.dirname(LAST_FULL_PATH), exist_ok=True)
        with open(LAST_FULL_PATH, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1)
    except (OSError, subprocess.CalledProcessError):
        pass


def select(specs_module, machine_symbols, bin_dir: str, since: str | None):
    """Returns (names or None for all specs, harness_changed, description).
    machine_symbols(name) -> address of our function in the DLL."""
    if since is None:
        try:
            last = json.load(open(LAST_FULL_PATH, encoding="utf-8"))
        except (OSError, ValueError):
            return None, True, f"no full clean run recorded ({LAST_FULL_PATH}): all specs"
        ref = last["commit"]
        changed = files_changed_since(ref) | set(last.get("dirty", []))
        old_units = last.get("specs_units")
        label = f"since the last full clean run ({ref[:10]}, {last.get('time', '?')})"
    else:
        ref = since
        changed = files_changed_since(ref)
        old_units = None
        label = f"since {ref}"

    harness = sorted(p for p in changed if p in HARNESS_FILES or p.startswith(HARNESS_PREFIXES))
    if harness:
        return None, True, f"{label}: harness/build changed ({', '.join(harness)}): all specs"

    names: set[str] = set()
    reasons: list[str] = []
    if SPECS_PATH in changed:
        if old_units is None:
            try:
                old_units = unit_fingerprints(_git("show", f"{ref}:{SPECS_PATH}"))
            except subprocess.CalledProcessError:
                old_units = []  # specs.py didn't exist then
        changed_names = changed_spec_names(specs_module, old_units)
        names |= changed_names
        reasons.append(f"specs.py ({len(changed_names)} spec(s) new or changed)")

    sources = sorted(p for p in changed if p.startswith("src/"))
    if sources:
        objects = object_sources(bin_dir)
        compiled = set().union(*objects.values())
        unknown = [p for p in sources if p.lower() not in compiled and p.startswith("src/s3re/")
                   and os.path.exists(os.path.join(ROOT, p))]
        if unknown:
            return None, False, (f"{label}: {', '.join(unknown)} not in any object of the build "
                                 "(new or not built?): all specs")
        changed_lower = {p.lower() for p in sources}
        affected_objects = {obj for obj, paths in objects.items() if paths & changed_lower}
        if affected_objects:
            dll = DllReferences(os.path.join(bin_dir, "s3re.dll"), os.path.join(bin_dir, "s3re.map"))
            own = set(objects)
            for spec in specs_module.SPECS:
                if spec.ours in names:
                    continue
                if dll.objects_reached(machine_symbols(spec.ours), own) & affected_objects:
                    names.add(spec.ours)
        reasons.append(", ".join(p for p in sources if p.lower() in compiled) or "(sources not compiled)")
        newest = max((os.path.getmtime(os.path.join(ROOT, p)) for p in sources
                      if os.path.exists(os.path.join(ROOT, p))), default=0)
        if newest > os.path.getmtime(os.path.join(bin_dir, "s3re.dll")):
            reasons.append("WARNING: sources are newer than s3re.dll, rebuild first")
    description = f"{label}: changed files: {'; '.join(reasons) or 'none that affect specs'}"
    return names, False, description

