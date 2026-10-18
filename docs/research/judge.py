"""The goldens judge (Workflow W4): the changed sweep combos and golden fields of
the current tree against a reference build, classified for the user's approval.

tools/judge.sh (make judge) produces the inputs:
  --old / --new   the reference and the current build's dumps of the changed
                  sweep combos (lines "DUMP\\t{json}", RC_SWEEP_DUMP)
  --rebless       the REBLESS / REBLESS-JSON lines of the current build (the
                  changed sweep hashes; the changed fixtures, corpus and regions
                  fields), what apply_rebless.py applies after the approval
  --rules NAME    a stage's classifier on top of the generic classes:
                  docs/research/<NAME>_judge.py with judge_combo(old_dump,
                  new_dump) -> {"planned", "physical", "cls", "fine"} and,
                  optionally, judge_rebless_lines(path) -> (classes, examples,
                  findings) for the fixture and corpus fields (stage106: the
                  departure rule; stage111: the realization by the first entry)
  --report PATH   the Markdown report (stdout otherwise)

Generic classes per changed combo, from the dumps alone (no mpmath):
  aim      the first beam of the planned trace: same | reversed (the same
           circle the other way round) | other | none
  first    the first differing event of each trace: its index and the two
           kinds (old -> new), "prefix" when one trace is a prefix of the other
  cursor   whether the cursor event is reached, old -> new
  limbs    the final tiers, old -> new

Exit status: 0 nothing changed; 2 changes without a rule (the report is for the
user); 1 a rule's findings (what the rule does not explain).
"""
import argparse
import collections
import importlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "oracle"))

EVENT_FIELDS = ("kind", "sid", "applied", "rehit", "popped", "teleport", "left", "x", "y")


def dbl(h):
    return struct.unpack("<d", bytes.fromhex(h))[0]


def load(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8"):
        if line.startswith("DUMP\t"):
            d = json.loads(line[5:])
            out[(d["group"], d["combo"])] = d
    return out


def events(trace):
    return (trace or {}).get("events", [])


def same_event(a, b):
    return all(a.get(f) == b.get(f) for f in EVENT_FIELDS)


def first_diff(o, n):
    eo, en = events(o), events(n)
    m = min(len(eo), len(en))
    i = 0
    while i < m and same_event(eo[i], en[i]):
        i += 1
    if i == m and len(eo) == len(en):
        return None
    if i == m:
        return {"at": i, "old": eo[i]["kind"] if i < len(eo) else "-", "new": en[i]["kind"] if i < len(en) else "-", "prefix": True}
    return {"at": i, "old": eo[i]["kind"] + "@" + str(eo[i]["sid"]), "new": en[i]["kind"] + "@" + str(en[i]["sid"]), "prefix": False}


def first_beam(trace):
    for e in events(trace):
        if "beam" in e:
            return [dbl(h) for h in e["beam"]]
    return None


def close(a, b, rel=1e-9):
    return abs(a - b) <= rel * max(1.0, abs(a), abs(b))


def aim_class(o, n):
    bo, bn = first_beam(o), first_beam(n)
    if bo is None or bn is None:
        return "none"
    if all(close(x, y) for x, y in zip(bo, bn)):
        return "same"
    # the same circle the other way round: p equal, tau and kappa negated
    if close(bo[0], bn[0]) and close(bo[1], bn[1]) and close(bo[2], -bn[2]) and close(bo[3], -bn[3]) and close(bo[4], -bn[4]):
        return "reversed"
    return "other"


def cursor_reached(trace):
    return any(e["kind"] == "cursor" for e in events(trace))


def generic(o, n):
    rec = {"aim": aim_class(o.get("planned"), n.get("planned"))}
    for which in ("planned", "physical"):
        to, tn = o.get(which), n.get(which)
        rec[which] = {
            "first": first_diff(to, tn),
            "cursor": [cursor_reached(to), cursor_reached(tn)],
            "limbs": [(to or {}).get("limbs"), (tn or {}).get("limbs")],
        }
    return rec


def load_rules(name):
    mod = importlib.import_module(name + "_judge")
    if hasattr(mod, "mp"):
        mod.mp.mp.dps = 200
    return mod


def read_rebless(path):
    sweeps = collections.Counter()
    fields = collections.defaultdict(collections.Counter)
    if not path or not os.path.exists(path):
        return sweeps, fields
    for line in open(path, encoding="utf-8"):
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "REBLESS":
            sweeps[parts[1].rsplit("/", 1)[0]] += 1
        elif parts[0] == "REBLESS-JSON":
            fields[parts[1]][parts[2] + " " + parts[3]] += 1
    return sweeps, fields


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--rebless")
    ap.add_argument("--rules")
    ap.add_argument("--ref", default="")
    ap.add_argument("--report")
    ap.add_argument("--examples", type=int, default=8)
    args = ap.parse_args()
    old, new = load(args.old), load(args.new)
    keys = sorted(set(old) & set(new))
    missing = sorted(set(old) ^ set(new))
    rules = load_rules(args.rules) if args.rules else None
    records = []
    for key in keys:
        rec = {"group": key[0], "combo": key[1]}
        rec.update(generic(old[key], new[key]))
        if rules is not None:
            rec["rules"] = rules.judge_combo(old[key], new[key])
            rec["cls"] = rec["rules"]["cls"]
            rec["fine"] = rec["rules"]["fine"]
        records.append(rec)
    sweeps, fields = read_rebless(args.rebless)
    n_fields = sum(sum(c.values()) for c in fields.values())
    field_rules = None
    if rules is not None and args.rebless and hasattr(rules, "judge_rebless_lines") and os.path.exists(args.rebless):
        field_rules = rules.judge_rebless_lines(args.rebless)

    out = []
    w = out.append
    w("# Goldens judge report%s" % (" (against %s)" % args.ref if args.ref else ""))
    w("")
    w("- changed sweep combos: %d (dumped and judged: %d, in one dump only: %d)" % (sum(sweeps.values()), len(records), len(missing)))
    w("- changed golden fields: %d (%s)" % (n_fields, ", ".join("%s %d" % (f, sum(c.values())) for f, c in sorted(fields.items())) or "none"))
    w("- rules: %s" % (args.rules or "none (generic classes only: the report is the evidence)"))
    w("")
    if records:
        by_group = collections.defaultdict(collections.Counter)
        for r in records:
            by_group[r["group"].rsplit("/", 1)[0]]["aim " + r["aim"]] += 1
            for which in ("planned", "physical"):
                f = r[which]["first"]
                if f is not None:
                    by_group[r["group"].rsplit("/", 1)[0]][which + " first " + ("prefix" if f["prefix"] else f["old"].split("@")[0] + "->" + f["new"].split("@")[0])] += 1
                c = r[which]["cursor"]
                if c[0] != c[1]:
                    by_group[r["group"].rsplit("/", 1)[0]][which + " cursor " + ("reached" if c[1] else "lost")] += 1
        w("## By group: the generic classes")
        w("")
        w("| group | combos | classes |")
        w("|---|---|---|")
        for g in sorted(by_group):
            c = by_group[g]
            n = sum(1 for r in records if r["group"].rsplit("/", 1)[0] == g)
            w("| %s | %d | %s |" % (g, n, ", ".join("%s %d" % kv for kv in sorted(c.items()))))
        w("")
        aims = collections.Counter(r["aim"] for r in records)
        w("Aim of the planned trace: " + ", ".join("%s %d" % kv for kv in sorted(aims.items())))
        limbs = collections.Counter("%s->%s" % tuple(r["planned"]["limbs"]) for r in records)
        w("Planned tiers (old->new): " + ", ".join("%s %d" % kv for kv in sorted(limbs.items())))
        w("")
        if rules is not None:
            cls = collections.Counter(r["cls"] for r in records)
            w("## The rule's classes (%s)" % args.rules)
            w("")
            w("| class | combos | fine |")
            w("|---|---|---|")
            for c, n in sorted(cls.items(), key=lambda kv: -kv[1]):
                fine = all(r["fine"] for r in records if r["cls"] == c)
                w("| %s | %d | %s |" % (c, n, "yes" if fine else "FINDING"))
            w("")
            bad = [r for r in records if not r["fine"]]
            if bad:
                w("### Findings (%d)" % len(bad))
                for r in bad[:50]:
                    w("- %s combo %d: %s" % (r["group"], r["combo"], json.dumps(r["rules"])[:400]))
                w("")
        w("## Examples")
        w("")
        for r in records[: args.examples]:
            w("- %s combo %d: aim %s; planned %s; physical %s" % (r["group"], r["combo"], r["aim"], json.dumps(r["planned"]), json.dumps(r["physical"])))
        w("")
    if field_rules is not None:
        counts, ex, f_findings = field_rules
        w("## The rule's classes of the fixture and corpus fields (%s)" % args.rules)
        w("")
        w("| class | fields |")
        w("|---|---|")
        for c, n in sorted(counts.items(), key=lambda kv: -kv[1]):
            w("| %s | %d |" % (c, n))
        w("")
        for c in sorted(ex):
            for e in ex[c][:4]:
                w("- %s: e.g. %s" % (c, e))
        w("")
    if fields:
        w("## Changed golden fields")
        w("")
        for f in sorted(fields):
            w("### %s (%d)" % (f, sum(fields[f].values())))
            for k, c in sorted(fields[f].items())[:80]:
                w("- %s%s" % (k, "" if c == 1 else " x%d" % c))
            w("")
    if missing:
        w("## In one dump only (%d)" % len(missing))
        for k in missing[:40]:
            w("- %s combo %d" % k)
        w("")
    text = "\n".join(out)
    if args.report:
        open(args.report, "w", encoding="utf-8").write(text + "\n")
    print(text)
    if not records and n_fields == 0 and not missing:
        print("\nnothing changed")
        return 0
    if rules is not None:
        findings = sum(1 for r in records if not r["fine"]) + (field_rules[2] if field_rules is not None else 0)
        print("\nfindings: %d" % findings)
        return 1 if findings else 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
