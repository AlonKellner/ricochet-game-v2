"""Stage 83 fixtures: projective effects and degenerate inputs, traced by the
oracle (trace2). Same fixture format as gen_fixtures_stage81.py.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage83.py
Writes bench/fixtures/stage83_projective.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import REFL, TERM, arc, fixture, line, room   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def proj(kind, back=False, normal=None):
    c = {"type": kind, "interactive": True, "back": back}
    if normal is not None:
        c["normal"] = list(normal)
    return c


def both(c):
    return c, c


def level(name):
    return json.load(open(os.path.join(ROOT, "bench", "scenes", name + ".surfaces.json")))["surfaces"]


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -900, -900, 900, 900)
    for back in (False, True):
        p = line(1, (250, -500), (250, 500), *both(proj("line_normal_projection", back)))
        fx.append(fixture("line_normal_%s" % ("back" if back else "through"), [p] + walls, (100.0, 200.0),
                          direction=(500.0, 200.0)))
        a = arc(1, (200, 100), (200, 300), (300, 200), *both(proj("circle_normal_projection", back)))
        fx.append(fixture("circle_normal_inside_%s" % ("back" if back else "through"), [a] + walls, (250.0, 200.0),
                          direction=(1.0, 0.125)))
        fx.append(fixture("circle_normal_outside_%s" % ("back" if back else "through"), [a] + walls, (500.0, 150.0),
                          direction=(-1.0, 0.125)))
        d = line(1, (300, -500), (300, 500), *both(proj("line_directional_projection", back, (1, 1))))
        fx.append(fixture("line_directional_%s" % ("back" if back else "through"), [d] + walls, (0.0, 0.0),
                          direction=(1.0, 0.1)))
        cd = arc(1, (300, 200), (300, 400), (400, 300), *both(proj("circle_directional_projection", back, (1, 0))))
        fx.append(fixture("circle_directional_%s" % ("back" if back else "through"), [cd] + walls, (200.0, 300.0),
                          direction=(1.0, 0.125)))
    # a projective after a mirror clears the stack: the mirror is pushed again
    m = line(1, (100, -500), (100, 500), REFL, REFL)
    pn = line(2, (-300, -500), (-300, 500), *both(proj("line_normal_projection", True)))
    fx.append(fixture("projective_clears_stack", [m, pn] + walls, (0.0, 0.0), direction=(1.0, 0.25)))
    # PLANNED mode consumes a projective plan entry without applying it
    pl = level("playground__projective_lines")
    fx.append(fixture("planned_passes_projective", pl, (960.0, 540.0), cursor=(1300.0, 600.0),
                      plan=[(5, "LEFT")], mode="planned"))
    # degenerate inputs (points at infinity in the anchored construction):
    # player / cursor exactly at an inversion center
    ig = level("playground__inversion_grid")
    fx.append(fixture("player_at_inversion_center", ig, (760.0, 380.0), cursor=(1000.0, 700.0),
                      plan=[(1, "RIGHT")], mode="physical"))
    fx.append(fixture("player_at_inversion_center_planned", ig, (760.0, 380.0), cursor=(1000.0, 700.0),
                      plan=[(1, "RIGHT")], mode="planned"))
    fx.append(fixture("cursor_at_inversion_center", ig, (600.0, 650.0), cursor=(960.0, 540.0),
                      plan=[(5, "LEFT")], mode="planned"))
    fx.append(fixture("cursor_at_inversion_center_no_plan", ig, (600.0, 650.0), cursor=(960.0, 540.0),
                      mode="physical"))
    out = os.path.join(ROOT, "bench", "fixtures", "stage83_projective.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["side"], e["applied"]) for e in f["expected"]][:5], len(f["expected"]))


if __name__ == "__main__":
    main()
