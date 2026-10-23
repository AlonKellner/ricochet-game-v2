"""Stage 136: the judge of the traces the infinity switch's removal changed.

Infinity is always on (the user, 2026-10-23): the switch, a leftover of the Stage
88 migration, is gone. Whatever was recorded with it off (the native core's
default: the sweep goldens, the fixtures' and corpus' native goldens) is traced
with it on. So a changed trace is the old one with passages of infinity (and of
the ray's origin, which come with the wraps) inserted; where the old trace
escaped (a line leaving through infinity) the new one wraps and goes on; and
the passages count toward the limits, so a trace may be cut sooner.

Classes:
- passages-inserted: the new trace is the old one with infinity / origin
  events inserted;
- wraps-past-escape: the same up to the old trace's escape (the end of a line's
  loop with nothing fired: the switch off left through infinity there); the new
  one goes round again (its passage of infinity is listed on the loop the old
  one ended: whatever follows is new);
- cut-earlier: the same up to the new trace's max cut, sooner than the old end;
- cursor-after-an-unplanned-passage: a physical walk that passed infinity or
  the origin unplanned before its cursor (a divergence there) no longer lists
  the cursor (GAME_SPEC 12.6: a cursor is reached on the plan only);
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
    i = j = 0
    inserted = 0
    passed = False   # an inserted passage before this point
    while i < len(o) and j < len(n):
        if eq(o[i], n[j]):
            i += 1
            j += 1
        elif o[i]["kind"] == "cursor" and physical and passed and not any(e["kind"] == "cursor" for e in n):
            i += 1
        elif n[j]["kind"] in PASSAGES:
            inserted += 1
            passed = True
            j += 1
        elif o[i]["kind"] == "escape":
            # (the old walk ended at the end of its loop: the new one goes round again,
            # its passage of infinity listed on the loop it ended -- whatever follows is new)
            return ("wraps-past-escape" if i == len(o) - 1 else "DECISION-CHANGED"), {"at_old": i, "at_new": j, "new": n[j]["kind"]}
        elif n[j]["kind"] == "max" and j == len(n) - 1:
            return "cut-earlier", {"at_old": i, "old_rest": [e["kind"] for e in o[i:]][:6], "inserted": inserted}
        else:
            return "DECISION-CHANGED", {"at_old": i, "at_new": j, "old": o[i], "new": n[j]}
    rest_n = n[j:]
    rest_o = o[i:]
    if rest_o and rest_o[0]["kind"] == "escape" and len(rest_o) == 1:
        return ("wraps-past-escape" if rest_n else "DECISION-CHANGED"), {"at_old": i, "at_new": j}
    if physical and [e["kind"] for e in rest_o] == ["cursor"] and passed:
        rest_o = []
    if rest_o:
        return "DECISION-CHANGED", {"rest_old": [e["kind"] for e in rest_o][:6], "rest_new": [e["kind"] for e in rest_n][:6]}
    if any(e["kind"] not in PASSAGES for e in rest_n):
        return "DECISION-CHANGED", {"rest_new": [e["kind"] for e in rest_n][:6]}
    inserted += len(rest_n)
    lost_cursor = physical and any(e["kind"] == "cursor" for e in o) and not any(e["kind"] == "cursor" for e in n)
    if lost_cursor:
        return "cursor-after-an-unplanned-passage", {"inserted": inserted}
    return ("passages-inserted" if inserted else "exact-exit"), {"inserted": inserted}


def ev(trace):
    return (trace or {}).get("events", [])


def judge_walk(old, new, physical):
    if old is None or new is None:
        return ("exact-exit" if old is None and new is None else "DECISION-CHANGED"), {}
    return classify(ev(old), ev(new), same, physical)


FINE = ("passages-inserted", "wraps-past-escape", "cut-earlier", "cursor-after-an-unplanned-passage", "exact-exit")
ORDER = ["DECISION-CHANGED", "cursor-after-an-unplanned-passage", "wraps-past-escape", "cut-earlier", "passages-inserted", "exact-exit"]


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
