"""Stage 113 fixtures (K11, K12): a projective's hit point at a data point of
its segment, and the cursor at a projective's departure point.

K11: the back-propagated hit point H of a planned projective is a data point
of its segment (an endpoint, the via) when an exact predicate on the data says
so (trace2._exact_data_hit) -- never by the rounding of c + R u. At an
endpoint the physical hit is blocked on one side only (GAME_SPEC 11.1), so it
does not fire while the planned walk fires the entry there: the walks diverge
at H. The arc here is the right half of the circle centred (700, 500), r 250,
whose endpoint radius (0.6, -0.8) is not representable: by arithmetic alone
the hit point lands an ulp off the endpoint and fires.

K12: a cursor exactly on a line projective's segment is its own hit point; the
planned walk reaches it at once, on the exit beam (K7's rule for a departure
point). Same format as gen_fixtures_stage112_parallel.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage113_exact_hits.py
Writes bench/fixtures/stage113_exact_hits.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import arc, line, room  # noqa: E402
from gen_fixtures_stage85 import fixture  # noqa: E402
from gen_fixtures_stage87 import proj  # noqa: E402
from gen_fixtures_stage88 import pair  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def kinds(f):
    """(kind, surface, applied, x, y) per event (the points as floats)."""
    return [(e["kind"], e["surface_id"], e["applied"], None if "x" not in e else float(e["x"]), None if "y" not in e else float(e["y"]))
            for e in f["expected"]]


def stable(f, player, cursor, plan, mode):
    ks = [(e["kind"], e["surface_id"], e["applied"]) for e in f["expected"]]
    for dps in (60, 120):
        with mp.workdps(dps):
            g = fixture(f["name"], f["surfaces"], player, plan=plan, mode=mode, cursor=cursor)
        assert [(e["kind"], e["surface_id"], e["applied"]) for e in g["expected"]] == ks, (f["name"], dps)


def main():
    mp.mp.dps = 80
    T.EMIT_INFINITY = True
    fx = []
    walls = room(10, 560, 40, 1360, 840)   # (the top at y = 40: the players sit above the arcs)
    # K11: the arc's top endpoint (850, 300); the cursor on the radial through it
    # (through: the planned walk converges through the centre and reaches the
    # cursor beyond the endpoint after the wrap, passing the unplanned walls as
    # a planned walk does; the physical one, not fired, ends at a wall)
    for tag, back in (("through", False), ("back", True)):
        p = proj("circle_normal_projection", back)
        surfs = [arc(2, (850, 300), (850, 700), (950, 500), p, p)] + walls
        player, cursor, plan = (1000.0, 150.0), (940.0, 180.0), [(2, "LEFT")]
        f = pair(fx, "k11_arc_endpoint_" + tag, surfs, player, cursor, plan)
        ph = fx[-1]
        assert kinds(f)[0] == ("hit", 2, True, 850.0, 300.0), kinds(f)
        assert kinds(ph)[0] == ("hit", 2, False, 850.0, 300.0), kinds(ph)
        stable(f, player, cursor, plan, "planned")
        stable(ph, player, cursor, plan, "physical")
    # K11: a line normal's endpoint (750, 300); the cursor on the normal through it
    p = proj("line_normal_projection", False)
    surfs = [line(2, (750, 300), (750, 780), p, p)] + walls
    player, cursor, plan = (650.0, 400.0), (900.0, 300.0), [(2, "RIGHT")]
    f = pair(fx, "k11_line_endpoint", surfs, player, cursor, plan)
    ph = fx[-1]
    assert kinds(f)[0] == ("hit", 2, True, 750.0, 300.0), kinds(f)
    assert kinds(ph)[0] == ("hit", 2, False, 750.0, 300.0), kinds(ph)
    stable(f, player, cursor, plan, "planned")
    stable(ph, player, cursor, plan, "physical")
    # K11: a directional arc's endpoint where the normal is tangent (the exit
    # along +axis): the cursor along the tangent
    p = proj("circle_directional_projection", False, (1.0, 0.0))
    surfs = [arc(2, (850, 390), (850, 690), (1000, 540), p, p)] + walls
    player, cursor, plan = (960.0, 300.0), (1000.0, 390.0), [(2, "LEFT")]
    f = pair(fx, "k11_directional_tangency", surfs, player, cursor, plan)
    ph = fx[-1]
    assert kinds(f)[0] == ("hit", 2, True, 850.0, 390.0), kinds(f)
    assert kinds(ph)[0] == ("hit", 2, False, 850.0, 390.0), kinds(ph)
    stable(f, player, cursor, plan, "planned")
    stable(ph, player, cursor, plan, "physical")
    # K12: the cursor on the line normal's segment: its own hit point, reached at once
    p = proj("line_normal_projection", False)
    surfs = [line(2, (750, 300), (750, 780), p, p)] + walls
    player, cursor, plan = (760.0, 400.0), (750.0, 540.0), [(2, "LEFT")]
    f = pair(fx, "k12_cursor_at_departure", surfs, player, cursor, plan)
    assert kinds(f)[:2] == [("hit", 2, True, 750.0, 540.0), ("cursor", -1, False, 750.0, 540.0)], kinds(f)
    stable(f, player, cursor, plan, "planned")
    out = os.path.join(ROOT, "bench", "fixtures", "stage113_exact_hits.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    print("wrote %d fixtures to %s" % (len(fx), out))


if __name__ == "__main__":
    main()
