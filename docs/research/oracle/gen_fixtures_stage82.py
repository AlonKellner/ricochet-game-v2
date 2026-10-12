"""Stage 82 portal fixtures: hand-built and real-level scenes traced by the
oracle (trace2). Same fixture format as gen_fixtures_stage81.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage82.py
Writes bench/fixtures/stage82_portals.json.
"""
import json
import math
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import REFL, TERM, fixture, line, room   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def portal_pair(sid, a, b, theta, d, via=None):
    """Source sid (a -> b, optional arc via) and its pair sid + 1 = T(source),
    T(z) = e^{i theta} z + d (as the game builds it, float64 here)."""
    c, s = math.cos(theta), math.sin(theta)

    def T(p):
        return [c * p[0] - s * p[1] + d[0], s * p[0] + c * p[1] + d[1]]
    en = (c, -s)
    inv_d = [-(en[0] * d[0] - en[1] * d[1]), -(en[0] * d[1] + en[1] * d[0])]
    ca = {"type": "portal", "interactive": True, "theta": theta, "d": list(d), "pair": sid + 1}
    cb = {"type": "portal", "interactive": True, "theta": -theta, "d": inv_d, "pair": sid}
    if via is None:
        return line(sid, a, b, ca, ca), line(sid + 1, T(a), T(b), cb, cb)
    src = {"id": sid, "line": False, "start": list(a), "end": list(b), "via": list(via), "full": False,
           "left": ca, "right": ca, "is_target": False}
    tgt = {"id": sid + 1, "line": False, "start": T(a), "end": T(b), "via": T(via), "full": False,
           "left": cb, "right": cb, "is_target": False}
    return src, tgt


def main():
    mp.mp.dps = 80
    fx = []
    src, tgt = portal_pair(1, (100, -500), (100, 500), 0.0, (300, 0))
    fx.append(fixture("portal_translate", [src, tgt] + room(3, -200, -600, 900, 600), (0.0, 0.0), direction=(1.0, 0.25)))
    short_s, short_t = portal_pair(1, (100, -10), (100, 10), 0.0, (300, 0))
    fx.append(fixture("portal_pop_departure_rehit", [short_s, short_t], (0.0, 0.0), direction=(1.0, 0.0625)))
    m = line(3, (500, -500), (500, 500), REFL, REFL)
    fx.append(fixture("portal_mirror_then_pair_pushes", [src, tgt, m, line(4, (-200, -500), (-200, 500), TERM, TERM)],
                      (0.0, 0.0), direction=(1.0, 0.25)))
    rs, rt = portal_pair(1, (300, -100), (300, 100), math.pi / 2, (0.0, 0.0))
    fx.append(fixture("portal_rotated_quarter", [rs, rt] + room(3, -600, -600, 600, 600), (0.0, 0.0), direction=(1.0, 0.125)))
    as_, at = portal_pair(1, (300, -100), (300, 100), 0.0, (400, 0), via=(250, 0))
    fx.append(fixture("portal_arc", [as_, at] + room(3, -600, -600, 1200, 600), (0.0, 0.0), direction=(1.0, 0.125)))
    # planned through a portal on the real level
    tp = json.load(open(os.path.join(ROOT, "bench", "scenes", "test_levels__portals.surfaces.json")))["surfaces"]
    fx.append(fixture("planned_through_portal_level", tp, (760.0, 500.0), cursor=(1250.0, 560.0),
                      plan=[(6, "RIGHT")], mode="planned"))
    fx.append(fixture("physical_through_portal_level", tp, (760.0, 500.0), cursor=(1250.0, 560.0),
                      plan=[(6, "RIGHT")], mode="physical"))
    out = os.path.join(ROOT, "bench", "fixtures", "stage82_portals.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["side"], e["applied"], e["on_segment"]) for e in f["expected"]][:6],
              len(f["expected"]))


if __name__ == "__main__":
    main()
