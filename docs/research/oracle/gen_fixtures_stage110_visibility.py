"""Stage 110: cursors labelled visible or not by the oracle's definition of the
visibility region (visibility.py), on shipped levels' scene dumps.

Each case: a level, a player, a plan; its cursors: a grid over the level, every
segment endpoint, and the points beyond each endpoint on the line from the
player (2e - P: exactly on a shadow's edge).

Stage 113: plans through projective entries (GAME_SPEC 15, 13.3). A plan entry
may carry the player's click ("click": the fork of a circle projective's
back-propagation). Such cases add cursors that probe the pencil of exit lines:
the projective's endpoints and via point (exact points on its carrier), the
centre of a circle normal (every member passes it), and points along the exit
lines through those anchors, on both sides of the carrier.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage110_visibility.py
Writes bench/fixtures/stage110_visibility.json.
"""
import json
import multiprocessing
from fractions import Fraction
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TRANSFORMATIVE = ("reflection", "inversion", "portal")
PROJECTIVE = ("line_normal_projection", "circle_normal_projection", "line_directional_projection",
              "circle_directional_projection")


def dump(name):
    return json.load(open(os.path.join(ROOT, "bench", "scenes", name + ".surfaces.json")))["surfaces"]


def sides(surfs, types):
    """(id, side) of every side whose effect type is in types."""
    out = []
    for s in surfs:
        if s.get("pre_transform"):
            continue
        for side, key in (("LEFT", "left"), ("RIGHT", "right")):
            cfg = s.get(key)
            if cfg and cfg.get("type") in types:
                out.append((s["id"], side))
    return out


def carrier_extension(surfs, plan):
    """Cursors on the planned surfaces' carriers (lines, and a portal's pair)
    beyond their segments: S + k (E - S), k in {-1, -1/2, -1/8, 9/8, 3/2, 2}, kept when
    exact in float32 (where a walk departs from a planned surface crossed off
    its segment -- B4)."""
    by_id = {s["id"]: s for s in surfs}
    ids = []
    for sid, _side in plan:
        s = by_id.get(sid)
        if s is None:
            continue
        ids.append(sid)
        for key in ("left", "right"):
            cfg = s.get(key) or {}
            if cfg.get("type") == "portal" and cfg.get("pair") is not None:
                ids.append(cfg["pair"])
    out = []
    for sid in ids:
        s = by_id[sid]
        if not s.get("line", True) or s.get("full"):
            continue
        S, E = (Fraction(s["start"][0]), Fraction(s["start"][1])), (Fraction(s["end"][0]), Fraction(s["end"][1]))
        for k in (Fraction(-1), Fraction(-1, 2), Fraction(-1, 8), Fraction(9, 8), Fraction(3, 2), Fraction(2)):
            p = (S[0] + k * (E[0] - S[0]), S[1] + k * (E[1] - S[1]))
            if all(Fraction(V._f32(c)) == c for c in p):
                out.append((float(p[0]), float(p[1])))
    return out


def _unit(z):
    return z / abs(z)


def projective_cursors(sc, plan):
    """Cursors probing a planned projective's pencil (Stage 113): its endpoints
    and via (on the carrier), a circle normal's centre, and the points
    h + k u along the exit line through each anchor h (u: the carrier's normal,
    the radius, or the fixed normal), k on both sides of the carrier."""
    by_id = {s.id: s for s in sc}
    out = []
    for sid, side in plan:
        s = by_id.get(sid)
        if s is None:
            continue
        cfg = s.cfg(side)
        if not cfg or cfg.get("type") not in PROJECTIVE:
            continue
        seg = s.seg
        anchors = [seg.S, seg.V, seg.E]
        if cfg["type"] == "circle_normal_projection":
            out.append(seg.c)
        for h in anchors:
            if cfg["type"] == "line_normal_projection":
                d = seg.E - seg.S
                u = _unit(mp.mpc(-d.imag, d.real))
            elif cfg["type"] == "circle_normal_projection":
                u = _unit(h - seg.c)
            else:
                u = _unit(mp.mpc(cfg["normal"][0], cfg["normal"][1]))
            ks = (-400, -150, -60, 60, 150, 400)
            if cfg["type"] == "circle_normal_projection":
                r = mp.sqrt(seg.R2)
                ks = ks + (-float(r) / 2, float(r) / 2, -float(2 * r))
            for k in ks:
                z = h + k * u
                out.append(z)
            out.append(h)
    return [(float(z.real), float(z.imag)) for z in out]


def cursors(surfs, player, plan=(), extra=()):
    pts = carrier_extension(surfs, plan) + list(extra)
    for i in range(14):
        for j in range(8):
            pts.append((40.0 + i * 140.0, 40.0 + j * 140.0))
    for s in surfs:
        if s.get("full"):
            continue
        for e in (s["start"], s["end"]):
            pts.append((float(e[0]), float(e[1])))
            pts.append((2.0 * e[0] - player[0], 2.0 * e[1] - player[1]))
    out = []
    for p in pts:
        if -50 <= p[0] <= 1970 and -50 <= p[1] <= 1130 and p not in out and p != player:
            out.append(p)
    return out


CANDIDATES = [(760.0, 400.0), (1200.0, 400.0), (950.0, 700.0), (650.0, 650.0), (1250.0, 700.0)]

# Stage 131: a case's cursors are labelled in parallel -- each label is an
# independent oracle trace; forked workers inherit the case (and the oracle's
# precision), and the labels come back in order, so the file is the same as a
# sequential run's. RC_REGEN_JOBS=1: sequential.
JOBS = int(os.environ.get("RC_REGEN_JOBS", "0")) or (os.cpu_count() or 1)
_JOB = None


def _label(c):
    sc, player, plan, hints = _JOB
    return V.visible(sc, player, c, plan, None, hints)


def labels(sc, player, plan, hints, cs):
    global _JOB
    _JOB = (sc, player, plan, hints)
    if JOBS <= 1 or len(cs) < 2 * JOBS:
        return [_label(c) for c in cs]
    with multiprocessing.get_context("fork").Pool(JOBS) as pool:
        return pool.map(_label, cs, chunksize=max(1, len(cs) // (JOBS * 4)))


def planned_case(name, scene, plan, players=None, hints=None):
    """The case at the first candidate player from which the plan shows some cursors."""
    best = None
    for player in (players or CANDIDATES):
        c = case(name, scene, player, plan, hints)
        n = sum(1 for x in c["cursors"] if x[2])
        if best is None or n > best[0]:
            best = (n, c)
        if n >= 10:
            break
    return best[1]


def best_case(name, scene, plans, players):
    """Over the plans (side combinations) and players: the first showing at least
    10 cursors, else the one showing the most."""
    best = None
    for plan in plans:
        for player in players:
            c = case(name, scene, player, plan)
            n = sum(1 for x in c["cursors"] if x[2])
            if best is None or n > best[0]:
                best = (n, c)
            if n >= 10:
                return c
    return best[1]


def case(name, scene, player, plan, hints=None, extra=()):
    """hints: plan entry index -> the click (x, y), the fork of a circle projective;
    extra: cursors added to the grid."""
    surfs = dump(scene)
    sc = T.scene_from_json(surfs)
    labelled = []
    cs = cursors(surfs, player, plan, projective_cursors(sc, plan) + list(extra))
    for c, v in zip(cs, labels(sc, player, plan, hints, cs)):
        if v is not None:
            labelled.append([c[0], c[1], bool(v)])
    entries = []
    for i, (a, b) in enumerate(plan):
        e = {"surface_id": a, "side": b}
        if hints and i in hints:
            e["click"] = [float(hints[i][0]), float(hints[i][1])]
        entries.append(e)
    return {"name": name, "scene": scene, "player": list(player), "plan": entries, "cursors": labelled}


def projective_cases():
    """Stage 113: plans through projective entries, one case per projective side
    of the projective playgrounds, the inner sides of the arcs from a player
    inside the circle, the fork settled by a click, the degenerate directional,
    and the compositions (a mirror or an inversion before or after a
    projective; two projectives)."""
    out = []
    sides_of = lambda scene: sides(dump(scene), PROJECTIVE)  # noqa: E731
    for sid, side in sides_of("playground__projective_lines"):
        out.append(planned_case("projective_lines_%d%s" % (sid, side[0]), "playground__projective_lines", [(sid, side)]))
    # circle normals 5 (through) and 6 (back): the right halves of the circles
    # centred (850, 540) and (1100, 540), r 150. LEFT is the outer side: from a
    # player left of the circle the planned walk fires off-segment at the far
    # half, so the outer side is approached from above or below; the inner side
    # from the left (through the far half, a pass) or from inside.
    arc_players = {(5, "LEFT"): [(960.0, 300.0), (960.0, 780.0)], (5, "RIGHT"): [(760.0, 400.0), (900.0, 540.0)],
                   (6, "LEFT"): [(1250.0, 300.0), (1250.0, 780.0)], (6, "RIGHT"): [(1000.0, 400.0), (1150.0, 540.0)]}
    for scene in ("playground__projective_arcs", "playground__directional_arcs"):
        tag = scene.split("__")[1]
        for sid, side in sides_of(scene):
            out.append(planned_case("%s_%d%s" % (tag, sid, side[0]), scene, [(sid, side)], arc_players[(sid, side)]))
        # the inner side from inside the circle
        out.append(case("%s_5R_inside" % tag, scene, (900.0, 540.0), [(5, "RIGHT")]))
        out.append(case("%s_6R_inside" % tag, scene, (1150.0, 540.0), [(6, "RIGHT")]))
    # the fork of a converging circle normal (both candidates reach a cursor
    # inside the circle): the click picks the candidate; the clicks are off the
    # axis of symmetry so that no cursor's candidates tie
    for tag, click in (("top", (880.0, 395.0)), ("bottom", (880.0, 685.0))):
        out.append(case("projective_arcs_5L_click_" + tag, "playground__projective_arcs", (960.0, 300.0), [(5, "LEFT")], {0: click}))
    for tag, click in (("top", (1130.0, 395.0)), ("bottom", (1130.0, 685.0))):
        out.append(case("projective_arcs_6R_inside_click_" + tag, "playground__projective_arcs", (1150.0, 540.0), [(6, "RIGHT")],
                        {0: click}))
    for sid, side in sides_of("playground__directional_lines"):
        out.append(planned_case("directional_lines_%d%s" % (sid, side[0]), "playground__directional_lines", [(sid, side)]))
    # a directional normal parallel to its carrier: no exit family, nothing is reached
    # (the degenerate directional -- its normal along its line -- is rejected at
    # load since B9's follow-up; the native and the oracle pin its empty region
    # on bench/fixtures/stage113_degenerate_directional.json directly)
    # the mixed playground: 11 line directional (x = 500, n = +x), 6 line normal
    # (x = 700); the portals 12 and 13 span y 300..700, so the rays run above them
    pg = "playground__projective"
    # (the grid misses the band above the portals: cursors along y = 230 and 280)
    band = [(x, y) for y in (230.0, 280.0) for x in (520.0, 650.0, 720.0, 745.0, 760.0, 900.0, 1300.0, 1600.0)]
    out.append(case("two_projectives", pg, (300.0, 230.0), [(11, "RIGHT"), (6, "RIGHT")], None, band))
    # projective_inversion: 5 mirror (x = 1300, y 460..500, reflective toward -x),
    # 6 inversion circle (1200, 600) r 100, 7 line normal back (x = 700, y
    # 300..700: its exits run +x), 8 circle normal back (centre (1000, 380) r 70,
    # right half)
    pi = "playground__projective_inversion"
    out.append(best_case("line_normal_then_mirror", pi, [[(7, s), (5, "LEFT")] for s in ("LEFT", "RIGHT")],
                         [(960.0, 640.0), (960.0, 540.0)]))
    out.append(best_case("circle_normal_then_mirror", pi, [[(8, s), (5, "LEFT")] for s in ("LEFT", "RIGHT")],
                         [(1150.0, 300.0), (1250.0, 380.0)]))
    out.append(best_case("mirror_then_line_normal", pi, [[(5, "LEFT"), (7, s)] for s in ("LEFT", "RIGHT")],
                         [(1230.0, 620.0), (1160.0, 570.0), (1200.0, 650.0)]))
    out.append(best_case("line_normal_then_inversion", pi, [[(7, s), (6, "LEFT")] for s in ("LEFT", "RIGHT")],
                         [(960.0, 480.0), (1100.0, 480.0)]))
    out.append(best_case("inversion_then_line_normal", pi, [[(6, "LEFT"), (7, s)] for s in ("LEFT", "RIGHT")],
                         [(1000.0, 480.0), (1100.0, 470.0)]))
    out.append(best_case("line_normal_then_circle_normal", pi, [[(7, s), (8, t)] for s in ("LEFT", "RIGHT") for t in ("LEFT", "RIGHT")],
                         [(960.0, 380.0), (900.0, 420.0)]))
    # projective_concentric: 7 circle normal (right half, centre (960, 540) r 100),
    # 6 a full inversion circle centred there too (r 250), 5 a mirror (x = 700).
    # After the projective the inversion sends the centre to infinity: the pencil's
    # poles swap (Stage 113 follow-up)
    pc = "playground__projective_concentric"
    out.append(best_case("circle_normal_then_concentric_inversion", pc, [[(7, s), (6, t)] for s in ("LEFT", "RIGHT") for t in ("LEFT", "RIGHT")],
                         [(1100.0, 700.0), (960.0, 300.0), (800.0, 300.0)]))
    out.append(best_case("concentric_inversion_then_circle_normal", pc, [[(6, t), (7, s)] for s in ("LEFT", "RIGHT") for t in ("LEFT", "RIGHT")],
                         [(800.0, 300.0), (1100.0, 700.0), (1250.0, 540.0)]))
    # (K15, open: with the mirror 5 after them the planned walk wraps through the
    # real infinity and meets the inversion on the beam through its own centre;
    # the native solver has no trace there while the oracle has one -- that case
    # is test_visibility.cpp's known-failing "K15" test, not a label)
    # K14: line 6 of the mixed playground runs through arc 9's centre: the fork
    # line of the default click is line 6's carrier, where the physical walk fires
    # 6 and the planned walk 9 at one point (two frames, not one epoch count)
    out.append(case("projective_9R_fork_on_line_6", "playground__projective", (676.666687, 390.0), [(9, "RIGHT")], None,
                    [(700.0, 200.0), (700.0, 800.0), (700.0, 100.0), (700.0, 240.0), (700.0, 760.0), (700.0, 900.0)]))
    return out


def main():
    mp.mp.dps = 50
    out = []
    room = (760.0, 400.0)   # inside the test levels' room (560..1360 x 240..840)
    out.append(case("room_with_mirror_no_plan", "test_levels__room_with_mirror", room, []))
    mirror = sides(dump("test_levels__room_with_mirror"), ("reflection",))[0]
    out.append(planned_case("room_with_mirror_planned", "test_levels__room_with_mirror", [mirror]))
    two = sides(dump("test_levels__two_mirrors"), ("reflection",))
    out.append(planned_case("two_mirrors_one", "test_levels__two_mirrors", [two[0]]))
    out.append(planned_case("two_mirrors_both", "test_levels__two_mirrors", [two[0], two[-1]]))
    inv = sides(dump("test_levels__room_with_inversion"), ("inversion", "reflection"))
    out.append(planned_case("room_with_inversion_planned", "test_levels__room_with_inversion", [inv[0]]))
    out.append(case("pre_inverted_no_plan", "playground__pre_inverted", (500.0, 300.0), []))
    out.append(case("empty_room_infinity", "test_levels__empty_room", (500.0, 300.0), [(T.INFINITY_ID, "LEFT")]))
    out.append(case("room_with_passthroughs_no_plan", "test_levels__room_with_passthroughs", room, []))
    # B5: a co-carrier write of the planned value (hit_effects: 15 fires, 16 is planned)
    out.append(case("hit_effects_state_plan", "test_levels__hit_effects", (960.0, 540.0), [(16, "RIGHT")]))
    # B4: the reported thin lines -- departures off the planned segment (float32 player)
    three = (V._f32(914.4775), V._f32(425.2639))
    out.append(case("three_mirrors_departures", "test_levels__three_mirrors", three, [(4, "LEFT"), (6, "RIGHT")]))
    out += projective_cases()
    path = os.path.join(ROOT, "bench", "fixtures", "stage110_visibility.json")
    json.dump({"dps": mp.mp.dps, "cases": out}, open(path, "w"), indent=1)
    for c in out:
        print(c["name"], len(c["cursors"]), "cursors,", sum(1 for x in c["cursors"] if x[2]), "visible")


if __name__ == "__main__":
    main()
