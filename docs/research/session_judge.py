#!/usr/bin/env python3
"""Compares two recordings of the session replay golden (docs/research/session_digest.md).

    python3 -I docs/research/session_judge.py <reference dir> <candidate dir> [--report out.md]

For every session (<name>.jsonl in either directory): identical, missing on one
side, or the first differing record (step index and op) with the fields that
differ there and how many records differ in all. Large fields are sha256 in
the golden; rerun the recorder with --full on both sides to diff their content.

Exit status: 0 identical, 2 differences (a behaviour change for the user to
judge), 1 a broken input.
"""
import json
import pathlib
import struct
import sys


def f64(v):
    if isinstance(v, str) and len(v) == 17 and v[0] == "f":
        return struct.unpack("<d", bytes.fromhex(v[1:]))[0]
    return v


def readable(v, depth=0):
    """Hex floats decoded, long values cut (for the report only)."""
    if isinstance(v, list):
        return [readable(x, depth + 1) for x in v[:8]] + (["..."] if len(v) > 8 else [])
    if isinstance(v, dict):
        return {k: readable(x, depth + 1) for k, x in list(v.items())[:12]}
    return f64(v)


def diff_fields(a, b, prefix=""):
    """The leaf paths where two records differ (one level into dictionaries)."""
    out = []
    for k in sorted(set(a) | set(b)):
        x, y = a.get(k, "<absent>"), b.get(k, "<absent>")
        if x == y:
            continue
        if isinstance(x, dict) and isinstance(y, dict) and not prefix:
            out += diff_fields(x, y, k + ".")
        else:
            out.append((prefix + k, x, y))
    return out


def load(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 1
    ref, cand = pathlib.Path(args[0]), pathlib.Path(args[1])
    report_path = sys.argv[sys.argv.index("--report") + 1] if "--report" in sys.argv else None
    names = sorted({p.name for p in ref.glob("*.jsonl")} | {p.name for p in cand.glob("*.jsonl")})
    lines = ["# Session replay: %s vs %s" % (ref, cand), ""]
    changed = 0
    for name in names:
        a, b = ref / name, cand / name
        if not a.exists() or not b.exists():
            lines.append("- `%s`: only in %s" % (name, cand if b.exists() else ref))
            changed += 1
            continue
        if a.read_bytes() == b.read_bytes():
            continue
        changed += 1
        ra, rb = load(a), load(b)
        differing = [i for i in range(max(len(ra), len(rb))) if i >= len(ra) or i >= len(rb) or ra[i] != rb[i]]
        first = differing[0]
        lines.append("- `%s`: %d of %d records differ, the first at step %d" % (name, len(differing), len(ra), first))
        if first < len(ra) and first < len(rb):
            lines.append("  - op `%s`" % ra[first].get("op"))
            for path, x, y in diff_fields(ra[first], rb[first])[:12]:
                lines.append("  - `%s`: %s -> %s" % (path, json.dumps(readable(x))[:160], json.dumps(readable(y))[:160]))
        else:
            lines.append("  - record counts %d vs %d" % (len(ra), len(rb)))
    lines += ["", "%d of %d sessions differ" % (changed, len(names))]
    text = "\n".join(lines) + "\n"
    if report_path:
        pathlib.Path(report_path).write_text(text)
    print(text, end="")
    return 2 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
