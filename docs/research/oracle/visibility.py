"""Stage 110: the visibility region's definition (GAME_SPEC 15.1), in the oracle.

A cursor is visible when the preview shows an all-green path from the player to
it: the planned walk reaches the cursor, and both walks leave every event before
it in the same frame (the same decision: kind, surface, side, applied, and the
flags after a hit). An event in the cursor's own point changes no step before
the cursor (no step is drawn between them), so a run of events there just
before the cursor does not count -- a cursor exactly at a departure point is
reached before the divergence shows.

The native region (rc/visibility.hpp) is a construction; this is what it must
equal. gen_fixtures_stage110_visibility.py labels cursors with it, and the native
test checks the region and the native traces against the labels.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from geometry import P  # noqa: E402


def same_decision(a, b):
    if a.kind != b.kind or a.surface_id != b.surface_id or a.applied != b.applied:
        return False
    if a.kind == "hit" and (a.side != b.side or (a.flags or {}) != (b.flags or {})):
        return False
    return True


def visible(surfaces, player, cursor, plan, flags=None, hints=None, spans=None):
    """True / False, or None when there is no aim (the cursor at the player)."""
    if P(player) == P(cursor):
        return None
    pl = T.planned(surfaces, player, cursor, plan, flags, hints, spans)
    ph = T.physical(surfaces, player, cursor, plan, flags, hints, spans)
    ci = next((i for i, e in enumerate(pl.events) if e.kind == "cursor"), None)
    if ci is None:
        return False
    first_here = ci
    while first_here > 0 and pl.events[first_here - 1].point is not None and pl.events[first_here - 1].point == P(cursor):
        first_here -= 1
    if len(ph.events) < first_here:
        return False
    return all(same_decision(pl.events[k], ph.events[k]) for k in range(first_here))
