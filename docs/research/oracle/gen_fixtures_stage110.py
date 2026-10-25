"""Stage 110 fixtures: B1, entries that change no frame (GAME_SPEC 14.1).

A planned entry the planned walk consumes without changing the frame (a pass
side, a terminal crossed off its segment, a write of the value a flag already
has) is consumed by the physical walk at the same event, and an unplanned write
that leaves the flags as they are does not take the physical walk off the plan.
Both walks therefore reach the cursor together, as the merge (frame ids) says.
A toggler (it always changes the flags) is the control: unplanned, the two walks
leave it in different frames.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage110.py
Writes bench/fixtures/stage110_quiet_entries.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import PASS, TERM, line, room  # noqa: E402
from gen_fixtures_stage107 import fixture, set_, tog, writes  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def side_toward(surfs, player, cursor, sid):
    """The side the straight shot player -> cursor crosses surface sid from."""
    # (Stage 134: the surface as a mirror -- a pass surface is no candidate, never
    # computed; Stage 139: a long mirror along its carrier, so the crossing is on
    # its segment and an event; only the side is read)
    refl = {"type": "reflection", "interactive": True}

    def long(d):
        (sx, sy), (ex, ey) = d["start"], d["end"]
        dx, dy = ex - sx, ey - sy
        return dict(d, start=[sx - 1e4 * dx, sy - 1e4 * dy], end=[ex + 1e4 * dx, ey + 1e4 * dy], left=refl, right=refl, resolver=None)

    probe = [long(d) if d["id"] == sid else d for d in surfs]
    w = T.trace_physical_direction(T.scene_from_json(probe), player, T.P(cursor) - T.P(player))
    return next(e.side for e in w.events if e.kind == "hit" and e.surface_id == sid)


def decisions(f):
    """(kind, surface, side, applied, flags) per significant event, up to and
    including the cursor (Stage 134: the walks compute different candidates --
    a crossing that fires nothing is no decision)."""
    out = []
    prev = json.dumps(f.get("flags") or {}, sort_keys=True)
    for e in f["expected"]:
        flags = json.dumps(e["flags"] or {}, sort_keys=True)
        sig = e["kind"] != "hit" or e["applied"] or flags != prev
        if e["kind"] == "hit":
            prev = flags
        if sig:
            out.append((e["kind"], e["surface_id"], e["side"], e["applied"], flags))
        if e["kind"] == "cursor":
            break
    return out


def reaches(f):
    return any(e["kind"] == "cursor" for e in f["expected"])


def pair(name, surfs, player, cursor, plan, flags=None):
    """The planned and the physical fixture of one shot."""
    out = []
    for mode in ("planned", "physical"):
        out.append(fixture("%s_%s" % (name, mode), surfs, player, plan=plan, mode=mode, cursor=cursor, flags=flags))
    return out


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    player, cursor = (100.0, 0.0), (500.0, 100.0)

    def quiet(name, surf, flags=None, planned=True):
        surfs = [surf] + walls
        plan = [(1, side_toward(surfs, player, cursor, 1))] if planned else []
        pl, ph = pair(name, surfs, player, cursor, plan, flags)
        assert reaches(pl) and reaches(ph), (name, [e["kind"] for e in ph["expected"]])
        assert decisions(pl) == decisions(ph), (name, decisions(pl), decisions(ph))
        fx.extend([pl, ph])

    # a planned pass side (an open door)
    quiet("pass_entry", line(1, (300, -400), (300, 400), PASS, PASS))
    # a planned terminal crossed off its segment (the beam passes beside it): Stage 162 (the user) -- the planned
    # walk stops at its planned hit, the physical one passes and reaches the cursor (the walks diverge)
    surfs = [line(1, (300, 200), (300, 400), TERM, TERM)] + walls
    pl, ph = pair("terminal_entry_off_segment", surfs, player, cursor, [(1, side_toward(surfs, player, cursor, 1))])
    assert not reaches(pl) and reaches(ph), ([e["kind"] for e in pl["expected"]], [e["kind"] for e in ph["expected"]])
    assert pl["expected"][0]["terminal"] and not pl["expected"][0]["on_segment"], pl["expected"][0]
    fx.extend([pl, ph])
    # ... and met on its segment: both walks stop there (the cursor beyond it)
    surfs = [line(1, (300, -400), (300, 400), TERM, TERM)] + walls
    pl, ph = pair("terminal_entry_on_segment", surfs, player, cursor, [(1, side_toward(surfs, player, cursor, 1))])
    assert decisions(pl) == decisions(ph) and not reaches(pl) and not reaches(ph), (decisions(pl), decisions(ph))
    fx.extend([pl, ph])
    # a planned write of the value the flag already has
    same = writes(set_("k", 1))
    quiet("noop_write_entry", line(1, (300, -400), (300, 400), same, same), flags={"k": 1})
    # an unplanned write of the value the flag already has
    quiet("noop_write_unplanned", line(1, (300, -400), (300, 400), same, same), flags={"k": 1}, planned=False)

    # control: an unplanned toggler always changes the flags -- the two walks
    # leave the toggler in different frames (a divergence)
    t = writes(tog("k"))
    surfs = [line(1, (300, -400), (300, 400), t, t)] + walls
    pl, ph = pair("toggle_unplanned", surfs, player, cursor, [])
    assert decisions(pl) != decisions(ph), decisions(ph)
    fx.extend([pl, ph])

    out = os.path.join(ROOT, "bench", "fixtures", "stage110_quiet_entries.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(e["kind"], e["surface_id"], e["applied"]) for e in f["expected"]][:5])


if __name__ == "__main__":
    main()
