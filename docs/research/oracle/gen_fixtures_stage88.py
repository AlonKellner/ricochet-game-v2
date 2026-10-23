"""Stage 88 fixtures: plannable infinity (GAME_SPEC 4.2, 6.3, 12.5, 13.1;
the realization by the first entry since Stage 111),
traced by the oracle (trace2) with EMIT_INFINITY on. Same format as
gen_fixtures_stage85.py; the infinity plan entry is {"surface_id": -3
(INFINITY_ID), "side": ""}, and the "infinity" event carries surface_id -3,
no point, applied = the entry was consumed.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage88.py
Writes bench/fixtures/stage88_infinity.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import INV, PASS, REFL, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INF = (T.INFINITY_ID, "")


def reaches(f):
    return any(e["kind"] == "cursor" for e in f["expected"])


def infinities(f):
    return [e["applied"] for e in f["expected"] if e["kind"] == "infinity"]


def pair(fx, name, surfs, player, cursor, plan, expect_reach=True):
    """Planned and physical fixtures of one plan (sides given)."""
    p = fixture(name + "_planned", surfs, player, plan=plan, mode="planned", cursor=cursor)
    assert reaches(p) == expect_reach, (name, kinds(p))
    fx.append(p)
    fx.append(fixture(name + "_physical", surfs, player, plan=plan, mode="physical", cursor=cursor))
    return p


def pair_search(fx, name, surfs, player, cursor, entries):
    """`entries`: surface ids or INF. The first side combination of the
    surface entries whose planned trace reaches the cursor is recorded."""
    import itertools
    ids = [e for e in entries if e is not INF]
    found = None
    for sides in itertools.product(("LEFT", "RIGHT"), repeat=len(ids)):
        it = iter(sides)
        plan = [INF if e is INF else (e, next(it)) for e in entries]
        p = fixture(name + "_planned", surfs, player, plan=plan, mode="planned", cursor=cursor)
        if not reaches(p):
            continue
        q = fixture(name + "_physical", surfs, player, plan=plan, mode="physical", cursor=cursor)
        if reaches(q):     # realizable: preferred (Stage 111: see gen_fixtures_stage87.planned_pair)
            fx.append(p)
            fx.append(q)
            return p
        if found is None:
            found = (p, q)
    if found is not None:
        fx.append(found[0])
        fx.append(found[1])
        return found[0]
    raise AssertionError("%s: no side combination reaches the cursor" % name)


def main():
    mp.mp.dps = 80
    fx = []
    # --- the event itself (direction traces, no plan) ---
    # (Stage 134: walls crossed off their segments -- candidates that fire nothing)
    term = dict(PASS, type="terminal")
    ahead = line(1, (50, 100), (50, 200), term, term)
    behind = line(2, (-50, 100), (-50, 200), term, term)
    f = fixture("wrap_between_ahead_and_wrapped_crossings", [ahead, behind], (0.0, 0.0), direction=(1.0, 0.0))
    # (Stage 131: no loop detection -- each loop passes infinity and the ray's
    # origin, both significant, until the 11th)
    ks = [e["kind"] for e in f["expected"]]
    assert ks[:4] == ["hit", "infinity", "hit", "origin"] and ks.count("infinity") + ks.count("origin") == T.MAX_SIGNIFICANT and ks[-1] == "max", kinds(f)
    fx.append(f)
    # a mirror in an open scene: reflect, wrap, re-hit (pop), wrap, ... cut by the
    # tenth significant event (Stage 131)
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    f = fixture("mirror_wrap_loop_counts_to_max", [m], (0.0, 0.0), direction=(1.0, 1.0))
    assert f["expected"][-1]["kind"] == "max" and len(f["expected"]) == T.MAX_SIGNIFICANT + 1, kinds(f)
    fx.append(f)
    # the cut on the wrap itself: MAX_HITS pass lines ahead
    lines = [line(i + 1, (10 * (i + 1), -500), (10 * (i + 1), 500), PASS, PASS) for i in range(T.MAX_HITS)]
    f = fixture("max_at_infinity_has_no_point", lines, (0.0, 0.0), direction=(1.0, 0.0))
    last = f["expected"][-1]
    assert last["kind"] == "max" and last["surface_id"] == T.INFINITY_ID and "x" not in last, last
    fx.append(f)
    # a circle beam's infinity is the inversion center (Stage 101), but a beam
    # that crosses the whole circle pops at the second crossing before reaching
    # it: inversion, re-hit (pop), then the straight beam wraps (an infinity
    # event without a point), loop
    inv = circle(1, (500.0, 0.0), 200.0, INV, INV)
    f = fixture("circle_beam_pops_before_the_center", [inv], (0.0, 0.0), direction=(1.0, 0.1))
    ks = [e["kind"] for e in f["expected"]]
    assert ks[:3] == ["hit", "hit", "infinity"] and "x" not in f["expected"][2], ks
    assert not any(e["kind"] == "infinity" and "x" in e for e in f["expected"]), ks
    fx.append(f)
    # --- plans ---
    # [inf] alone: the second realization of the empty plan (aim away)
    p = pair(fx, "infinity_only", [], (0.0, 0.0), (300.0, 0.0), [INF])
    assert kinds(p)[:2] == [("infinity", T.INFINITY_ID, "", True, False), ("cursor", -1, "", False, False)], kinds(p)
    # the same without the entry: direct, the wrap after the cursor passes
    p = pair(fx, "empty_plan_direct", [], (0.0, 0.0), (300.0, 0.0), [])
    assert [e["kind"] for e in p["expected"]][:2] == ["cursor", "infinity"], kinds(p)
    # one mirror, both realizations: direct [M] reaches through an unapplied
    # wrap; [inf, M] reaches the other way round
    m = line(1, (200, -300), (200, 300), REFL, REFL)
    p = pair(fx, "mirror_direct_reaches_through_unapplied_wrap", [m], (0.0, 0.0), (300.0, 50.0), [(1, "RIGHT")])
    # (Stage 131: back from the wrap, the ray's origin -- the player's image (400, 0) -- first)
    assert [e["kind"] for e in p["expected"]][:4] == ["hit", "infinity", "origin", "cursor"] and infinities(p)[0] is False, kinds(p)
    p = pair_search(fx, "infinity_then_mirror", [m], (0.0, 0.0), (300.0, 50.0), [INF, 1])
    assert [e["kind"] for e in p["expected"]][:3] == ["infinity", "hit", "cursor"] and infinities(p)[0] is True, kinds(p)
    # mirror first, the reflected beam wraps onto the cursor
    p = pair_search(fx, "mirror_then_infinity", [m], (0.0, 0.0), (600.0, 50.0), [1, INF])
    assert [e["kind"] for e in p["expected"]][:3] == ["hit", "infinity", "cursor"] and infinities(p)[0] is True, kinds(p)
    # [inf, M] is a plan of its own (Stage 111, R1): the wrap first, then the
    # mirror's far face, onto the cursor; its near face cannot follow the wrap
    # (the mirror is met at once, passed by the planned walk: unmet)
    got = {}
    for side in ("LEFT", "RIGHT"):
        f = fixture("infinity_before_mirror_%s_planned" % side.lower(), [m], (0.0, 0.0),
                    plan=[INF, (1, side)], mode="planned", cursor=(600.0, 50.0))
        got[side] = f
        fx.append(f)
    # (Stage 131: no loop detection -- the near face reaches the cursor too, on the
    # next loop: the far face's plan is the one that reaches it on the first)
    # (the far face's reflected beam passes the player's image, the ray's origin, first)
    far = [s for s, f in got.items() if [e["kind"] for e in f["expected"]][:4] == ["infinity", "hit", "origin", "cursor"]]
    assert len(far) == 1, {s: kinds(f) for s, f in got.items()}
    near = got["LEFT" if far[0] == "RIGHT" else "RIGHT"]
    assert [e["kind"] for e in near["expected"]][:2] == ["hit", "infinity"] and not near["expected"][0]["applied"], kinds(near)
    # two mirrors with the wrap between them
    m1 = line(1, (200, -300), (200, 300), REFL, REFL)
    m2 = line(2, (900, -300), (900, 300), REFL, REFL)
    try:
        pair_search(fx, "mirror_infinity_mirror", [m1, m2], (0.0, 0.0), (1200.0, 40.0), [1, INF, 2])
    except AssertionError:
        pair_search(fx, "mirror_infinity_mirror", [m1, m2], (0.0, 0.0), (1000.0, 40.0), [1, INF, 2])
    # infinity after an inversion (Stage 101): the entry is the passage of the
    # inverted frame's infinity, the center; met on the side whose reversed
    # epoch passes the center before the cursor
    met = []
    for side in ("LEFT", "RIGHT"):
        f = fixture("infinity_after_inversion_%s_planned" % side.lower(), [inv], (0.0, 0.0),
                    plan=[(1, side), INF], mode="planned", cursor=(900.0, 60.0))
        assert all(("x" not in e) or (abs(float(e["x"]) - 500.0) < 1e-20 and abs(float(e["y"])) < 1e-20)
                   for e in f["expected"] if e["kind"] == "infinity"), kinds(f)
        met.append(reaches(f))
        fx.append(f)
    assert all(met), met   # (Stage 131: no loop detection -- the other side meets it on a later loop)
    # a projective whose exit points away from the cursor: reached through the
    # wrap; the infinity entry in that epoch is optional
    proj = {"type": "line_normal_projection", "interactive": True, "back": False}
    ln = line(1, (300, -400), (300, 400), proj, proj)
    p = pair(fx, "projective_exit_away_without_entry", [ln], (100.0, -100.0), (100.0, 200.0), [(1, "RIGHT")])
    assert [e["kind"] for e in p["expected"]][:3] == ["hit", "infinity", "cursor"] and infinities(p)[0] is False, kinds(p)
    p = pair(fx, "projective_exit_away_with_entry", [ln], (100.0, -100.0), (100.0, 200.0), [(1, "RIGHT"), INF])
    assert [e["kind"] for e in p["expected"]][:3] == ["hit", "infinity", "cursor"] and infinities(p)[0] is True, kinds(p)
    # a ghost stretch: the toggler sets ghost, the beam passes the room wall,
    # wraps (consuming the entry), passes the opposite wall and reaches the cursor
    walls = room(10, -1000, -1000, 1000, 1000)
    tog = line(1, (200, -400), (200, 400),
               {"type": "pass", "interactive": True, "state": {"key": "ghost", "op": "toggle", "value": True}},
               {"type": "pass", "interactive": True, "state": {"key": "ghost", "op": "toggle", "value": True}})
    p = pair_search(fx, "ghost_wrap_through_walls", [tog] + walls, (0.0, 0.0), (-800.0, 0.0), [1, INF])
    ks = [(e["kind"], e["surface_id"], e["applied"]) for e in p["expected"]]
    assert ks[0] == ("hit", 1, False) and p["expected"][0]["state"] and ("infinity", T.INFINITY_ID, True) in ks, ks
    assert ks[-1][0] == "cursor" or "cursor" in [k[0] for k in ks], ks
    out = os.path.join(ROOT, "bench", "fixtures", "stage88_infinity.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(p["surface_id"], p["side"]) for p in f["plan"]],
              [(k[0][:3], k[1], k[3]) for k in kinds(f)][:7], reaches(f))


if __name__ == "__main__":
    main()
