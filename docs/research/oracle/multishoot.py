"""S5b: multiple-shooting Newton refinement of the planned path.

Unknowns (2K): for each applied hit j=1..K, the angle phi_j of h_j on S_j and
the outgoing tangent angle alpha_j of beam B_j at h_j.
B_0 is the line player -> h_1. B_j passes through h_j with tangent alpha_j and
through h_{j+1} (or the cursor for j=K), which fixes its curvature:
    kappa_j = 2 cross(tau_j, w) / |w|^2,  w = next - h_j
Residuals at each h_j (all local, dimensionless):
    E1_j = Im(conj(tau_refl) * tau_j)          tangent reflection law
    E2_j = R_j * (kappa_j + kappa_{j-1} + 2 cross(tau_in, h_j - c)/R^2)   curvature law
tau_in = tangent of B_{j-1} at h_j = u^2 conj(tau_{j-1}),  u = unit chord.
Pass events are not unknowns: they do not change the beam.
"""
from __future__ import annotations

import mpmath as mp

from beam import cross


def unpack(x, plan, by_id):
    K = len(plan)
    hs, taus = [], []
    for j in range(K):
        S = by_id[plan[j][0]]
        hs.append(S.c + S.R * mp.expj(x[2 * j]))
        taus.append(mp.expj(x[2 * j + 1]))
    return hs, taus


def residuals(x, plan, by_id, player, cursor):
    K = len(plan)
    hs, taus = unpack(x, plan, by_id)
    pts = [player] + hs + [cursor]
    # beam 0: line player -> h1
    w0 = hs[0] - player
    tau_prev = w0 / abs(w0)
    kappa_prev = mp.mpf(0)
    start_prev = player
    out = []
    for j in range(K):
        S = by_id[plan[j][0]]
        h = hs[j]
        w = h - start_prev
        u = w / abs(w)
        tau_in = u * u * mp.conj(tau_prev)
        nu = (h - S.c) / S.R
        tau_refl = -mp.conj(tau_in) * nu * nu
        tau_j = taus[j]
        nxt = pts[j + 2]
        wn = nxt - h
        kappa_j = 2 * cross(tau_j, wn) / abs(wn) ** 2
        out.append((tau_refl.conjugate() * tau_j).imag)
        out.append(S.R * (kappa_j + kappa_prev + 2 * cross(tau_in, h - S.c) / S.R ** 2))
        tau_prev, kappa_prev, start_prev = tau_j, kappa_j, h
    return out


def x_from_events(events, plan, by_id, beams):
    """Initial unknowns from an approximate planned trace (applied hits)."""
    x = []
    applied = [(e, B) for e, B in zip(events, beams) if e.kind == "hit" and e.applied]
    from beam import invert_beam
    for (e, B), (sid, _) in zip(applied, plan):
        S = by_id[sid]
        x.append(mp.arg(e.point - S.c))
        nb = invert_beam(B, e.t, S)
        x.append(mp.arg(nb.tau))
    return x


def newton(x0, plan, by_id, player, cursor, iters=12, fd_rel=None):
    x = [mp.mpf(v) for v in x0]
    n = len(x)
    hist = []
    h = fd_rel if fd_rel is not None else mp.sqrt(mp.eps)
    for _ in range(iters):
        r = residuals(x, plan, by_id, player, cursor)
        hist.append(float(max(abs(v) for v in r)))
        if hist[-1] == 0:
            break
        Jm = mp.matrix(n, n)
        for k in range(n):
            xk = list(x)
            xk[k] += h
            rk = residuals(xk, plan, by_id, player, cursor)
            for i in range(n):
                Jm[i, k] = (rk[i] - r[i]) / h
        try:
            dx = mp.lu_solve(Jm, mp.matrix(r))
        except ZeroDivisionError:
            break
        x = [x[i] - dx[i] for i in range(n)]
    return x, hist
