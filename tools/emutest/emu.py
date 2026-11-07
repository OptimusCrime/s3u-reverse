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
"""

from __future__ import annotations

import bisect
import os
import re
import struct
from dataclasses import dataclass, field

import capstone
import pefile
from unicorn import (UC_ARCH_X86, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
                     UC_MODE_32, Uc, UcError)
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI,
                               UC_X86_REG_ESP)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

WORLD_BASE = 0x40000000
WORLD_SIZE = 0x02000000  # 32 MB covers every world offset used so far
STACK_BASE = 0x00200000
STACK_SIZE = 0x00100000
RETURN_ADDRESS = 0x00010000  # sentinel: emulation stops when it is reached
MAX_INSTRUCTIONS = 20_000_000

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

    def analyze(self, start: int) -> Function:
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
        outcome = Outcome()
        try:
            uc.emu_start(address, RETURN_ADDRESS, count=MAX_INSTRUCTIONS)
            pc = uc.reg_read(UC_X86_REG_EIP)
            if pc != RETURN_ADDRESS:
                outcome.fault = f"did not return within {MAX_INSTRUCTIONS} instructions"
        except UcError as error:
            outcome.fault = f"{error} at {uc.reg_read(UC_X86_REG_EIP):#x}"
        outcome.eax = uc.reg_read(UC_X86_REG_EAX)
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
