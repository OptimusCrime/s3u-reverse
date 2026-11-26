#!/usr/bin/env python3
"""Compiler-generated static-initializer code: find, classify, generate.

MSVC 6 compiles every global with a dynamic initializer (a constructor, a
non-constant expression) into small functions and puts the address of one
of them per global into the CRT initializer table __xc_a..__xc_z
(0x692000..0x695970 in S3.EXE; the CRT's _initterm 0x61f075 calls each
entry at start-up). Per global with a destructor there are four functions,
each padded to 16 bytes:

  entry      call init; jmp reg           (the table entry)
  init       the constructor call / the initializer expression
  reg        push dtor; call atexit; pop ecx; ret
  dtor       mov ecx, global; jmp ~Class  (run by the CRT's exit code)

A global without a destructor has `jmp init` as its entry (or the
initializer itself). Ghidra found the init and reg functions but not the
entries and the destructor stubs (nothing calls them directly), so the
inventory's StaticDtorReg_ rows used to swallow the next stubs; this tool
finds those starts from the table itself.

Classes (by byte pattern; every operand is read from the instructions):
  call_jump     call I; jmp R                     -> I(); return R()
  jump          jmp I                             (trivial forwarder,
                                                   tools/trivial.py)
  atexit_reg    push T; call 0x61e812; pop ecx; ret  -> return atexit(T)
  this_jump     mov ecx, G; jmp F                 -> return F(this = G)
  this_call     push imm...; mov ecx, G; call F; ret -> return F(G, imm...)
  float_ratio   x87: [dst] = float((a1 - a0) / (double)(b1 - b0)) with
                a*/b* unsigned 8/16-bit loads (see float_ratio())
  concat_init   std::string dest = left + "text" (94 instructions with an
                EH frame; msvc6/static_string_inits.cpp)
  guarded_reg   guard byte |= 1 (stored only when clear), then jmp to an
                atexit_reg (a local static with an empty destructor)
  other         everything else (listed by `scan`)

Usage:
  python tools/static_inits.py scan            classes, counts and bytes
  python tools/static_inits.py list CLASS      one line per stub with operands
  python tools/static_inits.py missed          starts not in re/*_functions.tsv
  python tools/static_inits.py apply-starts    add the missed starts to
                                               re/extra_functions.tsv (kind
                                               manual) and re/functions.csv
  python tools/static_inits.py gen             write the generated C++
                                               (src/s3re/msvc6/static_stub_*.cpp)
                                               and the emulator spec tables
  python tools/static_inits.py apply-verified [GROUP...]
                                               mark the generated stubs verified
                                               (after their specs and tests pass)
"""

from __future__ import annotations

import bisect
import csv
import os
import re
import struct
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm_tools import ROOT, load_starts  # noqa: E402
from trivial import load_image  # noqa: E402

XC_A, XC_Z = 0x692000, 0x695970   # __xc_a .. __xc_z (_initterm(0x692000, 0x695970) at 0x61f084)
ATEXIT = 0x0061e812
STRING_FROM_CSTR = 0x00401a10
CONCAT_REFERENCE = 0x004c6430     # a concat init done in wip/logic-r4
CONCAT_REFERENCE_OPERANDS = (0x7aa5c0, 0x7aa5d0, 0x6959f4)  # dest, left, text
INVENTORY = os.path.join(ROOT, "re", "functions.csv")
EXTRA = os.path.join(ROOT, "re", "extra_functions.tsv")


@dataclass
class Stub:
    address: int
    role: str              # entry, init, reg, dtor, entry_jump_target
    cls: str = "other"
    operands: dict = field(default_factory=dict)
    text: str = ""
    size: int = 0          # bytes up to the next known start
    globl: int | None = None  # the global it is about (for names)


_md = None


def disassembler():
    global _md
    if _md is None:
        import capstone
        _md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        _md.detail = False
    return _md


class Image:
    def __init__(self):
        self.base, self.data = load_image()

    def u32(self, address):
        return struct.unpack_from("<I", self.data, address - self.base)[0]

    def u8(self, address):
        return self.data[address - self.base]

    def decode(self, address, limit=400, stop=("ret", "jmp")):
        out = []
        code = bytes(self.data[address - self.base:address - self.base + 4096])
        for ins in disassembler().disasm(code, address):
            out.append(ins)
            if ins.mnemonic in stop or len(out) >= limit:
                break
        return out

    def decode_range(self, start, end):
        code = bytes(self.data[start - self.base:end - self.base])
        return list(disassembler().disasm(code, start))


def table_entries(image):
    entries = []
    for slot in range(XC_A + 4, XC_Z, 4):
        value = image.u32(slot)
        if value:
            entries.append((slot, value))
    return entries


def num(text):
    return int(text, 16) if text.startswith("0x") else int(text)


def imm_value(text):
    v = num(text)
    return v & 0xFFFFFFFF


def instruction_text(ins):
    return f"{ins.mnemonic} {ins.op_str}".strip()


MEM_RE = re.compile(r"(byte|word|dword) ptr \[(0x[0-9a-f]+)\]")
REG32 = ("eax", "ecx", "edx", "ebx")
SUB8 = {"al": ("eax", 0), "ah": ("eax", 1), "cl": ("ecx", 0), "ch": ("ecx", 1),
        "dl": ("edx", 0), "dh": ("edx", 1), "bl": ("ebx", 0), "bh": ("ebx", 1)}
SUB16 = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx"}


def float_ratio(ins):
    """Symbolic execution of the x87 ratio initializer. Registers hold four
    byte slots, each 0 or a source byte address; `sub` of two zero-extended
    loads makes a difference. Returns {dst, a1, a0, b1, b0} with loads as
    (address, size) or None if the code is anything else."""
    regs = {r: None for r in REG32}  # None = unknown
    slot = None  # [esp]: the pushed ecx
    stack = []
    diffs = {}

    def load(dst, width, addr):
        old = regs[dst] if regs[dst] is not None else ["?"] * 4
        if width == 4:
            regs[dst] = [addr, addr + 1, addr + 2, addr + 3]
        elif width == 2:
            regs[dst] = [addr, addr + 1, old[2], old[3]]
        else:
            regs[dst] = [addr] + old[1:]

    def as_load(value):
        if value is None or isinstance(value, tuple):
            return None
        used = [b for b in value if b != 0]
        if not used or "?" in used:
            return None
        n = len(used)
        if value[:n] != [used[0] + i for i in range(n)] or any(b != 0 for b in value[n:]) or n not in (1, 2):
            return None
        return (used[0], n)

    if not ins or instruction_text(ins[0]) != "push ecx" or instruction_text(ins[-1]) != "ret":
        return None
    if instruction_text(ins[-2]) != "pop ecx":
        return None
    for i in ins[1:-2]:
        t = instruction_text(i)
        m = re.fullmatch(r"mov (e[a-d]x), dword ptr \[(0x[0-9a-f]+)\]", t)
        if m:
            load(m.group(1), 4, num(m.group(2)))
            continue
        m = re.fullmatch(r"mov ([a-d]x), word ptr \[(0x[0-9a-f]+)\]", t)
        if m:
            load(SUB16[m.group(1)], 2, num(m.group(2)))
            continue
        m = re.fullmatch(r"mov ([a-d][lh]), byte ptr \[(0x[0-9a-f]+)\]", t)
        if m:
            reg, pos = SUB8[m.group(1)]
            if pos != 0:
                return None
            load(reg, 1, num(m.group(2)))
            continue
        m = re.fullmatch(r"mov ([a-d][lh]), ([a-d][lh])", t)
        if m:
            (d, dp), (s, sp) = SUB8[m.group(1)], SUB8[m.group(2)]
            if regs[s] is None or regs[d] is None and dp != 0:
                return None
            value = list(regs[d]) if regs[d] is not None else ["?"] * 4
            value[dp] = regs[s][sp]
            regs[d] = value
            continue
        m = re.fullmatch(r"mov (e[a-d]x), (e[a-d]x)", t)
        if m:
            regs[m.group(1)] = list(regs[m.group(2)]) if isinstance(regs[m.group(2)], list) else regs[m.group(2)]
            continue
        m = re.fullmatch(r"xor (e[a-d]x), (e[a-d]x)", t)
        if m and m.group(1) == m.group(2):
            regs[m.group(1)] = [0, 0, 0, 0]
            continue
        m = re.fullmatch(r"and (e[a-d]x), (0x[0-9a-f]+)", t)
        if m and regs[m.group(1)] is not None and isinstance(regs[m.group(1)], list):
            mask = num(m.group(2))
            if mask not in (0xff, 0xffff):
                return None
            keep = 1 if mask == 0xff else 2
            regs[m.group(1)] = regs[m.group(1)][:keep] + [0] * (4 - keep)
            continue
        m = re.fullmatch(r"sub (e[a-d]x), (e[a-d]x)", t)
        if m:
            a, b = as_load(regs[m.group(1)]), as_load(regs[m.group(2)])
            if a is None or b is None:
                return None
            regs[m.group(1)] = ("diff", a, b)
            continue
        m = re.fullmatch(r"mov dword ptr \[esp(?: \+ 0)?\], (e[a-d]x)", t)
        if m:
            slot = regs[m.group(1)]
            continue
        if t in ("fild dword ptr [esp]", "fidiv dword ptr [esp]"):
            if not isinstance(slot, tuple):
                return None
            stack.append((t.split()[0], slot))
            continue
        m = re.fullmatch(r"fstp dword ptr \[(0x[0-9a-f]+)\]", t)
        if m:
            if [s[0] for s in stack] != ["fild", "fidiv"]:
                return None
            (_, (_, a1, a0)), (_, (_, b1, b0)) = stack
            return {"dst": num(m.group(1)), "a1": a1, "a0": a0, "b1": b1, "b0": b0}
        return None
    return None


def classify_body(image, address, end):
    """(class, operands, instruction text) of the code at address."""
    ins = image.decode(address)
    text = " ; ".join(instruction_text(i) for i in ins)
    if len(ins) == 2 and ins[0].mnemonic == "call" and ins[1].mnemonic == "jmp":
        return "call_jump", {"init": num(ins[0].op_str), "reg": num(ins[1].op_str)}, text
    if len(ins) == 1 and ins[0].mnemonic == "jmp" and ins[0].op_str.startswith("0x"):
        return "jump", {"target": num(ins[0].op_str)}, text
    m = re.fullmatch(r"push (0x[0-9a-f]+) ; call (0x[0-9a-f]+) ; pop ecx ; ret", text)
    if m and num(m.group(2)) == ATEXIT:
        return "atexit_reg", {"function": num(m.group(1))}, text
    m = re.fullmatch(r"mov ecx, (0x[0-9a-f]+) ; jmp (0x[0-9a-f]+)", text)
    if m:
        return "this_jump", {"this": num(m.group(1)), "function": num(m.group(2))}, text
    m = re.fullmatch(r"((?:push (?:0x[0-9a-f]+|-?\d+) ; )*)mov ecx, (0x[0-9a-f]+) ; call (0x[0-9a-f]+) ; ret", text)
    if m:
        pushes = [imm_value(p) for p in re.findall(r"push (0x[0-9a-f]+|-?\d+)", m.group(1))]
        return "this_call", {"this": num(m.group(2)), "function": num(m.group(3)),
                             "args": tuple(reversed(pushes))}, text
    m = re.fullmatch(r"mov cl, byte ptr \[(0x[0-9a-f]+)\] ; mov al, 1 ; test al, cl ; jne (0x[0-9a-f]+) ; "
                     r"or cl, al ; mov byte ptr \[(0x[0-9a-f]+)\], cl ; jmp (0x[0-9a-f]+)", text)
    if m and m.group(1) == m.group(3) and num(m.group(2)) == ins[-1].address:
        return "guarded_reg", {"guard": num(m.group(1)), "reg": num(m.group(4))}, text
    ratio = float_ratio(ins)
    if ratio is not None and ins[-1].address + 1 <= end:
        return "float_ratio", ratio, text
    concat = concat_operands(image, address)
    if concat is not None:
        return "concat_init", concat, text
    return "other", {}, text


_concat_ref = None


def _normalize(ins_list, symbols):
    out = []
    start = ins_list[0].address
    for i in ins_list:
        t = instruction_text(i)
        if i.mnemonic.startswith("j") and i.mnemonic != "jmp" or i.mnemonic == "jmp" and start <= num(i.op_str) < start + 0x200:
            t = f"{i.mnemonic} +0x{num(i.op_str) - start:x}"  # a branch inside the body
        t = re.sub(r"0x[0-9a-f]+", lambda m: symbols.get(num(m.group(0)), m.group(0)), t)
        out.append(t)
    return out


def concat_symbols(dest, left, text, handler):
    return {dest: "D", dest + 4: "D4", dest + 8: "D8", dest + 12: "D12", left: "L", text: "T", handler: "H"}


def concat_operands(image, address):
    global _concat_ref
    if _concat_ref is None:
        ref = image.decode(CONCAT_REFERENCE, limit=200, stop=())
        # the reference body: up to and including its final `ret` (before the next stub)
        end_index = [k for k, i in enumerate(ref) if i.mnemonic == "ret"][1]  # an inner ret, then the final one
        ref = ref[:end_index + 1]
        _concat_ref = (len(ref), _normalize(ref, concat_symbols(*CONCAT_REFERENCE_OPERANDS, num(ref[1].op_str))),
                       ref[-1].address + 1 - CONCAT_REFERENCE)
    count, normalized, length = _concat_ref
    ins = image.decode(address, limit=count, stop=())
    if len(ins) != count:
        return None
    try:
        dest = num(re.fullmatch(r"mov byte ptr \[(0x[0-9a-f]+)\], cl", instruction_text(ins[16])).group(1))
        left = num(re.fullmatch(r"mov edx, (0x[0-9a-f]+)", instruction_text(ins[10])).group(1))
        text = num(re.fullmatch(r"push (0x[0-9a-f]+)", instruction_text(ins[9])).group(1))
        handler = num(re.fullmatch(r"push (0x[0-9a-f]+)", instruction_text(ins[1])).group(1))
    except (AttributeError, IndexError):
        return None
    if _normalize(ins, concat_symbols(dest, left, text, handler)) != normalized:
        return None
    return {"dest": dest, "left": left, "text": text, "handler": handler}


def collect(image=None, starts=None):
    """Every stub reachable from the initializer table, by address."""
    image = image or Image()
    starts = sorted(set(starts if starts is not None else load_starts()))
    stubs: dict[int, Stub] = {}

    def add(address, role):
        if address not in stubs:
            stubs[address] = Stub(address, role)
        return stubs[address]

    for _, entry in table_entries(image):
        add(entry, "entry")
    # follow entries to inits, regs and destructor stubs
    pending = list(stubs)
    all_new = set(stubs)
    while pending:
        address = pending.pop()
        stub = stubs[address]
        cls, ops, text = classify_body(image, address, 1 << 32)
        follow = []
        if cls == "call_jump":
            follow = [(ops["init"], "init"), (ops["reg"], "reg")]
        elif cls == "jump" and stub.role == "entry":
            follow = [(ops["target"], "init")]
        elif cls == "atexit_reg":
            follow = [(ops["function"], "dtor")]
        elif cls == "guarded_reg":
            follow = [(ops["reg"], "reg")]
        for target, role in follow:
            if target not in stubs and 0x401000 <= target < 0x61e4ef:
                add(target, role)
                pending.append(target)
                all_new.add(target)
    known = sorted(set(starts) | set(stubs))
    for address, stub in stubs.items():
        k = bisect.bisect_right(known, address)
        end = known[k] if k < len(known) else address + 16
        stub.size = end - address
        stub.cls, stub.operands, stub.text = classify_body(image, address, end)
    # the global each stub is about
    for stub in stubs.values():
        o = stub.operands
        stub.globl = o.get("this") or o.get("dst") or o.get("dest")
    for stub in stubs.values():
        if stub.cls == "call_jump":
            init, reg = stubs.get(stub.operands["init"]), stubs.get(stub.operands["reg"])
            stub.globl = init.globl if init else None
            if reg and reg.globl is None:
                reg.globl = stub.globl
        if stub.cls == "jump" and stub.operands["target"] in stubs:
            stub.globl = stubs[stub.operands["target"]].globl
    for stub in stubs.values():
        if stub.cls == "atexit_reg":
            dtor = stubs.get(stub.operands["function"])
            if dtor is not None and dtor.globl is not None:
                stub.globl = dtor.globl
    return stubs


def cmd_scan():
    stubs = collect()
    starts = set(load_starts())
    from collections import defaultdict
    by = defaultdict(lambda: [0, 0, 0])
    for s in stubs.values():
        d = by[(s.cls, s.role)]
        d[0] += 1
        d[1] += s.size
        d[2] += s.address not in starts
    print(f"{'class':<13}{'role':<7}{'count':>7}{'bytes':>9}{'missed':>8}")
    for (cls, role), (n, size, missed) in sorted(by.items(), key=lambda kv: -kv[1][1]):
        print(f"{cls:<13}{role:<7}{n:7d}{size:9d}{missed:8d}")
    print(f"total {len(stubs)} stubs, {sum(s.size for s in stubs.values())} bytes, "
          f"{sum(1 for s in stubs.values() if s.address not in starts)} missed starts")
    others = [s for s in stubs.values() if s.cls == "other"]
    for s in sorted(others, key=lambda s: s.address):
        print(f"  other 0x{s.address:x} {s.role} {s.size}: {s.text[:150]}")


def fmt_ops(ops):
    def f(v):
        if isinstance(v, tuple):
            return "(" + ",".join(f(x) for x in v) + ")"
        return f"0x{v:x}" if isinstance(v, int) else str(v)
    return " ".join(f"{k}={f(v)}" for k, v in ops.items())


def cmd_list(cls):
    for s in sorted(collect().values(), key=lambda s: s.address):
        if s.cls == cls:
            print(f"0x{s.address:08x} {s.role:<6} {s.size:5d} {fmt_ops(s.operands)}")


def stub_name(stub):
    """Our name for a new row (the existing rows keep theirs)."""
    g = stub.globl
    suffix = f"{g:x}" if g is not None else f"at_{stub.address:x}"
    if stub.role == "entry":
        if stub.cls == "guarded_reg":
            return f"StaticGuardEntry_{stub.address:x}"
        return f"StaticInitEntry_{suffix}"
    if stub.role == "dtor":
        return f"StaticDtor_{suffix}"
    if stub.role == "reg":
        return f"StaticDtorReg_{suffix}" if g is not None else f"AtexitReg_{stub.address:x}"
    return f"StaticInit_{suffix}"


def evidence(stub, slots, referrers):
    body = stub.text if len(stub.text) <= 90 else stub.text[:87] + "..."
    if stub.role == "entry":
        return (f"static initializer: its address is in the CRT initializer table __xc_a..__xc_z "
                f"(slot 0x{slots[stub.address]:x}); body: {body}")
    via = referrers.get(stub.address)
    if stub.role == "dtor":
        return (f"atexit destructor stub: pushed by the atexit registration 0x{via:x} "
                f"(push 0x{stub.address:x}; call atexit 0x61e812); body: {body}")
    if stub.role == "reg":
        return f"atexit registration: jumped to by the guarded initializer-table entry 0x{via:x}; body: {body}"
    return (f"static initializer body: jumped to by the initializer-table entry 0x{via:x} "
            f"(jmp 0x{stub.address:x}); body: {body}")


def row_note(stub):
    kind = {"entry": "Initializer-table entry", "dtor": "atexit destructor stub",
            "reg": "atexit registration", "init": "Static initializer"}[stub.role]
    return (f"Compiler (stubs-r5, tools/static_inits.py class {stub.cls}): {kind}; "
            f"a start Ghidra missed. {fmt_ops(stub.operands)}")


def cmd_apply_starts(dry_run=False):
    image = Image()
    starts = set(load_starts())
    stubs = collect(image)
    slots = {entry: slot for slot, entry in table_entries(image)}
    referrers = {}
    for s in stubs.values():
        o = s.operands
        for key in ("function", "target", "reg", "init"):
            if key in o and isinstance(o[key], int) and s.cls in ("atexit_reg", "jump", "guarded_reg", "call_jump"):
                referrers.setdefault(o[key], s.address)
    missed = sorted(a for a in stubs if a not in starts)
    print(f"{len(missed)} missed starts")
    if dry_run:
        for a in missed[:40]:
            print(f"0x{a:08x} {stub_name(stubs[a])}: {evidence(stubs[a], slots, referrers)}")
        return
    # re/extra_functions.tsv: keep the header and rows, insert ours sorted
    lines = open(EXTRA, encoding="utf-8").read().splitlines()
    head = [l for l in lines if not l.startswith("0x")]
    rows = {int(l.split("\t")[0], 16): l for l in lines if l.startswith("0x")}
    for a in missed:
        rows[a] = f"0x{a:08x}\tmanual\t{evidence(stubs[a], slots, referrers)}"
    with open(EXTRA, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(head + [rows[a] for a in sorted(rows)]) + "\n")
    # re/functions.csv
    with open(INVENTORY, encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        columns = reader.fieldnames
        inventory = {int(r["address"], 16): r for r in reader}
    names = {r["name"] for r in inventory.values()}
    added = 0
    for a in missed:
        if a in inventory:
            continue
        name = stub_name(stubs[a])
        if name in names:
            name = f"{name}_at_{a:x}"
        names.add(name)
        inventory[a] = {"address": f"0x{a:08x}", "name": name, "category": "compiler",
                        "status": "identified", "hook": "none", "verified_by": "", "source_file": "",
                        "notes": row_note(stubs[a])}
        added += 1
    old = "; the stub follows in the same Ghidra function"
    for r in inventory.values():
        if old in r["notes"]:
            r["notes"] = r["notes"].replace(old, "; the stub is its own function (a start Ghidra missed, stubs-r5)")
    with open(INVENTORY, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=columns, lineterminator="\n")
        w.writeheader()
        for a in sorted(inventory):
            w.writerow(inventory[a])
    print(f"added {added} rows to re/functions.csv")


# ---------------------------------------------------------------------------
# gen: the generated C++ (src/s3re/msvc6/static_stub_*.cpp) and spec tables
# (the marked region of tools/emutest/specs/msvc6.py)

SRC_DIR = os.path.join(ROOT, "src", "s3re", "msvc6")
SPECS_FILE = os.path.join(ROOT, "tools", "emutest", "specs", "msvc6.py")
SPEC_BEGIN = "# BEGIN generated by tools/static_inits.py gen (do not edit)"
SPEC_END = "# END generated by tools/static_inits.py gen"

# (file, table, group, purpose, what the compiler generates)
GENERATED = [
    ("static_stub_entries.cpp", "kEntryStubs", "entries",
     "Runs one global's initializer at program start and registers its destructor (an entry of the CRT "
     "initializer table __xc_a..__xc_z).",
     "The initializer-table entries of globals with a destructor: `call init; jmp reg`. The compiler "
     "emits one per global and puts its address into the table; init constructs the global, reg "
     "registers its destructor stub with atexit."),
    ("static_stub_atexit.cpp", "kAtexitStubs", "atexit",
     "Registers one global's destructor stub with atexit at program start, so exit() destroys the global.",
     "The atexit registrations: `push dtor; call atexit 0x61e812; pop ecx; ret`. The compiler emits one "
     "per global with a destructor; the CRT's exit code calls the registered stubs in reverse order."),
    ("static_stub_dtors.cpp", "kDtorStubs", "dtors",
     "Destroys one global at program exit (registered with atexit by the stub before it).",
     "The destructor stubs: `mov ecx, global; jmp ~Class`. The compiler emits one per global with a "
     "destructor, because atexit takes a function without arguments."),
    ("static_stub_strings.cpp", "kStringStubs", "strings",
     "Constructs one global std::string from a string literal at program start.",
     "The initializers of `static std::string name = \"text\";`: `push -1; push text; mov ecx, global; "
     "call std::string(const char*, n) 0x401a10; ret` (n = npos)."),
    ("static_stub_floats.cpp", "kFloatStubs", "floats",
     "Computes one global float constant (a ratio of two differences of 8/16-bit values in .rdata, "
     "probably screen layout coordinates) at program start.",
     "The initializers of `static const float name = (a1 - a0) / (float)(b1 - b0);` with a* and b* "
     "unsigned 8- or 16-bit fields of constant structures: the compiler does not fold them, so it emits "
     "x87 code (fild of the int difference, fidiv by the other, fstp to the float). Each object file "
     "that includes the header gets its own copy, hence 1,360 of them."),
    ("static_stub_concat.cpp", "kConcatStubs", "concat",
     "Builds one global std::string by concatenating another global string and a literal at program start.",
     "The initializers of `static std::string name = left + \"text\";` that wip/logic-r4 had not done "
     "(it did the 117 of 0x4c0000..0x560000, static_string_inits.cpp, whose shared body ConcatInit "
     "these use too)."),
    ("static_stub_guards.cpp", "kGuardStubs", "guards",
     "Marks a function-local static as constructed and registers its (empty) destructor at program start.",
     "The initializer-table entries of guarded statics: `mov cl,[guard]; mov al,1; test al,cl; jne; or "
     "cl,al; mov [guard],cl; jmp reg` (bit 0 of the guard byte, stored only when clear), then the "
     "atexit registration."),
]


def c_string_comment(image, address, limit=60):
    out = []
    for k in range(limit):
        b = image.u8(address + k)
        if b == 0:
            break
        c = chr(b)
        if c == "\\":
            out.append("\\\\")
        elif c == '"':
            out.append('\\"')
        elif 0x20 <= b < 0x7f:
            out.append(c)
        else:
            out.append(f"\\x{b:02x}")
    else:
        out.append("...")
    return '"' + "".join(out).replace("*/", "*\\/") + '"'


def generated_stubs(stubs, inventory):
    """The stubs each generated file covers: (stub, name, C++ call, operand0, operand1, comment)."""
    groups = {g[2]: [] for g in GENERATED}
    image = Image()
    for s in sorted(stubs.values(), key=lambda s: s.address):
        row = inventory.get(s.address)
        if row is None or row["hook"] != "none" and "stubs-r5" not in row["notes"]:
            continue  # done before (e.g. the 117 concat inits of wip/logic-r4)
        name = row["name"]
        o = s.operands
        if s.cls == "call_jump":
            groups["entries"].append((s, name, f"return CallThenJump(0x{o['init']:08x}, 0x{o['reg']:08x});",
                                      o["init"], o["reg"], ""))
        elif s.cls == "atexit_reg":
            groups["atexit"].append((s, name, f"return RegisterAtexit(0x{o['function']:08x});",
                                     o["function"], 0, ""))
        elif s.cls == "this_jump" and s.role == "dtor":
            f = "kStringDestructor" if o["function"] == 0x401b60 else f"0x{o['function']:08x}"
            groups["dtors"].append((s, name, f"return JumpWithThis({f}, 0x{o['this']:x});",
                                    o["function"], o["this"], ""))
        elif (s.cls == "this_call" and o["function"] == 0x401a10 and len(o["args"]) == 2
              and o["args"][1] == 0xFFFFFFFF):
            text = o["args"][0]
            groups["strings"].append((s, name, f"return StringFromChars(0x{o['this']:x}, 0x{text:x});",
                                      o["this"], text, c_string_comment(image, text)))
        elif s.cls == "float_ratio":
            def ld(v):
                return f"{{0x{v[0]:x}, {v[1]}}}"
            groups["floats"].append((s, name, f"FloatRatio(0x{o['dst']:x}, {ld(o['a1'])}, {ld(o['a0'])}, "
                                     f"{ld(o['b1'])}, {ld(o['b0'])});", o["dst"], 0, ""))
        elif s.cls == "concat_init":
            groups["concat"].append((s, name, f"ConcatInit(0x{o['dest']:x}, 0x{o['left']:x}, 0x{o['text']:x});",
                                     o["dest"], o["left"], c_string_comment(image, o["text"])))
        elif s.cls == "guarded_reg":
            groups["guards"].append((s, name, f"return GuardThenJump(0x{o['guard']:x}, 0x{o['reg']:08x});",
                                     o["guard"], o["reg"], ""))
    return groups


def wrap_comment(text, width=76):
    words, lines, line = text.split(), [], "//"
    for w in words:
        if len(line) + 1 + len(w) > width:
            lines.append(line)
            line = "//"
        line += " " + w
    lines.append(line)
    return "\n".join(lines)


def write_cpp(filename, table, purpose, description, items):
    void = table in ("kFloatStubs", "kConcatStubs")
    ret, fn_type = ("void", "VoidStubFn") if void else ("std::uint32_t", "StubFn")
    out = ["// Generated by tools/static_inits.py gen from S3.EXE's bytes: do not edit.",
           wrap_comment(description),
           "// The class, what each helper does and the tests: msvc6/static_stubs.h and",
           "// static_stubs.cpp. The emulator specs: tools/emutest/specs/msvc6.py.",
           "// Every line: the stub's address in the hook; the operands are its",
           "// instructions' (raw-ok: S3.EXE data and code addresses of the stub).",
           "",
           '#include "hooks.h"',
           '#include "msvc6/static_stubs.h"',
           "",
           "#include <cstdint>",
           "#include <iterator>",
           "",
           "namespace s3re::msvc6 {",
           "",
           wrap_comment("Purpose (each): " + purpose),
           ]
    for s, name, call, _, _, comment in items:
        tail = f"  // {comment}" if comment else ""
        out.append(f"{ret} __cdecl {name}() {{ {call} }}  // raw-ok{tail}")
        out.append(f"S3RE_HOOK(0x{s.address:08x}, {name}, {fn_type}, Original_{name}, {name});")
    out += ["", "namespace {", "", "const StubEntry kEntries[] = {"]
    for s, name, _, op0, op1, _ in items:
        out.append(f'    {{"{name}", 0x{s.address:08x}, &Original_{name}, reinterpret_cast<const void*>(&{name}), '
                   f"0x{op0:x}, 0x{op1:x}}},  // raw-ok")
    out += ["};", "", "}  // namespace", "",
            f"const StubTable {table}{{kEntries, std::size(kEntries)}};", "",
            "}  // namespace s3re::msvc6", ""]
    with open(os.path.join(SRC_DIR, filename), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def spec_tables(groups):
    def h(v):
        return f"0x{v:x}"
    lines = [SPEC_BEGIN,
             "# The static-initializer stubs of stubs-r5, one row per stub: (address, our name, operands...).",
             "# Built into specs below the END marker."]
    for key, var in (("entries", "STATIC_STUB_ENTRIES"), ("atexit", "STATIC_STUB_ATEXIT"),
                     ("dtors", "STATIC_STUB_DTORS"), ("strings", "STATIC_STUB_STRINGS"),
                     ("concat", "STATIC_STUB_CONCAT"), ("guards", "STATIC_STUB_GUARDS")):
        lines.append(f"{var} = [")
        for s, n, _, a, b, _ in groups[key]:
            lines.append(f"    ({h(s.address)}, \"{n}\", {h(a)}, {h(b)}),")
        lines.append("]")
    lines.append("STATIC_STUB_FLOATS = [")
    for s, n, _, _, _, _ in groups["floats"]:
        o = s.operands
        loads = ", ".join(f"({h(o[k][0])}, {o[k][1]})" for k in ("a1", "a0", "b1", "b0"))
        lines.append(f"    ({h(s.address)}, \"{n}\", {h(o['dst'])}, {loads}),")
    lines.append("]")
    lines.append(SPEC_END)
    return "\n".join(lines)


def load_inventory_rows():
    with open(INVENTORY, encoding="utf-8", newline="") as f:
        return {int(r["address"], 16): r for r in csv.DictReader(f)}


def cmd_gen():
    stubs = collect()
    groups = generated_stubs(stubs, load_inventory_rows())
    for filename, table, key, purpose, description in GENERATED:
        write_cpp(filename, table, purpose, description, groups[key])
        print(f"{filename}: {len(groups[key])} stubs")
    text = open(SPECS_FILE, encoding="utf-8").read()
    block = spec_tables(groups)
    if SPEC_BEGIN in text:
        a = text.index(SPEC_BEGIN)
        b = text.index(SPEC_END) + len(SPEC_END)
        text = text[:a] + block + text[b:]
    else:
        text = text.rstrip("\n") + "\n\n\n" + block + "\n"
    with open(SPECS_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"spec tables written to {os.path.relpath(SPECS_FILE, ROOT)}")


# the in-process tests of each generated group (static_stubs.cpp)
GROUP_TESTS = {
    "entries": "StaticEntryStubs_MatchOriginal StaticEntryStubs_ComparisonDetectsWrongImplementation",
    "atexit": "StaticAtexitStubs_MatchOriginal StaticAtexitStubs_ComparisonDetectsWrongImplementation",
    "dtors": "StaticDtorStubs_MatchOriginal StaticDtorStubs_ComparisonDetectsWrongImplementation",
    "strings": "StaticStringStubs_MatchOriginal StaticStringStubs_ComparisonDetectsWrongImplementation",
    "floats": "StaticFloatStubs_MatchOriginal StaticFloatStubs_ComparisonDetectsWrongImplementation",
    "concat": "StaticConcatStubs_MatchOriginal StaticConcatStubs_ComparisonDetectsWrongImplementation",
    "guards": "StaticGuardStubs_MatchOriginal StaticGuardStubs_ComparisonDetectsWrongImplementation",
}
GROUP_SCOPE = {
    "entries": "in-process: the entries of string initializers",
    "dtors": "in-process: the std::string destructor stubs",
}


def cmd_apply_verified(groups_wanted):
    """Marks the generated stubs of the given groups verified (after their
    emulator specs and in-process tests passed)."""
    stubs = collect()
    inventory = load_inventory_rows()
    groups = generated_stubs(stubs, inventory)
    files = {g[2]: g[0] for g in GENERATED}
    changed = 0
    for key in groups_wanted:
        for s, name, _, _, _, _ in groups[key]:
            row = inventory[s.address]
            row["status"], row["hook"], row["verified_by"] = "verified", "replaced", GROUP_TESTS[key]
            scope = GROUP_SCOPE.get(key, "in-process: all of them")
            note = (f"Replaced by {name} (stubs-r5, msvc6/{files[key]}, generated by tools/static_inits.py). "
                    f"Evidence: emulator spec {name} (calls/bytes compared, full coverage); {scope} "
                    f"({GROUP_TESTS[key].split()[0]}).")
            if "Replaced by" not in row["notes"]:
                row["notes"] = (row["notes"] + " " + note).strip()
            changed += 1
    with open(INVENTORY, encoding="utf-8", newline="") as f:
        columns = csv.DictReader(f).fieldnames
    with open(INVENTORY, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=columns, lineterminator="\n")
        w.writeheader()
        for a in sorted(inventory):
            w.writerow(inventory[a])
    print(f"{changed} rows set to verified")


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "scan":
        cmd_scan()
    elif argv[0] == "missed":
        cmd_apply_starts(dry_run=True)
    elif argv[0] == "apply-starts":
        cmd_apply_starts()
    elif argv[0] == "apply-verified":
        cmd_apply_verified(argv[1:] or list(GROUP_TESTS))
    elif argv[0] == "gen":
        cmd_gen()
    elif argv[0] == "list":
        cmd_list(argv[1])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
