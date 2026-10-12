"""Oracle v2: arbitrary-precision trace semantics for general scenes (Stage 81).

Surfaces are line / arc / full-circle segments with per-side configs:
None or {"type": "pass"} (no effect), "terminal", "reflection", "inversion".
(Portals: Stage 82; projectives: Stage 83/87.)

Rules (TDD_09 "Target architecture" + "Legacy quirks"):
- Every carrier crossing is an event (counts toward MAX_HITS = 32, as do the
  cursor and the final origin/escape event).
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
  surface id; the departure re-hit is last.
"""

from __future__ import annotations

from dataclasses import dataclass, field

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

def apply_effect(beam: Beam, t, surf: Surface, etype) -> Beam:
    if surf.seg.kind == "line":
        assert etype == "reflection"
        h = beam.at(t)
        tau_h = beam.tangent_at(t)
        D = surf.seg.E - surf.seg.S
        u2 = D * D / abs(D) ** 2
        tau2 = u2 * mp.conj(tau_h)
        return Beam(h, tau2 / abs(tau2), -beam.kappa)
    return invert_beam(beam, t, circle_of(surf.seg))


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
            return [] if c2 == 0 else [-c1 / c2]
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


@dataclass
class Walk:
    """Mutable trace state."""
    surfaces: list
    plan: list
    cursor: object
    mode: int
    post_mode: int
    plan_index: int = 0
    plan_matched: bool = True
    cursor_done: bool = False
    count: int = 0
    events: list = field(default_factory=list)
    stack: list = field(default_factory=list)
    targets: set = field(default_factory=set)
    done: bool = False


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
            cands.append((sort_key(tc, t0), 0, -1, tc, False))
        cands.sort(key=lambda c: (c[0], c[1], c[2]))
        if depart is not None:
            cands.append(((2, 0), 2, depart, t0, True))      # departure re-hit, last
        applied = False
        for _, _, sid, t, rehit in cands:
            walk.count += 1
            if walk.count > MAX_HITS:
                walk.events.append(Event("max"))
                walk.done = True
                return
            if sid == -1:
                walk.events.append(Event("cursor", point=beam.at(t), t=t, beam=beam))
                walk.cursor_done = True
                walk.mode = walk.post_mode
                continue
            surf = by_id[sid]
            pt, on_seg, side, full_block = classify(walk, beam, t, surf,
                                                    point=beam.p if (rehit and fresh) else None)
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
                beam, depart, t0, fresh = _apply(walk, beam, t, surf, do, depart, fresh, t0, rehit, pt)
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


def _apply(walk: Walk, beam, t, surf: Surface, etype, depart, fresh, t0, rehit, pt):
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
        return top.beam, surf.id, t_res, False
    other = _other_root(beam, surf, t, fresh, depart, t0)
    walk.stack.append(StackEntry(beam, surf.id, t, other))
    return apply_effect(beam, t, surf, etype), surf.id, mp.mpf(0), True


# ---------------------------------------------------------------- entry points

def trace_physical_direction(surfaces, player, direction, plan=(), cursor=None):
    """Physical trace of an explicit direction (forward shooting reference)."""
    walk = Walk(surfaces, list(plan), cursor, PHYSICAL, PHYSICAL)
    walk_from(walk, Beam(P(player), direction / abs(direction), mp.mpf(0)), None)
    return walk


def trace_mode_direction(surfaces, player, direction, plan, cursor, mode):
    walk = Walk(surfaces, list(plan), cursor, mode, PHYSICAL)
    walk_from(walk, Beam(P(player), direction / abs(direction), mp.mpf(0)), None)
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


def planned(surfaces, player, cursor, plan):
    """Planned trace = forward PLANNED-mode walk of the exact aim direction."""
    d = exact_direction(surfaces, plan, player, cursor)
    if d is None:
        d = P(cursor) - P(player)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PLANNED)


def physical(surfaces, player, cursor, plan):
    d = exact_direction(surfaces, plan, player, cursor)
    if d is None:
        d = P(cursor) - P(player)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PHYSICAL)
