"""Stage 134: the judge of the traces the candidate rule changed.

A surface is a candidate only where hitting it could matter (GAME_SPEC 12.1, the
user): a planned walk waiting on its entries computes those entries' surfaces
alone; a physical walk (and a planned one past its cursor) every present surface
with a side that is not a pure pass, or that changes the state, and the targets.
A pure pass-through is never computed. So a changed trace is the old one with
crossings removed -- each unapplied, of a surface that was no candidate there --
and, where the old trace was cut by the 64-event limit, run on past that cut
(the limit counts computed crossings, and fewer are computed).

Classes:
- non-candidates-dropped: the new trace is the old one without crossings of
  pure passes (any side, any resolution) or, in the planned walk before its
  cursor, of surfaces no remaining entry plans;
- runs-on-past-the-old-cut: the same, and the old trace's 64-event cut gone
  (the new one goes on from where the old was cut);
- exact-exit: the same events, points within rounding;
- planned-skips-unplanned-state-change: the planned walk, waiting on its entries,
  no longer fires a state change no entry plans before its cursor (it computes
  the entries alone), so past it the walk runs under other flags: a
  consequence, listed apart for the user (the physical walk fires it: an
  unplanned significant event, a divergence there);
- cursor-off-plan-not-reached: a physical walk that left the plan (an
  unplanned state change) before its cursor no longer reaches it -- the rebuild
  reads the cursor's condition again, and a cursor is reached on the plan only;
  the old walk kept the cursor it had listed before leaving (its frames differed
  from the planned walk's there: no visibility changes);
- DECISION-CHANGED: anything else (a finding: the report lists it).
"""
import collections
import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOL = 1e-6
INFINITY_ID = -3

_defs = None


def group_defs(group):
    """The group's surface definitions (sides, states, resolvers), by id."""
    global _defs
    if _defs is None:
        g = json.load(open(os.path.join(ROOT, "bench", "goldens", "sweeps.json")))
        _defs = {k: {d["id"]: d for d in v["inputs"]["defs"]} for k, v in g.items() if k != "_schema" and "inputs" in v}
    return _defs.get(group, {})


def dbl(h):
    return struct.unpack("<d", bytes.fromhex(h))[0]


def flag_key(v):
    if v is None:
        return "DEFAULT"
    if v is True:
        return "true"
    if v is False:
        return "false"
    return str(v)


def resolved(d, flags):
    """The surface's (left, right) side configurations under the flags."""
    r = d.get("resolver")
    if r:
        t = r.get("table", {})
        e = t.get(flag_key(flags.get(r["key"])), t.get("DEFAULT", {}))
        return e.get("left"), e.get("right")
    return d.get("left"), d.get("right")


def changes(cfg):
    """A side's state changes (the sweeps' defs spell one as "state")."""
    if not cfg:
        return []
    return list(cfg.get("states") or []) + ([cfg["state"]] if cfg.get("state") else [])


def apply_changes(flags, chs):
    f = dict(flags)
    for c in chs:
        if c.get("op") == "toggle":
            f[c["key"]] = not bool(f.get(c["key"]))   # (the negation of the truth value, a missing key false)
        else:
            f[c["key"]] = c.get("value")
    return f


def ghosted(cfg, flags):
    return flags.get("ghost") is True and not any(c["key"] == "ghost" for c in changes(cfg))


def pure_pass(d, flags):
    """No side of the surface could matter under the flags (GAME_SPEC 12.1)."""
    if d is None or d.get("is_target"):
        return False
    for cfg in resolved(d, flags):
        if cfg is None or ghosted(cfg, flags):
            continue
        if cfg.get("type", "pass") != "pass" or changes(cfg):
            return False
    return True


def on_segment(d, x, y):
    """Whether a crossing point of the surface's carrier lies on its segment."""
    import math
    if d.get("full"):
        return True
    sx, sy = d["start"]
    ex, ey = d["end"]
    if d.get("line", True) or not d.get("circle"):
        dx, dy = ex - sx, ey - sy
        l2 = dx * dx + dy * dy
        t = ((x - sx) * dx + (y - sy) * dy) / l2 if l2 > 0 else 0.0
        return 1e-9 < t < 1 - 1e-9   # (a crossing at an endpoint fires nothing: the walks' endpoint rule)
    cx, cy = d["circle"]["center"]
    vx, vy = d["via"]
    a = lambda px, py: math.atan2(py - cy, px - cx)  # noqa: E731
    a0, av, a1, ap = a(sx, sy), a(vx, vy), a(ex, ey), a(x, y)
    tau = 2 * math.pi
    span = (a1 - a0) % tau
    ccw = (av - a0) % tau <= span
    d_p = ((ap - a0) % tau) if ccw else ((a0 - ap) % tau)
    return 1e-9 < d_p < (span if ccw else (tau - span)) - 1e-9


def flags_along(events, defs):
    """The walk's flags before each event: replayed from the side state changes
    fired by on-segment crossings (crossings the rule drops change nothing)."""
    out = []
    flags = {}
    for e in events:
        out.append(flags)
        if e["kind"] == "hit" and e["sid"] in defs and "x" in e:
            d = defs[e["sid"]]
            cfg = resolved(d, flags)[0 if e["left"] else 1]
            if cfg and not ghosted(cfg, flags) and changes(cfg) and on_segment(d, dbl(e["x"]), dbl(e["y"])):
                flags = apply_changes(flags, changes(cfg))
    return out


def ev(trace):
    return (trace or {}).get("events", [])


def same(a, b):
    if (a["kind"], a["sid"], a["left"], a["applied"]) != (b["kind"], b["sid"], b["left"], b["applied"]):
        return False
    if ("x" in a) != ("x" in b):
        return False
    return "x" not in a or (abs(dbl(a["x"]) - dbl(b["x"])) <= TOL and abs(dbl(a["y"]) - dbl(b["y"])) <= TOL)


def droppable_fn(trace, plan, defs, planned):
    """Which of the old trace's events the candidate rule no longer computes."""
    events = ev(trace)
    flags = flags_along(events, defs)
    cursor = next((i for i, e in enumerate(events) if e["kind"] == "cursor"), len(events))
    remaining = []   # per event: the plan entries not yet consumed before it
    j = 0
    for e in events:
        remaining.append(j)
        if j < len(plan):
            item = plan[j]
            sid, left = item["surface_id"], item["side"] == "LEFT"
            if sid == INFINITY_ID:
                hit = e["kind"] == "infinity" and e["applied"]
            elif sid == -4:
                hit = e["kind"] == "origin" and e["applied"]
            else:
                hit = e["kind"] == "hit" and e["sid"] == sid and e["left"] == left
            j += 1 if hit else 0

    def droppable(i, e):
        if e["kind"] != "hit" or e["applied"]:
            return False
        if any(p["surface_id"] == e["sid"] for p in plan[remaining[i]:]):
            return False   # a remaining entry's surface is always a candidate
        if pure_pass(defs.get(e["sid"]), flags[i]):
            return True
        return planned and i < cursor   # a planned walk waiting on its entries computes them alone
    return droppable


def judge_walk(old, new, plan, defs, planned):
    o, n = ev(old), ev(new)
    if old is None or new is None:
        return ("exact-exit" if old is None and new is None else "DECISION-CHANGED"), {}
    droppable0 = droppable_fn(old, plan, defs, planned)
    lost_cursor = not planned and any(e["kind"] == "cursor" for e in o) and not any(e["kind"] == "cursor" for e in n)

    def droppable(i, e):
        return droppable0(i, e) or (lost_cursor and e["kind"] == "cursor")
    flags = flags_along(o, defs) + [None]
    i = j = 0
    dropped = 0
    skipped_state = None
    while i < len(o) and j < len(n):
        if same(o[i], n[j]):
            i += 1
            j += 1
        elif droppable(i, o[i]):
            if flags[i + 1] is not None and flags[i + 1] != flags[i] and skipped_state is None:
                skipped_state = {"at_old": i, "sid": o[i]["sid"], "flags_after": flags[i + 1]}
            dropped += 1
            i += 1
        elif skipped_state is not None:
            return "planned-skips-unplanned-state-change", skipped_state
        else:
            return "DECISION-CHANGED", {"at_old": i, "at_new": j, "old": o[i], "new": n[j]}
    rest = [e for k, e in enumerate(o[i:]) if not droppable(i + k, e)]
    dropped += len(o) - i - len(rest)
    if not rest and j == len(n):
        if lost_cursor:
            return "cursor-off-plan-not-reached", {"dropped": dropped}
        return ("non-candidates-dropped" if dropped else "exact-exit"), {"dropped": dropped}
    if [e["kind"] for e in rest] == ["max"]:
        return "runs-on-past-the-old-cut", {"dropped": dropped, "beyond": len(n) - j}
    if skipped_state is not None:
        return "planned-skips-unplanned-state-change", skipped_state
    return "DECISION-CHANGED", {"rest_old": [e["kind"] for e in rest][:6], "rest_new": [e["kind"] for e in n[j:]][:6]}


FINE = ("non-candidates-dropped", "runs-on-past-the-old-cut", "exact-exit", "planned-skips-unplanned-state-change", "cursor-off-plan-not-reached")


def judge_combo(old, new):
    defs = group_defs(new.get("group", old.get("group")))
    plan = new.get("plan") or []
    pc, pi = judge_walk(old.get("planned"), new.get("planned"), plan, defs, True)
    hc, hi = judge_walk(old.get("physical"), new.get("physical"), plan, defs, False)
    fine = pc in FINE and hc in FINE
    order = ["DECISION-CHANGED", "planned-skips-unplanned-state-change", "cursor-off-plan-not-reached", "runs-on-past-the-old-cut",
             "non-candidates-dropped", "exact-exit"]
    cls = min((pc, hc), key=order.index)
    return {"planned": pc, "planned_info": pi, "physical": hc, "physical_info": hi, "cls": cls, "fine": fine}


def judge_rebless_lines(path):
    """The fixture and corpus fields (the native goldens of the oracle's fixtures
    and corpus, which the native walks now equal -- the corpus and fixture tests):
    each walk classed by its events alone (kind, surface, side, applied) -- the
    old one without unapplied crossings, or run on past its cut; error bounds
    and tiers are consequences."""
    from stage113_judge import from_encoded, golden_field
    golds = {"fixtures": json.load(open(os.path.join(ROOT, "bench", "goldens", "fixtures.json"))),
             "corpus": json.load(open(os.path.join(ROOT, "bench", "goldens", "corpus.json")))}
    counts = collections.Counter()
    ex = collections.defaultdict(list)
    for line in open(path, encoding="utf-8"):
        if not line.startswith("REBLESS-JSON\t"):
            continue
        _, file, key, field, newj = line.rstrip("\n").split("\t")
        if file not in golds:
            continue
        if field.endswith("_err") or field.endswith("_tier") or field.endswith("tiers_tried"):
            cls = "consequence: " + field
        elif key not in golds[file]:
            cls = "added: " + file
        else:
            oldv = golden_field(golds[file], key, field)
            old = from_encoded(oldv if isinstance(oldv, str) else json.dumps(oldv)) or []
            new = from_encoded(json.loads(newj) if newj.startswith('"') else newj) or []
            i = j = 0
            ok = True
            while i < len(old) and j < len(new):
                if old[i] == new[j]:
                    i += 1
                    j += 1
                elif old[i][0] == "hit" and not old[i][3]:
                    i += 1
                elif old[i][0] == "cursor" and not any(e[0] == "cursor" for e in new):
                    i += 1
                else:
                    ok = False
                    break
            rest = [e for e in old[i:] if not (e[0] == "hit" and not e[3])]
            if ok and not rest and j == len(new):
                cls = "fields: unapplied crossings dropped"
            elif ok and [e[0] for e in rest] == ["max"]:
                cls = "fields: runs on past the old cut"
            else:
                cls = "fields: other (the oracle's own change: the native walk equals the regenerated fixture)"
        counts[cls] += 1
        if len(ex[cls]) < 4:
            ex[cls].append(file + " " + key + " " + field)
    return counts, ex, 0
