"""Oracle v2 self-tests (Stage 80).

Run: docs/research/.venv/bin/python -m pytest docs/research/oracle -q
"""
import glob
import json
import os
import sys

import mpmath as mp
import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from beam import Beam                       # noqa: E402
from geometry import (LEFT, RIGHT, P, Seg, endpoint_blocked_sides, line_hits,  # noqa: E402
                      on_segment, seg_hits, side_at)
from level_json import build_defs, level_names, load_level  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
mp.mp.dps = 60


def B(p, tau, k=0):
    tau = P(tau)
    return Beam(P(p), tau / abs(tau), mp.mpf(k))


# ---------------------------------------------------------------- importer round trip

DUMPS = sorted(glob.glob(os.path.join(ROOT, "bench", "scenes", "*.surfaces.json")))


@pytest.mark.parametrize("name", level_names())
def test_level_data_matches_game(name):
    """Stage 115.2: the level data built in Python equals the level dump (the
    game's defs; a dump's zero stands for either sign: Godot's JSON drops it)."""
    game = json.load(open(os.path.join(ROOT, "bench", "scenes", name.replace("/", "__") + ".surfaces.json")))
    ours = build_defs(load_level(name))
    assert len(ours) == len(game["surfaces"])
    for a, b in zip(ours, game["surfaces"]):
        assert a == b, f"{name} surface {b['id']}: {a} != {b}"


def test_dumps_exist():
    assert len(DUMPS) >= 25, "make dumps first"
    assert len(level_names()) == len(DUMPS), "a level dump per level data file"


# ---------------------------------------------------------------- line hits (closed forms)

def test_line_hits_axis_aligned_exact():
    vline = Seg.line(1, (7, -50), (7, 50))
    assert line_hits(B((0, 0), (1, 0)), vline) == [7]
    assert line_hits(B((0, 3), (-1, 0)), vline) == [-7]
    hline = Seg.line(2, (-50, 9), (50, 9))
    assert line_hits(B((4, 1), (0, 1)), hline) == [8]
    assert line_hits(B((4, 1), (1, 0)), hline) == []   # parallel


def test_line_hits_oblique_analytic():
    seg = Seg.line(1, (10, -100), (10, 100))
    b = B((1, 2), (3, 4))
    (t,) = line_hits(b, seg)
    assert abs(t - mp.mpf(15)) < mp.mpf(10) ** -50      # 9 / (3/5)


def test_curved_beam_line_roots_on_both():
    seg = Seg.line(1, (0, 50), (100, 50))
    b = B((10, 0), (1, 1), mp.mpf("0.01"))
    ts = line_hits(b, seg)
    assert len(ts) == 2
    for t in ts:
        assert abs(b.at(t).imag - 50) < mp.mpf(10) ** -45


def test_tangent_line_double_root():
    # beam = circle of radius 10 centered (0,10) through origin heading +x;
    # the vertical line x = 10 touches it at (10,10) (t = 20, finite)
    b = Beam(mp.mpc(0, 0), mp.mpc(1, 0), mp.mpf("0.1"))
    seg = Seg.line(1, (10, -50), (10, 50))
    ts = line_hits(b, seg)
    assert len(ts) >= 1
    for t in ts:
        assert abs(b.at(t) - mp.mpc(10, 10)) < mp.mpf(10) ** -20


def test_seg_hits_dispatch_circle():
    c = Seg.full_circle(1, (0, 0), 5)
    ts = seg_hits(B((-10, 0), (1, 0)), c)
    assert sorted(ts) == [5, 15]


# ---------------------------------------------------------------- on-segment, side, blocked sides

def test_on_segment_endpoints_and_outside():
    seg = Seg.line(1, (0, 0), (10, 0))
    assert on_segment(seg, mp.mpc(0, 0))
    assert on_segment(seg, mp.mpc(10, 0))
    assert on_segment(seg, mp.mpc(5, 0))
    assert not on_segment(seg, mp.mpc(10 + mp.mpf(2) ** -40, 0))
    assert not on_segment(seg, mp.mpc(-mp.mpf(2) ** -40, 0))


def test_on_segment_arc():
    seg = Seg.arc(1, (1, 0), (-1, 0), (0, 1))
    assert on_segment(seg, mp.mpc(0, 1))
    assert on_segment(seg, mp.expj(mp.pi / 4))
    assert not on_segment(seg, mp.expj(-mp.pi / 4))


def test_side_vertical_line_hand_computed():
    # S=(100,0) E=(100,100): grad (-100, 0), winding -10000 < 0; moving +x:
    # approach = 100 > 0 -> RIGHT (Intersection._determine_side rule)
    seg = Seg.line(1, (100, 0), (100, 100))
    assert side_at(seg, mp.mpc(100, 50), mp.mpc(1, 0)) == RIGHT
    assert side_at(seg, mp.mpc(100, 50), mp.mpc(-1, 0)) == LEFT


def test_side_full_circle_entering_is_left():
    seg = Seg.full_circle(1, (0, 0), 5)
    assert side_at(seg, mp.mpc(-5, 0), mp.mpc(1, 0)) == LEFT
    assert side_at(seg, mp.mpc(5, 0), mp.mpc(1, 0)) == RIGHT


def test_endpoint_blocked_sides_line():
    seg = Seg.line(1, (0, 0), (10, 0))
    # grazing the start endpoint perpendicular: tangent into segment (+x)
    assert endpoint_blocked_sides(mp.mpc(0, 1), seg, 1) == [True, False]
    assert endpoint_blocked_sides(mp.mpc(0, -1), seg, 1) == [False, True]
    assert endpoint_blocked_sides(mp.mpc(1, 0), seg, 1) == [True, True]
