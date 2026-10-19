"""The shape of every committed fixture (Stage 103): the files the GUT and
ctest replays read. A generator that changes the format, or a hand edit, fails
here before it fails in a confusing place downstream.

Run: docs/research/.venv/bin/pytest -q docs/research/oracle
"""
import glob
import json
import os

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FIXTURES = os.path.join(ROOT, "bench", "fixtures")
INFINITY_ID = -3

# data files that are not trace fixtures (no oracle generator writes them)
NOT_TRACES = {
    "bigfloat_cases.json",   # BigFloat arithmetic cases (precision research, read by precision_microbench.gd)
    "stage80_geometry.json",  # beam/segment intersection cases (gen_fixtures_stage80.py)
    "stage110_visibility.json",  # visibility labels (gen_fixtures_stage110_visibility.py), checked below
}
FIXTURE_KEYS = {"name", "surfaces", "player", "direction", "cursor", "plan", "mode", "expected", "targets"}
EVENT_KINDS = {"hit", "cursor", "origin", "escape", "max", "infinity"}
EVENT_KEYS = {"kind", "surface_id", "side", "applied", "terminal", "on_segment", "rehit"}


def trace_files():
    return sorted(os.path.basename(p) for p in glob.glob(os.path.join(FIXTURES, "*.json"))
                  if os.path.basename(p) not in NOT_TRACES)


def generators_text():
    return {os.path.basename(p): open(p).read() for p in glob.glob(os.path.join(HERE, "gen_fixtures_*.py"))}


def test_every_fixture_file_has_a_generator():
    gens = generators_text()
    for name in sorted(os.listdir(FIXTURES)):
        if not name.endswith(".json") or name == "bigfloat_cases.json":
            continue
        stem = name[:-len(".json")]
        assert any(stem in text for text in gens.values()), "%s: no gen_fixtures_*.py writes it" % name


@pytest.mark.parametrize("name", trace_files())
def test_trace_fixture_schema(name):
    d = json.load(open(os.path.join(FIXTURES, name)))
    assert set(d) == {"dps", "fixtures"}, name
    assert isinstance(d["dps"], int) and d["dps"] >= 50
    names = set()
    for fx in d["fixtures"]:
        label = "%s/%s" % (name, fx.get("name"))
        assert FIXTURE_KEYS <= set(fx), (label, FIXTURE_KEYS - set(fx))
        assert fx["name"] not in names, label + ": duplicate name"
        names.add(fx["name"])
        assert fx["mode"] in ("physical", "planned"), label
        assert fx["direction"] is not None or fx["cursor"] is not None, label + ": a direction or a cursor"
        for p in fx["plan"]:
            assert {"surface_id", "side"} <= set(p), label
            if p["surface_id"] == INFINITY_ID:
                assert p["side"] == "", label + ": the infinity entry has no side"
            else:
                assert p["side"] in ("LEFT", "RIGHT"), label
        for i, e in enumerate(fx["expected"]):
            where = "%s event %d" % (label, i)
            assert EVENT_KEYS <= set(e), (where, EVENT_KEYS - set(e))
            assert e["kind"] in EVENT_KINDS, where
            assert ("x" in e) == ("y" in e), where
            if e["kind"] == "hit":
                assert e["side"] in ("LEFT", "RIGHT") and e["surface_id"] >= 0 and "x" in e, where
            else:
                assert e["side"] == "", where
            if e["kind"] == "infinity":
                assert e["surface_id"] == INFINITY_ID, where
            if e["kind"] in ("escape",):
                assert "x" not in e, where + ": an escape has no point"
            if e["kind"] == "cursor":
                assert "x" in e, where


def test_visibility_labels_schema():
    """Stage 110: cases of {name, scene, player, plan, cursors: [[x, y, visible]]}."""
    d = json.load(open(os.path.join(FIXTURES, "stage110_visibility.json")))
    assert set(d) == {"dps", "cases"}
    assert d["cases"]
    for c in d["cases"]:
        assert set(c) == {"name", "scene", "player", "plan", "cursors"}, c["name"]
        assert os.path.exists(os.path.join(ROOT, "bench", "scenes", c["scene"] + ".surfaces.json")), c["scene"]
        for p in c["plan"]:
            assert {"surface_id", "side"} <= set(p) <= {"surface_id", "side", "click"}, c["name"]
            if "click" in p:  # Stage 113: the fork of a circle projective
                assert len(p["click"]) == 2, c["name"]
        assert c["cursors"] and all(len(x) == 3 and isinstance(x[2], bool) for x in c["cursors"]), c["name"]
