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


def visible(surfaces, player, cursor, plan, flags=None, hints=None, spans=None):
    """True / False, or None when there is no aim (the cursor at the player)."""
    if P(player) == P(cursor):
        return None
    pl = T.planned(surfaces, player, cursor, plan, flags, hints, spans)
    ph = T.physical(surfaces, player, cursor, plan, flags, hints, spans)
    ci = next((i for i, e in enumerate(pl.events) if e.kind == "cursor"), None)
    if ci is None:
        return False                                    # V1
    fpl, fph = _frames_after(pl.events, flags), _frames_after(ph.events, flags)
    rpl, rph = _runs(pl.events), _runs(ph.events)
    kc = next(k for k, (_, last) in enumerate(rpl) if last >= ci)
    if len(rph) <= kc:
        return False                                    # the physical walk ends before the cursor's step
    for k in range(kc):                                 # V2
        if fpl[rpl[k][1]] != fph[rph[k][1]]:
            return False
    # V3: the frame each walk leaves the cursor's point in
    leave_pl = fpl[rpl[kc][1]]
    if rph[kc][0] == rpl[kc][0]:
        leave_ph = fph[rph[kc][1]]
    else:
        leave_ph = fph[rph[kc - 1][1]] if kc > 0 else (0, (), tuple(sorted((flags or {}).items())), False)
    return leave_pl == leave_ph
