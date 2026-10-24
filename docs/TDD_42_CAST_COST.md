# TDD 42: The cast-cost program, Stage A (Stage 137)

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
| A.12 | Docs, `make gates`, sign-off | Pending |

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

## Results

`make regions` at 8 and at 1 thread, six players, main 1b9f4f0 (before) against the end of this stage (after); medians, ms per region. Every one of the 256 plans' region hashes is equal.

- Plans over 4 ms at 8 threads: 57 on 11 levels → 26 on 6 levels; the worst 12.7 → 9.8 ms.
- Every plan summed: 1 thread 2809 → 1732 ms (0.62x); 8 threads 679 → 537 ms (0.79x). The 8-thread total falls less than the single-threaded one: the schedule (F29) is the next lever.

**The levels, by their worst side before:**

| Level | Worst side (before) | 8 threads: before | after | 1 thread: before | after | every plan, 1 thread: before | after |
|---|---|---|---|---|---|---|---|
| playground/tangent_infinity | [8R] | 12.7 ms | 6.5 | 61.3 | 28.4 | 386 | 167 (0.43x) |
| hit_effects | [12L] | 10.8 ms | 6.8 | 56.5 | 27.5 | 325 | 169 (0.52x) |
| playground/portals | [9R] | 10.2 ms | 6.9 | 43.6 | 24.0 | 285 | 197 (0.69x) |
| crossings | [17L] | 9.3 ms | 7.6 | 40.3 | 29.3 | 409 | 305 (0.75x) |
| playground/projective | [9L] | 9.2 ms | 5.9 | 46.4 | 28.6 | 287 | 211 (0.74x) |
| playground/pre_inverted | [3L] | 7.1 ms | 3.0 | 32.7 | 9.8 | 111 | 35 (0.31x) |
| playground/two_sided | [7L] | 5.7 ms | 3.6 | 26.4 | 11.9 | 137 | 70 (0.51x) |
| main | [9L] | 5.6 ms | 3.9 | 23.8 | 11.8 | 46 | 30 (0.66x) |
| playground/inversion_grid | [3R] | 5.0 ms | 3.4 | 17.6 | 10.5 | 335 | 210 (0.63x) |
| playground/projective_arcs | [6L] | 4.5 ms | 3.1 | 20.7 | 12.9 | 69 | 42 (0.60x) |
| playground/projective_inversion | [6L] | 4.4 ms | 3.6 | 17.3 | 11.4 | 80 | 53 (0.67x) |
| playground/projective_concentric | [6R] | 3.4 ms | 2.8 | 13.3 | 7.9 | 59 | 39 (0.67x) |
| semi_circle_mirror | [5R] | 3.2 ms | 2.6 | 13.6 | 7.1 | 28 | 15 (0.52x) |
| playground/plannable_inversion | [5L] | 3.2 ms | 2.2 | 13.0 | 6.6 | 14 | 7 (0.53x) |

**The worst plans after, and where their time is:**

| Plan | 8 threads after | casts | members | cells | serial ms | tail ms | 1 thread after |
|---|---|---|---|---|---|---|---|
| crossings [17R] | 9.8 ms | 1665 | 507 | 0 | 1.70 | 2.51 | 28.6 |
| crossings [17L] | 7.6 ms | 1665 | 507 | 2 | 1.67 | 0.66 | 29.3 |
| crossings [19L] | 7.0 ms | 1643 | 512 | 24 | 1.32 | 0.98 | 22.3 |
| hit_effects [12R] | 7.0 ms | 1278 | 385 | 0 | 1.30 | 0.62 | 25.6 |
| playground/portals [9R] | 6.9 ms | 1392 | 362 | 45 | 1.03 | 0.88 | 24.0 |
| crossings [20R] | 6.9 ms | 1640 | 511 | 0 | 1.45 | 0.62 | 22.4 |
| hit_effects [12L] | 6.8 ms | 1278 | 385 | 70 | 1.16 | 0.51 | 27.5 |
| playground/portals [9L] | 6.8 ms | 1392 | 362 | 38 | 1.16 | 0.63 | 26.1 |


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
