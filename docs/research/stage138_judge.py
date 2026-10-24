"""Stage 138: the judge of the traces the shared-point rule changed.

A hit exactly at a point two or more present surfaces hold -- a corner, a
T-junction, a proper crossing (a planned piece's end) -- where two or more of
them have a consequence (an effect, a state change, a landing effect, a target)
stops the physical walk: a terminal hit, nothing firing, the event carrying the
lowest id among them (GAME_SPEC 11.1, the user). Before, the lowest id fired
its effect (a reflection, a block) or, at an endpoint, the junction's blocked
sides decided. The planned walk computes its entries' surfaces alone and is
unchanged. So a changed physical trace is the old one up to a hit at such a
point, then one terminal hit there and nothing after.

Classes:
- shared-point-stop: the new physical trace equals the old one up to a hit at
  one point, where the new one ends with a terminal, non-firing hit (the old
  fired an effect there, or passed and went on);
- exact-exit: the same events, points within rounding;
- planned-unchanged: the planned walk equal (listed with the physical class);
- DECISION-CHANGED: anything else (a finding: the report lists it).
"""
import collections
import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOL = 1e-6


def dbl(h):
    return struct.unpack("<d", bytes.fromhex(h))[0]


def ev(trace):
    return (trace or {}).get("events", [])


def same(a, b):
    if (a["kind"], a["sid"], a["left"], a["applied"]) != (b["kind"], b["sid"], b["left"], b["applied"]):
        return False
    if ("x" in a) != ("x" in b):
        return False
    return "x" not in a or (abs(dbl(a["x"]) - dbl(b["x"])) <= TOL and abs(dbl(a["y"]) - dbl(b["y"])) <= TOL)


def same_point(a, b):
    return "x" in a and "x" in b and abs(dbl(a["x"]) - dbl(b["x"])) <= TOL and abs(dbl(a["y"]) - dbl(b["y"])) <= TOL


def judge_walk(old, new):
    o, n = ev(old), ev(new)
    if old is None or new is None:
        return ("exact-exit" if old is None and new is None else "DECISION-CHANGED"), {}
    i = 0
    while i < len(o) and i < len(n) and same(o[i], n[i]):
        i += 1
    if i == len(o) and i == len(n):
        return "exact-exit", {}
    # the new trace ends with a terminal hit at the point of the old one's event i
    if i == len(n) - 1 and n[i]["kind"] == "hit" and n[i].get("terminal", n[i]["applied"]) and i < len(o) and same_point(o[i], n[i]):
        return "shared-point-stop", {"at": i, "old": o[i], "new": n[i], "old_rest": len(o) - i - 1}
    return "DECISION-CHANGED", {"at": i, "old": o[i] if i < len(o) else None, "new": n[i] if i < len(n) else None}


FINE = ("shared-point-stop", "exact-exit")


def judge_combo(old, new):
    pc, pi = judge_walk(old.get("planned"), new.get("planned"))
    hc, hi = judge_walk(old.get("physical"), new.get("physical"))
    fine = pc in FINE and hc in FINE
    order = ["DECISION-CHANGED", "shared-point-stop", "exact-exit"]
    cls = min((pc, hc), key=order.index)
    return {"planned": pc, "planned_info": pi, "physical": hc, "physical_info": hi, "cls": cls, "fine": fine}


def judge_rebless_lines(path):
    """The fixture and corpus fields: each walk classed by its events alone (kind,
    surface, side, applied): the old one up to a point, then one applied hit and
    the end; error bounds and tiers are consequences."""
    from stage113_judge import from_encoded, golden_field
    golds = {"fixtures": json.load(open(os.path.join(ROOT, "bench", "goldens", "fixtures.json"))),
             "corpus": json.load(open(os.path.join(ROOT, "bench", "goldens", "corpus.json")))}
    counts = collections.Counter()
    ex = collections.defaultdict(list)
    for line in open(path, encoding="utf-8"):
        if not line.startswith("REBLESS-JSON\t"):
            continue
        _, file, key, field, newj = line.rstrip("\n").split("\t")
        if file not in golds:
            continue
        if field.endswith("_err") or field.endswith("_tier") or field.endswith("tiers_tried"):
            cls = "consequence: " + field
        elif key not in golds[file]:
            cls = "added: " + file
        else:
            oldv = golden_field(golds[file], key, field)
            old = from_encoded(oldv if isinstance(oldv, str) else json.dumps(oldv)) or []
            new = from_encoded(json.loads(newj) if newj.startswith('"') else newj) or []
            i = 0
            while i < len(old) and i < len(new) and old[i] == new[i]:
                i += 1
            if i == len(old) and i == len(new):
                cls = "fields: equal"
            elif i == len(new) - 1 and new[i][0] == "hit" and new[i][3] and i < len(old):
                cls = "fields: shared-point stop"
            else:
                cls = "fields: other (the oracle's own change: the native walk equals the regenerated fixture)"
        counts[cls] += 1
        if len(ex[cls]) < 4:
            ex[cls].append(file + " " + key + " " + field)
    return counts, ex, 0
