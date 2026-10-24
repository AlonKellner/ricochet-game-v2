"""Stage 110 fixtures: B5, a plan entry matched by the frame (GAME_SPEC 14.1).

Surfaces on one carrier are crossed at one point (K6). When the physical walk
fires an unplanned surface there that writes what the planned entry writes, it
leaves the point in the frame the planned walk leaves it in: the entry counts
as consumed, whatever the order of the surfaces at the point. (Stage 138: two
surfaces with a consequence at one point stop the arrow instead; the rescue
remains where one surface alone is hit at the point.) Both walks then
reach the cursor together, as the merge (frame ids) says. Controls: a different
value, a different point, a transformative effect (stack frames carry surface
ids) -- each a divergence.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage110_frames.py
Writes bench/fixtures/stage110_frame_entries.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, REFL, line, room  # noqa: E402
from gen_fixtures_stage107 import present_when, set_, tog, writes  # noqa: E402
from gen_fixtures_stage110 import decisions, pair, reaches, side_toward  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

PLAYER, CURSOR = (100.0, 0.0), (500.0, 100.0)   # the shot crosses x = 300 at y = 50
ON = ((300, 0), (300, 100))      # a segment the shot crosses
UPPER = ((300, 200), (300, 400))  # the same carrier, off the shot
LOWER = ((300, -400), (300, -200))


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    rescues = []
    T.B5_HOOK = lambda w: rescues.append(1)

    def side_of(surfs, sid, player, cursor):
        """The side the straight shot crosses surface sid's carrier from (a plain
        pass copy of it alone: no writes, no presence, no other surface)."""
        s = dict(next(x for x in surfs if x["id"] == sid), left=PASS, right=PASS)
        s.pop("presence", None)
        return side_toward([s], player, cursor, sid)

    def case(name, surfs, plan_ids, rescued, player=PLAYER, cursor=CURSOR, extra_plan=()):
        """plan_ids: surface ids planned, each from the side the straight shot crosses it."""
        plan = [(sid, side_of(surfs, sid, player, cursor)) for sid in plan_ids] + list(extra_plan)
        before = len(rescues)
        pl, ph = pair(name, surfs, player, cursor, plan)
        fired = len(rescues) > before
        if rescued:
            assert fired, (name, "no rescue")
            assert reaches(pl) and reaches(ph), (name, [e["kind"] for e in ph["expected"]])
        else:
            assert not (reaches(pl) and reaches(ph) and decisions(pl) == decisions(ph)), name
        fx.extend([pl, ph])

    f_p = writes(set_("f", "p"))
    # the physical walk fires the lower surface; the planned one, above it on the
    # carrier, is absent once the flag is written (hit_effects' gate, surface 16)
    case("lower_fires_upper_planned",
         [present_when(line(1, *ON, f_p, f_p), ("f", ["DEFAULT"])),
          present_when(line(2, *UPPER, f_p, f_p), ("f", ["DEFAULT"]))] + walls, [2], True)
    # the same without presence: the planned surface stays, a write of the
    # current value (B1) that the shadow consumes
    case("lower_fires_upper_present", [line(1, *ON, f_p, f_p), line(2, *UPPER, f_p, f_p)] + walls, [2], True)
    # the planned surface comes first (lower id, off the shot); the physical
    # walk fires the next one
    case("upper_fires_lower_planned", [line(1, *UPPER, f_p, f_p), line(2, *ON, f_p, f_p)] + walls, [1], True)
    # one entry, two unplanned writes at one point: Stage 138 (the user) -- two
    # surfaces with a consequence hold the point, so the physical walk stops
    # there, neither writes, and the cursor is not reached (no rescue)
    ab = writes(set_("a", 1), set_("b", 1))
    case("two_writes_one_entry",
         [line(1, *ON, writes(set_("a", 1)), writes(set_("a", 1))),
          line(2, (300, 25), (300, 75), writes(set_("b", 1)), writes(set_("b", 1))),
          line(3, *UPPER, ab, ab)] + walls, [3], False)
    # a rescue, then a mirror consumed by its surface id (cursor behind the mirror's image)
    mirror = line(4, (400, -400), (400, 400), REFL, REFL)
    cur = (350.0, 150.0)
    surfs = [line(1, *ON, f_p, f_p), line(2, *UPPER, f_p, f_p), mirror] + walls
    case("rescue_then_mirror", surfs, [2], True, cursor=cur,
         extra_plan=[(4, side_toward([mirror] + walls, PLAYER, (450.0, 150.0), 4))])
    # ghost togglers (GAME_SPEC 10.6): both walks pass the mirror ghosted
    g = writes(tog("ghost"))
    case("ghost_toggler_rescue",
         [line(1, *ON, g, g), line(2, *UPPER, g, g), line(4, (400, -400), (400, 400), REFL, REFL)] + walls, [2], True)
    # the cursor exactly at the shared crossing (exact data)
    case("cursor_at_the_point", [line(1, *ON, f_p, f_p), line(2, *UPPER, f_p, f_p)] + walls, [2], True,
         cursor=(300.0, 50.0))
    # the reported level: test_levels/hit_effects, plan [16 R]
    d = json.load(open(os.path.join(ROOT, "bench", "scenes", "test_levels__hit_effects.surfaces.json")))
    case("hit_effects_16R", d["surfaces"], [16], True, player=(960.0, 540.0), cursor=(1260.0, 324.0))

    # controls
    f_q = writes(set_("f", "q"))
    case("different_value", [line(1, *ON, f_q, f_q), line(2, *UPPER, f_p, f_p)] + walls, [2], False)
    case("different_point", [line(1, *ON, f_p, f_p), line(2, (350, 200), (350, 400), f_p, f_p)] + walls, [2], False)
    case("transform_not_rescued", [line(1, *ON, REFL, REFL), line(2, *UPPER, REFL, REFL)] + walls, [2], False,
         cursor=(150.0, 100.0))

    out = os.path.join(ROOT, "bench", "fixtures", "stage110_frame_entries.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["applied"]) for e in f["expected"]][:6])


if __name__ == "__main__":
    main()
