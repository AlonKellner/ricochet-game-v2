"""Stage 113 (K14): which canon fields changed in each re-blessed sweep combo.

The judge's dumps (RC_SWEEP_DUMP) carry the events, points and tiers; the
sweep goldens hash the display canon (tools/dump_goldens.gd's pair_canon), which
also carries each step's frame id and error bound. K14 relabels the frame ids
of a pair whose walks fire different projectives, so a combo whose dumps are
identical may still change its hash. This script splits such combos by the
canon fields that differ, from the reference build's canon (the REBLESS lines
of the reference rc_tests run against a goldens copy with blank hashes) and
the current build's (the judge's rebless.raw).

    python -I docs/research/stage113_canon_diff.py \
        --old native/build-ref/judge/old_canon.raw --new native/build-ref/judge/rebless.raw
"""
import argparse
import collections
import json

STEP_FIELDS = ["start", "end", "via", "frame_id", "type", "surface_id", "hit_side", "hit_on_segment", "after_portal", "ghost",
               "is_arc_step", "est_error", "precision_bits", "hit"]


def load(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        if not line.startswith("REBLESS\t"):
            continue
        _, group, _slot, combo, _hash, canon = line.rstrip("\n").split("\t", 5)
        out[(group, int(combo))] = json.loads(canon)
    return out


def steps_of(path_canon):
    head, _, steps = path_canon.partition("steps=")
    return head, [s for s in steps.split("|") if s]


def fields_differing(old, new):
    """The names of the canon fields that differ (per path: P/H, its header, the
    steps' fields; F/R lines by name); 'steps-count' when the step counts differ."""
    diff = set()
    ol, nl = old.split("\n"), new.split("\n")
    for o, n in zip(ol, nl):
        if o == n:
            continue
        tag = o[:2]
        if tag in ("P:", "H:"):
            oh, os_ = steps_of(o)
            nh, ns = steps_of(n)
            if oh != nh:
                diff.add(tag + "header")
            if len(os_) != len(ns):
                diff.add(tag + "steps-count")
            for a, b in zip(os_, ns):
                fa, fb = a.split(","), b.split(",")
                for name, x, y in zip(STEP_FIELDS, fa, fb):
                    if x != y:
                        diff.add(tag + name)
        else:
            diff.add(tag.rstrip(":"))
    if len(ol) != len(nl):
        diff.add("lines")
    return diff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    args = ap.parse_args()
    old, new = load(args.old), load(args.new)
    keys = sorted(k for k in new if k in old)
    classes = collections.Counter()
    by_group = collections.defaultdict(collections.Counter)
    for k in keys:
        d = fields_differing(old[k], new[k])
        names = {x[2:] if x[:2] in ("P:", "H:") else x for x in d}
        cls = "frame-id only" if names == {"frame_id"} else "+".join(sorted(names))
        classes[cls] += 1
        by_group[k[0].split("/[")[0]][cls] += 1
    print("combos in both: %d (new %d, old %d)" % (len(keys), len(new), len(old)))
    for cls, n in classes.most_common():
        print("  %-40s %d" % (cls, n))
    print("by group:")
    for g in sorted(by_group):
        print("  %-40s %s" % (g, dict(by_group[g])))


if __name__ == "__main__":
    main()
