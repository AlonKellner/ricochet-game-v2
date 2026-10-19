"""Stage 80 fixture: beam vs segment geometry ground truth.

Writes bench/fixtures/stage80_geometry.json: beams (exact float64 inputs)
against line / arc / full-circle segments, with the oracle's roots, points,
on-segment flags and sides (40 significant digits).

Usage: docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage80.py
"""
import json
import math
import os
import random
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from beam import Beam                                      # noqa: E402
from geometry import Seg, on_segment, seg_hits, side_at    # noqa: E402
from level_json import full_circle_points                 # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OUT = os.path.join(ROOT, "bench", "fixtures", "stage80_geometry.json")
mp.mp.dps = 60
D = 40


def s(x):
    return mp.nstr(x, D, strip_zeros=False)


def seg_json(kind, sid, **kw):
    return dict(kind=kind, id=sid, **kw)


def make_segments(rng):
    segs = []
    for i in range(12):
        x1, y1 = rng.randint(100, 1800), rng.randint(100, 1000)
        x2, y2 = rng.randint(100, 1800), rng.randint(100, 1000)
        segs.append(seg_json("line", len(segs) + 1, start=[x1, y1], end=[x2, y2],
                             via=[(x1 + x2) / 2.0, (y1 + y2) / 2.0]))
    for i in range(8):
        cx, cy, r = rng.randint(300, 1600), rng.randint(200, 900), rng.randint(30, 200)
        a0 = rng.uniform(0, 2 * math.pi)
        span = rng.uniform(0.5, 5.5)
        pts = [(round(cx + r * math.cos(a)), round(cy + r * math.sin(a)))
               for a in (a0, a0 + span, a0 + span / 2)]
        if len(set(pts)) < 3:
            continue
        segs.append(seg_json("arc", len(segs) + 1, start=list(pts[0]), end=list(pts[1]), via=list(pts[2])))
    for i in range(6):
        cx, cy, r = rng.randint(300, 1600), rng.randint(200, 900), rng.randint(30, 200)
        st, en, vi = full_circle_points(cx, cy, float(r))
        segs.append(seg_json("full_circle", len(segs) + 1, center=[float(cx), float(cy)], radius=float(r),
                             start=list(st), end=list(en), via=list(vi)))
    return segs


def to_seg(j):
    if j["kind"] == "line":
        return Seg.line(j["id"], j["start"], j["end"])
    if j["kind"] == "arc":
        return Seg.arc(j["id"], j["start"], j["end"], j["via"])
    sg = Seg.full_circle(j["id"], j["center"], j["radius"])
    sg.S, sg.E, sg.V = (mp.mpc(*map(mp.mpf, j[k])) for k in ("start", "end", "via"))
    return sg


def main():
    rng = random.Random(80)
    segs = make_segments(rng)
    beams = []
    for i in range(30):
        px, py = rng.uniform(50, 1870), rng.uniform(50, 1030)
        a = rng.uniform(0, 2 * math.pi)
        tx, ty = math.cos(a), math.sin(a)
        k = 0.0 if i % 3 == 0 else rng.choice([-1, 1]) * 10 ** rng.uniform(-4, -2)
        beams.append(dict(p=[px, py], tau=[tx, ty], kappa=k))
    cases = []
    for bi, bj in enumerate(beams):
        b = Beam(mp.mpc(mp.mpf(bj["p"][0]), mp.mpf(bj["p"][1])),
                 mp.mpc(mp.mpf(bj["tau"][0]), mp.mpf(bj["tau"][1])), mp.mpf(bj["kappa"]))
        for sj in segs:
            sg = to_seg(sj)
            roots = sorted(seg_hits(b, sg))
            hits = []
            for t in roots:
                pt = b.at(t)
                hits.append(dict(t=s(t), x=s(pt.real), y=s(pt.imag), on_segment=bool(on_segment(sg, pt)),
                                 side=side_at(sg, pt, b.tangent_at(t))))
            cases.append(dict(beam=bi, seg=sj["id"], hits=hits))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(dict(beams=beams, segments=segs, cases=cases), open(OUT, "w"), indent=1)
    print("cases", len(cases), "with hits", sum(1 for c in cases if c["hits"]))


if __name__ == "__main__":
    main()
