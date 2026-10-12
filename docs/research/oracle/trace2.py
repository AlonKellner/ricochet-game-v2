"""Oracle v2: arbitrary-precision trace semantics for general scenes (Stage 81).

Surfaces are line / arc / full-circle segments with per-side configs:
None or {"type": "pass"} (no effect), "terminal", "reflection", "inversion".
(Portals: Stage 82; projectives: Stage 83/87.)

Rules (TDD_09 "Target architecture" + "Legacy quirks"):
- Every carrier crossing is an event (counts toward MAX_HITS = 32, as do the
  cursor and the final origin/escape event). A trace cut by MAX_HITS ends with
  a "max" event at the next crossing (drawn up to it, no effect).
- PHYSICAL: an effect fires only on a fully blocked hit: on-segment interior,
  or at an endpoint where blocked sides (incl. other segments sharing the
  exact endpoint = junction) cover both sides. Blockage is per hit point.
  terminal -> trace ends; reflection/inversion -> beam map; pass -> nothing.
  Plan bookkeeping by surface id only.
- PLANNED: the next plan entry fires when id AND side match, even off-segment;
  everything else passes (terminals included).
- Beams: reflection in a line: tau' = u^2 conj(tau_h), kappa' = -kappa;
  reflection on a circle carrier = inversion in it; inversion: beam.invert_beam.
- Departure: after applying at h on S, the new beam departs S at h (t = 0).
  The departure point is excluded as an ordinary candidate but re-hit LAST
  (after the full loop), as an ordinary hit.
- Provenance stack: re-applying the surface on top of the stack pops it; the
  earlier beam is restored and resumed at the shared point actually hit.
- Projective order from the current parameter t0; ties: cursor first, then
  surface id; the departure re-hit is last. A cursor exactly at the
  departure point (t = t0) is reached immediately, before everything. The parameter is projective:
  t = inf is the antipode of a circle beam's point p (a root there is a real
  crossing, ordered at the wrap point).
- Provenance (known) points: exact data points a beam passes through -- the
  player (initial beam, t = 0), the cursor (whenever it is a candidate: plan
  complete and matched, the beam is B_K), the departure point of a beam born
  at an exact hit point, every known point of the previous beam lying on the
  carrier of the surface applied (an effect fixes its carrier pointwise), and
  every segment endpoint on an exact line (below). Pops restore the earlier
  beam's known points.
- Exact lines: a straight beam defined by exact data, in implicit form
  a x + b y = c (Fractions) -- the initial beam of a direction trace or of a
  trace without plan (player -> cursor), or of a mirror-only plan (the aim
  image is exact), the reflection of an exact line in a line mirror (an exact
  map of the coefficients), and an exact line inverted in a circle whose
  center it contains exactly (it maps onto itself: the new beam is exactly
  straight).
- Tangency: an exact line exactly tangent to a circle carrier touches it
  without crossing: no event.
- Incidence (exact predicates on the data, Fractions): a surface whose
  carrier contains a known point exactly is hit exactly there: its root
  nearest the point takes the point's parameter and the exact point. No
  rounding decides an endpoint or an incidence.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from fractions import Fraction

import mpmath as mp

from beam import Beam, Circle, circle_hits, cross, dot, invert_beam, invert_point
from geometry import (LEFT, RIGHT, P, Seg, endpoint_blocked_sides, line_hits, on_segment,
                      side_at)

MAX_HITS = 32
PHYSICAL = 0
PLANNED = 1
TRANSFORMATIVE = ("reflection", "inversion")


# ---------------------------------------------------------------- scene

@dataclass
class Surface:
    id: int
    seg: Seg
    left: dict | None
    right: dict | None
    is_target: bool = False

    def effect(self, side):
        cfg = self.left if side == LEFT else self.right
        if cfg is None or cfg.get("type") == "pass":
            return None
        return cfg["type"]


def surface_from_json(d):
    sid = int(d["id"])
    if d.get("line"):
        seg = Seg.line(sid, d["start"], d["end"], full=bool(d.get("full")) and d.get("via") is None)
        if d.get("via") is not None:
            seg.V = P(d["via"])
    elif d.get("circle") is not None:
        c = d["circle"]
        seg = Seg.full_circle(sid, c["center"], c["radius"])
        seg.S, seg.E, seg.V = P(d["start"]), P(d["end"]), P(d["via"])
    else:
        seg = Seg.arc(sid, d["start"], d["end"], d["via"])
    return Surface(sid, seg, d.get("left"), d.get("right"), bool(d.get("is_target")))


def scene_from_json(surfaces):
    return [surface_from_json(d) for d in surfaces]


def circle_of(seg: Seg):
    return Circle(seg.id, seg.c, mp.sqrt(seg.R2))


# ---------------------------------------------------------------- beam maps

def apply_effect(beam: Beam, t, surf: Surface, etype, h=None) -> Beam:
    """The new beam departs from the hit point: h when known exactly by
    provenance (snapped endpoint, departure point), else beam.at(t)."""
    if h is None:
        h = beam.at(t)
    if surf.seg.kind == "line":
        assert etype == "reflection"
        tau_h = beam.tangent_at(t)
        D = surf.seg.E - surf.seg.S
        u2 = D * D / abs(D) ** 2
        tau2 = u2 * mp.conj(tau_h)
        return Beam(h, tau2 / abs(tau2), -beam.kappa)
    circ = circle_of(surf.seg)
    tau_h = beam.tangent_at(t)
    u = h - circ.c
    nu = u / circ.R
    tau2 = -mp.conj(tau_h) * nu * nu
    return Beam(h, tau2 / abs(tau2), -beam.kappa - 2 * cross(tau_h, u) / (circ.R ** 2))


def map_point(z, surf: Surface):
    """The surface's transform applied to a point (None = infinity)."""
    seg = surf.seg
    if seg.kind == "line":
        if z is None:
            return None
        D = seg.E - seg.S
        return seg.S + (D * D / abs(D) ** 2) * mp.conj(z - seg.S)
    if z is None:
        return seg.c
    if z == seg.c:
        return None
    return invert_point(z, circle_of(seg))


def map_pole(state, surf: Surface):
    """(P, Q, delta) with Q None = infinity, delta = P - Q exact."""
    P0, Q0, d0 = state
    seg = surf.seg
    P1 = map_point(P0, surf)
    if seg.kind == "line":
        if Q0 is None:
            return (P1, None, None)
        D = seg.E - seg.S
        d1 = (D * D / abs(D) ** 2) * mp.conj(d0)
        return (P1, map_point(Q0, surf), d1)
    R2 = seg.R2
    if Q0 is None:
        u = P0 - seg.c
        return (P1, seg.c, R2 / mp.conj(u))
    uq = Q0 - seg.c
    if uq == 0:
        return (P1, None, None)
    Q1 = map_point(Q0, surf)
    d1 = -R2 * mp.conj(d0 / ((P0 - seg.c) * uq))
    return (Q1 + d1, Q1, d1)


# ---------------------------------------------------------------- candidates

def roots(beam: Beam, surf: Surface, depart_on: bool):
    seg = surf.seg
    if seg.kind == "line":
        if depart_on:
            # p on the line exactly: the t = 0 root is the departure
            k = beam.kappa
            g = seg.grad(beam.p)
            c1 = dot(g, beam.tau)
            c2 = (k / 2) * cross(beam.tau, g)
            if c2 == 0:
                return [mp.inf] if k != 0 else []
            return [-c1 / c2]
        return line_hits(beam, seg)
    return circle_hits(beam, circle_of(seg), depart_on)


def sort_key(t, t0):
    return (0, t) if t > t0 else (1, t)


@dataclass
class Event:
    kind: str                 # hit | cursor | origin | escape | max
    surface_id: int = -1
    side: str = ""
    applied: bool = False
    terminal: bool = False
    on_segment: bool = False
    point: object = None
    t: object = None
    beam: object = None
    rehit: bool = False


@dataclass
class StackEntry:
    beam: Beam
    sid: int
    t_hit: object
    other: object
    known: list = field(default_factory=list)
    line: object = None


@dataclass
class Walk:
    """Mutable trace state."""
    surfaces: list
    plan: list
    cursor: object
    mode: int
    post_mode: int
    known: list = field(default_factory=list)   # (point, t, skip surface id) on the current beam
    line: object = None                         # exact implicit line (a, b, c) of the current beam, or None
    plan_index: int = 0
    plan_matched: bool = True
    cursor_done: bool = False
    count: int = 0
    events: list = field(default_factory=list)
    stack: list = field(default_factory=list)
    targets: set = field(default_factory=set)
    done: bool = False


# ---------------------------------------------------------------- exact data

def F(x):
    """Exact rational value of a binary float (mpf)."""
    sign, man, exp, _bc = mp.mpf(x)._mpf_
    v = Fraction(int(man)) * (Fraction(2) ** int(exp))
    return -v if sign else v


def FC(z):
    return (F(mp.mpc(z).real), F(mp.mpc(z).imag))


def _cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1])


def incident(k, surf: Surface):
    """Exact: k lies on surf's carrier (k, the carrier data: exact floats)."""
    seg = surf.seg
    if not seg.full and (k == seg.S or k == seg.E):
        return True
    K = FC(k)
    if seg.kind == "line":
        S, E = FC(seg.S), FC(seg.E)
        return _cross(_sub(E, S), _sub(K, S)) == 0
    if seg.full:
        C = FC(seg.c)
        d = _sub(K, C)
        return d[0] * d[0] + d[1] * d[1] == F(seg.R2)
    A, B, C = FC(seg.S), FC(seg.V), FC(seg.E)
    a, b, c = _sub(A, K), _sub(B, K), _sub(C, K)
    la, lb, lc = a[0] ** 2 + a[1] ** 2, b[0] ** 2 + b[1] ** 2, c[0] ** 2 + c[1] ** 2
    return la * _cross(b, c) + lb * _cross(c, a) + lc * _cross(a, b) == 0


def line_from_dir(p, d):
    P, D = FC(p), FC(d)
    a, b = -D[1], D[0]
    return (a, b, a * P[0] + b * P[1])


def on_line(line, k):
    a, b, c = line
    K = FC(k)
    return a * K[0] + b * K[1] == c


def reflect_line_data(line, seg):
    """The implicit line reflected in the line seg: z on L' iff R(z) on L."""
    a, b, c = line
    S, E = FC(seg.S), FC(seg.E)
    M = _sub(E, S)
    mm = M[0] * M[0] + M[1] * M[1]
    m2r, m2i = M[0] * M[0] - M[1] * M[1], 2 * M[0] * M[1]
    a2 = a * m2r + b * m2i
    b2 = a * m2i - b * m2r
    return (a2, b2, c * mm - (a * S[0] + b * S[1]) * mm + a2 * S[0] + b2 * S[1])


def _add_line_endpoints(walk: Walk, beam):
    for s in walk.surfaces:
        if s.seg.full:
            continue
        for ep in (s.seg.S, s.seg.E):
            if not any(k[0] == ep for k in walk.known) and on_line(walk.line, ep):
                walk.known.append((ep, beam.param_of(ep), None))


def _junction_blocked(walk: Walk, surf: Surface, pt, direction):
    """Blocked sides at an exact endpoint, incl. other segments sharing it."""
    bl = br = False
    for s in walk.surfaces:
        for which, ep in ((1, s.seg.S), (2, s.seg.E)):
            if s.seg.full or ep != pt:
                continue
            a, b = endpoint_blocked_sides(direction, s.seg, which)
            bl, br = bl or a, br or b
    return bl, br


def classify(walk: Walk, beam: Beam, t, surf: Surface, point=None):
    pt = beam.at(t) if point is None else point
    tang = beam.tangent_at(t)
    on_seg = on_segment(surf.seg, pt)
    side = side_at(surf.seg, pt, tang)
    at_ep = 0 if surf.seg.full else (1 if pt == surf.seg.S else 2 if pt == surf.seg.E else 0)
    if not on_seg:
        full_block = False
    elif at_ep == 0:
        full_block = True
    else:
        bl, br = _junction_blocked(walk, surf, pt, tang)
        full_block = bl and br
    return pt, on_seg, side, full_block


def physical_decision(walk: Walk, surf: Surface, side, full_block):
    """None (pass) | 'terminal' | effect type."""
    if not full_block:
        return None
    return surf.effect(side)


def walk_from(walk: Walk, beam: Beam, depart, t0=None):
    """Forward walk. depart: surface id the beam departs from (or None).
    t0 None: fresh (beam.p is the departure point, t = 0); else resumed at t0."""
    by_id = {s.id: s for s in walk.surfaces}
    fresh = t0 is None
    if fresh:
        t0 = mp.mpf(0)
    while not walk.done:
        cands = []
        for s in walk.surfaces:
            if walk.line is not None and s.seg.kind != "line" and _tangent(walk.line, s.seg):
                continue        # a tangent touch is not a crossing (exact)
            if fresh:
                rs = roots(beam, s, depart == s.id)
                rs = [t for t in rs if t != 0] if depart == s.id else [t for t in rs]
            else:
                rs = roots(beam, s, False)
                if depart == s.id:
                    rs = [t for t in rs if t != t0]
            for t in rs:
                cands.append((sort_key(t, t0), 1, s.id, t, False))
        if not walk.cursor_done and walk.plan_index >= len(walk.plan) and walk.plan_matched \
                and walk.cursor is not None:
            tc = beam.param_of(walk.cursor)
            # the cursor exactly at the departure point is reached immediately
            dep = beam.p if fresh else beam.at(t0)
            if (fresh and walk.cursor == dep) or (depart is not None and any(
                    k[0] == walk.cursor and k[2] == depart for k in walk.known)):
                tc = t0
            cands.append(((-1, 0) if tc == t0 else sort_key(tc, t0), 0, -1, tc, False))
            if not any(k[0] == walk.cursor for k in walk.known):
                walk.known.append((walk.cursor, tc, None))
        snapped = {}
        for kp, kt, skip in walk.known:
            for s in walk.surfaces:
                if s.id == skip or not incident(kp, s):
                    continue
                idx = [i for i, c in enumerate(cands) if c[1] == 1 and c[2] == s.id]
                if not idx:
                    continue
                best = min(idx, key=lambda i: abs(beam.at(cands[i][3]) - kp))
                cands[best] = (sort_key(kt, t0), 1, s.id, kt, False)
                snapped[(s.id, kt)] = kp
        cands.sort(key=lambda c: (c[0], c[1], c[2]))
        if depart is not None:
            cands.append(((2, 0), 2, depart, t0, True))      # departure re-hit, last
        applied = False
        for _, _, sid, t, rehit in cands:
            walk.count += 1
            if walk.count > MAX_HITS:
                # the trace is cut at the next crossing (drawn up to it, no effect)
                mp_ = beam.p if (rehit and fresh) else snapped.get((sid, t), beam.at(t))
                walk.events.append(Event("max", point=mp_, t=t, beam=beam))
                walk.done = True
                return
            if sid == -1:
                walk.events.append(Event("cursor", point=beam.at(t), t=t, beam=beam))
                walk.cursor_done = True
                walk.mode = walk.post_mode
                continue
            surf = by_id[sid]
            known_pt = beam.p if (rehit and fresh) else (None if rehit else snapped.get((sid, t)))
            pt_exact = known_pt is not None and any(k[0] == known_pt for k in walk.known)
            pt, on_seg, side, full_block = classify(walk, beam, t, surf, point=known_pt)
            if surf.is_target and on_seg:
                walk.targets.add(sid)
            do = None
            if walk.mode == PHYSICAL:
                do = physical_decision(walk, surf, side, full_block)
                if do in TRANSFORMATIVE:
                    _plan_bookkeeping(walk, sid)
            elif walk.plan_index < len(walk.plan) and walk.plan[walk.plan_index] == (sid, side):
                walk.plan_index += 1
                eff = surf.effect(side)
                do = eff if eff in TRANSFORMATIVE else None
            ev = Event("hit", sid, side, do is not None, do == "terminal", on_seg, pt, t, beam, rehit)
            walk.events.append(ev)
            if do == "terminal":
                walk.done = True
                return
            if do in TRANSFORMATIVE:
                beam, depart, t0, fresh = _apply(walk, beam, t, surf, do, depart, fresh, t0, rehit, pt, pt_exact)
                applied = True
                break
        if not applied:
            # nothing fired on the full loop: a line escapes through infinity;
            # a circle closes at its departure point (beam.at(t0)) and repeats
            walk.count += 1
            if walk.count <= MAX_HITS:
                if beam.kappa == 0:
                    walk.events.append(Event("escape"))
                else:
                    walk.events.append(Event("origin", point=beam.p if fresh else beam.at(t0)))
            walk.done = True
            return


def _plan_bookkeeping(walk: Walk, sid):
    if walk.plan_index < len(walk.plan):
        if sid == walk.plan[walk.plan_index][0]:
            walk.plan_index += 1
        else:
            walk.plan_matched = False
    else:
        walk.plan_matched = False


def _other_root(beam, surf: Surface, t, fresh, depart, t0):
    if surf.id == depart:
        return t0
    for r in roots(beam, surf, False):
        if r != t:
            return r
    return t


def _apply(walk: Walk, beam, t, surf: Surface, etype, depart, fresh, t0, rehit, pt, pt_exact):
    if walk.stack and walk.stack[-1].sid == surf.id:
        top = walk.stack.pop()
        if rehit:
            t_res = top.t_hit
        elif fresh and depart == surf.id:
            t_res = top.other
        else:
            dh = abs(top.beam.at(top.t_hit) - pt)
            do = abs(top.beam.at(top.other) - pt)
            t_res = top.t_hit if dh < do else top.other
        walk.known = top.known
        walk.line = top.line
        return top.beam, surf.id, t_res, False
    other = _other_root(beam, surf, t, fresh, depart, t0)
    walk.stack.append(StackEntry(beam, surf.id, t, other, walk.known, walk.line))
    nb = apply_effect(beam, t, surf, etype, pt)
    old_known = walk.known
    walk.known = [(pt, mp.mpf(0), surf.id)] if pt_exact else []
    walk.known += [(k, nb.param_of(k), None) for k, _, _ in old_known if k != pt and incident(k, surf)]
    if walk.line is not None and surf.seg.kind == "line":
        walk.line = reflect_line_data(walk.line, surf.seg)
        _add_line_endpoints(walk, nb)
    elif walk.line is not None and _line_through_center(walk.line, surf.seg):
        nb = Beam(nb.p, nb.tau, mp.mpf(0))
        walk.known = [k for k in walk.known if k[2] == surf.id]
        _add_line_endpoints(walk, nb)
    else:
        walk.line = None
    return nb, surf.id, mp.mpf(0), True


def _center_h(seg):
    if seg.full:
        C = FC(seg.c)
        return C[0], C[1], Fraction(1), F(seg.R2)
    A, B, Cc = FC(seg.S), FC(seg.V), FC(seg.E)
    la, lb, lc = A[0] ** 2 + A[1] ** 2, B[0] ** 2 + B[1] ** 2, Cc[0] ** 2 + Cc[1] ** 2
    w = 2 * (A[0] * (B[1] - Cc[1]) + B[0] * (Cc[1] - A[1]) + Cc[0] * (A[1] - B[1]))
    nx = la * (B[1] - Cc[1]) + lb * (Cc[1] - A[1]) + lc * (A[1] - B[1])
    ny = la * (Cc[0] - B[0]) + lb * (A[0] - Cc[0]) + lc * (B[0] - A[0])
    rr = (A[0] - nx / w) ** 2 + (A[1] - ny / w) ** 2
    return nx, ny, w, rr


def _tangent(line, seg):
    """Exact: the line is tangent to seg's circle."""
    a, b, c = line
    nx, ny, w, rr = _center_h(seg)
    d = (a * nx + b * ny) / w - c
    return d * d == rr * (a * a + b * b)


def _line_through_center(line, seg):
    a, b, c = line
    if seg.full:
        C = FC(seg.c)
        return a * C[0] + b * C[1] == c
    A, B, Cc = FC(seg.S), FC(seg.V), FC(seg.E)
    la, lb, lc = A[0] ** 2 + A[1] ** 2, B[0] ** 2 + B[1] ** 2, Cc[0] ** 2 + Cc[1] ** 2
    w = 2 * (A[0] * (B[1] - Cc[1]) + B[0] * (Cc[1] - A[1]) + Cc[0] * (A[1] - B[1]))
    nx = la * (B[1] - Cc[1]) + lb * (Cc[1] - A[1]) + lc * (A[1] - B[1])
    ny = la * (Cc[0] - B[0]) + lb * (A[0] - Cc[0]) + lc * (B[0] - A[0])
    return a * nx + b * ny == c * w


# ---------------------------------------------------------------- entry points

def trace_physical_direction(surfaces, player, direction, plan=(), cursor=None):
    """Physical trace of an explicit direction (forward shooting reference)."""
    return trace_mode_direction(surfaces, player, direction, plan, cursor, PHYSICAL)


def trace_mode_direction(surfaces, player, direction, plan, cursor, mode, line="direction"):
    """line: the initial beam's exact implicit line; "direction": `direction`
    is exact data; None: not exact."""
    walk = Walk(surfaces, list(plan), cursor, mode, PHYSICAL)
    p0 = P(player)
    b0 = Beam(p0, direction / abs(direction), mp.mpf(0))
    walk.known = [(p0, mp.mpf(0), None)]
    walk.line = line_from_dir(p0, direction) if line == "direction" else line
    if walk.line is not None:
        _add_line_endpoints(walk, b0)
    walk_from(walk, b0, None)
    return walk


def aim_image(surfaces, plan, cursor):
    by_id = {s.id: s for s in surfaces}
    z = P(cursor)
    for sid, _side in reversed(plan):
        z = map_point(z, by_id[sid])
        if z is None:
            return None
    return z


def exact_direction(surfaces, plan, player, cursor):
    img = aim_image(surfaces, plan, cursor)
    if img is None:
        return None
    d = img - P(player)
    return None if d == 0 else d


def initial_line(surfaces, player, cursor, plan):
    """Exact implicit line of the initial beam: player -> F_K^-1(cursor) when
    every frame change of the plan is a line reflection (the image is then an
    exact rational point); None if the plan has an inversion."""
    by_id = {s.id: s for s in surfaces}
    X = FC(P(cursor))
    for sid, side in reversed(list(plan)):
        surf = by_id.get(sid)
        if surf is None or surf.effect(side) not in TRANSFORMATIVE:
            continue
        if surf.seg.kind != "line":
            return None
        S, E = FC(surf.seg.S), FC(surf.seg.E)
        M = _sub(E, S)
        mm = M[0] * M[0] + M[1] * M[1]
        m2r, m2i = M[0] * M[0] - M[1] * M[1], 2 * M[0] * M[1]
        wx, wy = X[0] - S[0], -(X[1] - S[1])
        X = (S[0] + (m2r * wx - m2i * wy) / mm, S[1] + (m2r * wy + m2i * wx) / mm)
    Pp = FC(P(player))
    a, b = -(X[1] - Pp[1]), X[0] - Pp[0]
    return (a, b, a * Pp[0] + b * Pp[1])


def planned(surfaces, player, cursor, plan):
    """Planned trace = forward PLANNED-mode walk of the exact aim direction."""
    d = exact_direction(surfaces, plan, player, cursor)
    if d is None:
        d = P(cursor) - P(player)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PLANNED,
                                line=initial_line(surfaces, player, cursor, plan))


def physical(surfaces, player, cursor, plan):
    d = exact_direction(surfaces, plan, player, cursor)
    if d is None:
        d = P(cursor) - P(player)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PHYSICAL,
                                line=initial_line(surfaces, player, cursor, plan))
