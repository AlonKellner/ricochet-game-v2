"""Stage 106: the independent judge of changed traces.

Two builds of the native solver dump the same sweep combos (RC_SWEEP_DUMP, see
native/tests/test_goldens.cpp): every event with its point and the beam it
lies on. Where the two traces part, this script decides which one is right
from geometry alone, at 200 digits: the beam of the first differing hit event
(both traces agree up to there, so it is the same restored beam), the point D
of the hit before it (the departure), every crossing of that beam with every
carrier, the crossing at D excluded, ordered as a beam is walked (ahead of D
first, then the wrapped ones). The next crossing is the event the trace must
show. None of the walker's rules (stack, known points, snapping) take part:
only the root algebra of the oracle (geometry.py / beam.py) is reused.

Usage:
  docs/research/.venv/bin/python docs/research/stage106_judge.py --old old.txt --new new.txt \
      [--dps 200] [--json out.json] [--examples 6]
old.txt / new.txt: the stdout of the two dump runs (lines "DUMP\t{json}").

Classes per trace: fix (new = judge != old), regression (old = judge != new),
both-wrong (neither = judge), undecidable (two candidates within TIE px, or no
hit differs: a difference only in non-hit events or in the sign of a zero),
beam-differs (the restored beams are not the same object: not a departure
question), prefix (one trace is a prefix of the other: the shorter one ended
by the event budget; judged on the longer).
"""
import argparse
import collections
import json
import os
import struct
import sys

import mpmath as mp

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "oracle"))
from beam import Beam  # noqa: E402
from geometry import Seg, seg_hits  # noqa: E402

POINT_TOL = 1e-6      # px: an event point "is" a crossing
DEPART_TOL = 1e-7     # px: a root "is" the departure point D (D carries float64 rounding)
TIE = 1e-9            # px: two candidates this close are the ladder's business, not the judge's


def dbl(h):
    return struct.unpack("<d", bytes.fromhex(h))[0]


def load(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        if line.startswith("DUMP\t"):
            d = json.loads(line[5:])
            out[(d["group"], d["combo"])] = d
    return out


def hits(trace):
    return [e for e in (trace or {}).get("events", []) if e["kind"] == "hit"]


def pt(e):
    return (dbl(e["x"]), dbl(e["y"])) if "x" in e else None


def same_hit(a, b):
    if a["sid"] != b["sid"] or a["applied"] != b["applied"]:
        return False
    pa, pb = pt(a), pt(b)
    if pa is None or pb is None:
        return pa == pb
    return abs(pa[0] - pb[0]) <= 1e-9 and abs(pa[1] - pb[1]) <= 1e-9


def segs_of(defs):
    out = []
    for d in defs:
        if d["pre"]:
            continue
        s = (dbl(d["s"][0]), dbl(d["s"][1]))
        e = (dbl(d["e"][0]), dbl(d["e"][1]))
        if d["line"]:
            out.append(Seg.line(d["id"], s, e, d["full"]))
        elif "circle" in d:
            out.append(Seg.full_circle(d["id"], (dbl(d["circle"][0]), dbl(d["circle"][1])), dbl(d["circle"][2])))
        else:
            out.append(Seg.arc(d["id"], s, e, (dbl(d["v"][0]), dbl(d["v"][1]))))
    return out


def judge_next(beam_hex, D, segs):
    """The next crossing after D along the beam: (sid, point, margin_px) or
    ('rehit', D, inf) when no other crossing exists; None when undecidable."""
    px, py, tx, ty, k = (dbl(h) for h in beam_hex)
    beam = Beam(mp.mpc(px, py), mp.mpc(tx, ty), mp.mpf(k))
    Dz = mp.mpc(D[0], D[1])
    tD = beam.param_of(Dz)
    cands = []
    at_d = 0
    for seg in segs:
        for t in seg_hits(beam, seg):
            z = beam.at(t)
            if abs(z - Dz) < DEPART_TOL:
                at_d += 1
                continue
            ahead = (t == mp.inf) or (t > tD)
            cands.append((0 if ahead else 1, t, seg.id, z))
    if at_d > 1:
        return None
    if not cands:
        return ("rehit", D, mp.inf)
    cands.sort(key=lambda c: (c[0], c[1]))
    first = cands[0]
    margin = abs(cands[1][3] - first[3]) if len(cands) > 1 else mp.inf
    if margin < TIE:
        return None
    return (first[2], (float(first[3].real), float(first[3].imag)), float(margin))


def beams_close(a, b, rel=1e-9):
    for x, y in zip(a, b):
        fx, fy = dbl(x), dbl(y)
        if abs(fx - fy) > rel * max(1.0, abs(fx), abs(fy)):
            return False
    return True


def matches(expected, event, D):
    if event is None:
        return False
    sid, p, _ = expected
    if sid == "rehit":
        q = pt(event)
        return bool(event.get("rehit")) or (q is not None and abs(q[0] - D[0]) < POINT_TOL and abs(q[1] - D[1]) < POINT_TOL)
    q = pt(event)
    return event["sid"] == sid and q is not None and abs(q[0] - p[0]) < POINT_TOL and abs(q[1] - p[1]) < POINT_TOL


def cursor_index(trace):
    """The number of hit events before the cursor event (None: no cursor event)."""
    n = 0
    for e in (trace or {}).get("events", []):
        if e["kind"] == "cursor":
            return n
        if e["kind"] == "hit":
            n += 1
    return None


def judge_trace(old, new, segs, portal_src):
    ho, hn = hits(old), hits(new)
    n = min(len(ho), len(hn))
    i = 0
    while i < n and same_hit(ho[i], hn[i]):
        i += 1
    if i == n and len(ho) == len(hn):
        return {"cls": "undecidable", "why": "no hit differs"}
    eo = ho[i] if i < len(ho) else None
    en = hn[i] if i < len(hn) else None
    if i == 0:
        return {"cls": "undecidable", "why": "the first hit differs (the aim, not a departure)"}
    beam_hex = (en or eo).get("beam")
    if eo is not None and en is not None and eo.get("beam") != en.get("beam"):
        # the same restored beam up to rounding (an exact vs a rounded hit point
        # upstream): judged on the new build's beam; a real difference is not a
        # departure question
        if beam_hex is None or eo.get("beam") is None or not beams_close(beam_hex, eo["beam"]):
            return {"cls": "beam-differs", "at": i}
    prev = ho[i - 1]
    # the departure point: a fresh departure (an effect fired, no pop) starts the
    # beam at its own p (a portal's pair, after a teleport); a pop or a pass
    # leaves the beam at the previous hit's point
    if beam_hex is None:
        return {"cls": "undecidable", "why": "no beam"}
    D = (dbl(beam_hex[0]), dbl(beam_hex[1])) if (prev["applied"] and not prev.get("popped")) else pt(prev)
    if prev.get("popped") and prev.get("teleport"):
        # a pop through a portal pair: the beam resumes at the pop point mapped
        # back through the source portal (a translation; a rotation is not judged)
        src = portal_src.get(prev["sid"])
        if src is None or src[2] != 0.0:
            return {"cls": "undecidable", "why": "a rotated portal pop"}
        D = (D[0] - src[0], D[1] - src[1])
    if D is None:
        return {"cls": "undecidable", "why": "no departure point"}
    expected = judge_next(beam_hex, D, segs)
    if expected is None:
        return {"cls": "undecidable", "why": "a tie", "at": i}
    ok_old, ok_new = matches(expected, eo, D), matches(expected, en, D)
    cls = {(True, False): "fix", (False, True): "regression", (False, False): "both-wrong", (True, True): "undecidable"}[(ok_new, ok_old)]
    if ok_old and ok_new and eo is not None and en is not None and eo["applied"] != en["applied"]:
        # the same crossing, applied in one trace only: the cursor sits at the
        # departure point and is reached immediately in one of them (so the walk
        # is in post-cursor physical mode there and not in the other)
        cls = "applied-differs"
        return {"cls": cls, "at": i, "expected": [expected[0], list(expected[1]) if expected[0] != "rehit" else list(D), expected[2]],
                "old": [eo["sid"], list(pt(eo) or ()), eo["applied"]], "new": [en["sid"], list(pt(en) or ()), en["applied"]], "departure": list(D),
                "cursor_event_index": {"old": cursor_index(old), "new": cursor_index(new)}, "hit_index": i}
    if eo is None or en is None:
        cls = cls if cls != "undecidable" else "prefix"
    return {"cls": cls, "at": i, "expected": [expected[0], list(expected[1]) if expected[0] != "rehit" else list(D), expected[2]],
            "old": None if eo is None else [eo["sid"], list(pt(eo) or ()), eo["applied"]],
            "new": None if en is None else [en["sid"], list(pt(en) or ()), en["applied"]],
            "departure": list(D)}


# ---- the rules-module interface of docs/research/judge.py (make judge RULES=stage106)

FINDINGS = ("regression", "both-wrong", "beam-differs", "applied-differs")


def judge_combo(old, new):
    mp.mp.dps = max(mp.mp.dps, 200)
    segs = segs_of(new["defs"])
    portal_src = {}
    for df in new["defs"]:
        if "portal" in df:
            portal_src[df["pair"]] = tuple(dbl(h) for h in df["portal"])
    out = {}
    worst = "undecidable"
    rank = {"regression": 4, "both-wrong": 3, "beam-differs": 2, "applied-differs": 2, "fix": 1, "prefix": 1, "undecidable": 0}
    for which in ("planned", "physical"):
        if old.get(which) is None or new.get(which) is None:
            continue
        r = judge_trace(old[which], new[which], segs, portal_src)
        out[which] = r["cls"]
        if rank[r["cls"]] > rank[worst]:
            worst = r["cls"]
    out["cls"] = worst
    out["fine"] = worst not in FINDINGS
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--dps", type=int, default=200)
    ap.add_argument("--json")
    ap.add_argument("--examples", type=int, default=6)
    args = ap.parse_args()
    mp.mp.dps = args.dps
    old, new = load(args.old), load(args.new)
    keys = sorted(set(old) & set(new))
    missing = sorted(set(old) ^ set(new))
    records = []
    per_group = collections.defaultdict(collections.Counter)
    for key in keys:
        o, nw = old[key], new[key]
        segs = segs_of(nw["defs"])
        # pair id -> (dx, dy, theta) of the portal whose pair it is: the map that
        # took the beam from the source to the pair, undone at a pop at the pair
        portal_src = {}
        for df in nw["defs"]:
            if "portal" in df:
                portal_src[df["pair"]] = tuple(dbl(h) for h in df["portal"])
        rec = {"group": key[0], "combo": key[1]}
        worst = "undecidable"
        for which in ("planned", "physical"):
            if o.get(which) is None or nw.get(which) is None:
                continue
            r = judge_trace(o[which], nw[which], segs, portal_src)
            rec[which] = r
            rank = {"regression": 4, "both-wrong": 3, "beam-differs": 2, "fix": 1, "prefix": 1, "applied-differs": 1, "undecidable": 0}
            if rank[r["cls"]] > rank[worst]:
                worst = r["cls"]
        rec["cls"] = worst
        records.append(rec)
        per_group[key[0].rsplit("/", 1)[0]][worst] += 1
    total = collections.Counter(r["cls"] for r in records)
    print("combos judged: %d (in one dump only: %d)" % (len(records), len(missing)))
    print("classes:", dict(total))
    print()
    print("| group | changed | fix | regression | both-wrong | undecidable | other |")
    print("|---|---|---|---|---|---|---|")
    for g in sorted(per_group):
        c = per_group[g]
        other = sum(v for k, v in c.items() if k not in ("fix", "regression", "both-wrong", "undecidable"))
        print("| %s | %d | %d | %d | %d | %d | %d |" % (g, sum(c.values()), c["fix"], c["regression"], c["both-wrong"], c["undecidable"], other))
    for cls in ("regression", "both-wrong", "beam-differs", "applied-differs", "undecidable"):
        bad = [r for r in records if r["cls"] == cls]
        if bad:
            print("\n%s (%d):" % (cls, len(bad)))
            for r in bad[:50]:
                print("  %s combo %d: %s" % (r["group"], r["combo"], json.dumps({k: v for k, v in r.items() if k in ("planned", "physical")})[:300]))
    fixes = [r for r in records if r["cls"] == "fix"]
    print("\nexamples of fixes:")
    for r in fixes[:args.examples]:
        print("  %s combo %d: %s" % (r["group"], r["combo"], json.dumps({k: v for k, v in r.items() if k in ("planned", "physical")})[:400]))
    if args.json:
        json.dump({"classes": dict(total), "records": records, "missing": missing}, open(args.json, "w"), indent=1)


if __name__ == "__main__":
    main()
