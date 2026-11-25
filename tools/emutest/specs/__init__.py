"""Emulator test specs (see docs/tools/emulator.md).

What tools/emutest/run.py tests: one Spec per replaced function.

A Spec names the original function (address in S3.EXE) and ours (the C++
function name in s3re.dll), the calling convention, how to generate each
stack argument, which bits of EAX must match, and optionally world fields
to pin per case (`presets`, e.g. a small map size so map-wide loops stay
fast in the emulator) and S3.EXE globals to set per case (`globals`). Everything else in the world is lazily random (see
emu.py).

Argument generators take (rng, pool); `pool` holds small numbers and the
constants found in the original function's code.

The specs are split by subsystem (session 51): common.py holds the Spec
class, the argument generators and the helpers several parts use; each
other part holds the specs of one folder of src/s3re/game (or src/s3re)
and their own helpers; self_test.py the deliberately wrong pairings;
covered_elsewhere.py the blocks with evidence elsewhere. The parts are
executed in PARTS order into this module's namespace, exactly like the
former single file, so `specs.SPECS`, `specs.Spec`, `specs.SCRATCH` and
every helper stay available as before. A new spec goes into the part of
the folder its replacement's .cpp file is in (a new folder: add a part
here); helpers used by several parts go into common.py.
"""
from __future__ import annotations

import ast as _ast
import os as _os

PARTS = ['common', 'ai', 'buildings', 'economy', 'environment', 'fileformats', 'framework', 'graphics', 'logic', 'map', 'mapgen', 'msvc6', 'net', 'pathing', 'settlers', 'sound', 'spells', 'ui', 'watcom', 'world', 'self_test', 'covered_elsewhere']


class NameClashError(Exception):
    """A top-level name of the spec parts was bound twice to different values."""


def _definition_key(node, value):
    """What a top-level binding is compared by: a def or class by its source
    (two parts with the same helper text don't clash), anything else by
    value (==, identity for objects without equality)."""
    if isinstance(node, (_ast.FunctionDef, _ast.AsyncFunctionDef, _ast.ClassDef)):
        return ("source", _ast.dump(node, include_attributes=False))
    return ("value", value)


def _same(old, new) -> bool:
    if old[0] != new[0]:
        return False
    if old[0] == "source":
        return old[1] == new[1]
    try:
        return old[1] is new[1] or bool(old[1] == new[1])
    except Exception:  # noqa: BLE001 (e.g. numpy-like ambiguous truth values)
        return False


def _load() -> None:
    """Execute the parts statement by statement into this module's
    namespace and fail on a top-level name that a later statement (in the
    same part or a later one) binds to a different value or definition.
    Names resolve at call time, so such a redefinition would silently
    change every earlier spec whose helpers use the name (GFX_HEADERS,
    session 55; five helpers in session 57). Names starting with "_" are
    exempt (loop temporaries); mutating an object in place (SPECS += [...],
    TABLE[key] = ...) is not a rebinding."""
    here = _os.path.dirname(__file__)
    namespace = globals()
    bound: dict[str, tuple[tuple, str]] = {}
    clashes: list[str] = []
    for _part in PARTS:
        _path = _os.path.join(here, _part + ".py")
        with open(_path, encoding="utf-8") as _f:
            tree = _ast.parse(_f.read(), _path)
        for node in tree.body:
            before = {name: namespace.get(name, _MISSING) for name in _bound_names(node)}
            exec(compile(_ast.Module(body=[node], type_ignores=[]), _path, "exec"), namespace)  # noqa: S102
            for name in before:
                if name.startswith("_") or name not in namespace:
                    continue
                key = _definition_key(node, namespace[name])
                where = f"{_part}.py:{node.lineno}"
                if name in bound and not _same(bound[name][0], key):
                    clashes.append(f"{name}: {bound[name][1]} and {where}")
                bound[name] = (key, where)
    if clashes:
        raise NameClashError("top-level names bound twice with different values in the spec parts "
                             "(rename one; see docs/tools/emulator.md):"
                             + "".join("\n  " + clash for clash in clashes))


_MISSING = object()


def _bound_names(node) -> list[str]:
    """The names a top-level statement binds (not subscripts or attributes)."""
    if isinstance(node, (_ast.FunctionDef, _ast.AsyncFunctionDef, _ast.ClassDef)):
        return [node.name]
    targets = []
    if isinstance(node, _ast.Assign):
        targets = node.targets
    elif isinstance(node, (_ast.AnnAssign, _ast.AugAssign)):
        targets = [node.target]
    elif isinstance(node, (_ast.For, _ast.AsyncFor)):
        targets = [node.target]
    elif isinstance(node, (_ast.With, _ast.AsyncWith)):
        targets = [item.optional_vars for item in node.items if item.optional_vars is not None]
    elif isinstance(node, (_ast.Import, _ast.ImportFrom)):
        return [(alias.asname or alias.name).split(".")[0] for alias in node.names]
    names = []

    def collect(target):
        if isinstance(target, _ast.Name):
            names.append(target.id)
        elif isinstance(target, (_ast.Tuple, _ast.List)):
            for element in target.elts:
                collect(element)
        elif isinstance(target, _ast.Starred):
            collect(target.value)

    for target in targets:
        collect(target)
    return names


_load()
