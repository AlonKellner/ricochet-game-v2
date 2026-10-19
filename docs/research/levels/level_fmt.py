#!/usr/bin/env python3
"""Level data (rc-level/1, docs/LEVEL_FORMAT.md) in its canonical spelling.

    python3 -I docs/research/levels/level_fmt.py <in dir> <out dir>

Reads every *.json under <in dir> (the one-shot exporter's output,
scripts/testing/export_levels.gd: floats as "f" + 16 hex digits of their
float64 bytes, little-endian) and writes it under <out dir> with every float
as its shortest round-trip decimal (Python's repr: exact in any correctly
rounded reader).

The canonical spelling (rc_tool fmt reproduces it): object keys sorted, one
per line, indented by one space per level; an array of scalars on one line
("[1.0, 2.0]"), any other array one element per line; ints as ints, floats as
Python's repr; strings as JSON with non-ASCII kept.
"""
import json
import pathlib
import re
import struct
import sys

HEX = re.compile(r"^f[0-9a-f]{16}$")


def decode(v):
    if isinstance(v, str) and HEX.match(v):
        return struct.unpack("<d", bytes.fromhex(v[1:]))[0]
    if isinstance(v, list):
        return [decode(x) for x in v]
    if isinstance(v, dict):
        return {k: decode(x) for k, x in v.items()}
    return v


def _scalar(v):
    return not isinstance(v, (list, dict))


def _fmt(v, depth):
    pad = " " * (depth + 1)
    if isinstance(v, dict):
        if not v:
            return "{}"
        items = [pad + json.dumps(k, ensure_ascii=False) + ": " + _fmt(v[k], depth + 1) for k in sorted(v)]
        return "{\n" + ",\n".join(items) + "\n" + " " * depth + "}"
    if isinstance(v, list):
        if all(_scalar(x) for x in v):
            return "[" + ", ".join(_fmt(x, depth) for x in v) + "]"
        return "[\n" + ",\n".join(pad + _fmt(x, depth + 1) for x in v) + "\n" + " " * depth + "]"
    if isinstance(v, float):
        return repr(v)
    return json.dumps(v, ensure_ascii=False)


def dumps(doc):
    return _fmt(doc, 0) + "\n"


def main():
    src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    n = 0
    for f in sorted(src.rglob("*.json")):
        doc = decode(json.loads(f.read_text()))
        out = dst / f.relative_to(src)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(dumps(doc))
        # the decimals read back to the same bits
        assert json.loads(out.read_text()) == doc, f
        n += 1
    print(f"{n} levels -> {dst}")


if __name__ == "__main__":
    main()
