# TDD 42: The cast-cost program, Stages A and B (Stage 137)

The user asked for a higher-quality optimisation plan than a first agent's (2026-10-24). The plan that replaced it
(`~/.claude/plans/i-asked-a-weaker-transient-hanrahan.md`, approved 2026-10-24) measures first and removes measured
waste bit for bit before any rule changes. This stage is its Stage A: the measurement tooling and the per-cast wins
that the profiles pointed at. The user's rules: regions arrive on time without smoothing (the Godot host waits up to
4 ms per frame); every change is bit-exact (goldens, region digests, sessions, the hash of every plan's region) or
judged; timing is evidence, never a gate.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| A.0 | `rc_tool regions`, `make regions`, `make profile`, `tools/profile_incl.py`, the batches' wall time counted, allocations counted on request | Done |
| A.1 | Caches for every solver; the exact memos shared across the ladder's tiers; four chains; Q0 once per scene | Done (bit-exact) |
| A.2 | Same-carrier ties decided by the carrier; a surface's exact line computed once | Done (bit-exact) |
| A.3 | The exact pole carried lazily behind a float shadow (`exact::PoleRec`) | Done (bit-exact) |
| A.4 | The exact start circle carried lazily (`BeamRec::from_gc_lazy`) | Done (bit-exact) |
| A.5 | `plan_effects` and `piece_spans` memoized | Done (bit-exact) |
| A.6 | Derived lines and beam records share their parent | Done (bit-exact; small) |
| A.7 | The candidate surfaces per walk state; the ghost flag cached | Done in part (bit-exact; small) -- the flags' interning deferred |
| A.8 | Known points' incidence lists per point; the margins' instrument on request; the read horizon cached | Done (bit-exact) |
| A.9 | A lap on an unchanged beam reusing its candidates | Deferred (previews only; regions stop at the horizon) |
| A.10 | The whole build on the compute threads' allocator; the sized delete without `malloc_size` | Done in part (bit-exact) -- the limb buffers deferred |
| A.11 | `-O3` on every rc target | Done (bit-exact; 1-5%) |
| A.12 | Docs, `make gates` (green 2026-10-24), sign-off | Pending (the user) |
| B.1 | One batch for the side and the middle casts | Done (bit-exact) |
| B.2 | Both halves of a split plan at once | Not done (they share one tracer's helpers; the pool runs one batch at a time) |
| B.3 | The casts likely to be slow first in each batch; each cast's time recorded | Done (bit-exact) |
| B.4 | The schedule's overheads: sized deallocation, the sized free's class, the decision keys, 64 memo shards, `scene_supported` once, the job thread's cache for life, the performance cores | Done (bit-exact) |
| B.5 | Cross-worker exact memos | Not done (no evidence of lost hits) |
| B.6 | A dependency-driven schedule | Not done (the tails are 0.3-0.4 ms at six threads after B.1 and B.3) |
| B.7 | Stage C's measurement: the off-segment non-planned hits per walk | Done (below) |

## A.0 Measuring

**`rc_tool regions`** (`native/hosts/headless/regions.cpp`; `make regions [THREADS=k] [PLAYERS=n]`): every plannable
side of every level planned alone, every projective side, the infinity entry (`--pairs` adds the first sides' pairs),
each built at six players. Per plan: the build's median, p95 and max in ms, the union and fill after it, casts by
kind, refinements, members, cells, the serial part of the build (outside the parallel batches), the batches' tail
(their wall time beyond their casts' CPU shared over the threads), climbs, BigFloat time, allocations (`--allocs`),
and one hash of every region's edges (`rc/region_digest.hpp`, the digest the regions golden pins). Two reports
that agree on every hash built the same regions bit for bit: `make perf-diff A= B=` says so per plan. That check
is what every phase below ran, beside the gates.

**`make profile LEVEL=... [THREADS=1]`** (`tools/profile.sh`, `tools/profile_incl.py`): the level's regions under
macOS `sample`, then the self-time leaders and the inclusive share of named functions (a node counted once per
path). Every "x% of a region" below is such a share, single-threaded.

**What the baseline showed** (main 1b9f4f0, before this stage):

- 256 plans on 37 levels; 57 over 4 ms at 8 threads, on 11 levels; the worst tangent_infinity [8R] 12.7 ms
  (61.9 ms on one thread), hit_effects [12L] 10.8, portals [9R] 10.2, crossings [17L] 9.3.
- Four casts per member (crit, two sides, mid). Refinements are zero on every level but projective. Climbs are at
  most 5% of a region. The cost is the two Dual walks per cast and the exact arithmetic around them.
- 8 threads give 4.7-5.4x; the ideal with the per-cast thread cost of TDD_40 is about 7.1x.
- 93 of the 256 plans build an empty region (0 cells) at full cost: a side the player cannot reach first still
  casts every member (F28, below).

## A.1 Caches for every solver

The tracer's decision solvers (`tie_choice`, `realization_reversed`, `plan_refuses`, `infinity_point`,
`origin_point`) and the region builder's own solver ran with a null `SolveCache`. In a split plan every crit cast
makes a tie choice and a realization decision, and each of those built the plan's chain and, in a pre-transform
frame, the exact start circle (`gc_through`, 51 BigFloat products): 30% of a tangent_infinity region, 25% of
inversion_grid (`realization_reversed`), 14% of crossings. They share the tracer's cache now.

The exact memos -- the aim images, the pre-transform starts, the pole predicates, the carrier incidences -- were one
set per tier's `SolveCache`, so a climb through {4, 8, 16, 32, 64} limbs recomputed every one of them per tier. They
are pure functions of exact inputs (float64 data and exact arithmetic), the same at every tier: one `ExactCache` per
`LadderContext`, shared by its tiers' caches; the memos' scene token is the memo itself.

The chain cache held one entry; a split half alternates the force on one cursor (its tie cast and its own cast), so
each evicted the other. It holds four. Q0 of the pre-transform (`pre_q_exact`) is computed once per scene.

## A.2 Same-carrier ties

`concurrent_ties`: adjacent candidates on one carrier (`carrier_rep` is assigned by exact collinearity) went through
`Line::concurrent`, whose float filter rightly declines for proportional rows, to BigFloat, which found two copies of
one line concurrent with the beam's line: 36% of a hit_effects region, where three surfaces lie on every wall's line.
The pair is decided by the carrier: concurrent iff the beam's line has exact coefficients (`Line::has_exact`, which
runs the root's source but not the reflections since). On a circular beam such a pair is parallel, as the exact test
said (`Xw = 0`). A surface's exact line is computed once (`Surf::exact_line`) and handed to the predicates that
rebuilt it from the endpoints per call.

## A.3 The exact pole, lazily

`pole_after` computed the exact pole after every effect (23 products for a reflection, 12 for an inversion, the
image of Q memoized), then `set_pole` dropped it when it passed the limb budget: 29% of an inversion_grid region, 10%
of crossings. Its consumers -- the tangency at Q (`pole_tangent`, asked for every surface at every step), the pinning
of crossings at Q -- decide by a float filter first (`eb_of` of the exact value) and ask the exact pole only where it
declines.

`exact::PoleRec` records the start and the maps applied since, like `BeamRec`, and keeps a float64 shadow -- Q
homogeneous and D, with rigorous running bounds (`EB`) -- propagated through the same formulas as the exact maps
(`reflect_point_h`, `invert_point_h`, the differentials). The predicates' filters run on the shadow
(`PoleRec::on_line`, `on_circle`, `tangent_line`, `tangent_circle`: false where the value is certainly non-zero,
undecided otherwise); the exact pole is computed, once per record, only where a filter cannot decide. A record
without an exact pole (none at the start, a degenerate map, the budget) answers every predicate as no pole did: the
candidate stays, nothing is pinned. `depart_q` reads the pole's presence only when the effect fired at Q, so it
forces the exact pole there alone. The realization's `first_firing` takes the record.

## A.4 The exact start circle, lazily

A pre-transform start's exact circle through the player, the aim image and Q0 was built eagerly per walk
(`gc_through`), though every consumer (`cursor_on_beam`, `exact_hits_on_beam`, `beam_tangent`,
`concurrent_ties_circle`) filters in float first. After A.1 it was still 27% of a tangent_infinity region.
`BeamRec::from_gc_lazy` computes it when a decision asks; a lazy circle answers `passes_h` as one born done did,
whatever the queried point's size (the budget check on the point stays below the circle case).

## A.5 Memoized pure functions

`plan_effects` (the effect each plan entry resolves to under the flags it meets) was computed 6-10 times per cast;
`piece_spans` (a planned piece's ends matched to the carriers' crossings) by every walk and every tier, with the
crossings at the tier's precision. Both are memoized in the `SolveCache` (the last 8 and 4).

## A.6 Shared parents

`Line::reflected` / `translated` and `BeamRec::reflected` / `translated` / `inverted` copied their parent
(`make_shared<X>(*this)`: its lazy source, its mirror, any exact value), so a parent's exact value computed through
one child was unknown to the others. They share the parent (`enable_shared_from_this`).

## A.7 The candidate surfaces per walk state

`candidate(w, s)` was evaluated for every surface at every step and every lap: a scan of the remaining plan, then
the surface's presence and both sides' effects, state changes and landing under the flags -- each a string lookup,
and each effect lookup a string search for the ghost flag. It is a pure function of the walk's mode, plan index and
flags: a mask per walk state, rebuilt where the state changes. `Flags` keeps the ghost flag's truth current in `set`
and `erase`, so `is_true("ghost")` is a load. The flags' full interning (keys as ids, canonical keys cached, the
frame keys as ids) is deferred: after these two the flags are 2-3% of hit_effects.

## A.8 Off the hot path

- **Known points' incidence.** For every known point of every beam -- an endpoint on a reflected line, a point kept
  through a reflection -- `snap` asked the incidence memo once per surface: 16% of a crossings region in the memo's
  probes (a 2^19-slot table, cold). The surfaces whose carrier contains a point are a function of the scene and the
  point: `Scene::incidence_lists` keeps them by the point's bits, one lookup per known point per beam, and a point
  kept through a reflection keeps its list (the same point, the same carriers).
- **The margins' instrument.** `gaps()` filled `Event::why` (what set each margin, with an O(surfaces) scan per
  small margin) on every step; only `LadderStats::why*` read it. It fills under `RC_LADDER_WHY=1`.
- **The read horizon.** `(*w.read_horizon)(w.events)` scanned every event through a `std::function` at every
  candidate. It is pure in the events: its value is kept until an event is added.

## A.10 Allocation

`vis::compute` and the session's region job mark their thread a compute thread for the whole build, so collect,
cells, merge, the sequential casts, the union and the fill allocate through the block cache of Stage 135 (only the
batches did). The sized `operator delete` classes a block by the size it knows (the block holds at least it) instead
of asking `malloc_size` per free (4% of hit_effects). Deferred: the BigFloat limb buffers (`SmallVec` spills are
7-12% of a region), the copy of the planned result into the physical one.

## A.11 `-O3`

`rc_apply_flags` adds `-O3` for RelWithDebInfo on every rc target; `-ffp-contract=off -fno-fast-math` stay, and
`fma_sensitive` and the goldens prove nothing moved. Worth 1-5%.

## Stage B: the schedule

**B.0 Measured** (after Stage A, crossings [17R], the regions tool at 1, 4, 6 and 8 threads): the casts' summed CPU was
28 ms on one thread, 33.6 at four, 35.2 at six and 47.5 at eight -- the two threads past the six performance cores run on
efficiency cores, each cast there about twice as long, and the batch waits for them: eight threads cost 35% more CPU than
six for the same wall time. The serial part of a build was 1.2-1.7 ms (collect 0.3, the job-building loops in
`cast_members` 0.9, cells and merge under 0.1), the three batches' tails 0.5-2.5 ms, and on hit_effects the union
and the fill after the build 1.4 ms more.

**B.1** The gaps' middle casts read the crit casts alone (`dead_gap`), like the side casts: one batch for both, so two
barriers per half instead of three (`pre_mid_`; `build_cells` takes the mids where it would cast; a refinement's new
gaps are cast there as before).

**B.3** A batch waits for its slowest cast, so the casts likely to be slow run first: in the crit batch the members at a
tangency or at a proper crossing (an irrational point the walk comes back to, F13: the climbs), then the exact
endpoints; in the side-and-mid batch each cast by the time its member's (members') crit cast took (`Cast::usec`). The
results land by slot: the order of the work changes nothing.

**B.4** What the profile at six threads showed beyond one thread's:

- Nearly every free was unsized (250k of 255k allocations per region), so the sized-delete path of A.10 never ran and
  `malloc_size` ran per free: Apple clang emits sized deletes only under `-fsized-deallocation`, now on every rc
  target. Then the sized free filed a block under the largest class *at most* its size -- one below the class the
  request takes from, for any size that is not a class size -- so every free moved a block down a class and every
  allocation of that size missed: 61k system mallocs per region on crossings. A block of up to 8 KiB is its class's
  size on any thread now, and the sized free files it by the class the request takes from: 20k system mallocs per
  region, and the single-threaded region 29.6 → 25.4 ms.
- The tracer's tie and realization memo keys were strings of `%a`'s (a `snprintf` per double per decision):
  `__vfprintf` 1.8% at six threads. They are the inputs' bits (`DecisionKey`).
- The split plans' shared memo's 16 shards waited on each other: 64. `scene_supported(defs)` ran per decision: once
  per `set_defs`.
- The region job's thread (and the tool's) is a compute thread for its life, so a region's results freed there come
  back for the next.
- On macOS the default thread count is the performance cores' (`hw.perflevel0.logicalcpu`): the same wall time for
  about 25% less CPU.

**B.7 Stage C's measurement.** Of the walks' hit events (`LadderStats::hits`), those off their segment on a surface no
remaining plan entry names: hit_effects 42% (its worst side 53%), crossings 36%, portals 34%, projective 26%,
tangent_infinity 17%, inversion_grid 0%. Stage C's rule would remove them as events; the per-step work (the roots of
every candidate surface) stays unless the filter runs before the roots, so its gain is their per-candidate work.

**Not done, and why:** B.2 (the two halves of a split plan share one tracer's helpers and the pool runs one batch at a
time; the memo already dedups their casts); B.5 (no evidence of lost memo hits); B.6 (after B.1 and B.3 the tails are
0.3-0.4 ms at six threads: a dependency-driven schedule would buy at most that). The union and fill after the build
(F31: Clipper2 on the cells' conforming rings, 1.3 ms of a 6.4 ms hit_effects region) are hashed by the session golden
(the fill's points and indices), so a cheaper union is a judged change.

## Results

`make regions` (`rc_tool regions --all`, six players; medians, ms per region): main 1b9f4f0 at 8 threads (its
default) and on one thread, against the end of Stage A at 8 threads and the end of Stage B at its default, 6
threads (the performance cores). Every one of the 256 plans' region hashes is equal at every count, so every
number below is the same region, bit for bit.

- Plans over 4 ms: main 57 on 11 levels → Stage A 26 on 6 → Stage B 22 on 6; the worst 12.7 → 9.8 → 6.7 ms.
- Every plan summed: one thread 2809 → 1732 → 1524 ms (0.54x); in parallel 679 (8 threads) → 537 (8) → 440 (6) ms.

**The levels, by their worst side before:**

| Level | Worst side (before) | main, 8 threads | Stage A, 8 | Stage B, 6 | main, 1 thread | Stage B, 1 | cells |
|---|---|---|---|---|---|---|---|
| playground/tangent_infinity | [8R] | 12.7 | 6.5 | 6.7 | 61.3 | 25.3 | 0 |
| hit_effects | [12L] | 10.8 | 6.8 | 5.8 | 56.5 | 24.2 | 70 |
| playground/portals | [9R] | 10.2 | 6.9 | 5.6 | 43.6 | 21.2 | 45 |
| crossings | [17L] | 9.3 | 7.6 | 6.3 | 40.3 | 26.3 | 2 |
| playground/projective | [9L] | 9.2 | 5.9 | 5.5 | 46.4 | 27.1 | 0 |
| playground/pre_inverted | [3L] | 7.1 | 3.0 | 2.3 | 32.7 | 9.0 | 2 |
| playground/two_sided | [7L] | 5.7 | 3.6 | 2.8 | 26.4 | 10.2 | 0 |
| main | [9L] | 5.6 | 3.9 | 3.0 | 23.8 | 10.5 | 17 |
| playground/inversion_grid | [3R] | 5.0 | 3.4 | 2.5 | 17.6 | 9.1 | 0 |
| playground/projective_arcs | [6L] | 4.5 | 3.1 | 2.9 | 20.7 | 11.6 | 7 |
| playground/projective_inversion | [6L] | 4.4 | 3.6 | 2.6 | 17.3 | 9.6 | 32 |
| playground/projective_concentric | [6R] | 3.4 | 2.8 | 2.1 | 13.3 | 6.6 | 15 |
| semi_circle_mirror | [5R] | 3.2 | 2.6 | 1.6 | 13.6 | 5.5 | 3 |
| playground/plannable_inversion | [5L] | 3.2 | 2.2 | 1.6 | 13.0 | 5.6 | 6 |

**The worst plans after Stage B, and where their time is** (serial: the build outside the parallel batches; tail:
the batches' wall beyond their casts' CPU shared over six threads; union+fill: after the build, in the job):

| Plan (Stage B, 6 threads) | median | casts | cells | serial ms | tail ms | union+fill ms | off-seg non-planned hits |
|---|---|---|---|---|---|---|---|
| playground/tangent_infinity [8R] | 6.7 | 1125 | 0 | 0.54 | 0.70 | 0.00 | 18% |
| playground/portals [9L] | 6.4 | 1392 | 38 | 1.45 | 0.36 | 0.02 | 31% |
| crossings [17L] | 6.3 | 1665 | 2 | 1.05 | 0.42 | 0.00 | 37% |
| crossings [17R] | 6.3 | 1665 | 0 | 1.15 | 0.40 | 0.00 | 32% |
| playground/projective [7L] | 6.1 | 426 | 0 | 0.05 | 2.21 | 0.00 | 24% |
| playground/tangent_infinity [8L] | 6.0 | 1125 | 26 | 0.88 | 0.42 | 0.39 | 21% |
| hit_effects [12R] | 5.8 | 1278 | 0 | 0.79 | 0.35 | 0.00 | 46% |
| hit_effects [12L] | 5.8 | 1278 | 70 | 0.69 | 0.30 | 1.37 | 53% |

The empty regions (0 cells, F28) are still the worst plans on tangent_infinity, crossings and hit_effects: a side the
player cannot reach first costs the whole cast set. After both stages the time is the casts' CPU (five of the six
worst sides are 1100-1700 casts at 20-25 µs), then the serial build (0.5-1.5 ms), the union and fill on levels with
many cells (hit_effects 1.4 ms), and projective's climbs (its tail is one slow cast, 2.2 ms).

## Findings

| # | Finding | Status |
|---|---|---|
| F28 | 93 of the 256 measured plans build an **empty region** (a side the player cannot reach first) at full cost: tangent_infinity [8R] casts 1125 members' casts for 0 cells. The dead-gap rule of Stage 127 requires valid crit casts, so it spares nothing here. A rule that ends such a build early is a region-structure change (judged): plan Stage D | Open |
| F29 | At 8 threads a worst side is now about 40% schedule: the serial phases (collect, cells, merge, the union and fill: 1-2 ms) and the barrier tails (0.5-2 ms). Stage B of the plan | Open |
| F30 | What remains per cast after this stage, single-threaded: `candidates` 35-40% (the roots, the sort, the known points' snapping, the endpoints on a new line), the exact predicates whose filters decline on provenance-degenerate inputs (`tangent_to`, `pole_on`, `Line::contains`, `cursor_on_beam`: the true cases), the 4-limb climbs on crossings (15%: casts aimed at proper crossings), `SmallVec` limb spills 7-12%, malloc/free 6-9% | Open (plan Stages C and E) |
| F31 | `union_outline` is 10% of a tangent_infinity region and 3-4% elsewhere, outside `build_usec` | Open (Stage B) |
| F32 | The aimed endpoint's exact `Line::contains` runs on every reflected line though the cast knows the point (`aim_known`); adding it first would reorder the known points, which `snap` reads in order | Recorded |

## Tests

| Test | What it checks |
|---|---|
| `make regions` at 1 and 8 threads against the baseline report | every plan's region hash equal (37 levels, 256 plans) |
| ctest, the goldens, the sessions, the Godot suite (`make gate` per commit, `make gates` at the close) | unchanged |
| `fma_sensitive` | no contraction under `-O3` |
