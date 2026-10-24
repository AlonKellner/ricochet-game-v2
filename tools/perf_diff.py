#!/usr/bin/env python3
"""Stage 120: two rc_tool bench reports (or two rc_tool regions reports: per level and plan, with the hashes) (--json) side by side -- per level and
phase, the mean / p95 / max in ms and their ratio (B / A), and every count that
differs. Timing is evidence, not a gate: machines and load differ.

    python3 -I tools/perf_diff.py <A.json> <B.json> [--min-ms 0.5]
"""
import json
import sys


def regions(a, b, min_ms):
    """rc_tool regions reports: per level and plan the median ms, its ratio, casts, climbs, and whether the
    regions' hash is the same (the same regions bit for bit)."""
    print("| level | plan | median A | median B | B/A | casts A->B | climbs A->B | tail A->B | hash |")
    print("|---|---|---|---|---|---|---|---|---|")
    same = differ = 0
    for level in sorted(set(a["levels"]) & set(b["levels"])):
        for plan in a["levels"][level]:
            if plan not in b["levels"][level]:
                continue
            x, y = a["levels"][level][plan], b["levels"][level][plan]
            equal = x["hash"] == y["hash"]
            same, differ = same + equal, differ + (not equal)
            if max(x["median"], y["median"]) < min_ms and equal:
                continue
            ratio = y["median"] / x["median"] if x["median"] > 0 else float("nan")
            tail = lambda r: r["batch_wall_ms"] - r["batch_sum_ms"] / max(1, a["threads"])
            print("| %s | %s | %.2f | %.2f | %.2f | %.0f->%.0f | %.1f->%.1f | %.2f->%.2f | %s |" % (
                level.split("levels/")[-1], plan, x["median"], y["median"], ratio, x["casts"], y["casts"], x["climbs"], y["climbs"],
                tail(x), tail(y), "=" if equal else "DIFFERS"))
    print("\n%d plans' regions identical, %d differ (threads A %s, B %s)" % (same, differ, a["threads"], b["threads"]))
    return 1 if differ else 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    min_ms = float(sys.argv[sys.argv.index("--min-ms") + 1]) if "--min-ms" in sys.argv else 0.5
    if "--min-ms" in sys.argv:
        args.remove(sys.argv[sys.argv.index("--min-ms") + 1])
    ra, rb = (json.load(open(p)) for p in args[:2])
    if ra.get("kind") == "regions":
        return regions(ra, rb, min_ms)
    a, b = ra["levels"], rb["levels"]
    print("| level | phase | mean A | mean B | B/A | p95 A | p95 B | max A | max B | counts |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for level in sorted(set(a) & set(b)):
        for phase in a[level]:
            if phase not in b[level]:
                continue
            x, y = a[level][phase], b[level][phase]
            if max(x["mean"], y["mean"], x["max"], y["max"]) < min_ms:
                continue
            counts = ", ".join("%s %s->%s" % (k, x[k], y[k]) for k in ("previews", "regions", "climbs", "region_climbs") if x[k] != y[k])
            ratio = y["mean"] / x["mean"] if x["mean"] > 0 else float("nan")
            print("| %s | %s | %.3f | %.3f | %.2f | %.3f | %.3f | %.3f | %.3f | %s |" % (
                level.split("levels/")[-1], phase, x["mean"], y["mean"], ratio, x["p95"], y["p95"], x["max"], y["max"], counts or "="))


if __name__ == "__main__":
    sys.exit(main() or 0)
