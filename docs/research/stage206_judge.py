"""Stage 206: the judge of the traces counterpart surfaces changed (GAME_SPEC 12.1).

Where the planned walk's beam cannot reach its next entry's surface before the
frame's infinity (an aim outside a planned circle's tangent cone; an aim facing
away from a planned line -- infinity unplanned is a divergence, so the plan is
missed; the user, 2026-11-07), it meets the surface's counterpart: a significant
"counterpart" event that consumes nothing and marks the plan dead -- no entry
fires after it, the cursor never comes; a planned terminal's ends the walk. So
a changed planned trace is the old one up to the counterpart, and past it only
passages of infinity and of the ray's origin and the final cut, or nothing (a
terminal's). PHYSICAL walks, the realization and the regions compute none: a
changed physical trace is a finding.

Classes:
- counterpart-inserted: the planned trace is the old one up to the counterpart
  event; past it passages (infinity / origin) and a max cut at most, or nothing;
- cut-at-counterpart: the same events but the last, a max cut: at the passage of infinity
  before (the eleventh significant event), at the counterpart now (one significant event
  sooner, at its crossing: no point at infinity, its point on the beam);
- exact-exit: the same events, points within rounding;
- DECISION-CHANGED: anything else (a finding: the report lists it).
"""
import collections
import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOL = 1e-6
PASSAGES = ("infinity", "origin")


def dbl(h):
    return struct.unpack("<d", bytes.fromhex(h))[0]


def same(a, b):
    if (a["kind"], a["sid"], a["left"], a["applied"]) != (b["kind"], b["sid"], b["left"], b["applied"]):
        return False
    if ("x" in a) != ("x" in b):
        return False
    return "x" not in a or (abs(dbl(a["x"]) - dbl(b["x"])) <= TOL and abs(dbl(a["y"]) - dbl(b["y"])) <= TOL)


def classify(o, n, eq, physical):
    """The old events o against the new n (eq: event equality)."""
    i = 0
    while i < len(o) and i < len(n) and eq(o[i], n[i]):
        i += 1
    if i == len(o) and i == len(n):
        return "exact-exit", {}
    if physical:
        return "DECISION-CHANGED", {"at": i, "old": o[i:i + 3], "new": n[i:i + 3]}
    if i < len(n) and n[i]["kind"] == "counterpart":
        rest = n[i + 1:]
        if all(e["kind"] in PASSAGES for e in rest[:-1]) and (not rest or rest[-1]["kind"] in PASSAGES + ("max",)):
            return "counterpart-inserted", {"at": i, "surface": n[i]["sid"], "old_rest": [e["kind"] for e in o[i:]][:6],
                                            "terminal": not rest}
    if i == len(o) - 1 and i == len(n) - 1 and o[i]["kind"] == "max" and n[i]["kind"] == "max" and o[i]["sid"] == -3:
        return "cut-at-counterpart", {"at": i}
    return "DECISION-CHANGED", {"at": i, "old": [e["kind"] for e in o[i:]][:6], "new": [e["kind"] for e in n[i:]][:6]}


def ev(trace):
    return (trace or {}).get("events", [])


def judge_walk(old, new, physical):
    if old is None or new is None:
        return ("exact-exit" if old is None and new is None else "DECISION-CHANGED"), {}
    return classify(ev(old), ev(new), same, physical)


FINE = ("counterpart-inserted", "cut-at-counterpart", "exact-exit")
ORDER = ["DECISION-CHANGED", "counterpart-inserted", "cut-at-counterpart", "exact-exit"]


def judge_combo(old, new):
    pc, pi = judge_walk(old.get("planned"), new.get("planned"), False)
    hc, hi = judge_walk(old.get("physical"), new.get("physical"), True)
    return {"planned": pc, "planned_info": pi, "physical": hc, "physical_info": hi, "cls": min((pc, hc), key=ORDER.index),
            "fine": pc in FINE and hc in FINE}


def judge_rebless_lines(path):
    """The fixture and corpus fields: each walk classed by its events alone
    (kind, surface, side, applied); error bounds and tiers are consequences."""
    from stage113_judge import from_encoded, golden_field
    golds = {"fixtures": json.load(open(os.path.join(ROOT, "bench", "goldens", "fixtures.json"))),
             "corpus": json.load(open(os.path.join(ROOT, "bench", "goldens", "corpus.json")))}
    counts = collections.Counter()
    ex = collections.defaultdict(list)
    findings = 0

    def as_dicts(evs):
        return [{"kind": k, "sid": s, "left": lf, "applied": a} for (k, s, lf, a) in evs or []]
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
            old = as_dicts(from_encoded(oldv if isinstance(oldv, str) else json.dumps(oldv)))
            new = as_dicts(from_encoded(json.loads(newj) if newj.startswith('"') else newj))
            c, _ = classify(old, new, lambda a, b: a == b, "physical" in key or "physical" in field)
            cls = "fields: " + c
            findings += 0 if c in FINE else 1
        counts[cls] += 1
        if len(ex[cls]) < 4:
            ex[cls].append(file + " " + key + " " + field)
    return counts, ex, findings
