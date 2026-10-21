"""Stage 125 (K11 extended, GAME_SPEC 13.3): with no data point the hit point, a
projective's hit point computed exactly from the exact target -- a line
normal's foot, a directional's crossing of its line carrier -- is exact data
when both its coordinates are binary floats exactly (_exact_float_hit)."""
import os
import sys
from fractions import Fraction

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402

mp.mp.dps = 50


def _line(sid, s, e, cfg):
    return T.surface_from_json({"id": sid, "line": True, "start": list(s), "end": list(e),
                                "via": [(s[0] + e[0]) / 2.0, (s[1] + e[1]) / 2.0], "full": False,
                                "left": cfg, "right": cfg})


NORMAL = {"type": "line_normal_projection", "interactive": True, "back": False}


def test_an_axis_aligned_line_normals_foot_is_exact_data():
    s = _line(1, (750.0, 300.0), (750.0, 780.0), NORMAL)
    X = (Fraction(900), Fraction(1234.5))
    D = T._exact_float_hit(s, "line_normal_projection", NORMAL, X)
    assert D == mp.mpc(750, 1234.5)  # off the segment too: exact all the same


def test_a_foot_between_floats_is_not():
    # the line y = x / 3 through (0, 0) and (3, 1): the foot of (1, 0) is (0.9, 0.3), not floats
    s = _line(1, (0.0, 0.0), (3.0, 1.0), NORMAL)
    assert T._exact_float_hit(s, "line_normal_projection", NORMAL, (Fraction(1), Fraction(0))) is None
    # ... while the foot of (5, 5) is (6, 2) exactly
    assert T._exact_float_hit(s, "line_normal_projection", NORMAL, (Fraction(5), Fraction(5))) == mp.mpc(6, 2)


def test_a_directionals_crossing_of_its_line_is_exact_data_when_floats():
    cfg = {"type": "line_directional_projection", "interactive": True, "back": False, "normal": [1.0, 0.0]}
    s = _line(1, (100.0, 0.0), (300.0, 400.0), cfg)  # x = 100 + y / 2
    # along +x from (0, 100): the carrier at (150, 100)
    assert T._exact_float_hit(s, "line_directional_projection", cfg, (Fraction(0), Fraction(100))) == mp.mpc(150, 100)
    # from (0, 0.1): x = 100.05, not a float
    assert T._exact_float_hit(s, "line_directional_projection", cfg, (Fraction(0), Fraction(1, 10))) is None


def test_a_circle_carrier_is_not_extended():
    cfg = {"type": "circle_normal_projection", "interactive": True, "back": False}
    c = T.surface_from_json({"id": 1, "line": False, "start": [700.0, 0.0], "end": [400.0, -150.0],
                             "via": [400.0, 150.0], "full": True, "circle": {"center": [500.0, 0.0], "radius": 200.0},
                             "left": cfg, "right": cfg})
    assert T._exact_float_hit(c, "circle_normal_projection", cfg, (Fraction(800), Fraction(0))) is None


def test_the_backward_pass_keeps_the_image_exact_through_such_a_hit():
    s = _line(5, (750.0, 300.0), (750.0, 780.0), NORMAL)
    hits = T.projective_hits([s], [(5, "LEFT")], (900.0, 541.25))
    H, line, D = hits[0]
    assert D == mp.mpc(750, 541.25) and H == D and line is not None
