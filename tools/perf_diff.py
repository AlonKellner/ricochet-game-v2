#!/usr/bin/env python3
"""Stage 120: two rc_tool bench reports (--json) side by side -- per level and
phase, the mean / p95 / max in ms and their ratio (B / A), and every count that
differs. Timing is evidence, not a gate: machines and load differ.

    python3 -I tools/perf_diff.py <A.json> <B.json> [--min-ms 0.5]
"""
import json
import sys


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    min_ms = float(sys.argv[sys.argv.index("--min-ms") + 1]) if "--min-ms" in sys.argv else 0.5
    if "--min-ms" in sys.argv:
        args.remove(sys.argv[sys.argv.index("--min-ms") + 1])
    a, b = (json.load(open(p))["levels"] for p in args[:2])
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
    main()
