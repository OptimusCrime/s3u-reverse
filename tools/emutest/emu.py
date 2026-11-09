"""Emulator-based differential testing of original functions against ours.

Loads S3.EXE (the original code) and s3re.dll (our reimplementations) into a
Unicorn x86 emulator and calls an original function and its replacement on
identical arguments, registers and memory. See tools/emutest/run.py for what
is compared.

Memory model. The world object (ECX of the GameWorld methods) is mapped at
WORLD_BASE. Its contents are random but lazy: the first time a byte is read
or written it is materialized from a deterministic function of (case seed,
address), so both functions see the same random world without any setup,
and only the touched bytes cost anything. Values are drawn per aligned dword
from a pool of small numbers, random numbers and constants that appear in
the original function's code, so comparisons against those constants are
hit. Tests can pin specific bytes (presets).

Every run logs the bytes it overwrote; afterwards the writes are undone so
the other function starts from the same state.

Calls of the game's DebugLog (DEBUG_LOG, a bare `ret`) are recorded with
their format address and as many argument dwords as the format string has
conversions, so replacements must log the same messages with the same
arguments in the same order.

The game's operator new and delete are replaced by a bump allocator in the
world area (HEAP_OFFSET; reset for every call, so both functions get the
same addresses, and the blocks are lazily random and compared like the
world); every call is recorded ("new", size, address) / ("delete",
address) and must match. std::_Xlen and std::_Xran (which throw a C++
exception in the game) are recorded as ("throw", address) and end the run;
the stack and callee-saved registers are then not compared.

FS points at a fake thread information block (outside the compared memory),
so code with exception-handling frames (fs:[0]) runs.
"""

from __future__ import annotations

import bisect
import os
import re
import struct
from dataclasses import dataclass, field

import capstone
import pefile
from unicorn import (UC_ARCH_X86, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_READ,
                     UC_HOOK_MEM_WRITE, UC_MODE_32, Uc, UcError)
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI,
                               UC_X86_REG_ESP, UC_X86_REG_FS, UC_X86_REG_GDTR,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

WORLD_BASE = 0x40000000
WORLD_SIZE = 0x02000000  # 32 MB covers every world offset used so far
DEBUG_LOG = 0x00428720
_CONVERSION = re.compile(rb"%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[hlL]*([a-zA-Z%])")
STACK_BASE = 0x00200000
STACK_SIZE = 0x00100000
RETURN_ADDRESS = 0x00010000  # sentinel: emulation stops when it is reached
OPERATOR_NEW = 0x0061efb9
OPERATOR_DELETE = 0x0061e4ef
THROWERS = (0x0061e4fa, 0x0061e646)  # std::_Xlen, std::_Xran
HEAP_OFFSET = 0x01fc0000  # world offset of the fake heap (after specs.SCRATCH)
HEAP_SIZE = 0x00030000
TEB_BASE = 0x00300000
GDT_BASE = 0x00310000
FLAT_SELECTOR = 2 << 3    # GDT entry 2, RPL 0
FS_SELECTOR = (3 << 3) | 3  # GDT entry 3, RPL 3
MAX_INSTRUCTIONS = 20_000_000
# Probes: a fake vtable whose slot k points at VIRTUAL_CODE + k, and stub
# addresses for imported Windows functions (API_CODE + 4 * n, written into
# the import tables of S3.EXE and s3re.dll). Calling one records the call
# instead of running anything (see Emulator._on_virtual / _on_api).
PROBE_BASE = 0x00340000
FAKE_VTABLE = PROBE_BASE              # 0x400 slots
VIRTUAL_CODE = PROBE_BASE + 0x1000
API_CODE = PROBE_BASE + 0x2000
PROBE_SIZE = 0x3000


def _rect(machine, address):
    return struct.unpack("<4i", machine.guest_read(address, 16))


def _api_intersect_rect(machine, uc, args):
    """IntersectRect(dst, a, b), as user32 does it: an empty result (all 0)
    and FALSE unless both are non-empty and overlap."""
    dst, a, b = args
    ra, rb = _rect(machine, a), _rect(machine, b)
    empty = lambda r: r[2] <= r[0] or r[3] <= r[1]
    result = (max(ra[0], rb[0]), max(ra[1], rb[1]), min(ra[2], rb[2]), min(ra[3], rb[3]))
    ok = not empty(ra) and not empty(rb) and not empty(result)
    machine.guest_write(dst, struct.pack("<4i", *(result if ok else (0, 0, 0, 0))))
    return int(ok), ("IntersectRect", *ra, *rb)


def _api_is_rect_empty(machine, uc, args):
    r = _rect(machine, args[0])
    return int(r[2] <= r[0] or r[3] <= r[1]), ("IsRectEmpty", *r)


def _api_pt_in_rect(machine, uc, args):
    r = _rect(machine, args[0])
    x, y = (struct.unpack("<i", struct.pack("<I", v))[0] for v in args[1:3])
    return int(r[0] <= x < r[2] and r[1] <= y < r[3]), ("PtInRect", *r, x, y)


# name -> (stack argument dwords, handler(emulator, uc, args) -> (EAX, record)). The
# record holds what the call saw (rectangle contents, not pointers, which
# differ between the original's and our stack frames).
API_HANDLERS = {
    "IntersectRect": (3, _api_intersect_rect),
    "IsRectEmpty": (1, _api_is_rect_empty),
    "PtInRect": (3, _api_pt_in_rect),
}

CALLEE_SAVED = {"ebx": UC_X86_REG_EBX, "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI,
                "ebp": UC_X86_REG_EBP}


def _mix(value: int) -> int:
    """SplitMix64 finalizer: a fast deterministic hash."""
    value = (value + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return value ^ (value >> 31)


def _page_up(value: int) -> int:
    return (value + 0xFFF) & ~0xFFF


def map_pe(uc: Uc, path: str) -> tuple[int, int]:
    """Maps a PE image at its preferred base; returns (base, size). No
    relocation is needed: S3.EXE has none, and nothing else occupies
    s3re.dll's preferred base in our address space."""
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    size = _page_up(pe.OPTIONAL_HEADER.SizeOfImage)
    uc.mem_map(base, size)
    uc.mem_write(base, pe.header[:pe.OPTIONAL_HEADER.SizeOfHeaders])
    for section in pe.sections:
        data = section.get_data()
        if section.Misc_VirtualSize:
            data = data[:section.Misc_VirtualSize]
        uc.mem_write(base + section.VirtualAddress, data)  # the rest stays zero (.bss)
    return base, size


def read_function_starts() -> list[int]:
    starts = set()
    for name in ("ghidra_functions.tsv", "extra_functions.tsv"):
        for line in open(os.path.join(ROOT, "re", name), encoding="utf-8"):
            if line.startswith("0x") and "funclet" not in line:
                starts.add(int(line.split("\t")[0], 16))
    return sorted(starts)


def read_dll_symbols(map_path: str) -> dict[str, int]:
    """Decorated name -> address, from the linker map of s3re.dll."""
    symbols = {}
    pattern = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s", re.I)
    for line in open(map_path, encoding="utf-8", errors="replace"):
        match = pattern.match(line)
        if match:
            symbols[match.group(1)] = int(match.group(2), 16)
    return symbols


@dataclass
class Function:
    """Static facts about an original function."""
    start: int
    end: int             # next known function start
    leaders: list[int]   # basic block start addresses
    constants: list[int]  # immediate operands in its code


@dataclass
class Outcome:
    fault: str | None = None
    eax: int = 0
    popped: int = 0                  # bytes the callee removed from the stack (incl. return address)
    saved: dict[str, int] = field(default_factory=dict)
    initial: dict[int, int] = field(default_factory=dict)  # compared address -> byte before
    final: dict[int, int] = field(default_factory=dict)    # compared address -> byte after
    debug_logs: list[tuple[int, ...]] = field(default_factory=list)  # (format, args...) per DebugLog call
    heap_calls: list[tuple] = field(default_factory=list)  # ("new", size, address) / ("delete", address)
    thrown: int | None = None        # address of the std::_Xlen/_Xran called, if any
    probe_calls: list[tuple] = field(default_factory=list)  # virtual and API calls, in order
    spec_error: str | None = None    # a call the spec didn't declare


class Emulator:
    def __init__(self, exe_path: str, dll_path: str, map_path: str):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.exe_range = map_pe(self.uc, exe_path)
        self.dll_range = map_pe(self.uc, dll_path)
        self.symbols = read_dll_symbols(map_path)
        self.function_starts = read_function_starts()
        self.uc.mem_map(WORLD_BASE, WORLD_SIZE)
        self.uc.mem_map(STACK_BASE, STACK_SIZE)
        self.uc.mem_map(RETURN_ADDRESS, 0x1000)
        self.disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.disassembler.detail = True

        # State of the current case.
        self.seed = 0
        self.pool: list[int] = [0]
        self.presets: dict[int, int] = {}       # address -> byte, overrides the random fill
        self.materialized: set[int] = set()     # world bytes given a value this case
        self.undo: dict[int, int] | None = None  # address -> byte before this run's first write
        self.blocks: set[tuple[int, int]] = set()  # executed (start, size) in cover_range
        self.cover_range = (0, 0)

        self.uc.hook_add(UC_HOOK_MEM_READ, self._on_read, begin=WORLD_BASE,
                         end=WORLD_BASE + WORLD_SIZE - 1)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        self.uc.hook_add(UC_HOOK_BLOCK, self._on_block)
        self.uc.hook_add(UC_HOOK_CODE, self._on_debug_log, begin=DEBUG_LOG, end=DEBUG_LOG)
        self.debug_logs: list[tuple[int, ...]] = []
        for address, handler in ((OPERATOR_NEW, self._on_new), (OPERATOR_DELETE, self._on_delete),
                                 *((a, self._on_throw) for a in THROWERS)):
            self.uc.hook_add(UC_HOOK_CODE, handler, begin=address, end=address)
        self.heap_calls: list[tuple] = []
        self.heap_next = 0
        self.thrown: int | None = None
        self._map_thread_block()
        # Virtual-call and API probes.
        self.virtuals: dict[int, int | tuple[int, tuple[int, ...]]] = {}
        self.probe_calls: list[tuple] = []
        self.spec_error: str | None = None
        self.uc.mem_map(PROBE_BASE, PROBE_SIZE)
        self.uc.mem_write(FAKE_VTABLE, b"".join(struct.pack("<I", VIRTUAL_CODE + 4 * k)
                                                for k in range(0x400)))
        self.uc.mem_write(VIRTUAL_CODE, b"\xc3" * 0x2000)
        self.uc.hook_add(UC_HOOK_CODE, self._on_virtual, begin=VIRTUAL_CODE, end=VIRTUAL_CODE + 0xFFF)
        self.uc.hook_add(UC_HOOK_CODE, self._on_api, begin=API_CODE, end=API_CODE + 0xFFF)
        self.api_names: list[str] = []
        for path in (exe_path, dll_path):
            self._bind_imports(path)

    def _bind_imports(self, path: str) -> None:
        """Points every import of the image at an API probe."""
        pe = pefile.PE(path)
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in entry.imports:
                name = imp.name.decode() if imp.name else f"{entry.dll.decode()}#{imp.ordinal}"
                if name not in self.api_names:
                    self.api_names.append(name)
                stub = API_CODE + 4 * self.api_names.index(name)
                self.uc.mem_write(imp.address, struct.pack("<I", stub))

    def _probe_value(self) -> int:
        """EAX of a probed virtual call: 0, 1 or random, from the case seed
        and the call's position (so both functions get the same values as
        long as they make the same calls)."""
        h = _mix((self.seed << 32) ^ (0x5EED0000 + len(self.probe_calls)))
        choice = h & 3
        return 0 if choice == 0 else 1 if choice == 1 else (h >> 8) & 0xFFFFFFFF

    def _stack_args(self, uc, count: int) -> tuple[int, int, tuple[int, ...]]:
        esp = uc.reg_read(UC_X86_REG_ESP)
        values = struct.unpack(f"<{count + 1}I", bytes(uc.mem_read(esp, 4 * (count + 1))))
        return esp, values[0], values[1:]

    def _finish_probe(self, uc, esp: int, ret: int, count: int, eax: int) -> None:
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + 4 * count)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def _on_virtual(self, uc, address, size, user_data):
        """A call through the fake vtable: recorded as ("virtual", slot
        offset, ECX, arguments...). The spec declares each slot's stack
        argument count (callee pops) and, optionally, which arguments are
        pointers into the stack whose first bytes are recorded instead of
        the address: virtuals = {offset: count} or {offset: (count, {arg:
        bytes})}."""
        offset = address - VIRTUAL_CODE
        declared = self.virtuals.get(offset)
        if declared is None:
            self.spec_error = f"virtual call to slot {offset:#x} not declared in the spec"
            uc.emu_stop()
            return
        count, pointers = (declared, {}) if isinstance(declared, int) else declared
        esp, ret, args = self._stack_args(uc, count)
        shown = []
        for i, value in enumerate(args):
            if i in pointers:
                shown.append(("contents", self.guest_read(value, pointers[i])))
            else:
                shown.append(value)
        self.probe_calls.append(("virtual", offset, uc.reg_read(UC_X86_REG_ECX), *shown))
        self._finish_probe(uc, esp, ret, count, self._probe_value())

    def _on_api(self, uc, address, size, user_data):
        name = self.api_names[(address - API_CODE) // 4]
        if name not in API_HANDLERS:
            self.spec_error = f"call of {name}, which the emulator doesn't implement"
            uc.emu_stop()
            return
        count, handler = API_HANDLERS[name]
        esp, ret, args = self._stack_args(uc, count)
        eax, record = handler(self, uc, args)
        self.probe_calls.append(record)
        self._finish_probe(uc, esp, ret, count, eax)

    def _map_thread_block(self) -> None:
        """A data segment for FS whose base is TEB_BASE. Unicorn can't set the
        FS base directly in 32-bit mode, so through a GDT; loading one needs
        flat DS/ES/SS descriptors as well."""
        def descriptor(base, limit, access, flags):
            return struct.pack("<Q", (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (access << 40)
                               | (((limit >> 16) & 0xF) << 48) | (flags << 52) | ((base >> 24) << 56))
        self.uc.mem_map(TEB_BASE, 0x1000)
        self.uc.mem_map(GDT_BASE, 0x1000)
        flat_data = (FLAT_SELECTOR >> 3, 0, 0xFFFFF, 0x93, 0xC)  # ring 0, read/write, 4 GB
        teb = (FS_SELECTOR >> 3, TEB_BASE, 0xFFF, 0xF3, 0x4)     # ring 3, read/write
        for index, base, limit, access, flags in (flat_data, teb):
            self.uc.mem_write(GDT_BASE + 8 * index, descriptor(base, limit, access, flags))
        self.uc.reg_write(UC_X86_REG_GDTR, (0, GDT_BASE, 0xFFF, 0))
        for register in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            self.uc.reg_write(register, FLAT_SELECTOR)
        self.uc.reg_write(UC_X86_REG_FS, FS_SELECTOR)

    # -- symbols and static analysis ---------------------------------------

    def our_function(self, name: str) -> int:
        """Address of our replacement `name` (its C++ name, e.g.
        GameWorld_AddStatistic), not of its counting thunk."""
        found = [address for symbol, address in self.symbols.items()
                 if symbol.startswith(f"?{name}@") and "Counting" not in symbol
                 and not symbol.startswith(f"?{name}@@3")]
        if len(found) != 1:
            raise KeyError(f"{name}: {len(found)} matching symbols in s3re.map")
        return found[0]

    def analyze(self, start: int, end: int | None = None) -> Function:
        """Basic blocks and constants of the function at `start`, which ends at
        the next known function start (or at `end`, for code that continues
        past a function start Ghidra put inside it)."""
        if end is None:
            end = self.function_starts[bisect.bisect_right(self.function_starts, start)]
        code = bytes(self.uc.mem_read(start, end - start))
        instructions = list(self.disassembler.disasm(code, start))
        last_ret = max((ins.address for ins in instructions
                        if capstone.CS_GRP_RET in ins.groups), default=end)
        leaders = {start}
        constants = set()
        for i, ins in enumerate(instructions):
            if ins.address > last_ret:
                break  # padding after the last ret
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    constants.add(op.imm & 0xFFFFFFFF)
            if set(ins.groups) & {capstone.CS_GRP_JUMP, capstone.CS_GRP_RET}:
                if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                    target = ins.operands[0].imm
                    if start <= target <= last_ret:
                        leaders.add(target)
                if i + 1 < len(instructions) and instructions[i + 1].address <= last_ret:
                    leaders.add(instructions[i + 1].address)
        return Function(start, end, sorted(leaders), sorted(constants))

    # -- lazy world --------------------------------------------------------

    def _fill_byte(self, address: int) -> int:
        if address in self.presets:
            return self.presets[address]
        dword = address & ~3
        h = _mix((self.seed << 32) ^ dword)
        choice = h & 7
        if choice < 3:
            value = (h >> 8) & 0xFFFFFFFF   # anything
        elif choice < 5:
            value = (h >> 8) % 9            # small numbers
        elif choice < 6:
            value = 0
        else:
            value = self.pool[(h >> 8) % len(self.pool)]
        return (value >> (8 * (address - dword))) & 0xFF

    def _materialize(self, address: int, size: int) -> None:
        for a in range(address, address + size):
            if WORLD_BASE <= a < WORLD_BASE + WORLD_SIZE and a not in self.materialized:
                self.materialized.add(a)
                self.uc.mem_write(a, bytes([self._fill_byte(a)]))

    def _on_read(self, uc, access, address, size, value, user_data):
        self._materialize(address, size)

    def _on_write(self, uc, access, address, size, value, user_data):
        self._materialize(address, size)
        if self.undo is not None:
            old = bytes(uc.mem_read(address, size))
            for i in range(size):
                self.undo.setdefault(address + i, old[i])

    def guest_read(self, address: int, size: int) -> bytes:
        """A read made on the guest's behalf: lazy world bytes get their
        value first (Python's mem_read doesn't trigger the hooks)."""
        self._materialize(address, size)
        return bytes(self.uc.mem_read(address, size))

    def guest_write(self, address: int, data: bytes) -> None:
        """A write made on the guest's behalf (by a probe), tracked like the
        guest's own writes (Python's mem_write doesn't trigger the hooks)."""
        self._on_write(self.uc, None, address, len(data), 0, None)
        self.uc.mem_write(address, data)

    def _on_debug_log(self, uc, address, size, user_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        fmt = struct.unpack("<I", bytes(uc.mem_read(esp + 4, 4)))[0]
        base, image_size = self.exe_range
        count = 0
        if base <= fmt < base + image_size:
            text = bytes(uc.mem_read(fmt, 256)).split(b"\0", 1)[0]
            count = sum(1 for m in _CONVERSION.finditer(text) if m.group(1) != b"%")
        args = struct.unpack(f"<{count}I", bytes(uc.mem_read(esp + 8, 4 * count))) if count else ()
        self.debug_logs.append((fmt, *args))

    def _return_from_cdecl(self, uc, eax: int) -> None:
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack("<I", bytes(uc.mem_read(esp, 4)))[0]
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def _on_new(self, uc, address, size, user_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        count = struct.unpack("<I", bytes(uc.mem_read(esp + 4, 4)))[0]
        block = 0
        if count < HEAP_SIZE - self.heap_next:
            block = WORLD_BASE + HEAP_OFFSET + self.heap_next
            self.heap_next += (count + 15) & ~15
        self.heap_calls.append(("new", count, block))
        self._return_from_cdecl(uc, block)

    def _on_delete(self, uc, address, size, user_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        block = struct.unpack("<I", bytes(uc.mem_read(esp + 4, 4)))[0]
        self.heap_calls.append(("delete", block))
        self._return_from_cdecl(uc, 0)

    def _on_throw(self, uc, address, size, user_data):
        self.thrown = address
        uc.emu_stop()

    def _on_block(self, uc, address, size, user_data):
        low, high = self.cover_range
        if low <= address < high:
            self.blocks.add((address, size))

    def covered(self, leaders: list[int]) -> set[int]:
        """The basic block starts (from analyze) that lie in an executed block.
        Unicorn's own blocks don't always start at our leaders, hence the
        containment test."""
        return {leader for leader in leaders
                if any(start <= leader < start + size for start, size in self.blocks)}

    def compared(self, address: int) -> bool:
        """Writes that must match: the world and the game's own memory. Our
        DLL's internal data and the stack (locals) may differ."""
        if WORLD_BASE <= address < WORLD_BASE + WORLD_SIZE:
            return True
        base, size = self.exe_range
        return base <= address < base + size

    def begin_case(self, seed: int, pool: list[int], presets: dict[int, int]) -> None:
        """Forgets the previous case's world and starts a new random one."""
        for a in self.materialized:
            self.uc.mem_write(a, b"\0")
        self.materialized = set()
        self.seed = seed
        self.pool = pool or [0]
        self.presets = presets

    # -- calls -------------------------------------------------------------

    def call(self, address: int, ecx: int, edx: int, args: list[int],
             registers: dict[str, int], cover: tuple[int, int] = (0, 0)) -> Outcome:
        """Calls `address` the way the game does (arguments on the stack, then a
        return address), records the outcome and undoes the writes."""
        uc = self.uc
        esp = STACK_BASE + STACK_SIZE - 0x1000
        for arg in reversed(args):
            esp -= 4
            uc.mem_write(esp, struct.pack("<I", arg & 0xFFFFFFFF))
        esp -= 4
        uc.mem_write(esp, struct.pack("<I", RETURN_ADDRESS))
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.reg_write(UC_X86_REG_ECX, ecx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EAX, registers["eax"])
        for name, reg in CALLEE_SAVED.items():
            uc.reg_write(reg, registers[name])

        self.undo = {}
        self.cover_range = cover
        self.debug_logs = []
        self.heap_calls = []
        self.heap_next = 0
        self.thrown = None
        self.probe_calls = []
        self.spec_error = None
        outcome = Outcome()
        try:
            uc.emu_start(address, RETURN_ADDRESS, count=MAX_INSTRUCTIONS)
            pc = uc.reg_read(UC_X86_REG_EIP)
            if pc != RETURN_ADDRESS and self.thrown is None:
                outcome.fault = f"did not return within {MAX_INSTRUCTIONS} instructions"
        except UcError as error:
            outcome.fault = f"{error} at {uc.reg_read(UC_X86_REG_EIP):#x}"
        outcome.eax = uc.reg_read(UC_X86_REG_EAX)
        outcome.debug_logs = self.debug_logs
        outcome.heap_calls = self.heap_calls
        outcome.thrown = self.thrown
        outcome.probe_calls = self.probe_calls
        outcome.spec_error = self.spec_error
        outcome.popped = uc.reg_read(UC_X86_REG_ESP) - esp
        outcome.saved = {name: uc.reg_read(reg) for name, reg in CALLEE_SAVED.items()}
        undo, self.undo = self.undo, None
        for a, old in undo.items():
            if self.compared(a):
                outcome.initial[a] = old
                outcome.final[a] = bytes(uc.mem_read(a, 1))[0]
        for a, old in undo.items():
            uc.mem_write(a, bytes([old]))
        return outcome
