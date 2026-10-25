"""The level data (data/levels/*.json, docs/LEVEL_FORMAT.md), through its
rc-level/1 lowering (bench/lowered/<dir>__<name>.json, `make lowered`: the
rc-level/2 elements expanded by the native reader), as scene defs: an independent Python builder of what the game builds from a level
(SurfaceSpec.build, RigidMotionEffect.create_portal_pair,
Surface.pre_transform_circle, SceneDefs.defs_from_surfaces), including the
float32 rounding of Vector2. Checked against the level dumps
(bench/scenes/*.surfaces.json) by test_oracle_v2.py; the native builder
(native/game, rcg::build_level) is checked against the same dumps.

Stage 115.2: replaces tscn_import.py (the levels were Godot scenes then).
"""

from __future__ import annotations

import json
import math
import os

import numpy as np

F32 = np.float32
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def f32(x) -> float:
    return float(F32(x))


def v2(x, y):
    return (f32(x), f32(y))


def v2_add(a, b):
    return (float(F32(a[0]) + F32(b[0])), float(F32(a[1]) + F32(b[1])))


def v2_normalized(v):
    x, y = F32(v[0]), F32(v[1])
    l2 = x * x + y * y
    if l2 == 0:
        return (0.0, 0.0)
    ln = np.sqrt(l2, dtype=F32)
    return (float(x / ln), float(y / ln))


def _det3x3(a11, a12, a13, a21, a22, a23, a31, a32, a33):
    return (a11 * (a22 * a33 - a23 * a32)
            - a12 * (a21 * a33 - a23 * a31)
            + a13 * (a21 * a32 - a22 * a31))


def _row(p):
    if p is None:
        return [1.0, 0.0, 0.0, 0.0]
    return [p[0] * p[0] + p[1] * p[1], p[0], p[1], 1.0]


def derive_carrier(s, e, v):
    """Segment.derive_carrier: (a, b, c, d) of a|z|^2 + bx + cy + d through start, via, end (float64)."""
    r1, r2, r3 = _row(s), _row(v), _row(e)
    return (_det3x3(r1[1], r1[2], r1[3], r2[1], r2[2], r2[3], r3[1], r3[2], r3[3]),
            -_det3x3(r1[0], r1[2], r1[3], r2[0], r2[2], r2[3], r3[0], r3[2], r3[3]),
            _det3x3(r1[0], r1[1], r1[3], r2[0], r2[1], r2[3], r3[0], r3[1], r3[3]),
            -_det3x3(r1[0], r1[1], r1[2], r2[0], r2[1], r2[2], r3[0], r3[1], r3[2]))


def derive_is_line(s, e, v):
    """Segment.derive_carrier(...).is_line()."""
    return derive_carrier(s, e, v)[0] == 0.0


def full_circle_carrier(cx, cy, r):
    """Exact float64 carrier of a full circle: center (from the float32 center)
    and radius, as GeneralizedCircle.from_circle -> center()/radius()."""
    ctr32 = v2(cx, cy)
    a = 1.0
    b = -2.0 * ctr32[0]
    c = -2.0 * ctr32[1]
    d = ctr32[0] * ctr32[0] + ctr32[1] * ctr32[1] - r * r
    return {"center": [-b / (2.0 * a), -c / (2.0 * a)],
            "radius": math.sqrt((b * b + c * c - 4.0 * a * d) / (4.0 * a * a))}


def full_circle_points(cx, cy, r):
    """Segment.full_from_carrier(GeneralizedCircle.from_circle(c, r)): start, end, via."""
    car = full_circle_carrier(cx, cy, r)
    ctr = v2(*car["center"])
    rad = car["radius"]
    p1 = v2_add(ctr, v2(rad, 0.0))
    p2 = v2_add(ctr, v2(-0.5 * rad, 0.866 * rad))
    p3 = v2_add(ctr, v2(-0.5 * rad, -0.866 * rad))
    return p1, p3, p2


def apply_motion(z, theta, d):
    """RigidMotionEffect.apply_motion: float64 arithmetic on the float32 point, rounded to float32."""
    c, s = math.cos(theta), math.sin(theta)
    return v2(c * z[0] - s * z[1] + d[0], s * z[0] + c * z[1] + d[1])


def _effect_def(e, theta=None, d=None, pair=None):
    """Effect.to_spec of the effect a side spec builds."""
    t = e.get("type", "pass")
    if t in ("pass", "terminal", "reflection", "inversion"):
        return {"type": t}
    if t in ("line_normal_projection", "circle_normal_projection"):
        return {"type": t, "back": bool(e.get("back", False))}
    if t in ("line_directional_projection", "circle_directional_projection"):
        n = e["normal"]
        return {"type": t, "back": bool(e.get("back", False)), "normal": list(v2_normalized(v2(n[0], n[1])))}
    assert t == "portal", t
    return {"type": "portal", "theta": theta, "d": list(d), "pair": pair}


def _writes(hits, host_id):
    """A side's in-shot writes, in order (HitEffect.in_shot_writes)."""
    out = []
    for h in hits:
        if h.get("deferred", False):
            continue
        k = h.get("kind")
        if k == "state":
            tog = h.get("op", "set") == "toggle"
            out.append({"key": h["key"], "op": "toggle" if tog else "set", "value": True if tog else h.get("value", True)})
            if h.get("when"):   # Stage 156: conditions
                out[-1]["when"] = [{"key": c["key"], "values": list(c["values"])} for c in h["when"]]
        elif k == "target":
            out.append({"key": "target:%d" % host_id, "op": "set", "value": True})
        elif k == "reset":
            out += [{"key": "chan:" + c, "op": "set", "value": "armed"} for c in h.get("channels", [])]
        elif k == "spawn":
            out.append({"key": "chan:" + h["channel"], "op": "set", "value": "pending"})
    return out


def _side_def(side, host_id, **portal):
    if side is None:
        return None
    d = _effect_def(side.get("effect", {"type": "pass"}), **portal)
    d["interactive"] = bool(side.get("interactive", False))
    w = _writes(side.get("hits", []), host_id)
    if w:
        d["states"] = w
    if any(h.get("deferred", False) or h.get("kind") in ("grapple", "pull") for h in side.get("hits", [])):
        d["landing"] = True      # (Stage 134: its hit changes the game at the landing; Stage 146: a journey)
    if side.get("rigid", False):
        d["rigid"] = True        # (Stage 156: frost never turns it)
    return d


def build_defs(level):
    """The scene defs of a level (its data as loaded), in build order."""
    out = []
    for spec in level["surfaces"]:
        ids = spec["ids"]
        if spec.get("pre_transform"):
            c = spec["circle"]
            s, e, v = full_circle_points(c["center"][0], c["center"][1], c["radius"])
            inv = {"type": "inversion", "interactive": False}
            out.append({"circle": full_circle_carrier(c["center"][0], c["center"][1], c["radius"]), "id": ids[0],
                        "start": list(s), "end": list(e), "via": list(v), "full": True, "line": False,
                        "left": inv, "right": dict(inv), "is_target": False, "player_solid": False,
                        "pre_transform": True})
            continue
        table = spec.get("resolver")
        sides = [spec.get("left"), spec.get("right")]
        if table:
            sides += [x for _, row in table["table"] for x in (row.get("left"), row.get("right"))]
        channels = []
        for sd in sides:
            for h in (sd or {}).get("hits", []):
                if h.get("kind") == "spawn" and h["channel"] not in channels:
                    channels.append(h["channel"])
        presence = [{"key": c["key"], "values": list(c["values"])} for c in spec.get("presence", [])]
        presence += [{"key": "chan:" + c, "values": ["DEFAULT", "armed"]} for c in channels]
        is_target = any(h.get("kind") == "target" for sd in sides if sd for h in sd.get("hits", []))
        if "circle" in spec:
            c = spec["circle"]
            s, e, v = full_circle_points(c["center"][0], c["center"][1], c["radius"])
            geo = {"circle": full_circle_carrier(c["center"][0], c["center"][1], c["radius"]), "full": True, "line": False}
        else:
            s, e, v = v2(*spec["start"]), v2(*spec["end"]), v2(*spec["via"])
            geo = {"circle": None, "full": False, "line": bool(spec.get("straight")) or derive_is_line(s, e, v)}

        def base(sid, p, q, w, g, left, right):
            d = {"circle": g["circle"], "id": sid, "start": list(p), "end": list(q), "via": list(w), "full": g["full"],
                 "line": g["line"], "left": left, "right": right, "is_target": is_target,
                 "player_solid": bool(spec.get("player_solid", False))}
            if presence:
                d["presence"] = presence
            return d

        left, right = spec.get("left"), spec.get("right")
        if not table and left and left.get("effect", {}).get("type") == "portal":
            theta, d = float(left["effect"]["theta"]), v2(*left["effect"]["d"])
            rs = right or left
            out.append(base(ids[0], s, e, v, geo, _side_def(left, ids[0], theta=theta, d=d, pair=ids[1]),
                            _side_def(rs, ids[0], theta=theta, d=d, pair=ids[1])))
            inv = apply_motion(d, -theta, (0.0, 0.0))
            inv_d = (-inv[0], -inv[1])
            ts, te, tv = (apply_motion(p, theta, d) for p in (s, e, v))
            tgeo = {"circle": None, "full": geo["full"],
                    "line": (geo["line"] and not geo["full"]) or derive_is_line(ts, te, tv)}
            if geo["full"] and not tgeo["line"]:
                a_, b_, c_, d_ = derive_carrier(ts, te, tv)
                tgeo["circle"] = {"center": [-b_ / (2.0 * a_), -c_ / (2.0 * a_)],
                                  "radius": math.sqrt((b_ * b_ + c_ * c_ - 4.0 * a_ * d_) / (4.0 * a_ * a_))}
            out.append(base(ids[1], ts, te, tv, tgeo, _side_def(left, ids[1], theta=-theta, d=inv_d, pair=ids[0]),
                            _side_def(rs, ids[1], theta=-theta, d=inv_d, pair=ids[0])))
            continue
        if table:
            d = base(ids[0], s, e, v, geo, None, None)
            d["resolver"] = {"key": table["key"], "table": {
                k: {"left": _side_def(row.get("left"), ids[0]), "right": _side_def(row.get("right"), ids[0])}
                for k, row in table["table"]}}
            out.append(d)
            continue
        out.append(base(ids[0], s, e, v, geo, _side_def(left, ids[0]), _side_def(right, ids[0])))
    return out


def load_level(name):
    """A level's rc-level/1 lowering by name ("test_levels/portals")."""
    with open(os.path.join(ROOT, "bench", "lowered", name.replace("/", "__") + ".json")) as f:
        return json.load(f)


def level_names():
    base = os.path.join(ROOT, "data", "levels")
    out = []
    for d, _, files in os.walk(base):
        for f in files:
            if f.endswith(".json"):
                out.append(os.path.relpath(os.path.join(d, f), base)[:-5].replace(os.sep, "/"))
    return sorted(out)
