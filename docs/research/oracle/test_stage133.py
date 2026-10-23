"""Stage 133 (GAME_SPEC 9.7, the user): a piece's end stored as the float32
rounding of a crossing of its carrier with another surface's carrier is that
crossing, by provenance (F23: crossings' overlapping circles 19 and 20)."""
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from level_json import build_defs, load_level  # noqa: E402

mp.mp.dps = 50


def test_a_piece_end_is_the_crossing_it_rounds():
    sc = T.scene_from_json(build_defs(load_level("test_levels/crossings")))
    by = {s.id: s for s in sc}
    x = T._piece_end(sc, 20, mp.mpc(1400.0, 587.550048828125))
    assert x != mp.mpc(1400.0, 587.550048828125)
    # on both circles, exactly at the working precision
    for sid in (19, 20):
        assert abs(abs(x - by[sid].seg.c) ** 2 - by[sid].seg.R2) < mp.mpf(10) ** -40
    # a design endpoint (no crossing near it) is itself
    s = by[20].seg
    assert T._piece_end(sc, 20, s.S) == s.S or abs(T._piece_end(sc, 20, s.S) - s.S) < 1e-3


def test_the_spans_read_their_ends_as_the_crossings():
    sc = T.scene_from_json(build_defs(load_level("test_levels/crossings")))
    span = {"start": [1400.0, 587.550048828125], "end": [1400.0, 712.449951171875], "via": [1530.0, 650.0]}
    seg = T.plan_spans_of([span], sc, [(20, "LEFT")])[0]
    assert abs(seg.S - mp.mpc(1400, 587.550048828125)) < 1e-4 and seg.S.imag != 587.550048828125
    assert abs(seg.E - mp.mpc(1400, 712.449951171875)) < 1e-4 and seg.E.imag != 712.449951171875
