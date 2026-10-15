"""Oracle v2: arbitrary-precision trace semantics for general scenes (Stage 81).

Surfaces are line / arc / full-circle segments with per-side configs:
None or {"type": "pass"} (no effect), "terminal", "reflection", "inversion",
"portal" {theta, d, pair}, and the projectives {back, normal}:
"line_normal_projection", "circle_normal_projection",
"line_directional_projection", "circle_directional_projection" (Stage 83;
planning through them: Stage 87).

Projectives (PHYSICAL): at the hit h the beam leaves as a straight line from h
along +-axis -- the carrier normal (line), the radius h - c (circle), or the
fixed normal (directional). The sign is exact: "through" exits to the far side
(sign(exit . grad) = sign(dir . grad)), "back" (project_back) returns to the
approach side; an axis tangent to the carrier (axis . grad = 0) exits along
+axis (through) / -axis (back). The transform stack clears; the new beam
departs the surface (departure re-hit as usual). PLANNED mode applies a
projective plan entry like any other (Stage 87), even off-segment; the aim is
found by back-propagation (back_propagate: 13.3).

Portals (Stage 82): surface S with rigid motion T(z) = a z + d, a = unit(cos
theta, sin theta) of the float64 values, paired with S' = T(S) (which carries
T^-1). A hit at h maps the beam onto S': p' = T(h), tau' = a tau_h, kappa'
= kappa; the new beam departs S'. Hitting the pair of the stack's top pops it:
the earlier beam is restored, resumed at T^-1 of the point hit (on S).

Rules (TDD_09 "Target architecture" + "Legacy quirks"):
- Every carrier crossing is an event (counts toward MAX_HITS = 32, as do the
  cursor and the final origin/escape event). A full loop around the beam
  without an effect ends the trace (escape / origin) -- unless it changed the
  state: then the next loop differs and is walked too. A trace cut by MAX_HITS ends with
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
  player (initial beam, t = 0), the cursor on the initial beam when every
  frame change of the plan fixes it (it lies on each planned carrier: the aim
  image is the cursor itself), the cursor (whenever it is a candidate: plan
  complete and matched, the beam is B_K), the departure point of a beam born
  at an exact hit point, every known point of the previous beam lying on the
  carrier of the surface applied (an effect fixes its carrier pointwise), and
  every segment data point (endpoints, via) on an exact line (below). Pops restore the earlier
  beam's known points.
- Exact lines: a straight beam defined by exact data, in implicit form
  a x + b y = c (Fractions) -- the initial beam of a direction trace or of a
  trace without plan (player -> cursor), or of a plan of line reflections,
  inversions and translations (the aim image is an exact rational point),
  the reflection of an exact line in a line mirror (an exact
  map of the coefficients), and an exact line inverted in a circle whose
  center it contains exactly (it maps onto itself: the new beam is exactly
  straight). Any beam through a known point that is exactly the inversion
  center inverts to an exactly straight beam.
- Tangency: an exact line exactly tangent to a circle carrier touches it
  without crossing: no event.
- Concurrency: on an exact line, adjacent crossings of two line carriers that
  meet the line at the same point share one parameter (the id rule orders
  them).
- Incidence (exact predicates on the data, Fractions): a surface whose
  carrier contains a known point exactly is hit exactly there: its root
  nearest the point takes the point's parameter and the exact point. No
  rounding decides an endpoint or an incidence.

State flags (Stage 85, GAME_SPEC 9.4 / 10.6 / 13.7):
- A trace carries flags (key -> value), starting from the game state's.
- A surface's side config is resolved under the current flags: fixed
  (left / right), or categorical ("resolver": {key, table}): the table entry
  of flag_key(flags[key]); a missing key or value falls back to the "DEFAULT"
  entry, then to the first entry. Categorical sides are pass, terminal,
  reflection or inversion (portals and projectives are fixed).
- A side config may carry a state change {"key", "op": "set" | "toggle",
  "value"}; toggle stores the negation of the current truth value (missing
  key = false).
- The state change fires when the side fires: PHYSICAL -- a fully blocked
  hit (the same rule as effects); PLANNED -- the consumed plan entry. It is
  applied after the event's own config was resolved (last writer wins along
  the trace). PHYSICAL plan bookkeeping counts a hit that fires a state change.
- Planning simulates the plan forward: entry j resolves under the flags left
  by entries < j (each consumed entry applies its state change); an entry
  resolving to a non-transformative effect is consumed without a frame change.
- Pops restore beams, not flags: re-applying the surface on top of the stack
  pops it only when the same effect fires again (an involution undone);
  flags are never restored by a pop.

Infinity (Stage 88): a plan entry INFINITY_ID (the player clicked the screen
edge) means "the beam wraps through infinity here". Every plan has two
realizations: the aim toward the aim image X_0 (the direct one), and the aim
away from it, reaching X_0 through infinity. An infinity entry in the plan's
first epoch (before its first projective) selects the second; in a later
epoch the projective's exit already fixes the orientation, so the entry is
only matched. With EMIT_INFINITY, a straight beam passing t = inf (the visual
wrap: out through one screen edge, back in through the opposite one) emits an
"infinity" event, ordered between the crossings ahead (t > t0) and the wrapped
ones (t <= t0), counted toward MAX_HITS like a pass; a trace cut by MAX_HITS
there ends with a "max" event at infinity (no point). The event is applied
(consumes the entry) when the next plan entry is the infinity entry, in both
modes; otherwise it passes. Circle beams never emit it (their t = inf is the
finite antipode).

Ghost mode (Stage 86): the reserved flag "ghost". While it is true every side
resolves to a pass -- mirrors, inversions, portals, projectives, terminals and
room walls alike, their state changes included -- except ghost togglers (a
side whose state change writes "ghost"), which still fire: a ghost arrow
crosses everything, can leave through a wall, wrap through infinity and
materialize at the next toggler (or at the same one, one loop later). Ghost is
per shot: every trace starts with it false (the game never keeps it).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from fractions import Fraction

import mpmath as mp

from beam import Beam, Circle, circle_hits, cross, dot, invert_beam, invert_point
from geometry import (LEFT, RIGHT, P, Seg, endpoint_blocked_sides, line_hits, on_segment,
                      side_at, winding)

MAX_HITS = 32
PHYSICAL = 0
PLANNED = 1
TRANSFORMATIVE = ("reflection", "inversion", "portal")
PROJECTIVE = ("line_normal_projection", "circle_normal_projection",
              "line_directional_projection", "circle_directional_projection")
INFINITY_ID = -3        # plan entry / event id of the point at infinity (Stage 88)
# Stage 88: emit the "infinity" event at the wrap of a straight beam. Off until
# Stage 88b regenerates the fixtures (the shipped solver does not emit it yet);
# gen_fixtures_stage88 turns it on.
EMIT_INFINITY = False


# ---------------------------------------------------------------- scene

GHOST = "ghost"


def is_ghost_toggler(cfg):
    """A side config whose state change writes the ghost flag."""
    return cfg is not None and (cfg.get("state") or {}).get("key") == GHOST


def flag_key(v):
    """Canonical table key of a flag value (JSON spelling)."""
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return str(v)


def apply_state(flags, change):
    """flags after a state change (a new dict; the input is not modified)."""
    out = dict(flags)
    if change["op"] == "toggle":
        out[change["key"]] = not bool(flags.get(change["key"], False))
    else:
        out[change["key"]] = change["value"]
    return out


@dataclass
class Surface:
    id: int
    seg: Seg
    left: dict | None
    right: dict | None
    is_target: bool = False
    resolver: dict | None = None

    def cfg(self, side, flags=None):
        """The side config under the flags (ghost: everything but a ghost
        toggler is a pass)."""
        c = self._base_cfg(side, flags)
        if (flags or {}).get(GHOST) is True and not is_ghost_toggler(c):
            return None
        return c

    def _base_cfg(self, side, flags=None):
        if self.resolver is None:
            return self.left if side == LEFT else self.right
        table = self.resolver["table"]
        k = self.resolver["key"]
        flags = flags or {}
        entry = None
        if k in flags and flag_key(flags[k]) in table:
            entry = table[flag_key(flags[k])]
        elif "DEFAULT" in table:
            entry = table["DEFAULT"]
        else:
            entry = next(iter(table.values()))
        return entry.get("left") if side == LEFT else entry.get("right")

    def effect(self, side, flags=None):
        cfg = self.cfg(side, flags)
        if cfg is None or cfg.get("type", "pass") == "pass":
            return None
        return cfg["type"]

    def state_change(self, side, flags=None):
        cfg = self.cfg(side, flags)
        return None if cfg is None else cfg.get("state")

    def portal(self):
        """(a, d, pair id) of a portal surface, else None."""
        for cfg in (self.left, self.right):
            if cfg is not None and cfg.get("type") == "portal":
                c, sn = math.cos(cfg["theta"]), math.sin(cfg["theta"])
                a = mp.mpc(c, sn)
                return a / abs(a), P(cfg["d"]), int(cfg["pair"])
        return None


def portal_map(surf, z, inverse=False):
    a, d, _ = surf.portal()
    return mp.conj(a) * (z - d) if inverse else a * z + d


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
    res = d.get("resolver")
    if res is not None:
        assert res["table"], "a categorical resolver needs a table"
        for entry in res["table"].values():
            for cfg in (entry.get("left"), entry.get("right")):
                assert cfg is None or cfg.get("type", "pass") in ("pass", "terminal", "reflection", "inversion"), \
                    "categorical sides: pass / terminal / reflection / inversion (portals, projectives are fixed)"
    return Surface(sid, seg, d.get("left"), d.get("right"), bool(d.get("is_target")), res)


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
    if etype == "portal":
        a, d, _ = surf.portal()
        return Beam(a * h + d, a * beam.tangent_at(t), beam.kappa)
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


def map_kind(surf: Surface, etype):
    """The map an effect applies: reflection on a circle carrier is the
    inversion in it."""
    if etype == "reflection" and surf.seg.kind != "line":
        return "inversion"
    return etype


def map_point(z, surf: Surface, inverse=False):
    """The surface's transform (or its inverse) applied to a point (None =
    infinity)."""
    if surf.portal() is not None:
        return None if z is None else portal_map(surf, z, inverse)
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
    kind: str                 # hit | cursor | origin | escape | max | infinity
    surface_id: int = -1
    side: str = ""
    applied: bool = False
    terminal: bool = False
    on_segment: bool = False
    point: object = None
    t: object = None
    beam: object = None
    rehit: bool = False
    state: bool = False         # the side's state change fired
    flags: dict = None          # flags after the event


@dataclass
class StackEntry:
    beam: Beam
    sid: int
    t_hit: object
    other: object
    known: list = field(default_factory=list)
    line: object = None
    pop_id: int = None          # the surface whose hit pops this entry (sid, or a portal's pair)
    etype: str = None           # the effect pushed (a pop needs the same effect)


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
    flags: dict = field(default_factory=dict)
    flags_before: dict = field(default_factory=dict)   # flags the current event resolved under
    proj_hits: dict = field(default_factory=dict)      # projective entry index -> (H, exact exit line or None)
    on_plan: bool = True                               # on the planned beams: every projective fired at its H
    # (transformative entries are maps of the whole plane: whichever crossing
    # fires them, the next beam is the planned one -- with the plan matched)


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
    """Segment data points (endpoints and via) exactly on the exact line."""
    for s in walk.surfaces:
        if s.seg.full:
            continue
        for ep in (s.seg.S, s.seg.E, s.seg.V):
            if ep is None:
                continue
            if not any(k[0] == ep for k in walk.known) and on_line(walk.line, ep):
                walk.known.append((ep, beam.param_of(ep), None))


def _line_coeffs(seg):
    S, E = FC(seg.S), FC(seg.E)
    a, b = -(E[1] - S[1]), E[0] - S[0]
    return a, b, a * S[0] + b * S[1]


def _concurrent_ties(walk: Walk, cands):
    """On an exact line, adjacent crossings of two line carriers that meet the
    line at the same point (exact concurrency) share one parameter: the id
    rule orders them, never rounding."""
    by_id = {s.id: s for s in walk.surfaces}
    a, b, c = walk.line
    out = list(cands)
    i = 0
    while i + 1 < len(out):
        x, y = out[i], out[i + 1]
        if x[1] == 1 and y[1] == 1 and by_id[x[2]].seg.kind == "line" and by_id[y[2]].seg.kind == "line":
            a1, b1, c1 = _line_coeffs(by_id[x[2]].seg)
            a2, b2, c2 = _line_coeffs(by_id[y[2]].seg)
            det = a * (b1 * c2 - b2 * c1) - b * (a1 * c2 - a2 * c1) + c * (a1 * b2 - a2 * b1)
            if det == 0:
                lo, hi = (x, y) if x[2] < y[2] else (y, x)
                out[i] = (x[0], 1, lo[2], x[3], False)
                out[i + 1] = (x[0], 1, hi[2], x[3], False)
        i += 1
    return out


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
    return surf.effect(side, walk.flags)


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
        # the cursor is on the beam only on the planned path (its final
        # anchored beam passes through the cursor by construction); off it, a
        # cursor is never "reached" at its projection
        if not walk.cursor_done and walk.plan_index >= len(walk.plan) and walk.plan_matched \
                and walk.cursor is not None and walk.on_plan:
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
        if EMIT_INFINITY and beam.kappa == 0:
            # the wrap through infinity: after every crossing ahead (t > t0),
            # before the wrapped ones (no root of a straight beam is at inf)
            cands.append(((0, mp.inf), 3, INFINITY_ID, mp.inf, False))
        cands.sort(key=lambda c: (c[0], c[1], c[2]))
        if walk.line is not None:
            cands = _concurrent_ties(walk, cands)
        if depart is not None:
            cands.append(((2, 0), 2, depart, t0, True))      # departure re-hit, last
        applied = False
        fired_state = False
        ci = 0
        while ci < len(cands):
            key, _, sid, t, rehit = cands[ci]
            ci += 1
            walk.count += 1
            if sid == INFINITY_ID:
                if walk.count > MAX_HITS:
                    walk.events.append(Event("max", INFINITY_ID, t=t, beam=beam))
                    walk.done = True
                    return
                consumed = walk.plan_index < len(walk.plan) and walk.plan[walk.plan_index][0] == INFINITY_ID
                if consumed:
                    walk.plan_index += 1
                walk.events.append(Event("infinity", INFINITY_ID, applied=consumed, t=t, beam=beam,
                                         flags=walk.flags))
                if consumed:
                    _late_cursor(walk, beam, cands, ci, key, t0)
                continue
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
                walk.on_plan = False
                continue
            surf = by_id[sid]
            known_pt = beam.p if (rehit and fresh) else (None if rehit else snapped.get((sid, t)))
            pt_exact = known_pt is not None and any(k[0] == known_pt for k in walk.known)
            pt, on_seg, side, full_block = classify(walk, beam, t, surf, point=known_pt)
            if surf.is_target and on_seg:
                walk.targets.add(sid)
            do = None
            change = surf.state_change(side, walk.flags)
            fires_state = False
            if walk.mode == PHYSICAL:
                do = physical_decision(walk, surf, side, full_block)
                fires_state = change is not None and full_block
                if do in TRANSFORMATIVE or do in PROJECTIVE or fires_state:
                    _plan_bookkeeping(walk, sid)
            elif walk.plan_index < len(walk.plan) and walk.plan[walk.plan_index] == (sid, side):
                walk.plan_index += 1
                eff = surf.effect(side, walk.flags)
                do = eff if (eff in TRANSFORMATIVE or eff in PROJECTIVE) else None
                fires_state = change is not None
            walk.flags_before = walk.flags
            if fires_state:
                walk.flags = apply_state(walk.flags, change)
                fired_state = True
            ev = Event("hit", sid, side, do is not None, do == "terminal", on_seg, pt, t, beam, rehit,
                       fires_state, walk.flags)
            walk.events.append(ev)
            if do == "terminal":
                walk.done = True
                return
            if do in TRANSFORMATIVE:
                beam, depart, t0, fresh = _apply(walk, beam, t, surf, do, depart, fresh, t0, rehit, pt, pt_exact)
                applied = True
                break
            if do in PROJECTIVE:
                # on the planned path at its back-propagated hit point (the
                # crossing nearest H): the exit is exact data when the target
                # behind it is (projective_hits)
                planned_exit = None
                if walk.on_plan:
                    hj = walk.proj_hits.get(walk.plan_index - 1)
                    # the crossing fired is the one at H: of the beam's crossings
                    # of surf, the one nearest pt is the one nearest H (distinct
                    # crossings, compared as roots -- not as near-zero distances)
                    rs = roots(beam, surf, False)
                    if hj is None or not rs or \
                            min(rs, key=lambda r: abs(beam.at(r) - pt)) != min(rs, key=lambda r: abs(beam.at(r) - hj[0])):
                        walk.on_plan = False
                    else:
                        planned_exit = hj[1]
                beam = _project(walk, beam, t, surf, side, pt, pt_exact)
                if planned_exit is not None:
                    walk.line = planned_exit
                    _add_line_endpoints(walk, beam)
                depart, t0, fresh = sid, mp.mpf(0), True
                applied = True
                break
            _late_cursor(walk, beam, cands, ci, key, t0)
        if not applied and fired_state:
            # no effect, but the loop changed the state: the next loop on the
            # same beam differs (e.g. a ghost toggler re-hit) -- walk it again
            continue
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


def _late_cursor(walk: Walk, beam, cands, ci, key, t0):
    """The plan completed at this event without a frame change (an entry that
    resolves to a pass / state change, an unapplied projective, or the
    infinity entry): the cursor is on this same beam -- a candidate if it lies
    ahead (inserted in order among the remaining candidates)."""
    if not walk.cursor_done and walk.cursor is not None and walk.plan_index >= len(walk.plan) and walk.on_plan \
            and walk.plan_matched and not any(c[1] == 0 for c in cands):
        tc = beam.param_of(walk.cursor)
        kc = sort_key(tc, t0)
        if kc >= key:
            j = ci
            while j < len(cands) and (cands[j][0], cands[j][1]) <= (kc, 0):
                j += 1
            cands.insert(j, (kc, 0, -1, tc, False))
            if not any(k[0] == walk.cursor for k in walk.known):
                walk.known.append((walk.cursor, tc, None))


def _project(walk: Walk, beam, t, surf: Surface, side, pt, pt_exact):
    """Projective effect at pt: a straight beam along +-axis; clears the stack."""
    cfg = surf.cfg(side, walk.flags_before)
    etype = cfg["type"]
    seg = surf.seg
    g = seg.grad(pt)
    if etype == "line_normal_projection":
        axis = g
    elif etype == "circle_normal_projection":
        axis = pt - seg.c
    else:
        axis = P(cfg["normal"])
    d = beam.tangent_at(t)
    a_g = dot(axis, g)
    if a_g == 0:
        sgn = 1
    else:
        sgn = (1 if a_g > 0 else -1) * (1 if dot(d, g) > 0 else -1)
    if cfg.get("back"):
        sgn = -sgn
    exit = axis * sgn
    nb = Beam(pt, exit / abs(exit), mp.mpf(0))
    walk.stack = []
    walk.known = [(pt, mp.mpf(0), surf.id)] if pt_exact else []
    walk.line = None
    if pt_exact:
        # exact data line: through pt along the axis (exact for line normals,
        # full-circle radii and directional normals)
        if etype == "line_normal_projection":
            D = _sub(FC(seg.E), FC(seg.S))
            ax = (-D[1], D[0])
        elif etype == "circle_normal_projection":
            nx, ny, w, _rr = _center_h(seg)
            K = FC(pt)
            ax = (K[0] * w - nx, K[1] * w - ny)
        else:
            ax = FC(P(cfg["normal"]))
        K = FC(pt)
        a, b = -ax[1], ax[0]
        walk.line = (a, b, a * K[0] + b * K[1])
        _add_line_endpoints(walk, nb)
    return nb


def _plan_bookkeeping(walk: Walk, sid):
    if walk.plan_index < len(walk.plan):
        if sid == walk.plan[walk.plan_index][0]:
            walk.plan_index += 1
        else:
            walk.plan_matched = False
    else:
        walk.plan_matched = False
    if not walk.plan_matched:
        walk.on_plan = False      # a surface fired out of plan: off the planned beams


def _other_root(beam, surf: Surface, t, fresh, depart, t0, pt):
    """The beam's other crossing of surf: the root that is not the hit's
    crossing (at pt) -- the one farther from pt, the same position rule that
    matches a known point to its root (the hit's parameter may be a snapped
    known point's, not bit-equal to the recomputed root)."""
    if surf.id == depart:
        return t0
    rs = roots(beam, surf, False)
    if len(rs) < 2:
        return t
    return max(rs, key=lambda r: abs(beam.at(r) - pt))


def _apply(walk: Walk, beam, t, surf: Surface, etype, depart, fresh, t0, rehit, pt, pt_exact):
    if walk.stack and walk.stack[-1].pop_id == surf.id and walk.stack[-1].etype == map_kind(surf, etype):
        top = walk.stack.pop()
        src = {s.id: s for s in walk.surfaces}[top.sid]
        # the point on the restored beam: T^-1(pt) for a portal, pt otherwise
        q = portal_map(src, pt, inverse=True) if src.portal() is not None else pt
        if rehit:
            t_res = top.t_hit
        elif fresh and depart == surf.id:
            t_res = top.other
        else:
            dh = abs(top.beam.at(top.t_hit) - q)
            do = abs(top.beam.at(top.other) - q)
            t_res = top.t_hit if dh < do else top.other
        walk.known = top.known
        walk.line = top.line
        return top.beam, top.sid, t_res, False
    other = _other_root(beam, surf, t, fresh, depart, t0, pt)
    portal = surf.portal()
    pop_id = portal[2] if portal is not None else surf.id
    walk.stack.append(StackEntry(beam, surf.id, t, other, walk.known, walk.line, pop_id, map_kind(surf, etype)))
    nb = apply_effect(beam, t, surf, etype, pt)
    old_known = walk.known
    if portal is not None:
        _portal_known(walk, surf, pt, pt_exact, old_known, nb)
        return nb, pop_id, mp.mpf(0), True
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
        if surf.seg.kind != "line" and any(_is_center(k, surf.seg) for k, _, _ in old_known):
            # the old beam passes exactly through the inversion center (a
            # known point): its image is exactly a line
            nb = Beam(nb.p, nb.tau, mp.mpf(0))
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


def _exact_point(F2):
    """The exact rational point as a float64 data point (mpc), or None if it
    is not exactly representable."""
    x, y = float(F2[0]), float(F2[1])
    if Fraction(x) == F2[0] and Fraction(y) == F2[1]:
        return mp.mpc(x, y)
    return None


def _portal_image(surf, k):
    """T(k) as exact data: endpoint correspondence (the pair is built as T of
    the source) or, for a translation, an exactly representable k + d."""
    pair = {s.id: s for s in WALK_SURFACES}[surf.portal()[2]]
    if not surf.seg.full:
        if k == surf.seg.S:
            return pair.seg.S
        if k == surf.seg.E:
            return pair.seg.E
    cfg = surf.left if surf.left and surf.left.get("type") == "portal" else surf.right
    if cfg["theta"] != 0:
        return None
    K = FC(k)
    return _exact_point((K[0] + F(cfg["d"][0]), K[1] + F(cfg["d"][1])))


def _portal_known(walk, surf, pt, pt_exact, old_known, nb):
    """Known points and exact line after a portal push (rigid image)."""
    global WALK_SURFACES
    WALK_SURFACES = walk.surfaces
    pair_id = surf.portal()[2]
    known = []
    if pt_exact:
        h2 = _portal_image(surf, pt)
        if h2 is not None:
            known.append((h2, mp.mpf(0), pair_id))
    for k, _, _ in old_known:
        if k == pt:
            continue
        k2 = _portal_image(surf, k)
        if k2 is not None and not any(x[0] == k2 for x in known):
            known.append((k2, nb.param_of(k2), None))
    walk.known = known
    cfg = surf.left if surf.left and surf.left.get("type") == "portal" else surf.right
    if walk.line is not None and cfg["theta"] == 0:
        a, b, c = walk.line
        walk.line = (a, b, c + a * F(cfg["d"][0]) + b * F(cfg["d"][1]))
        _add_line_endpoints(walk, nb)
    else:
        walk.line = None
    return True


WALK_SURFACES = []


def _is_center(k, seg):
    nx, ny, w, _rr = _center_h(seg)
    K = FC(k)
    return K[0] * w == nx and K[1] * w == ny


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

def plan_resolved(surfaces, plan, flags=None):
    """Forward state simulation: (effect, flags before the entry) per entry."""
    by_id = {s.id: s for s in surfaces}
    f = dict(flags or {})
    out = []
    for sid, side in plan:
        surf = by_id.get(sid)
        if surf is None:
            out.append((None, f))
            continue
        out.append((surf.effect(side, f), f))
        ch = surf.state_change(side, f)
        if ch is not None:
            f = apply_state(f, ch)
    return out


def projective_exit(surf, side, cfg, H, axis):
    """The exit direction at H of a projective side entered from `side`
    (Stage 83 rule: through -> sign(exit . grad) = sign(dir . grad), back ->
    the opposite; an axis tangent to the carrier exits along +-axis)."""
    g = surf.seg.grad(H)
    w = winding(surf.seg)
    approach_pos = (side == LEFT) == (w >= 0)       # approach = -dir . grad > 0
    dg = -1 if approach_pos else 1                   # sign(dir . grad)
    a_g = dot(axis, g)
    sgn = 1 if a_g == 0 else (1 if a_g > 0 else -1) * dg
    if cfg.get("back"):
        sgn = -sgn
    return axis * sgn


def back_propagate(surf, side, X, flags=None, x_on_carrier=False, click=None):
    """The hit point: the candidate nearest the point the player clicked --
    by default the segment's midpoint (its via point). A circle projective
    has two candidates (both exit lines pass through X; one may reach X only
    through infinity): a fork, settled at planning time by the click. None
    without a candidate."""
    c = back_propagate_candidates(surf, side, X, flags, x_on_carrier)
    if not c:
        return None
    pool = [H for H, _ in c]
    if len(pool) == 1:
        return pool[0]
    point = P(click) if click is not None else surf.seg.V
    return min(pool, key=lambda H: abs(H - point))


def back_propagate_candidates(surf, side, X, flags=None, x_on_carrier=False):
    """The candidate hit points H on a projective side from which the
    projected beam passes through X (GAME_SPEC 13.3), on the carrier: line
    normal -> the foot of X; circle normal -> c +- R u (u = unit(X - c));
    directional -> the carrier crossings of the line through X along the
    normal. Returns [(H, direct)] (direct: the exit ray from H, by the plan
    side and the exact exit rule, reaches X directly). x_on_carrier
    (provenance: X is the hit point of a later projective entry on this
    surface, or the cursor exactly on its carrier): X is itself a candidate
    and is excluded -- the beam must reach X from the other one."""
    cfg = surf.cfg(side, flags)
    etype = cfg["type"]
    seg = surf.seg
    cands = []
    if etype == "line_normal_projection":
        D = seg.E - seg.S
        cands = [seg.S + (dot(X - seg.S, D) / dot(D, D)) * D]
    elif etype == "circle_normal_projection":
        u = X - seg.c
        if u == 0:
            return []
        u = u / abs(u)
        R = mp.sqrt(seg.R2)
        cands = [seg.c + R * u, seg.c - R * u]
    else:
        n = P(cfg["normal"])
        if seg.kind == "line":
            D = seg.E - seg.S
            den = cross(D, n)
            if den == 0:
                return []
            cands = [X - (cross(D, X - seg.S) / den) * n]
        else:
            # |X - s n - c|^2 = R^2
            a = dot(n, n)
            b = -2 * dot(n, X - seg.c)
            c0 = dot(X - seg.c, X - seg.c) - seg.R2
            disc = b * b - 4 * a * c0
            if disc < 0:
                return []
            sq = mp.sqrt(disc)
            cands = [X - ((-b + sq) / (2 * a)) * n, X - ((-b - sq) / (2 * a)) * n]

    def axis_at(H):
        if etype == "line_normal_projection":
            return seg.grad(H)
        if etype == "circle_normal_projection":
            return H - seg.c
        return P(cfg["normal"])

    if x_on_carrier and len(cands) == 2:
        cands = [max(cands, key=lambda H: abs(X - H))]

    def direct(H):
        return dot(X - H, projective_exit(surf, side, cfg, H, axis_at(H))) > 0
    return [(H, direct(H)) for H in cands]


def plan_effects(surfaces, plan, flags=None):
    """Forward state simulation (13.7): the effect each plan entry resolves
    to, under the flags left by the entries before it."""
    by_id = {s.id: s for s in surfaces}
    f = dict(flags or {})
    out = []
    for sid, side in plan:
        surf = by_id.get(sid)
        if surf is None:
            out.append(None)
            continue
        out.append(surf.effect(side, f))
        ch = surf.state_change(side, f)
        if ch is not None:
            f = apply_state(f, ch)
    return out


def trace_physical_direction(surfaces, player, direction, plan=(), cursor=None, flags=None):
    """Physical trace of an explicit direction (forward shooting reference)."""
    return trace_mode_direction(surfaces, player, direction, plan, cursor, PHYSICAL, flags=flags)


def trace_mode_direction(surfaces, player, direction, plan, cursor, mode, line="direction", flags=None,
                         extra_known=(), proj_hits=None):
    """line: the initial beam's exact implicit line; "direction": `direction`
    is exact data; None: not exact."""
    walk = Walk(surfaces, list(plan), cursor, mode, PHYSICAL)
    walk.flags = dict(flags or {})
    walk.flags_before = walk.flags
    walk.proj_hits = proj_hits or {}
    p0 = P(player)
    b0 = Beam(p0, direction / abs(direction), mp.mpf(0))
    walk.known = [(p0, mp.mpf(0), None)]
    walk.known += [(k, b0.param_of(k), None) for k in extra_known if k != p0]
    walk.line = line_from_dir(p0, direction) if line == "direction" else line
    if walk.line is not None:
        _add_line_endpoints(walk, b0)
    walk_from(walk, b0, None)
    return walk


def aim_image(surfaces, plan, cursor, flags=None, hints=None):
    """The aim point: the cursor pulled back through the plan -- transformative
    entries by their inverse maps, projectives by back-propagation (13.3;
    an entry without a back-propagated point is skipped)."""
    return _backward(surfaces, plan, cursor, flags, hints)[0]


def projective_hits(surfaces, plan, cursor, flags=None, hints=None):
    """Plan entry index -> (H, exact exit line or None) of each projective
    entry: the back-propagated hit point, and -- for the last projective, when
    the cursor pulled back through the entries after it is exact data -- the
    exact line it leaves along (through that point, along the exact axis)."""
    hits = _backward(surfaces, plan, cursor, flags, hints)[1]
    if not hits:
        return {}
    by_id = {s.id: s for s in surfaces}
    j = max(hits)
    X = exact_image(surfaces, cursor, plan, flags, start=j + 1)
    line = None
    if X is not None:
        sid, side = plan[j]
        surf = by_id[sid]
        etype = surf.effect(side, plan_resolved(surfaces, plan, flags)[j][1])
        if etype == "line_normal_projection":
            D = _sub(FC(surf.seg.E), FC(surf.seg.S))
            ax = (-D[1], D[0])
        elif etype == "circle_normal_projection":
            nx, ny, w, _rr = _center_h(surf.seg)
            ax = (X[0] - nx / w, X[1] - ny / w)
        else:
            ax = FC(P(surf.cfg(side)["normal"]))
        if ax != (0, 0):
            a, b = -ax[1], ax[0]
            line = (a, b, a * X[0] + b * X[1])
    return {k: (hits[k], line if k == j else None) for k in hits}


def _backward(surfaces, plan, cursor, flags=None, hints=None):
    """(aim point or None, {projective entry index: H}): the backward pass.
    A circle projective whose target lies inside the circle has two
    candidate hit points (both exits reach the target): the fork is settled
    at planning time by back_propagate -- the candidate nearer the point the
    player clicked (hints: plan entry index -> click point), by default the
    segment's midpoint. An entry without a candidate is skipped."""
    by_id = {s.id: s for s in surfaces}
    plan = list(plan)
    hints = hints or {}
    res = plan_resolved(surfaces, plan, flags)
    hits = {}
    z = P(cursor)
    on_sid = None        # the surface whose carrier z lies on (provenance)
    for j in range(len(plan) - 1, -1, -1):
        sid, side = plan[j]
        eff, f = res[j]
        if eff in PROJECTIVE:
            on = on_sid == sid or (on_sid is None and incident(z, by_id[sid]))
            H = back_propagate(by_id[sid], side, z, f, on, hints.get(j))
            if H is None:
                continue
            z = H
            on_sid = sid
            hits[j] = H
            continue
        if eff not in TRANSFORMATIVE:
            continue
        on_sid = -1
        z = map_point(z, by_id[sid], inverse=True)
        if z is None:
            return None, hits
    return z, hits


def exact_direction(surfaces, plan, player, cursor, flags=None, hints=None):
    img = aim_image(surfaces, plan, cursor, flags, hints)
    if img is None:
        return None
    d = img - P(player)
    return None if d == 0 else d


def exact_image(surfaces, cursor, plan, flags=None, start=0):
    """The aim image F_K^-1(cursor) as exact rationals, when every frame
    change of the plan is a rational map of exact data -- a line reflection,
    an inversion in a circle (exact circumcenter and radius²) or a
    translation portal. None for a rotation portal, or an image at infinity."""
    by_id = {s.id: s for s in surfaces}
    effs = plan_effects(surfaces, plan, flags)[start:]
    plan = list(plan)[start:]
    if any(e in PROJECTIVE for e in effs):
        return None      # a back-propagated hit point is not exact data
    X = FC(P(cursor))
    for (sid, side), eff in reversed(list(zip(plan, effs))):
        surf = by_id.get(sid)
        if surf is None or eff not in TRANSFORMATIVE:
            continue
        if surf.portal() is not None:
            cfg = surf.left if surf.left and surf.left.get("type") == "portal" else surf.right
            if cfg["theta"] != 0:
                return None
            X = (X[0] - F(cfg["d"][0]), X[1] - F(cfg["d"][1]))
            continue
        if surf.seg.kind != "line":
            # inversion: X' = c + r² (X - c) / |X - c|² (exact rationals)
            nx, ny, w, rr = _center_h(surf.seg)
            cx, cy = nx / w, ny / w
            dx, dy = X[0] - cx, X[1] - cy
            dd = dx * dx + dy * dy
            if dd == 0:
                return None
            X = (cx + rr * dx / dd, cy + rr * dy / dd)
            continue
        S, E = FC(surf.seg.S), FC(surf.seg.E)
        M = _sub(E, S)
        mm = M[0] * M[0] + M[1] * M[1]
        m2r, m2i = M[0] * M[0] - M[1] * M[1], 2 * M[0] * M[1]
        wx, wy = X[0] - S[0], -(X[1] - S[1])
        X = (S[0] + (m2r * wx - m2i * wy) / mm, S[1] + (m2r * wy + m2i * wx) / mm)
    return X


def initial_line(surfaces, player, cursor, plan, flags=None):
    """Exact implicit line of the initial beam: player -> the exact aim image."""
    X = exact_image(surfaces, cursor, plan, flags)
    if X is None:
        return None
    Pp = FC(P(player))
    a, b = -(X[1] - Pp[1]), X[0] - Pp[0]
    return (a, b, a * Pp[0] + b * Pp[1])


def fixes_cursor(surfaces, cursor, plan, flags=None):
    """Exact: every frame change of the plan fixes the cursor (it lies on the
    carrier of each planned reflection / inversion: an effect fixes its carrier
    pointwise; a translation portal fixes nothing), so the aim image is the
    cursor itself."""
    by_id = {s.id: s for s in surfaces}
    k = P(cursor)
    for (sid, side), eff in zip(plan, plan_effects(surfaces, plan, flags)):
        if eff in PROJECTIVE:
            return False
        if eff not in TRANSFORMATIVE:
            continue
        surf = by_id[sid]
        if surf.portal() is not None or not incident(k, surf):
            return False
    return True


def via_infinity(surfaces, plan, flags=None):
    """Stage 88: the plan's first epoch (its entries before the first
    projective) holds an infinity entry: the aim is the second realization,
    away from the aim image, reaching it through infinity."""
    for entry, eff in zip(plan, plan_effects(surfaces, plan, flags)):
        if eff in PROJECTIVE:
            return False
        if entry[0] == INFINITY_ID:
            return True
    return False


def _aim(surfaces, player, cursor, plan, flags=None, hints=None):
    """(direction, exact line or None, known points): the exact aim image's
    direction, or -- when the image is at infinity (aim undetermined) --
    straight at the cursor. When every frame change fixes the cursor, the aim
    image is the cursor: exact data the initial beam passes through (known).
    An infinity entry in the first epoch reverses the aim (via_infinity)."""
    away = via_infinity(surfaces, plan, flags)
    d = exact_direction(surfaces, plan, player, cursor, flags, hints)
    if d is None:
        d0 = P(cursor) - P(player)
        return (-d0 if away else d0), "direction", []
    known = [P(cursor)] if plan and fixes_cursor(surfaces, cursor, plan, flags) else []
    return (-d if away else d), initial_line(surfaces, player, cursor, plan, flags), known


def planned(surfaces, player, cursor, plan, flags=None, hints=None):
    """Planned trace = forward PLANNED-mode walk of the exact aim direction.
    hints: plan entry index -> the point the player clicked (settles forks)."""
    d, line, known = _aim(surfaces, player, cursor, plan, flags, hints)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PLANNED, line=line, flags=flags,
                                extra_known=known, proj_hits=projective_hits(surfaces, plan, cursor, flags, hints))


def physical(surfaces, player, cursor, plan, flags=None, hints=None):
    d, line, known = _aim(surfaces, player, cursor, plan, flags, hints)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PHYSICAL, line=line, flags=flags,
                                extra_known=known, proj_hits=projective_hits(surfaces, plan, cursor, flags, hints))
