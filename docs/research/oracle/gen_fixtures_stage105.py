"""Stage 105 fixtures: a beam tangent to a carrier at the frame's infinity
point is a touch, not a crossing (GAME_SPEC 12.5, 31), decided exactly from
the pole record (trace2: Walk.pole). Every case asserts that the rule fired
on the intended surface, so a fixture cannot pass by a lucky rounding.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage105.py
Writes bench/fixtures/stage105_tangent_at_infinity.json.
"""
import json
import os
import struct
import sys
from fractions import Fraction

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, line  # noqa: E402

W3 = dict(PASS, interactive=True, states=[{"key": "w", "op": "toggle", "value": True}])
from gen_fixtures_stage85 import circle, fixture, kinds  # noqa: E402
from gen_fixtures_stage88 import INF, pair  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
INV = {"type": "inversion", "interactive": True}
INV_OFF = {"type": "inversion", "interactive": False}


def pre(sid, center, radius):
    d = circle(sid, center, radius, INV_OFF, INV_OFF)
    d["pre_transform"] = True
    return d


class Spy:
    """Records the (surface id, Q) the tangency rule skipped while active."""

    def __init__(self):
        self.seen = []

    def __enter__(self):
        self.orig = T._pole_tangent

        def spy(pole, seg):
            r = self.orig(pole, seg)
            if r:
                self.seen.append((seg.id, (pole[0], pole[1])))
            return r
        T._pole_tangent = spy
        return self

    def __exit__(self, *a):
        T._pole_tangent = self.orig

    def fired(self, sid, q=None):
        return any(s == sid and (q is None or p == (Fraction(q[0]), Fraction(q[1]))) for s, p in self.seen)


def no_hits_on(f, sid):
    return not any(e["kind"] == "hit" and e["surface_id"] == sid for e in f["expected"])


def main():
    mp.mp.dps = 80
    fx = []
    Q0 = pre(9, (500.0, 0.0), 200.0)
    mirror_q0 = line(1, (500.0, -300.0), (500.0, 300.0), REFL, REFL)
    # (i) a pre-transform frame: the direction shot whose circle is tangent to the mirror at Q0
    with Spy() as s:
        f = fixture("pre_tangent_mirror_direction", [Q0, mirror_q0], (400.0, 200.0), direction=(200.0, -150.0))
    assert s.fired(1, (500, 0)) and no_hits_on(f, 1), kinds(f)
    fx.append(f)
    # the control: a transversal direction crosses the mirror at Q0
    with Spy() as s:
        f = fixture("pre_transversal_mirror_direction", [Q0, mirror_q0], (400.0, 200.0), direction=(1.0, 0.0))
    assert not s.seen and not no_hits_on(f, 1), kinds(f)
    fx.append(f)
    # (ii) a circle surface tangent to the beam at Q0
    with Spy() as s:
        # (Stage 134: a mirror circle -- a candidate; a pass circle is never computed)
        f = fixture("pre_tangent_circle_direction", [Q0, circle(2, (600.0, 0.0), 100.0, REFL, REFL)], (400.0, 200.0),
                    direction=(200.0, -150.0))
    assert s.fired(2) and no_hits_on(f, 2), kinds(f)
    fx.append(f)
    # aimed shots through the same circle: P (400, 200), cursor (100, 200), direct and infinity first
    for plan, name in (([], "pre_tangent_mirror_direct"), ([INF], "pre_tangent_mirror_infinity_first")):
        with Spy() as s:
            p = pair(fx, name, [Q0, mirror_q0], (400.0, 200.0), (100.0, 200.0), plan)
        assert s.fired(1, (500, 0)) and no_hits_on(p, 1) and no_hits_on(fx[-1], 1), kinds(p)
    # (v) the identity frame: a straight aim parallel to the mirror y = 0 through the
    # inversion center (500, 0): its image circle is tangent to the mirror there
    inv = circle(2, (500.0, 0.0), 200.0, INV, INV)
    m0 = line(1, (0.0, 0.0), (1000.0, 0.0), REFL, REFL)
    with Spy() as s:
        f = fixture("inversion_center_on_mirror", [inv, m0], (0.0, 100.0), direction=(1.0, 0.0))
    assert s.fired(1, (500, 0)), kinds(f)
    fx.append(f)
    # (vi) the record reflected: the image circle reflects in x = 650, Q' = (800, 0), tangent to y = 0 there
    m1 = line(1, (650.0, -500.0), (650.0, 500.0), REFL, REFL)
    m3 = line(3, (700.0, 0.0), (900.0, 0.0), REFL, REFL)
    with Spy() as s:
        f = fixture("reflected_pole_second_mirror", [inv, m1, m3], (0.0, 100.0), direction=(1.0, 0.0))
    assert s.fired(3, (800, 0)), kinds(f)
    fx.append(f)
    # (viii) departing from Q: a mirror through Q0 = (576, 320) reflects the beam at Q,
    # the beam returns there (the departure re-hit, again from Q) while a pass circle
    # through Q is crossed at the departure point at the end of each loop
    q1 = pre(1, (576.0, 320.0), 64.0)
    pc = circle(2, (608.0, 320.0), 32.0, PASS, PASS)
    mv = line(3, (576.0, 256.0), (576.0, 384.0), REFL, REFL)
    f = fixture("departing_from_q_rehit", [q1, pc, mv], (608.0, 528.0), direction=(0.0, -1.0))
    at_q = [e for e in f["expected"] if e["kind"] == "hit" and e["surface_id"] == 3 and e.get("applied")]
    assert len(at_q) >= 3, kinds(f)
    fx.append(f)
    # (ix) a straight beam through Q0 = (448, 704) (the player, Q0 and the direction
    # are collinear): the concurrent mirror and pass line through Q tie exactly there
    q2 = pre(1, (448.0, 704.0), 96.0)
    f = fixture("straight_beam_concurrent_at_q", [q2, line(2, (448.0, 505.0), (448.0, 903.0), REFL, REFL),
                # (Stage 134: line 3 passes and writes a flag -- a candidate; a pure pass is never computed)
                line(3, (313.0, 704.0), (583.0, 704.0), W3, W3), line(4, (720.0, 136.0), (816.0, 176.0), REFL, REFL)],
                (672.0, 592.0), direction=(2.0, -1.0))
    assert sum(1 for e in f["expected"] if e["kind"] == "hit" and e["surface_id"] == 3) >= 3, kinds(f)
    fx.append(f)
    # (vii) K1: the portals playground, a vertical aim (translation portal moves the pole)
    dump = json.load(open(os.path.join(ROOT, "bench", "scenes", "playground__portals.surfaces.json")))
    f32 = lambda x: struct.unpack("f", struct.pack("f", x))[0]  # noqa: E731
    # (the planned trace of [8/L, 8/L] passes the unplanned arc on a straight beam:
    # nothing to do with the tangency, and the oracle's mp aim -- an approximation of
    # an exactly vertical direction -- meets walls 1e70 px away; physical only)
    for plan, name, modes in (([], "k1_portals_vertical", ("planned", "physical")),
                              ([(8, "LEFT"), (8, "LEFT")], "k1_portals_vertical_inversion_twice", ("physical",))):
        for mode in modes:
            with Spy() as s:
                f = fixture(name + "_" + mode, dump["surfaces"], (f32(1243.333), 690.0), plan=plan, mode=mode,
                            cursor=(f32(1243.333), 390.0))
            if mode == "physical":
                # (the planned traces pass the unplanned arc: their beam stays straight
                # and crosses mirror 5 transversally)
                assert s.fired(5) and no_hits_on(f, 5), kinds(f)
            fx.append(f)
    out = os.path.join(ROOT, "bench", "fixtures", "stage105_tangent_at_infinity.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(k[0][:3], k[1], k[3]) for k in kinds(f)][:8])


if __name__ == "__main__":
    main()
