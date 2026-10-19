"""Stage 114: the judge of the traces the fork rule changed.

Of a fork's candidates those on the segment are now preferred, then the
click-nearest (GAME_SPEC 13.3). A trace changes when the old click-nearest
candidate was off the segment while its antipode was on it: the planned walk
fires the projective at the other candidate (its first differing decision is
that projective's hit, off segment before, on segment now, or the hit point
moved), and the realization aims elsewhere, so the physical walk changes too.
A target on the carrier is now its own candidate (the on-carrier exclusion is
gone): the same shape of change. K15: a combo the old build had no trace for
(the beam through an inversion's centre) is traced now.

Classes: fork-on-segment (the planned walk's first change is at a projective
hit of the plan, or its hit point moved; the physical walk follows), k15-traced
(no old trace), exact-exit (same decisions, points within rounding),
DECISION-CHANGED (anything else).
"""
import collections
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOL = 1e-6


def dbl(h):
    import struct
    return struct.unpack("<d", bytes.fromhex(h))[0]


def decisions(trace):
    return [(e["kind"], e.get("sid"), e.get("left"), e.get("applied"), e.get("on_segment")) for e in (trace or {}).get("events", [])]


def points(trace):
    out = []
    for e in (trace or {}).get("events", []):
        p = e.get("point")
        out.append(None if p is None else (dbl(p[0]), dbl(p[1])))
    return out


def max_point_diff(a, b):
    m = 0.0
    for p, q in zip(points(a), points(b)):
        if p is None or q is None:
            continue
        m = max(m, abs(p[0] - q[0]), abs(p[1] - q[1]))
    return m


def first_change(old, new):
    do, dn = decisions(old), decisions(new)
    i = 0
    while i < min(len(do), len(dn)) and do[i] == dn[i]:
        i += 1
    return i, do, dn


def judge_planned(old, new, proj_sids):
    i, do, dn = first_change(old, new)
    if i == len(do) == len(dn):
        m = max_point_diff(old, new)
        return ("exact-exit" if m <= TOL else "DECISION-CHANGED"), {"max_dpx": m}
    # the first change at (or right before) a projective's applied hit: the fork switched
    for k in (i, i - 1):
        if 0 <= k < len(dn) and dn[k][0] == "hit" and dn[k][3] and dn[k][1] in proj_sids:
            return "fork-on-segment", {"at": k, "surface": dn[k][1], "old": do[k] if k < len(do) else None, "new": dn[k]}
        if 0 <= k < len(do) and do[k][0] == "hit" and do[k][3] and do[k][1] in proj_sids:
            return "fork-on-segment", {"at": k, "surface": do[k][1], "old": do[k], "new": dn[k] if k < len(dn) else None}
    return "DECISION-CHANGED", {"at": i, "old": do[i] if i < len(do) else None, "new": dn[i] if i < len(dn) else None}


FINE = ("fork-on-segment", "k15-traced", "exact-exit")


def is_fine(cls):
    return cls.startswith("consequence: ") or cls in FINE


def judge_combo(old, new):
    if old.get("planned") is None and new.get("planned") is not None:
        return {"planned": "k15-traced", "planned_info": {}, "physical": "k15-traced", "physical_info": {}, "cls": "k15-traced", "fine": True}
    proj = set()
    for e in new.get("plan") or []:
        proj.add(e.get("surface_id"))
    pc, pi = judge_planned(old.get("planned"), new.get("planned"), proj)
    # the physical walk follows the realization: any change is a consequence when the planned one is a fork switch
    hi, _, _ = first_change(old.get("physical"), new.get("physical"))
    same_h = decisions(old.get("physical")) == decisions(new.get("physical"))
    hc = "exact-exit" if same_h and max_point_diff(old.get("physical"), new.get("physical")) <= TOL else ("fork-on-segment" if pc == "fork-on-segment" else "DECISION-CHANGED")
    fine = is_fine(pc) and is_fine(hc)
    cls = pc if not is_fine(pc) else (hc if not is_fine(hc) else (pc if pc != "exact-exit" else hc))
    return {"planned": pc, "planned_info": pi, "physical": hc, "physical_info": {"at": hi}, "cls": cls, "fine": fine}


from stage113_judge import from_encoded, golden_field  # noqa: E402  (the goldens' encoding)


def first_projective_firing(text):
    """(sid, on_segment, point) of the first applied projective hit of an encoded
    trace (the goldens' event fields: kind;sid;left;applied;terminal;on_segment;
    ...;projective;...;point), or None."""
    j = json.loads(text) if isinstance(text, str) else text
    if j is None:
        return None
    for s in j["events"]:
        f = s.split(";")
        if f[0] == "hit" and f[3] == "1" and len(f) > 16 and f[9] == "1":
            return (int(f[1]), f[5] == "1", f[16])
    return None


def judge_rebless_lines(path):
    """The fixture and corpus fields: a change whose first differing event is a
    projective's applied hit (the fork switched) is the rule's; error bounds and
    tiers are consequences; an old field with no trace is K15's."""
    golds = {"fixtures": json.load(open(os.path.join(ROOT, "bench", "goldens", "fixtures.json"))),
             "corpus": json.load(open(os.path.join(ROOT, "bench", "goldens", "corpus.json")))}
    counts = collections.Counter()
    ex = collections.defaultdict(list)
    classed = {}
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
        if old is None and new is not None:
            cls = "k15-traced"
        elif old == new:
            cls = "exact-exit"
        else:
            # the fork switched: the first applied projective hit moved (another
            # candidate: its point, or off the segment before and on it now);
            # everything after it, and the aim before it, follows
            fo, fn = first_projective_firing(oldv if isinstance(oldv, str) else json.dumps(oldv)), \
                first_projective_firing(json.loads(newj) if newj.startswith('"') else newj)
            fork = fo is not None and fn is not None and fo[0] == fn[0] and (fo[1] != fn[1] or fo[2] != fn[2])
            cls = "fork-on-segment" if fork else "DECISION-CHANGED"
        classed[(file, key, field)] = cls
    # the physical walk follows the realization: a physical field whose planned
    # sibling (the same tier) is the fork's is the fork's too
    for (file, key, field), cls in list(classed.items()):
        if cls == "DECISION-CHANGED" and "physical" in field:
            sib = field.replace("physical", "planned")
            if classed.get((file, key, sib)) == "fork-on-segment":
                classed[(file, key, field)] = "fork-on-segment"
    for (file, key, field), cls in classed.items():
        counts[cls] += 1
        if len(ex[cls]) < 4:
            ex[cls].append(file + " " + key + " " + field)
    findings = sum(v for k, v in counts.items() if not (is_fine(k) or k.startswith("added: ")))
    return counts, ex, findings
