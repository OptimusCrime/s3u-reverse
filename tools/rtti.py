#!/usr/bin/env python3
"""Recover C++ classes from the MSVC RTTI in S3.EXE.

MSVC stores run-time type information for every polymorphic class:

  vtable[-1]          -> CompleteObjectLocator (COL)
  COL                 =  {signature 0, offset, cdOffset, TypeDescriptor*,
                          ClassHierarchyDescriptor*}
  TypeDescriptor      =  {type_info vtable*, spare, ".?AVName@Namespace@@"}
  ClassHierarchyDesc. =  {signature 0, attributes, numBaseClasses,
                          BaseClassDescriptor*[]}  (entry 0 is the class itself)
  BaseClassDescriptor =  {TypeDescriptor*, numContainedBases,
                          mdisp, pdisp, vdisp, attributes}

From these we get every polymorphic class, its base classes, its vtables and
therefore the address of each virtual function. We also find:
  - functions that store a vtable address (constructors, destructors, or code
    that inlined them),
  - scalar/vector deleting destructors (the compiler-generated vtable entry
    that calls the destructor and then operator delete), and the destructor
    they call.

Commands:
  scan    Write re/classes.json (machine-readable) and re/classes.md
          (class hierarchy for reading).
  apply   Add rows for the recovered functions to re/functions.csv with
          status "classified". Existing rows are never changed.

Usage: python tools/rtti.py scan|apply [--exe <path to S3.EXE>]
"""

import argparse
import bisect
import collections
import csv
import ctypes
import hashlib
import io
import json
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXE = ROOT / "Settlers3Ultimate" / "S3.EXE"
BASELINE = ROOT / "re" / "ghidra_functions.tsv"
INVENTORY = ROOT / "re" / "functions.csv"
CLASSES_JSON = ROOT / "re" / "classes.json"
CLASSES_MD = ROOT / "re" / "classes.md"
EXPECTED_SHA256 = "00a925cfdcfc3349b5d0f3b8a4c312f01b813e363b4464f6f3d6de17804d950b"

EXTRA_FUNCTIONS = ROOT / "re" / "extra_functions.tsv"

# Classes from libraries rather than the game (see category_of).
CRT_CLASSES = {"type_info", "exception", "bad_cast", "bad_typeid", "__non_rtti_object",
               # old (pre-standard) iostream library from MSVCIRT
               "ios", "istream", "ostream", "iostream", "ostream_withassign", "streambuf",
               "filebuf", "fstream", "ofstream", "ifstream", "strstreambuf", "ostrstream",
               "istrstream"}
MFC_ROOTS = {"CObject", "CNoTrackObject", "CCmdUI", "CHandleMap"}
# MFC classes that have RTTI but no vtable in S3.EXE, so their bases are unknown.
MFC_CLASSES = {"CClientDC", "CCmdTarget", "CDC", "CException", "CFile", "CFileException",
               "CGdiObject", "CMapPtrToPtr", "CMemoryException", "CMenu",
               "CNotSupportedException", "CResourceException", "CSimpleException",
               "CTempDC", "CTempGdiObject", "CTempMenu", "CTempWnd", "CTestCmdUI",
               "CThreadData", "CUserException", "CWinApp", "CWinThread", "CWnd",
               "CArchiveException"}


# ---------------------------------------------------------------------------
# PE image access
# ---------------------------------------------------------------------------

class Image:
    def __init__(self, data: bytes):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.sections = []
        table = pe + 24 + optional_size
        for i in range(count):
            entry = table + 40 * i
            name = data[entry:entry + 8].rstrip(b"\0").decode()
            vsize, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, entry + 8)
            self.sections.append((name, self.base + rva, vsize, raw_offset, raw_size))

    def section(self, name):
        return next(s for s in self.sections if s[0] == name)

    def offset(self, va):
        for _, start, _, raw_offset, raw_size in self.sections:
            if start <= va < start + raw_size:
                return raw_offset + va - start
        return None

    def va(self, offset):
        for _, start, _, raw_offset, raw_size in self.sections:
            if raw_offset <= offset < raw_offset + raw_size:
                return start + offset - raw_offset
        return None

    def u32(self, va):
        offset = self.offset(va)
        if offset is None or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, offset)[0]

    def bytes_at(self, va, size):
        offset = self.offset(va)
        return b"" if offset is None else self.data[offset:offset + size]

    def in_section(self, va, name):
        _, start, vsize, _, _ = self.section(name)
        return start <= va < start + vsize


# ---------------------------------------------------------------------------
# Names
# ---------------------------------------------------------------------------

def _undecorate(mangled: str) -> str | None:
    """Demangle '.?AVName@NS@@' with dbghelp's UnDecorateSymbolName."""
    try:
        dbghelp = ctypes.WinDLL("dbghelp")
    except (OSError, AttributeError):
        return None
    symbol = ("??_7" + mangled[4:] + "6B@").encode()  # as if it were the vftable
    buffer = ctypes.create_string_buffer(2048)
    if not dbghelp.UnDecorateSymbolName(symbol, buffer, len(buffer), 0):
        return None
    text = buffer.value.decode()
    match = re.fullmatch(r"const (.*)::`vftable'", text)
    return match.group(1) if match else None


def _simple_demangle(mangled: str) -> str:
    # Fallback for non-template names: '.?AVName@NS@@' -> 'NS::Name'.
    parts = mangled[4:].rstrip("@").split("@")
    return "::".join(reversed([p for p in parts if p]))


def demangle(mangled: str) -> str:
    return _undecorate(mangled) or _simple_demangle(mangled)


def identifier(class_name: str) -> str:
    """A name usable in the inventory and Ghidra: templates are flattened,
    e.g. CDefaultFunctor<class Menus::priv::CMainMenu> ->
    CDefaultFunctor_Menus_priv_CMainMenu."""
    def flatten(match):
        inner = re.sub(r"\b(class|struct|enum|union)\s+", "", match.group(1))
        inner = re.sub(r"[^A-Za-z0-9_]+", "_", inner).strip("_")
        return "_" + inner
    name = class_name
    while "<" in name:
        name = re.sub(r"<([^<>]*)>", flatten, name)
    return re.sub(r"[^A-Za-z0-9_:~]", "_", name)


def category_of(class_name: str, bases: list[str]) -> str:
    """game, crt (C runtime incl. old iostreams), stl (std::), mfc (Microsoft
    Foundation Classes) or watcom (Watcom C++ container library)."""
    if class_name in CRT_CLASSES:
        return "crt"
    if class_name.startswith("std::"):
        return "stl"
    if class_name.startswith("WC"):
        return "watcom"
    if (class_name.startswith(("AFX_", "_AFX_")) or class_name in MFC_ROOTS
            or class_name in MFC_CLASSES or any(base in MFC_ROOTS for base in bases)):
        return "mfc"
    return "game"


# ---------------------------------------------------------------------------
# Baseline functions
# ---------------------------------------------------------------------------

def load_baseline():
    starts, kinds = [], {}
    with BASELINE.open(encoding="utf-8") as f:
        lines = (line for line in f if not line.startswith("#"))
        for row in csv.DictReader(lines, delimiter="\t"):
            address = int(row["address"], 16)
            starts.append(address)
            kinds[address] = row["kind"]
    starts.sort()
    return starts, kinds


def containing_function(starts, address):
    index = bisect.bisect_right(starts, address) - 1
    return starts[index] if index >= 0 else None


# ---------------------------------------------------------------------------
# RTTI scan
# ---------------------------------------------------------------------------

def scan(image: Image):
    starts, kinds = load_baseline()
    text = image.section(".text")
    data_sections = [s for s in image.sections if s[0] in (".rdata", ".data")]

    # Type descriptors: the mangled name sits 8 bytes into the structure.
    type_descriptors = {}
    for match in re.finditer(rb"\.\?A[VU][\x21-\x7e]+?@@\0", image.data):
        va = image.va(match.start())
        if va is not None:
            type_descriptors[va - 8] = match.group()[:-1].decode()

    def read_hierarchy(chd):
        signature, attributes, count, array = (image.u32(chd + 4 * i) for i in range(4))
        if signature != 0 or count is None or not 1 <= count <= 64 or array is None:
            return None
        bases = []
        for i in range(count):
            bcd = image.u32(array + 4 * i)
            if bcd is None:
                return None
            td, contained, mdisp, pdisp, vdisp, battr = (
                image.u32(bcd + 4 * j) for j in range(6))
            if td not in type_descriptors:
                return None
            bases.append({"type": type_descriptors[td], "contained_bases": contained,
                          "mdisp": mdisp, "pdisp": struct.unpack("<i", struct.pack(
                              "<I", pdisp))[0], "vdisp": vdisp, "attributes": battr})
        return {"attributes": attributes, "bases": bases}

    # Complete object locators.
    locators = {}
    for _, start, _, raw_offset, raw_size in data_sections:
        for offset in range(raw_offset, raw_offset + raw_size - 20, 4):
            signature, sub_offset, cd_offset, td, chd = struct.unpack_from(
                "<IIIII", image.data, offset)
            if signature == 0 and td in type_descriptors:
                hierarchy = read_hierarchy(chd)
                if hierarchy and hierarchy["bases"][0]["type"] == type_descriptors[td]:
                    locators[image.va(offset)] = {"offset": sub_offset,
                                                  "cd_offset": cd_offset,
                                                  "type": type_descriptors[td],
                                                  "hierarchy": hierarchy}

    # Vtables: the word before a vtable points to its COL.
    col_slots = {}
    for _, start, _, raw_offset, raw_size in data_sections:
        for offset in range(raw_offset, raw_offset + raw_size - 4, 4):
            value = struct.unpack_from("<I", image.data, offset)[0]
            if value in locators:
                col_slots[image.va(offset)] = value

    vtables = []
    for col_slot, col in sorted(col_slots.items()):
        address = col_slot + 4
        slots = []
        cursor = address
        while cursor not in col_slots:
            target = image.u32(cursor)
            if target is None or not image.in_section(target, ".text"):
                break
            slots.append(target)
            cursor += 4
        vtables.append({"address": address, "col": col, "slots": slots})

    # Code that stores a vtable address: mov dword ptr [reg(+disp)], imm32.
    vtable_addresses = {v["address"] for v in vtables}
    writers = collections.defaultdict(set)
    _, text_start, text_size, text_raw, _ = text
    code = image.data[text_raw:text_raw + text_size]
    for match in re.finditer(rb"\xC7[\x00-\x07\x40-\x47\x80-\x87]", code):
        modrm = code[match.start() + 1]
        if modrm & 7 == 4:  # SIB byte present
            continue
        displacement = {0: 0, 1: 1, 2: 4}[modrm >> 6]
        if modrm >> 6 == 0 and modrm & 7 == 5:  # [disp32], not [reg]
            continue
        immediate_at = match.start() + 2 + displacement
        if immediate_at + 4 > len(code):
            continue
        value = struct.unpack_from("<I", code, immediate_at)[0]
        if value in vtable_addresses:
            function = containing_function(starts, text_start + match.start())
            if function is not None:
                writers[value].add(function)

    # Deleting destructors. The flag argument decides whether to free memory:
    #   push esi; mov esi, ecx; call ~X; test byte ptr [esp+8], 1   (scalar)
    #   ...                            test byte ptr [esp+8], 2   (vector)
    #   mov al, [esp+4]; push esi; mov esi, ecx; test al, 1        (~X inlined)
    def deleting_destructor(function):
        body = image.bytes_at(function, 64)
        if body.find(b"\xF6\x44\x24\x08\x02") >= 0:
            return "vector", None
        position = body.find(b"\xF6\x44\x24\x08\x01")
        if 0 <= position < 32:
            calls = [m.start() for m in re.finditer(rb"\xE8", body[:position])
                     if m.start() + 5 <= position]
            destructor = None
            if calls:
                last = calls[-1]
                rel = struct.unpack_from("<i", body, last + 1)[0]
                destructor = function + last + 5 + rel
                if not image.in_section(destructor, ".text"):
                    destructor = None
            return "scalar", destructor
        if body.startswith(b"\x8A\x44\x24\x04") and b"\xA8\x01" in body[4:12]:
            return "scalar", None
        return None

    # This-adjusting thunks for multiple/virtual inheritance:
    #   [sub ecx, [ecx-4]] (vtordisp)  [sub ecx, imm]  jmp target
    def adjustor_thunk(function):
        body = image.bytes_at(function, 16)
        position = 0
        if body.startswith(b"\x2B\x49\xFC"):
            position = 3
        if body[position:position + 2] == b"\x83\xE9":
            position += 3
        elif body[position:position + 2] == b"\x81\xE9":
            position += 6
        if position == 0 or body[position:position + 1] != b"\xE9":
            return None
        rel = struct.unpack_from("<i", body, position + 1)[0]
        return function + position + 5 + rel

    # Assemble classes.
    classes = {}
    for vtable in vtables:
        col = locators[vtable["col"]]
        mangled = col["type"]
        cls = classes.setdefault(mangled, {
            "mangled": mangled,
            "name": demangle(mangled),
            "bases": [demangle(b["type"]) for b in col["hierarchy"]["bases"][1:]],
            "base_details": [{"name": demangle(b["type"]), "mdisp": b["mdisp"],
                              "contained_bases": b["contained_bases"]}
                             for b in col["hierarchy"]["bases"][1:]],
            "vtables": [],
        })
        cls["vtables"].append({
            "address": vtable["address"],
            "object_offset": col["offset"],
            "slots": vtable["slots"],
            "writers": sorted(writers.get(vtable["address"], ())),
        })
    for mangled in sorted(set(type_descriptors.values()) - set(classes)):
        # Types with RTTI but no vtable of their own (e.g. only used in throw
        # or dynamic_cast) still carry a name.
        classes[mangled] = {"mangled": mangled, "name": demangle(mangled), "bases": [],
                            "base_details": [], "vtables": []}

    by_name = {cls["name"]: cls for cls in classes.values()}

    # Decide which class owns each virtual function: among all vtables that
    # contain it, the class that is a base of all the others. Functions in
    # vtables of unrelated classes were merged by the linker (identical code
    # folding) and get no class name.
    occurrences = collections.defaultdict(list)
    for cls in classes.values():
        for vtable in cls["vtables"]:
            for slot, target in enumerate(vtable["slots"]):
                occurrences[target].append((cls["name"], vtable["object_offset"], slot))

    functions = {}
    for target, uses in occurrences.items():
        names = {name for name, _, _ in uses}
        owner = None
        for candidate in names:
            if all(other == candidate or candidate in by_name[other]["bases"]
                   for other in names):
                owner = candidate
                break
        entry = {"address": target, "uses": len(uses), "classes": sorted(names),
                 "baseline_kind": kinds.get(target, "not-a-function-start")}
        if owner is not None:
            slot = min((s, o) for n, o, s in uses if n == owner)
            entry.update(owner=owner, slot=slot[0], object_offset=slot[1])
        functions[target] = entry

    for target, entry in functions.items():
        thunk = adjustor_thunk(target)
        if thunk is not None:
            entry["thunk_to"] = thunk
            continue
        destructor = deleting_destructor(target)
        if destructor:
            entry["deleting_destructor"] = destructor[0]
            if destructor[1] is not None:
                entry["destructor"] = destructor[1]

    for cls in classes.values():
        cls["category"] = category_of(cls["name"], cls["bases"])

    return {
        "image_sha256": EXPECTED_SHA256,
        "type_descriptor_count": len(type_descriptors),
        "classes": sorted(classes.values(), key=lambda c: c["name"]),
        "virtual_functions": [functions[a] for a in sorted(functions)],
    }


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def hexify(value):
    if isinstance(value, dict):
        return {k: hexify(v) if k in ("address", "col", "slots", "writers", "destructor",
                                      "thunk_to")
                or isinstance(v, (dict, list)) else v for k, v in value.items()}
    if isinstance(value, list):
        return [hexify(v) for v in value]
    if isinstance(value, int):
        return f"0x{value:08x}"
    return value


def write_markdown(result):
    classes = {c["name"]: c for c in result["classes"]}
    children = collections.defaultdict(list)
    roots = []
    for cls in result["classes"]:
        # The direct base is the first base whose own bases make up the rest
        # of the chain (single inheritance); otherwise list under each
        # top-level base.
        direct = [b for b in cls["base_details"] if b["mdisp"] == 0]
        parent = None
        for base in cls["bases"]:
            if base in classes and len(classes[base]["bases"]) == len(cls["bases"]) - 1 \
                    and set(classes[base]["bases"]) <= set(cls["bases"]):
                parent = base
                break
        if parent is None and direct:
            parent = direct[0]["name"] if direct[0]["name"] in classes else None
        if parent:
            children[parent].append(cls["name"])
        else:
            roots.append(cls["name"])

    def size(cls):
        main = next((v for v in cls["vtables"] if v["object_offset"] == 0), None)
        return len(main["slots"]) if main else 0

    lines = [
        "# Classes in S3.EXE",
        "",
        "Generated by `python tools/rtti.py scan` from the MSVC RTTI in S3.EXE.",
        "Do not edit by hand. Machine-readable data is in `classes.json`.",
        "",
        f"{len(classes)} classes with RTTI. Each entry shows the vtable address and its "
        "number of virtual functions. Classes with more than one base list the others "
        "in brackets.",
        "",
        "```",
    ]

    def emit(name, depth):
        cls = classes[name]
        main = next((v for v in cls["vtables"] if v["object_offset"] == 0), None)
        label = f"{'  ' * depth}{name}"
        if cls["category"] != "game":
            label += f"  ({cls['category']})"
        if main:
            label += f"  vtable 0x{main['address']:08x}, {size(cls)} virtuals"
        others = [b for b in cls["base_details"] if b["mdisp"] != 0]
        if others:
            label += "  [also: " + ", ".join(f"{b['name']} @+{b['mdisp']:#x}"
                                             for b in others) + "]"
        lines.append(label)
        for child in sorted(children[name]):
            emit(child, depth + 1)

    for root in sorted(roots):
        emit(root, 0)
    lines.append("```")
    CLASSES_MD.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def extra_function_starts(result, kinds):
    """Function starts proven by the RTTI that Ghidra's analysis did not create."""
    extra = {}
    for f in result["virtual_functions"]:
        if f["address"] not in kinds:
            extra[f["address"]] = "virtual function (vtable entry)"
        for key, what in (("destructor", "destructor (called by a deleting destructor)"),
                          ("thunk_to", "target of a this-adjusting thunk")):
            if key in f and f[key] not in kinds:
                extra.setdefault(f[key], what)
    return extra


def write_extra_functions(extra):
    with EXTRA_FUNCTIONS.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# Function starts in S3.EXE that Ghidra's auto-analysis did not create but\n")
        out.write("# the MSVC RTTI proves exist (mostly functions only reachable through a\n")
        out.write("# vtable). Generated by `python tools/rtti.py scan`; do not edit.\n")
        out.write("# Ghidra cannot decompile these; disassemble them with inspect_with_objdump.\n")
        out.write("address\tkind\tevidence\n")
        for address in sorted(extra):
            out.write(f"0x{address:08x}\trtti\t{extra[address]}\n")


def command_scan(image):
    result = scan(image)
    CLASSES_JSON.write_text(json.dumps(hexify(result), indent=1) + "\n", encoding="utf-8",
                            newline="\n")
    write_markdown(result)
    _, kinds = load_baseline()
    extra = extra_function_starts(result, kinds)
    write_extra_functions(extra)

    functions = result["virtual_functions"]
    owned = sum(1 for f in functions if "owner" in f)
    with_vtables = sum(1 for c in result["classes"] if c["vtables"])
    writers = {w for c in result["classes"] for v in c["vtables"] for w in v["writers"]}
    print(f"type descriptors: {result['type_descriptor_count']}")
    print(f"classes: {len(result['classes'])} ({with_vtables} with vtables)")
    print(f"distinct virtual functions: {len(functions)} "
          f"({owned} assigned to a class, {len(functions) - owned} shared by unrelated "
          "classes)")
    print(f"deleting destructors: "
          f"{sum(1 for f in functions if 'deleting_destructor' in f)}, "
          f"destructors found through them: {sum(1 for f in functions if 'destructor' in f)}")
    print(f"functions that store a vtable (constructors/destructors/inlined): "
          f"{len(writers)}")
    print(f"this-adjusting thunks: {sum(1 for f in functions if 'thunk_to' in f)}")
    categories = collections.Counter(c["category"] for c in result["classes"])
    print("classes by category: " + ", ".join(f"{k} {v}" for k, v in sorted(categories.items())))
    print(f"function starts Ghidra missed: {len(extra)}")
    print(f"wrote {CLASSES_JSON.relative_to(ROOT)}, {CLASSES_MD.relative_to(ROOT)} and "
          f"{EXTRA_FUNCTIONS.relative_to(ROOT)}")
    return 0


def command_apply(image):
    result = scan(image)
    _, kinds = load_baseline()
    for address in extra_function_starts(result, kinds):
        kinds[address] = "rtti"
    class_category = {c["name"]: c["category"] for c in result["classes"]}
    with INVENTORY.open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        columns = reader.fieldnames
        rows = list(reader)
    existing = {int(r["address"], 16) for r in rows}
    used_names = {r["name"] for r in rows}

    proposals = {}

    def propose(address, name, category, note):
        if address in existing or address in proposals:
            return
        if kinds.get(address) not in ("function", "library", "rtti"):
            return
        unique = name
        suffix = 2
        while unique in used_names:
            unique = f"{name}_{suffix}"
            suffix += 1
        used_names.add(unique)
        proposals[address] = {"address": f"0x{address:08x}", "name": unique,
                              "category": category, "status": "classified",
                              "hook": "none", "verified_by": "", "notes": note}

    # Destructors first (strongest evidence), then deleting destructors, then
    # the remaining virtual functions by slot.
    for f in result["virtual_functions"]:
        if "owner" in f and "destructor" in f:
            owner = identifier(f["owner"])
            short = owner.split("::")[-1]
            propose(f["destructor"], f"{owner}::~{short}", class_category[f["owner"]],
                    f"Destructor; called by the scalar deleting destructor at "
                    f"0x{f['address']:08x}.")
    for f in result["virtual_functions"]:
        if "owner" not in f:
            continue
        owner = identifier(f["owner"])
        category = class_category[f["owner"]]
        where = f"vtable slot {f['slot']}"
        if f["object_offset"]:
            where += f" of the base subobject at +{f['object_offset']:#x}"
        if "thunk_to" in f:
            suffix = f"_sub{f['object_offset']:x}" if f["object_offset"] else ""
            propose(f["address"], f"{owner}::vf{f['slot']:02d}{suffix}_thunk", "compiler",
                    f"This-adjusting thunk ({where}); jumps to 0x{f['thunk_to']:08x}.")
        elif "deleting_destructor" in f:
            propose(f["address"], f"{owner}::{f['deleting_destructor']}_deleting_destructor",
                    category, f"Compiler-generated {f['deleting_destructor']} deleting "
                    f"destructor ({where}).")
        else:
            suffix = f"_sub{f['object_offset']:x}" if f["object_offset"] else ""
            propose(f["address"], f"{owner}::vf{f['slot']:02d}{suffix}", category,
                    f"Virtual function, {where}; purpose not yet known.")

    # Functions shared by unrelated classes: the linker merged identical
    # bodies. Name the recognizable ones.
    for f in result["virtual_functions"]:
        if "owner" in f:
            continue
        body = image.bytes_at(f["address"], 16)
        if body.startswith(b"\x6A\x19\xE8") and body[7:9] == b"\x59\xC3":
            propose(f["address"], "_purecall", "crt",
                    "CRT _purecall: fills vtable slots of pure virtual functions; calls "
                    f"_amsg_exit(25). In {f['uses']} vtable slots.")
        elif body[:1] == b"\xC3":
            propose(f["address"], "NoOp", "game",
                    f"Empty virtual function (ret), shared by {f['uses']} vtable slots "
                    "through identical code folding.")
        elif body[:1] == b"\xC2" and body[2:3] == b"\x00":
            size = body[1]
            propose(f["address"], f"NoOp_ret{size}", "game",
                    f"Empty virtual function (ret {size}: {size // 4} argument(s)), shared "
                    f"by {f['uses']} vtable slots through identical code folding.")

    if not proposals:
        print("nothing to add")
        return 0
    rows.extend(proposals.values())
    rows.sort(key=lambda r: int(r["address"], 16))
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=columns, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    INVENTORY.write_text(buffer.getvalue(), encoding="utf-8", newline="\n")
    counts = collections.Counter(p["category"] for p in proposals.values())
    print(f"added {len(proposals)} rows to {INVENTORY.relative_to(ROOT)} "
          f"({', '.join(f'{k} {v}' for k, v in sorted(counts.items()))})")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("scan", "apply"))
    parser.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    args = parser.parse_args()

    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        print(f"{args.exe} is not the expected S3.EXE build", file=sys.stderr)
        return 1
    image = Image(data)
    return command_scan(image) if args.command == "scan" else command_apply(image)


if __name__ == "__main__":
    sys.exit(main())
