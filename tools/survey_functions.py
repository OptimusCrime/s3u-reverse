#!/usr/bin/env python3
"""Function survey: classifies every known function of S3.EXE that has no row
in re/functions.csv yet, mechanically, and writes re/function_survey.csv.

Method and naming scheme: re/function_survey.md. Inputs: build/s3.asm (see
tools/asm_tools.py), Settlers3Ultimate/S3.EXE (strings, import table),
re/classes.json, re/source_files.csv, re/functions.csv.

Layers, most reliable first (a function gets the first layer that applies;
later layers only add subsystem/evidence):
  1. compiler stubs     static initializers, atexit registrations, thunks
  2. library code       C runtime / old iostreams by address, zlib/libpng
                        (non-aligned code inside the picture library),
                        Watcom helpers by call graph
  3. vtable writers     constructors/destructors from re/classes.json
  4. source files       ranges from re/source_files.csv, gaps between files
                        of one folder, the simulation gap
  5. call graph         subsystem votes of callers and callees
  6. world fields       CGameData regions touched (re/world_layout.md)
  7. imports            Windows APIs called (file, registry, GDI, ...)
  8. manual             a few hand-checked names (MANUAL below)

Usage:
  python tools/survey_functions.py            write re/function_survey.csv
  python tools/survey_functions.py --stats    only print counts
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm_tools import ROOT, load_inventory  # noqa: E402
from survey_lib import build, callers_map  # noqa: E402
import world_fields  # noqa: E402
from rtti import identifier  # noqa: E402

OUT = os.path.join(ROOT, "re", "function_survey.csv")
UNITS_OUT = os.path.join(ROOT, "re", "obj_units.csv")
COLUMNS = ["address", "name", "category", "confidence", "subsystem", "source_file", "evidence"]

STRING_CTOR = 0x401A10      # std::string::string_from_cstr
STRING_DTOR = 0x401B60      # std::string::~string
ATEXIT = 0x61E812           # _atexit
DEBUG_LOG = 0x428720        # DebugLog (empty in release; also the folded empty function)
IOSTREAM_START = 0x61C4F0   # first function of the old iostream library (MSVCIRT, static)
CRT_START = 0x61E4EF        # operator delete; C runtime to the end of .text
# Non-16-aligned code inside the Credits picture library (CPNGDecoder): zlib.
ZLIB_RANGE = (0x5B2F00, 0x5B5200)
SIM_GAP = (0x4C85A0, 0x509980)
BLOCK_GAP = 0x400
# CRT helpers std::string calls: delete, _Xlen, new, memmove/memcpy, _Xran
STRING_CRT = {0x61E4EF, 0x61E4FA, 0x61EFB9, 0x61E860, 0x61E646}
PATH_PREFIX = r"E:\Siedler3" + "\\"
MIN_BLOCK_STUBS = 8

# Subsystem of each original source file (re/source_files.md).
FILE_SUBSYSTEM = {
    "Dirk/Console.cpp": "App", "Dirk/ConsoleInit.cpp": "App", "Dirk/DGlobals.cpp": "App",
    "Dirk/dialog.cpp": "UI", "Dirk/Draw.cpp": "Render", "Dirk/DrawInitSurfaces.cpp": "Render",
    "Dirk/eventqueue.cpp": "App", "Dirk/GameParams.cpp": "GameSetup",
    "Dirk/LobbyDialogs.cpp": "Lobby", "Dirk/LobbyManager.cpp": "Lobby",
    "Dirk/mainwindow.cpp": "Window", "Dirk/MenuNetThread.cpp": "Menu",
    "Dirk/MenuPrivTools.cpp": "Menu", "Dirk/MenuShared.cpp": "Menu",
    "Dirk/Net.cpp": "Net", "Dirk/netdialogs.cpp": "UI", "Dirk/nethelp.cpp": "Net",
    "Dirk/Pathing.cpp": "Pathing", "Dirk/SaveGames.cpp": "SaveGame", "Dirk/Text.cpp": "Text",
    "Volker/DrawBuilding.cpp": "Render", "Volker/GameDataSave.cpp": "SaveGame",
    "Volker/LogicDraw.cpp": "Logic", "Volker/LogicHandleNet.cpp": "Logic",
    "Volker/LogicInit.cpp": "Logic", "Volker/GodlyEvents.cpp": "Logic",
    "Volker/landscape.cpp": "Render",
    "Rainer/Cevaland.cpp": "AI", "Rainer/CompDoBuildHouseReq.cpp": "AI",
    "Rainer/CompGExpand.cpp": "AI", "Rainer/compGlobs.cpp": "AI",
    "Rainer/CompHouseReq.cpp": "AI", "Rainer/CompMapWorkerAgent.cpp": "AI",
    "Credits/cmy_owners.h": "Credits", "Credits/cmy_scanning.cpp": "Credits",
    "Credits/cpp_exo_text.cpp": "Credits", "Credits/cpp_exo_text__diverses.h": "Picture",
    "DirectX/Gamebase.cpp": "DirectX", "DirectX/Dxbase.cpp": "DirectX",
    "DirectX/dinput.cpp": "Input", "DirectX/netsession.cpp": "Net",
    "DirectX/Ddraw.cpp": "Render", "DirectX/Dplay.cpp": "Net", "DirectX/Dsound.cpp": "Sound",
    "Dirk/lobbyclient.cpp": "Lobby", "Dirk/Lobby.cpp": "Lobby",
    "Dirk/graphicsdata.cpp": "GfxData", "Dirk/SoundData.cpp": "Sound",
    "Dirk/protocol.cpp": "Lobby",
    "Win32/Synchronize.cpp": "Win32", "Win32/synchronize.ipp": "Win32",
    "Win32/Classes.cpp": "Win32", "Win32/cdaudio.cpp": "Sound", "Win32/Registry.cpp": "Win32",
    "Win32/Internet.cpp": "Win32", "Win32/Mapping.cpp": "Win32", "Win32/Detach.cpp": "Win32",
}

NS_SUBSYSTEM = {"Lobby": "Lobby", "LobbyClient": "Lobby", "DirectX": "DirectX",
                "Win32": "Win32", "Menus": "Menu", "std": "STL"}

# Class name patterns that decide the subsystem over the position.
CLASS_SUBSYSTEM = [
    (r"^C(DirectDraw|Surface|SimpleSurface|OverlaySurface|PrimarySurface|Palette|Clipper)", "Render"),
    (r"^CDirectPlay|^CNetSession|^CGameNetSession|^CRunningSession", "Net"),
    (r"^C(DirectSound|DirectSoundBuffer|PrimaryBuffer|SoundEffectBuffer|SoundData)", "Sound"),
    (r"^CDirectInput", "Input"),
    (r"^CLobby|^CLM|^CPlayerProtocol", "Lobby"),
    (r"^C(Internet|FTP|Registry|FileMapping|Win32|Waitable|Thread|Application|Event|Mutex|"
     r"Semaphore)", "Win32"),
]

# Windows imports that point at a subsystem (only when nothing better is known).
IMPORT_SUBSYSTEM = [
    (re.compile(r"^(Reg\w+)$"), "Win32"),
    (re.compile(r"^(CreateFile|ReadFile|WriteFile|SetFilePointer|GetFileSize|FindFirstFile|"
                r"FindNextFile|DeleteFile|CopyFile|MoveFile|CreateDirectory|GetFileAttributes|"
                r"SetFileAttributes|GetDiskFreeSpace|GetDriveType|GetVolumeInformation)"), "File"),
    (re.compile(r"^(mci|aux|mixer)"), "Sound"),
    (re.compile(r"^(wave|sndPlay|PlaySound)"), "Sound"),
    (re.compile(r"^(CreateFont|TextOut|ExtTextOut|GetTextExtent|SelectObject|SetTextColor|"
                r"SetBkMode|SetBkColor|DrawText|GetTextMetrics)"), "Text"),
    (re.compile(r"^(CreateWindow|DefWindowProc|RegisterClass|ShowWindow|SetCursor|ShowCursor|"
                r"GetMessage|PeekMessage|DispatchMessage|PostMessage|SendMessage|SetWindowPos|"
                r"MessageBox|GetCursorPos|ClipCursor|SetCapture|ReleaseCapture)"), "Window"),
    (re.compile(r"^(Uuid)"), "Net"),
    (re.compile(r"^(CoCreateInstance|CoInitialize|CoUninitialize)"), "DirectX"),
    (re.compile(r"^(CreateThread|ExitThread|WaitForSingleObject|WaitForMultipleObjects|"
                r"CreateEvent|SetEvent|ResetEvent|CreateMutex|ReleaseMutex|CreateSemaphore|"
                r"ReleaseSemaphore|EnterCriticalSection|LeaveCriticalSection|"
                r"InitializeCriticalSection|DeleteCriticalSection)"), "Win32"),
]

# Hand-checked names (evidence in the notes). address -> (name, category, confidence,
# subsystem, evidence)
MANUAL = {
    0x61E4FA: ("std::_Xlen", "stl", "high", "STL",
               "MSVC6 std::_Xlen: builds a string from a literal (strlen + assign_cstr_n), "
               "constructs a logic_error-derived object (0x61e554, vtable 0x65db54 = "
               "std::length_error) and throws it; called by std::string when n > max_size."),
    0x61E646: ("std::_Xran", "stl", "high", "STL",
               "MSVC6 std::_Xran: same shape as std::_Xlen, throws std::out_of_range "
               "(vtable 0x65a538)."),
    0x61E860: ("memmove", "crt", "medium", "CRT",
               "cdecl (dst, src, n) copy that compares dst and src for overlap first: MSVC "
               "memmove/memcpy (the same code in this CRT)."),
    0x621380: ("_itoa", "crt", "high", "CRT",
               "cdecl (value, buffer, radix): negative flag only when radix == 10, as in "
               "MSVC _itoa; Win32::ThrowError calls it with radix 10 for the line number."),
    0x622C30: ("__EH_prolog", "crt", "medium", "CRT",
               "called first with eax = the function's EH handler table (mov eax, X; call): "
               "the MSVC __EH_prolog helper that sets up the C++ exception frame."),
    0x401D20: ("std::string::_Tidy_free", "stl", "medium", "STL",
               "MSVC6 basic_string: drops the reference (refcount byte at _Ptr[-1], "
               "operator delete when it reaches 0), zeroes _Ptr/_Len/_Res; like _Tidy(true) "
               "without the argument (inlined copy)."),
    0x401D60: ("std::string::assign_substr", "stl", "high", "STL",
               "MSVC6 basic_string::assign(const string&, pos, n): used by string_from_cstr "
               "(see its inventory note); 814 callers."),
    0x402170: ("std::string::_Tidy", "stl", "high", "STL",
               "MSVC6 basic_string::_Tidy(bool built): if built, decrement the refcount byte "
               "at _Ptr[-1] or operator delete; zero _Ptr/_Len/_Res; ret 4. 1025 callers."),
    0x4020A0: ("std::string::assign_cstr", "stl", "medium", "STL",
               "MSVC6 basic_string::assign(const char*): strlen (repne scasb), _Xlen check "
               "(0x61e4fa) for n > npos-3, then the same copy as assign_cstr_n. 234 callers."),
    0x4021C0: ("std::string::assign_cstr_n", "stl", "high", "STL",
               "MSVC6 basic_string::assign(const char* s, size_type n): _Xlen (0x61e4fa) when "
               "n > 0xFFFFFFFD, _Tidy/_Copy, rep movs, terminator; ret 8. 706 callers."),
    0x402260: ("std::string::max_size", "stl", "high", "STL",
               "returns 0xFFFFFFFD (npos - 2), MSVC6 basic_string::max_size."),
    0x402270: ("std::string::_Copy", "stl", "high", "STL",
               "MSVC6 basic_string::_Copy(n): capacity n|0x1f, operator new(cap+2) with "
               "catch-retry, copies the old text (the copy loop at 0x4022f0 is a separate "
               "Ghidra function)."),
    0x4022F0: ("std::string::_Copy_tail", "stl", "medium", "STL",
               "continuation of std::string::_Copy (0x402270) after the try block: uses the "
               "caller's ebp/ebx frame; not a real function start."),
    0x401C70: ("std::string::string_concat_cstr", "stl", "low", "STL",
               "builds a string from (edx string, stack const char*) through 0x402380 and "
               "assign_substr: probably an inlined operator+(const string&, const char*) "
               "result constructor. 357 callers."),
    0x407880: ("std::string::string_default", "stl", "high", "STL",
               "MSVC6 basic_string(const allocator&): copies the allocator byte, zeroes "
               "_Ptr/_Len/_Res; ret 4. 77 callers."),
    0x610960: ("Win32::ThrowError", "game", "high", "Win32",
               "builds '<text in ECX>, file <EDX>, line <arg>' (strings ', file ', ', line ', "
               "_itoa base 10) and throws an exception object of 0x18 bytes (ctor 0x6124a0) "
               "with __CxxThrowException; 102 callers (e.g. CLogicThread::OnStart when "
               "operator new fails). Win32/Classes.cpp range."),
    0x610de0: ("Win32::ThrowWin32Error", "game", "high", "Win32",
               "same message as Win32::ThrowError plus an error code turned into an HRESULT "
               "(code & 0xffff | 0x80070000, ctor 0x613220, object 0x2c bytes); 94 callers."),
    ATEXIT: ("atexit", "crt", "high", "CRT",
             "_atexit: 2,300 static-initializer stubs push a destructor stub and call it, "
             "then pop ecx (cdecl, one argument)."),
}


def h6(v):
    return f"{v:06x}"


def load_ranges():
    ranges = []
    with open(os.path.join(ROOT, "re", "source_files.csv"), encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["code_start"]:
                ranges.append((int(row["code_start"], 16), int(row["code_last_function"], 16),
                               f'{row["folder"]}/{row["file"]}', int(row["order"])))
    return ranges


def real_insns(f):
    return [t for _, t in f.insns if not t.startswith("nop") and not t.startswith("int ")]


def norm(t):
    return re.sub(r"\s+", " ", t)


IMM = r"0?([0-9A-F]+)h"


def parse_stub(f, inventory, funcs):
    """Recognize compiler-generated static initializer / atexit / thunk stubs.
    Returns (name, category, confidence, evidence) or None."""
    ins = [norm(t) for t in real_insns(f)]
    if not ins:
        return None
    text = " ; ".join(ins)

    def label(addr):
        row = inventory.get(addr)
        return f"{row['name']} (0x{addr:x})" if row else f"0x{addr:x}"

    # push -1; push text; mov ecx, global; call string ctor; ret
    m = re.fullmatch(r"push 0FFFFFFFFh ; push " + IMM + r" ; mov ecx," + IMM +
                     r" ; call 00401A10 ; ret", text)
    if m:
        s, g = int(m.group(1), 16), int(m.group(2), 16)
        lit = next((t for v, t in f.strings if v == s), None)
        lit = "?" if lit is None else lit
        return (f"StaticInit_{h6(g)}", "compiler", "high",
                f'static std::string at 0x{g:x} = "{lit[:80]}" (string_from_cstr 0x401a10)')
    # push dtorstub; call _atexit; pop ecx; ret [; mov ecx,g; jmp dtor] [; call init; jmp next]
    m = re.match(r"push " + IMM + r" ; call 0061E812 ; pop ecx ; ret(?: ; mov ecx," + IMM +
                 r" ; jmp ([0-9A-F]{8}))?", text)
    if m:
        stub = int(m.group(1), 16)
        g = int(m.group(2), 16) if m.group(2) else None
        dtor = int(m.group(3), 16) if m.group(3) else None
        if g is None and stub in funcs:
            st = " ; ".join(norm(t) for t in real_insns(funcs[stub]))
            m2 = re.match(r"mov ecx," + IMM + r" ; jmp ([0-9A-F]{8})", st)
            if m2:
                g, dtor = int(m2.group(1), 16), int(m2.group(2), 16)
        if g is not None:
            return (f"StaticDtorReg_{h6(g)}", "compiler", "high",
                    f"atexit registration of the destructor stub 0x{stub:x} "
                    f"(ecx = 0x{g:x}, jmp {label(dtor)}); the stub follows in the same Ghidra "
                    "function")
        return (f"AtexitReg_{h6(f.start)}", "compiler", "high",
                f"atexit registration of 0x{stub:x}")
    # static guard flag variant: test/or a byte, then register (local static)
    m = re.fullmatch(r"mov cl,byte ptr ds:\[" + IMM + r"\] ; mov al,1 ; test al,cl ; jne "
                     r"[0-9A-F]{8} ; or cl,al ; mov byte ptr ds:\[[0-9A-F]+h\],cl ; jmp "
                     r"[0-9A-F]{8} ; push " + IMM + r" ; call 0061E812 ; pop ecx ; ret", text)
    if m:
        return (f"StaticGuardReg_{h6(f.start)}", "compiler", "high",
                f"guarded atexit registration of 0x{int(m.group(2), 16):x} "
                f"(guard byte 0x{int(m.group(1), 16):x})")
    # mov ecx, global; call ctor; ret   /   mov ecx, global; jmp ctor
    m = re.fullmatch(r"(?:push (?:\w+|" + IMM + r") ; )*mov ecx," + IMM +
                     r" ; (?:call ([0-9A-F]{8}) ; ret|jmp ([0-9A-F]{8}))", text)
    if m and int(m.group(2), 16) >= 0x690000:
        g = int(m.group(2), 16)
        target = int(m.group(3) or m.group(4), 16)
        tname = label(target)
        if "~" in tname or "destructor" in tname.lower():
            return (f"StaticDtor_{h6(g)}", "compiler", "high",
                    f"static destructor stub: ecx = 0x{g:x}, {tname}")
        return (f"StaticInit_{h6(g)}", "compiler", "medium",
                f"static initializer: ecx = global 0x{g:x}, calls {tname} (probably its "
                "constructor; static-initializer stub shape)")
    # vtordisp / this-adjusting thunks
    m = re.fullmatch(r"sub ecx,dword ptr \[ecx-4\] ; sub ecx," + IMM + r" ; jmp ([0-9A-F]{8})",
                     text)
    if m:
        return (f"VtordispThunk_{h6(f.start)}", "compiler", "high",
                f"vtordisp thunk (virtual base adjustment ecx -= [ecx-4] + 0x"
                f"{int(m.group(1), 16):x}) to {label(int(m.group(2), 16))}")
    m = re.fullmatch(r"(add|sub) ecx," + IMM + r" ; jmp ([0-9A-F]{8})", text)
    if m:
        return (f"AdjustorThunk_{h6(f.start)}", "compiler", "high",
                f"this-adjusting thunk (ecx {m.group(1)} 0x{int(m.group(2), 16):x}) to "
                f"{label(int(m.group(3), 16))}")
    m = re.fullmatch(r"jmp ([0-9A-F]{8})", text)
    if m:
        return (f"JmpThunk_{h6(f.start)}", "compiler", "medium",
                f"single jmp to {label(int(m.group(1), 16))} (thunk or folded wrapper)")
    m = re.fullmatch(r"jmp dword ptr ds:\[" + IMM + r"\]", text)
    if m:
        return (f"ImportThunk_{h6(f.start)}", "compiler", "high",
                f"jmp through IAT slot 0x{int(m.group(1), 16):x} ({describe_imports(f)})")
    return None


def describe_imports(f):
    return ", ".join(sorted({n for _, n in f.imports})) or "?"


def vtable_writer(f, classes):
    """Constructor/destructor shape from vtable stores. Returns (class, kind) or None."""
    stores = []
    first_call = None
    for i, (a, t) in enumerate(f.insns):
        if t.startswith("call") and first_call is None:
            first_call = i
        m = re.match(r"mov\s+dword ptr \[(e\w\w)(?:\+[0-9A-F]+h)?\],0?([0-9A-F]+)h$", t)
        if m and int(m.group(2), 16) in classes:
            stores.append((i, classes[int(m.group(2), 16)]))
    if not stores:
        return None
    if first_call is not None and stores[0][0] < first_call:
        return stores[0][1], "dtor-or-inline-ctor"
    return stores[-1][1], "ctor"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--inventory", default=None, help="functions.csv to treat as existing")
    args = ap.parse_args()

    starts, funcs = build()
    baseline = {}
    with open(os.path.join(ROOT, "re", "ghidra_functions.tsv"), encoding="utf-8") as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) == 3 and p[0].startswith("0x"):
                baseline[int(p[0], 16)] = (p[1], p[2])
    inventory = load_inventory()
    if args.inventory:
        inventory = {}
        with open(args.inventory, encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                inventory[int(row["address"], 16)] = row
    callers = callers_map(funcs)
    ranges = load_ranges()
    classes_json = json.load(open(os.path.join(ROOT, "re", "classes.json"), encoding="utf-8"))
    vt_class = {}
    class_cat = {}
    for c in classes_json["classes"]:
        class_cat[c["name"]] = c["category"]
        for vt in c["vtables"]:
            vt_class[int(vt["address"], 16)] = c["name"]

    out = {}          # addr -> dict(row)
    used_names = {r["name"] for r in inventory.values()}

    def add(addr, name, category, confidence, subsystem, source_file, evidence):
        name = name.replace("KERNEL32_DLL::", "ImportThunk_").replace("OLE32_DLL::", "ImportThunk_")
        if name in used_names:
            name = f"{name}_{h6(addr)}"
        used_names.add(name)
        out[addr] = dict(address=f"0x{addr:08x}", name=name, category=category,
                         confidence=confidence, subsystem=subsystem,
                         source_file=source_file, evidence=evidence)

    # ---- per-function subsystem from source-file ranges ---------------------------
    range_starts = [r[0] for r in ranges]

    def file_of(addr):
        i = bisect.bisect_right(range_starts, addr) - 1
        if i >= 0 and ranges[i][0] <= addr <= ranges[i][1]:
            return ranges[i][2], "in"
        before = ranges[i] if i >= 0 else None
        after = ranges[i + 1] if i + 1 < len(ranges) else None
        return (before, after), "gap"

    def base_subsystem(addr):
        """(subsystem, source_file, confidence, evidence) from position only."""
        if addr >= CRT_START:
            return "CRT", "", "high", "after operator delete 0x61e4ef: statically linked C runtime"
        if addr >= IOSTREAM_START:
            return "iostream", "", "high", ("old iostream library (MSVCIRT, static): ios/"
                                            "streambuf/filebuf RTTI classes in this range, "
                                            "non-aligned code")
        if ZLIB_RANGE[0] <= addr < ZLIB_RANGE[1] and addr % 16:
            return "zlib", "Credits/cpp_exo_text__diverses.h", "medium", (
                "non-aligned code inside the picture library (CPNGDecoder): third-party "
                "zlib/libpng compiled with other options")
        where, kind = file_of(addr)
        if kind == "in":
            return FILE_SUBSYSTEM[where], where, "medium", f"inside the code range of {where}"
        before, after = where
        if SIM_GAP[0] < addr < SIM_GAP[1]:
            return "Logic", "", "medium", ("in the unnamed simulation gap between "
                                           "Volker/DrawBuilding.cpp and Volker/GameDataSave.cpp")
        if before and after:
            sb, sa = FILE_SUBSYSTEM[before[2]], FILE_SUBSYSTEM[after[2]]
            fb, fa = before[2].split("/")[0], after[2].split("/")[0]
            if sb == sa:
                return sb, "", "low", f"between {before[2]} and {after[2]} (both {sb})"
            if fb == fa and fb in ("Volker", "Rainer"):
                return {"Volker": "Logic", "Rainer": "AI"}[fb], "", "low", (
                    f"between {before[2]} and {after[2]} ({fb}\\ files)")
            return None, "", "low", f"between {before[2]} and {after[2]}"
        if before:
            return None, "", "low", f"after {before[2]}"
        return None, "", "low", f"before {after[2]}"

    # ---- layer 1 + 2 + 3 -----------------------------------------------------------
    pending = []
    for s in starts:
        if s in inventory:
            continue
        f = funcs[s]
        if s in MANUAL:
            name, cat, conf, sub, ev = MANUAL[s]
            add(s, name, cat, conf, sub, "", ev)
            continue
        stub = parse_stub(f, inventory, funcs)
        sub, src, conf, ev = base_subsystem(s)
        if stub:
            name, cat, sconf, sev = stub
            if sub in ("CRT", "iostream"):
                cat = "crt"
            add(s, name, cat, sconf, "Compiler" if cat == "compiler" else (sub or ""),
                src, sev)
            continue
        if sub in ("CRT", "iostream") and baseline.get(s, ("", ""))[0] in ("library", "thunk"):
            gname = re.sub(r"[^A-Za-z0-9_:~]", "_", baseline[s][1])
            add(s, gname, "crt", "high", sub, "",
                f"Ghidra Function ID / thunk name {baseline[s][1]}; " + ev)
            continue
        if sub in ("CRT", "iostream"):
            add(s, f"{'IOS' if sub == 'iostream' else 'CRT'}_sub_{h6(s)}", "crt", conf, sub, "",
                ev + (f"; calls {describe_imports(f)}" if f.imports else ""))
            continue
        if sub == "zlib":
            add(s, f"Zlib_sub_{h6(s)}", "game", conf, "Picture.zlib", src, ev)
            continue
        pending.append(s)

    # ---- static-initializer blocks and object-file units ---------------------------
    # Every .obj that includes the common globals header (registry key names, data
    # file names: ~20 static std::string) gets a block of initializer stubs at the
    # END of its code. Runs of stubs closer than BLOCK_GAP bytes form one block.
    def is_stub(a):
        r = out.get(a)
        return bool(r) and r["category"] == "compiler" and r["name"].startswith(
            ("StaticInit", "StaticDtor", "AtexitReg", "StaticGuard"))

    blocks = []
    for a in starts:
        if a >= IOSTREAM_START:
            break
        if is_stub(a):
            end = funcs[a].end
            if blocks and a - blocks[-1][1] < BLOCK_GAP:
                blocks[-1][1] = end
                blocks[-1][2] += 1
            else:
                blocks.append([a, end, 1])
    blocks = [b for b in blocks if b[2] >= MIN_BLOCK_STUBS]
    # non-stub functions inside a block without direct callers: initializer
    # expressions (e.g. string g = other + "text")
    for b0, b1, _ in blocks:
        for a in starts[bisect.bisect_left(starts, b0):bisect.bisect_left(starts, b1)]:
            if a in pending and not callers.get(a):
                f = funcs[a]
                gl = sorted({int(x, 16) for x in re.findall(r"ds:\[0?([0-9A-F]{6,8})h\]",
                                                              " ".join(t for _, t in f.insns))
                             if int(x, 16) >= 0x690000})
                add(a, f"StaticInitExpr_{h6(a)}", "compiler", "medium", "Compiler", "",
                    f"no direct callers, inside the static-initializer block "
                    f"0x{b0:x}..0x{b1:x}: dynamic initializer of a global "
                    f"(writes {', '.join(hex(g) for g in gl[:4]) or '?'})")
                pending.remove(a)
    block_ends = [b[1] for b in blocks]
    units = []  # (first, end, index)
    prev = 0x401000
    for i, (b0, b1, _) in enumerate(blocks):
        units.append((prev, b1, i + 1, b0))
        prev = b1
    unit_starts = [u[0] for u in units]

    def unit_of(addr):
        i = bisect.bisect_right(unit_starts, addr) - 1
        if i >= 0 and addr < units[i][1]:
            return units[i]
        return None

    unit_files = {}
    for u0, u1, idx, b0 in units:
        files = [f for a, b, f, _ in ranges if a < b0 and b >= u0]
        unit_files[idx] = files

    def unit_note(addr):
        u = unit_of(addr)
        if not u:
            return None, ""
        u0, u1, idx, b0 = u
        files = unit_files[idx]
        label = f"obj unit {idx} (0x{u0:x}..0x{u1:x}, initializer block at 0x{b0:x})"
        if len(files) == 1:
            return files[0], label + f", the only named file overlapping it: {files[0]}"
        if len(files) > 2:
            return None, ""
        if files:
            return None, label + f", named files overlapping: {', '.join(files)}"
        return f"obj{idx:03d}", label + ", no named file (unnamed source file)"

    # ---- more std::string code: functions that only call std::string internals ----
    def is_string_fn(a):
        r = inventory.get(a) or out.get(a)
        return bool(r) and r["name"].startswith("std::string::")

    changed = True
    while changed:
        changed = False
        for s in list(pending):
            f = funcs[s]
            callees = set(f.calls) | f.tails
            if not callees or f.imports or f.indirect or len(real_insns(f)) > 300:
                continue
            if [t for _, t in f.strings if len(t) >= 4]:
                continue
            strs = [c for c in callees if is_string_fn(c)]
            if not strs or not all(is_string_fn(c) or c in STRING_CRT for c in callees):
                continue
            if not any("0FFh" in t for _, t in f.insns):
                continue  # must handle the refcount byte itself (0xFF = frozen)
            add(s, f"std::string::sub_{h6(s)}", "stl", "medium", "STL", "",
                "calls only std::string internals (" + ", ".join(sorted(
                    (inventory.get(c) or out.get(c))["name"] for c in strs)) +
                ") and CRT helpers, and tests the refcount byte against 0xFF: an MSVC6 "
                f"basic_string<char> member instantiated here; {len(callers.get(s, ()))} callers")
            pending.remove(s)
            changed = True

    # ---- Watcom helpers: called only from Watcom code ------------------------------
    def cat_of(a):
        if a in inventory:
            return inventory[a]["category"]
        if a in out:
            return out[a]["category"]
        return None

    def name_of(a):
        if a in inventory:
            return inventory[a]["name"]
        if a in out:
            return out[a]["name"]
        return None

    changed = True
    while changed:
        changed = False
        for s in list(pending):
            cs = callers.get(s, set())
            if not cs:
                continue
            cats = [cat_of(c) for c in cs]
            if all(c == "watcom" for c in cats):
                cls = Counter(name_of(c).split("::")[0] for c in cs)
                owner = cls.most_common(1)[0][0]
                add(s, f"{owner}::sub_{h6(s)}", "watcom", "medium", "Watcom", "",
                    f"called only from Watcom container code ({', '.join(sorted(name_of(c) for c in cs))[:200]})")
                pending.remove(s)
                changed = True

    # ---- vtable writers (constructors/destructors) ---------------------------------
    special = {}  # addr -> (name, category, confidence, evidence); subsystem decided later
    for s in list(pending):
        f = funcs[s]
        vw = vtable_writer(f, vt_class)
        if not vw:
            continue
        cls, kind = vw
        cat = class_cat.get(cls, "game")
        n_ins = len(real_insns(f))
        cid = identifier(cls)
        if kind == "ctor":
            special[s] = (f"{cid}::{cid.split('::')[-1]}", cat, "medium",
                          f"stores the vtable of {cls} after its first call (constructor "
                          f"shape: base constructor first); {n_ins} instructions")
        else:
            special[s] = (f"{cid}::ctor_or_dtor_{h6(s)}", cat, "low",
                          f"stores the vtable of {cls} before any call: a destructor, or a "
                          f"constructor with inlined bases; {n_ins} instructions")

    # ---- names from the function's own name strings (DirectX/Win32 wrappers pass
    # "Class::Method" to their error handler) ------------------------------------------
    meth_re = re.compile(r"^((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*::~?[A-Za-z_]\w*)(?:\.\d+)?(?:[: ]|$)")
    meth_funcs = defaultdict(set)
    meth_of = {}
    for a, f in funcs.items():
        ms = set()
        for _, t in f.strings:
            m = meth_re.match(t)
            if m:
                ms.add(m.group(1))
        meth_of[a] = ms
        for m in ms:
            meth_funcs[m].add(a)
    short_names = defaultdict(set)
    for cname in class_cat:
        short_names[cname.split("::")[-1]].add(cname)
    for s in pending:
        ms = meth_of[s]
        if len(ms) != 1:
            continue
        m = next(iter(ms))
        if len(meth_funcs[m]) != 1:
            continue
        cls, meth = m.rsplit("::", 1)
        if not meth[0].isupper():
            continue  # a variable name in an assert, e.g. Dirk::Globals::lpGraphicsDataLock
        full = short_names.get(cls.split("::")[-1], set())
        if "::" not in cls and len(full) == 1:
            cls = next(iter(full))
        n_ins = len(real_insns(funcs[s]))
        special[s] = (f"{cls}::{meth}", class_cat.get(cls, "game"),
                      "high" if n_ins <= 80 else "medium",
                      f'references the name string "{m}" (only this function does; the '
                      f"wrappers pass their own name to the error handler); {n_ins} instructions")

    # ---- source files / position -----------------------------------------------------
    sub_of = {}
    for a, row in inventory.items():
        pass
    position = {}
    for s in pending:
        position[s] = base_subsystem(s)

    # subsystem of existing inventory rows, for the call-graph votes
    known_sub = {}
    for a in inventory:
        sb, *_ = base_subsystem(a)
        if inventory[a]["category"] == "game" and sb:
            known_sub[a] = sb
    for s, (sb, src, conf, ev) in position.items():
        if sb:
            known_sub[s] = sb

    # ---- call graph propagation for functions without a position subsystem --------
    for _ in range(4):
        for s in pending:
            if position[s][0] and position[s][2] != "low":
                continue
            votes = Counter()
            for c in callers.get(s, ()):
                if c in known_sub:
                    votes[known_sub[c]] += 2
            for t in set(funcs[s].calls) | funcs[s].tails:
                if t in known_sub and t != s:
                    votes[known_sub[t]] += 1
            if not votes:
                continue
            best, n = votes.most_common(1)[0]
            total = sum(votes.values())
            if position[s][0] is None and n * 3 >= total * 2:
                position[s] = (best, position[s][1], "low",
                               position[s][3] + f"; call graph votes {dict(votes)}")
                known_sub[s] = best

    # ---- world fields, imports, strings: evidence and subsystem detail ------------
    for s in pending:
        f = funcs[s]
        sb, src, conf, ev = position[s]
        detail = None
        evid = [ev]
        ufile, unote = unit_note(s)
        if unote:
            evid.append(unote)
            if ufile and not src:
                src = ufile
            if ufile in FILE_SUBSYSTEM and sb is None:
                sb = FILE_SUBSYSTEM[ufile]
        # world regions
        regions = Counter()
        for t in f.mem:
            for m in re.finditer(r"\[([^\]]+)\]", t):
                mm = re.search(r"\+([0-9A-F]+)h$", m.group(1))
                if not mm or "*" in m.group(1) and "*8" not in m.group(1) and "*4" not in m.group(1):
                    continue
                off = int(mm.group(1), 16)
                if 0x2D000 <= off < world_fields.WORLD_SIZE and not 0x400000 <= off < 0x800000:
                    name, *_ = world_fields.region_of(off)
                    if name:
                        regions[name] += 1
        if regions and sb in (None, "Logic", "AI", "SaveGame", "Pathing"):
            top = [r for r, _ in regions.most_common(3)]
            evid.append("world regions " + ", ".join(f"{r} x{n}" for r, n in regions.most_common(4)))
            if sb is None:
                sb = "Logic"
                evid.append("touches CGameData fields")
            dom = regions.most_common(1)[0]
            if dom[1] >= 3 and dom[1] * 2 >= sum(regions.values()):
                detail = dom[0]
        if f.imports:
            imps = sorted({n for _, n in f.imports})
            evid.append("imports " + ", ".join(imps[:6]) + ("..." if len(imps) > 6 else ""))
            if sb is None:
                for rx, isub in IMPORT_SUBSYSTEM:
                    if any(rx.match(n) for n in imps):
                        sb = isub
                        break
        strs = [t for _, t in f.strings if len(t) >= 4]
        if DEBUG_LOG in f.calls and strs:
            evid.append("DebugLog")
        if strs:
            uniq = list(dict.fromkeys(strs))
            evid.append("strings " + " | ".join(f'"{x[:60]}"' for x in uniq[:3]))
        cs = callers.get(s, set())
        evid.append(f"{len(cs)} callers, {len(set(f.calls))} callees, "
                    f"{len(real_insns(f))} instructions")
        paths = sorted({t[len(PATH_PREFIX):].replace(chr(92), "/") for _, t in f.strings
                        if t.startswith(PATH_PREFIX)})
        if paths:
            evid.insert(0, "references the __FILE__ path(s) " + ", ".join(paths) +
                        " (assert macro)")
            cpp = [x for x in paths if x.endswith(".cpp")]
            if len(cpp) == 1:
                src = cpp[0]
                if cpp[0] in FILE_SUBSYSTEM:
                    sb = FILE_SUBSYSTEM[cpp[0]]
                    conf = "high" if conf != "high" else conf
        if s in special:
            name, cat, sconf, sev = special[s]
            for rx, csub in CLASS_SUBSYSTEM:
                if re.search(rx, name.split("::")[-2] if "::" in name else name):
                    if sb and sb != csub:
                        evid.append(f"(position says {sb}, the class says {csub})")
                    sb = csub
                    break
            if sb is None:
                ns = name.split("::")[0]
                sb = NS_SUBSYSTEM.get(ns) or ("Watcom" if cat == "watcom" else
                                              "STL" if cat == "stl" else None)
            evid.insert(0, sev)
            add(s, name, cat, sconf, sb or "Unknown", src, "; ".join(evid))
            continue
        if sb is None:
            sb = "Unknown"
        sub_full = sb + (f".{detail}" if detail else "")
        prefix = sb.replace(".", "_")
        add(s, f"{prefix}_sub_{h6(s)}", "game", conf if sb != "Unknown" else "low",
            sub_full, src, "; ".join(evid))

    rows = [out[a] for a in sorted(out)]
    if not args.stats:
        with open(UNITS_OUT, "w", encoding="utf-8", newline="") as fh:
            w = csv.writer(fh, lineterminator="\n")
            w.writerow(["unit", "code_start", "code_end", "init_block_start", "functions",
                        "init_stubs", "first_function", "named_file_ranges_overlapping"])
            for u0, u1, idx, b0 in units:
                fs = starts[bisect.bisect_left(starts, u0):bisect.bisect_left(starts, u1)]
                stubs = sum(1 for a in fs if is_stub(a) or a in out and
                            out[a]["name"].startswith("StaticInitExpr"))
                first = fs[0] if fs else u0
                fname = (inventory.get(first) or out.get(first) or {}).get("name", "")
                w.writerow([idx, f"0x{u0:06x}", f"0x{u1:06x}", f"0x{b0:06x}", len(fs) - stubs,
                            stubs, f"0x{first:06x} {fname}", " ".join(unit_files[idx])])
    total = len(starts)
    have = len(inventory)
    print(f"known functions {total}; inventory rows {have} ({100 * have / total:.1f}%); "
          f"survey rows {len(rows)}; after merge {have + len(rows)} "
          f"({100 * (have + len(rows)) / total:.1f}%)")
    print("by category:", dict(Counter(r["category"] for r in rows)))
    print("by confidence:", dict(Counter(r["confidence"] for r in rows)))
    print("by subsystem:", dict(Counter(r["subsystem"].split(".")[0] for r in rows).most_common()))
    if not args.stats:
        with open(args.out, "w", encoding="utf-8", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=COLUMNS, lineterminator="\n")
            w.writeheader()
            w.writerows(rows)
        print("wrote", args.out)


if __name__ == "__main__":
    main()
