"""Apply re-bless lines to the pinned goldens (bench/goldens/*.json).

The native goldens test prints, on request, what a changed pin would become:
  RC_SWEEP_REBLESS=1 rc_tests -tc="goldens: the sweep combos*"
      REBLESS\t<group>\t<hash slot>\t<combo>\t<sha16>\t<canon as a JSON string>
  RC_GOLDEN_REBLESS=1 rc_tests -tc="goldens: every stage fixture*,goldens: the corpus*"
      REBLESS-JSON\t<fixtures|corpus>\t<key>\t<field path>\t<json>
This script applies such lines, which an approved behaviour change produced
(docs/TDD_10_NATIVE_MIGRATION.md, the B-list rule: evidence first, then the
user's approval). It never decides anything itself.

Usage:
  docs/research/.venv/bin/python docs/research/apply_rebless.py <lines file>...
sweeps.json is edited textually (its float spellings are Godot's, which a
re-serialisation would change); fixtures.json and corpus.json round-trip
through json exactly and are re-serialised compactly. Every edit is verified
by re-parsing: only the named fields differ.
"""
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GOLD = os.environ.get("RC_GOLD_DIR") or os.path.join(ROOT, "bench", "goldens")  # RC_GOLD_DIR: a copy, for tests


def read_lines(paths):
    for path in paths:
        for line in open(path, encoding="utf-8"):
            line = line.rstrip("\n")
            if line.startswith("REBLESS\t") or line.startswith("REBLESS-JSON\t"):
                yield line.split("\t")


def apply_sweeps(items):
    """items: {group: {"hashes": {slot: sha16}, "samples": {combo: canon}}}"""
    path = os.path.join(GOLD, "sweeps.json")
    text = open(path, encoding="utf-8").read()
    before = json.loads(text)
    dec = json.JSONDecoder()
    for group, upd in items.items():
        marker = json.dumps(group) + ":{"
        start = text.index(marker) + len(marker) - 1
        obj, end = dec.raw_decode(text, start)
        body = text[start:end]
        # the hashes: a fixed-width string, slots replaced in place
        hk = '"hashes":"'
        h0 = body.index(hk) + len(hk)
        h1 = body.index('"', h0)
        hashes = list(body[h0:h1])
        for slot, sha in upd["hashes"].items():
            assert len(sha) == 16 and slot * 16 + 16 <= len(hashes), (group, slot)
            hashes[slot * 16:slot * 16 + 16] = list(sha)
        body = body[:h0] + "".join(hashes) + body[h1:]
        # the samples: re-serialised (strings only: canonical either way)
        if upd["samples"]:
            samples = dict(obj["samples"])
            for combo, canon in upd["samples"].items():
                if str(combo) in samples:
                    samples[str(combo)] = canon
            sk = '"samples":{'
            s0 = body.index(sk) + len(sk) - 1
            _, s1 = dec.raw_decode(body, s0)
            body = body[:s0] + json.dumps(samples, separators=(",", ":"), ensure_ascii=False) + body[s1:]
        text = text[:start] + body + text[end:]
    after = json.loads(text)
    # verification: only hashes / samples of the named groups changed
    for key in before:
        if key not in items:
            assert before[key] == after[key], key
        else:
            for f in before[key]:
                if f not in ("hashes", "samples"):
                    assert before[key][f] == after[key][f], (key, f)
    open(path, "w", encoding="utf-8").write(text)
    return sum(len(u["hashes"]) for u in items.values())


def apply_json(file, items):
    """items: [(key, path, value)]"""
    path = os.path.join(GOLD, file + ".json")
    text = open(path, encoding="utf-8").read()
    d = json.loads(text)
    assert json.dumps(d, separators=(",", ":"), ensure_ascii=False) == text, "%s does not round-trip" % file
    for key, field_path, value in items:
        node = d[key]
        parts = field_path.split("/")
        for part in parts[:-1]:
            node = node[part]
        node[parts[-1]] = value
    open(path, "w", encoding="utf-8").write(json.dumps(d, separators=(",", ":"), ensure_ascii=False))
    return len(items)


def main(paths):
    sweeps = {}
    others = {}
    for parts in read_lines(paths):
        if parts[0] == "REBLESS":
            _, group, slot, combo, sha, canon = parts[:6]
            g = sweeps.setdefault(group, {"hashes": {}, "samples": {}})
            g["hashes"][int(slot)] = sha
            g["samples"][int(combo)] = json.loads(canon)  # the canon spans lines: a JSON string
        else:
            _, file, key, field_path, value = parts[:5]
            others.setdefault(file, []).append((key, field_path, json.loads(value)))
    n = 0
    if sweeps:
        n += apply_sweeps(sweeps)
        print("sweeps.json: %d hash slots in %d groups" % (n, len(sweeps)))
    for file, items in others.items():
        k = apply_json(file, items)
        n += k
        print("%s.json: %d fields" % (file, k))
    if n == 0:
        print("nothing to apply")


if __name__ == "__main__":
    main(sys.argv[1:])
