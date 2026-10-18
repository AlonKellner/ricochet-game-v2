"""Stage 111: the judge of the traces the realization rule changed.

The first epoch's orientation is now fixed by the plan's first entry (GAME_SPEC
13.1, R1-R3) instead of by the cursor's image. Every changed trace must be a
consequence of exactly that: the aim is the old aim run the other way round
(the same start, the tangent and curvature negated), and the first planned
entry e is met by the new realization where the old one never fired it (R1),
or both fired it and the new one is the orientation whose planned walk reaches
the cursor (R2). Anything else is a finding.

Inputs:
  --old old.txt --new new.txt   sweep dumps of the two builds (RC_SWEEP_DUMP,
                                native/tests/test_goldens.cpp): "DUMP\t{json}"
  --rebless lines.txt           RC_GOLDEN_REBLESS output: "REBLESS-JSON\t<file>\t<key>\t<field>\t<json>",
                                judged against bench/goldens/{fixtures,corpus}.json
Usage:
  docs/research/.venv/bin/python docs/research/stage111_judge.py --old old.txt --new new.txt \
      --rebless rebless_fixtures.txt [--json out.json] [--examples 4]

Classes (planned walks): R1-fix (the old aim never met e from its side, the
new one fires it), R2-reach (both orientations fire e; only the new one reaches
the cursor), R2-choice (both reach: the order against Q_0 or the entry's own
order decided), R2-neither (neither reaches), and the findings in capitals:
SAME-AIM-DIFF (the trace changed without the aim changing), AIM-OTHER (not
the same circle the other way), REVERSED-LOST-ENTRY (the old realization
fired e, the new does not), R2-LOST-REACH (the old reached the cursor, the new
does not). Physical walks: physical-follows (the aim reversed with the planned
twin's), else a finding. Fields that are consequences (error bounds, tiers)
are counted, not judged.
"""
import argparse
import collections
import json
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
INFINITY_ID = -3


def dbl(h):
    if len(h) == 17 and h.startswith("f"):
        h = h[1:]       # the goldens' encoding prefixes a float64 with "f"
    return struct.unpack("<d", bytes.fromhex(h))[0]


def cn(s):
    a, b = s.split("/")
    if a.startswith("b") or b.startswith("b"):
        return None
    return (dbl(a), dbl(b))


# ---- the two encodings, read into one shape: {"beam": (px, py, tx, ty, k) | None, "events": [(kind, sid, left, applied)]}

def from_dump_climb(c):
    if c is None:
        return None
    evs = []
    beam = None
    for e in c["events"]:
        evs.append((e["kind"], int(e["sid"]), bool(e["left"]), bool(e["applied"])))
        if beam is None and e.get("beam"):
            beam = tuple(dbl(h) for h in e["beam"])
    return {"beam": beam, "events": evs}


def from_encoded(text):
    if text is None or text == "null":
        return None
    j = json.loads(text) if isinstance(text, str) else text
    evs = []
    beam = None
    for s in j["events"]:
        f = s.split(";")
        evs.append((f[0], int(f[1]), f[2] == "1", f[3] == "1"))
        if beam is None and int(f[14]) >= 0:
            b = j["beams"][int(f[14])].split(";")
            p, t = cn(b[0]), cn(b[1])
            k = None if b[2].startswith("b") else dbl(b[2])
            if p is not None and t is not None:
                beam = (p[0], p[1], t[0], t[1], k)
    return {"beam": beam, "events": evs}


# ---- the plan's first entry and the walk's facts

def first_entry(plan):
    for p in plan:
        sid = int(p["surface_id"])
        if sid != INFINITY_ID:
            return sid, (p["side"] == "LEFT")
    return None


def fired(tr, e):
    return e is not None and any(k == "hit" and sid == e[0] and left == e[1] for k, sid, left, _ in tr["events"])


def reached(tr):
    return any(k == "cursor" for k, _, _, _ in tr["events"])


def aim_relation(a, b):
    """same | reversed | other | unknown (a beam not readable)"""
    if a is None or b is None or a["beam"] is None or b["beam"] is None:
        return "unknown"
    pa, pb = a["beam"], b["beam"]
    # a beam printed from another tier lands an ulp or two off: a tolerance of
    # the judge's, never a solver decision
    def close(x, y):
        return abs(x - y) <= 1e-9 * max(1.0, abs(x), abs(y))
    if not (close(pa[0], pb[0]) and close(pa[1], pb[1])):
        return "other"
    same = close(pa[2], pb[2]) and close(pa[3], pb[3]) and (pa[4] is None or pb[4] is None or close(pa[4], pb[4]))
    rev = close(pa[2], -pb[2]) and close(pa[3], -pb[3]) and (pa[4] is None or pb[4] is None or close(pa[4], -pb[4]))
    if same:
        return "same"
    if rev:
        return "reversed"
    return "other"


# the classes that are the rule's own consequences; everything else is a finding
FINE = {"R1-fix", "R2-reach", "R2-choice", "R2-neither", "reversed-neither-fires", "no-entry-reversed", "physical-follows"}


def is_fine(cls):
    return cls.startswith("consequence: ") or cls.split(": ")[-1].replace(" (aim unread)", "") in FINE


def judge_planned(old, new, plan):
    if old is None or new is None:
        return "NULL-TRACE"
    e = first_entry(plan)
    rel = aim_relation(old, new)
    if rel == "same":
        return "SAME-AIM-DIFF"
    if rel == "other":
        return "AIM-OTHER"
    # reversed, or unread (the big-limb encodings carry no float beam: the
    # facts below still decide; the aim is checked on the f64 twin)
    suffix = " (aim unread)" if rel == "unknown" else ""
    fo, fn = fired(old, e), fired(new, e)
    ro, rn = reached(old), reached(new)
    if not fo and fn:
        return "R1-fix" + suffix
    if fo and fn:
        if rn and not ro:
            return "R2-reach" + suffix
        if rn and ro:
            return "R2-choice" + suffix
        if ro and not rn:
            return "R2-LOST-REACH" + suffix
        return "R2-neither" + suffix
    if fo and not fn:
        return "REVERSED-LOST-ENTRY" + suffix
    return ("reversed-neither-fires" if e is not None else "no-entry-reversed") + suffix


def judge_physical(old, new, planned_class):
    if old is None or new is None:
        return "NULL-TRACE"
    rel = aim_relation(old, new)
    twin_fine = planned_class is not None and is_fine(planned_class)
    if rel in ("reversed", "unknown") and twin_fine:
        return "physical-follows" + (" (aim unread)" if rel == "unknown" else "")
    if rel in ("reversed", "unknown"):
        return "PHYSICAL-WITH-" + str(planned_class)
    return "PHYSICAL-AIM-" + rel.upper()


# ---- the sweep dumps

def load_dump(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        if line.startswith("DUMP\t"):
            d = json.loads(line[5:])
            out[(d["group"], d["combo"])] = d
    return out


def judge_sweeps(old_path, new_path, examples, verdicts):
    old = load_dump(old_path)
    new = load_dump(new_path)
    counts = collections.Counter()
    by_group = collections.defaultdict(collections.Counter)
    ex = collections.defaultdict(list)
    for key in sorted(new):
        if key not in old:
            counts["missing-in-old"] += 1
            continue
        o, n = old[key], new[key]
        plan = n["plan"]
        po, pn = from_dump_climb(o["planned"]), from_dump_climb(n["planned"])
        cls = judge_planned(po, pn, plan)
        ho, hn = from_dump_climb(o["physical"]), from_dump_climb(n["physical"])
        pcls = judge_physical(ho, hn, cls)
        counts[cls] += 1
        counts["physical: " + pcls] += 1
        by_group[key[0]][cls] += 1
        if len(ex[cls]) < examples:
            ex[cls].append("%s combo %d: player %s cursor %s plan %s" % (key[0], key[1], hexpt(n["player"]), hexpt(n["cursor"]),
                                                                           [(p["surface_id"], p["side"][:1]) for p in plan]))
        if len(ex["physical: " + pcls]) < examples:
            ex["physical: " + pcls].append("%s combo %d" % key)
        verdicts.append({"group": key[0], "combo": key[1], "planned": cls, "physical": pcls})
    return counts, by_group, ex


def hexpt(p):
    return "(%.4g, %.4g)" % (dbl(p[0]), dbl(p[1]))


# ---- the fixtures and the corpus

def plan_of_key(file, key):
    if file == "fixtures":
        stage, name = key.split("/", 1)
        fx = json.load(open(os.path.join(ROOT, "bench", "fixtures", stage + ".json")))["fixtures"]
        for f in fx:
            if f["name"] == name:
                return f["plan"], name.endswith("_physical")
        return None, False
    sub, name = key.split("/", 1)
    d = os.path.join(ROOT, "bench", "scenarios" if sub == "main" else os.path.join(sub, "scenarios"))
    scn = json.load(open(os.path.join(d, name + ".json")))
    return scn["plan"], None


def golden_field(gold, key, field):
    v = gold[key]
    for part in field.split("/"):
        v = v[part]
    return v


def judge_rebless(path, examples, verdicts):
    golds = {"fixtures": json.load(open(os.path.join(ROOT, "bench", "goldens", "fixtures.json"))),
             "corpus": json.load(open(os.path.join(ROOT, "bench", "goldens", "corpus.json")))}
    counts = collections.Counter()
    ex = collections.defaultdict(list)
    planned_cls = {}
    lines = [l.rstrip("\n").split("\t") for l in open(path, encoding="utf-8") if l.startswith("REBLESS-JSON\t")]
    # planned fields first: the physical ones are judged against their twin
    def is_planned(file, key, field):
        if file == "fixtures":
            return key.endswith("_planned")
        return "planned" in field
    lines.sort(key=lambda l: (0 if is_planned(l[1], l[2], l[3]) else 1))
    for _, file, key, field, newj in lines:
        if file not in golds:
            continue
        if field.endswith("_err") or field.endswith("_tier") or field.endswith("tiers_tried"):
            counts["consequence: " + field] += 1
            continue
        plan, _ = plan_of_key(file, key)
        oldv = golden_field(golds[file], key, field)
        old = from_encoded(oldv if isinstance(oldv, str) else json.dumps(oldv))
        new = from_encoded(json.loads(newj) if newj.startswith('"') else newj)
        tag = file + " " + key + " " + field
        if is_planned(file, key, field):
            cls = judge_planned(old, new, plan)
            planned_cls[(file, key.replace("_planned", ""), field.replace("planned", ""))] = cls
        else:
            twin = planned_cls.get((file, key.replace("_physical", ""), field.replace("physical", "")))
            cls = "physical: " + judge_physical(old, new, twin)
        counts[cls] += 1
        if len(ex[cls]) < examples:
            ex[cls].append(tag)
        verdicts.append({"file": file, "key": key, "field": field, "class": cls})
    return counts, ex


# ---- the rules-module interface of docs/research/judge.py (make judge RULES=stage111)

def judge_combo(old, new):
    """one changed sweep combo, both dumps -> {"planned": cls, "physical": cls, "cls": the worst, "fine": bool}"""
    po, pn = from_dump_climb(old["planned"]), from_dump_climb(new["planned"])
    cls = judge_planned(po, pn, new["plan"])
    pcls = judge_physical(from_dump_climb(old["physical"]), from_dump_climb(new["physical"]), cls)
    fine = is_fine(cls) and is_fine(pcls)
    return {"planned": cls, "physical": pcls, "cls": cls if not is_fine(cls) else (pcls if not is_fine(pcls) else cls), "fine": fine}


def judge_rebless_lines(path):
    """the REBLESS-JSON lines of the fixtures and the corpus -> (Counter of classes, examples, findings);
    other goldens' lines (regions) are left to the generic report"""
    verdicts = []
    counts, ex = judge_rebless(path, 4, verdicts)
    findings = sum(v for k, v in counts.items() if not is_fine(k))
    return counts, ex, findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old")
    ap.add_argument("--new")
    ap.add_argument("--rebless")
    ap.add_argument("--json")
    ap.add_argument("--examples", type=int, default=4)
    a = ap.parse_args()
    verdicts = []
    findings = 0
    if a.old and a.new:
        counts, by_group, ex = judge_sweeps(a.old, a.new, a.examples, verdicts)
        print("== sweeps: %d changed combos" % sum(v for k, v in counts.items() if not k.startswith("physical")))
        for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
            print("  %7d  %s" % (v, k))
            for e in ex[k]:
                print("             e.g. %s" % e)
            if not is_fine(k):
                findings += v
        print("  by group:")
        for g in sorted(by_group):
            print("    %-60s %s" % (g, dict(by_group[g])))
    if a.rebless:
        counts, ex = judge_rebless(a.rebless, a.examples, verdicts)
        print("== fixtures and corpus: %d changed fields" % sum(counts.values()))
        for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
            print("  %7d  %s" % (v, k))
            for e in ex[k]:
                print("             e.g. %s" % e)
            if not is_fine(k):
                findings += v
    if a.json:
        json.dump(verdicts, open(a.json, "w"), indent=0)
    print("== findings: %d" % findings)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
