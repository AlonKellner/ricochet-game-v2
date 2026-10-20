"""The C API's failures are statuses with messages, never crashes."""
import ctypes

import pytest

import rc_host
from rc_host import lib

MAIN = (rc_host.ROOT / "data" / "levels" / "main.json").read_text()


def test_version_major():
    assert lib.rc_api_version() >> 16 == rc_host.API_MAJOR


def test_a_level_off_schema_is_refused_with_its_message():
    with pytest.raises(rc_host.RcError) as e:
        rc_host.Level('{"schema": "rc-level/0"}')
    assert e.value.status == rc_host.E_LEVEL
    assert "schema" in str(e.value)


def test_malformed_json_is_a_level_error():
    with pytest.raises(rc_host.RcError) as e:
        rc_host.Level("{")
    assert e.value.status in (rc_host.E_LEVEL, rc_host.E_ARGUMENT)


def test_null_arguments():
    out = ctypes.c_void_p()
    assert lib.rc_level_parse(None, 0, ctypes.byref(out)) == rc_host.E_ARGUMENT
    assert lib.rc_game_create(None, None, ctypes.byref(out)) == rc_host.E_ARGUMENT
    assert lib.rc_game_event(None, rc_host.EVENT_FIRE) == rc_host.E_ARGUMENT
    assert lib.rc_last_error()


def test_bad_calls_on_a_game():
    game = rc_host.Game(rc_host.Level(MAIN))
    for bad in (0, 11, -1):
        with pytest.raises(rc_host.RcError):
            game.event(bad)
    for dt in (-0.5, float("inf"), float("nan")):
        with pytest.raises(rc_host.RcError):
            game.advance(dt)
    with pytest.raises(rc_host.RcError):
        game.record([1, 2])
    # the game is still usable
    game.advance(0)
    assert game.record({"i": 0}).startswith("{")


def test_success_clears_the_last_error():
    with pytest.raises(rc_host.RcError):
        rc_host.Level("{")
    rc_host.Level(MAIN)
    assert lib.rc_last_error() == b""


def test_an_older_hosts_smaller_frame_is_filled_as_far_as_it_reaches():
    game = rc_host.Game(rc_host.Level(MAIN))
    game.teleport(100.0, 200.0)
    f = rc_host.Frame()
    ctypes.memset(ctypes.byref(f), 0x55, ctypes.sizeof(f))
    f.size = rc_host.Frame.facing.offset
    assert lib.rc_game_frame(game._h, ctypes.byref(f)) == rc_host.OK
    assert (f.player_x, f.player_y) == (100.0, 200.0)
    assert f.size == rc_host.Frame.facing.offset
    assert f.flying == 0x55555555   # beyond the caller's size: untouched


def test_a_level_outlives_its_handle():
    level = rc_host.Level(MAIN)
    game = rc_host.Game(level)
    del level
    game.advance(0)
    assert game.frame().bounds_w > 0
