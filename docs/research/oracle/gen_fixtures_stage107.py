"""Stage 107 fixtures: a side's LIST of state changes and surface presence
(GAME_SPEC 9.6 / 10.6), traced by the oracle (trace2). Same format as
gen_fixtures_stage85.py (per event: "state" = the side's changes fired,
"flags" = the flags after the event).

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage107.py
Writes bench/fixtures/stage107_hit_effects.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import categorical, kinds  # noqa: E402
from gen_fixtures_stage85 import fixture as fixture85  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def writes(*changes, effect=None, interactive=True):
    """A side config with a list of state changes (and an optional effect)."""
    c = dict(effect) if effect else {"type": "pass"}
    c["interactive"] = interactive
    c["states"] = [dict(ch) for ch in changes]
    return c


def set_(key, value):
    return {"key": key, "op": "set", "value": value}


def tog(key):
    return {"key": key, "op": "toggle", "value": True}


def fired(o, e):
    """Each event's `fired` bit (Stage 107: the side fired, ghost aside)."""
    o["fired"] = bool(e.fired)


def fixture(*args, **kw):
    return fixture85(*args, event_extra=fired, **kw)


def present_when(surf, *conds):
    """surf with presence conditions (key, [values])."""
    out = dict(surf)
    out["presence"] = [{"key": k, "values": list(v)} for k, v in conds]
    return out


def flags_after(f, sid):
    """The flags after the first hit on surface sid."""
    return next(e["flags"] for e in f["expected"] if e["kind"] == "hit" and e["surface_id"] == sid)


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    dirn = (1.0, 0.0625)

    # N1: two writes of different keys on one side fire together
    two = writes(tog("a"), set_("b", 7))
    f = fixture("multi_state_two_keys", [line(1, (300, -400), (300, 400), two, two)] + walls, (100.0, 0.0),
                direction=dirn)
    assert kinds(f)[0][4] and flags_after(f, 1) == {"a": True, "b": 7}, kinds(f)
    fx.append(f)

    # N1: list order -- set then toggle the same key; toggle then set
    st = writes(set_("k", "x"), tog("k"))
    ts = writes(tog("j"), set_("j", "y"))
    f = fixture("multi_state_order", [line(1, (300, -400), (300, 400), st, st),
                                      line(2, (500, -400), (500, 400), ts, ts)] + walls, (100.0, 0.0),
                direction=dirn)
    assert flags_after(f, 1) == {"k": False}, flags_after(f, 1)
    assert flags_after(f, 2) == {"k": False, "j": "y"}, flags_after(f, 2)
    fx.append(f)

    # N1: a side whose second change writes ghost is a ghost toggler: the
    # mirror after it passes, the second toggler materializes, the mirror after
    # that reflects
    gt = writes(set_("n", 1), tog("ghost"))
    scene = [line(1, (300, -400), (300, 400), gt, gt), line(2, (450, -400), (450, 400), REFL, REFL),
             line(3, (600, -400), (600, 400), gt, gt), line(4, (800, -400), (800, 400), REFL, REFL)] + walls
    f = fixture("multi_state_ghost_in_list", scene, (100.0, 0.0), direction=dirn)
    ks = kinds(f)
    # (Stage 134: while ghosted the mirror 2 is passed -- no candidate, never computed)
    assert [k[1] for k in ks[:3]] == [1, 3, 4] and ks[2][3], ks
    assert ks[0][4] and ks[1][4], ks
    assert [e["fired"] for e in f["expected"][:3]] == [True, True, True], f["expected"][:3]
    fx.append(f)

    # N1: planned -- a consumed entry applies all its writes; the next entry is
    # categorical on the second key (reflects only when b == 1)
    pl = writes(set_("a", True), set_("b", 1))
    gate = categorical(line(2, (500, -400), (500, 400)), "b", {"1": (REFL, REFL), "DEFAULT": (TERM, TERM)})
    scene = [line(1, (300, -400), (300, 400), pl, pl), gate] + walls
    for mode in ("planned", "physical"):
        f = fixture("multi_state_planned_%s" % mode, scene, (100.0, 0.0), plan=[(1, "RIGHT"), (2, "RIGHT")],
                    mode=mode, cursor=(200.0, 150.0))
        assert any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
        fx.append(f)

    # N2: the spawn lifecycle in one shot. The host (a pass) sets chan:c to
    # "pending"; a mirror present only while armed vanishes (the beam crosses
    # where it was); a block present only while pending appears and stops it.
    chan = "chan:c"
    host = present_when(line(1, (300, -400), (300, 400), writes(set_(chan, "pending")),
                             writes(set_(chan, "pending"))), (chan, ["DEFAULT", "armed"]))
    armed_mirror = present_when(line(2, (500, -400), (500, 400), REFL, REFL), (chan, ["DEFAULT", "armed"]))
    pending_block = present_when(line(3, (700, -400), (700, 400), TERM, TERM), (chan, ["pending"]))
    f = fixture("presence_vanish_and_appear", [host, armed_mirror, pending_block] + walls, (100.0, 0.0),
                direction=dirn)
    ks = kinds(f)
    assert [k[1] for k in ks[:2]] == [1, 3] and ks[1][3], ks
    fx.append(f)
    # the same scene armed by default but started "pending": the host and the
    # mirror are absent, the block stops the shot at once
    f = fixture("presence_absent_from_start", [host, armed_mirror, pending_block] + walls, (100.0, 0.0),
                direction=dirn, flags={chan: "pending"})
    assert kinds(f)[0][1] == 3, kinds(f)
    fx.append(f)

    # N2: a reset (sets "armed") before a spawned group (present at "g1"): the
    # group vanishes in the same shot, the host reappears and fires
    reset = line(1, (200, -400), (200, 400), writes(set_(chan, "armed")), writes(set_(chan, "armed")))
    group = present_when(line(2, (400, -400), (400, 400), REFL, REFL), (chan, ["g1"]))
    host2 = present_when(line(3, (600, -400), (600, 400), writes(set_(chan, "pending")),
                              writes(set_(chan, "pending"))), (chan, ["DEFAULT", "armed"]))
    f = fixture("presence_reset_restores", [reset, group, host2] + walls, (100.0, 0.0), direction=dirn,
                flags={chan: "g1"})
    ks = kinds(f)
    assert [k[1] for k in ks[:2]] == [1, 3] and ks[1][4], ks
    assert flags_after(f, 3) == {chan: "pending"}, flags_after(f, 3)
    fx.append(f)

    # N2: an absent surface holds no point. Two collinear mirrors share the
    # endpoint (500, 0); the beam hits it exactly: both present -> a shared point
    # of two consequences stops the arrow, nothing fires (Stage 138); the lower
    # one absent -> a lone endpoint, the beam grazes it
    upper = line(1, (500, -300), (500, 0), REFL, REFL)
    lower = present_when(line(2, (500, 0), (500, 300), REFL, REFL), ("b", ["true"]))
    for name, flags in (("presence_junction_both", {"b": True}), ("presence_junction_absent", {})):
        f = fixture(name, [upper, lower] + walls, (100.0, 0.0), direction=(1.0, 0.0), flags=flags)
        first = f["expected"][0]
        both = name == "presence_junction_both"
        assert first["kind"] == "hit" and first["applied"] == both and first["terminal"] == both, kinds(f)
        assert not first["fired"], first
        fx.append(f)

    # N2: a plan entry on an absent surface is never consumed (no effect, no
    # state change); the planned path does not reach the cursor
    mirror = present_when(line(1, (500, -400), (500, 400), REFL, REFL), ("m", ["true"]))
    f = fixture("presence_plan_entry_absent", [mirror] + walls, (100.0, 0.0), plan=[(1, "LEFT")],
                mode="planned", cursor=(200.0, 150.0))
    assert not any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
    assert not any(e["surface_id"] == 1 for e in f["expected"]), kinds(f)
    fx.append(f)

    out = os.path.join(ROOT, "bench", "fixtures", "stage107_hit_effects.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], kinds(f)[:6], len(f["expected"]))


if __name__ == "__main__":
    main()
