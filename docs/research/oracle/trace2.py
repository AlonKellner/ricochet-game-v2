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
  Plan bookkeeping by surface id: an event that changes the frame (an
  applied transformative / projective, or a state change that changes the
  flags) advances the plan or marks it unmatched; an event where the planned
  walk consumes the next entry without a frame change (a pass side, a
  terminal off its segment, a write of a flag's current value) is consumed
  too (B1, Stage 110): both walks reach the cursor together.
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

Hit effects and presence (Stage 107, GAME_SPEC 9.6 / 10.6):
- A side config carries a LIST of state changes, "states": [...] (the legacy
  single "state" is read as a one-element list). They fire together, by the
  rule above, and apply in list order (last writer wins).
- A side is a ghost toggler when any of its state changes writes "ghost".
- A hit event records whether its side FIRED (`fired`): the state-change rule
  (PHYSICAL: fully blocked; PLANNED: the consumed entry), not ghosted. The
  side's hit effects fire exactly then, in shot or at landing.
- Stage 108 (GAME_SPEC 9.7): a plan entry may name a PIECE of its surface --
  a span {start, end, via} on the surface's carrier. PHYSICAL: a firing hit
  matches the entry only inside the span (else the paths diverge); PLANNED:
  the entry is consumed as before, its event on-segment only inside the span.
- Surfaces on exactly the same carrier (K6, Stage 109) are crossed at the same
  points: their roots are computed once per loop and shared, so simultaneous
  crossings tie exactly and the id breaks the tie (GAME_SPEC 9.3).
- A surface may carry "presence": [{"key", "values": [flag_key...]}], all
  required ("DEFAULT" matches a missing key). An absent surface does not exist
  for the walk: its crossings are skipped when reached (presence is read under
  the current flags, so a surface can vanish or appear mid-loop), it blocks no
  junction, and a plan entry on it resolves to nothing (no effect, no state
  change) and is never consumed.

Infinity (Stage 88): a plan entry INFINITY_ID (the player clicked the screen
edge) means "the beam wraps through infinity here". Every plan has two
realizations: the aim toward the aim image X_0 (the direct one), and the aim
away from it, reaching X_0 through infinity (the same circle the other way
round). Which one is the plan's (Stage 111, `realization`) is fixed by the
first planned entry e of the first epoch that names a present surface: R1 --
when only one orientation crosses e's carrier from e's side (a straight beam
and a line carrier, always), that one: on the planned side aim at the carrier,
on the other side aim away and come back through infinity; R2 -- when both do
(a circle carrier, a circle beam), the one whose planned walk consumes the
plan and reaches the cursor, else the one that fires e before the passage of
Q_0 -- after it when an infinity entry precedes e; R3 -- otherwise (no e, no
crossing, both orientations alike) direct, reversed by an infinity entry in
the first epoch (via_infinity). In a later epoch the projective's exit fixes
the orientation. Infinity entries are otherwise only matched. With EMIT_INFINITY, a beam passing the current frame's image
of infinity Q = F(inf) emits an "infinity" event, counted toward MAX_HITS like
a pass. On a straight beam with Q = inf that is the passage of t = inf (the
visual wrap: out through one screen edge, back in through the opposite one),
ordered between the crossings ahead (t > t0) and the wrapped ones (t <= t0); a
trace cut by MAX_HITS there ends with a "max" event at infinity (no point).
When the frame has moved infinity to a finite point Q (after an inversion, or
in a scene with a pre-transform, below), the beam passes Q at a finite
parameter: the event carries the point Q and is ordered like any crossing (at
a tie after the cursor and the hits). Q is tracked along the walk: the
pre-transform's image of inf at the start, mapped forward by every applied
effect, restored by a pop, inf again after a projective. The event is applied
(consumes the entry) when the next plan entry is the infinity entry, in both
modes; otherwise it passes -- an unplanned passage is not a divergence, while
an infinity entry that is not reached when another entry fires is one, like
any unmet entry.

Pre-transform (Stage 101): a scene may carry surfaces flagged "pre_transform"
(full inversion circles, never hit). They define the frame the shot starts in:
Q0 = the composite image of inf, and every beam of the first epoch is the
image of a straight line under that frame -- a circle through Q0. The initial
beam is the oriented circle through the player, the aim point and Q0 (P -> X
-> Q0, or P -> Q0 -> X with an infinity entry in the first epoch: the long way
round); a direction trace starts along its direction on the circle through Q0.

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
EMIT_INFINITY = True


# ---------------------------------------------------------------- scene

GHOST = "ghost"


def side_states(cfg):
    """A side config's state changes, in order (Stage 107: "states"; the
    legacy single "state" is a one-element list)."""
    if cfg is None:
        return []
    if cfg.get("states") is not None:
        return list(cfg["states"])
    return [cfg["state"]] if cfg.get("state") is not None else []


def is_ghost_toggler(cfg):
    """A side config with a state change that writes the ghost flag."""
    return any(ch.get("key") == GHOST for ch in side_states(cfg))


def present(presence, flags):
    """Whether a surface with these presence conditions exists under the
    flags (Stage 107): every condition's values hold flag_key of its key's
    value ("DEFAULT" for a missing key)."""
    for cond in presence or ():
        k = cond["key"]
        v = flag_key(flags[k]) if flags is not None and k in flags else "DEFAULT"
        if v not in cond["values"]:
            return False
    return True


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


def apply_states(flags, changes):
    """flags after a side's state changes, in order."""
    for ch in changes:
        flags = apply_state(flags, ch)
    return flags


@dataclass
class Surface:
    id: int
    seg: Seg
    left: dict | None
    right: dict | None
    is_target: bool = False
    resolver: dict | None = None
    pre_transform: bool = False    # Stage 101: part of the scene's pre-transform, never hit
    presence: list | None = None   # Stage 107: [{key, values}], all required; None = always

    def present(self, flags=None):
        return present(self.presence, flags)

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

    def ghosted(self, side, flags=None):
        """Ghost mode: the side is a pass (it is not a ghost toggler)."""
        return (flags or {}).get(GHOST) is True and not is_ghost_toggler(self._base_cfg(side, flags))

    def effect(self, side, flags=None):
        cfg = self.cfg(side, flags)
        if cfg is None or cfg.get("type", "pass") == "pass":
            return None
        return cfg["type"]

    def state_changes(self, side, flags=None):
        """The side's state changes under the flags, in order (Stage 107)."""
        return side_states(self.cfg(side, flags))

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
    presence = d.get("presence") or None
    assert presence is None or not d.get("pre_transform"), "a pre-transform surface is always present"
    return Surface(sid, seg, d.get("left"), d.get("right"), bool(d.get("is_target")), res,
                   bool(d.get("pre_transform")), presence)


def scene_from_json(surfaces):
    return [surface_from_json(d) for d in surfaces]


def same_carrier(a: Surface, b: Surface):
    """Exact: two surfaces lie on one carrier (K6, Stage 109)."""
    sa, sb = a.seg, b.seg
    if sa.kind != sb.kind:
        return False
    if sa.kind == "line":
        S, E = FC(sa.S), FC(sa.E)
        D = _sub(E, S)
        return _cross(D, _sub(FC(sb.S), S)) == 0 and _cross(D, _sub(FC(sb.E), S)) == 0
    n1x, n1y, w1, r1 = _center_h(sa)
    n2x, n2y, w2, r2 = _center_h(sb)
    return n1x * w2 == n2x * w1 and n1y * w2 == n2y * w1 and r1 == r2


def carrier_groups(surfaces):
    """surface id -> the first (lowest) id of the surfaces on exactly its carrier."""
    out = {}
    for i, s in enumerate(surfaces):
        out[s.id] = next((r.id for r in surfaces[:i] if same_carrier(r, s)), s.id)
    return out


def hittable(surfaces):
    """The surfaces a beam can meet (the pre-transform's are not)."""
    return [s for s in surfaces if not s.pre_transform]


def pre_q(surfaces):
    """Q0: the scene's pre-transform applied to infinity (None = infinity)."""
    q = None
    for s in surfaces:
        if s.pre_transform:
            q = map_point(q, s)
    return q


def beam_through(p, x, q):
    """The oriented circle through p -> x -> q anchored at p (q None: the
    straight beam from p toward x). HPBeam.through."""
    a = x - p
    if q is None or q == x or q == p:
        # (the aim point or the player at the frame's infinity: the line through them)
        return Beam(p, a / abs(a), mp.mpf(0))
    b = q - p
    # g = |a|^2 J(b) - |b|^2 J(a), J = rotate by -90 deg; tau = -i unit(g)
    g = abs(a) ** 2 * (-1j * b) - abs(b) ** 2 * (-1j * a)
    tau = -1j * g
    tau = tau / abs(tau)
    kap = 2 * cross(a, b) / (abs(a) * abs(b) * abs(b - a))
    return Beam(p, tau, kap)


def beam_through_tangent(p, tau, q):
    """The beam from p along the unit tangent tau whose circle passes q (q
    None: the straight beam): kappa = 2 cross(tau, q - p) / |q - p|^2."""
    if q is None or q == p:
        return Beam(p, tau, mp.mpf(0))     # (the player at Q: a straight beam, Q at its start)
    w = q - p
    return Beam(p, tau, 2 * cross(tau, w) / abs(w) ** 2)


def reversed_beam(b):
    """The same circle run the other way round."""
    return Beam(b.p, -b.tau, -b.kappa)


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
    fired: bool = False         # Stage 107: the side fired (its hit effects fire), ghost aside
    popped: bool = False        # the applied effect popped the transform stack (Stage 110: frames)
    projective: bool = False    # the applied effect was projective (a new epoch, the stack cleared)


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
    q: object = None            # the frame's image of infinity before the push (restored by the pop)
    pole: object = None         # the exact pole record before the push (Stage 105)
    gc: object = None           # the exact generalized circle before the push (Perf P2b)


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
    gc: object = None                           # Perf P2b: its exact generalized circle (A, B, C, D), or None
    plan_index: int = 0
    plan_matched: bool = True
    plan_spans: list = None   # Stage 108: per entry, a piece's span (Seg) or None (the whole surface)
    carrier_groups: dict = None   # K6: surface id -> the lowest id on exactly the same carrier
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
    Q: object = None                                   # the current frame's image of infinity (None = inf)
    # Stage 105: the exact pole record (Qx, Qy, Dx, Dy), Fractions: the frame's
    # infinity point and the beam's direction there (any nonzero real scale),
    # or None (Q at infinity, or the frame's data not exact)
    pole: object = None
    # the current beam departs from the frame's infinity point (provenance: an
    # effect fired at a crossing pinned at Q; reflections and inversions fix the
    # hit point, portals move it with Q): no pinning, no passage of Q
    depart_q: bool = False
    # (transformative entries are maps of the whole plane: whichever crossing
    # fires them, the next beam is the planned one -- with the plan matched)
    # B5 (Stage 110): the point being crossed (a run of tied candidates), with
    # the planned walk's shadow over it -- see _Point
    point: object = None


@dataclass
class _Point:
    """B5 (Stage 110): the physical walk at one point -- a run of candidates
    with one sort key (surfaces on one carrier share their crossings, K6; ties
    and snaps give one parameter). The plan is matched by the frame the walk
    leaves the point in, whatever the order of the surfaces there: `flags` and
    `index` are the planned walk's (its shadow) over the point's candidates; a
    physical flag change out of plan leaves the point `pending` until the
    shadow's flags equal the walk's (the walk then takes the shadow's plan
    index), and a point closed pending unmatches the plan."""
    key: tuple
    rehit: bool
    flags: dict
    index: int
    broke: bool = False      # the shadow consumed a transformative or projective entry
    pending: bool = False


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


# ---------------------------------------------------------------- the exact beam (Perf P2b)
# A generalized circle A (x^2 + y^2) + B x + C y + D = 0, exact (A = 0: a line):
# the current beam's, carried through every map as `line` is (the line's own
# while there is one; a beam without it makes no exact claim).

def gc_from_line(line):
    a, b, c = line
    return (Fraction(0), a, b, -c)


def _gc_shift(g, dx, dy):
    """The image under z -> z + d: G(z - d)."""
    A, B, C, D = g
    return (A, B - 2 * A * dx, C - 2 * A * dy, A * (dx * dx + dy * dy) - B * dx - C * dy + D)


def gc_reflect(g, seg):
    """G(R z): R the reflection in the line of seg (an involution)."""
    A, B, C, D = g
    S, E = FC(seg.S), FC(seg.E)
    M = _sub(E, S)
    mm = M[0] * M[0] + M[1] * M[1]
    r11, r12 = (M[0] * M[0] - M[1] * M[1]) / mm, 2 * M[0] * M[1] / mm
    vx, vy = 2 * A * S[0] + B, 2 * A * S[1] + C
    rvx, rvy = r11 * vx + r12 * vy, r12 * vx - r11 * vy
    ss = S[0] * S[0] + S[1] * S[1]
    k = A * ss + B * S[0] + C * S[1] + D
    return (A, rvx - 2 * A * S[0], rvy - 2 * A * S[1], A * ss - (rvx * S[0] + rvy * S[1]) + k)


def gc_invert(g, seg):
    """The image under the inversion in seg's circle (center c, radius^2 rr)."""
    nx, ny, w, rr = _center_h(seg)
    cx, cy = nx / w, ny / w
    A, B, C, D = _gc_shift(g, -cx, -cy)
    return _gc_shift((D, rr * B, rr * C, A * rr * rr), cx, cy)


def gc_translate(g, dx, dy):
    return _gc_shift(g, F(dx), F(dy))


def gc_through(P1, P2, P3):
    """The generalized circle through three exact points (a line when they are
    collinear): the rows (x^2 + y^2, x, y, 1) of the points, expanded."""
    rows = [(p[0] * p[0] + p[1] * p[1], p[0], p[1], Fraction(1)) for p in (P1, P2, P3)]

    def det3(cols):
        m = [[r[c] for c in cols] for r in rows]
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    return (det3((1, 2, 3)), -det3((0, 2, 3)), det3((0, 1, 3)), -det3((0, 1, 2)))


def gc_at(g, X):
    A, B, C, D = g
    return A * (X[0] * X[0] + X[1] * X[1]) + B * X[0] + C * X[1] + D


def _concurrent_ties_gc(walk: Walk, beam, cands):
    """Perf P2b: on a circular beam with an exact circle, adjacent crossings of
    two line carriers at their common point, which the beam passes exactly,
    share one parameter: the id rule orders them, never rounding (as on an
    exact line, _concurrent_ties). The roots at the point are told from the
    carriers' other roots by distance; the decision itself is exact."""
    by_id = {s.id: s for s in walk.surfaces}
    tol = mp.mpf(10) ** (-(mp.mp.dps // 2))
    out = list(cands)
    i = 0
    while i + 1 < len(out):
        x, y = out[i], out[i + 1]
        if x[1] == 1 and y[1] == 1 and not x[4] and not y[4] and by_id[x[2]].seg.kind == "line" and by_id[y[2]].seg.kind == "line":
            a1, b1, c1 = _line_coeffs(by_id[x[2]].seg)
            a2, b2, c2 = _line_coeffs(by_id[y[2]].seg)
            det = a1 * b2 - a2 * b1
            if det != 0:
                X = ((c1 * b2 - c2 * b1) / det, (a1 * c2 - a2 * c1) / det)
                xm = mp.mpc(mp.mpf(X[0].numerator) / X[0].denominator, mp.mpf(X[1].numerator) / X[1].denominator)
                scale = max(mp.mpf(1), abs(xm))
                if gc_at(walk.gc, X) == 0 and abs(beam.at(x[3]) - xm) <= tol * scale and abs(beam.at(y[3]) - xm) <= tol * scale:
                    lo, hi = (x, y) if x[2] < y[2] else (y, x)
                    out[i] = (x[0], 1, lo[2], x[3], False)
                    out[i + 1] = (x[0], 1, hi[2], x[3], False)
        i += 1
    return out


def _junction_blocked(walk: Walk, surf: Surface, pt, direction):
    """Blocked sides at an exact endpoint, incl. other segments sharing it."""
    bl = br = False
    for s in walk.surfaces:
        if not s.present(walk.flags):
            continue        # an absent surface blocks no junction (Stage 107)
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
        if POLE_HOOK is not None:
            POLE_HOOK(walk, beam)       # tests: the record beside each beam
        cands = []
        # surfaces on one carrier (exactly) are crossed at the same points: their
        # roots are computed once per loop and shared (K6, Stage 109) -- on the
        # departing member's own segment when the beam departs that carrier
        if walk.carrier_groups is None:
            walk.carrier_groups = carrier_groups(walk.surfaces)
        groups = walk.carrier_groups
        dep_key = groups.get(depart) if depart is not None else None
        shared = {}
        for s in walk.surfaces:
            if walk.line is not None and s.seg.kind != "line" and _tangent(walk.line, s.seg):
                continue        # a tangent touch is not a crossing (exact)
            if walk.pole is not None and _pole_tangent(walk.pole, s.seg):
                continue        # tangent at the frame's infinity point (exact, Stage 105)
            key = groups[s.id]
            if key not in shared:
                departing = dep_key == key
                src = by_id[depart] if departing else s
                if fresh:
                    rs = roots(beam, src, departing)
                    rs = [t for t in rs if t != 0] if departing else [t for t in rs]
                else:
                    rs = roots(beam, src, False)
                    if departing and rs:
                        # a resumed beam departs this carrier at t0: its root nearest t0
                        # is the departure (identified, not compared for equality: the
                        # roots are recomputed on the restored beam and can land an ulp
                        # off t0; Stage 106)
                        rs.remove(min(rs, key=lambda t: abs(t - t0)))
                shared[key] = rs
            rs = shared[key]
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
                    k[0] == walk.cursor and k[2] == depart and k[1] == t0 for k in walk.known)):
                tc = t0
            cands.append(((-1, 0) if tc == t0 else sort_key(tc, t0), 0, -1, tc, False))
            if not any(k[0] == walk.cursor for k in walk.known):
                walk.known.append((walk.cursor, tc, None))
        # K7: the cursor is a known point of every beam whose exact circle passes
        # it, so a carrier through the cursor is crossed exactly there (the
        # departure point after it is the cursor exactly: reached at once)
        if walk.cursor is not None and walk.gc is not None and not any(k[0] == walk.cursor for k in walk.known) \
                and gc_at(walk.gc, FC(walk.cursor)) == 0:
            walk.known.append((walk.cursor, beam.param_of(walk.cursor), None))
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
        pinned = set()
        if walk.pole is not None and walk.Q is not None:
            # Stage 105: a carrier through the frame's infinity point is crossed
            # exactly there (the record makes Q exact): its root nearest Q becomes
            # Q's parameter, so simultaneous crossings at Q tie exactly (by id,
            # the passage of Q last). When the beam departs from Q (depart_q), the
            # crossing of another carrier through Q is at the departure point:
            # the end of the loop, like a known departure point's
            tq = t0 if walk.depart_q else beam.param_of(walk.Q)
            if walk.depart_q or tq != t0:
                for s in walk.surfaces:
                    if not _pole_on(walk.pole, s.seg) or (walk.depart_q and s.id == depart):
                        continue
                    idx = [i for i, c in enumerate(cands) if c[1] == 1 and c[2] == s.id]
                    if not idx:
                        continue
                    best = min(idx, key=lambda i: abs(beam.at(cands[i][3]) - walk.Q))
                    cands[best] = (sort_key(tq, t0), 1, s.id, tq, False)
                    snapped[(s.id, tq)] = walk.Q
                    pinned.add((s.id, tq))
        if EMIT_INFINITY:
            if walk.Q is None:
                if beam.kappa == 0:
                    # the wrap through infinity: after every crossing ahead (t > t0),
                    # before the wrapped ones (no root of a straight beam is at inf)
                    cands.append(((0, mp.inf), 3, INFINITY_ID, mp.inf, False))
            elif not walk.depart_q:
                # the frame's image of infinity is a finite point on this beam:
                # an ordinary crossing (not at the departure point)
                tq = beam.param_of(walk.Q)
                if tq != t0:
                    cands.append((sort_key(tq, t0), 3, INFINITY_ID, tq, False))
                    snapped[(INFINITY_ID, tq)] = walk.Q
        cands.sort(key=lambda c: (c[0], c[1], c[2]))
        if walk.line is not None:
            cands = _concurrent_ties(walk, cands)
        elif walk.gc is not None:
            cands = _concurrent_ties_gc(walk, beam, cands)
        if depart is not None:
            cands.append(((2, 0), 2, depart, t0, True))      # departure re-hit, last
        applied = False
        fired_state = False
        ci = 0
        while ci < len(cands):
            key, _, sid, t, rehit = cands[ci]
            ci += 1
            if walk.mode == PHYSICAL:
                # B5: the point this candidate is at
                if walk.point is not None and (walk.point.key != key or walk.point.rehit != rehit):
                    _point_close(walk)
                if walk.point is None and sid != -1 and walk.plan_matched and walk.on_plan and not walk.cursor_done:
                    _point_open(walk, key, rehit)
            if sid in by_id and not by_id[sid].present(walk.flags):
                p = walk.point
                if p is not None and by_id[sid].present(p.flags):
                    # absent for the walk, present for the planned shadow (B5)
                    pt, _on, side, _fb = classify(walk, beam, t, by_id[sid], point=snapped.get((sid, t)))
                    _point_step(walk, by_id[sid], sid, side, pt)
                    if _point_sync(walk):
                        _late_cursor(walk, beam, cands, ci, key, t0)
                continue        # absent under the current flags (Stage 107): no crossing at all
            walk.count += 1
            if sid == INFINITY_ID:
                q_pt = snapped.get((INFINITY_ID, t))   # None: the real infinity
                if walk.count > MAX_HITS:
                    walk.events.append(Event("max", INFINITY_ID, point=q_pt, t=t, beam=beam))
                    walk.done = True
                    return
                consumed = walk.plan_index < len(walk.plan) and walk.plan[walk.plan_index][0] == INFINITY_ID
                if consumed:
                    walk.plan_index += 1
                walk.events.append(Event("infinity", INFINITY_ID, applied=consumed, point=q_pt, t=t, beam=beam,
                                         flags=walk.flags))
                if walk.mode == PHYSICAL:
                    _point_step(walk, None, INFINITY_ID, None, None)
                    consumed = _point_sync(walk) or consumed
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
            if rehit:
                # the departure point: the beam's own p when fresh, else the known
                # point the departure tagged (a push's or, since Stage 106, a pop's)
                known_pt = beam.p if fresh else next((k[0] for k in walk.known if k[2] == depart and k[1] == t0), None)
            else:
                known_pt = snapped.get((sid, t))
            pt_exact = known_pt is not None and any(k[0] == known_pt for k in walk.known)
            pt, on_seg, side, full_block = classify(walk, beam, t, surf, point=known_pt)
            if surf.is_target and on_seg:
                walk.targets.add(sid)
            do = None
            changes = surf.state_changes(side, walk.flags)
            fires_state = False
            ghosted = surf.ghosted(side, walk.flags)
            fired = False
            if walk.mode == PHYSICAL:
                do = physical_decision(walk, surf, side, full_block)
                fires_state = bool(changes) and full_block
                fired = full_block and not ghosted
                # B1 (Stage 110): what counts is the frame the event leaves -- the beam
                # and the flags. A state change that rewrites the flags as they are
                # changes no frame.
                changes_flags = fires_state and apply_states(walk.flags, changes) != walk.flags
                if do in TRANSFORMATIVE or do in PROJECTIVE:
                    _plan_bookkeeping(walk, sid, pt)
                elif changes_flags:
                    # B5: a flag change keeps the beam -- matched by the point's frame
                    _plan_bookkeeping(walk, sid, pt, defer=walk.point)
                elif do is None and _quiet_entry(walk, surf, sid, side, pt):
                    # the planned walk consumes this entry without a frame change (a
                    # pass side, a terminal crossed off its segment, a write of the
                    # current value): so does the physical one, at the same event
                    walk.plan_index += 1
            elif walk.plan_index < len(walk.plan) and walk.plan[walk.plan_index] == (sid, side):
                # a planned piece (Stage 108): a hit outside its span is off its segment
                on_seg = on_seg and in_planned_span(walk, walk.plan_index, pt)
                walk.plan_index += 1
                eff = surf.effect(side, walk.flags)
                do = eff if (eff in TRANSFORMATIVE or eff in PROJECTIVE) else None
                fires_state = bool(changes)
                fired = not ghosted
            walk.flags_before = walk.flags
            if fires_state:
                walk.flags = apply_states(walk.flags, changes)
                fired_state = True
            ev = Event("hit", sid, side, do is not None, do == "terminal", on_seg, pt, t, beam, rehit,
                       fires_state, walk.flags, fired)
            walk.events.append(ev)
            if walk.mode == PHYSICAL and walk.point is not None:
                if do in TRANSFORMATIVE or do in PROJECTIVE:
                    _point_close(walk)
                else:
                    _point_step(walk, surf, sid, side, pt)
                    _point_sync(walk)
            if do == "terminal":
                walk.done = True
                return
            if do in TRANSFORMATIVE:
                depth = len(walk.stack)
                beam, depart, t0, fresh = _apply(walk, beam, t, surf, do, depart, fresh, t0, rehit, pt, pt_exact)
                ev.popped = len(walk.stack) < depth
                # the beam departs from Q: an effect fired at Q (a pinned crossing,
                # or the departure re-hit when the beam had departed from Q)
                walk.depart_q = walk.pole is not None and ((sid, t) in pinned or (rehit and walk.depart_q))
                applied = True
                break
            if do in PROJECTIVE:
                ev.projective = True
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
                walk.depart_q = False
                if planned_exit is not None:
                    walk.line = planned_exit
                    walk.gc = gc_from_line(planned_exit)
                    _add_line_endpoints(walk, beam)
                depart, t0, fresh = sid, mp.mpf(0), True
                applied = True
                break
            _late_cursor(walk, beam, cands, ci, key, t0)
        if walk.mode == PHYSICAL:
            _point_close(walk)
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
            and walk.plan_matched and not (walk.point is not None and walk.point.pending) \
            and not any(c[1] == 0 for c in cands):
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
    walk.Q = None
    walk.pole = None
    walk.known = [(pt, mp.mpf(0), surf.id)] if pt_exact else []
    walk.line = None
    walk.gc = None
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
        walk.gc = gc_from_line(walk.line)
        _add_line_endpoints(walk, nb)
    return nb


def in_planned_span(walk: Walk, i, pt):
    """Stage 108: plan entry i names a piece -- pt must lie in its span."""
    span = walk.plan_spans[i] if walk.plan_spans is not None and i < len(walk.plan_spans) else None
    return span is None or on_segment(span, pt)


def _quiet_entry(walk: Walk, surf: Surface, sid, side, pt):
    """B1 (Stage 110): the next plan entry is (sid, side) at pt, and the planned
    walk consumes it there without changing the frame: its effect is not
    transformative or projective, and its state changes leave the flags as
    they are."""
    if walk.plan_index >= len(walk.plan) or walk.plan[walk.plan_index] != (sid, side):
        return False
    if not in_planned_span(walk, walk.plan_index, pt):
        return False
    eff = surf.effect(side, walk.flags)
    if eff in TRANSFORMATIVE or eff in PROJECTIVE:
        return False
    return apply_states(walk.flags, surf.state_changes(side, walk.flags)) == walk.flags


def _plan_bookkeeping(walk: Walk, sid, pt, defer=None):
    """defer: the point (B5) when the event keeps the beam and only changes the
    flags -- a mismatch then waits for the point's frame (_point_sync)."""
    if walk.plan_index < len(walk.plan) and sid == walk.plan[walk.plan_index][0] \
            and in_planned_span(walk, walk.plan_index, pt):
        walk.plan_index += 1
        return
    if defer is not None:
        defer.pending = True
        return
    walk.plan_matched = False
    walk.on_plan = False      # a surface fired out of plan: off the planned beams


def _point_open(walk: Walk, key, rehit):
    walk.point = _Point(key, rehit, dict(walk.flags), walk.plan_index)


def _point_close(walk: Walk):
    p = walk.point
    walk.point = None
    if p is not None and p.pending:
        walk.plan_matched = False
        walk.on_plan = False


def _point_step(walk: Walk, surf, sid, side, pt):
    """The planned walk's decision at this candidate, on the shadow: the next
    entry is consumed (its state changes applied under the shadow's flags);
    a transformative or projective entry ends the shadow."""
    p = walk.point
    if p is None or p.broke or p.index >= len(walk.plan):
        return
    if sid == INFINITY_ID:
        if walk.plan[p.index][0] == INFINITY_ID:
            p.index += 1
        return
    if walk.plan[p.index] != (sid, side) or not in_planned_span(walk, p.index, pt):
        return
    eff = surf.effect(side, p.flags)
    if eff in TRANSFORMATIVE or eff in PROJECTIVE:
        p.broke = True
        return
    p.flags = apply_states(p.flags, surf.state_changes(side, p.flags))
    p.index += 1


def _point_sync(walk: Walk):
    """A pending point resolves when the walk is in the shadow's frame."""
    p = walk.point
    if p is not None and p.pending and not p.broke and p.flags == walk.flags:
        walk.plan_index = p.index
        p.pending = False
        if B5_HOOK is not None:
            B5_HOOK(walk)
        return True
    return False


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


def _tag_departure(known, q, t_res, sid, pt_exact, portal):
    """Stage 106: the pop point is the restored beam's departure point, tagged
    like a push's. A known point that is the pop point -- by data (pt_exact and
    equal to q) or by its parameter on this beam (t == t_res: the same beam and
    the same parameter are the same point) -- gets skip = sid and t = t_res; an
    exact pop point not yet known is added (not for a portal: its mapped-back
    point is a rounded computation). Without the tag a known point at the pop
    point snaps the departing carrier's other root onto it (K2)."""
    out = []
    found = False
    for k in known:
        if (pt_exact and k[0] == q) or k[1] == t_res:
            out.append((k[0], t_res, sid))
            found = True
        else:
            out.append(k)
    if not found and pt_exact and not portal:
        out.append((q, t_res, sid))
    return out


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
        walk.known = _tag_departure(top.known, q, t_res, top.sid, pt_exact, src.portal() is not None)
        walk.line = top.line
        walk.gc = top.gc
        walk.Q = top.q
        walk.pole = top.pole
        return top.beam, top.sid, t_res, False
    other = _other_root(beam, surf, t, fresh, depart, t0, pt)
    portal = surf.portal()
    pop_id = portal[2] if portal is not None else surf.id
    walk.stack.append(StackEntry(beam, surf.id, t, other, walk.known, walk.line, pop_id, map_kind(surf, etype), walk.Q,
                                 walk.pole, walk.gc))
    walk.pole = _pole_apply(walk.pole, walk.Q, walk.line, surf)
    walk.Q = map_point(walk.Q, surf)
    nb = apply_effect(beam, t, surf, etype, pt)
    old_known = walk.known
    if portal is not None:
        _portal_known(walk, surf, pt, pt_exact, old_known, nb)
        return nb, pop_id, mp.mpf(0), True
    walk.known = [(pt, mp.mpf(0), surf.id)] if pt_exact else []
    walk.known += [(k, nb.param_of(k), None) for k, _, _ in old_known if k != pt and incident(k, surf)]
    if walk.line is not None and surf.seg.kind == "line":
        walk.line = reflect_line_data(walk.line, surf.seg)
        walk.gc = gc_from_line(walk.line)
        _add_line_endpoints(walk, nb)
    elif walk.line is not None and _line_through_center(walk.line, surf.seg):
        nb = Beam(nb.p, nb.tau, mp.mpf(0))
        walk.known = [k for k in walk.known if k[2] == surf.id]
        _add_line_endpoints(walk, nb)
    else:
        walk.line = None
        if walk.gc is not None:
            walk.gc = gc_reflect(walk.gc, surf.seg) if surf.seg.kind == "line" else gc_invert(walk.gc, surf.seg)
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
        walk.gc = gc_from_line(walk.line)
        _add_line_endpoints(walk, nb)
    else:
        walk.line = None
        walk.gc = gc_translate(walk.gc, cfg["d"][0], cfg["d"][1]) if walk.gc is not None and cfg["theta"] == 0 else None
    return True


WALK_SURFACES = []
POLE_HOOK = None    # tests only: called with (walk, beam) at every loop of a beam
B5_HOOK = None      # tests and measurements only: called with (walk) at every B5 rescue


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


# ---------------------------------------------------------------- the exact pole (Stage 105)
#
# Every beam of an epoch is the image F(l) of a straight line l under the
# frame's maps; it passes Q = F(inf), with the direction there the image of
# l's direction. The record (Qx, Qy, Dx, Dy) keeps both exactly, so a beam
# tangent to a carrier at Q is a touch, decided without rounding.

def _cmul(a, b):
    return (a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0])


def _conj(a):
    return (a[0], -a[1])


def _pole_ok(Q, D):
    """The record, or None when the direction degenerates."""
    if D[0] == 0 and D[1] == 0:
        return None
    return (Q[0], Q[1], D[0], D[1])


def _pole_reflect(pole, seg):
    S, E = FC(seg.S), FC(seg.E)
    M = _sub(E, S)
    mm = M[0] * M[0] + M[1] * M[1]
    M2 = _cmul(M, M)
    w = _conj(_sub((pole[0], pole[1]), S))
    r = _cmul(M2, w)
    Q = (S[0] + r[0] / mm, S[1] + r[1] / mm)
    return _pole_ok(Q, _cmul(M2, _conj((pole[2], pole[3]))))


def _pole_invert(pole, seg):
    """Inversion of the record; None when Q is the center (Q goes to inf)."""
    nx, ny, w, rr = _center_h(seg)
    U = (pole[0] - nx / w, pole[1] - ny / w)
    uu = U[0] * U[0] + U[1] * U[1]
    if uu == 0:
        return None
    Q = (nx / w + rr * U[0] / uu, ny / w + rr * U[1] / uu)
    return _pole_ok(Q, _cmul(_conj((pole[2], pole[3])), _cmul(U, U)))


def _pole_from_line(line, seg):
    """A straight beam (Q at inf) inverted: Q becomes the center, the
    direction there the line's."""
    a, b, _c = line
    nx, ny, w, _rr = _center_h(seg)
    return _pole_ok((nx / w, ny / w), (b, -a))


def _pole_apply(pole, q_before, line, surf):
    """The record after surf's map; q_before: the approximate Q before it
    (None: at infinity)."""
    portal = surf.portal()
    if portal is not None:
        cfg = surf.left if surf.left and surf.left.get("type") == "portal" else surf.right
        if pole is None or cfg["theta"] != 0:
            return None
        d = (F(cfg["d"][0]), F(cfg["d"][1]))
        return (pole[0] + d[0], pole[1] + d[1], pole[2], pole[3])
    if surf.seg.kind == "line":
        return None if pole is None else _pole_reflect(pole, surf.seg)
    if q_before is None:
        return None if line is None else _pole_from_line(line, surf.seg)
    return None if pole is None else _pole_invert(pole, surf.seg)


def _pole_on(pole, seg):
    """Exact: seg's carrier passes the frame's infinity point Q."""
    Q = (pole[0], pole[1])
    if seg.kind == "line":
        S, E = FC(seg.S), FC(seg.E)
        return _cross(_sub(E, S), _sub(Q, S)) == 0
    nx, ny, w, rr = _center_h(seg)
    U = (Q[0] - nx / w, Q[1] - ny / w)
    return U[0] * U[0] + U[1] * U[1] == rr


def _pole_tangent(pole, seg):
    """Exact: seg's carrier passes Q and is tangent there to the beam."""
    if not _pole_on(pole, seg):
        return False
    D = (pole[2], pole[3])
    if seg.kind == "line":
        S, E = FC(seg.S), FC(seg.E)
        return _cross(D, _sub(E, S)) == 0
    nx, ny, w, _rr = _center_h(seg)
    U = (pole[0] - nx / w, pole[1] - ny / w)
    return D[0] * U[0] + D[1] * U[1] == 0


def pre_q_exact(surfaces):
    """Q0 of the pre-transform as exact rationals (None: infinity)."""
    Q = None
    for s in surfaces:
        if not s.pre_transform:
            continue
        nx, ny, w, rr = _center_h(s.seg)
        if Q is None:
            Q = (nx / w, ny / w)
            continue
        U = (Q[0] - nx / w, Q[1] - ny / w)
        uu = U[0] * U[0] + U[1] * U[1]
        Q = None if uu == 0 else (nx / w + rr * U[0] / uu, ny / w + rr * U[1] / uu)
    return Q


def pole_start_direction(P0, d, Q0):
    """A direction shot on the circle through Q0 tangent to d at P0: the
    direction at Q0 is conj(d) (P0 - Q0)^2 (invert about Q0)."""
    W = _sub(P0, Q0)
    return _pole_ok(Q0, _cmul(_conj(d), _cmul(W, W)))


def pole_start_aimed(P0, X, Q0):
    """The circle through P0, X, Q0: the direction at Q0 is
    conj(P0 - X) (X - Q0) (P0 - Q0)."""
    return _pole_ok(Q0, _cmul(_conj(_sub(P0, X)), _cmul(_sub(X, Q0), _sub(P0, Q0))))


# ---------------------------------------------------------------- entry points

def plan_resolved(surfaces, plan, flags=None):
    """Forward state simulation: (effect, flags before the entry) per entry."""
    by_id = {s.id: s for s in surfaces}
    f = dict(flags or {})
    out = []
    for sid, side in plan:
        surf = by_id.get(sid)
        if surf is None or not surf.present(f):
            out.append((None, f))
            continue
        out.append((surf.effect(side, f), f))
        f = apply_states(f, surf.state_changes(side, f))
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
        if surf is None or not surf.present(f):
            out.append(None)
            continue
        out.append(surf.effect(side, f))
        f = apply_states(f, surf.state_changes(side, f))
    return out


def trace_physical_direction(surfaces, player, direction, plan=(), cursor=None, flags=None):
    """Physical trace of an explicit direction (forward shooting reference)."""
    return trace_mode_direction(surfaces, player, direction, plan, cursor, PHYSICAL, flags=flags)


def trace_mode_direction(surfaces, player, direction, plan, cursor, mode, line="direction", flags=None,
                         extra_known=(), proj_hits=None, beam=None, exact_aim=None, spans=None):
    """line: the initial beam's exact implicit line; "direction": `direction`
    is exact data; None: not exact. beam: the initial beam itself (a scene
    with a pre-transform: the circle through Q0); else the straight beam
    along `direction`, or the circle through Q0 tangent to it."""
    walk = Walk(hittable(surfaces), list(plan), cursor, mode, PHYSICAL)
    walk.plan_spans = spans
    walk.flags = dict(flags or {})
    walk.flags_before = walk.flags
    walk.proj_hits = proj_hits or {}
    walk.Q = pre_q(surfaces)
    p0 = P(player)
    # the exact pole record of a pre-transform frame (Stage 105): Q0 exact, the
    # direction there from the initial beam (an aimed shot needs the exact aim
    # image `exact_aim`; a direction shot its exact direction)
    q0e = pre_q_exact(surfaces)
    if q0e is not None:
        if beam is not None:
            walk.pole = None if exact_aim is None else pole_start_aimed(FC(p0), exact_aim, q0e)
        else:
            walk.pole = pole_start_direction(FC(p0), FC(direction), q0e)
    if beam is not None:
        b0 = beam
    elif walk.Q is None:
        b0 = Beam(p0, direction / abs(direction), mp.mpf(0))
    else:
        b0 = beam_through_tangent(p0, direction / abs(direction), walk.Q)
    if b0.kappa != 0:
        line = None             # a circle beam has no exact line
    walk.known = [(p0, mp.mpf(0), None)]
    walk.known += [(k, b0.param_of(k), None) for k in extra_known if k != p0]
    walk.line = line_from_dir(p0, direction) if line == "direction" else line
    walk.gc = gc_from_line(walk.line) if walk.line is not None else None
    if walk.gc is None and q0e is not None and beam is not None and exact_aim is not None:
        # K9: an aimed shot in a pre-transform frame: the circle through P, X0, Q0
        walk.gc = gc_through(FC(p0), exact_aim, q0e)
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


def first_entry(surfaces, plan, flags=None):
    """The index of the first planned entry of the first epoch that names a
    present surface (infinity entries and absent surfaces skipped; a pass
    side, a terminal, a projective count: the planned walk fires only on an id
    and side match), or None."""
    by_id = {s.id: s for s in surfaces}
    f = dict(flags or {})
    for j, (sid, side) in enumerate(plan):
        if sid == INFINITY_ID:
            continue
        surf = by_id.get(sid)
        if surf is None or not surf.present(f):
            continue
        return j
    return None


def _first_firing(beam, surf, side, tq, line=None):
    """(t, q) of the first crossing of surf's carrier from `side` along the
    beam from t = 0 in the walker's projective order (t > 0 ascending, the
    pole, t <= 0 ascending; at one parameter the hit before the pole), with q
    = the pole was passed first; None when no crossing fires the entry."""
    if line is not None and surf.seg.kind != "line" and _tangent(line, surf.seg):
        return None         # a tangent touch is not a crossing (exact)
    rs = [t for t in roots(beam, surf, False) if t != 0]
    if len(rs) == 2 and rs[0] == rs[1]:
        return None         # a double root: a touch
    cands = [(sort_key(t, mp.mpf(0)), 1, t) for t in rs]
    if tq is not None and tq != 0:
        cands.append((sort_key(tq, mp.mpf(0)), 3, tq))
    cands.sort(key=lambda c: (c[0], c[1]))
    q = False
    for key, kind, t in cands:
        if kind == 3:
            q = True
            continue
        if side_at(surf.seg, beam.at(t), beam.tangent_at(t)) == side:
            return t, q
    return None


def realization(surfaces, player, cursor, plan, flags=None, hints=None):
    """Stage 111: "direct" (B_0 runs P -> X_0 -> Q_0, toward the aim image) or
    "reversed" (the same circle the other way, reaching X_0 through Q_0). Let
    e be the first planned entry of the first epoch naming a present surface.
    R1: when only one orientation of B_0 crosses e's carrier from e's side
    (a straight B_0 and a line carrier, always), that one. R2: when both do,
    one before the passage of Q_0 and one after it, the one whose order
    matches the plan: Q_0 first iff an infinity entry precedes e. R3:
    otherwise (no e, no crossing, both orientations alike) today's rule --
    direct, reversed by an infinity entry in the first epoch (via_infinity).
    Infinity entries are otherwise only matched by the walker."""
    fallback = "reversed" if via_infinity(surfaces, plan, flags) else "direct"
    j = first_entry(surfaces, plan, flags)
    if j is None:
        return fallback
    d = exact_direction(surfaces, plan, player, cursor, flags, hints)
    if d is None:
        return fallback     # the aim image at infinity or the player itself (K3)
    q0 = pre_q(surfaces)
    p = P(player)
    if q0 is None:
        b = Beam(p, d / abs(d), mp.mpf(0))
        line = initial_line(surfaces, player, cursor, plan, flags)
    else:
        if q0 == p:
            return fallback     # the player at the frame's infinity: every aim degenerates
        b = beam_through(p, p + d, q0)
        line = None
    by_id = {s.id: s for s in surfaces}
    surf = by_id[plan[j][0]]
    side = plan[j][1]
    found = {}
    for name, bm in (("direct", b), ("reversed", reversed_beam(b))):
        tq = None if q0 is None else bm.param_of(q0)
        if q0 is None:
            tq = mp.inf
        r = _first_firing(bm, surf, side, tq, line)
        if r is not None:
            found[name] = r[1]
    if len(found) == 1:
        return next(iter(found))
    if len(found) == 0:
        return fallback
    # both orientations fire e (a circle carrier or a circle beam): the one
    # whose planned walk consumes the plan and reaches the cursor; when both or
    # neither do, the one that fires e before Q_0 -- after it when an infinity
    # entry precedes e; when both fire on the same side of Q_0, today's rule
    reaching = []
    for name in ("direct", "reversed"):
        w = _planned_oriented(surfaces, player, cursor, plan, flags, hints, name == "reversed")
        if any(e.kind == "cursor" for e in w.events):
            reaching.append(name)
    if len(reaching) == 1:
        return reaching[0]
    if found["direct"] != found["reversed"]:
        before = any(sid == INFINITY_ID for sid, _ in plan[:j])
        return "direct" if found["direct"] == before else "reversed"
    return fallback


def _planned_oriented(surfaces, player, cursor, plan, flags, hints, away):
    """The planned walk of the aim in the given orientation (no realization
    lookup: realization itself uses it)."""
    d, line, known, beam, xe = _aim_oriented(surfaces, player, cursor, plan, flags, hints, away)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PLANNED, line=line, flags=flags,
                                extra_known=known, proj_hits=projective_hits(surfaces, plan, cursor, flags, hints),
                                beam=beam, exact_aim=xe)


def _aim(surfaces, player, cursor, plan, flags=None, hints=None):
    """(direction, exact line or None, known points): the exact aim image's
    direction, or -- when the image is at infinity (aim undetermined) --
    straight at the cursor. When every frame change fixes the cursor, the aim
    image is the cursor: exact data the initial beam passes through (known).
    The orientation is the plan's realization (Stage 111)."""
    away = realization(surfaces, player, cursor, plan, flags, hints) == "reversed"
    return _aim_oriented(surfaces, player, cursor, plan, flags, hints, away)


def _aim_oriented(surfaces, player, cursor, plan, flags, hints, away):
    """_aim in a given orientation: away = the second realization, the same
    circle the other way round (P -> Q -> X)."""
    d = exact_direction(surfaces, plan, player, cursor, flags, hints)
    straight_at_cursor = d is None
    if d is None:
        d = P(cursor) - P(player)
        line, known = "direction", []
    else:
        line = initial_line(surfaces, player, cursor, plan, flags)
        known = [P(cursor)] if plan and fixes_cursor(surfaces, cursor, plan, flags) else []
    q0 = pre_q(surfaces)
    if q0 is None:
        return (-d if away else d), line, known, None, None
    # a pre-transform: the circle through the player, the aim point and Q0;
    # the exact aim image for the exact pole record (None when not exact)
    b = beam_through(P(player), P(player) + d, q0)
    # the exact aim image for the exact pole record; a shot straight at the cursor
    # (the image at infinity or the player itself, K3) passes the cursor: exact data
    if straight_at_cursor:
        xe = FC(P(cursor))
    else:
        xe = exact_image(surfaces, cursor, plan, flags) if line != "direction" else None
    return d, None, known, (reversed_beam(b) if away else b), xe


def plan_spans_of(spans):
    """Plan entries' piece spans ({start, end, via} or None) as segments."""
    if spans is None:
        return None
    # on_segment reads only the span's three points (the carrier is the surface's)
    return [None if sp is None else Seg(-1, P(sp["start"]), P(sp["end"]), P(sp["via"])) for sp in spans]


def planned(surfaces, player, cursor, plan, flags=None, hints=None, spans=None):
    """Planned trace = forward PLANNED-mode walk of the exact aim direction.
    hints: plan entry index -> the point the player clicked (settles forks)."""
    d, line, known, beam, xe = _aim(surfaces, player, cursor, plan, flags, hints)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PLANNED, line=line, flags=flags,
                                extra_known=known, proj_hits=projective_hits(surfaces, plan, cursor, flags, hints),
                                beam=beam, exact_aim=xe, spans=plan_spans_of(spans))


def physical(surfaces, player, cursor, plan, flags=None, hints=None, spans=None):
    d, line, known, beam, xe = _aim(surfaces, player, cursor, plan, flags, hints)
    return trace_mode_direction(surfaces, player, d, plan, P(cursor), PHYSICAL, line=line, flags=flags,
                                extra_known=known, proj_hits=projective_hits(surfaces, plan, cursor, flags, hints),
                                beam=beam, exact_aim=xe, spans=plan_spans_of(spans))
