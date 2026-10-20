#!/usr/bin/env python3
"""The session scripts of the replay golden (docs/research/session_digest.md).

Writes data/sessions/<level>__<name>.json (rc-session/1) for every level with
a GameManager, from the level dumps (bench/scenes/*.surfaces.json): engine-
neutral input scripts that the game's hosts play (rc_tool replay records the
golden since Stage 119; the GDScript recorder did until then). Run once; the scripts are committed input
data (a new level gets its own by a rerun with --only).

    python3 -I docs/research/sessions/gen_sessions.py [--only <substring>]

Every coordinate is a multiple of 1/4 px (exact in any decimal parser).

Sessions per level:
  tour      hover both sides of every interactive surface, plan clicks (pieces,
            the B7 snap band of circle directionals), the display / visibility /
            gravity toggles, a fire with a speed-up, undo, remove and clear
            clicks, reset, the infinity entry (an edge click) and a shot through it
  shots     the player teleported to a free point; a shot at every interactive
            surface in turn (the hit effects accumulate: toggles, targets,
            spawns), undo, reset; shots along one-entry plans
  walk      (kinematic, the levels where the player moves) movement and jumps,
            gravity toggled: evidence for the native controller, not a gate
"""
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
BOUNDS = (0.0, 0.0, 1920.0, 1080.0)   # no level overrides LevelSettings.level_bounds
HOVER_OFF = 6.0     # px off a surface: inside the click tolerance (24), on one side
FREE_CLEAR = 40.0   # a free point is this far from every surface
MAX_HOVER = 10
MAX_PLAN = 3
MAX_SHOTS = 8
KINEMATIC = ["main", "open_room_playable", "playground/plannable_inversion", "playground/inversion_grid"]


def q(x):
    return round(x * 4.0) / 4.0


def pt(p):
    return [q(p[0]), q(p[1])]


def add(a, b, k=1.0):
    return (a[0] + b[0] * k, a[1] + b[1] * k)


def unit(v):
    n = math.hypot(v[0], v[1])
    return (v[0] / n, v[1] / n) if n > 0 else (0.0, 0.0)


def circumcenter(a, b, c):
    d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]))
    if abs(d) < 1e-9:
        return None
    ux = ((a[0] ** 2 + a[1] ** 2) * (b[1] - c[1]) + (b[0] ** 2 + b[1] ** 2) * (c[1] - a[1]) + (c[0] ** 2 + c[1] ** 2) * (a[1] - b[1])) / d
    uy = ((a[0] ** 2 + a[1] ** 2) * (c[0] - b[0]) + (b[0] ** 2 + b[1] ** 2) * (a[0] - c[0]) + (c[0] ** 2 + c[1] ** 2) * (b[0] - a[0])) / d
    return (ux, uy)


def param_points(s, fractions):
    """Points on surface s at the given fractions of its length, with the unit normal there."""
    if s.get("full"):
        (cx, cy), r = s["circle"]["center"], s["circle"]["radius"]
        out = []
        for f in fractions:
            a = 2 * math.pi * f
            n = (math.cos(a), math.sin(a))
            out.append(((cx + r * n[0], cy + r * n[1]), n))
        return out
    a, b, v = tuple(s["start"]), tuple(s["end"]), tuple(s["via"])
    c = None if s.get("line") else circumcenter(a, v, b)
    if c is None:
        n = unit((-(b[1] - a[1]), b[0] - a[0]))
        return [((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f), n) for f in fractions]
    r = math.hypot(a[0] - c[0], a[1] - c[1])
    ang = lambda p: math.atan2(p[1] - c[1], p[0] - c[0])
    a0, av, a1 = ang(a), ang(v), ang(b)
    tau = 2 * math.pi
    sweep = (a1 - a0) % tau
    if (av - a0) % tau > sweep:      # through the via the other way round
        sweep = sweep - tau
    out = []
    for f in fractions:
        t = a0 + sweep * f
        n = (math.cos(t), math.sin(t))
        out.append(((c[0] + r * n[0], c[1] + r * n[1]), n))
    return out


def polyline(s, k=96):
    return [p for p, _ in param_points(s, [i / (k - 1) for i in range(k)])]


def interactive(s):
    sides = [s.get("left"), s.get("right")]
    res = s.get("resolver")
    if res:
        for e in res["table"].values():
            sides += [e.get("left"), e.get("right")]
    return any(d and d.get("interactive") for d in sides)


def is_edge(s):
    x0, y0, x1, y1 = BOUNDS
    on = lambda p: p[0] in (x0, x1) or p[1] in (y0, y1)
    return s.get("line") and on(s["start"]) and on(s["end"]) and not interactive(s)


def free_point(surfaces, near):
    """The grid point (20 px) nearest `near` at least FREE_CLEAR from every surface and inside the bounds' margin."""
    pts = [p for s in surfaces if not is_edge(s) for p in polyline(s)]
    best = None
    for gx in range(60, 1861, 20):
        for gy in range(60, 1021, 20):
            if any(math.hypot(gx - p[0], gy - p[1]) < FREE_CLEAR for p in pts):
                continue
            d = math.hypot(gx - near[0], gy - near[1])
            if best is None or d < best[0]:
                best = (d, (float(gx), float(gy)))
    return best[1]


def directional_snaps(s):
    """B7: clicks inside the snap band (8 px) of a circle directional's diameter across its normal."""
    out = []
    side = s.get("left") or {}
    if side.get("type") != "circle_directional_projection" or s.get("line"):
        return out
    c = (tuple(s["circle"]["center"]) if s.get("full") else circumcenter(tuple(s["start"]), tuple(s["via"]), tuple(s["end"])))
    if c is None:
        return out
    r = math.hypot(s["start"][0] - c[0], s["start"][1] - c[1]) if not s.get("full") else s["circle"]["radius"]
    n = unit(tuple(side["normal"]))
    perp = (-n[1], n[0])
    poly = polyline(s)
    for sign in (1, -1):
        e = add(c, perp, sign * r)
        if min(math.hypot(e[0] - p[0], e[1] - p[1]) for p in poly) < 12:
            out += [pt(add(e, n, 4)), pt(add(e, n, -4))]
    return out


def tour(surfaces, f1):
    inter = [s for s in surfaces if interactive(s) and not s.get("pre_transform")]
    steps = [{"op": "cursor", "at": pt(f1)}]
    for s in inter[:MAX_HOVER]:
        (p, n), = param_points(s, [0.5])
        steps += [{"op": "cursor", "at": pt(add(p, n, HOVER_OFF))}, {"op": "cursor", "at": pt(add(p, n, -HOVER_OFF))}]
    for i, s in enumerate(inter[:MAX_PLAN]):
        (p, n), = param_points(s, [0.3 if i % 2 == 0 else 0.7])
        steps += [{"op": "cursor", "at": pt(add(p, n, HOVER_OFF if i % 2 == 0 else -HOVER_OFF))}, {"op": "click", "button": "left"}]
    steps += [{"op": "cursor", "at": pt(f1)}]
    steps += [{"op": "key", "action": "cycle_display"}] * 3
    steps += [{"op": "key", "action": "toggle_visibility"}] * 2
    steps += [{"op": "fire", "speedup_at": 30}, {"op": "key", "action": "undo"}]
    if inter:
        (p, n), = param_points(inter[0], [0.5])
        at = pt(add(p, n, HOVER_OFF))
        steps += [{"op": "cursor", "at": at}, {"op": "click", "button": "left"}, {"op": "click", "button": "right"},
                  {"op": "click", "button": "left"}, {"op": "cursor", "at": pt(f1)}, {"op": "click", "button": "right"}]
    for s in inter:
        for at in directional_snaps(s):
            steps += [{"op": "cursor", "at": at}, {"op": "click", "button": "left"}]
    steps += [{"op": "cursor", "at": pt(f1)}, {"op": "key", "action": "clear_plan"}, {"op": "key", "action": "reset"}]
    steps += [{"op": "key", "action": "toggle_gravity"}] * 2
    # the infinity entry: the edge band (10 px inside the right edge), a shot through it
    steps += [{"op": "cursor", "at": [1910.0, 540.0]}, {"op": "click", "button": "left"},
              {"op": "cursor", "at": [10.0, 300.0]}, {"op": "click", "button": "left"},
              {"op": "cursor", "at": pt(f1)}, {"op": "fire", "speedup_at": 20},
              {"op": "key", "action": "undo"}, {"op": "key", "action": "reset"}]
    return steps


def shots(surfaces, f1, f2):
    inter = [s for s in surfaces if interactive(s) and not s.get("pre_transform")]
    steps = [{"op": "teleport", "at": pt(f2)}, {"op": "cursor", "at": pt(f1)}]
    for s in inter[:MAX_SHOTS]:
        (p, _), = param_points(s, [0.5])
        aim = add(p, unit((p[0] - f2[0], p[1] - f2[1])), 40.0)
        steps += [{"op": "cursor", "at": pt(aim)}, {"op": "fire", "speedup_at": 20}]
    steps += [{"op": "key", "action": "undo"}, {"op": "key", "action": "undo"}, {"op": "key", "action": "reset"}]
    for s in inter[:MAX_PLAN]:
        (p, n), = param_points(s, [0.5])
        steps += [{"op": "cursor", "at": pt(add(p, n, HOVER_OFF))}, {"op": "click", "button": "left"},
                  {"op": "cursor", "at": pt(f1)}, {"op": "fire", "speedup_at": 20}, {"op": "key", "action": "clear_plan"}]
    steps += [{"op": "key", "action": "reset"}]
    return steps


def walk():
    hold = lambda acts, n: {"op": "hold", "actions": acts, "ticks": n}
    return [{"op": "cursor", "at": [1200.0, 400.0]},
            hold(["move_right"], 45), hold(["move_left"], 90), hold(["move_down"], 45), hold(["move_up"], 90),
            hold(["move_right", "move_up"], 60), hold(["move_left", "move_down"], 60),
            {"op": "key", "action": "toggle_gravity"}, hold([], 30),
            hold(["move_right"], 45), hold(["move_up"], 30), hold([], 60),
            {"op": "key", "action": "toggle_gravity"}, hold(["move_down", "move_left"], 60)]


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else ""
    out_dir = ROOT / "data" / "sessions"
    out_dir.mkdir(parents=True, exist_ok=True)
    n = 0
    for tscn in sorted((ROOT / "scenes").rglob("*.tscn")):
        if "game_manager.gd" not in tscn.read_text():
            continue
        level = tscn.relative_to(ROOT / "scenes").with_suffix("").as_posix()
        if only and only not in level:
            continue
        dump = json.loads((ROOT / "bench" / "scenes" / (level.replace("/", "__") + ".surfaces.json")).read_text())
        surfaces = dump["surfaces"]
        f1 = free_point(surfaces, (1180.0, 520.0))
        f2 = free_point(surfaces, (760.0, 600.0))
        sessions = {"tour": ("teleport", tour(surfaces, f1)), "shots": ("teleport", shots(surfaces, f1, f2))}
        if level in KINEMATIC:
            sessions["walk"] = ("kinematic", walk())
        for name, (kind, steps) in sessions.items():
            doc = {"schema": "rc-session/1", "level": level, "kind": kind, "steps": steps}
            path = out_dir / f"{level.replace('/', '__')}__{name}.json"
            path.write_text(json.dumps(doc, indent=1) + "\n")
            n += 1
    print(f"{n} session scripts in {out_dir.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
