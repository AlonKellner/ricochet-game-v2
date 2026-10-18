"""Stage 112 (K10): the rules module of docs/research/judge.py for the
parallel-carrier rule (make judge RULES=stage112).

A changed trace is fine when the new trace is the old one with some events
removed, each removed event a non-applied, off-segment hit of a line carrier
(a phantom crossing of a carrier parallel to the beam's exact line), and
nothing else differs; the aims are the same. Anything else is a finding.
"""
import collections
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FINDINGS = ("NOT-A-REMOVAL", "AIM-DIFFERS", "APPLIED-REMOVED", "ON-SEGMENT-REMOVED", "CIRCLE-REMOVED", "NULL-TRACE")
EVENT_FIELDS = ("kind", "sid", "applied", "rehit", "popped", "teleport", "left", "x", "y")


def same_event(a, b):
    return all(a.get(f) == b.get(f) for f in EVENT_FIELDS)


def line_ids(defs):
    return {d["id"] for d in defs if d.get("line")}


def judge_trace(old, new, lines):
    if old is None or new is None:
        return "NULL-TRACE", []
    eo, en = old.get("events", []), new.get("events", [])
    if eo and en and eo[0].get("beam") != en[0].get("beam"):
        return "AIM-DIFFERS", []
    # the new trace must be a subsequence of the old one: walk both
    i = 0
    removed = []
    for e in eo:
        if i < len(en) and same_event(e, en[i]):
            i += 1
            continue
        removed.append(e)
    if i != len(en):
        return "NOT-A-REMOVAL", removed
    for e in removed:
        if e["kind"] != "hit":
            return "NOT-A-REMOVAL", removed
        if e.get("applied"):
            return "APPLIED-REMOVED", removed
        if e["sid"] not in lines:
            return "CIRCLE-REMOVED", removed
    return ("phantom-parallel-removed" if removed else "identical"), removed


def judge_combo(old, new):
    lines = line_ids(new["defs"])
    out = {}
    worst = "identical"
    for which in ("planned", "physical"):
        cls, removed = judge_trace(old.get(which), new.get(which), lines)
        out[which] = cls + (" x%d" % len(removed) if removed else "")
        if cls in FINDINGS:
            worst = cls
        elif worst == "identical" and cls != "identical":
            worst = cls
    out["cls"] = worst
    out["fine"] = worst not in FINDINGS
    return out


def judge_rebless_lines(path):
    """fixture and corpus fields: counted by file and field; the traces are
    judged through the sweep dumps (the fields carry no old/new event pairs)"""
    counts = collections.Counter()
    for line in open(path, encoding="utf-8"):
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "REBLESS-JSON":
            counts[parts[1] + " " + parts[3].split("/")[-1]] += 1
    return counts, {}, 0
