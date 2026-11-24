"""Which emulator specs a change can affect (run.py --changed).

Default (since 2026-11-23): selection by compiled code. A spec is selected
unless a pass is recorded for its current
  - code fingerprint: our replacement's compiled code in s3re.dll and that
    of every s3re.dll function and datum it reaches, with addresses
    normalized to symbol names (CodeFingerprints), so a header change that
    doesn't change the code (a new enum, a comment, moved lines) selects
    nothing, and a changed callee selects its callers' specs;
  - spec fingerprint: its entry and the helpers it uses (spec_units; the
    specs/ parts read as one text, specs_source);
  - harness fingerprint: emu.py, tools/requirements.txt, the function lists
    in re/, and the case-running functions of run.py (RESULT_FUNCTIONS).
Passes are recorded by run.py for every spec that ran with the standard
inputs (2000 cases, seed 1) with no mismatch and full coverage (or all its
cases run), in quick or full mode, in build/emutest_passes.json of the main
checkout, shared by all worktrees. A harness fingerprint that hasn't passed
the self-test yet also runs the self-test.

The older selection by source files (--since REF, or --changed
--by-headers) is kept below: a spec is selected when
  - a source file that one of the objects behind its replacement was
    compiled from changed (the .cpp or any header it includes), or
  - its definition in the specs changed (the Spec(...) entry or any top-level
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
run count as changed, and the specs are compared with the copy that run used.
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
SPECS_PATH = "tools/emutest/specs.py"  # the single file, until session 51
SPECS_DIR = "tools/emutest/specs"     # the package of parts since then (specs/__init__.py)

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

def _parts_of(init_source: str) -> list[str]:
    for statement in ast.parse(init_source).body:
        if isinstance(statement, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "PARTS"
                                                     for t in statement.targets):
            return list(ast.literal_eval(statement.value))
    raise ValueError("specs/__init__.py has no PARTS")


def specs_source(ref: str | None = None) -> str:
    """All spec code as one text: the parts of the specs package in PARTS
    order (they run in one namespace, so this is what the specs see), or
    the single specs.py of a commit from before the split. ref=None: the
    working tree. Raises CalledProcessError if neither exists at ref."""
    def read(path: str) -> str:
        if ref is None:
            return open(os.path.join(ROOT, path), encoding="utf-8").read()
        return _git("show", f"{ref}:{path}")
    try:
        parts = _parts_of(read(f"{SPECS_DIR}/__init__.py"))
    except (OSError, subprocess.CalledProcessError):
        return read(SPECS_PATH)
    return "\n".join(read(f"{SPECS_DIR}/{part}.py") for part in parts)


def specs_changed(paths) -> bool:
    return any(p == SPECS_PATH or p.startswith(SPECS_DIR + "/") for p in paths)


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
    current = specs_source()
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


_ANONYMOUS_NAMESPACE = re.compile(r"\?A0x[0-9a-f]{8}", re.I)


def stable_name(name: str) -> str:
    """A decorated name without the anonymous-namespace hash: MSVC names an
    anonymous namespace `?A0x` plus a hash of the source file's absolute
    path, so the same code built in another worktree (another path) got
    other names, and every replacement that reaches a symbol in an
    anonymous namespace another fingerprint (206 of 703 specs reselected in
    a fresh worktree, session 49). Two such symbols of the same name in
    different files still differ by their own digests."""
    return _ANONYMOUS_NAMESPACE.sub("?A0x", name)


class CodeFingerprints(DllReferences):
    """Fingerprints of the compiled code behind each replacement.

    A symbol's normalized form is its bytes (code: up to the next symbol,
    without the int 3 padding) with every address that depends on where the
    linker put things replaced by the name of the symbol it points into
    (plus the offset): the 32-bit values at the DLL's base relocations
    (absolute addresses in code and data, jump tables included) and the
    targets of relative calls and jumps that leave the symbol (followed
    through incremental-linking thunks). So moving a function, or a callee
    moving, doesn't change anything; changing an instruction, a constant, a
    string or a table does. Names are taken without MSVC's per-path
    anonymous-namespace hash (stable_name), so a build in another worktree
    gives the same fingerprints.

    A replacement's fingerprint hashes the normalized forms of every symbol
    of our own objects it can reach (calls, jumps, data references and the
    pointers in that data, transitively, as in DllReferences); library code
    (the C runtime) counts by name only."""

    def __init__(self, dll_path: str, map_path: str):
        super().__init__(dll_path, map_path)
        pe = pefile.PE(dll_path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
        self.relocations: list[int] = sorted(
            self.base + entry.rva
            for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", [])
            for entry in block.entries if entry.type == 3)  # IMAGE_REL_BASED_HIGHLOW
        self._digest: dict[int, tuple[str, set[int]]] = {}
        self._fingerprint: dict[int, str] = {}

    def _name(self, value: int, symbol: int) -> tuple[str, int | None]:
        """How an address in `symbol`'s bytes is written in the normalized
        form, and the symbol it refers to (None: same symbol / outside)."""
        if not self._in_image(value):
            return f"abs:{value:#x}", None
        target_value = value
        if value not in self.symbols and self._in_text(value) and self.image[value - self.base] == 0xE9:
            target_value = (value + 5 + int.from_bytes(self.image[value - self.base + 1:value - self.base + 5],
                                                       "little", signed=True)) & 0xFFFFFFFF
        target = self.symbol_at(target_value)
        if target is None:
            return f"abs:{value:#x}", None
        if target == symbol:
            return f"self+{target_value - target:#x}", None
        return f"{stable_name(self.symbols[target][0])}+{target_value - target:#x}", target

    def digest(self, symbol: int) -> tuple[str, set[int]]:
        """(hash of the normalized form, symbols it refers to)."""
        if symbol in self._digest:
            return self._digest[symbol]
        code = self._bytes(symbol)
        is_code = symbol in self.functions or self._in_text(symbol)
        if is_code:
            code = code.rstrip(b"\xcc")
        else:
            # Alignment padding up to the next symbol depends on the layout,
            # which differs between worktrees (stable_name: the symbols sort
            # differently): leave out up to 15 trailing zero bytes.
            kept = len(code.rstrip(b"\0"))
            code = code[:max(kept, len(code) - 15)]
        end = symbol + len(code)
        patches: dict[int, tuple[int, str]] = {}  # offset -> (length, replacement)
        refs: set[int] = set()
        low = bisect.bisect_left(self.relocations, symbol)
        high = bisect.bisect_left(self.relocations, end)
        relocated = set()
        values = {address: int.from_bytes(code[address - symbol:address - symbol + 4], "little")
                  for address in self.relocations[low:high] if address + 4 <= end}
        # A pointer one past the end of a table (a loop bound) equals the
        # start of whatever symbol the linker put next, which depends on the
        # layout: when the symbol before it is referenced too, name it as
        # that symbol's end instead.
        inside = {self.symbol_at(v) for v in values.values() if self._in_image(v)}
        for address, value in values.items():
            index = bisect.bisect_left(self.addresses, value)
            if (0 < index < len(self.addresses) and self.addresses[index] == value
                    and self.addresses[index - 1] in inside and self.addresses[index - 1] != symbol
                    and any(self.addresses[index - 1] <= v < value for v in values.values())):
                previous = self.addresses[index - 1]
                patches[address - symbol] = (4, f"{stable_name(self.symbols[previous][0])}+{value - previous:#x}")
                relocated.update(range(address, address + 4))
                refs.add(previous)
                continue
            name, target = self._name(value, symbol)
            patches[address - symbol] = (4, name)
            relocated.update(range(address, address + 4))
            if target is not None:
                refs.add(target)
        if is_code:
            for ins in self.disassembler.disasm(code, symbol):
                if not (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)):
                    continue
                if any(a in relocated for a in range(ins.address, ins.address + ins.size)):
                    continue
                op = ins.operands[0] if ins.operands else None
                if op is None or op.type != capstone.x86.X86_OP_IMM:
                    continue
                destination = op.imm & 0xFFFFFFFF
                if symbol <= destination < end:
                    continue  # a branch inside the function: position-independent
                size = 1 if ins.bytes[0] in (0xEB,) or 0x70 <= ins.bytes[0] <= 0x7F or \
                    ins.bytes[0] in (0xE0, 0xE1, 0xE2, 0xE3) else 4
                name, target = self._name(destination, symbol)
                patches[ins.address + ins.size - size - symbol] = (size, name)
                if target is not None:
                    refs.add(target)
        digest = hashlib.sha1()
        digest.update(b"code" if is_code else b"data")
        position = 0
        for offset in sorted(patches):
            length, name = patches[offset]
            if offset < position:
                continue
            digest.update(code[position:offset])
            digest.update(b"\0<" + name.encode() + b">\0")
            position = offset + length
        digest.update(code[position:])
        self._digest[symbol] = (digest.hexdigest(), refs)
        return self._digest[symbol]

    @staticmethod
    def own(obj: str) -> bool:
        """Our object files; library members are written "lib:member.obj"."""
        return obj.endswith(".obj") and ":" not in obj

    def fingerprint(self, start: int) -> str:
        """Fingerprint of the function at `start` and everything of ours it
        reaches."""
        start_symbol = self.symbol_at(start)
        if start_symbol in self._fingerprint:
            return self._fingerprint[start_symbol]
        todo = [start_symbol]
        seen = {start_symbol}
        parts = []
        while todo:
            symbol = todo.pop()
            name = stable_name(self.symbols[symbol][0])
            if not self.own(self.object_of(symbol)):
                parts.append(f"{name} lib")
                continue
            digest, refs = self.digest(symbol)
            parts.append(f"{name} {digest}")
            for target in refs:
                if target not in seen:
                    seen.add(target)
                    todo.append(target)
        result = hashlib.sha1("\n".join(sorted(parts)).encode()).hexdigest()
        self._fingerprint[start_symbol] = result
        return result


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
            line = line.strip().lstrip("ï»¿")
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
                "specs_units": unit_fingerprints(specs_source())}
        os.makedirs(os.path.dirname(LAST_FULL_PATH), exist_ok=True)
        with open(LAST_FULL_PATH, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1)
    except (OSError, subprocess.CalledProcessError):
        pass


# -- selection by compiled code (the default of --changed) -----------------------

# Files whose contents decide what a spec's result is, besides the spec and
# our code: a change selects every spec (and the self-test). Of run.py only
# the functions that run cases and compare them count (RESULT_FUNCTIONS), so
# command-line or report changes don't.
RESULT_FILES = ("tools/emutest/emu.py", "tools/requirements.txt", "re/ghidra_functions.tsv",
                "re/extra_functions.tsv")
RESULT_FUNCTIONS = ("first_difference", "case_count", "quick_case_count", "seed_name", "RENAMED_FUNCTIONS", "run_spec",
                    "QUICK_FRACTION")
PASSES_KEPT = 8  # passing fingerprints kept per spec (worktrees on different code)


def passes_path() -> str:
    """The pass store is shared by all worktrees of the repository: it lives
    in the main checkout's build/ (fingerprints are content hashes, so a
    pass recorded anywhere is valid everywhere)."""
    try:
        common = _git("rev-parse", "--path-format=absolute", "--git-common-dir").strip()
        main = os.path.dirname(common) if os.path.basename(common) == ".git" else ROOT
    except (OSError, subprocess.CalledProcessError):
        main = ROOT
    return os.path.normpath(os.path.join(main, "build", "emutest_passes.json"))


def harness_fingerprint() -> str:
    digest = hashlib.sha1()
    for path in RESULT_FILES:
        try:
            digest.update(path.encode() + b"\0" + open(os.path.join(ROOT, path), "rb").read()
                          .replace(b"\r\n", b"\n"))
        except OSError:
            digest.update(path.encode() + b"\0missing")
    tree = ast.parse(open(os.path.join(ROOT, "tools/emutest/run.py"), encoding="utf-8").read())
    for statement in tree.body:
        names = [statement.name] if isinstance(statement, ast.FunctionDef) else             [t.id for t in getattr(statement, "targets", []) if isinstance(t, ast.Name)]
        if any(name in RESULT_FUNCTIONS for name in names):
            digest.update(ast.unparse(statement).encode())
    return digest.hexdigest()


def spec_fingerprints(specs_module) -> dict[str, str]:
    """spec key (ours@original) -> fingerprint of its entry in specs.py
    (spec_units: the entry and the helpers it uses, without comments)."""
    namespace = vars(specs_module)
    result = {}
    current = specs_source()
    for element, fingerprint in spec_units(current):
        value = element.value if isinstance(element, ast.Starred) else element
        produced = eval(compile(ast.Expression(value), SPECS_PATH, "eval"), namespace)  # noqa: S307
        for spec in (list(produced) if isinstance(element, ast.Starred) else [produced]):
            result[f"{spec.ours}@{spec.original:#x}"] = fingerprint
    return result


def load_passes() -> dict:
    try:
        return json.load(open(passes_path(), encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def record_passes(entries: dict[str, dict], self_test_harness: str | None = None) -> None:
    """Adds passing results {spec key: {"key": pass key, ...details}} (and a
    passed self-test for a harness fingerprint) to the shared store. Merged
    with what is on disk right before the write, then replaced atomically,
    so parallel runs at worst lose an entry (which only means a rerun)."""
    if not entries and not self_test_harness:
        return
    path = passes_path()
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        data = load_passes()
        specs_part = data.setdefault("specs", {})
        for spec_key, entry in entries.items():
            kept = [e for e in specs_part.get(spec_key, []) if e.get("key") != entry["key"]]
            specs_part[spec_key] = ([entry] + kept)[:PASSES_KEPT]
        if self_test_harness:
            tests = [h for h in data.get("self_test", []) if h != self_test_harness]
            data["self_test"] = ([self_test_harness] + tests)[:PASSES_KEPT]
        temporary = f"{path}.{os.getpid()}.tmp"
        with open(temporary, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=0, sort_keys=True)
        os.replace(temporary, path)
    except OSError:
        pass


def pass_key(code: str, spec: str, harness: str) -> str:
    return hashlib.sha1(f"{code}\n{spec}\n{harness}".encode()).hexdigest()


def select_by_code(specs_module, machine_symbols, bin_dir: str):
    """Returns (names, harness_changed, description, keys): the specs
    without a recorded pass for their current (compiled code, specs.py
    entry, harness) fingerprints, and keys = {spec key: {"key": pass key, "code", "spec",
    "harness": the fingerprints}} for recording the passes of this run."""
    fingerprints = CodeFingerprints(os.path.join(bin_dir, "s3re.dll"), os.path.join(bin_dir, "s3re.map"))
    entries = spec_fingerprints(specs_module)
    harness = harness_fingerprint()
    store = load_passes()
    passed = store.get("specs", {})
    keys: dict[str, dict] = {}
    names: set[str] = set()
    why = {"never passed": 0, "code": 0, "spec": 0, "harness": 0}
    for spec in specs_module.SPECS:
        spec_key = f"{spec.ours}@{spec.original:#x}"
        code = fingerprints.fingerprint(machine_symbols(spec.ours))
        key = pass_key(code, entries.get(spec_key, "?"), harness)
        keys[spec_key] = {"key": key, "code": code, "spec": entries.get(spec_key), "harness": harness}
        history = passed.get(spec_key, [])
        if any(e.get("key") == key for e in history):
            continue
        names.add(spec.ours)
        if not history:
            why["never passed"] += 1
        elif history[0].get("harness") != harness:
            why["harness"] += 1
        elif history[0].get("spec") != entries.get(spec_key):
            why["spec"] += 1
        else:
            why["code"] += 1
    harness_changed = harness not in store.get("self_test", [])
    reasons = ", ".join(f"{n} {k}" for k, n in why.items() if n) or "nothing changed"
    description = (f"by compiled code (passes in {passes_path()}): {reasons}"
                   + ("; harness not self-tested yet" if harness_changed else ""))
    return names, harness_changed, description, keys


# -- selection by source files (--since REF / --by-headers) ----------------------

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
    if specs_changed(changed):
        if old_units is None:
            try:
                old_units = unit_fingerprints(specs_source(ref))
            except subprocess.CalledProcessError:
                old_units = []  # specs.py didn't exist then
        changed_names = changed_spec_names(specs_module, old_units)
        names |= changed_names
        reasons.append(f"specs ({len(changed_names)} spec(s) new or changed)")

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

