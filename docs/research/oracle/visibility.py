"""Stage 110: the visibility region's definition (GAME_SPEC 15.1), in the oracle.

A cursor is visible when
  (V1) the planned walk reaches it,
  (V2) the preview's steps before it are aligned: the two walks leave every
       point before the cursor's in the same frame, and
  (V3) both walks leave the cursor's own point in the same frame (B4).

A step ends at each drawn point (float32, as the preview draws it): the events
at one point form a run, and the merge compares the frame each step is walked
in -- the frame after the run before it (epoch, transform stack, flags). An
epoch is identified by the projective firings that opened it (surface and
side), not by their count: two walks firing different projectives at one point
leave it in different frames (K14, Stage 113). V3
counts the decisions made exactly at the cursor: a cursor where a planned
surface is crossed off its segment (the planned walk reflects there, the
physical one passes) is not visible. When the physical walk has no event at
the cursor's point it passes it in the frame it had.

The native region (rc/visibility.hpp) is a construction; this is what it must
equal. gen_fixtures_stage110_visibility.py labels cursors with it, and the native
test checks the region and the native traces against the labels.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace2 as T  # noqa: E402
from geometry import P  # noqa: E402


def _f32(x):
    """x as the preview draws it: float64, then float32."""
    return struct.unpack("f", struct.pack("f", float(x)))[0]


def _drawn(e):
    """The event's drawn point, or None (an escape, the wrap at the real infinity)."""
    if e.point is None:
        return None
    return (_f32(e.point.real), _f32(e.point.imag))


def _frames_after(events, flags0):
    """The frame after each event: (epoch, transform stack, flags, off the
    plan), as the native cast reads it (Tracer::to_path_events); the epoch is
    the tuple of the projective firings so far, (surface_id, side) each (K14);
    off the plan: the walk has left it (Stage 129, GAME_SPEC 14.2)."""
    out = []
    stack, epoch, flags = [], (), dict(flags0 or {})
    for e in events:
        if e.kind == "hit":
            if e.applied and not e.terminal:
                if e.projective:
                    stack = []
                    epoch = epoch + ((e.surface_id, e.side),)
                elif e.popped:
                    stack = stack[:-1]
                else:
                    stack = stack + [e.surface_id]
            flags = dict(e.flags or {})
        out.append((epoch, tuple(stack), tuple(sorted(flags.items())), e.off_plan))
    return out


def _runs(events):
    """Maximal runs of consecutive events at one drawn point (an event without
    a point is a run of its own): (drawn point, index of the run's last event)."""
    runs = []
    for i, e in enumerate(events):
        d = _drawn(e)
        if runs and d is not None and runs[-1][0] == d:
            runs[-1] = (d, i)
        else:
            runs.append((d, i))
    return runs


def _significant(events, frames, flags0):
    """The significant points (GAME_SPEC 12.6): the runs of events at one drawn
    point (_runs) holding a significant event -- an applied effect, a firing
    state change, a passage of infinity or of the ray's origin -- each as (index
    of the run's last event, drawn point, the frame the run leaves in). By point,
    not event by event: surfaces crossed at one point in another order, or one
    write for two, leave it in one frame (B5, GAME_SPEC 14.1)."""
    out = []
    prev = dict(flags0 or {})
    run_sig = False
    for i, e in enumerate(events):
        sig = e.kind in ("infinity", "origin")
        if e.kind == "hit":
            now = dict(e.flags or {})
            sig = e.applied or now != prev
            prev = now
        run_sig = run_sig or sig
        d = _drawn(e)
        if i + 1 == len(events) or d is None or _drawn(events[i + 1]) != d:
            if run_sig:
                out.append((i, d, frames[i]))
            run_sig = False
    return out


def _leave(events, frames, i):
    """The frame after the run of events at event i's drawn point."""
    d, j = _drawn(events[i]), i
    while j + 1 < len(events) and d is not None and _drawn(events[j + 1]) == d:
        j += 1
    return frames[j]


def visible(surfaces, player, cursor, plan, flags=None, hints=None, spans=None):
    """True / False, or None when there is no aim (the cursor at the player).
    Stage 134 (GAME_SPEC 14.5): the walks compute different candidates, so they
    are compared by their significant events, not event by event: (V2) both
    reach the cursor with the same significant events before it (drawn points
    and the frames they leave in), (V3) and leave its point in one frame."""
    if P(player) == P(cursor):
        return None
    pl = T.planned(surfaces, player, cursor, plan, flags, hints, spans)
    ph = T.physical(surfaces, player, cursor, plan, flags, hints, spans)
    ci = next((i for i, e in enumerate(pl.events) if e.kind == "cursor"), None)
    if ci is None:
        # Stage 162: a planned walk that ends at the cursor's point -- a planned terminal there -- reaches it
        last = len(pl.events) - 1
        c = P(cursor)
        if last >= 0 and pl.events[last].kind == "hit" and pl.events[last].terminal \
                and _drawn(pl.events[last]) == (_f32(c.real), _f32(c.imag)):
            ci = last
        else:
            return False                                # V1
    cj = next((i for i, e in enumerate(ph.events) if e.kind == "cursor"), None)
    if cj is None:
        # (a physical walk that ends at the cursor's point -- a terminal there --
        # reaches it: the arrow stops at the cursor)
        last = len(ph.events) - 1
        if last >= 0 and ph.events[last].kind == "hit" and ph.events[last].terminal \
                and _drawn(ph.events[last]) == _drawn(pl.events[ci]):
            cj = last
        else:
            return False                                # the physical walk does not reach it on the plan
    fpl, fph = _frames_after(pl.events, flags), _frames_after(ph.events, flags)
    spl = [(d, f) for i, d, f in _significant(pl.events, fpl, flags) if i < ci]
    sph = [(d, f) for i, d, f in _significant(ph.events, fph, flags) if i < cj]
    if spl != sph:
        return False                                    # V2
    return _leave(pl.events, fpl, ci) == _leave(ph.events, fph, cj)   # V3
