"""Stage 113 (K11): the judge of the traces the exact hit points changed.

A planned projective's hit point that is a data point of its segment is now
that point exactly, and the exit line (and the aim image behind it) is exact
data; a planned chain's beams carry exact lines where a tier's rounding had
to be climbed before. Every changed trace must be a consequence of exactly
that: the same decisions (the same events: kind, surface, side, applied,
rehit) in both walks, the points within the tiers' rounding, the ladder
settling at a tier no higher than before. A changed decision is a finding --
unless it is the finding itself: a physical hit at a segment data point that
fired before and does not now (the endpoint is blocked on one side), which
the labels and bench/fixtures/stage113_exact_hits.json pin against the oracle.

K14 (the same stage): an epoch is identified by its projective firings, not
their count, so the step frame ids of a pair whose two walks fire different
projectives are interned in another order; the events, points and tiers are
untouched. Such a combo is classed frame-id when the display canons (the
reference's native/build-ref/judge/old_canon.raw, tools/judge.sh, against
the current rebless.raw) differ in the steps' frame_id field alone
(stage113_canon_diff.py); without the canons, identical dumps.

Classes: frame-id (K14's relabelling), exact-exit (same
decisions; the max point difference and the tiers are reported), endpoint-unfired (the physical walk's first change is a hit
whose `applied` went true -> false at a data point of its segment, and the
walks after it differ as a divergence does), DECISION-CHANGED (anything
else). Fields that are consequences (error bounds, tiers) are counted.
"""
import collections
import json
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOL = 1e-6


def dbl(h):
    if len(h) == 17 and h.startswith("f"):
        h = h[1:]
    return struct.unpack("<d", bytes.fromhex(h))[0]


def decisions(trace):
    return [(e["kind"], int(e["sid"]), bool(e.get("left")), bool(e["applied"]), bool(e.get("rehit"))) for e in (trace or {}).get("events", [])]


def points(trace):
    out = []
    for e in (trace or {}).get("events", []):
        out.append((dbl(e["x"]), dbl(e["y"])) if "x" in e else None)
    return out


def max_point_diff(a, b):
    m = 0.0
    for p, q in zip(points(a), points(b)):
        if p is None or q is None:
            continue
        m = max(m, abs(p[0] - q[0]), abs(p[1] - q[1]))
    return m


def data_points(defs, sid):
    for d in defs:
        if int(d["id"]) == sid:
            pts = [d["s"], d["e"]] + ([d["v"]] if d.get("v") else [])
            return [(dbl(p[0]), dbl(p[1])) for p in pts if p is not None]
    return []


def judge_trace(old, new, defs):
    do, dn = decisions(old), decisions(new)
    if do == dn:
        m = max_point_diff(old, new)
        lo, ln = (old or {}).get("limbs"), (new or {}).get("limbs")
        if m <= TOL and (ln is None or lo is None or ln <= lo):
            return "exact-exit", {"max_dpx": m, "limbs": [lo, ln]}
        return "DECISION-CHANGED", {"max_dpx": m, "limbs": [lo, ln], "why": "points or tiers"}
    # the first change: a hit that fired before and not now, at a data point of its segment
    i = 0
    while i < min(len(do), len(dn)) and do[i] == dn[i]:
        i += 1
    if i < min(len(do), len(dn)) and do[i][:3] == dn[i][:3] and do[i][3] and not dn[i][3]:
        e = new["events"][i]
        if "x" in e:
            p = (dbl(e["x"]), dbl(e["y"]))
            if any(abs(p[0] - q[0]) <= 1e-9 and abs(p[1] - q[1]) <= 1e-9 for q in data_points(defs, do[i][1])):
                return "endpoint-unfired", {"at": i, "surface": do[i][1]}
    return "DECISION-CHANGED", {"at": i, "old": do[i] if i < len(do) else None, "new": dn[i] if i < len(dn) else None}


FINE = ("frame-id", "exact-exit", "endpoint-unfired")


def is_fine(cls):
    return cls.startswith("consequence: ") or cls in FINE


_CANON = None  # (group, combo) -> the set of differing canon field names, or None without the canons


def canon_diff(group, combo):
    global _CANON
    if _CANON is None:
        _CANON = {}
        out = os.path.join(ROOT, "native", "build-ref", "judge")
        old_p, new_p = os.path.join(out, "old_canon.raw"), os.path.join(out, "rebless.raw")
        if os.path.exists(old_p) and os.path.exists(new_p):
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import stage113_canon_diff as C
            old, new = C.load(old_p), C.load(new_p)
            for k in new:
                if k in old:
                    d = C.fields_differing(old[k], new[k])
                    _CANON[k] = {x[2:] if x[:2] in ("P:", "H:") else x for x in d}
    return _CANON.get((group, combo))


def judge_combo(old, new):
    same_dumps = old.get("planned") == new.get("planned") and old.get("physical") == new.get("physical")
    canon = canon_diff(new.get("group"), new.get("combo"))
    if canon == {"frame_id"} or (canon is None and same_dumps):
        # K14: only the steps' frame ids (in the canon, not the dump) changed
        return {"planned": "frame-id", "planned_info": {}, "physical": "frame-id", "physical_info": {}, "cls": "frame-id", "fine": True}
    pc, pi = judge_trace(old["planned"], new["planned"], new.get("defs", []))
    hc, hi = judge_trace(old["physical"], new["physical"], new.get("defs", []))
    fine = is_fine(pc) and is_fine(hc)
    cls = pc if not is_fine(pc) else (hc if not is_fine(hc) else (pc if pc != "exact-exit" else hc))
    return {"planned": pc, "planned_info": pi, "physical": hc, "physical_info": hi, "cls": cls, "fine": fine}


# ---- the fixture and corpus fields (the goldens' encoding)

def from_encoded(text):
    j = json.loads(text) if isinstance(text, str) else text
    if j is None:
        return None
    evs = []
    for s in j["events"]:
        f = s.split(";")
        evs.append((f[0], int(f[1]), f[2] == "1", f[3] == "1"))
    return evs


def golden_field(gold, key, field):
    v = gold[key]
    for part in field.split("/"):
        v = v[part]
    return v


def judge_rebless_lines(path):
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
            counts["consequence: " + field] += 1
            continue
        if key not in golds[file]:
            counts["added: " + file] += 1
            continue
        oldv = golden_field(golds[file], key, field)
        old = from_encoded(oldv if isinstance(oldv, str) else json.dumps(oldv))
        new = from_encoded(json.loads(newj) if newj.startswith('"') else newj)
        cls = "exact-exit" if old == new else "DECISION-CHANGED"
        counts[cls] += 1
        if len(ex[cls]) < 4:
            ex[cls].append(file + " " + key + " " + field)
    findings = sum(v for k, v in counts.items() if not (is_fine(k) or k.startswith("added: ")))
    return counts, ex, findings
