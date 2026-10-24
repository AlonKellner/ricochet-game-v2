#!/usr/bin/env python3
"""The cast-cost program: a `sample` call graph as two tables -- the self time
of the top symbols (sample's own "top of stack" section) and the inclusive
share of named functions (a node is counted once per path: a function that
calls itself is not counted twice).

    python3 -I tools/profile_incl.py <sample.txt> [--top N] [--min PCT] [--pats a,b,c]

The default patterns are the solver's, the region's and the exact layer's hot
spots; --pats replaces them (substrings of the demangled names).
"""
import re
import sys

DEFAULT = [
    "cast_batch", "collect_members", "build_cells", "merge_cells", "union_outline", "fill_of", "refine(",
    "Ladder::solve", "replay_climb", "realize(", "build_chain", "tie_choice", "realization_reversed", "select_chain",
    "solve_physical_into", "walk_from", "candidates(", "gaps(", "snap(", "incident(", "classify(", "emit(", "apply(", "project(",
    "concurrent_ties", "Line::concurrent", "Line::contains", "tangent(", "parallel(", "add_line_endpoints",
    "pole_on(", "pole_tangent(", "pole_after", "set_pole", "start_pole", "start_gc", "gc_through", "pre_q_exact",
    "exact_image_cached", "initial_line", "piece_spans", "plan_effects", "first_firing", "cursor_on_beam", "exact_hits_on_beam",
    "cast_events", "canonical_key", "Flags::", "Surf<rc::Dual>::entry", "present(", "candidate(", "PoleMemo::entry",
    "geom::hits", "horizon", "sort", "BigFloat::", "exact::", "SmallVec", "malloc", "free", "_tlv_get_addr", "operator new",
]


def main():
    args = sys.argv[1:]
    top, min_pct, pats = 25, 0.3, DEFAULT
    path = None
    i = 0
    while i < len(args):
        if args[i] == "--top":
            top = int(args[i + 1]); i += 2
        elif args[i] == "--min":
            min_pct = float(args[i + 1]); i += 2
        elif args[i] == "--pats":
            pats = args[i + 1].split(","); i += 2
        else:
            path = args[i]; i += 1
    if path is None:
        print(__doc__)
        return 2
    lines = open(path, errors="replace").read().split("\n")
    # self time: sample's "Sort by top of stack" section
    print("self time (samples):")
    at = next((k for k, ln in enumerate(lines) if ln.startswith("Sort by top of stack")), None)
    if at is not None:
        shown = 0
        for ln in lines[at + 1:]:
            if ln.startswith("Binary Images") or shown >= top:
                break
            m = re.match(r"^\s+(.+?)\s+\(in ([^)]+)\)\s+(\d+)\s*$", ln)
            if m:
                name = re.sub(r"std::__1::", "", m.group(1))
                print("  %6s  %s" % (m.group(3), name[:140]))
                shown += 1
    # inclusive: the call graph, depth by the count's column
    rx = re.compile(r"^([\s+!:|]*?)(\d+) (.+?)(?:  \(in |$| \+ \d+ )")
    incl = {p: 0 for p in pats}
    stack = []
    roots = []
    started = False
    for ln in lines:
        if ln.startswith("Call graph"):
            started = True
            continue
        if ln.startswith("Total number in stack"):
            break
        if not started:
            continue
        m = rx.match(ln)
        if not m:
            continue
        depth, cnt, name = len(m.group(1)), int(m.group(2)), m.group(3)
        while stack and stack[-1][0] >= depth:
            stack.pop()
        inherited = stack[-1][1] if stack else set()
        here = set(inherited)
        for p in pats:
            if p in name and p not in inherited:
                incl[p] += cnt
                here.add(p)
        stack.append((depth, here))
        if len(stack) == 1:
            roots.append(cnt)
    total = sum(roots) or 1
    print("inclusive (of %d samples):" % total)
    for p, c in sorted(incl.items(), key=lambda x: -x[1]):
        if c and 100.0 * c / total >= min_pct:
            print("  %6d %5.1f%%  %s" % (c, 100.0 * c / total, p))
    return 0


if __name__ == "__main__":
    sys.exit(main())
