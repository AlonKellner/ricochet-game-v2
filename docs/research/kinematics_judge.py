#!/usr/bin/env python3
"""The native player controller (Stage 117, rcg/kinematics.hpp) against Godot's
CharacterBody2D on the kinematic sessions (data/sessions/*__walk.json).

    python3 -I docs/research/kinematics_judge.py <godot records dir> <native records dir> [--report out.md]

Per hold step: the deviation of the native trajectory from Godot's (the largest
and the final, px), the highest point (min y) of each, and each trajectory's
smallest clearance from the level's solid surfaces (the player's radius is 12:
below 12 is a penetration). The solids are the player-solid surfaces (lines,
arcs, full circles) and the level's colliders.
"""
import json
import math
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
RADIUS = 12.0


def f64(v):
    return struct.unpack("<d", bytes.fromhex(v[1:]))[0] if isinstance(v, str) and v.startswith("f") else v


def pt(p):
    return (f64(p[0]), f64(p[1]))


def circumcenter(a, b, c):
    d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]))
    if abs(d) < 1e-12:
        return None
    ux = ((a[0] ** 2 + a[1] ** 2) * (b[1] - c[1]) + (b[0] ** 2 + b[1] ** 2) * (c[1] - a[1]) + (c[0] ** 2 + c[1] ** 2) * (a[1] - b[1])) / d
    uy = ((a[0] ** 2 + a[1] ** 2) * (c[0] - b[0]) + (b[0] ** 2 + b[1] ** 2) * (a[0] - c[0]) + (c[0] ** 2 + c[1] ** 2) * (b[0] - a[0])) / d
    return (ux, uy)


def solids(level):
    out = []
    for s in level["surfaces"]:
        if not s.get("player_solid"):
            continue
        if "circle" in s:
            out.append(("circle", tuple(s["circle"]["center"]), s["circle"]["radius"]))
            continue
        a, b, v = tuple(s["start"]), tuple(s["end"]), tuple(s["via"])
        c = circumcenter(a, v, b)
        if c is None:
            out.append(("line", a, b))
        else:
            out.append(("arc", a, b, v, c))
    for k in level.get("colliders", []):
        out.append(("line", tuple(k["a"]), tuple(k["b"])))
    return out


def dist_line(p, a, b):
    ex, ey = b[0] - a[0], b[1] - a[1]
    l2 = ex * ex + ey * ey
    u = max(0.0, min(1.0, ((p[0] - a[0]) * ex + (p[1] - a[1]) * ey) / l2)) if l2 else 0.0
    return math.hypot(p[0] - a[0] - ex * u, p[1] - a[1] - ey * u)


def dist_arc(p, a, b, v, c):
    r = math.hypot(a[0] - c[0], a[1] - c[1])
    ang = lambda q: math.atan2(q[1] - c[1], q[0] - c[0])
    a0, av, a1 = ang(a), ang(v), ang(b)
    sweep = (a1 - a0) % (2 * math.pi)
    if (av - a0) % (2 * math.pi) > sweep:
        sweep -= 2 * math.pi
    t = (ang(p) - a0) % (2 * math.pi) if sweep > 0 else (a0 - ang(p)) % (2 * math.pi)
    if t <= abs(sweep):
        return abs(math.hypot(p[0] - c[0], p[1] - c[1]) - r)
    return min(math.hypot(p[0] - a[0], p[1] - a[1]), math.hypot(p[0] - b[0], p[1] - b[1]))


def clearance(p, sol):
    best = math.inf
    for s in sol:
        if s[0] == "line":
            d = dist_line(p, s[1], s[2])
        elif s[0] == "circle":
            d = abs(math.hypot(p[0] - s[1][0], p[1] - s[1][1]) - s[2])
        else:
            d = dist_arc(p, *s[1:])
        best = min(best, d)
    return best


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    gdir, ndir = pathlib.Path(args[0]), pathlib.Path(args[1])
    report = sys.argv[sys.argv.index("--report") + 1] if "--report" in sys.argv else None
    lines = ["# The native player controller against Godot's (the walk sessions)", "",
             "| session | step | held | ticks | max dev | final dev | Godot top y | native top y | Godot min clearance | native min clearance |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    worst = 0.0
    for gf in sorted(gdir.glob("*__walk.jsonl")):
        nf = ndir / gf.name
        if not nf.exists():
            continue
        script = json.loads((ROOT / "data" / "sessions" / gf.name.replace(".jsonl", ".json")).read_text())
        level = json.loads((ROOT / "data" / "levels" / (script["level"] + ".json")).read_text())
        sol = solids(level)
        g_recs = [json.loads(l) for l in gf.read_text().splitlines() if l]
        n_recs = [json.loads(l) for l in nf.read_text().splitlines() if l]
        for gr, nr in zip(g_recs, n_recs):
            if "trajectory" not in gr:
                continue
            gt = [pt(p) for p in gr["trajectory"]]
            nt = [pt(p) for p in nr["trajectory"]]
            devs = [math.hypot(a[0] - b[0], a[1] - b[1]) for a, b in zip(gt, nt)]
            step = script["steps"][gr["i"] - 1]
            worst = max(worst, max(devs))
            lines.append("| %s | %d | %s | %d | %.2f | %.2f | %.1f | %.1f | %.2f | %.2f |" % (
                gf.name.replace("__walk.jsonl", ""), gr["i"], "+".join(a.replace("move_", "") for a in step["actions"]) or "-",
                step["ticks"], max(devs), devs[-1], min(p[1] for p in gt), min(p[1] for p in nt),
                min(clearance(p, sol) for p in gt), min(clearance(p, sol) for p in nt)))
    lines += ["", "Largest deviation: %.2f px. Clearance below %.0f is a penetration." % (worst, RADIUS)]
    text = "\n".join(lines) + "\n"
    if report:
        pathlib.Path(report).write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
