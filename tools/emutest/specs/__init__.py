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

import os as _os

PARTS = ['common', 'ai', 'buildings', 'economy', 'environment', 'framework', 'graphics', 'logic', 'map', 'mapgen', 'msvc6', 'net', 'pathing', 'settlers', 'spells', 'ui', 'watcom', 'world', 'self_test', 'covered_elsewhere']


def _load() -> None:
    here = _os.path.dirname(__file__)
    for _part in PARTS:
        _path = _os.path.join(here, _part + ".py")
        with open(_path, encoding="utf-8") as _f:
            exec(compile(_f.read(), _path, "exec"), globals())  # noqa: S102


_load()
