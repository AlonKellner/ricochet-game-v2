"""Every session script played through the C API from Python equals the golden
(bench/sessions): a host in another language reproduces the game bit for bit."""
import json

import pytest

import rc_host

SESSIONS = sorted((rc_host.ROOT / "data" / "sessions").glob("*.json"))


@pytest.mark.parametrize("path", SESSIONS, ids=[p.stem for p in SESSIONS])
def test_session_equals_golden(path):
    golden = (rc_host.ROOT / "bench" / "sessions" / (path.stem + ".jsonl")).read_text().splitlines()
    got = rc_host.replay(json.loads(path.read_text()))
    assert len(got) == len(golden)
    for line, want in zip(got, golden):
        assert line == want, "record %d differs (docs/research/session_judge.py tells where)" % json.loads(want)["i"]
