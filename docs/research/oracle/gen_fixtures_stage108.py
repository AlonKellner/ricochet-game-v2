"""Stage 108 fixtures: plan entries naming a PIECE of a surface (GAME_SPEC
9.7): a span {start, end, via} on the surface's carrier. Same format as
gen_fixtures_stage85.py; a plan entry may carry "span".

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage108.py
Writes bench/fixtures/stage108_piece_plans.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import REFL, TERM, ev_json, line, room  # noqa: E402
from gen_fixtures_stage85 import circle, kinds  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def span(a, b, via=None):
    via = via if via is not None else ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
    return {"start": list(a), "end": list(b), "via": list(via)}


def fixture(name, surfs, player, cursor, plan, spans, mode):
    sc = T.scene_from_json(surfs)
    f = T.planned if mode == "planned" else T.physical
    w = f(sc, player, cursor, plan, None, None, spans)
    expected = []
    for e in w.events:
        o = ev_json(e)
        o["state"] = bool(e.state)
        o["flags"] = dict(e.flags) if e.flags is not None else None
        expected.append(o)
    return {"name": name, "surfaces": surfs, "player": list(player), "direction": None, "cursor": list(cursor),
            "plan": [dict({"surface_id": a, "side": b}, **({"span": sp} if sp else {})) for (a, b), sp in zip(plan, spans)],
            "mode": mode, "flags": {}, "expected": expected, "targets": sorted(w.targets)}


def hit_on(f, sid):
    return next(e for e in f["expected"] if e["kind"] == "hit" and e["surface_id"] == sid)


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(90, -1000, -1000, 1000, 1000)
    # a mirror at x = 500 (y -300..300), crossed by a block at y = 0: pieces
    # (500, -300)-(500, 0) [upper on screen] and (500, 0)-(500, 300) [lower]
    M = line(1, (500, -300), (500, 300), REFL, REFL)
    B = line(2, (400, 0), (600, 0), TERM, TERM)
    upper = span((500, -300), (500, 0))
    lower = span((500, 0), (500, 300))
    scene = [M, B] + walls
    # the cursor reflected in the mirror, reached through the upper piece
    for mode in ("planned", "physical"):
        f = fixture("piece_planned_reached_%s" % mode, scene, (100.0, -200.0), (200.0, -100.0),
                    [(1, "RIGHT")], [upper], mode)
        h = hit_on(f, 1)
        assert h["on_segment"] and h["applied"], kinds(f)
        assert any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
        fx.append(f)
    # the same aim planned on the LOWER piece: the planned hit is off its piece;
    # the physical hit fires on the upper piece -- not the planned one: diverged
    f = fixture("piece_planned_other_piece_planned", scene, (100.0, -200.0), (200.0, -100.0),
                [(1, "RIGHT")], [lower], "planned")
    assert not hit_on(f, 1)["on_segment"], kinds(f)
    fx.append(f)
    f = fixture("piece_planned_other_piece_physical", scene, (100.0, -200.0), (200.0, -100.0),
                [(1, "RIGHT")], [lower], "physical")
    assert not any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
    fx.append(f)
    # without a span the entry matches the whole surface (as before Stage 108)
    f = fixture("piece_unspecified_matches_whole", scene, (100.0, -200.0), (200.0, -100.0),
                [(1, "RIGHT")], [None], "physical")
    assert any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
    fx.append(f)
    # an arc piece of a full mirror circle (center (500, 0), r 100), planned on
    # its left half: (500, 100) -> (400, 0) -> (500, -100)
    C = circle(3, (500.0, 0.0), 100.0, REFL, REFL)
    left_half = span((500, 100), (500, -100), (400, 0))
    right_half = span((500, -100), (500, 100), (600, 0))
    for name, sp, reached in (("circle_piece_reached", left_half, True), ("circle_piece_other", right_half, False)):
        for mode in ("planned", "physical"):
            f = fixture("%s_%s" % (name, mode), [C] + walls, (100.0, 30.0), (150.0, 200.0), [(3, "LEFT")], [sp], mode)
            if mode == "planned":
                assert hit_on(f, 3)["on_segment"] == reached, kinds(f)
            fx.append(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage108_piece_plans.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], kinds(f)[:6], len(f["expected"]))


if __name__ == "__main__":
    main()
