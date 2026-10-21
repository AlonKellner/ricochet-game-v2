# TDD 29: Regions with a plan -- less work per cast, walks that end where they stop being read (Stage 124)

The fifth stage of the smooth-frames program. Stage 123 (TDD_28) made the region with no plan fit the 4 ms
budget on every level. With a plan it was still 4–35 ms on 14 levels (TDD_28, F12), so those regions arrived
late and the presenter smoothed them. The user asked for this stage next (2026-10-21): "Proceed to work on
regions with a plan".

The rules carried over: points equal by definition are decided by provenance, never by a precision climb;
the drawing stays exact; a changed golden is brought with its evidence for approval.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 124.1 | K7's exact test pulls the cursor back through the maps; a walk's steps and BigFloat's spilled limbs stop allocating (bit-exact) | Done |
| 124.2 | A region cast's walks end two events past what the cast reads (bit-exact) | Done |
| 124.3 | A member's exact point is known to its cast after its own plan entries; an exact crossing is exact data (re-blessed, approved) | Done |
| 124.4 | Projective members know their exact points too; a cast's paths are converted lean (results unchanged) | Done |
| 124.5 | Docs, `make gates`, sign-off | In progress |

## Where the time was

The measurements are from the stage's tool: 120 regions while walking with a one-entry plan (the first
plannable side, as `rc_tool bench` picks it), 8 threads. A planned region was cast throughput, not climbs.
inversion_grid made 580 casts per region, about 134 ms of single-threaded work, and ran only 3.8× faster on
8 threads. Two things stood out:

- **The exact test behind K7** (the cursor is a known point of a beam whose exact circle passes it) was 28%
  of the time on inversion_grid. Each cast's beam after the plan's inversion built its exact circle, whose
  coefficients grow with every inversion, just to test one point.
- **malloc contention.** Going from 1 thread to 2 cost 41% more CPU for the same work. Independent processes
  scaled almost perfectly (4 processes at single-process speed), so the loss was in-process. The busy profile
  at 4 threads grew mostly in macOS's small-allocation zone (`_xzm_xzone_malloc_tiny`: 400 → 1100 samples).
  Setting `MallocNanoZone=1` alone took inversion_grid from 29 to 21 ms. An environment variable cannot fix
  the Godot host, whose allocator is set up before the game loads, so the allocations had to go.

## 124.1 Pull the point back; stop allocating per step

- **`exact::BeamRec::passes(x, y)`.** Instead of pushing the circle forward, the point is pulled back through
  the beam's maps to its exact base line: a reflection and an inversion are their own inverses, and a
  translation by d is undone by −d. The answer is the same exact one; the point's coordinates stay small. A
  point exactly at an inversion's centre falls back to the circle. K7 (`cursor_on_beam`) and K11's exact hits
  use it.
- **`InlineVec`** (`rc/inline_vec.hpp`): a beam's roots on a carrier are kept inline (at most three: two
  crossings and the antipode of a curved beam).
- **The walk keeps a step's working vectors** (its candidates, their gaps and the gaps' points) across steps.
- **`SmallVec`'s spilled blocks** (BigFloat limbs beyond 8) come from a per-thread cache by power-of-two size.
  The cache is bounded; a block freed on another thread joins that thread's cache.

inversion_grid with a plan: 35 → 21 ms (8 threads), 108 → 85 ms single-threaded.

## 124.2 Walks end where they stop being read

Stage 123.8 let a region's cast tell the ladder what it reads (`Ladder::horizon`):

- the physical walk through its first stop after the plan's entries;
- the planned walk through its cursor's point.

The walks themselves still ran to the hit limit: on inversion_grid a physical walk averaged 31.6 events, of
which 5.9 were read. Now the ladder hands its walks the horizon (`Solver::read_horizon`), and a walk ends two
events past it. Its events so far are the whole walk's, since a walk is decided step by step. This replaces
Stage 123.9's cut-and-rewalk of a climb.

A planned walk that reaches no cursor is read through the event that consumes the plan's last entry (the
cast's `eK`), plus two events.

inversion_grid 21 → 11 ms, hit_effects 13 → 11 ms.

## 124.3 and 124.4 More points known by construction

Stage 123.10 gave an aim member through a segment's endpoint its point (`Solver::aim_known`). The point was
known only on the walk's first beam, and only for endpoints. Now:

- **After its own plan entries.** `AimKnown` is (j, x, y): the beam after the plan's first j entries passes the
  point. The solver adds it as a known point of that beam (to rounding) when the beam is born.
- **Exact crossings.** A crossing whose double coordinates lie exactly on both carriers (by the exact
  predicates) is exact data, like an endpoint. crossings has many: (1400, 210) where circle 17 meets line 18,
  and (900, 250) and (1100, 250) on segment 14.
- **Every pencil.** A projective pencil's members carry their exact points too, both their own and the
  earlier epochs' points pushed through the projective (`aim_known_of`, one rule).
- **Not the departure carrier.** A point on the carrier the beam departs after entry j is left out: the
  departure point there is its own. Without this exclusion, VIS-REGULAR caught a whisker on two_sided [8/R].

Four region digests changed, all approved: only the inclusive flags of boundary rays aimed exactly through
such a point (2, 1, 1 and 5 edges). crossings 23 → 18 ms; projective_lines' climbs per 120 regions
3264 → 1212.

## 124.4 Lean cast paths

A cast reads its paths' steps, their frames, the events' steps and frames, and the cursor's index. It does
not read hit records, targets or the steps' display fields. `Tracer::cast_paths` now builds only what is read;
it no longer computes a beam tangent per hit, which was costly on BigFloat walks. In both modes, a hit's flags
key is rebuilt only when the flags change. projective_lines 10 → 7 ms.

## Results

The stage's tool, a region while walking (ms, 8 threads):

| Level | No plan (Stage 123) | No plan (now) | One-entry plan (Stage 123) | One-entry plan (now) |
|---|---|---|---|---|
| inversion_grid | 3.1 | 0.9 | 35 | 10.8 |
| crossings | 1.9 | 1.3 | 26 | 17.6 |
| projective_concentric | | 0.5 | 20 | 8.8 |
| full_circle_mirror | | 0.4 | 15 | 5.8 |
| hit_effects | 1.6 | 1.2 | 13 | 10.9 |
| semi_circle_mirror | | 0.3 | 13 | 7.1 |
| directional_lines | | 0.4 | 11 | 7.5 |
| projective_lines | | 0.4 | 11 | 7.7 |
| circle_no_walls | | 0.4 | 10.4 | 3.5 |
| two_sided | | 0.6 | 9.6 | 7.2 |
| projective_arcs | | 0.4 | 7.4 | 4.8 |
| portals | | 0.7 | 7.2 | 6.3 |
| ghost_room | | 0.5 | 6.8 | 5.7 |
| tangent_infinity | 3.6 | 1.6 | | 1.5 |

Every region with no plan now takes under 1.6 ms. With a plan, 11 levels are still over the 4 ms budget, and
those regions are smoothed when late.

`make perf` (the frame of a plan click, synchronous: the preview and the region recomputed; means in ms):

| Level | Stage 123 | Stage 124 |
|---|---|---|
| crossings | 32.4 | 17.7 |
| inversion_grid | 33.3 | 10.5 |
| tangent_infinity | 29.3 | 13.0 |
| hit_effects | 13.8 | 11.1 |
| projective | 14.5 | 10.3 |
| semi_circle_mirror | 13.5 | 6.2 |
| full_circle_mirror | 12.0 | 4.4 |
| circle_no_walls | 8.7 | 2.8 |

## Findings

| # | Finding | Status |
|---|---|---|
| F12 (TDD_28) | A region with a plan was over the budget on 14 levels (4–35 ms) | Reduced (now 4.8–17.6 ms on 11 levels); open |
| F14 | What remains is the number of casts: about 1400 per region on crossings, because every crossing of two carriers bounds a member, and each member costs a crit cast, two side casts and a mid cast in each of a split plan's two halves. Per cast, the work is now spread thin: candidates, emitting events, path conversion | Open: fewer casts is a change to the region's construction (which members can matter), for a later stage |
| F15 | A projective's own planned hit point H is exact data only when it is a data point of its segment (K11). For an exact cursor and an axis-aligned projection, H is exact in double precision too, and a tie there still climbs (projective_lines: 1212 climbs per 120 regions) | Open: K11 is the solver's provenance for previews too (oracle, fixtures), a spec change |
| F16 | More cast threads (10 or 12 instead of 8) gain 0–10% | Kept at 8 |

## Tests

| Test | What it checks |
|---|---|
| `visibility: the regions' digest matches bench/goldens/regions.json` | every digest region bit for bit (re-blessed once, approved) |
| the sweeps, unsampled (`rc_sweeps_*`) | VIS-REGULAR and the other region invariants on every manifest combo; they caught the departure-carrier case in 124.3 |
| the session golden, the goldens | unchanged by 124.1, 124.2, 124.4; 124.3 changed four region digests (approved) |
