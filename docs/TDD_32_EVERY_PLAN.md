# TDD 32: Every plan measured; less work outside the plan's reach (Stage 127)

The eighth stage of the smooth-frames program. The user asked (2026-10-22) to bring the slowest planned regions
down, starting by measuring every level's inversions and projectives rather than only the first plannable side
(TDD_31, F19). The user also dropped the original plan's remaining animations (overlays, the spawn preview, the
flight's dimming).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 127.0 | Every plannable side of every level measured | Done |
| 127.1 | No casts in a gap outside the plan's reach; side and mid casts do not read their cursor; walks end within a step (results unchanged) | Done |
| 127.2 | Docs, `make gates`, sign-off | Done (user, 2026-10-22) |

## 127.0 Every plan measured

Each plannable side of each level (and the infinity entry) was planned alone, and its region timed at 12 players
along a walk from the start (8 threads). The worst side per level, before this stage:

| Level | Worst side | ms |
|---|---|---|
| hit_effects | 12L | 29.7 |
| crossings | 20R (17, 19, 20 all over 25) | 27.3 |
| tangent_infinity | 8L | 24.0 |
| playground/portals | 9L | 21.3 |
| projective | 12L | 17.7 |
| two_sided | 8R | 16.2 |
| pre_inverted | 2L | 16.0 |
| main | 9L | 14.5 |

The earlier benchmarks, which planned only the first side, had understated this: crossings looked like 18 ms.

**Where the time goes.** A worst case is 1000–2000 casts per region. Nearly every member is inert: its own
cast and both side casts have one shape. About a third of the members' walks do not meet the plan at all. Most
of the rest meet it, but the plan entry is occluded and nothing is visible.

## 127.1 Less work outside the plan's reach

**Dead gaps** (`Builder::dead_gap`). In an aim pencil with a plan, consider a gap whose two members' walks both
fail to meet the plan, where neither member is a tangency. Such a gap holds no cell. The plan's reach is
bounded by members (its surfaces' endpoints and tangencies, in every frame), so no walk between them meets it.
Its mid cast and the side casts facing it are therefore the member's own shape at their own parameters, and
are not cast. The members' own casts now run first and the side casts after, each as a parallel batch.

Checked on every side of every level at six players: never a met cast in such a gap. Two variants were tried
and dropped:

- **Invisible gaps instead of unmet ones.** A member bounding a visible stretch is itself unseen, because its
  edge is exclusive. ghost_room [11R, 6L] lost its whole region.
- **Projective pencils.** Their reach is not bounded by their members alone (eight such cases on
  projective_inversion, ten on directional_arcs).

**Side and mid casts do not read their cursor.** Their `cursor_ok` is never read, so their planned walk is read
only through the plan's last entry (`Ladder::solve`'s `reads_cursor`; Stage 123.8's horizon).

**Walks end within a step.** `Solver::read_horizon` is checked before each candidate, not only each step. A
straight beam on crossings crosses a dozen pass-through carriers in one step, so the planned walks ran 28
events for 14 read.

**Results unchanged.** Every region of a 2010-region set (every side and some two-entry plans of every level,
six players each) is bit for bit the same. So are the digests, the sweeps (unsampled) and the sessions.

**A side cast's sparing that does not pay** (Stage 125, F14) is the complement: inside the plan's reach the
side casts are needed.

## Results

The worst side per level, a region while walking (ms, 8 threads):

| Level | Before | After |
|---|---|---|
| hit_effects | 29.7 | 22.4 |
| crossings | 27.3 | 21.7 |
| tangent_infinity | 24.0 | 22.6 |
| playground/portals | 21.3 | 18.8 |
| projective | 17.7 | 18.7 (a projective pencil: unchanged) |
| two_sided | 16.2 | 15.1 |
| pre_inverted | 16.0 | 15.6 |
| main | 14.5 | 13.1 |
| inversion_grid | 12.0 | 10.9 |
| ghost_room | 8.1 | 4.9 |

Every worst side is now under 25 ms, below the 33 ms at which a late region is smoothed (Stage 126). On 20 of
37 levels some side still takes over 4 ms, and those regions arrive a frame or two after their request.

## Findings

| # | Finding | Status |
|---|---|---|
| F19 (TDD_31) | crossings with its inversion planned about 27 ms | Reduced (22) |
| F20 | The remaining cost per region: planned walks about 45% (a plan entry consumed after a dozen pass-throughs), physical walks about 30%, climbs about 10%, path conversion about 10%. The walks that do not meet the plan (11% of side and mid walks) are read whole: only the whole walk shows that | Reduced by Stage 128 (TDD_33): contention and allocations; the resumed walk is not bit-exact; a region updated from the last one stays open |

## Tests

| Test | What it checks |
|---|---|
| the region digests, the sweeps (unsampled), the sessions | unchanged |
| a 2010-region comparison against Stage 126 (scratch, every side of every level and two-entry plans, six players) | every region bit for bit |
