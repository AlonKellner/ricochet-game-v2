# TDD Document 12: Crossing Surfaces

**Stage 108** | Design surfaces may cross. The player plans and clicks *pieces*: the design surfaces split at every proper crossing. Spawned surfaces are trimmed to the piece containing their midpoint.

Specification: GAME_SPEC §9.7 and §10.6.3. Background: TDD_11 (hit effects, spawns, K5).

### Stage Status

| Phase | Topic | Status |
|-------|-------|--------|
| 108.0 | Spec, this document, the design decision (below) | Done |
| 108.1 | Solver: a plan entry may name a piece (its span), oracle first | Done (oracle, native, bindings, ASan) |
| 108.2 | Game: crossings (math), the piece builder, piece-level clicks, hover and plans | Done |
| 108.3 | Spawn trimming (`SpawnTemplate.trim`), preview | Done |
| 108.4 | Levels: the no-crossing guard lifted, a crossing level, interactive tests | Done; user sign-off 2026-10-16 |

**Workflow:**
- branch `stage108-crossings`;
- small green commits;
- goldens byte-identical, and level parity for every shipped level.

## The design decision (user, 2026-10-16)

**Where pieces live.** The first idea put pieces in the solver as separate surfaces sharing their design surface's carrier. Investigation showed that splitting changes **no physical outcome**:
- away from crossings, a piece is its design surface;
- a beam exactly through a crossing hits both design surfaces at one point, the same outcome as the four pieces' junction: a full block, the lowest id first.

Pieces as solver surfaces also have costs:
- **The hit budget:** they add one off-segment event per sibling piece at every carrier crossing, which uses up MAX_HITS and changes traces.
- **Identity rules:** every rule would have to become design-aware (the departure point, the pop on a re-hit of the same mirror, ties).

The user chose **pieces in the game layer**. The solver traces whole design surfaces, and a plan entry may name a piece, which matches only hits inside its span. Clicks, hover, plans, drawing and spawn trimming work on pieces.

**Crossings are represented as the user proposed:** carrier A, carrier B and an *enters* bit. Two carriers meet in two points of the extended plane, counted with multiplicity. At one of them A, oriented start → via → end, enters B; at the other it exits. The sign of A's tangent against B's gradient tells them apart. The math layer finds and orders crossings by it, and rounds each crossing's position once (float32) for the pieces' shared ends.

## What Is Introduced

### 108.1 Solver (oracle first)
- **A plan item may carry a span:** `{start, end, via}` of a piece, on the planned surface's carrier.
- **Planned path:** the matched event's `on_segment` is tested against that span, so a hit outside the piece is off its segment.
- **Physical path:** a plan match also requires the hit point to be inside the span. A hit on the design surface outside the planned piece is an unplanned hit, so the paths diverge.
- An item without a span behaves exactly as before, so goldens and fixtures are unchanged.
- **Oracle:** `trace2.py`, the plan tuples (sid, side) plus an optional span.
- **Native:** `PlanItem.span`, `plan_from_items` / `plan_from_json`, and the plan match in the walker.
- **Tests:**
  - oracle fixtures `gen_fixtures_stage108.py`: a planned piece reached; the other piece of the same mirror hit, giving a divergence; the planned path off the planned piece;
  - native at f64, Dual, 4 and 64 limbs;
  - GUT through the bindings.

### 108.2 Game
- **`scripts/math/crossings.gd` (`Crossings`):** proper crossings of two segments.
  - line × line: exact orientation signs (float32 points in double are exact; `Exact.orient_zero`);
  - line × circle and circle × circle: double arithmetic, with exact incidence tests (`Exact.orient_zero` / `on_circle_zero` / `incircle_zero`) ruling out T-junctions, corners and touches.
  - Each crossing is `{point (float32), enters, other}`, ordered along the carrier by its parameter.
- **`scripts/game/pieces.gd` (`Pieces`):** the world's design surfaces become pieces `{design, index, segment}`.
  - The segment carries the design surface's carrier (native `Segment.with_carrier(s, e, v, carrier)`).
  - Pieces are recomputed on every structural change, ignoring presence.
  - Portals and pre-transform circles are not split.
- **Clicks and hover** resolve a piece (`ClickDetector` returns `piece`). `PlanManager.PlanEntry.piece` holds it, and plan items carry the piece's span.
- **Chevrons and plan overlays** are drawn over the piece. The design surface is drawn whole, with a small tick at each crossing.

### 108.3 Spawn trimming
- `SpawnTemplate.trim` (default true).
- `LandingFold.place` splits the placed spec against the surfaces present under the landed flags (`Crossings`), and keeps the piece containing the midpoint. For a full circle it keeps the longest arc (a spawned circle is centred on the hit point by default, so "nearest the hit" is undefined).
- The preview uses the same function.

### 108.4 Levels
- The no-intersecting-segments guard (`test_invariant_combos.gd`) is lifted: pieces never cross.
- New level `scenes/test_levels/crossings.tscn`, built by a generator. It covers:
  - the counting cases (4, 7, 9 pieces);
  - line × circle and circle × circle;
  - a mirror crossed by a block (two planning targets);
  - a spawner whose spawns get trimmed.

## Unit Tests Added

| File | Covers |
|---|---|
| oracle `test_trace2.py` + `gen_fixtures_stage108.py` | Piece plan entries: reached, diverged on the sibling piece, planned path off the piece |
| native `test_crossings.cpp` | The fixtures at f64, Dual, 4 and 64 limbs; a plan without spans is unchanged |
| `test_stage108_crossings.gd` | Proper crossings: line × line, line × circle, circle × circle; T-junctions, corners and touches not split; the enters bit; order along a carrier |
| `test_stage108_pieces.gd` | The counts 4 / 7 / 9; shared ends; the carrier kept; piece-level clicks and plans; crossing-free levels have one piece per surface |
| `test_stage108_trim.gd` | Trimming keeps the midpoint piece; full circles; opt-out; preview = landing |
| `test_stage108_level.gd` / `_level_sweep.gd` | The crossing level builds and plays; invariant sweep (plans on pieces) |

## Invariants

- **NO-PIECE-CROSSINGS:** no two pieces of a built world properly cross.
- **PIECES-PARTITION:** a design surface's pieces cover it exactly. Consecutive pieces share their ends, the first starts at its start, and the last ends at its end.
- **SPANLESS-PLAN PARITY:** a plan without pieces traces as before Stage 108 (goldens).

## Interactive User Tests

On `scenes/test_levels/crossings.tscn`, generated by `scripts/testing/make_crossings_level.gd`. The player starts at (300, 500).
1. **Pieces are visible.** Each crossing shows a small dot, and hovering highlights only the piece under the cursor:
   - the mirror at x = 600 crossed by a block (4 pieces);
   - the mirror at y = 600 crossed by two blocks (7);
   - the triangle of mirrors (9);
   - the mirror circle cut by a block (5);
   - the two overlapping circles (4).
2. **Planning a piece.** Click the upper piece of the x = 600 mirror and aim so the planned path reflects off it. The plan reads as valid, and the label "1" sits on that piece. Plan the lower piece with the same aim: the plan shows the hit as missed (red), and the shot diverges.
3. **Shooting is unchanged.** A shot reflects off a crossed mirror exactly as off an uncrossed one, on either piece.
4. **Trimming.** Shoot the spawner at x = 450 near the floor (e.g. from (300, 800) aiming right). The preview's dashed outline already shows the trimmed mirror, from the hit up 150 px and down to the floor. The landed mirror matches it. The reset on the floor line (y = 880) removes it.
5. **Undo (Z) and reset (R)** restore the world. Nodes and pieces follow.

## Files Modified

- **Spec and docs:** `GAME_SPEC.md` (§9.7, §10.6.3, the status table), this document.
- **Oracle:** `trace2.py` (plan spans), `gen_fixtures_stage108.py`, `bench/fixtures/stage108_piece_plans.json`.
- **Native:**
  - `PlanItem.span` and the plan match (`solver.hpp`), the tracer's plan key (`tracer.hpp`);
  - `plan_from_items` / `BeamSolverPlanItem.span`, `TracePath.physical_hits` (`point`);
  - `Segment.with_carrier`;
  - tests: `tests/test_crossings.cpp`, `support/json_defs.hpp`.
- **Math:** `scripts/math/crossings.gd` (new); `ChevronOverlay.piece`; the plan-overlay builder.
- **Game:**
  - `scripts/game/pieces.gd` (new);
  - `world.gd` (`pieces()`), `level_settings.gd` (a node per piece; room walls through the same path);
  - `surface_node.gd` (draws its piece), `click_detector.gd`, `plan_manager.gd`, `checkpoint.gd`, `game_manager.gd`;
  - `landing_fold.gd` (trimming), `spawn_template.gd` (`trim`).
- **Levels:** `scenes/test_levels/crossings.tscn` and its generator.
- **Tests:** `test_stage108_*.gd` (7 files); `test_invariant_combos.gd` (NO-PIECE-CROSSINGS replaces the no-crossing guard); `test_stage81_trace_rules.gd` (plan spans).
- **Runners:** `run_tests_parallel.sh` and `Makefile` (the level sweep is a sharded sweep file).
