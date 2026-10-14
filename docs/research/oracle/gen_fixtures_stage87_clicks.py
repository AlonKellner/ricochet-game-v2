"""Stage 87 fixtures: forks of projective back-propagation settled by the
player's click (trace2.back_propagate). Each fixture records the oracle's
traces and its back-propagated hit points ("hits": plan entry index -> H).

A circle projective forks when the target lies inside the circle: both
candidate hit points have exits that reach it directly. The candidate nearest
the click (default: the segment's midpoint) is taken; a target outside the
circle has one direct candidate, whatever the click.

Usage:
  docs/research/.venv/bin/python docs/research/oracle/gen_fixtures_stage87_clicks.py
Writes bench/fixtures/stage87_click_forks.json.
"""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from gen_fixtures_stage81 import REFL, line, room, s  # noqa: E402
from gen_fixtures_stage85 import circle, fixture  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def proj(kind, back=False, normal=None):
    c = {"type": kind, "interactive": True, "back": back}
    if normal is not None:
        c["normal"] = list(normal)
    return c


def with_hits(f):
    """Add the oracle's back-propagated hit points to a fixture."""
    sc = T.scene_from_json(f["surfaces"])
    plan = [(p["surface_id"], p["side"]) for p in f["plan"]]
    hints = {i: tuple(p["click"]) for i, p in enumerate(f["plan"]) if "click" in p}
    hits = T.projective_hits(sc, plan, f["cursor"], f["flags"] or None, hints)
    f["hits"] = {str(k): [s(v[0].real), s(v[0].imag)] for k, v in hits.items()}
    return f


def hit(f, j):
    return complex(float(f["hits"][str(j)][0]), float(f["hits"][str(j)][1]))


def reaches(f):
    return any(e["kind"] == "cursor" for e in f["expected"])


def main():
    mp.mp.dps = 80
    fx = []
    walls = room(10, -1000, -1000, 1000, 1000)
    player = (100.0, -300.0)
    # --- circle normal, through, target inside: a fork (two direct candidates)
    cn = circle(1, (500.0, 0.0), 200.0, *(proj("circle_normal_projection"),) * 2)
    inside = (520.0, 30.0)
    picks = {}
    for tag, click in (("near", (390.0, -160.0)), ("far", (610.0, 165.0)), ("none", None)):
        entry = (1, "LEFT") if click is None else (1, "LEFT", click)
        for mode in ("planned", "physical"):
            f = with_hits(fixture("cn_inside_click_%s_%s" % (tag, mode), [cn] + walls, player, plan=[entry],
                                  mode=mode, cursor=inside))
            fx.append(f)
        picks[tag] = hit(f, 0)
    assert abs(picks["near"] - picks["far"]) > 100, picks          # the click picks the hit point
    assert reaches(fx[0]), "the near candidate is reached first: the planned path realizes it"
    # the default click is the midpoint (the via point (400, 150) of this circle)
    via = complex(400.0, 150.0)
    assert picks["none"] == min((picks["near"], picks["far"]), key=lambda h: abs(h - via)), picks
    # --- circle normal, target outside: the click still decides -- one
    # candidate's exit reaches the target directly, the other's only through
    # infinity; the click (here near each) is honored either way
    outside = (900.0, 100.0)
    outs = {}
    for tag, click in (("near_direct", (310.0, -50.0)), ("near_wrap", (690.0, 50.0))):
        f = with_hits(fixture("cn_outside_click_%s_planned" % tag, [cn] + walls, player, plan=[(1, "LEFT", click)],
                              mode="planned", cursor=outside))
        fx.append(f)
        outs[tag] = hit(f, 0)
    assert abs(outs["near_direct"] - outs["near_wrap"]) > 100, outs
    # --- circle normal, back, target inside: no direct candidate, the click
    # decides among both
    cnb = circle(1, (500.0, 0.0), 200.0, *(proj("circle_normal_projection", True),) * 2)
    bp = {}
    for tag, click in (("near", (390.0, -160.0)), ("far", (610.0, 165.0))):
        f = with_hits(fixture("cn_back_inside_click_%s_planned" % tag, [cnb] + walls, player,
                              plan=[(1, "LEFT", click)], mode="planned", cursor=inside))
        fx.append(f)
        bp[tag] = hit(f, 0)
    assert abs(bp["near"] - bp["far"]) > 100, bp
    # --- circle directional (normal (1, 0)), through, target inside: a fork
    cd = circle(1, (500.0, 0.0), 200.0, *(proj("circle_directional_projection", False, (1, 0)),) * 2)
    dp = {}
    for tag, click in (("left", (320.0, 40.0)), ("right", (680.0, 40.0))):
        for mode in ("planned", "physical"):
            f = with_hits(fixture("cd_inside_click_%s_%s" % (tag, mode), [cd] + walls, player,
                                  plan=[(1, "LEFT", click)], mode=mode, cursor=(450.0, 40.0)))
            fx.append(f)
        dp[tag] = hit(f, 0)
    assert abs(dp["left"] - dp["right"]) > 100, dp
    # --- line normal: one candidate, the click is irrelevant
    ln = line(1, (300, -400), (300, 400), *(proj("line_normal_projection"),) * 2)
    lp = set()
    for tag, click in (("a", (300.0, -390.0)), ("b", (300.0, 390.0))):
        f = with_hits(fixture("ln_click_%s_planned" % tag, [ln] + walls, player, plan=[(1, "RIGHT", click)],
                              mode="planned", cursor=(600.0, 200.0)))
        fx.append(f)
        lp.add(str(f["hits"]["0"]))
    assert len(lp) == 1, lp
    # --- provenance: two entries on the same circle (the cursor beyond it);
    # the first entry's target is the second's hit point, on the carrier: the
    # candidate at it is excluded, even when clicked
    for tag, click in (("at_target", None), ("clicked_target", "H2")):
        base = fixture("tmp", [cn] + walls, player, plan=[(1, "LEFT"), (1, "RIGHT")], mode="planned",
                       cursor=(900.0, 100.0))
        h2 = complex(*[float(v) for v in with_hits(base)["hits"]["1"]])
        c1 = (h2.real, h2.imag) if click == "H2" else (390.0, -160.0)
        f = with_hits(fixture("cn_twice_%s_planned" % tag, [cn] + walls, player,
                              plan=[(1, "LEFT", c1), (1, "RIGHT")], mode="planned", cursor=(900.0, 100.0)))
        fx.append(f)
        assert abs(hit(f, 0) - hit(f, 1)) > 1, "the first hit is never the second one"
    # --- a fork after a mirror (a chord inside the circle): the target pulled
    # back through the mirror lies inside the circle. The candidate whose exit
    # crosses the mirror before the pulled-back point realizes the plan; the
    # other passes it first -- the click still decides, honestly
    mir = line(2, (520, -500), (520, 500), REFL, REFL)
    mf = {}
    mside = next(sd for sd in ("LEFT", "RIGHT") if reaches(fixture(
        "tmp", [cn, mir] + walls, player, plan=[(1, "LEFT", (310.0, -60.0)), (2, sd)], mode="planned",
        cursor=(440.0, 30.0))))
    for tag, click in (("near", (310.0, -60.0)), ("far", (690.0, 60.0))):
        f = with_hits(fixture("mirror_fork_click_%s_planned" % tag, [cn, mir] + walls, player,
                              plan=[(1, "LEFT", click), (2, mside)], mode="planned", cursor=(440.0, 30.0)))
        fx.append(f)
        mf[tag] = f
    assert abs(hit(mf["near"], 0) - hit(mf["far"], 0)) > 100
    assert reaches(mf["near"]) and not reaches(mf["far"]), "the clicked branch is taken, realizable or not"
    # --- two forks in one plan (concentric circles; the cursor inside the
    # inner one, whose hit point lies inside the outer one): all four click
    # combinations give four different hit pairs
    cn2 = circle(3, (500.0, 0.0), 80.0, *(proj("circle_normal_projection"),) * 2)
    pairs = set()
    for a in ((390.0, -160.0), (610.0, 165.0)):
        for b in ((440.0, -50.0), (560.0, 50.0)):
            f = with_hits(fixture("two_forks_%d_%d_%d_%d_planned" % (a + b), [cn, cn2] + walls, player,
                                  plan=[(1, "LEFT", a), (3, "LEFT", b)], mode="planned", cursor=(520.0, 10.0)))
            fx.append(f)
            pairs.add((str(f["hits"].get("0")), str(f["hits"].get("1"))))
    assert len(pairs) == 4, pairs
    out = os.path.join(ROOT, "bench", "fixtures", "stage87_click_forks.json")
    json.dump({"dps": mp.mp.dps, "fixtures": fx}, open(out, "w"), indent=1)
    for f in fx:
        print(f["name"], {k: [round(float(x), 2) for x in v] for k, v in f["hits"].items()}, "reaches" if reaches(f) else "-")
    print("two-fork pairs:", len(pairs))


if __name__ == "__main__":
    main()
