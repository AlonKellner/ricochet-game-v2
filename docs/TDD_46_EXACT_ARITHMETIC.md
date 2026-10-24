# TDD 46: Exact arithmetic (Stage 141)

The cast-cost program's Stage E (docs/TDD_42_CAST_COST.md, the plan's §3): bit-exact work on the exact layer, built
only for what still costs five percent or more of a region after Stages 137-140. Every change here keeps every
boolean, every limb and every event: the goldens, the region digests (`bench/goldens/regions.json`), the sessions,
the GUT rows and `rc_tool regions`' 256 hashes are the proof.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 141.0 | The profile after Stage 140 (five levels, one thread) and the limb-size histogram | Done (below) |
| 141.1 | The limb storage: the inline capacity measured at 8, 16 and 32 limbs | Done (stays at 8: no gain once the copies are gone) |
| 141.2 | BigFloat without the spare copies: `exact::mul`/`add` with the result's precision passed, `sub` without `neg()`, `add` without `place`'s buffer | Done |
| 141.3 | Fewer products: the duplicated `mx my + my mx` (three sites), `gc_through`'s shared minors | Done |
| 141.4 | The flags' lookups compared in place, the canonical key kept with the value; the keys' interning deferred | Done |
| 141.5 | The measurement after, docs, `make gates`, sign-off | Done (`make gates` ok; the user's interactive sign-off 2026-10-24) |

## 141.0 The profile

`make profile LEVEL=... THREADS=1` on main@04fe6fd (inclusive shares of the region build; `tools/profile_incl.py`):

| Level | candidates | exact:: | BigFloat:: | SmallVec | Line::contains | add_line_endpoints | other |
|---|---|---|---|---|---|---|---|
| crossings | 35% | 22% | 20% | 12% | 9.4% | 10% | replay_climb 12.6%, parallel 5.5% |
| hit_effects | 32% | 18.5% | 10.6% | 6.7% | 8.0% | 9.0% | Flags 11.3%, candidate() 10.4%, present 8.1%, cast_events 5.9% |
| tangent_infinity | 44% | 41% | 31% | 21.5% | -- | -- | tangent 27%, pole_on 26%, pole_tangent 23.5%, start_pole 20%, union_outline 12.6% |
| projective | 36% | 12% | 38% | 18% | 5.6% | 6.5% | replay_climb 34% (the climbs' tiers), classify 11%, geom::hits 9% |
| portals | 32% | 24.5% | 11.6% | 5.7% | 9.0% | 9.3% | apply 11.4%, pole_on 5.3%, tangent 5.2% |

The BigFloat work is arithmetic (`mul` 951 samples of 6779 on projective) and, as much again, storage: `SmallVec::
reserve`, `normalize`'s result buffer, `memmove`/`memset`, the block cache's `allocate`/`delete`/`_tlv_get_addr`
(12-25% together). The exact predicates that cannot be filtered (true cases: a line through the aimed endpoint,
`parallel_to` on axis-aligned walls, `pole_on` on tangent_infinity's carriers through Q) are what the exact layer
spends.

**The limb-size histogram** (`normalize`'s result sizes over a level's regions, one thread): crossings 50% of the
values have more than 8 limbs, 40% more than 16 (90% within 33); projective 63% / 50% (90% within 28);
tangent_infinity 61% / 42% (90% within 56). The ladder's tiers are 4-8 limbs (plus guard limbs); the exact
predicates' values grow with the maps applied (two limbs per reflection, more per inversion).

## 141.1 The inline capacity

Measured alone (one thread, three repetitions, the four levels' medians summed; every region hash equal):

| Level | 8 limbs inline | 16 | 32 |
|---|---|---|---|
| crossings | 90.2 ms | 88.8 | 89.7 |
| projective | 73.5 | 72.6 | 66.2 |
| tangent_infinity | 97.2 | 94.3 | 90.1 |
| hit_effects | 65.8 | 65.3 | 66.1 |

Sixteen limbs buy little (the values that spill are mostly beyond 16); thirty-two buy 7-10% where the exact layer
dominates and nothing elsewhere, at four times the value's size. Decided after 141.2 (below), which changes what is
copied.

## 141.2-141.3 The changes

**141.2 BigFloat without the spare copies** (bit-exact: the same limb operations on the same values).
`exact::mul(a, b)` widened a copy of `a` to `na + nb` limbs (an allocation and a `normalize`) only to set the
result's precision; `exact::add` widened copies of both operands; `sub` negated a copy of its operand; `add` built
two buffers per call (`wb`, `place`'s). Now `BigFloat::mul(o, n)`, `add(o, n)` and `sub(o, n)` take the result's
precision, `add_signed(o, osign, n)` reads the operand's sign without copying it, and `add` places into per-thread
scratch (`place_into`), so an exact operation allocates its result alone. The widened copies had the same value, and
`add` reads its operands by value alignment (`cmp_abs`, `place`), so every result is the same.

**141.3 Fewer products.** `mx my + my mx` was computed as two products at three sites (`reflect_point_h`,
`Line::reflected`'s mirror, `gc_reflect`); `BigFloat::mul` is commutative bit for bit (exact integer column sums),
so one product added to itself. `gc_through` computed each of the six 2x2 minors of rows 1 and 2 twice, in the same
order each time; now once (36 -> 24 products).

After 141.2 and 141.3 (one thread, three repetitions, hashes equal): crossings 90.2 -> 85.2 ms (0.94x), projective
73.5 -> 60.3 (0.82x), tangent_infinity 97.2 -> 80.7 (0.83x), hit_effects 65.8 -> 64.1 (0.97x). `make check` passes
as it is: every golden, digest and session unchanged.

**141.1 decided.** Thirty-two inline limbs re-measured on top of 141.2 (one thread, three repetitions): crossings
85.2 -> 85.6 ms, projective 60.3 -> 59.3, tangent_infinity 80.7 -> 81.5, hit_effects 64.1 -> 64.1 -- within noise once
the spare copies are gone. The capacity stays at 8 (the value stays small).

**141.4 The flags.** The presence conditions and the categorical tables compared `flag_key(v)` -- a `std::string`
built per check -- against their values; `flag_key_is(v, s)` compares the same spelling in place (`present()`,
`entry()`): hit_effects 64.1 -> 62.7 ms (0.98x). The canonical key (`canonical_key()`: sorted entries, one string) is
now computed once per value and carried by its copies (every event's flags): no measurable change on its own (kept:
it removes an allocation per event). What remains of `Flags::` on hit_effects (12.9% inclusive) is `get`'s string
compares and the frame strings of `cast_events` (8%); interning the keys would touch every consumer of a flag key
(the scene, the walker, the tracer, the session's JSON) for a few percent of one level: deferred, with the plan's
other deferred items.

## 141.5 The measurement after

`rc_tool regions --all` (six players, three repetitions), main@04fe6fd against the branch, paired; `make judge`:
no sweep combo, golden field, digest, session or GUT row changed; all 256 region hashes equal at both thread counts.

| | main | Stage 141 |
|---|---|---|
| medians summed, 256 plans, six threads | 209 ms | 194 ms (0.93x) |
| plans over 4 ms, six threads | 12 | 7 |
| the worst side, six threads | 5.22 ms | 5.19 ms |
| medians summed, one thread | 622 ms | 567 ms (0.91x) |
| by level, one thread | tangent_infinity 95.2 -> 80.6 (0.85x), projective 73.4 -> 60.4 (0.82x), crossings 88.0 -> 83.8, hit_effects 66.0 -> 62.2, portals 102.0 -> 98.7, inversion_grid 57.8 -> 53.7 | |

## The cast-cost program's end state

The plan's §1 table (docs/TDD_42_CAST_COST.md), main@1b9f4f0 before Stage 137 against the end of Stage 141, every
plannable side of every level (`rc_tool regions --all`, 256 plans, six players):

| | main 1b9f4f0 (8 threads) | Stage 141 (6 threads) |
|---|---|---|
| plans over 4 ms | 57 | 7 |
| the worst side | 12.7 ms (tangent_infinity) | 5.19 ms (crossings) |
| every plan summed, in parallel | 679 ms | 194 ms (0.29x) |
| every plan summed, one thread | 2809 ms | 567 ms (0.20x) |

Stages 137 (A+B, bit-exact: caches, lazy exact values, the schedule), 138 (shared points stop the arrow, the user's
rule), 139 (C: off-segment crossings are not events), 140 (D: spared members, the empty region at once) and 141 (E:
the exact layer's copies) -- three of the five judged, every judged change approved on its evidence. The deferred
items and their triggers are the plan's table: a pencil index above ~50 surfaces, regions updated from the previous
one, a bounding-volume tree above ~100 surfaces, the flags' interning, the union and fill (F31, judged).

## Tests

| Test | What it checks |
|---|---|
| `goldens: BigFloat` (`bench/goldens/bigfloat.json`), the exact goldens, the corpus, the sweeps | every limb and every boolean unchanged |
| the region digests, the sessions, the GUT rows, `rc_tool regions`' 256 hashes | the regions bit for bit |
