import json, glob, os, sys
S = sys.argv[1]
A = f"{S}/repo88/bench"; B = f"{S}/repo88_base/bench"

def event_lists(obj, path=""):
    """yield (path, list) for every list of event dicts (dicts with 'kind')"""
    if isinstance(obj, list):
        if obj and all(isinstance(e, dict) and "kind" in e for e in obj):
            yield path, obj; return
        if not obj:
            yield path, obj; return
        for i, e in enumerate(obj):
            key = e.get("name", i) if isinstance(e, dict) else i
            yield from event_lists(e, f"{path}[{key}]")
    elif isinstance(obj, dict):
        for k, v in obj.items():
            yield from event_lists(v, f"{path}.{k}")

def strip_inf(ev): return [e for e in ev if e["kind"] != "infinity"]

def pt(e): return (e.get("x"), e.get("y"))
def classify(base, inf):
    if base == inf: return "identical", 0, 0
    n_inf = sum(1 for e in inf if e["kind"] == "infinity")
    s = strip_inf(inf)
    if s == base: return "pure insertion", n_inf, 0
    # the trace is cut earlier by MAX_HITS: the non-infinity events of the new trace
    # are a prefix of the old ones, and the new `max` sits where the old trace continued
    if s and s[-1]["kind"] == "max" and len(s) <= len(base):
        body = s[:-1]
        if base[:len(body)] == body:
            nxt = base[len(body)] if len(body) < len(base) else None
            last_inf = inf[-2]["kind"] == "infinity" if len(inf) >= 2 else False
            if s[-1].get("surface_id") == -3 and s[-1].get("x") is None:
                return "cut at infinity (max at inf)", n_inf, len(base) - len(s)
            if nxt is not None and pt(s[-1]) == pt(nxt):
                return "cut earlier (max at next crossing)", n_inf, len(base) - len(s)
            return f"cut earlier, max point differs from next ({pt(s[-1])} vs {pt(nxt) if nxt else None})", n_inf, -1
    if len(s) < len(base) and base[:len(s)] == s:
        return "cut at infinity", n_inf, len(base) - len(s)
    return "OTHER", n_inf, -1

report = {}
for corpus, pat in [("fixtures", "fixtures/*.json"), ("scenes_corpus", "scenes_corpus/expected/*.json"),
                    ("expected", "expected/*.json"), ("stress", "stress/expected/*.json")]:
    files = sorted(set(os.path.relpath(p, A) for p in glob.glob(f"{A}/{pat}")) |
                   set(os.path.relpath(p, B) for p in glob.glob(f"{B}/{pat}")))
    tot = {"files": 0, "files_changed": 0, "lists": 0, "identical": 0, "pure insertion": 0, "cut at infinity": 0, "cut at infinity (max at inf)": 0, "cut earlier (max at next crossing)": 0,
           "other": 0, "infinity_events": 0, "events_dropped": 0, "new_files": [], "details": []}
    for f in files:
        pa, pb = f"{A}/{f}", f"{B}/{f}"
        if not os.path.exists(pb):
            tot["new_files"].append(f); continue
        tot["files"] += 1
        a = json.load(open(pa)); b = json.load(open(pb))
        la = dict(event_lists(a)); lb = dict(event_lists(b))
        if set(la) != set(lb):
            tot["details"].append(f"{f}: event-list keys differ {set(la) ^ set(lb)}")
        changed = False
        for k in lb:
            if k not in la: continue
            tot["lists"] += 1
            kind, n_inf, dropped = classify(lb[k], la[k])
            tot["infinity_events"] += n_inf
            if kind == "identical": tot["identical"] += 1; continue
            changed = True
            if kind in tot: tot[kind] += 1
            else: tot["other"] += 1; tot["details"].append(f"{f}{k}: {kind}")
            if dropped > 0:
                tot["events_dropped"] += dropped
                tot["cut_cases"] = tot.get("cut_cases", []) + [f"{f}{k}: {kind}; {dropped} old event(s) now beyond MAX_HITS ({n_inf} infinity events)"]
        if changed: tot["files_changed"] += 1
    report[corpus] = tot

for c, t in report.items():
    print(f"== {c}: files {t['files']} (changed {t['files_changed']}), event lists {t['lists']}: identical {t['identical']}, "
          f"pure insertion {t['pure insertion']}, cut {t['cut at infinity']}, other {t['other']}; "
          f"infinity events {t['infinity_events']}, dropped {t['events_dropped']}; new files {t['new_files']}")
    for d in t["details"]: print("   ", d)
    for d in t.get("cut_cases", []): print("    cut:", d)
json.dump(report, open(f"{S}/infinity_diff.json", "w"), indent=1)
