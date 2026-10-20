#!/usr/bin/env python3
"""Stage 119 (one-shot, before the GUT suite is retired): the solver scenarios
of the GUT tests the ledger marks `capture` (docs/research/gut_ledger.tsv),
recorded as fixtures. Each test runs alone in Godot with the BeamTracer
capture on (RC_CAPTURE, native/gd/gd_tracer.cpp): every trace_pair call's
inputs (the scene defs, player, cursor, plan, flags, the infinity switch) and
its output (rcg/trace_json.hpp). A test that fails records nothing.

    python3 -I tools/capture_gut_traces.py [--jobs 12] [--only <substring>]

Writes bench/gut_traces/<gut file stem>.jsonl, one line per call, keyed
"<gut file>::<test function>"; rcg_tests "gut traces" replays them bit for bit.
"""
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "bench" / "gut_traces"
GODOT = os.environ.get("GODOT_BIN", "godot")


def rows():
    for line in (ROOT / "docs" / "research" / "gut_ledger.tsv").read_text().splitlines():
        if line.startswith("#") or line.startswith("gut_file\t"):
            continue
        f, fn, disp = line.split("\t")[:3]
        if disp == "capture":
            yield f, fn


def run(f, fn):
    with tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False) as tmp:
        cap = tmp.name
    env = dict(os.environ, RC_CAPTURE=cap, RC_CAPTURE_KEY="%s::%s" % (f, fn))
    p = subprocess.run([GODOT, "--headless", "--path", str(ROOT), "-s", "addons/gut/gut_cmdln.gd", "-gconfig=",
                        "-gtest=res://" + f, "-gunit_test_name=" + fn, "-gexit"],
                       env=env, capture_output=True, text=True, timeout=900)
    lines = pathlib.Path(cap).read_text().splitlines() if os.path.exists(cap) else []
    os.unlink(cap)
    log = p.stdout + p.stderr
    passed = p.returncode == 0 and "Passing Tests" in log and "Failing Tests" not in log
    return f, fn, passed, lines, log


def main():
    jobs = int(sys.argv[sys.argv.index("--jobs") + 1]) if "--jobs" in sys.argv else 12
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else ""
    todo = [r for r in rows() if only in r[0] + "::" + r[1]]
    by_file = {}
    bad = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for f, fn, passed, lines, log in ex.map(lambda r: run(*r), todo):
            if not passed or not lines:
                bad.append((f, fn, passed, len(lines), log[-600:]))
                continue
            by_file.setdefault(f, {})[fn] = lines
    OUT.mkdir(parents=True, exist_ok=True)
    for f, tests in by_file.items():
        path = OUT / (pathlib.Path(f).stem + ".jsonl")
        keep = {}
        if path.exists() and only:
            for line in path.read_text().splitlines():
                keep.setdefault(line.split('"key":"', 1)[1].split('"', 1)[0].split("::", 1)[1], []).append(line)
        keep.update(tests)
        path.write_text("".join(l + "\n" for fn in sorted(keep) for l in keep[fn]))
    print("%d tests captured, %d calls" % (sum(len(t) for t in by_file.values()),
                                           sum(len(l) for t in by_file.values() for l in t.values())))
    for f, fn, passed, n, log in bad:
        print("!! %s::%s passed=%s calls=%d\n%s" % (f, fn, passed, n, log))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
