"""Scenario schema shared by the Python oracle and the GDScript bench.

Scenario JSON:
{
  "name": str,
  "player": [x, y], "cursor": [x, y],
  "circles": [{"id": int, "center": [x, y], "radius": r}],
  "walls": true,                      # four pass-through screen bounds (ids after circles)
  "plan": [{"surface_id": int, "side": "LEFT"|"RIGHT"}],
  "tags": [str]
}
Coordinates are plain JSON floats: game inputs are float32/float64 values and
therefore exactly representable; the oracle lifts them to mp exactly.
"""

from __future__ import annotations

import json
import re

import mpmath as mp

from beam import Circle

GRID_CENTERS = [
    (760, 380), (960, 380), (1160, 380),
    (760, 540), (960, 540), (1160, 540),
    (760, 700), (960, 700), (1160, 700),
]
GRID_RADIUS = 60.0


def grid_circles(radius=GRID_RADIUS, centers=GRID_CENTERS):
    return [{"id": i + 1, "center": [float(x), float(y)], "radius": float(radius)}
            for i, (x, y) in enumerate(centers)]


def to_mp_circles(scn):
    return [Circle(c["id"], mp.mpc(mp.mpf(c["center"][0]), mp.mpf(c["center"][1])),
                   mp.mpf(c["radius"])) for c in scn["circles"]]


def plan_tuples(scn):
    return [(e["surface_id"], e["side"]) for e in scn["plan"]]


def mpc(xy):
    return mp.mpc(mp.mpf(xy[0]), mp.mpf(xy[1]))


def load(path):
    with open(path) as f:
        return json.load(f)


def save(scn, path):
    with open(path, "w") as f:
        json.dump(scn, f, indent=1)


_SURF_RE = re.compile(
    r"Surface (\d+): \(\(([-\d.e]+), ([-\d.e]+)\) → \(([-\d.e]+), ([-\d.e]+)\)\) len=[\d.]+ L=(\w+) R=(\w+)")
_VEC_RE = r"\(([-\d.e]+), ([-\d.e]+)\)"


def parse_dump(text, name="dump"):
    """Parse an F12 DEBUG STATE dump of a full-inversion-circle scene.

    Full circles are printed as start = c + (r, 0), end = c + (-r/2, -0.866 r)
    (Segment.full_from_carrier), which determines c and r.
    """
    player = re.search(r"Player: " + _VEC_RE, text)
    cursor = re.search(r"Cursor: " + _VEC_RE, text)
    circles = []
    for m in _SURF_RE.finditer(text):
        sid = int(m.group(1))
        sx, sy, ex, ey = (float(m.group(i)) for i in range(2, 6))
        left, right = m.group(6), m.group(7)
        if left == "inversion" and right == "inversion":
            # sx = cx + r, sy = cy ; ex = cx - r/2
            r = (sx - ex) / 1.5
            circles.append({"id": sid, "center": [sx - r, sy], "radius": r})
    plan = [{"surface_id": int(a), "side": b}
            for a, b in re.findall(r"\[\d+\] surface_id=(\d+) side=(\w+)", text)]
    return {
        "name": name,
        "player": [float(player.group(1)), float(player.group(2))],
        "cursor": [float(cursor.group(1)), float(cursor.group(2))],
        "circles": circles,
        "walls": True,
        "plan": plan,
        "tags": ["dump"],
    }
