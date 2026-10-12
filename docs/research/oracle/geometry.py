"""Oracle v2 geometry: segments (line / arc / full circle) against beams.

Mirrors the game's definitions exactly (scripts/math/intersection.gd,
scripts/math/segment.gd), evaluated in mpmath at arbitrary precision:

  - Carrier implicit function f with gradient ∇f. Any nonzero scaling of f
    leaves every decision below unchanged.
  - Side (Intersection._determine_side): approach = -dir·∇f(P),
    winding = -(E - S) × ∇f(V); LEFT iff (winding >= 0) == (approach > 0).
  - On-segment (Intersection.is_on_segment): Re(CR(S,P;E,V)) >= 0 with
    homogeneous points (V may be the point at infinity).
  - Endpoint blocked sides (Intersection.endpoint_blocked_sides).

Beam ∩ line carrier, with f(z) = g·z + f0 and g = ∇f:
    f(p)(1 + κ²t²/4) + (g·τ) t + (κ/2) cross(τ, g) t² = 0
Beam ∩ circle carrier: beam.circle_hits.
"""

from __future__ import annotations

from dataclasses import dataclass

import mpmath as mp

from beam import Beam, Circle, circle_hits, cross, dot

LEFT = "LEFT"
RIGHT = "RIGHT"


def P(z):
    """Point from mpc / complex / (x, y)."""
    if isinstance(z, (tuple, list)):
        return mp.mpc(mp.mpf(z[0]), mp.mpf(z[1]))
    return mp.mpc(z)


@dataclass
class Seg:
    id: int
    S: mp.mpc
    E: mp.mpc
    V: mp.mpc | None      # None = point at infinity (full line)
    full: bool = False
    kind: str = "line"    # "line" | "circle" (construction provenance)
    c: mp.mpc | None = None
    R2: mp.mpf | None = None

    @staticmethod
    def line(sid, S, E, full=False):
        S, E = P(S), P(E)
        return Seg(sid, S, E, None if full else (S + E) / 2, full, "line")

    @staticmethod
    def arc(sid, S, E, V):
        S, E, V = P(S), P(E), P(V)
        c = circumcenter(S, V, E)
        return Seg(sid, S, E, V, False, "circle", c, abs(S - c) ** 2)

    @staticmethod
    def full_circle(sid, c, r):
        c = P(c)
        r = mp.mpf(r)
        # Segment.full_from_carrier: S = c+(r,0), E = c+(-r/2,-0.866r), V = c+(-r/2, 0.866r)
        S = c + mp.mpc(r, 0)
        E = c + mp.mpc(-r / 2, -mp.mpf("0.866") * r)
        V = c + mp.mpc(-r / 2, mp.mpf("0.866") * r)
        return Seg(sid, S, E, V, True, "circle", c, r * r)

    def grad(self, z):
        if self.kind == "line":
            D = self.E - self.S
            return mp.mpc(-D.imag, D.real)
        return 2 * (z - self.c)

    def f(self, z):
        if self.kind == "line":
            return cross(self.E - self.S, z - self.S)
        return abs(z - self.c) ** 2 - self.R2


def circumcenter(A, B, C):
    """Center of the circle through A, B, C (exact in mp)."""
    ax, ay, bx, by, cx, cy = A.real, A.imag, B.real, B.imag, C.real, C.imag
    d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    a2, b2, c2 = ax * ax + ay * ay, bx * bx + by * by, cx * cx + cy * cy
    ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d
    uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d
    return mp.mpc(ux, uy)


def line_hits(beam: Beam, seg: Seg):
    g = seg.grad(beam.p)
    fp = seg.f(beam.p)
    k = beam.kappa
    c0 = fp
    c1 = dot(g, beam.tau)
    c2 = fp * k * k / 4 + (k / 2) * cross(beam.tau, g)
    if c2 == 0:
        # degree drop: a curved beam meets the line at its antipode (t = inf)
        at_inf = [mp.inf] if k != 0 else []
        return at_inf if c1 == 0 else [-c0 / c1] + at_inf
    disc = c1 * c1 - 4 * c2 * c0
    if disc < 0:
        return []
    s = mp.sqrt(disc)
    big = -(c1 + s) if c1 >= 0 else -(c1 - s)
    if big == 0:
        return [mp.mpf(0)]
    return [big / (2 * c2), (2 * c0) / big]


def seg_hits(beam: Beam, seg: Seg):
    if seg.kind == "line":
        return line_hits(beam, seg)
    return circle_hits(beam, Circle(seg.id, seg.c, mp.sqrt(seg.R2)), False)


def _hdet(zA, wA, zB, wB):
    return zA * wB - zB * wA


def on_segment(seg: Seg, P):
    if seg.full:
        return True
    if seg.V is None:
        zV, wV = mp.mpc(1, 0), 0
    else:
        zV, wV = seg.V, 1
    sv = _hdet(seg.S, 1, zV, wV)
    ep = _hdet(seg.E, 1, P, 1)
    sp = _hdet(seg.S, 1, P, 1)
    ev = _hdet(seg.E, 1, zV, wV)
    num = sv * ep
    den = sp * ev
    return (num.real * den.real + num.imag * den.imag) >= 0


def winding(seg: Seg):
    if seg.kind == "line":
        g = seg.grad(seg.S)
    else:
        g = seg.grad(seg.V)
    return -cross(seg.E - seg.S, g)


def side_at(seg: Seg, P, direction):
    approach = -dot(direction, seg.grad(P))
    w = winding(seg)
    if w >= 0:
        return LEFT if approach > 0 else RIGHT
    return RIGHT if approach > 0 else LEFT


def tangent_into_segment(seg: Seg, which):
    ep = seg.S if which == 1 else seg.E
    if seg.kind == "line":
        return (seg.E - seg.S) if which == 1 else (seg.S - seg.E)
    rad = ep - seg.c
    ccw = mp.mpc(-rad.imag, rad.real)
    cw = mp.mpc(rad.imag, -rad.real)
    ref = seg.V - ep
    return ccw if dot(ccw, ref) >= dot(cw, ref) else cw


def endpoint_blocked_sides(direction, seg: Seg, which):
    """[blocked_left, blocked_right] for a beam through endpoint `which`."""
    t = tangent_into_segment(seg, which)
    c = cross(direction, t)
    if c == 0:
        return [True, True]
    if c > 0:
        return [False, True]
    return [True, False]
