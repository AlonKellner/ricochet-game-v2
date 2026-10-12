"""Strategy prototypes evaluated against the oracle at a chosen precision.

All strategies run under mp.workprec(bits): bits=53 emulates float64
(correctly rounded ops), 106 ~ double-double, larger = BigFloat tiers.

  forward   (S2/S3): aim direction from the image chain, then forward beam
                     tracing in visual space (inversion of the beam state).
  anchored  (S4)   : cursor-anchored construction. Each planned beam B_j is
                     the circle through P_j = F_j(player), X_j = F_j(image),
                     Q_j = F_j(inf), oriented P -> X -> Q; P_j - Q_j is tracked
                     as an exact difference. No state is propagated along the
                     chain, so errors do not accumulate.
"""

from __future__ import annotations

import mpmath as mp

from beam import (Beam, Event, PHYSICAL, PLANNED, TraceResult, circle_hits,
                  cross, dot, exact_direction, invert_beam, invert_point,
                  side_of, trace, trace_from)


def J(v):
    """Rotate by -90 degrees."""
    return -1j * v


def beam_through(P, X, Q):
    """Oriented generalized circle through P -> X -> Q, anchored at P.
    Q may be None (= infinity). Returns Beam(P, tau, kappa)."""
    if Q is None:
        a = X - P
        return Beam(P, a / abs(a), mp.mpf(0))
    a = X - P
    b = Q - P
    g = (abs(a) ** 2) * J(b) - (abs(b) ** 2) * J(a)
    tau = -1j * g / abs(g)
    ba = b - a
    kappa = 2 * cross(a, b) / (abs(a) * abs(b) * abs(ba))
    return Beam(P, tau, kappa)


def beam_through_delta(P, X, delta):
    """Same as beam_through(P, X, P - delta) but with Q - P = -delta exact."""
    a = X - P
    b = -delta
    g = (abs(a) ** 2) * J(b) - (abs(b) ** 2) * J(a)
    tau = -1j * g / abs(g)
    ba = b - a
    kappa = 2 * cross(a, b) / (abs(a) * abs(b) * abs(ba))
    return Beam(P, tau, kappa)


def frames_for_plan(by_id, plan):
    """Transform stack after each plan entry (pop on immediate repeat of the
    same surface, as Tracer does). Returns list of stack tuples, len K+1."""
    stack = []
    out = [tuple(stack)]
    for sid, _ in plan:
        if stack and stack[-1] == sid:
            stack.pop()
        else:
            stack.append(sid)
        out.append(tuple(stack))
    return out


def pole_chain(by_id, stacks, player):
    """For every stack: (P_j, Q_j or None, delta_j = P_j - Q_j)."""
    memo = {(): (mp.mpc(player), None, None)}

    def get(st):
        if st in memo:
            return memo[st]
        P0, Q0, d0 = get(st[:-1])
        c = by_id[st[-1]]
        R2 = c.R ** 2
        P1 = invert_point(P0, c)
        if Q0 is None:
            Q1 = c.c
            d1 = R2 / mp.conj(P0 - c.c)
        else:
            u = Q0 - c.c
            if u == 0:
                Q1, d1 = None, None
            else:
                Q1 = invert_point(Q0, c)
                d1 = -R2 * mp.conj(d0) / (mp.conj(P0 - c.c) * mp.conj(u))
                P1 = Q1 + d1
        memo[st] = (P1, Q1, d1)
        return memo[st]

    return [get(st) for st in stacks]


def image_chain(by_id, plan, cursor):
    """X_j = F_j(F_K^{-1}(cursor)) for j = 0..K (X_K = cursor)."""
    K = len(plan)
    X = [None] * (K + 1)
    X[K] = mp.mpc(cursor)
    for j in range(K - 1, -1, -1):
        X[j] = invert_point(X[j + 1], by_id[plan[j][0]])
    return X


def roots_excluding_departure(B, S, prev_hit):
    """Roots of B ∩ S. If the beam departs from S, the root nearest the
    departure point IS the departure (provenance) and is dropped."""
    ts = circle_hits(B, S, False)
    if prev_hit is not None and prev_hit[0] == S.id and ts:
        k = min(range(len(ts)), key=lambda i: abs(B.at(ts[i]) - prev_hit[1]))
        ts = ts[:k] + ts[k + 1:]
    return ts


def progress_key(t, t0):
    """Projective order along a beam starting at parameter t0."""
    return (0, t) if t > t0 else (1, t)


def anchored_planned(circles, player, cursor, plan, post_events=5):
    by_id = {c.id: c for c in circles}
    K = len(plan)
    stacks = frames_for_plan(by_id, plan)
    chain = pole_chain(by_id, stacks, player)
    X = image_chain(by_id, plan, cursor)
    # memoize X by frame (exact provenance for pops)
    seen = {}
    for j in range(K, -1, -1):
        st = stacks[j]
        if st in seen:
            X[j] = seen[st]
        else:
            seen[st] = X[j]
    beams = []
    for j in range(K + 1):
        P, Q, d = chain[j]
        if Q is None:
            beams.append(beam_through(P, X[j], None))
        else:
            beams.append(beam_through_delta(P, X[j], d))

    res = TraceResult()
    t_start = mp.mpf(0)   # player is the anchor of B_0 -> t = 0
    prev_hit = None       # (surface, point) the beam departs from
    for j in range(K + 1):
        B = beams[j]
        if j < K:
            sid, side = plan[j]
            S = by_id[sid]
            ts = roots_excluding_departure(B, S, prev_hit)
            cand = [t for t in ts if side_of(B, t, S) == side]
            if not cand:
                return None
            # the matching-side root that comes first after t_start
            t_end = min(cand, key=lambda t: progress_key(t, t_start))
        else:
            t_end = B.param_of(mp.mpc(cursor))
        # pass events between t_start and t_end
        passes = []
        for c in circles:
            for t in roots_excluding_departure(B, c, prev_hit):
                if j < K and c.id == plan[j][0] and t == t_end:
                    continue
                if progress_key(t, t_start) < progress_key(t_end, t_start) and t != t_start:
                    passes.append((progress_key(t, t_start), t, c))
        passes.sort(key=lambda x: x[0])
        for _, t, c in passes:
            res.beams.append(B)
            res.events.append(Event("hit", c.id, side_of(B, t, c), B.at(t), False, t))
        res.beams.append(B)
        if j < K:
            h = B.at(t_end)
            res.events.append(Event("hit", plan[j][0], plan[j][1], h, True, t_end))
            prev_hit = (plan[j][0], h)
            t_start = beams[j + 1].param_of(h)
        else:
            res.events.append(Event("cursor", point=B.at(t_end), t=t_end))
            # post-cursor: physical continuation from the cursor state
            cur = Beam(B.at(t_end), B.tangent_at(t_end), B.kappa)
            depart = prev_hit[0] if prev_hit is not None else None
            post = trace_from(circles, cur, None, (), None, PHYSICAL, PHYSICAL,
                              max_events=post_events)
            res.events.extend(post.events)
            res.beams.extend(post.beams)
    return res


def anchored_physical(circles, player, cursor, plan, n_events=16):
    """Physical trace of the exact aim direction, built from the anchored
    planned path: identical up to the first pass event (physical applies it),
    then a forward physical continuation from that state."""
    planned = anchored_planned(circles, player, cursor, plan, post_events=n_events)
    if planned is None:
        return None
    res = TraceResult()
    plan_index = 0
    for e, B in zip(planned.events, planned.beams):
        if len(res.events) >= n_events:
            break
        if e.kind == "hit" and not e.applied:
            ev = Event("hit", e.surface_id, e.side, e.point, True, e.t)
            res.events.append(ev)
            res.beams.append(B)
            by_id = {c.id: c for c in circles}
            nb = invert_beam(B, e.t, by_id[e.surface_id])
            matched = plan_index < len(plan) and plan[plan_index][0] == e.surface_id
            rest = trace_from(circles, nb, e.surface_id, plan, cursor, PHYSICAL,
                              PHYSICAL, max_events=n_events - len(res.events),
                              plan_index=plan_index + (1 if matched else 0),
                              plan_matched=matched)
            res.events.extend(rest.events)
            return res
        res.events.append(e)
        res.beams.append(B)
        if e.kind == "hit":
            plan_index += 1
    return res


def forward_planned(circles, player, cursor, plan, n_events):
    d = exact_direction(circles, plan, mp.mpc(player), mp.mpc(cursor))
    return trace(circles, mp.mpc(player), d, plan, mp.mpc(cursor), PLANNED,
                 PHYSICAL, max_events=n_events)


def forward_physical(circles, player, cursor, plan, n_events):
    d = exact_direction(circles, plan, mp.mpc(player), mp.mpc(cursor))
    return trace(circles, mp.mpc(player), d, plan, mp.mpc(cursor), PHYSICAL,
                 PHYSICAL, max_events=n_events)
