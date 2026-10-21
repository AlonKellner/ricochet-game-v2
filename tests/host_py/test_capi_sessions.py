"""Every session script played through the C API from Python equals the golden
(bench/sessions): a host in another language reproduces the game bit for bit.
RC_PYHOST_SESSION_STRIDE=n plays every n-th session (the commit gate: the C++
host on the C API, ctest capi_sessions_*, plays them all; a merge plays all here too)."""
import json
import os

import pytest

import rc_host

SESSIONS = sorted((rc_host.ROOT / "data" / "sessions").glob("*.json"))[:: int(os.environ.get("RC_PYHOST_SESSION_STRIDE", "1"))]


@pytest.mark.parametrize("path", SESSIONS, ids=[p.stem for p in SESSIONS])
def test_session_equals_golden(path):
    golden = (rc_host.ROOT / "bench" / "sessions" / (path.stem + ".jsonl")).read_text().splitlines()
    got = rc_host.replay(json.loads(path.read_text()))
    assert len(got) == len(golden)
    for line, want in zip(got, golden):
        assert line == want, "record %d differs (docs/research/session_judge.py tells where)" % json.loads(want)["i"]
