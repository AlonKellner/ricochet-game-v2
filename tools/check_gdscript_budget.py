#!/usr/bin/env python3
"""Stage 119: the Godot host stays thin -- at most BUDGET lines of GDScript
outside tests and addons (the game is native: docs/PORTING.md).

    python3 -I tools/check_gdscript_budget.py
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUDGET = 300


def main():
    files = [p for p in ROOT.rglob("*.gd")
             if not any(part in ("tests", "addons", "extern", ".godot", ".claude", "build-ref") for part in p.relative_to(ROOT).parts)]
    total = 0
    for p in sorted(files):
        n = len(p.read_text().splitlines())
        total += n
        print("%5d %s" % (n, p.relative_to(ROOT)))
    print("GDScript outside tests and addons: %d lines (budget %d)" % (total, BUDGET))
    return 0 if total <= BUDGET else 1


if __name__ == "__main__":
    sys.exit(main())
