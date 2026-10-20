"""The draw list through the C API: its flat pools hold the native list
losslessly (each node and call equals the record's draw list, the
DrawRec-format list the golden's sha is taken of), and a shot flies."""
import json
import math
import struct

import rc_host


def f64(v):
    return struct.unpack("<d", bytes.fromhex(v[1:]))[0] if isinstance(v, str) and len(v) == 17 and v[0] == "f" else v


def dec(x):
    return [dec(y) for y in x] if isinstance(x, list) else f64(x)


def as_rec(c):
    """A C API command in the record's (DrawRec's) form."""
    k, p, col = c["kind"], [list(q) for q in c["points"]], [list(q) for q in c["colors"]]
    first = col[0] if col else None
    return {
        "line": lambda: ["line", p[0], p[1], first, c["width"]],
        "arc": lambda: ["arc", p[0], c["radius"], c["start"], c["end"], c["arc_point_count"], first, c["width"]],
        "polygon": lambda: ["polygon", p, col],
        "colored_polygon": lambda: ["colored_polygon", p, first],
        "circle": lambda: ["circle", p[0], c["radius"], first],
        "rect": lambda: ["rect", p[0], p[1], first],
        "polyline": lambda: ["polyline", p, first, c["width"]],
        "multiline": lambda: ["multiline", p, first, c["width"]],
        "dashed_line": lambda: ["dashed_line", p[0], p[1], first, c["width"], c["dash"]],
        "text": lambda: ["text", p[0], c["text"], c["alignment"], c["font_size"], first],
        "triangles": lambda: ["triangles", p, c["indices"], first],
        "polygon2d": lambda: ["polygon2d", p, first, c["rotation"]],
        "label": lambda: ["label", c["text"]],
    }[k]()


def check_level(level, steps):
    game = rc_host.Game(rc_host.Level((rc_host.ROOT / "data" / "levels" / (level + ".json")).read_text()))
    game.advance(0)
    for x, y in steps:
        game.set_cursor(x, y)
        game.advance(0)
        game.event(rc_host.EVENT_PLAN_ADD)
        game.advance(0)
        want = dec(json.loads(game.record(draw_list=True))["draw"])
        got = game.draw_list()
        assert len(got) == len(want)
        for (key, z, pos, mod, cmds), w in zip(got, want):
            assert [key, z, list(pos), list(mod)] == w[:4]
            assert [as_rec(c) for c in cmds] == w[4], key


def test_the_flat_list_is_the_records_list():
    check_level("main", [(1180, 520), (465.25, 430), (977.5, 829)])
    check_level("test_levels/crossings", [(960, 300), (700, 540)])
    check_level("test_levels/hit_effects", [(900, 400), (1200, 700)])


def test_a_shot_flies_and_the_arrow_is_drawn():
    game = rc_host.Game(rc_host.Level((rc_host.ROOT / "data" / "levels" / "main.json").read_text()))
    game.teleport(760, 600)
    game.set_cursor(1180, 520)
    game.advance(0)
    game.event(rc_host.EVENT_FIRE)
    f = game.frame()
    assert f.flying and (f.camera_x, f.camera_y) == (f.arrow_x, f.arrow_y)
    nodes = {json.dumps(n[0]): n for n in game.draw_list()}
    arrow = nodes['["arrow"]']
    assert [c["kind"] for c in arrow[4]] == ["line", "polygon"]
    assert nodes['["path"]'][3][3] == struct.unpack("f", struct.pack("f", 0.25))[0]   # dimmed in flight
    frames = 0
    while game.frame().flying:
        game.advance(1 / 60)
        frames += 1
    assert 0 < frames < 1000
    assert '["arrow"]' not in {json.dumps(n[0]) for n in game.draw_list()}


def test_tessellate_arc_is_godots_polyline():
    pts = rc_host.tessellate_arc(10, 20, 5, 0, math.pi, 5)
    assert len(pts) == 5
    assert abs(pts[0][0] - 15) < 1e-5 and abs(pts[0][1] - 20) < 1e-5
    assert abs(pts[2][0] - 10) < 1e-5 and abs(pts[2][1] - 25) < 1e-5
    assert abs(pts[4][0] - 5) < 1e-5
