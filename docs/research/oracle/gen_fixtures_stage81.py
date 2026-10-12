"""Stage 81 rule fixtures: hand-built scenes traced by the oracle (trace2).

Each fixture: {name, surfaces (scene-dump format), player, direction | null,
cursor | null, plan, mode, expected: [event, ...]}. A direction-based fixture
walks that exact direction in `mode`; otherwise the full planned / physical
trace (exact aim direction) is recorded.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage81.py
Writes bench/fixtures/stage81_rules.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T                      # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
D = 40

REFL = {"type": "reflection", "interactive": True}
INV = {"type": "inversion", "interactive": True}
TERM = {"type": "terminal", "interactive": False}
PASS = {"type": "pass", "interactive": False}


def s(x):
    return mp.nstr(x, D, strip_zeros=False)


def line(sid, a, b, left=None, right=None, target=False):
    return {"id": sid, "line": True, "start": list(a), "end": list(b),
            "via": [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0], "full": False,
            "left": left, "right": right, "is_target": target}


def arc(sid, a, b, via, left=None, right=None):
    return {"id": sid, "line": False, "start": list(a), "end": list(b), "via": list(via), "full": False,
            "left": left, "right": right, "is_target": False}


def ev_json(e):
    o = {"kind": e.kind, "surface_id": e.surface_id, "side": e.side, "applied": e.applied,
         "terminal": e.terminal, "on_segment": e.on_segment, "rehit": e.rehit}
    if e.point is not None:
        o["x"] = s(e.point.real)
        o["y"] = s(e.point.imag)
    return o


def fixture(name, surfs, player, plan=(), mode="physical", direction=None, cursor=None):
    sc = T.scene_from_json(surfs)
    plan = [tuple(p) for p in plan]
    if direction is not None:
        m = T.PHYSICAL if mode == "physical" else T.PLANNED
        w = T.trace_mode_direction(sc, player, mp.mpc(*direction), plan,
                                   None if cursor is None else T.P(cursor), m)
    elif mode == "physical":
        w = T.physical(sc, player, cursor, plan)
    else:
        w = T.planned(sc, player, cursor, plan)
    return {"name": name, "surfaces": surfs, "player": list(player),
            "direction": None if direction is None else list(direction),
            "cursor": None if cursor is None else list(cursor),
            "plan": [{"surface_id": a, "side": b} for a, b in plan], "mode": mode,
            "expected": [ev_json(e) for e in w.events], "targets": sorted(w.targets)}


def room(first_id=1, x0=0, y0=-500, x1=1000, y1=500):
    return [line(first_id, (x0, y0), (x1, y0), TERM, TERM), line(first_id + 1, (x1, y0), (x1, y1), TERM, TERM),
            line(first_id + 2, (x1, y1), (x0, y1), TERM, TERM), line(first_id + 3, (x0, y1), (x0, y0), TERM, TERM)]


def main():
    mp.mp.dps = 80
    fx = []
    # terminal: the block hit is the last event
    fx.append(fixture("terminal_ends_trace", room(), (500, 0), direction=(1, 0.25)))
    # pass surfaces are events (one line, then a wall)
    fx.append(fixture("pass_surfaces_emit_events",
                      [line(1, (300, -500), (300, 500), PASS, PASS), line(2, (600, -500), (600, 500), None, None)] + room(3),
                      (100, 0), direction=(1, 0.125)))
    # MAX_HITS = 32 between two parallel mirrors
    fx.append(fixture("max_hits_parallel_mirrors",
                      [line(1, (0, -500), (0, 500), REFL, REFL), line(2, (100, -500), (100, 500), REFL, REFL)],
                      (50, 0), direction=(1, 0)))
    # one-sided mirror: the inert (RIGHT) side passes, the active one reflects
    fx.append(fixture("one_sided_mirror_inert_side",
                      [line(1, (300, -500), (300, 500), REFL, None)] + room(2), (100, 0), direction=(1, 0.125)))
    fx.append(fixture("one_sided_mirror_active_side",
                      [line(1, (300, -500), (300, 500), REFL, None)] + room(2), (600, 0), direction=(-1, 0.125)))
    # partial inversion arc (upper half of a circle about (500,0), R=200)
    inv_arc = arc(1, (700, 0), (300, 0), (500, -200), INV, INV)
    fx.append(fixture("partial_inversion_off_segment_physical", [inv_arc] + room(2),
                      (100, 100), direction=(1, 0)))
    fx.append(fixture("partial_inversion_off_segment_planned", [inv_arc] + room(2),
                      (100, 100), direction=(1, 0), plan=[(1, "RIGHT")], mode="planned"))
    fx.append(fixture("partial_inversion_on_segment_physical", [inv_arc] + room(2),
                      (100, -100), direction=(1, 0)))
    # Y junction: two mirrors share the endpoint (500, 0); the beam goes through it
    yj = [line(1, (500, 0), (600, -100), REFL, REFL), line(2, (500, 0), (600, 100), REFL, REFL)] + room(3)
    fx.append(fixture("y_junction_fires_lower_id", yj, (100, 0), direction=(1, 0)))
    # one segment ending on the ray: the graze blocks one side only -> passes
    fx.append(fixture("endpoint_graze_passes",
                      [line(1, (500, 0), (500, 200), REFL, REFL)] + room(2), (100, 0), direction=(1, 0)))
    # partial blockage must not leak: graze an endpoint, then a mirror elsewhere
    # whose segment the beam misses (off-segment) -> no effect fires
    fx.append(fixture("partial_blockage_does_not_leak",
                      [line(1, (400, 0), (400, 200), REFL, REFL), line(2, (700, 50), (700, 300), REFL, REFL)] + room(3),
                      (100, 0), direction=(1, 0)))
    # zero-length in PLANNED: the planned entry at a junction point fires on the
    # id/side match like any other hit (no full-block requirement)
    fx.append(fixture("zero_length_planned_entry", yj, (100, 0), direction=(1, 0),
                      plan=[(2, "LEFT")], mode="planned"))
    fx.append(fixture("zero_length_planned_entry_right", yj, (100, 0), direction=(1, 0),
                      plan=[(2, "RIGHT")], mode="planned"))
    # departure re-hit: a straight beam reflected by a lone mirror wraps through
    # infinity back to the mirror's other side: an ordinary hit
    fx.append(fixture("departure_rehit_straight",
                      [line(1, (100, -500), (100, 500), REFL, REFL)], (0, 0), direction=(1, 1)))
    # departure re-hit off-segment (semi-circle mirror, planned through the
    # carrier off the arc): re-hit passes, the closed beam ends ("origin")
    semi = arc(5, (1160, 540), (760, 540), (960, 340), REFL, REFL)
    fx.append(fixture("departure_rehit_off_segment_planned", [semi], (651.6652221679688, 793.6358032226562),
                      cursor=(1024.504638671875, 802.863525390625), plan=[(5, "RIGHT")], mode="planned"))
    # provenance: the cursor is exactly the shared endpoint of a Y junction;
    # the aim is irrational, so only provenance (not the computed root) puts
    # the hit on the endpoint -> junction full block -> lower id reflects
    fx.append(fixture("cursor_at_junction_physical", yj, (100.25, 13.75), cursor=(500.0, 0.0), mode="physical"))
    fx.append(fixture("cursor_at_junction_planned", yj, (100.25, 13.75), cursor=(500.0, 0.0), mode="planned"))
    # provenance: the player stands exactly on an arc endpoint
    pa = arc(1, (500, 0), (300, 0), (400, -100), INV, INV)
    fx.append(fixture("player_at_arc_endpoint", [pa] + room(2), (500.0, 0.0), cursor=(431.125, 77.375), mode="physical"))
    fx.append(fixture("player_at_arc_endpoint_planned", [pa] + room(2), (500.0, 0.0), cursor=(431.125, 77.375),
                      plan=[(1, "LEFT")], mode="planned"))
    # provenance propagates through effects: the cursor is an arc endpoint on
    # the arc's carrier; the beam is inverted at the arc's other crossing, and
    # the inverted beam passes exactly through the cursor (a fixed point of
    # the inversion): that hit is exactly the endpoint
    pe = arc(7, (400, 250), (300, 250), (350, 200), REFL, REFL)
    fx.append(fixture("propagated_endpoint_physical", [pe] + room(1, 200, 100, 1700, 900), (210.0, 110.0),
                      cursor=(400.0, 250.0), mode="physical"))
    # exact incidence: the player -> cursor line passes exactly through an arc
    # endpoint (collinear exact data), not a constructed point
    fx.append(fixture("collinear_endpoint_physical", [pe] + room(1, 200, 100, 1700, 900), (200.0, 100.0),
                      cursor=(600.0, 700.0), mode="physical"))
    fx.append(fixture("collinear_endpoint_direction", [pe] + room(1, 200, 100, 1700, 900), (200.0, 100.0),
                      direction=(2.0, 3.0), mode="physical"))
    # exact chain: the cursor lies inside mirror 6 (incidence -> exact hit
    # point); the reflected exact line passes exactly through arc 8's endpoint
    m6 = line(6, (1300, 300), (1300, 700), REFL, REFL)
    a8 = arc(8, (1550, 750), (1450, 750), (1500, 700), REFL, REFL)
    fx.append(fixture("reflected_exact_line_endpoint", [m6, a8] + room(1, 200, 100, 1700, 900), (1690.0, 110.0),
                      cursor=(1300.0, 500.0), mode="physical"))
    # mirror-only plan: the aim image is an exact rational point, so the
    # initial line is exact data (real level, sweep case)
    tm = json.load(open(os.path.join(ROOT, "bench", "scenes", "test_levels__three_mirrors.surfaces.json")))["surfaces"]
    fx.append(fixture("planned_mirror_exact_image", tm, (1350.0, 540.0), cursor=(800.0, 300.0),
                      plan=[(4, "LEFT")], mode="planned"))
    fx.append(fixture("planned_mirror_exact_image_physical", tm, (1350.0, 540.0), cursor=(800.0, 300.0),
                      plan=[(4, "LEFT")], mode="physical"))
    # an exact line through a circle mirror's center maps onto itself: the
    # inverted beam is exactly straight (real level, sweep case: the screen
    # diagonal through the center and the corner cursor)
    cw = json.load(open(os.path.join(ROOT, "bench", "scenes", "test_levels__circle_no_walls.surfaces.json")))["surfaces"]
    fx.append(fixture("line_through_inversion_center", cw, (1440.0, 810.0), cursor=(0.0, 0.0), mode="physical"))
    # exact tangency: the player -> cursor line x = 820 touches the grid
    # circles 1, 4, 7 without crossing them (real level, sweep case)
    ig = json.load(open(os.path.join(ROOT, "bench", "scenes", "playground__inversion_grid.surfaces.json")))["surfaces"]
    fx.append(fixture("tangent_line_no_crossing", ig, (820.0, 380.0), cursor=(820.0, 700.0), mode="physical"))
    fx.append(fixture("tangent_line_no_crossing_planned", ig, (820.0, 380.0), cursor=(820.0, 700.0), mode="planned"))
    # targets: on-segment hits of target surfaces are recorded
    fx.append(fixture("target_recorded_on_segment",
                      [line(1, (500, -100), (500, 100), None, None, target=True)] + room(2), (100, 0), direction=(1, 0.0625)))
    out = os.path.join(ROOT, "bench", "fixtures", "stage81_rules.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["side"], e["applied"], e["on_segment"]) for e in f["expected"]][:6],
              len(f["expected"]))


if __name__ == "__main__":
    main()
