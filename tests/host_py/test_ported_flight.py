"""Stage 119.3: retired GUT tests that need the host's live play (docs/research/gut_ledger_session_units.tsv),
through the C API: the host ignores plan clicks while the arrow flies (rc_api.cpp)."""
import json

import rc_host


def _plan(game):
    return json.loads(game.record())["plan"]


# test_stage22_plan.gd: test_stage22_plan_blocked_during_flight
def test_stage22_plan_blocked_during_flight():
    level = rc_host.Level((rc_host.ROOT / "data" / "levels" / "main.json").read_text())
    game = rc_host.Game(level)
    assert game.frame().gravity_x == 0 and game.frame().gravity_y == 0  # (the GUT's gravity = ZERO: main's own)
    game.set_cursor(1200, 540)
    game.advance(0)
    game.event(rc_host.EVENT_FIRE)
    assert game.frame().flying, "Arrow should be flying"
    plan_before = len(_plan(game))
    game.event(rc_host.EVENT_PLAN_ADD)
    assert game.frame().flying
    assert len(_plan(game)) == plan_before, "Plan should not change during flight"
    # (beyond the GUT, whose cursor is on no surface: on the mirror at x = 1400 too, and after the flight it plans)
    game.set_cursor(1402, 540)
    game.event(rc_host.EVENT_PLAN_ADD)
    assert game.frame().flying
    assert len(_plan(game)) == plan_before
    while game.frame().flying:
        game.advance(rc_host.FRAME)
    game.event(rc_host.EVENT_PLAN_ADD)
    assert len(_plan(game)) == plan_before + 1
