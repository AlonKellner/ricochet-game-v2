"""Stage 139: the judge of the traces the off-segment rule changed.

A candidate's crossing off its segment, on a surface no remaining plan entry
names, is not an event any more (GAME_SPEC 12.1, 12.6, the user): not counted
by the 64 limit, not drawn. So a changed trace is the old one with some of its
unfired hits removed; a trace the old 64 count cut (its last event a "max")
may now go on past that cut. A carrier's roots now come from its
representative surface (the lowest id), so a point can move by rounding (the
tolerance below). The planned walk computes its entries' surfaces alone and
changes only by those roundings and after its cursor.

Classes:
- offseg-removed: the new trace is the old one minus hits that fired nothing
  (applied false), the other events equal within rounding;
- continues-past-max: the old trace ended with a "max" (the 64 count, which the
  removed crossings filled); the new one has the old's events (minus the removed)
  and goes on after them;
- exact-exit: the same events, points within rounding;
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


def removable(e):
    """An old event the rule removes: a hit that fired nothing (not the departure re-hit)."""
    return e["kind"] == "hit" and not e["applied"] and not e.get("rehit", False)


def judge_walk(old, new, plan_ids):
    if old is None or new is None:
        return ("exact-exit" if old is None and new is None else "DECISION-CHANGED"), {}
    o, n = ev(old), ev(new)
    i = j = 0
    removed = 0
    while i < len(o) and j < len(n):
        if same(o[i], n[j]):
            i += 1
            j += 1
        elif removable(o[i]):
            removed += 1
            i += 1
        else:
            break
    while i < len(o) and removable(o[i]) and j == len(n):
        removed += 1
        i += 1
    # the new trace's off-segment hits must be plan surfaces' (the dump carries on_segment since Stage 139)
    offseg_new = [e for e in n if e["kind"] == "hit" and e.get("on_segment") is False and e["sid"] not in plan_ids]
    if offseg_new:
        return "DECISION-CHANGED", {"offseg_unplanned": offseg_new[0]}
    if i == len(o) and j == len(n):
        return ("offseg-removed" if removed else "exact-exit"), {"removed": removed}
    if i == len(o) - 1 and o[i]["kind"] == "max" and j < len(n):
        return "continues-past-max", {"removed": removed, "old_cut_at": i, "new_more": len(n) - j}
    return "DECISION-CHANGED", {"at": (i, j), "old": o[i] if i < len(o) else None, "new": n[j] if j < len(n) else None, "removed": removed}


FINE = ("offseg-removed", "continues-past-max", "exact-exit")
ORDER = ["DECISION-CHANGED", "continues-past-max", "offseg-removed", "exact-exit"]


def judge_combo(old, new):
    plan_ids = {p.get("surface_id", p.get("sid")) for p in (new.get("plan") or []) if isinstance(p, dict)}
    pc, pi = judge_walk(old.get("planned"), new.get("planned"), plan_ids)
    hc, hi = judge_walk(old.get("physical"), new.get("physical"), plan_ids)
    fine = pc in FINE and hc in FINE
    cls = min((pc, hc), key=ORDER.index)
    return {"planned": pc, "planned_info": pi, "physical": hc, "physical_info": hi, "cls": cls, "fine": fine}


def judge_rebless_lines(path):
    """The fixture and corpus fields: each walk classed by its events alone (kind,
    surface, side, applied): the old one minus unfired hits, or the old one cut by
    a max and continued; error bounds and tiers are consequences."""
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
            i = j = 0
            removed = 0
            while i < len(old) and j < len(new):
                if old[i] == new[j]:
                    i += 1
                    j += 1
                elif old[i][0] == "hit" and not old[i][3]:
                    removed += 1
                    i += 1
                else:
                    break
            while i < len(old) and j == len(new) and old[i][0] == "hit" and not old[i][3]:
                removed += 1
                i += 1
            if i == len(old) and j == len(new):
                cls = "fields: off-segment hits removed" if removed else "fields: equal"
            elif i == len(old) - 1 and old[i][0] == "max" and j < len(new):
                cls = "fields: continues past the old max"
            else:
                cls = "fields: other (the oracle's own change: the native walk equals the regenerated fixture)"
        counts[cls] += 1
        if len(ex[cls]) < 4:
            ex[cls].append(file + " " + key + " " + field)
    return counts, ex, 0
