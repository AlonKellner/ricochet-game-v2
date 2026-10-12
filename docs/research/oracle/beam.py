"""
Arbitrary-precision ground-truth oracle for inversion-chain trajectories.

Model (equivalent to Tracer's normalized-frame semantics for scenes made of
full inversion circles plus pass-through walls):

  The visual beam is a generalized circle. At every applied hit on a surface
  circle S = (c, R) the beam is replaced by its inversion in S. Since S is
  fixed pointwise by the inversion, the beam stays continuous at the hit.

Beam state: point p, unit tangent tau, signed curvature kappa (w.r.t. the
normal i*tau, complex coordinates z = x + i*y; Godot's y-down only mirrors the
picture, the algebra is unchanged).

Local parametrization (projective, wraps through infinity like Tracer's t):
    z(t) = p + t*tau / (1 - i*kappa*t/2)
t = 0 is p, t -> +inf continues forward, wraps to -inf, and returns to 0-.

Inversion of the beam at h on S (derived in docs/precision_benchmark_findings.md):
    tau'   = -conj(tau_h) * ((h - c)/R)^2        (mirror reflection)
    kappa' = -kappa - 2*cross(tau_h, h - c)/R^2
"""

from __future__ import annotations

import mpmath as mp
from dataclasses import dataclass, field

LEFT = "LEFT"
RIGHT = "RIGHT"


def cross(a, b):
    return a.real * b.imag - a.imag * b.real


def dot(a, b):
    return a.real * b.real + a.imag * b.imag


@dataclass
class Circle:
    id: int
    c: mp.mpc
    R: mp.mpf


@dataclass
class Beam:
    p: mp.mpc
    tau: mp.mpc
    kappa: mp.mpf

    def at(self, t):
        if mp.isinf(t):
            # t = infinity: the antipode of p on a circle beam
            return self.p + 2j * self.tau / self.kappa
        return self.p + t * self.tau / (1 - 1j * self.kappa * t / 2)

    def tangent_at(self, t):
        if mp.isinf(t):
            return -self.tau
        f = 1 + 1j * self.kappa * t / 2
        v = self.tau * f * f
        return v / abs(v)

    def param_of(self, z):
        """Inverse of at(): real t of the (projection of) z onto the beam."""
        w = z - self.p
        den = self.tau + 1j * self.kappa * w / 2
        if den == 0:
            return mp.inf
        return (w / den).real


def invert_point(z, circ: Circle):
    u = z - circ.c
    return circ.c + circ.R ** 2 / mp.conj(u)


def circle_hits(beam: Beam, circ: Circle, depart_on: bool):
    """Real roots t of beam ∩ circle. If depart_on, p lies on the circle and the
    t = 0 root is dropped (Tracer's origin_on_surface)."""
    q = circ.c - beam.p
    tq = dot(q, beam.tau)
    k = beam.kappa
    if depart_on:
        P = mp.mpf(0)
    else:
        P = (abs(q) - circ.R) * (abs(q) + circ.R)
    a2 = 1 - k * cross(beam.tau, q) + k * k * P / 4
    roots = []
    if depart_on:
        # a2 t^2 - 2 tq t = 0 -> t = 2 tq / a2
        if a2 != 0:
            roots.append(2 * tq / a2)
        elif k != 0:
            roots.append(mp.inf)        # the other crossing is the antipode
        return roots
    disc = tq * tq - a2 * P
    if disc < 0:
        return roots
    s = mp.sqrt(disc)
    # numerically stable pair: t1 = (tq + sgn*s)/a2, t2 = P/(tq + sgn*s)
    big = tq + (s if tq >= 0 else -s)
    if big != 0:
        roots.append(P / big)
        if a2 != 0:
            roots.append(big / a2)
        elif k != 0:
            roots.append(mp.inf)        # the other crossing is the antipode
    elif a2 != 0:
        roots.append(mp.mpf(0))
    return roots


def sort_key(t):
    return (0, t) if t > 0 else (1, t)


def side_of(beam: Beam, t, circ: Circle):
    h = beam.at(t)
    d = beam.tangent_at(t)
    return LEFT if dot(d, h - circ.c) < 0 else RIGHT


def invert_beam(beam: Beam, t, circ: Circle) -> Beam:
    h = beam.at(t)
    tau_h = beam.tangent_at(t)
    u = h - circ.c
    nu = u / circ.R
    tau2 = -mp.conj(tau_h) * nu * nu
    tau2 = tau2 / abs(tau2)
    k2 = -beam.kappa - 2 * cross(tau_h, u) / (circ.R ** 2)
    return Beam(h, tau2, k2)


@dataclass
class Event:
    kind: str           # "hit" | "cursor" | "origin" | "escape" | "max"
    surface_id: int = -1
    side: str = ""
    point: mp.mpc = None
    applied: bool = False
    t: mp.mpf = None


@dataclass
class TraceResult:
    events: list = field(default_factory=list)
    beams: list = field(default_factory=list)   # beam in force before each event


PHYSICAL = 0
PLANNED = 1


def trace(circles, player, direction, plan=(), cursor=None, mode=PHYSICAL,
          post_cursor_mode=PHYSICAL, max_events=40):
    beam = Beam(mp.mpc(player), direction / abs(direction), mp.mpf(0))
    return trace_from(circles, beam, None, plan, cursor, mode, post_cursor_mode,
                      max_events)


def trace_from(circles, beam, depart, plan=(), cursor=None, mode=PHYSICAL,
               post_cursor_mode=PHYSICAL, max_events=40, plan_index=0,
               plan_matched=True, cursor_done=None):
    """Forward trace. `direction` is a complex number (any length).
    plan: list of (surface_id, side). Mirrors Tracer gating:
      PHYSICAL: every inversion hit applies; plan_index advances on id match.
      PLANNED : only the next plan entry (id AND side) applies; others pass.
      Cursor becomes reachable once plan_index == len(plan) and (PHYSICAL)
      plan_matched.
    """
    res = TraceResult()
    if cursor_done is None:
        cursor_done = cursor is None
    cur_mode = mode
    while len(res.events) < max_events:
        cands = []
        for c in circles:
            for t in circle_hits(beam, c, depart == c.id):
                if t == 0:
                    continue
                cands.append((sort_key(t), t, c))
        cursor_t = None
        if not cursor_done and plan_index >= len(plan) and plan_matched:
            cursor_t = beam.param_of(mp.mpc(cursor))
        cands.sort(key=lambda x: x[0])
        if cursor_t is not None:
            # insert cursor as a pseudo hit
            cands.append((sort_key(cursor_t), cursor_t, None))
            cands.sort(key=lambda x: x[0])
        applied_any = False
        for _, t, c in cands:
            if len(res.events) >= max_events:
                break
            if c is None:
                res.beams.append(beam)
                res.events.append(Event("cursor", point=beam.at(t), t=t))
                cursor_done = True
                cur_mode = post_cursor_mode
                # re-assemble from cursor point (Tracer continues the walk)
                continue
            side = side_of(beam, t, c)
            apply = False
            if cur_mode == PHYSICAL:
                apply = True
                if plan_index < len(plan):
                    if c.id == plan[plan_index][0]:
                        plan_index += 1
                    else:
                        plan_matched = False
                else:
                    plan_matched = False
            else:
                if plan_index < len(plan) and c.id == plan[plan_index][0] \
                        and side == plan[plan_index][1]:
                    apply = True
                    plan_index += 1
            res.beams.append(beam)
            res.events.append(Event("hit", c.id, side, beam.at(t), apply, t))
            if apply:
                beam = invert_beam(beam, t, c)
                depart = c.id
                applied_any = True
                break
        if not applied_any:
            # walked the whole beam back to the origin (or to infinity on a line)
            if len(res.events) < max_events:
                kind = "escape" if beam.kappa == 0 else "origin"
                res.beams.append(beam)
                res.events.append(Event(kind, point=beam.p))
            break
    return res


def aim_image(circles, plan, cursor):
    by_id = {c.id: c for c in circles}
    img = mp.mpc(cursor)
    for sid, _side in reversed(plan):
        img = invert_point(img, by_id[sid])
    return img


def exact_direction(circles, plan, player, cursor):
    return aim_image(circles, plan, cursor) - mp.mpc(player)


def hit_sensitivity(circles, player, direction, n_events, plan=(), cursor=None,
                    mode=PHYSICAL, delta=None):
    """|d h_k / d theta| per event via a tiny rotation of the direction
    (finite difference at working precision, delta far below any rounding we
    care about)."""
    if delta is None:
        delta = mp.mpf(10) ** (-(mp.mp.dps // 2))
    base = trace(circles, player, direction, plan, cursor, mode, max_events=n_events)
    rot = direction * mp.expj(delta)
    pert = trace(circles, player, rot, plan, cursor, mode, max_events=n_events)
    out = []
    for a, b in zip(base.events, pert.events):
        if a.point is None or b.point is None or a.kind != b.kind or a.surface_id != b.surface_id:
            out.append(None)
            continue
        out.append(abs(b.point - a.point) / delta)
    return out
