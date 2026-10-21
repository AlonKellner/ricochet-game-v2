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
        drawn = game.draw_list()
        # the draw order: by z, the scene's tree order within a z
        assert [n[1] for n in drawn] == sorted(n[1] for n in drawn)
        zero = [n[0][0] for n in drawn if n[1] == 0]
        assert zero[:3] == ["background", "player", "cursor"] and zero[-1] == "surface"
        # the record's order: by z, then the key's encoding
        got = sorted(drawn, key=lambda n: (n[1], json.dumps(n[0], separators=(",", ":"))))
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


def test_draw_changes_sends_only_what_changed():
    game = rc_host.Game(rc_host.Level((rc_host.ROOT / "data" / "levels" / "main.json").read_text()))
    game.teleport(760, 600)
    game.set_cursor(1180, 520)
    game.advance(0)
    first = game.draw_changes()
    assert first and not any(n[2] for n in first), "the first call sends everything"
    again = game.draw_changes()
    assert all(n[2] and not n[7] for n in again), "nothing changed: every node unchanged, no commands"
    assert [n[0] for n in again] == [n[0] for n in first], "the same ids, in the same order"
    game.set_cursor(1180, 560)
    game.advance(0)
    moved = {json.dumps(n[3]): n for n in game.draw_changes()}
    assert not moved['["path"]'][2], "the aim moved: the path is sent"
    assert moved['["cursor"]'][2] and moved['["cursor"]'][5] == (1180, 560), "the cursor's drawing is the same, only its position moved"
    assert all(n[2] for k, n in moved.items() if k.startswith('["surface"')), "the surfaces are unchanged"
    # the full list is unaffected and carries the versions too
    full = {json.dumps(n[0]): n for n in game.draw_list()}
    assert set(full) == set(moved)


def test_an_animated_game_smooths_only_a_late_region():
    """C API 1.3 (animate, budget_ms): a region on time is drawn as the game's;
    a late one (an asynchronous game with no budget, slower than 30 Hz: Stage 124)
    morphs in, then is the game's."""
    import time
    text = (rc_host.ROOT / "data" / "levels" / "main.json").read_text()

    def region(game):
        return [n for n in game.draw_list() if n[0] == ["visibility"]][0][4]

    def ready(game):
        for _ in range(5000):
            game.advance(0.0)
            region(game)  # (a host draws every frame)
            if not game.frame().region_pending:
                return
            time.sleep(0.001)
        raise AssertionError("the region never arrived")

    plain = rc_host.Game(rc_host.Level(text))
    on_time = rc_host.Game(rc_host.Level(text), animate=1)  # synchronous: always on time
    late = rc_host.Game(rc_host.Level(text), animate=1, asynchronous=1, late_ms=0.001)  # no budget: late (1.4: main's fast regions count as slow)
    for g in (plain, on_time, late):
        g.teleport(760.0, 600.0)
        g.set_cursor(1406.0, 550.0)  # 6 px off the mirror at x = 1400, on its interactive side
        g.advance(0.0)
    ready(late)
    for g in (plain, on_time, late):
        region(g)
        g.event(rc_host.EVENT_PLAN_ADD)
        g.advance(0.0)
    assert region(on_time) == region(plain)
    region(late)  # (a host draws every frame: the presenter sees the request)
    late.advance(0.05)  # a 50 ms frame: the region arrives at least 1/30 s after its request
    ready(late)  # its arrival frame drawn: the morph begins
    late.advance(0.016)
    assert region(late) != region(plain)
    late.advance(0.5)
    assert region(late) == region(plain)
