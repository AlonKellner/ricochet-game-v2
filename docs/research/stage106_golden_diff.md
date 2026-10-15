# Stage 106: the departure point after a pop — the changed pinned traces, judged

Base: `main` at dc7886f (Stage 105 merged). Branch `stage-106-pop-departure`. The user chose an
exact rework of K2 with a checkpoint before any re-bless (2026-10-15).

## What changed in the solver (oracle `trace2.py` first, native `solver.hpp` bit-identical)

1. **A pop tags the departure point.** The old beam is restored with its pre-push known points
   (`known = top.known`); the pop point is now tagged as that beam's departure point exactly as a
   push tags its hit point: the known point that *is* the pop point (by data identity when the
   point is exact, or by its parameter on the beam, `t == t_res`) gets `skip = the surface` and
   `t = t_res`; an exact pop point not yet known is added (not for a portal: its mapped-back point
   is a rounded computation). Without the tag a known point at the pop point (the player or the
   cursor standing exactly on a circle mirror) snapped the departing carrier's *other* root onto
   itself: the other crossing was lost, or re-hit as a phantom at the departure point (K2).
2. **A resumed beam identifies its departure root structurally.** After a pop the departing
   carrier's roots are recomputed on the restored beam and can land an ulp off `t0`; the root
   *nearest* `t0` is now the departure (one root dropped), instead of roots exactly equal to `t0`.
   The old stray near-duplicate root was removed by accident by defect 1's snap; alone, defect 1's
   fix exposed it (a zero-length loop: the effect re-applied at the pop point). Stage 105's
   "departing from Q: the root nearest Q" block became a special case and is gone.
3. **The oracle's departure re-hit after a pop takes the tagged point** (`dep_pt`), as native did:
   both stay bit-identical.
4. **A new invariant `REAPPLY-IN-PLACE`** (the sweeps, the live checker, GUT): an applied hit, then
   only cursor / infinity events at the same point, then an applied hit on the same surface at the
   same point is a zero-length loop (a teleport resets it: a round trip through a portal pair
   legitimately re-hits the same portal at the same point).

## What was regenerated, and what did not change

- `bench/fixtures/stage8x..105` (every `gen_fixtures_stage*.py`) and `bench/scenes_corpus` (396
  scenarios): **no change** (their positions never put a known point on a circle at a pop).
- `bench/expected`, `bench/stress` (`regen_bench_v2.py`, 161 + 40 scenarios, stable at 1920 digits): **no change**; `make bench` identical to the recorded table (ladder 161/161, beam_f64 physical 112/161, beam_big112 physical 160/161).
- `bench/goldens/fixtures.json`, `corpus.json`, `chain.json`, `exact.json`, `beam.json`: **no
  change** (the `rc_tests` ctest entry stays green).
- `bench/goldens/sweeps.json`: **1,795 of 284,096 combos change (0.63%)**, in 35 of 117 groups.
  The GDScript goldens pin the bugs, so they cannot judge the change; an independent judge did.

## The judge

`docs/research/stage106_judge.py` (mpmath, 200 digits). Both builds dumped the 1,795 combos
(`RC_SWEEP_DUMP`, `main` built in a scratch worktree with the dump tooling cherry-picked): every
event with its point and the beam it lies on. Where the two traces first differ in a hit event,
both agree on the prefix, so the beam of that event is the same restored beam; the judge takes
that beam and the departure point D (the previous hit's point; the beam's own origin after a fresh
departure; the pop point mapped back through the portal after a portal pop), computes every
crossing of the beam with every carrier, drops the one at D, orders them as a beam is walked
(ahead of D first, then the wrapped ones) and names the next crossing. None of the walker's rules
take part; only the root algebra of `oracle/geometry.py` / `beam.py` is reused.

| class | combos | meaning |
|---|---|---|
| fix | **1,784** | the new trace shows the judge's crossing, the old one does not |
| regression | **0** | the old trace was right and the new one is not |
| both-wrong | **0** | neither shows the judge's crossing |
| applied-differs | 6 | the same crossing in both, applied in the new trace only (below) |
| undecidable | 5 | no hit differs: the ladder's tier, and with it the last ulp of a beam (below) |

By sweep: all_scenes 150 (146 fix, 4 tier-only), combos 788 (all fix), precision 508 (all fix),
with_plans 349 (342 fix, 6 applied-differs, 1 tier-only). By group: see
`stage106_judge.py` output; every `combos/*` and `precision/*` group is 100% fix.

**The old traces' shapes** (all three gone):
- *A phantom re-hit at the pop point.* `all_scenes/circle_no_walls/[]` combo 43: the beam pops at
  the cursor (1160, 540) on the circle; old: `hit 1*` again at (1160, 540) (the circle's far root
  snapped onto the cursor), new: wall 3 at (1920, 893.8), the judge's crossing (margin 441 px).
- *A lost crossing.* `precision/no_plan/[]` combo 41: the player (820, 380) exactly on inversion
  circle 1; after the pop at the player the old trace re-hit circle 1 at the player; the new trace
  goes on to circle 2 at (924.7, 428.5), the judge's crossing. On the inversion grid the beam that
  returns to its entry point is a closed orbit (the line through the center maps to itself): the
  new traces alternate between the two crossings until the event budget, as the oracle does.
- *A zero-length re-teleport.* `all_scenes/portals/[]` combo 1114: the beam pops out through the
  portal pair at (1190, 440), i.e. arrives at (640, 440) on the source arc; old: `hit 8*` (a
  teleport) again at (640, 440); new: wall 4 at (560, 283.2), the judge's crossing.
- In the dumps, **276 of the 3,590 old traces** (planned + physical of the 1,795 combos) contain an
  in-place re-apply by the new invariant's rule; **0 of the new traces** do.

**applied-differs (6)**: `with_plans/full_circle_mirror/[5/L,5/L]` ×5, `room_with_inversion/[5/L,5/L]`
×1, planned traces. The cursor sits exactly at the pop point and the plan completes at the pop; the
rule "the cursor exactly at the departure point is reached immediately" (native 1377, oracle 666)
now fires after a pop too, because the pop point is tagged: the cursor event moves from after a
full extra loop (old: 10–20 hits in) to right at the pop (new), and the wall crossing that follows
is a post-cursor physical hit (applied, terminal) instead of a planned-mode pass. Same geometry in
both; the new order is the rule's.

**undecidable (5)**: `all_scenes/ghost_room/[]` ×4 and `with_plans/full_circle_mirror/[5/R]` combo
2697: not one event differs; the ladder's tier does (ghost_room: old climbed to 4, 8 and 64 limbs,
new converges at float64; 2697: planned 0 → 4 limbs), and a tier change re-renders the beam's last
ulp. Over the 1,795 combos the physical trace's tier went 64 → 0 limbs in 121, 8 → 0 in 21, 4 → 0
in 29 (the fake near-duplicate root made a decision margin tiny), and 0 → 4 in 5 planned traces.

## Tests and gates on the new code

- Oracle `test_trace2.py`: 113 passing (+5 Stage 106: K2, the cursor on a reflective arc, the
  closed orbit, a known point at the other crossing, a portal pop; 80 vs 200 digits).
- `bench/fixtures/stage106_pop_departure.json` (9 fixtures, each asserting the pop tagged a known
  point); native at F64, Dual, 4 and 64 limbs (`test_infinity.cpp`); `test_pop_departure.cpp`: the
  tag (K2), 1,080 random walks with players and cursors exactly on circle mirrors (7,579 pops):
  identical decisions at every tier, no REAPPLY-IN-PLACE.
- GUT: 794 passing / 0 failing (`test_stage106_pop_departure.gd`; the sweeps with the new
  invariant and with on-surface positions; **K3** reported as a known finding: on
  `tangent_infinity.tscn`, plan [6/L] or [6/R], the cursor the player's mirror image makes the
  aim image the player (every direction solves the plan); native picks the arc through Q0 and
  reflects there in place, the oracle picks the other arc; pre-existing, identical in `main`).
- ctest: everything green except the six `sweeps.json` parts (the 1,795 combos above), pending the
  re-bless (`RC_SWEEP_REBLESS=1` → `apply_rebless.py`).
