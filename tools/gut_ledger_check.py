#!/usr/bin/env python3
"""Stage 119: the GUT retirement ledger (docs/research/gut_ledger.tsv) holds.

    python3 -I tools/gut_ledger_check.py [--allow-todo]

Every row has a disposition and, where the GUT test went, the native column:
- capture: its scenario is in bench/gut_traces (the row's key on a line);
- port: a native test that exists -- "rc_tests: <TEST_CASE>", "rcg_tests: <TEST_CASE>"
  (the TEST_CASE's name, or its prefix up to a '*'), "session: <data/sessions name>",
  "host_py: <file>::<test>";
- keep: a test of the Godot suite that exists ("<gut file>::<function>");
- covered: its citation (target_or_reason) is non-empty; drop: its reason is.
And every GUT test function in tests/ is either a ledger row (not yet retired)
or a test of the final Godot suite (FINAL).
Exit status 1 with the offending rows listed.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEDGER = ROOT / "docs" / "research" / "gut_ledger.tsv"
# the final Godot suite: its tests need no ledger row
FINAL = {"engines/godot/tests/test_adapter_smoke.gd", "engines/godot/tests/test_session_replay.gd"}


def test_cases():
    names = set()
    for p in list((ROOT / "native" / "tests").rglob("*.cpp")):
        names.update(re.findall(r'TEST_CASE\("((?:[^"\\]|\\.)*)"', p.read_text()))
    return names


def main():
    allow_todo = "--allow-todo" in sys.argv
    cases = test_cases()
    traces = {}
    for p in (ROOT / "bench" / "gut_traces").glob("*.jsonl"):
        traces[p.name] = p.read_text()
    bad = []
    rows = []
    for n, line in enumerate(LEDGER.read_text().splitlines(), 1):
        if line.startswith("#") or line.startswith("gut_file\t") or not line.strip():
            continue
        cols = line.split("\t")
        if len(cols) != 5:
            bad.append("%d: %d columns" % (n, len(cols)))
            continue
        f, fn, disp, why, nat = cols
        rows.append((f, fn, disp, nat))
        where = "%d %s::%s" % (n, f, fn)
        if disp not in ("covered", "port", "capture", "keep", "drop"):
            bad.append("%s: disposition %r" % (where, disp))
        elif nat == "TODO":
            if not allow_todo:
                bad.append("%s: %s not done" % (where, disp))
        elif disp in ("covered", "drop"):
            if not why.strip():
                bad.append("%s: no citation / reason" % where)
        elif disp == "capture":
            text = traces.get(pathlib.Path(f).stem + ".jsonl", "")
            if '"key":"%s::%s"' % (f, fn) not in text:
                bad.append("%s: not in bench/gut_traces" % where)
        elif disp == "port":
            for ref in nat.split(" | "):
                kind, _, name = ref.partition(": ")
                if kind in ("rc_tests", "rcg_tests"):
                    ok = any(c == name or (name.endswith("*") and c.startswith(name[:-1])) for c in cases)
                elif kind == "session":
                    ok = (ROOT / "data" / "sessions" / (name + ".json")).exists()
                elif kind == "host_py":
                    file, _, test = name.partition("::")
                    p = ROOT / "tests" / "host_py" / file
                    ok = p.exists() and ("def %s(" % test) in p.read_text()
                else:
                    ok = False
                if not ok:
                    bad.append("%s: port target %r not found" % (where, ref))
        elif disp == "keep":
            file, _, test = nat.partition("::")
            p = ROOT / file
            if not p.exists() or ("func %s(" % test) not in p.read_text():
                bad.append("%s: keep target %r not found" % (where, nat))
    listed = {(f, fn) for f, fn, _, _ in rows}
    kept = {tuple(nat.split("::", 1)) for _, _, disp, nat in rows if disp == "keep" and "::" in nat}
    for p in sorted(list((ROOT / "tests").rglob("*.gd")) + list((ROOT / "engines" / "godot" / "tests").rglob("*.gd"))):
        rel = str(p.relative_to(ROOT))
        if "host_py" in rel:
            continue
        for fn in re.findall(r"^func (test_\w+)\(", p.read_text(), re.M):
            if (rel, fn) not in listed and (rel, fn) not in kept and rel not in FINAL:
                bad.append("%s::%s: a GUT test the ledger does not account for" % (rel, fn))
    counts = {}
    for _, _, disp, nat in rows:
        counts[disp] = counts.get(disp, 0) + 1
    print("gut ledger: %d rows %s" % (len(rows), " ".join("%s %d" % kv for kv in sorted(counts.items()))))
    for b in bad:
        print("!! " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
