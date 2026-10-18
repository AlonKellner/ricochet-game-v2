"""Regenerate the oracle's fixtures and the scene corpus, and report what changed
semantically (Workflow W3).

  make regen          # run every generator, compare, restore the files as they were
  make regen-apply    # the same, keeping the regenerated files whose content changed
  docs/research/.venv/bin/python docs/research/regen.py [--keep] [--only NAME...] [--all]

Every gen_fixtures_*.py and gen_scene_corpus.py writes in place (bench/fixtures,
bench/scenes_corpus); this script snapshots those directories first, runs the
generators (timed), compares each regenerated file with its snapshot as parsed
JSON (floats as floats, decimal strings as decimals, so a different spelling of
the same value is no change), prints a report, and puts the snapshot back
unless --keep, in which case only the files whose content changed stay
regenerated (a byte-only difference is never committed). --all adds the v1 main
and stress corpus generators (gen_corpus.py, regen_bench_v2.py), normally left
alone. Exit 0: nothing changed; 2: changes (see the report).
"""
import argparse
import decimal
import filecmp
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE = os.path.join(HERE, "oracle")
ROOT = os.path.dirname(os.path.dirname(HERE))
PY = sys.executable
DIRS = ["bench/fixtures", "bench/scenes_corpus"]
LEGACY = ["gen_corpus.py", "regen_bench_v2.py"]


def generators(only, include_legacy):
    gens = sorted(os.path.basename(p) for p in glob.glob(os.path.join(ORACLE, "gen_fixtures_*.py")))
    gens.append("gen_scene_corpus.py")
    if include_legacy:
        gens += LEGACY
    if only:
        gens = [g for g in gens if any(o in g for o in only)]
    return gens


def same_value(a, b):
    if type(a) != type(b):
        if isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool) and not isinstance(b, bool):
            return float(a) == float(b)
        return False
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(same_value(a[k], b[k]) for k in a)
    if isinstance(a, list):
        return len(a) == len(b) and all(same_value(x, y) for x, y in zip(a, b))
    if isinstance(a, str) and a != b:
        try:
            return decimal.Decimal(a) == decimal.Decimal(b)
        except decimal.InvalidOperation:
            return False
    return a == b


def changed_keys(a, b):
    """top-level entries (a fixture's name, a scenario) that differ"""
    if isinstance(a, dict) and isinstance(b, dict):
        if "fixtures" in a and isinstance(a["fixtures"], dict) and "fixtures" in b:
            a, b = a["fixtures"], b["fixtures"]
        elif "cases" in a and isinstance(a["cases"], dict) and "cases" in b:
            a, b = a["cases"], b["cases"]
        out = [k for k in a if k not in b or not same_value(a[k], b[k])]
        out += [k for k in b if k not in a]
        return out
    return ["<file>"]


def compare_dir(snapshot, live, rel):
    """-> (identical, byte_only, changed: {relpath: keys}, added, removed)"""
    s_root, l_root = os.path.join(snapshot, rel), os.path.join(live, rel)
    s_files = {os.path.relpath(p, s_root) for p in glob.glob(os.path.join(s_root, "**", "*.json"), recursive=True)}
    l_files = {os.path.relpath(p, l_root) for p in glob.glob(os.path.join(l_root, "**", "*.json"), recursive=True)}
    identical, byte_only, changed = [], [], {}
    for f in sorted(s_files & l_files):
        sp, lp = os.path.join(s_root, f), os.path.join(l_root, f)
        if filecmp.cmp(sp, lp, shallow=False):
            identical.append(f)
            continue
        a, b = json.load(open(sp)), json.load(open(lp))
        if same_value(a, b):
            byte_only.append(f)
        else:
            changed[f] = changed_keys(a, b)
    return identical, byte_only, changed, sorted(l_files - s_files), sorted(s_files - l_files)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--report")
    args = ap.parse_args()
    snap = tempfile.mkdtemp(prefix="ricochet_regen.")
    for d in DIRS:
        shutil.copytree(os.path.join(ROOT, d), os.path.join(snap, d))
    timings = []
    failed = []
    for g in generators(args.only, args.all):
        t0 = time.time()
        r = subprocess.run([PY, "-I", os.path.join(ORACLE, g)], cwd=ROOT, capture_output=True, text=True)
        timings.append((g, time.time() - t0, r.returncode))
        if r.returncode != 0:
            failed.append((g, r.stderr[-2000:]))
    lines = ["# Regeneration report", ""]
    lines.append("| generator | seconds | status |")
    lines.append("|---|---|---|")
    for g, t, rc in timings:
        lines.append("| %s | %.1f | %s |" % (g, t, "ok" if rc == 0 else "FAILED"))
    lines.append("")
    any_change = False
    keep = []
    for d in DIRS:
        identical, byte_only, changed, added, removed = compare_dir(snap, ROOT, d)
        lines.append("## %s: %d identical, %d byte-only, %d changed, %d added, %d removed" % (d, len(identical), len(byte_only), len(changed), len(added), len(removed)))
        for f in byte_only[:200]:
            lines.append("- byte-only: %s" % f)
        for f, keys in sorted(changed.items()):
            lines.append("- changed: %s (%d): %s" % (f, len(keys), ", ".join(str(k) for k in keys[:12]) + (" ..." if len(keys) > 12 else "")))
            keep.append(os.path.join(d, f))
        for f in added:
            lines.append("- added: %s" % f)
            keep.append(os.path.join(d, f))
        for f in removed:
            lines.append("- removed: %s" % f)
            keep.append(os.path.join(d, f))
        any_change = any_change or bool(changed or added or removed)
        lines.append("")
    for g, err in failed:
        lines.append("## %s failed\n```\n%s\n```\n" % (g, err))
    text = "\n".join(lines)
    print(text)
    if args.report:
        open(args.report, "w").write(text + "\n")
    # restore: everything, or everything but the changed files
    for d in DIRS:
        live, s = os.path.join(ROOT, d), os.path.join(snap, d)
        for p in glob.glob(os.path.join(live, "**", "*.json"), recursive=True):
            rel = os.path.join(d, os.path.relpath(p, live))
            if args.keep and rel in keep:
                continue
            sp = os.path.join(s, os.path.relpath(p, live))
            if os.path.exists(sp):
                shutil.copyfile(sp, p)
            else:
                os.remove(p)
        for p in glob.glob(os.path.join(s, "**", "*.json"), recursive=True):
            rel = os.path.join(d, os.path.relpath(p, s))
            lp = os.path.join(ROOT, rel)
            if not os.path.exists(lp) and not (args.keep and rel in keep):
                shutil.copyfile(p, lp)
    shutil.rmtree(snap)
    if failed:
        return 1
    print("\n%s" % ("changes kept (regenerated files whose content changed)" if args.keep and any_change else "changes listed, files restored" if any_change else "nothing changed"))
    return 2 if any_change else 0


if __name__ == "__main__":
    sys.exit(main())
