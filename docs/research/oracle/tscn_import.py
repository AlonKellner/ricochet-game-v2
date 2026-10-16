"""Import a level .tscn into the same surface list LevelSettings builds.

Replicates scripts/game/level_settings.gd (+ RoomBuilder, RigidMotionEffect.
create_portal_pair, Segment.full_from_carrier / derive_carrier) including the
float32 rounding of Vector2 (Godot's default single-precision build), so the
result is identical to what the game constructs. Verified against the game's
own dump (scripts/testing/dump_scene_surfaces.gd) by test_oracle_v2.py.

Surface ids follow construction order: room walls, exported arrays in _ready()
order (portal pairs: source then target), screen bounds, pass-through lines.
"""

from __future__ import annotations

import math
import re

import numpy as np

F32 = np.float32

DEFAULTS = {"build_room": True, "room_rect": (560.0, 240.0, 800.0, 600.0)}


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
    l = np.sqrt(l2, dtype=F32)
    return (float(x / l), float(y / l))


# ---------------------------------------------------------------- parsing

_NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"


def _nums(s):
    return [float(x) for x in re.findall(_NUM, s)]


def parse_root_properties(text):
    """Properties of the root node (the first [node] block)."""
    props = {}
    in_root = False
    for line in text.splitlines():
        if line.startswith("[node"):
            if in_root:
                break
            in_root = "parent=" not in line
            continue
        if not in_root or " = " not in line:
            continue
        key, val = line.split(" = ", 1)
        props[key.strip()] = val.strip()
    return props


def _vec4_list(val):
    return [tuple(_nums(m)) for m in re.findall(r"Vector4\(([^)]*)\)", val)]


def _floats(val):
    m = re.search(r"PackedFloat64Array\(([^)]*)\)", val)
    return _nums(m.group(1)) if m else []


# ---------------------------------------------------------------- geometry (f64, game op order)

def _det3x3(a11, a12, a13, a21, a22, a23, a31, a32, a33):
    return (a11 * (a22 * a33 - a23 * a32)
            - a12 * (a21 * a33 - a23 * a31)
            + a13 * (a21 * a32 - a22 * a31))


def _row(p):
    if p is None:
        return [1.0, 0.0, 0.0, 0.0]
    return [p[0] * p[0] + p[1] * p[1], p[0], p[1], 1.0]


def derive_is_line(s, e, v):
    r1, r2, r3 = _row(s), _row(v), _row(e)
    a = _det3x3(r1[1], r1[2], r1[3], r2[1], r2[2], r2[3], r3[1], r3[2], r3[3])
    return a == 0.0


def seg_from_v4(v):
    s = v2(v[0], v[1])
    e = v2(v[2], v[3])
    via = v2((v[0] + v[2]) / 2.0, (v[1] + v[3]) / 2.0)
    return s, e, via


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
    ctr32 = v2(cx, cy)
    # GeneralizedCircle.from_circle(center, r) then center()/radius()
    a = 1.0
    b = -2.0 * ctr32[0]
    c = -2.0 * ctr32[1]
    d = ctr32[0] * ctr32[0] + ctr32[1] * ctr32[1] - r * r
    ctr = v2(-b / (2.0 * a), -c / (2.0 * a))
    rad = math.sqrt((b * b + c * c - 4.0 * a * d) / (4.0 * a * a))
    p1 = v2_add(ctr, v2(rad, 0.0))
    p2 = v2_add(ctr, v2(-0.5 * rad, 0.866 * rad))
    p3 = v2_add(ctr, v2(-0.5 * rad, -0.866 * rad))
    return p1, p3, p2   # start, end, via


def portal_transform(theta, d):
    a = (f32(math.cos(theta)), f32(math.sin(theta)))
    b = (f32(d[0]), f32(d[1]))

    def apply(p):
        if p is None:
            return None
        zx, zy = p
        return (f32((a[0] * zx - a[1] * zy) + b[0]), f32((a[0] * zy + a[1] * zx) + b[1]))

    en = (f32(math.cos(theta)), f32(-math.sin(theta)))
    inv_d = (f32(-(en[0] * b[0] - en[1] * b[1])), f32(-(en[0] * b[1] + en[1] * b[0])))
    return apply, inv_d


# ---------------------------------------------------------------- build

def _cfg(t, interactive, **kw):
    d = {"type": t, "interactive": interactive}
    d.update(kw)
    return d


PASS0 = {"type": "pass", "interactive": False}


def build_surfaces(props):
    out = []

    def add(s, e, via, left, right, full=False, is_target=False, player_solid=True, line=None, circle=None,
            resolver=None, pre_transform=False):
        if line is None:
            line = False if full else derive_is_line(s, e, via)
        d = {
            "circle": circle,
            "id": len(out) + 1, "start": list(s), "end": list(e),
            "via": None if via is None else list(via), "full": full, "line": line,
            "left": left, "right": right, "is_target": is_target, "player_solid": player_solid,
        }
        if pre_transform:
            d["pre_transform"] = True
        if resolver is not None:
            d["resolver"] = resolver
        out.append(d)

    def flag(key):
        v = props.get(key)
        return DEFAULTS[key] if v is None else v == "true"

    if flag("build_room"):
        rr = _nums(props["room_rect"].split("(", 1)[1]) if "room_rect" in props else DEFAULTS["room_rect"]
        l, t, w, h = (f32(x) for x in rr)
        r, b = f32(l + w), f32(t + h)
        term = _cfg("terminal", False)
        for s, e, v in [((l, t), (r, t), ((l + r) / 2.0, t)), ((r, t), (r, b), (r, (t + b) / 2.0)),
                        ((r, b), (l, b), ((l + r) / 2.0, b)), ((l, b), (l, t), (l, (t + b) / 2.0))]:
            add(v2(*s), v2(*e), v2(*v), term, term)

    def lines(key):
        return _vec4_list(props.get(key, ""))

    def arcs(key, n):
        fl = _floats(props.get(key, ""))
        return [fl[i:i + n] for i in range(0, len(fl), n)]

    term = _cfg("terminal", False)
    for v in lines("block_lines"):
        add(*seg_from_v4(v), term, term)
    for v in lines("mirror_lines"):
        add(*seg_from_v4(v), _cfg("reflection", True), PASS0, player_solid=False)
    for v in lines("mirror_right_lines"):
        add(*seg_from_v4(v), PASS0, _cfg("reflection", True), player_solid=False)
    for v in lines("mirror_both_lines"):
        add(*seg_from_v4(v), _cfg("reflection", True), _cfg("reflection", True), player_solid=False)
    for a in arcs("inversion_left_arcs", 6):
        add(v2(a[0], a[1]), v2(a[2], a[3]), v2(a[4], a[5]), _cfg("inversion", True), PASS0)
    for a in arcs("full_inversion_arcs", 3):
        s, e, v = full_circle_points(*a)
        add(s, e, v, _cfg("inversion", True), _cfg("inversion", True), full=True, circle=full_circle_carrier(*a))
    for a in arcs("semi_reflective_arcs", 6):
        add(v2(a[0], a[1]), v2(a[2], a[3]), v2(a[4], a[5]), _cfg("reflection", True), PASS0, player_solid=False)
    for a in arcs("reflective_arcs", 6):
        add(v2(a[0], a[1]), v2(a[2], a[3]), v2(a[4], a[5]), _cfg("reflection", True), _cfg("reflection", True),
            player_solid=False)
    for a in arcs("full_reflective_arcs", 3):
        s, e, v = full_circle_points(*a)
        add(s, e, v, _cfg("reflection", True), _cfg("reflection", True), full=True, player_solid=False,
            circle=full_circle_carrier(*a))
    for key, back in [("normal_projection_lines", False), ("normal_projection_back_lines", True)]:
        for v in lines(key):
            c = _cfg("line_normal_projection", True, back=back)
            add(*seg_from_v4(v), c, c, player_solid=False)
    for key, back in [("normal_projection_arcs", False), ("normal_projection_back_arcs", True)]:
        for a in arcs(key, 6):
            c = _cfg("circle_normal_projection", True, back=back)
            add(v2(a[0], a[1]), v2(a[2], a[3]), v2(a[4], a[5]), c, c, player_solid=False)
    for key, back in [("circle_directional_arcs", False), ("circle_directional_back_arcs", True)]:
        for a in arcs(key, 8):
            n = v2_normalized(v2(a[6], a[7]))
            c = _cfg("circle_directional_projection", True, back=back, normal=list(n))
            add(v2(a[0], a[1]), v2(a[2], a[3]), v2(a[4], a[5]), c, c, player_solid=False)
    for key, back in [("directional_projection_lines", False), ("directional_projection_back_lines", True)]:
        for a in arcs(key, 6):
            n = v2_normalized(v2(a[4], a[5]))
            c = _cfg("line_directional_projection", True, back=back, normal=list(n))
            add(v2(a[0], a[1]), v2(a[2], a[3]), v2((a[0] + a[2]) / 2.0, (a[1] + a[3]) / 2.0), c, c,
                player_solid=False)

    def portal(s, e, v, theta, d):
        apply, inv_d = portal_transform(theta, d)
        src, tgt = len(out) + 1, len(out) + 2
        ca = _cfg("portal", True, theta=theta, d=[f32(d[0]), f32(d[1])], pair=tgt)
        add(s, e, v, ca, ca, player_solid=False)
        cb = _cfg("portal", True, theta=-theta, d=list(inv_d), pair=src)
        add(apply(s), apply(e), apply(v), cb, cb, player_solid=False)

    for a in arcs("portal_lines", 7):
        s, e, v = seg_from_v4(a[:4])
        portal(s, e, v, a[4], (a[5], a[6]))
    for a in arcs("portal_arcs", 9):
        portal(v2(a[0], a[1]), v2(a[2], a[3]), v2(a[4], a[5]), a[6], (a[7], a[8]))

    # the level's edge walls along level_bounds (Stage 100)
    bx, by, bw, bh = (f32(x) for x in (_nums(props["level_bounds"].split("(", 1)[1]) if "level_bounds" in props
                                       else (0.0, 0.0, 1920.0, 1080.0)))
    ex, ey = f32(bx + bw), f32(by + bh)
    for v in [(bx, by, ex, by), (ex, by, ex, ey), (ex, ey, bx, ey), (bx, ey, bx, by)]:
        add(*seg_from_v4(v), PASS0, PASS0, player_solid=False)
    # state flags (Stage 85) and ghost togglers (Stage 86), as LevelSettings builds them
    key = props.get("state_key", '"open"').strip('"')
    for v in lines("toggle_lines"):
        tog = _cfg("pass", True, states=[{"key": key, "op": "toggle", "value": True}])
        add(*seg_from_v4(v), tog, tog, player_solid=False)
    for v in lines("ghost_toggler_lines"):
        g = _cfg("pass", True, states=[{"key": "ghost", "op": "toggle", "value": True}])
        add(*seg_from_v4(v), g, g, player_solid=False)
    for v in lines("gate_lines"):
        opn, block = _cfg("pass", False), _cfg("terminal", False)
        add(*seg_from_v4(v), None, None, player_solid=False,
            resolver={"key": key, "table": {"true": {"left": opn, "right": opn}, "DEFAULT": {"left": block, "right": block}}})
    for v in lines("flag_mirror_lines"):
        refl, off = _cfg("reflection", True), _cfg("pass", False)
        add(*seg_from_v4(v), None, None, player_solid=False,
            resolver={"key": key, "table": {"true": {"left": refl, "right": refl}, "DEFAULT": {"left": off, "right": off}}})
    for v in lines("passthrough_lines"):
        add(*seg_from_v4(v), PASS0, PASS0, player_solid=False)
    # the pre-transform (Stage 101): full inversion circles, never hit
    for a in arcs("pre_transform_inversions", 3):
        s, e, v = full_circle_points(*a)
        inv = _cfg("inversion", False)
        add(s, e, v, inv, inv, full=True, player_solid=False, circle=full_circle_carrier(*a), pre_transform=True)
    return out


def import_scene(path):
    with open(path) as f:
        text = f.read()
    if "level_settings.gd" not in text:
        return None
    return build_surfaces(parse_root_properties(text))
