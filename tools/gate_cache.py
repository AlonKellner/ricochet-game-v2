#!/usr/bin/env python3
"""The commit gate's result cache (docs/WORKFLOW.md, "The result cache").

A test whose inputs hash the same as on a run where it passed passes again: the
native tests are deterministic functions of their binary, their command line,
their environment and the repo data they read. So `make gate` reruns only what a
change can affect. A change to the game layer leaves `rc_tests` (the core alone)
byte for byte the same, and its sweeps, regions and goldens are not run again.
`make gates` (a merge, a phase close) never reads the cache: it runs everything.

  gate_cache.py ctest BUILD_DIR [--jobs N] [--regex RE] [--no-cache]
      ctest in BUILD_DIR, skipping the entries that passed with the same key.
      An entry is cacheable when its command is a binary of BUILD_DIR. Its key
      covers that binary and the shared libraries it links from BUILD_DIR, its
      arguments and ENVIRONMENT property, the RC_*/RCG_* variables of this
      process, and DATA (the repo data the tests read). Others (lints, CMake
      scripts) always run.
  gate_cache.py step NAME --inputs PATH... -- COMMAND...
      runs COMMAND unless it passed with the same inputs. A PATH is a repo
      directory or file (tracked content via git, edits and untracked files by
      content) or file:GLOB (untracked build products, by content).
  gate_cache.py clear
"""
import glob
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "native", "build-core", "gate_cache.json")
KEEP = 8  # passing keys kept per entry (a revert, a branch switch back: still cached)
# what the native tests read from the repo (RC_REPO_ROOT)
DATA = ["data", "bench", "tests/data", "docs/research"]


def sha_file(path, memo={}):
    if path not in memo:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        memo[path] = h.hexdigest()
    return memo[path]


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], check=True, capture_output=True, text=True).stdout


def fingerprint(paths):
    """The content of repo paths: the index's blob ids, then every path whose
    working-tree content differs from the index (or is untracked) by content."""
    h = hashlib.sha256()
    repo = [p for p in paths if not p.startswith("file:")]
    if repo:
        h.update(git("ls-files", "-s", "--", *repo).encode())
        changed = set(git("diff", "--name-only", "--", *repo).split("\n"))
        changed |= set(git("ls-files", "-o", "--exclude-standard", "--", *repo).split("\n"))
        for p in sorted(c for c in changed if c):
            full = os.path.join(ROOT, p)
            h.update(f"\n{p}:{sha_file(full) if os.path.isfile(full) else 'deleted'}".encode())
    for spec in sorted(p[5:] for p in paths if p.startswith("file:")):
        for f in sorted(glob.glob(os.path.join(ROOT, spec))):
            h.update(f"\n{os.path.relpath(f, ROOT)}:{sha_file(f)}".encode())
        h.update(f"\n{spec}".encode())
    return h.hexdigest()


def rc_env():
    return sorted((k, v) for k, v in os.environ.items() if k.startswith(("RC_", "RCG_")))


def linked_libs(binary, build):
    """The shared libraries of BUILD_DIR the binary links (the C API's)."""
    try:
        out = subprocess.run(["otool", "-L", binary], capture_output=True, text=True).stdout
    except FileNotFoundError:
        out = subprocess.run(["ldd", binary], capture_output=True, text=True).stdout
    libs = []
    for name in sorted(set(re.findall(r"(lib[\w.+-]+\.(?:dylib|so[\d.]*))", out))):
        for f in glob.glob(os.path.join(build, "**", name), recursive=True):
            libs.append(f)
    return sorted(libs)


def load():
    try:
        with open(CACHE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def keys_of(cache, entry):
    v = cache.get(entry, [])
    return [v] if isinstance(v, str) else v


def passed(cache, entry, key):
    return key in keys_of(cache, entry)


def record(cache, entry, key, ok):
    keys = [k for k in keys_of(cache, entry) if k != key]
    cache[entry] = ([key] + keys)[:KEEP] if ok else keys


def save(cache):
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    tmp = CACHE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(cache, f, indent=0, sort_keys=True)
    os.replace(tmp, CACHE)


def ctest(build, jobs, regex, use_cache):
    build = os.path.abspath(build)
    listing = json.loads(subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"], check=True,
                                        capture_output=True, text=True).stdout)
    data = fingerprint(DATA)
    env = rc_env()
    cache = load()
    keys, run, skipped = {}, [], []
    lib_memo = {}
    for t in listing["tests"]:
        name = t["name"]
        if regex and not re.search(regex, name):
            continue
        cmd = t.get("command") or []
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        if cmd and os.path.isfile(cmd[0]) and os.path.abspath(cmd[0]).startswith(build + os.sep):
            if cmd[0] not in lib_memo:
                lib_memo[cmd[0]] = [(os.path.relpath(l, build), sha_file(l)) for l in linked_libs(cmd[0], build)]
            key = hashlib.sha256(json.dumps([sha_file(cmd[0]), lib_memo[cmd[0]], cmd[1:], props.get("ENVIRONMENT"),
                                             env, data]).encode()).hexdigest()
            keys[name] = key
            if use_cache and passed(cache, "ctest:" + name, key):
                skipped.append(name)
                continue
        run.append(name)
    print(f"gate_cache: {len(run)} ctest entries to run, {len(skipped)} unchanged since they passed"
          + ("" if use_cache else " (cache off)"), flush=True)
    status = 0
    if run:
        junit = os.path.join(build, "Testing", "gate_cache_junit.xml")
        regex_run = "^(" + "|".join(re.escape(n) for n in run) + ")$"
        status = subprocess.run(["ctest", "--test-dir", build, "-j", str(jobs), "--output-on-failure", "-R", regex_run,
                                 "--output-junit", junit]).returncode
        ok = set()
        try:
            text = open(junit).read()
            ok = set(re.findall(r'<testcase name="([^"]+)"[^>]*status="run"', text))
            ok -= set(re.findall(r'<testcase name="([^"]+)"[^>]*status="fail"', text))
        except OSError:
            pass
        cache = load()
        for name in run:
            if name in keys:
                record(cache, "ctest:" + name, keys[name], name in ok)
        save(cache)
    return status


def step(name, inputs, command, use_cache):
    key = hashlib.sha256(json.dumps([fingerprint(inputs), rc_env(), command]).encode()).hexdigest()
    if use_cache and passed(load(), "step:" + name, key):
        print(f"gate_cache: {name} unchanged since it passed", flush=True)
        return 0
    status = subprocess.run(command).returncode
    cache = load()
    record(cache, "step:" + name, key, status == 0)
    save(cache)
    return status


def main(argv):
    use_cache = os.environ.get("GATE_CACHE", "1") != "0"
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "clear":
        if os.path.exists(CACHE):
            os.remove(CACHE)
        return 0
    if argv[0] == "ctest" and len(argv) >= 2:
        jobs, regex, args = os.cpu_count() or 4, None, argv[2:]
        while args:
            if args[0] == "--jobs":
                jobs, args = int(args[1]), args[2:]
            elif args[0] == "--regex":
                regex, args = args[1], args[2:]
            elif args[0] == "--no-cache":
                use_cache, args = False, args[1:]
            else:
                print(f"unknown option {args[0]}")
                return 2
        return ctest(argv[1], jobs, regex, use_cache)
    if argv[0] == "step" and "--" in argv and len(argv) >= 4 and argv[2] == "--inputs":
        sep = argv.index("--")
        return step(argv[1], argv[3:sep], argv[sep + 1:], use_cache)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
