"""Emulator-based differential testing of original functions against ours.

Loads S3.EXE (the original code) and s3re.dll (our reimplementations) into a
Unicorn x86 emulator and calls an original function and its replacement on
identical arguments, registers and memory. See tools/emutest/run.py for what
is compared.

Memory model. The world object (ECX of the CGameData methods) is mapped at
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
arguments in the same order. A format in the stack (a text sprintf'd into
a buffer first) is recorded as its text.

The game's operator new and delete are replaced by a bump allocator in the
world area (HEAP_OFFSET; reset for every call, so both functions get the
same addresses, and the blocks are lazily random and compared like the
world); every call is recorded ("new", size, address) / ("delete",
address) and must match. std::_Xlen and std::_Xran (which throw a C++
exception in the game) are recorded as ("throw", address) and end the run;
the stack and callee-saved registers are then not compared.
_CxxThrowException (CXX_THROW) is a thrower too; it is recorded with the
thrown object pointer (read through its first argument, which points into
the frame of the function that throws) and the throw info.

Callee probes (Spec.callees): calls of chosen S3.EXE functions that are not
reimplemented (and whose own code the emulator can't run, e.g. constructors
of exception objects or the C runtime's free) are recorded with their
arguments instead of being run; see Emulator.set_callees.

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
                               UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FS, UC_X86_REG_GDTR,
                               UC_X86_REG_DS, UC_X86_REG_EFLAGS, UC_X86_REG_ES, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG, UC_X86_REG_SS)

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
CXX_THROW = 0x0061f027  # _CxxThrowException(object, throw info)
THROWERS = (0x0061e4fa, 0x0061e646, CXX_THROW)  # std::_Xlen, std::_Xran, _CxxThrowException
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
SCRATCH_PAGE = 0x00020000  # mapped and unmapped once at start (see new_uc)


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


def _api_sleep(machine, uc, args):
    return 0, ("Sleep", args[0])


def _api_output_debug_string(machine, uc, args):
    """OutputDebugStringA(text): records the text (up to its zero byte, at most
    256 bytes; the buffer's address differs between the two frames)."""
    text = machine.guest_read(args[0], 256).split(b"\0")[0]
    return 0, ("OutputDebugStringA", text)


def _api_close_handle(machine, uc, args):
    return 1, ("CloseHandle", args[0])


def _api_time_get_time(machine, uc, args):
    """timeGetTime(): a value fixed by the case and the number of probe calls
    so far (both functions get the same times as long as they make the same
    calls)."""
    value = _mix((machine.seed << 32) ^ (0x71AE0000 + len(machine.probe_calls))) & 0xFFFFFFFF
    return value, ("timeGetTime",)


def _api_get_drive_type(machine, uc, args):
    """GetDriveTypeA(root): a drive type 0..6 (sometimes any value) fixed by
    the case and the root's first four bytes, which are recorded (the
    buffer's address differs between the two frames)."""
    root = machine.guest_read(args[0], 4)
    value = _mix((machine.seed << 32) ^ 0x6D7E0000 ^ int.from_bytes(root, "little"))
    return (value % 7 if value & 0x700 else value & 0xFFFFFFFF), ("GetDriveTypeA", root)


# The text pointer UuidToStringA hands out (never dereferenced by the game:
# SaveGameToSlot only frees it again).
RPC_STRING = 0x5EED0000


def _api_uuid_to_string(machine, uc, args):
    """UuidToStringA(uuid, &text): records the 16 GUID bytes, stores
    RPC_STRING as the text (RPC_S_OK)."""
    guid = machine.guest_read(args[0], 16)
    uc.mem_write(args[1], struct.pack("<I", RPC_STRING))
    return 0, ("UuidToStringA", guid)


def _api_rpc_string_free(machine, uc, args):
    """RpcStringFreeA(&text): records the text pointer and clears it, as
    RPCRT4 does."""
    text = struct.unpack("<I", machine.guest_read(args[0], 4))[0]
    uc.mem_write(args[0], bytes(4))
    return 0, ("RpcStringFreeA", text)
def _api_hash(machine, salt):
    """A value fixed by the case, the probe calls so far and a salt."""
    return _mix((machine.seed << 32) ^ (salt + len(machine.probe_calls)))


def _api_text(machine, address, size=64):
    """A C string argument by content (null: None); its buffer may differ
    between the two frames."""
    if address == 0:
        return None
    return machine.guest_read(address, size).split(b"\0")[0]


def _api_fails(h):
    """True for about one call in four."""
    return (h & 3) == 0


def _api_value(h):
    """A 32-bit result that is never -1."""
    v = (h >> 8) & 0xFFFFFFFF
    return 0 if v == 0xFFFFFFFF else v


def _api_co_create_guid(machine, uc, args):
    """CoCreateGuid(guid): mostly S_OK with 16 random bytes written, about
    one call in four an error HRESULT (bit 31 set) without writing (session
    58, GameSettings::LoadSaveGame)."""
    h = _api_hash(machine, 0x60110000)
    if _api_fails(h):
        return 0x80000000 | ((h >> 8) & 0xFFFF), ("CoCreateGuid", args[0])
    guid = struct.pack("<QQ", _mix(h ^ 0x1111), _mix(h ^ 0x2222))
    machine.guest_write(args[0], guid)
    return 0, ("CoCreateGuid", args[0])


def _api_create_file(machine, uc, args):
    """CreateFileA: INVALID_HANDLE_VALUE or a handle."""
    h = _api_hash(machine, 0xCF000000)
    record = ("CreateFileA", _api_text(machine, args[0]), *args[1:])
    return (0xFFFFFFFF if _api_fails(h) else _api_value(h)), record


def _api_get_last_error(machine, uc, args):
    """GetLastError: mostly 0, 2, 3 or 5 (the codes the callers test), else
    any value."""
    h = _api_hash(machine, 0x6E000000)
    choices = (0, 2, 3, 5)
    return (choices[(h >> 4) & 3] if h & 7 else (h >> 8) & 0xFFFFFFFF), ("GetLastError",)


def _api_file_transfer(name, salt):
    def handler(machine, uc, args):
        """ReadFile / WriteFile(file, buffer, size, done, overlapped): fails
        or not; `done` (when given) gets a random count."""
        h = _api_hash(machine, salt)
        if args[3] != 0:
            machine.guest_write(args[3], struct.pack("<I", (h >> 16) & 0xFFFFFFFF))
        return (0 if _api_fails(h) else _api_value(h) | 1), (name, *args)
    return handler


def _api_set_file_pointer(machine, uc, args):
    """SetFilePointer(file, low, high*, method): -1 or a position; *high
    (when given) is recorded and gets a random value."""
    h = _api_hash(machine, 0x5E000000)
    high = None
    if args[2] != 0:
        high = machine.guest_read(args[2], 4)
        machine.guest_write(args[2], struct.pack("<I", (h >> 16) & 0xFFFFFFFF))
    return (0xFFFFFFFF if _api_fails(h) else _api_value(h)), ("SetFilePointer", *args, high)


def _api_get_file_size(machine, uc, args):
    """GetFileSize(file, high*): -1 or a size; *high gets a random value."""
    h = _api_hash(machine, 0x65000000)
    if args[1] != 0:
        machine.guest_write(args[1], struct.pack("<I", (h >> 16) & 0xFFFFFFFF))
    return (0xFFFFFFFF if _api_fails(h) else _api_value(h)), ("GetFileSize", *args)


def _api_create_file_mapping(machine, uc, args):
    h = _api_hash(machine, 0xC3000000)
    record = ("CreateFileMappingA", *args[:5], _api_text(machine, args[5]))
    return (0 if _api_fails(h) else _api_value(h) | 1), record


def _api_map_view(machine, uc, args):
    h = _api_hash(machine, 0x3A000000)
    return (0 if _api_fails(h) else _api_value(h) | 1), ("MapViewOfFile", *args)


def _api_unmap_view(machine, uc, args):
    h = _api_hash(machine, 0x0A000000)
    return (h >> 8) & 0xFFFFFFFF, ("UnmapViewOfFile", *args)


def _api_get_system_info(machine, uc, args):
    """GetSystemInfo(info): random fields, a page size of 0x800..0x2000 at
    +4 (the buffer is the caller's stack)."""
    h = _api_hash(machine, 0x51000000)
    info = bytearray(_mix(h + i) & 0xFF for i in range(36))
    info[4:8] = struct.pack("<I", (0x800, 0x1000, 0x2000, 0x1000)[h & 3])
    machine.guest_write(args[0], bytes(info))
    return 0, ("GetSystemInfo",)


# The memory DetachMemory walks in its spec (world offsets): VirtualQuery
# reports committed regions inside it.
DETACH_LOW = 0x01E00000
DETACH_HIGH = 0x01E40000
_PROTECTS = (0x02, 0x04, 0x08, 0x80, 0x01, 0x104, 0x20, 0x40, 0x202)


def _api_virtual_query(machine, uc, args):
    """VirtualQuery(address, info, size): a committed region from up to two
    pages below the address to past it, inside the DETACH area of the
    world (or reaching past its end), with one of a set of protections;
    sometimes a short count (the caller's error check)."""
    h = _api_hash(machine, 0x71000000)
    address, info, size = args
    low, high = WORLD_BASE + DETACH_LOW, WORLD_BASE + DETACH_HIGH
    page = address & ~0xFFF
    if address >= 0x80000000:
        return 0, ("VirtualQuery", address, size)  # kernel space: fails, as in Windows
    if not low <= address < high:
        # Outside the area: one inaccessible region up to 0x80000000
        # (nothing is touched).
        machine.guest_write(info, struct.pack("<7I", page, page, 0x01, 0x80000000 - page, 0x1000, 0x01, 0x20000))
        machine.last_protect = 0x01
        return 28, ("VirtualQuery", address, size)
    base = max(low, page - ((h >> 4) % 3) * 0x1000)
    below = page - base
    room = max(1, (high - page) // 0x1000)
    if (h >> 20) & 3 == 0:
        region = (high - base) + ((h >> 8) & 0x2FFF)
    else:
        region = below + (((h >> 24) % room) + 1) * 0x1000
    protect = _PROTECTS[(h >> 32) % len(_PROTECTS)]
    machine.last_protect = protect
    machine.guest_write(info, struct.pack("<7I", base, base, 0x04, region, 0x1000, protect, 0x40000))
    result = 28 if (h >> 40) & 15 else (h >> 44) % 28
    return result, ("VirtualQuery", address, size)


def _api_virtual_protect(machine, uc, args):
    """VirtualProtect(address, size, protect, old*): *old is the protection
    the last VirtualQuery reported (or sometimes another); fails sometimes."""
    h = _api_hash(machine, 0x7B000000)
    old = getattr(machine, "last_protect", 4) if h & 7 else (h >> 8) & 0xFFFFFFFF
    machine.guest_write(args[3], struct.pack("<I", old))
    return (0 if (h >> 40) & 15 == 0 else 1), ("VirtualProtect", *args[:3])


def _api_get_current_directory(machine, uc, args):
    """GetCurrentDirectoryA(size, buffer): 0 (failure) or a path."""
    h = _api_hash(machine, 0x6C000000)
    if _api_fails(h):
        return 0, ("GetCurrentDirectoryA", args[0])
    text = b"C:\\Games\\S3\\Gfx%u" % ((h >> 8) & 3)
    machine.guest_write(args[1], text + b"\0")
    return len(text), ("GetCurrentDirectoryA", args[0])


def _api_set_current_directory(machine, uc, args):
    h = _api_hash(machine, 0x5C000000)
    return (0 if _api_fails(h) else 1), ("SetCurrentDirectoryA", _api_text(machine, args[0]))


def _api_find_first_file(machine, uc, args):
    """FindFirstFileA(name, data): INVALID_HANDLE_VALUE or a handle; the
    data buffer (the caller's stack) is left alone."""
    h = _api_hash(machine, 0xFF000000)
    return (0xFFFFFFFF if (h & 1) == 0 else _api_value(h)), ("FindFirstFileA", _api_text(machine, args[0]))


def _api_find_close(machine, uc, args):
    h = _api_hash(machine, 0xFC000000)
    return (0 if _api_fails(h) else 1), ("FindClose", *args)


FAKE_MODULE = 0x7FFE0000
# GetProcAddress hands out this probe (a slot of the fake vtable's code):
# calling the returned function is recorded as a virtual call of slot
# PROC_SLOT, which the spec declares (session 56: DirectSoundCreate in
# CDirectSound::Connect).
PROC_SLOT = 0x3F0


def _api_load_library(machine, uc, args):
    """LoadLibraryA(name): null or a fake module handle."""
    h = _api_hash(machine, 0x11000000)
    return (0 if _api_fails(h) else FAKE_MODULE), ("LoadLibraryA", _api_text(machine, args[0]))


def _api_get_proc_address(machine, uc, args):
    """GetProcAddress(module, name): null or the PROC_SLOT probe."""
    h = _api_hash(machine, 0x9A000000)
    return (0 if _api_fails(h) else VIRTUAL_CODE + PROC_SLOT), ("GetProcAddress", args[0],
                                                                 _api_text(machine, args[1]))


# FormatMessageA's text (session 58): world memory below specs.SCRATCH.
FORMAT_MESSAGE_TEXT = WORLD_BASE + 0x01F80000


def _api_text_bytes(h, length):
    """`length` non-zero bytes fixed by the hash `h`."""
    return bytes((_mix(h + i) & 0xFF) | 1 for i in range(length))


def _api_format_message(machine, uc, args):
    """FormatMessageA(flags, source, id, language, buffer, size, arguments)
    with FORMAT_MESSAGE_ALLOCATE_BUFFER: *buffer = FORMAT_MESSAGE_TEXT, a
    text of 0..39 bytes (mostly ending in CR LF; sometimes 0 or 1 bytes),
    written also when the call fails (returns 0 in about one call in four;
    otherwise the length)."""
    h = _api_hash(machine, 0xF0000000)
    length = (h >> 8) % 2 if (h >> 4) % 6 == 0 else (h >> 8) % 40
    text = bytearray(_api_text_bytes(h, length))
    if length >= 2 and (h >> 40) & 3:
        text[-2:] = b"\r\n"
    machine.guest_write(FORMAT_MESSAGE_TEXT, bytes(text) + b"\0")
    machine.guest_write(args[4], struct.pack("<I", FORMAT_MESSAGE_TEXT))
    record = ("FormatMessageA", args[0], args[1], args[2], args[3], args[5], args[6])
    return (0 if _api_fails(h) else length), record


def _api_local_free(machine, uc, args):
    return 0, ("LocalFree", args[0])


def _api_mci_get_error_string(machine, uc, args):
    """mciGetErrorStringA(error, text, size): records the buffer's first and
    last four bytes (as the caller prepared it), then mostly writes a text
    of 0..49 bytes; returns 0 or 1."""
    h = _api_hash(machine, 0x3C000000)
    error, text, size = args
    record = ("mciGetErrorStringA", error, size, machine.guest_read(text, 4), machine.guest_read(text + size - 4, 4))
    if (h >> 4) & 3:
        machine.guest_write(text, _api_text_bytes(h, (h >> 8) % 50) + b"\0")
    return (h >> 12) & 1, record


def _api_success(name, salt, texts=()):
    """A Windows call that returns success (1, or a random non-zero value:
    a handle, a count) or 0 (failure, about one call in four); records its
    arguments (those in `texts` by their C text)."""
    def handler(machine, uc, args):
        h = _api_hash(machine, salt)
        record = tuple(_api_text(machine, a) if i in texts else a for i, a in enumerate(args))
        return (0 if _api_fails(h) else (1 if h & 0x10 else _api_value(h) | 1)), (name, *record)
    return handler


def _api_mci_send_command(machine, uc, args):
    """mciSendCommandA(device, message, flags, parameters) (session 58):
    records the parameter fields the callers set (MCI_OPEN 0x803: all five,
    then writes a device id; MCI_PLAY 0x806: callback, from, to;
    MCI_STATUS 0x814: the item, then writes the result), returns 0 or an
    MCI error."""
    h = _api_hash(machine, 0x3C100000)
    device, message, flags, block = args
    fields = ()
    if block:
        if message == 0x803:
            fields = struct.unpack("<5I", machine.guest_read(block, 20))
            machine.guest_write(block + 4, struct.pack("<I", (h >> 20) & 7 if h & 0x100 else (h >> 24) & 0xFFFFFFFF))
        elif message == 0x806:
            fields = struct.unpack("<3I", machine.guest_read(block, 12))
        elif message == 0x814:
            fields = struct.unpack("<I", machine.guest_read(block + 8, 4))
            machine.guest_write(block + 4, struct.pack("<I", (h >> 12) & 0x7F))
    result = (0, 0, 0x106, 0x101, 0x10000, (h >> 8) & 0xFFFFFFFF)[(h >> 4) % 6]
    return result, ("mciSendCommandA", device, message, flags, block != 0, *fields)


def _api_wait_for_single_object(machine, uc, args):
    """WaitForSingleObject: WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_ABANDONED,
    WAIT_FAILED, or any value."""
    h = _api_hash(machine, 0x3A170000)
    choices = (0, 0, 0x102, 0x102, 0x80, 0xFFFFFFFF, 1, (h >> 8) & 0xFFFFFFFF)
    return choices[(h >> 4) & 7], ("WaitForSingleObject", *args)
# -- GDI text functions and critical sections (wip/ui-gdi, session 57) -------
# Device contexts are plain numbers; the text functions record their text
# by contents (at most 256 bytes: the buffers differ between the frames).

def _api_counted_text(machine, address, length):
    if address == 0:
        return None
    n = max(0, min(length if length < 0x80000000 else 0, 256))
    return machine.guest_read(address, n) if n else b""


def _api_select_object(machine, uc, args):
    """SelectObject(dc, object): a random previous object."""
    return (_api_hash(machine, 0x5E100000) >> 8) & 0xFFFFFFFF, ("SelectObject", *args)


def _api_get_text_extent_point(machine, uc, args):
    """GetTextExtentPoint32A(dc, text, length, size*): text-like sizes (cx
    0..700, cy 0..40), sometimes any; FALSE about one call in eight."""
    dc, text, length, size = args
    h = _api_hash(machine, 0x7E100000)
    if (h >> 40) & 7 == 0:
        cx, cy = (h >> 8) & 0xFFFFFFFF, (h >> 24) & 0xFFFFFFFF
    else:
        cx, cy = ((h >> 8) & 0xFFFF) % 701, ((h >> 24) & 0xFF) % 41
    if size != 0:
        machine.guest_write(size, struct.pack("<II", cx, cy))
    ok = 0 if (h >> 44) & 7 == 0 else 1
    return ok, ("GetTextExtentPoint32A", dc, _api_counted_text(machine, text, length), length, size != 0)


def _api_set_bk_mode(machine, uc, args):
    return (_api_hash(machine, 0xB4000000) >> 8) % 3, ("SetBkMode", *args)


def _api_set_colour(name, salt):
    def handler(machine, uc, args):
        """SetTextColor / SetBkColor(dc, colour): a random previous colour."""
        return (_api_hash(machine, salt) >> 8) & 0xFFFFFF, (name, *args)
    return handler


def _api_text_out(machine, uc, args):
    """TextOutA(dc, x, y, text, length): TRUE mostly."""
    dc, x, y, text, length = args
    ok = 0 if (_api_hash(machine, 0x70000000) & 7) == 0 else 1
    return ok, ("TextOutA", dc, x, y, _api_counted_text(machine, text, length), length)


def _api_critical_section(name):
    def handler(machine, uc, args):
        """Enter/LeaveCriticalSection(section): recorded (the section lies in
        the compared memory, its address is the same for both)."""
        return 0, (name, args[0])
    return handler


# -- the registry (Win32::CRegistryKey, wip/app-r4) ---------------------------
# Results are Windows error codes: 0 mostly, else ERROR_FILE_NOT_FOUND,
# ERROR_ACCESS_DENIED, ERROR_MORE_DATA, ERROR_NO_MORE_ITEMS or any value.
# Out-parameters are written on success. Pointers into the world are
# recorded as numbers, others (stack locals, which differ between the two
# frames) by presence.

def _api_reg_result(h):
    if (h & 3) != 0:
        return 0
    return (2, 5, 234, 259, 6, (h >> 16) & 0xFFFFFFFF)[(h >> 4) % 6]


def _api_reg_pointer(p):
    if WORLD_BASE <= p < WORLD_BASE + WORLD_SIZE:
        return p
    return "local" if p else 0


def _api_reg_write32(machine, p, value):
    if p:
        machine.guest_write(p, struct.pack("<I", value & 0xFFFFFFFF))


def _api_reg_bytes(machine, h, buffer, length):
    """A name or data of up to *length bytes (at most 12); *length = the count."""
    if not length:
        return
    n = min(struct.unpack("<I", machine.guest_read(length, 4))[0], 12)
    k = (h >> 24) % (n + 1) if n else 0
    if buffer:
        machine.guest_write(buffer, bytes(((h >> (i % 7)) + i * 37) & 0xFF for i in range(k)))
    machine.guest_write(length, struct.pack("<I", k))


def _api_reg_open(machine, uc, args):
    key, sub, options, sam, result = args
    h = _api_hash(machine, 0x4E600000)
    r = _api_reg_result(h)
    if r == 0:
        _api_reg_write32(machine, result, h >> 20)
    return r, ("RegOpenKeyExA", key, _api_text(machine, sub), options, sam, _api_reg_pointer(result))


def _api_reg_create(machine, uc, args):
    key, sub, reserved, cls, options, sam, security, result, disposition = args
    h = _api_hash(machine, 0x4E610000)
    r = _api_reg_result(h)
    if r == 0:
        _api_reg_write32(machine, result, h >> 20)
        _api_reg_write32(machine, disposition, 1 + ((h >> 8) & 1))
    return r, ("RegCreateKeyExA", key, _api_text(machine, sub), reserved, _api_text(machine, cls), options, sam,
               security, _api_reg_pointer(result), disposition != 0)


def _api_reg_close(machine, uc, args):
    return _api_reg_result(_api_hash(machine, 0x4E620000)), ("RegCloseKey", args[0])


def _api_reg_delete(machine, uc, args):
    return (_api_reg_result(_api_hash(machine, 0x4E630000)),
            ("RegDeleteKeyA", args[0], _api_text(machine, args[1])))


def _api_reg_query_info(machine, uc, args):
    key, cls, cls_length, reserved, *counts, last_write = args
    h = _api_hash(machine, 0x4E640000)
    r = _api_reg_result(h)
    if r == 0:
        _api_reg_bytes(machine, h, cls, cls_length)
        for i, p in enumerate(counts):
            _api_reg_write32(machine, p, (h >> i) % 64 if (h >> (20 + i)) & 1 else _mix(h + i) >> 32)
        if last_write:
            machine.guest_write(last_write, struct.pack("<Q", _mix(h ^ 0x77)))
    return r, ("RegQueryInfoKeyA", key, _api_reg_pointer(cls), _api_reg_pointer(cls_length), reserved,
               *(_api_reg_pointer(p) for p in counts), _api_reg_pointer(last_write))


def _api_reg_enum_key(machine, uc, args):
    """RegEnumKeyExA: the FILETIME is written on every call (EnumKey copies
    its local also after ERROR_NO_MORE_ITEMS, when it would be
    uninitialized)."""
    key, index, name, name_length, reserved, cls, cls_length, last_write = args
    h = _api_hash(machine, 0x4E650000)
    if last_write:
        machine.guest_write(last_write, struct.pack("<Q", _mix(h ^ 0x55)))
    r = _api_reg_result(h)
    if r == 0:
        _api_reg_bytes(machine, h, name, name_length)
        _api_reg_bytes(machine, h >> 3, cls, cls_length)
    return r, ("RegEnumKeyExA", key, index, _api_reg_pointer(name), _api_reg_pointer(name_length), reserved,
               _api_reg_pointer(cls), _api_reg_pointer(cls_length), last_write != 0)


def _api_reg_enum_value(machine, uc, args):
    key, index, name, name_length, reserved, type_, data, data_size = args
    h = _api_hash(machine, 0x4E660000)
    r = _api_reg_result(h)
    if r == 0:
        _api_reg_bytes(machine, h, name, name_length)
        _api_reg_write32(machine, type_, (h >> 8) % 12)
        _api_reg_bytes(machine, h >> 5, data, data_size)
    return r, ("RegEnumValueA", key, index, _api_reg_pointer(name), _api_reg_pointer(name_length), reserved,
               _api_reg_pointer(type_), _api_reg_pointer(data), _api_reg_pointer(data_size))


def _api_reg_query_value(machine, uc, args):
    key, name, reserved, type_, data, data_size = args
    h = _api_hash(machine, 0x4E670000)
    size_in = struct.unpack("<I", machine.guest_read(data_size, 4))[0] if data_size else None
    r = _api_reg_result(h)
    if r == 0:
        _api_reg_write32(machine, type_, (h >> 8) % 12)
        if data == 0 and data_size:
            # The size query (QueryString): at least 1, as for a REG_SZ with
            # its terminator (a zero-size block would share the bump
            # allocator's next address).
            _api_reg_write32(machine, data_size, 1 + (h >> 24) % 12)
        else:
            _api_reg_bytes(machine, h >> 5, data, data_size)
    return r, ("RegQueryValueExA", key, _api_text(machine, name), reserved, _api_reg_pointer(type_),
               _api_reg_pointer(data), _api_reg_pointer(data_size), size_in)


def _api_reg_set_value(machine, uc, args):
    key, name, reserved, type_, data, size = args
    h = _api_hash(machine, 0x4E680000)
    first = struct.unpack("<I", machine.guest_read(data, 4))[0] if data and size >= 4 else None
    return _api_reg_result(h), ("RegSetValueExA", key, _api_text(machine, name), reserved, type_,
                                _api_reg_pointer(data), first, size)
def _api_interlocked_exchange(machine, uc, args):
    """InterlockedExchange(target, value) (wip/logic-r4): stores value at
    target, returns the old value (the target lies in the compared memory)."""
    target, value = args
    old = struct.unpack("<I", bytes(machine.guest_read(target, 4)))[0]
    machine.guest_write(target, struct.pack("<I", value))
    return old, ("InterlockedExchange", target, value)


# name -> (stack argument dwords, handler(emulator, uc, args) -> (EAX, record)). The
# record holds what the call saw (rectangle contents, not pointers, which
# differ between the original's and our stack frames).
API_HANDLERS = {
    "IntersectRect": (3, _api_intersect_rect),
    "IsRectEmpty": (1, _api_is_rect_empty),
    "PtInRect": (3, _api_pt_in_rect),
    "timeGetTime": (0, _api_time_get_time),
    "Sleep": (1, _api_sleep),
    "CloseHandle": (1, _api_close_handle),
    "OutputDebugStringA": (1, _api_output_debug_string),
    "GetDriveTypeA": (1, _api_get_drive_type),
    "UuidToStringA": (2, _api_uuid_to_string),
    "RpcStringFreeA": (1, _api_rpc_string_free),
    "CoCreateGuid": (1, _api_co_create_guid),
    "CreateFileA": (7, _api_create_file),
    "GetLastError": (0, _api_get_last_error),
    "ReadFile": (5, _api_file_transfer("ReadFile", 0x4E000000)),
    "WriteFile": (5, _api_file_transfer("WriteFile", 0x3F000000)),
    "SetFilePointer": (4, _api_set_file_pointer),
    "GetFileSize": (2, _api_get_file_size),
    "CreateFileMappingA": (6, _api_create_file_mapping),
    "MapViewOfFile": (5, _api_map_view),
    "UnmapViewOfFile": (1, _api_unmap_view),
    "GetSystemInfo": (1, _api_get_system_info),
    "VirtualQuery": (3, _api_virtual_query),
    "VirtualProtect": (4, _api_virtual_protect),
    "GetCurrentDirectoryA": (2, _api_get_current_directory),
    "SetCurrentDirectoryA": (1, _api_set_current_directory),
    "FindFirstFileA": (2, _api_find_first_file),
    "FindClose": (1, _api_find_close),
    "LoadLibraryA": (1, _api_load_library),
    "GetProcAddress": (2, _api_get_proc_address),
    "FormatMessageA": (7, _api_format_message),
    "LocalFree": (1, _api_local_free),
    "mciGetErrorStringA": (3, _api_mci_get_error_string),
    "SetEvent": (1, _api_success("SetEvent", 0x5E000001)),
    "ResetEvent": (1, _api_success("ResetEvent", 0x5E000002)),
    "PulseEvent": (1, _api_success("PulseEvent", 0x5E000003)),
    "ReleaseSemaphore": (3, _api_success("ReleaseSemaphore", 0x5E000004)),
    "CreateEventA": (4, _api_success("CreateEventA", 0x5E000005, texts=(3,))),
    "CreateSemaphoreA": (4, _api_success("CreateSemaphoreA", 0x5E000006, texts=(3,))),
    "CreateMutexA": (3, _api_success("CreateMutexA", 0x5E000007, texts=(2,))),
    "SetThreadPriority": (2, _api_success("SetThreadPriority", 0x5E000008)),
    "PostThreadMessageA": (4, _api_success("PostThreadMessageA", 0x5E000009)),
    "DestroyWindow": (1, _api_success("DestroyWindow", 0x5E00000A)),
    "PostMessageA": (4, _api_success("PostMessageA", 0x5E00000B)),
    "InterlockedExchange": (2, _api_interlocked_exchange),
    "SendMessageA": (4, _api_success("SendMessageA", 0x5E00000D)),
    "DeleteCriticalSection": (1, _api_critical_section("DeleteCriticalSection")),
    "WaitForSingleObject": (2, _api_wait_for_single_object),
    "mciSendCommandA": (4, _api_mci_send_command),
    "SelectObject": (2, _api_select_object),
    "GetTextExtentPoint32A": (4, _api_get_text_extent_point),
    "SetBkMode": (2, _api_set_bk_mode),
    "SetTextColor": (2, _api_set_colour("SetTextColor", 0x7C000000)),
    "SetBkColor": (2, _api_set_colour("SetBkColor", 0xBC000000)),
    "TextOutA": (5, _api_text_out),
    "EnterCriticalSection": (1, _api_critical_section("EnterCriticalSection")),
    "LeaveCriticalSection": (1, _api_critical_section("LeaveCriticalSection")),
    "RegOpenKeyExA": (5, _api_reg_open),
    "RegCreateKeyExA": (9, _api_reg_create),
    "RegCloseKey": (1, _api_reg_close),
    "RegDeleteKeyA": (2, _api_reg_delete),
    "RegQueryInfoKeyA": (12, _api_reg_query_info),
    "RegEnumKeyExA": (8, _api_reg_enum_key),
    "RegEnumValueA": (8, _api_reg_enum_value),
    "RegQueryValueExA": (6, _api_reg_query_value),
    "RegSetValueExA": (6, _api_reg_set_value),
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


def new_uc() -> Uc:
    """A 32-bit x86 Unicorn whose mapped regions get disjoint RAM offsets.

    Unicorn 2.1.4 gives every mem_map region an offset in QEMU's ram_addr
    space (the dirty-memory bitmaps and the per-page lists of translated
    code are indexed by it). As long as no region was ever unmapped, it
    places a new region after `ram_list.last_block`, which is the last
    block of the size-sorted block list, not the one that ends highest
    (qemu/exec.c find_ram_offset_last). Map a region bigger than an
    earlier one and later regions overlap it: with S3.EXE (0x3ed000
    bytes) mapped before s3re.dll (0x629000), the world and the stack got
    the same ram_addr range as s3re.dll. A store to world+X then counts as
    a store to s3re.dll+X: if translated code came from there, QEMU's
    self-modifying-code handling throws it away and reports the store
    twice, and when it is the block being run, Unicorn calls no memory
    hooks any more (from then on stores aren't tracked or compared and
    reads don't fill in the lazy world). Whether it happens depends on
    where our code sits in the DLL, so unrelated code changes made it
    come and go (2026-11-25: GenerateRandomMap in the Release build, whose
    tile loop at s3re.dll+0x7dda0 stored to world+0x7dda2).

    Once a region has been unmapped (`ram_list.freed`), Unicorn uses
    QEMU's gap search for every later region, which is correct; hence the
    page mapped and unmapped here. `check_ram_offsets` tests the result
    (run.py --self-test)."""
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(SCRATCH_PAGE, 0x1000)
    uc.mem_unmap(SCRATCH_PAGE, 0x1000)
    return uc


def check_ram_offsets(uc: Uc, code: int, data: list[tuple[int, int]]) -> str | None:
    """Checks that no region in `data` ((start, size) pairs) shares RAM
    offsets with the code page at `code` (see new_uc): runs a loop at
    `code` that stores to every page of each region at the page offset of
    the loop's own bytes and expects each store to be reported exactly
    once (a store to memory that aliases the running code is reported
    twice). Overwrites the bytes at `code` and the stored-to bytes, so use
    it on a throwaway emulator. Returns the problem, or None."""
    body = (b"\x89\x07"                      # mov [edi], eax
            + b"\x81\xc7\x00\x10\x00\x00"    # add edi, 0x1000
            + b"\x39\xf7"                    # cmp edi, esi
            + b"\x72\xf4")                   # jb (the mov)
    uc.mem_write(code, body)
    uc.ctl_flush_tb()
    stores: list[int] = []
    handle = uc.hook_add(UC_HOOK_MEM_WRITE,
                         lambda uc_, access, address, size, value, user: stores.append(address))
    try:
        for start, size in data:
            stores.clear()
            uc.reg_write(UC_X86_REG_EDI, start + (code & 0xFFF))
            uc.reg_write(UC_X86_REG_ESI, start + size)
            uc.emu_start(code, code + len(body))
            pages = size // 0x1000
            if len(stores) != pages or len(set(stores)) != pages:
                repeated = sorted(a for a in set(stores) if stores.count(a) > 1)
                return (f"region {start:#x}+{size:#x}: {len(stores)} stores reported for {pages} "
                        f"pages (repeated: {', '.join(f'{a:#x}' for a in repeated[:4]) or 'none'}); "
                        f"it shares RAM offsets with the code at {code:#x}")
    finally:
        uc.hook_del(handle)
    return None


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


def code_ranges(path: str) -> list[tuple[int, int]]:
    """(start, end) of the image's executable sections at its preferred base."""
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    IMAGE_SCN_MEM_EXECUTE = 0x20000000
    return [(base + s.VirtualAddress, base + s.VirtualAddress + _page_up(s.Misc_VirtualSize or s.SizeOfRawData))
            for s in pe.sections if s.Characteristics & IMAGE_SCN_MEM_EXECUTE]


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


_SYMBOLS_BY_NAME: dict[int, tuple[dict[str, int], dict[str, list[tuple[str, int]]]]] = {}


def _symbols_named(symbols: dict[str, int], name: str) -> list[tuple[str, int]]:
    """The (symbol, address) pairs of `symbols` that start with `?name@`,
    from an index built once per symbol table (12,000 specs since
    stubs-r5: a linear scan per lookup took minutes)."""
    cached = _SYMBOLS_BY_NAME.get(id(symbols))
    if cached is None or cached[0] is not symbols:
        index: dict[str, list[tuple[str, int]]] = {}
        for symbol, address in symbols.items():
            if symbol.startswith("?"):
                at = symbol.find("@")
                if at > 1:
                    index.setdefault(symbol[1:at], []).append((symbol, address))
        cached = (symbols, index)
        _SYMBOLS_BY_NAME[id(symbols)] = cached
    return cached[1].get(name, [])


def find_our_function(symbols: dict[str, int], name: str) -> int:
    """Address of our replacement `name` in s3re.dll (see
    Emulator.our_function)."""
    named = _symbols_named(symbols, name)
    found = [address for symbol, address in named
             if "Counting" not in symbol and not symbol.startswith(f"?{name}@@3")]
    if len(found) > 1:
        # A helper of the same name in an anonymous namespace (e.g.
        # NextDirection in world_create.cpp) doesn't count; functions only
        # in an anonymous namespace (the self-test's Wrong*) still match.
        found = [address for symbol, address in named
                 if "Counting" not in symbol and not symbol.startswith(f"?{name}@@3")
                 and not symbol.startswith(f"?{name}@?A0x")]
    if len(found) != 1:
        raise KeyError(f"{name}: {len(found)} matching symbols in s3re.map")
    return found[0]


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
    thrown: int | tuple | None = None  # the thrower called, if any (see _on_throw)
    probe_calls: list[tuple] = field(default_factory=list)  # virtual and API calls, in order
    spec_error: str | None = None    # a call the spec didn't declare


class Emulator:
    def __init__(self, exe_path: str, dll_path: str, map_path: str):
        self.uc = new_uc()
        self.exe_range = map_pe(self.uc, exe_path)
        self.dll_range = map_pe(self.uc, dll_path)
        self.code_ranges = code_ranges(exe_path) + code_ranges(dll_path)
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
        self.new_fails = False  # this case: operator new returns null (Spec.new_fails)
        self.new_fails_size: int | None = None  # only for this block size (Spec.new_fails_size)
        self.thrown: int | None = None
        self._map_thread_block()
        # Virtual-call and API probes.
        self.virtuals: dict[int, int | tuple[int, tuple[int, ...]]] = {}
        self.virtual_effects: dict = {}  # offset -> effect(machine, ecx, args, h), see specs.Spec
        self.callees: dict = {}          # address -> (count, convention, returns), see set_callees
        self._callee_hooks: list = []
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
        # The loaded images, to give every spec the same starting state
        # (see reset).
        self._pristine = [(base, bytes(self.uc.mem_read(base, size)))
                          for base, size in (self.exe_range, self.dll_range)]

    def reset(self) -> None:
        """Back to the state right after loading: S3.EXE and s3re.dll as in
        their files (Spec.globals write S3.EXE globals directly and would
        otherwise carry over into later specs), the stack zeroed, no world
        bytes materialized, no probes declared, no translated code cached.
        run.py calls it before every spec, so a spec's result doesn't depend
        on which specs ran before it in the same process (serial and
        parallel runs agree).

        Unicorn keeps translated blocks of guest code and doesn't notice
        when Python's mem_write changes the bytes they came from: a spec
        that writes a stub over a function another spec ran (WalkSettler's
        FindPath stub after Pathing_FindPath, found 2026-11-22) would run
        the old code. Hence the flush here, write_image for writes made by
        specs, and the flush in set_callees (code hooks only take effect in
        blocks translated after they were added)."""
        chunk = 0x10000
        for base, image in self._pristine:
            for offset in range(0, len(image), chunk):
                want = image[offset:offset + chunk]
                if bytes(self.uc.mem_read(base + offset, len(want))) != want:
                    self.uc.mem_write(base + offset, want)
        self.uc.ctl_flush_tb()
        self.uc.mem_write(STACK_BASE, bytes(STACK_SIZE))
        self.begin_case(0, [0], {})
        self.blocks = set()
        self.cover_range = (0, 0)
        self.virtuals = {}
        self.virtual_effects = {}
        self.set_callees({})
        self.new_fails = False
        self.new_fails_size = None

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
        options: dict = {}
        if isinstance(declared, int):
            count, pointers = declared, {}
        elif len(declared) == 2:
            count, pointers = declared
        else:
            count, pointers, options = declared
        esp, ret, args = self._stack_args(uc, count)
        shown = []
        for i, value in enumerate(args):
            if i in pointers and value == 0:
                shown.append(("null",))
            elif i in pointers and isinstance(pointers[i], int):
                shown.append(("contents", self.guest_read(value, pointers[i])))
            elif i in pointers:  # ((offset, size), ...): only these parts are defined
                shown.append(("contents", *(self.guest_read(value + o, n) for o, n in pointers[i])))
            else:
                shown.append(value)
        # COM methods (options["com"]) are stdcall with the interface as
        # their first stack argument; ECX is whatever the caller left there.
        ecx = None if options.get("com") else uc.reg_read(UC_X86_REG_ECX)
        self.probe_calls.append(("virtual", offset, ecx, *shown))
        eax = self._probe_value()
        if "returns" in options:  # returns(h) -> EAX, e.g. one of a set of HRESULTs
            eax = options["returns"](_mix((self.seed << 32) ^ (0x2E7C0000 + len(self.probe_calls))))
        effect = self.virtual_effects.get(offset)
        if effect is not None:
            effect(self, ecx, args, _mix((self.seed << 32) ^ (0xEFFEC700 + len(self.probe_calls))))
        self._finish_probe(uc, esp, ret, count, eax)

    def set_callees(self, callees: dict) -> None:
        """Probes calls of S3.EXE functions instead of running them:
        {address: (stack argument count, convention, returns)} with convention
        "cdecl" (caller pops), "stdcall" or "thiscall" (ECX recorded too) and
        returns "this" (EAX = ECX, a constructor), "probe" (0, 1 or random,
        as for virtual calls), "throw" (a function that never returns, such
        as Win32::ThrowWin32Error: the call is recorded and ends the run as
        a throw, compared like _CxxThrowException) or a function
        returns(h) -> EAX (h: a random
        64-bit value fixed by the case and the call's position). An optional
        fourth element effect(machine, ECX, arguments, h) runs after the
        call is recorded (a callee's side effects, written with
        guest_write); if it returns a value, that becomes EAX (a callee
        modelled in Python, e.g. a file read). An optional fifth element
        record(machine, ECX, arguments) -> tuple replaces the recorded
        arguments (e.g. a stack buffer's address, which differs between the
        original's and our frames). Recorded as ("callee", address, ECX or
        None, arguments...)."""
        for handle in self._callee_hooks:
            self.uc.hook_del(handle)
        self._callee_hooks = []
        self.callees = dict(callees)
        for address in self.callees:
            self._callee_hooks.append(self.uc.hook_add(UC_HOOK_CODE, self._on_callee, begin=address, end=address))
        self.uc.ctl_flush_tb()  # blocks translated without (or with) these hooks

    def _on_callee(self, uc, address, size, user_data):
        count, convention, returns = self.callees[address][:3]
        effect = self.callees[address][3] if len(self.callees[address]) > 3 else None
        record = self.callees[address][4] if len(self.callees[address]) > 4 else None
        esp, ret, args = self._stack_args(uc, count)
        ecx = uc.reg_read(UC_X86_REG_ECX) if convention == "thiscall" else None
        recorded = tuple(args) if record is None else tuple(record(self, ecx, args))
        self.probe_calls.append(("callee", address, ecx, *recorded))
        if returns == "throw":  # never returns: ends the run like _CxxThrowException
            self.thrown = (address,)
            uc.emu_stop()
            return
        if callable(returns):
            eax = returns(_mix((self.seed << 32) ^ (0xCA11E000 + len(self.probe_calls))))
        else:
            eax = ecx if returns == "this" else self._probe_value()
        if effect is not None:
            result = effect(self, ecx, args, _mix((self.seed << 32) ^ (0xCA11EF00 + len(self.probe_calls))))
            if result is not None:
                eax = result  # an effect that models the callee's result
        self._finish_probe(uc, esp, ret, 0 if convention == "cdecl" else count, eax)

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
        CGameData_AddStatistic), not of its counting thunk."""
        return find_our_function(self.symbols, name)

    def analyze(self, start: int, end: int | None = None) -> Function:
        """Basic blocks and constants of the function at `start`, which ends at
        the next known function start (or at `end`, for code that continues
        past a function start Ghidra put inside it)."""
        if end is None:
            end = self.function_starts[bisect.bisect_right(self.function_starts, start)]
        code = bytes(self.uc.mem_read(start, end - start))
        instructions = list(self.disassembler.disasm(code, start))
        # Without a ret (a stub that ends in a tail jump, stubs-r5), the
        # body ends at its last instruction that is not nop/int 3 padding.
        body = list(instructions)
        while body and body[-1].mnemonic in ("nop", "int3"):
            body.pop()
        last_ret = max((ins.address for ins in instructions
                        if capstone.CS_GRP_RET in ins.groups), default=body[-1].address if body else end)
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

    def _code_changed(self, address: int, size: int) -> None:
        """Python wrote [address, address+size): drops the translated blocks
        made from those bytes if they are code (see reset)."""
        for start, end in self.code_ranges:
            if address < end and start < address + size:
                self.uc.ctl_remove_cache(max(address, start), min(address + size, end))

    def write_image(self, address: int, data: bytes) -> None:
        """A write by the harness (Spec.globals) into S3.EXE or s3re.dll,
        e.g. a stub over a function's first bytes; not tracked (it is the
        case's starting state)."""
        if bytes(self.uc.mem_read(address, len(data))) != data:
            self.uc.mem_write(address, data)
            self._code_changed(address, len(data))

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
        self._code_changed(address, len(data))

    def _on_debug_log(self, uc, address, size, user_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        fmt = struct.unpack("<I", bytes(uc.mem_read(esp + 4, 4)))[0]
        base, image_size = self.exe_range
        count = 0
        if STACK_BASE <= fmt < STACK_BASE + STACK_SIZE:
            # A text formatted into a stack buffer (sprintf, then DebugLog of
            # the buffer, e.g. 0x594d40): the text, not the frame's address.
            self.debug_logs.append(("stack text", bytes(uc.mem_read(fmt, 256)).split(b"\0", 1)[0]))
            return
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
        fails = self.new_fails and (self.new_fails_size is None or count == self.new_fails_size)
        if count < HEAP_SIZE - self.heap_next and not fails:
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
        if address == CXX_THROW:
            esp = uc.reg_read(UC_X86_REG_ESP)
            pointer, info = struct.unpack("<II", bytes(uc.mem_read(esp + 4, 8)))
            self.thrown = (address, struct.unpack("<I", self.guest_read(pointer, 4))[0], info)
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
             registers: dict[str, int], cover: tuple[int, int] = (0, 0),
             max_instructions: int = MAX_INSTRUCTIONS) -> Outcome:
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
        # The x87 control word Windows gives every thread (53-bit precision,
        # round to nearest, exceptions masked); the game never changes it
        # (only _ftol does, and restores it). Unicorn starts with 0.
        uc.reg_write(UC_X86_REG_FPCW, 0x27F)
        # Nothing else of the CPU state carries over from earlier calls
        # (a case must not depend on the cases before it, see run_spec):
        # an empty x87 stack (a float result, or a run that faulted in the
        # middle, leaves values on it), cleared flags.
        uc.reg_write(UC_X86_REG_FPSW, 0)
        uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x202)

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
            uc.emu_start(address, RETURN_ADDRESS, count=max_instructions)
            pc = uc.reg_read(UC_X86_REG_EIP)
            if pc != RETURN_ADDRESS and self.thrown is None:
                outcome.fault = f"did not return within {max_instructions} instructions"
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
            if any(start <= a < end for start, end in self.code_ranges):
                self._code_changed(a, 1)  # e.g. a stub's data next to code
        return outcome
