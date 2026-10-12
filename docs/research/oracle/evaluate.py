"""Score strategy prototypes against bench/expected at several precisions.

Usage: docs/research/.venv/bin/python docs/research/oracle/evaluate.py [--bits 53,106,212] [--filter substr]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenarios import mpc, plan_tuples, to_mp_circles
import strategies as S

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
TOL = 0.01


def compare(events, expected):
    """Return (n_matched, max_err_px, first_mismatch_index or None)."""
    worst = 0.0
    # The tail after the beam returns to its departure point (origin/escape) is
    # a Tracer-specific termination detail; compare up to the first such event.
    cut = next((i for i, ex in enumerate(expected) if ex["kind"] in ("origin", "escape")), len(expected))
    expected = expected[:cut]
    for i, ex in enumerate(expected):
        if i >= len(events):
            return i, worst, i
        e = events[i]
        if e.kind != ex["kind"] or (ex["kind"] == "hit" and (
                e.surface_id != ex["surface_id"] or e.side != ex["side"] or e.applied != ex["applied"])):
            return i, worst, i
        if "x" in ex and e.point is not None:
            err = float(abs(e.point - mp.mpc(mp.mpf(ex["x"]), mp.mpf(ex["y"]))))
            worst = max(worst, err)
    return len(expected), worst, None


STRATS = {
    "forward_planned": lambda c, P, C, plan, n: S.forward_planned(c, P, C, plan, n),
    "anchored_planned": lambda c, P, C, plan, n: S.anchored_planned(c, P, C, plan, post_events=n),
    "forward_physical": lambda c, P, C, plan, n: S.forward_physical(c, P, C, plan, n),
    "anchored_physical": lambda c, P, C, plan, n: S.anchored_physical(c, P, C, plan, n),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bits", default="53,106,212")
    ap.add_argument("--filter", default="")
    ap.add_argument("--strats", default=",".join(STRATS))
    ap.add_argument("--out", default=os.path.join(ROOT, "bench", "results", "python_eval.json"))
    args = ap.parse_args()
    mp.mp.dps = 120
    files = sorted(glob.glob(os.path.join(ROOT, "bench", "scenarios", "*.json")))
    files = [f for f in files if args.filter in f]
    results = []
    for f in files:
        scn = json.load(open(f))
        exp = json.load(open(f.replace("scenarios", "expected")))
        for bits in [int(b) for b in args.bits.split(",")]:
            for name in args.strats.split(","):
                key = "physical" if "physical" in name else "planned"
                expected = exp[key]
                with mp.workprec(bits):
                    circles = to_mp_circles(scn)
                    t0 = time.time()
                    try:
                        r = STRATS[name](circles, mpc(scn["player"]), mpc(scn["cursor"]),
                                         plan_tuples(scn), len(expected))
                        err = None
                    except Exception as ex:  # noqa
                        r, err = None, repr(ex)
                    dt = time.time() - t0
                if r is None:
                    n, worst, mis = 0, float("inf"), 0
                else:
                    n, worst, mis = compare(r.events, expected)
                ok = mis is None and worst <= TOL
                results.append(dict(scenario=scn["name"], depth=len(scn["plan"]), bits=bits,
                                    strategy=name, matched=n, total=len(expected),
                                    max_err=worst, mismatch=mis, ok=ok, error=err,
                                    max_sens=exp["max_sens_to_cursor"], seconds=dt))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    json.dump(results, open(args.out, "w"), indent=1)
    # summary: pass rate per (strategy, bits, depth bucket)
    buckets = [(1, 3), (4, 6), (7, 9), (10, 12), (13, 15)]
    print(f"{'strategy':20s} {'bits':>4s} " + " ".join(f"d{a}-{b:<4d}" for a, b in buckets) + "  all")
    for name in args.strats.split(","):
        for bits in [int(b) for b in args.bits.split(",")]:
            row = [r for r in results if r["strategy"] == name and r["bits"] == bits]
            cells = []
            for a, b in buckets:
                rr = [r for r in row if a <= r["depth"] <= b]
                cells.append(f"{sum(r['ok'] for r in rr):3d}/{len(rr):<3d}" if rr else "   -   ")
            print(f"{name:20s} {bits:4d} " + " ".join(cells) + f"  {sum(r['ok'] for r in row)}/{len(row)}")


if __name__ == "__main__":
    main()
