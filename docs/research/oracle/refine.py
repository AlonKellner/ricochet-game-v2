"""S5: single-shooting Newton refinement of the aim angle.

Residual r(theta) = signed distance of the cursor from the final planned beam,
evaluated by a forward planned trace at `res_bits`. theta is stored at
`theta_bits`. Derivative by finite difference at the residual precision.
Start: the f64 image-based direction (what the game computes today, in f64).
"""
from __future__ import annotations

import mpmath as mp

from beam import Beam, PLANNED, PHYSICAL, cross, trace, exact_direction


def final_beam(circles, player, theta, plan, cursor):
    d = mp.expj(theta)
    r = trace(circles, player, d, plan, None, PLANNED, PHYSICAL, max_events=80)
    applied = [e for e in r.events if e.kind == "hit" and e.applied]
    if len(applied) < len(plan):
        return None
    # beam in force after the last applied hit
    idx = max(i for i, e in enumerate(r.events) if e.kind == "hit" and e.applied)
    from beam import invert_beam
    by_id = {c.id: c for c in circles}
    e = r.events[idx]
    return invert_beam(r.beams[idx], e.t, by_id[e.surface_id])


def residual(circles, player, theta, plan, cursor):
    B = final_beam(circles, player, theta, plan, cursor)
    if B is None:
        return None
    w = cursor - B.p
    # implicit beam equation kappa|w|^2 - 2 n.w = 0 with n = i*tau -> distance proxy
    n = 1j * B.tau
    f = B.kappa * abs(w) ** 2 - 2 * (n.real * w.real + n.imag * w.imag)
    g = abs(2 * B.kappa * w - 2 * n)
    return f / g


def newton(circles, player, cursor, plan, theta_bits, res_bits, iters=12):
    with mp.workprec(53):
        d = exact_direction(circles, plan, player, cursor)
        theta = mp.arg(d)
    hist = []
    for _ in range(iters):
        with mp.workprec(res_bits):
            r = residual(circles, player, theta, plan, cursor)
            if r is None:
                hist.append(None)
                break
            h = mp.mpf(2) ** (-(theta_bits - 8))
            r2 = residual(circles, player, theta + h, plan, cursor)
            if r2 is None or r2 == r:
                hist.append(float(abs(r)))
                break
            step = r * h / (r2 - r)
        hist.append(float(abs(r)))
        with mp.workprec(theta_bits):
            theta = +(theta - step)
    return theta, hist
