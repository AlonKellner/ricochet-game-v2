"""Stage 171 fixtures: flag expressions (GAME_SPEC 9.11) wherever a flag is read --
presence, a resolver's key, a state change's conditions -- traced by the oracle (trace2).

Usage:
  docs/research/.venv/bin/python -I docs/research/oracle/gen_fixtures_stage171.py

Writes bench/fixtures/stage171_flag_logic.json:
- A mirror present while "a && !b" appears when a pass writes a mid-shot; with b set it never does.
- A block present while "a ^ b" is false (the door form: "DEFAULT", "false") appears when the pass
  writes b: a false expression reads as a missing flag.
- A resolver keyed "(a && b) || c" reflects once c is written; one with an explicit "false" row.
- A state change applied when "!a || b": skipped with a alone, applied with a and b.
- Planned and physical with a plan through the expression mirror.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_fixtures_stage81 import REFL, TERM, line, room  # noqa: E402
from gen_fixtures_stage85 import categorical, kinds  # noqa: E402
from gen_fixtures_stage107 import fixture, flags_after, present_when, set_, writes  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def hit_ids(f):
    return [k[1] for k in kinds(f) if k[0] == "hit"]


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    dirn = (1.0, 0.0625)
    sets_a = writes(set_("a", True))

    # presence "a && !b": the pass writes a, the mirror appears and reflects; b set: it never appears
    mirror = present_when(line(2, (500, -400), (500, 400), REFL, REFL), ("a && !b", ["true"]))
    scene = [line(1, (300, -400), (300, 400), sets_a, sets_a), mirror] + walls
    f = fixture("expr_presence_appears", scene, (100.0, 0.0), direction=dirn)
    assert hit_ids(f)[:2] == [1, 2] and kinds(f)[1][3], kinds(f)
    fx.append(f)
    f = fixture("expr_presence_held_back", scene, (100.0, 0.0), direction=dirn, flags={"b": True})
    assert 2 not in hit_ids(f), kinds(f)
    fx.append(f)

    # the door form: present while "a ^ b" is false ("DEFAULT", "false"); a set, so absent until b is written
    sets_b = writes(set_("b", True))
    block = present_when(line(2, (500, -400), (500, 400), TERM, TERM), ("a ^ b", ["DEFAULT", "false"]))
    scene = [line(1, (300, -400), (300, 400), sets_b, sets_b), block] + walls
    f = fixture("expr_presence_false_reads_default", scene, (100.0, 0.0), direction=dirn, flags={"a": True})
    assert hit_ids(f)[:2] == [1, 2] and f["expected"][1]["terminal"], kinds(f)
    fx.append(f)
    f = fixture("expr_presence_true_absent", [block] + walls, (100.0, 0.0), direction=dirn, flags={"a": True})
    assert 2 not in hit_ids(f), kinds(f)
    fx.append(f)

    # a resolver keyed "(a && b) || c": a pass, until c is written
    sets_c = writes(set_("c", True))
    gate = categorical(line(2, (500, -400), (500, 400)), "(a && b) || c", {"true": (REFL, REFL), "DEFAULT": (None, None)})
    scene = [line(1, (300, -400), (300, 400), sets_c, sets_c), gate] + walls
    f = fixture("expr_resolver_switches", scene, (100.0, 0.0), direction=dirn, flags={"a": True})
    assert hit_ids(f)[:2] == [1, 2] and kinds(f)[1][3], kinds(f)
    fx.append(f)
    # an explicit "false" row: "!x" with x set -- false -- reflects; the "true" row a wall
    gate = categorical(line(2, (500, -400), (500, 400)), "!x", {"true": (TERM, TERM), "false": (REFL, REFL)})
    f = fixture("expr_resolver_false_row", [gate] + walls, (100.0, 0.0), direction=dirn, flags={"x": True})
    assert hit_ids(f)[0] == 2 and kinds(f)[0][3] and not f["expected"][0]["terminal"], kinds(f)
    fx.append(f)

    # a state change applied when "!a || b"
    cond = {"key": "k", "op": "set", "value": 1, "when": [{"key": "!a || b", "values": ["true"]}]}
    side = writes(cond)
    scene = [line(1, (300, -400), (300, 400), side, side)] + walls
    f = fixture("expr_when_skipped", scene, (100.0, 0.0), direction=dirn, flags={"a": True})
    assert "k" not in flags_after(f, 1), flags_after(f, 1)
    fx.append(f)
    f = fixture("expr_when_applied", scene, (100.0, 0.0), direction=dirn, flags={"a": True, "b": True})
    assert flags_after(f, 1).get("k") == 1, flags_after(f, 1)
    fx.append(f)

    # planned and physical through the expression mirror: the plan's simulated flags make it present
    mirror = present_when(line(2, (500, -400), (500, 400), REFL, REFL), ("a && !b", ["true"]))
    scene = [line(1, (300, -400), (300, 400), sets_a, sets_a), mirror] + walls
    for mode in ("planned", "physical"):
        f = fixture("expr_planned_%s" % mode, scene, (100.0, 0.0), plan=[(1, "RIGHT"), (2, "RIGHT")], mode=mode,
                    cursor=(200.0, 150.0))
        assert any(e["kind"] == "cursor" for e in f["expected"]), kinds(f)
        fx.append(f)

    out = os.path.join(ROOT, "bench", "fixtures", "stage171_flag_logic.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], [(k[0][:3], k[1], k[2], k[3]) for k in kinds(f)][:6])


if __name__ == "__main__":
    main()
