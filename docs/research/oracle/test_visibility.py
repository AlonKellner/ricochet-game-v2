"""Stage 110: the visibility definition (visibility.py, GAME_SPEC 15.1)."""
import json
import os
import sys

import mpmath as mp

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
import visibility as V  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
mp.mp.dps = 50


def _scene(name):
    path = os.path.join(ROOT, "bench", "scenes", name + ".surfaces.json")
    return T.scene_from_json(json.load(open(path))["surfaces"])


def test_b4_a_departure_off_the_planned_segment_is_not_visible():
    # three_mirrors: mirror 4 spans x = 800, y 300..780; the cursor is on its
    # carrier below the segment, where the planned walk reflects and the
    # physical one passes: the walks leave the cursor's point in different frames
    sc = _scene("test_levels__three_mirrors")
    assert V.visible(sc, (960.0, 827.999), (800.0, 800.0), [(4, "LEFT")]) is False


def test_b4_a_departure_on_the_planned_segment_is_visible():
    sc = _scene("test_levels__three_mirrors")
    assert V.visible(sc, (960.0, 827.999), (800.0, 600.0), [(4, "LEFT")]) is True


def test_b4_an_ordinary_cursor_after_the_mirror_is_visible():
    sc = _scene("test_levels__three_mirrors")
    assert V.visible(sc, (960.0, 827.999), (811.8456, 706.308), [(4, "LEFT")]) is True


def test_b5_the_co_carrier_write_leaves_the_cursor_visible():
    # hit_effects: surface 15 writes what the planned 16 writes, on one carrier
    sc = _scene("test_levels__hit_effects")
    assert V.visible(sc, (960.0, 540.0), (1260.0, 324.0), [(16, "RIGHT")]) is True


def test_the_cursor_at_the_player_is_not_classified():
    sc = _scene("test_levels__three_mirrors")
    assert V.visible(sc, (960.0, 827.999), (960.0, 827.999), []) is None
