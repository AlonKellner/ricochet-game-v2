# TDD Document 7: Game Systems

**Stages 52--60** | State-conditional surfaces, multi-shot puzzles, targets, win condition, game loop, menus, HUD, save system

> **Status: PLANNED** — All stages in this document are pending implementation. Note: stage number 52 conflicts with test file `test_stage52_norm_drift.gd` which covers a different topic (norm drift fix from v1.2.0). Some infrastructure exists: `GameState` class, `SideConfig.state_change` field, `Surface.is_target`, `TracedPath.targets_hit`.

### Stage Status

| Stage | Topic | Status |
|-------|-------|--------|
| 52 | CategoricalResolver and State-Conditional Surfaces | Planned |
| 53 | State Changes on Hit | Planned |
| 54 | State Simulation During Planning | Planned |
| 55 | Target Surfaces and Hit Detection | Planned |
| 56 | Win Condition and Multi-Shot Puzzles | Planned |
| 57 | Game Loop and Level Loading | Planned |
| 58 | Menus (Main, Level Select, Pause) | Planned |
| 59 | HUD | Planned |
| 60 | Save System | Planned |

**Regression Test Policy:** After implementing Stage N, run ALL tests from Stages 1 through N. The full test suite must pass before proceeding to Stage N+1. No exceptions.

**Feedback Loop Protocol (applies to every stage):**
1. AI implements the stage (code + unit tests).
2. AI runs all unit tests (new + all prior). All must pass.
3. User performs interactive tests from the checklist.
4. User provides feedback (pass/fail per item + notes on unexpected behavior).
5. AI converts any failing interactive tests into automated tests.
6. AI applies fixes and re-runs all tests.
7. Repeat steps 3--6 until all checks pass.
8. Stage is marked complete. Proceed to next stage.

**USER VALIDATION GATE (MANDATORY)**

No stage is complete until the user has personally verified every interactive test item and provided explicit sign-off. After every behavioral feature (not just infrastructure), the user must SEE the behavior working in the running game before the stage is marked complete.

1. User MUST test every interactive test item marked with `[ ]`.
2. User MUST provide written pass/fail feedback for EACH item.
3. Any "fail" or "unexpected behavior" becomes a NEW automated test before proceeding.
4. AI MUST NOT proceed to the next stage until ALL interactive items are `[x]`.
5. The user's word is final. If the user says it doesn't look right, it doesn't ship.

---

## Stage 52: CategoricalResolver and State-Conditional Surfaces

### Overview
Implement `CategoricalResolver` -- a `ConfigResolver` subtype that selects per-side configuration from a lookup table keyed by a game state flag. This stage also introduces `GameState` (a `flags` dictionary) and wires `active_side_config()` to dispatch through the resolver. Together these enable surfaces whose behavior changes at runtime (e.g., a mirror that becomes pass-through after being "broken").

Extends the GameState class introduced in Stage 12 with CategoricalResolver integration.

### Prerequisites
TDD_05 Stage 78 + TDD_06 Stage 38 (full math layer, all effect types, visibility system, physical trace, step tree, planning, arrow flight, checkpoints, arc rendering).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/game/game_state.gd` -- Modified to integrate CategoricalResolver dispatch (created in Stage 12) | §18.2, §9.4 |
| Script | `scripts/game/categorical_resolver.gd` -- CategoricalResolver extends ConfigResolver | §9.4 |
| Property | `CategoricalResolver.state_key: StringName` -- which game state key to read | §9.4 |
| Property | `CategoricalResolver.config_table: Dictionary[Variant, {left: SideConfig, right: SideConfig}]` | §9.4 |
| Method | `CategoricalResolver.resolve(side, game_state) -> SideConfig` | §9.4 |
| Method | `Surface.active_side_config(side, game_state) -> SideConfig` -- dispatches through resolver | §9.4 |
| Behavior | Missing key fallback: try `game_state[state_key]` -> if absent, try `DEFAULT` in config_table -> if absent, first entry in table. Warning in debug builds. | §9.4 |
| Data | `GameState.flags` -- mutable dictionary of game state flags | §18.2 |
| Method | `GameState.copy() -> GameState` -- deep copy for trace isolation | §18.2 |

### Unit Tests Added

1. **`test_stage52_categorical_resolver_basic`**: Create CategoricalResolver with `state_key="wall_intact"`, config_table `{true: {left: Block, right: Block}, false: {left: PassThrough, right: PassThrough}}`. Resolve with `game_state.flags["wall_intact"] = true`. Expected: left side returns Block SideConfig. Validates: §9.4 basic dispatch.
2. **`test_stage52_categorical_resolver_state_false`**: Same resolver as test 1. Resolve with `game_state.flags["wall_intact"] = false`. Expected: left side returns PassThrough SideConfig. Validates: §9.4 state-dependent dispatch.
3. **`test_stage52_categorical_resolver_right_side`**: Same resolver. Resolve `RIGHT` side with `wall_intact = true`. Expected: returns right-side Block config. Validates: §9.4 side-aware dispatch.
4. **`test_stage52_missing_key_fallback_default`**: CategoricalResolver with `state_key="mode"`, config_table `{DEFAULT: {left: Reflection, right: null}, "active": {left: Block, right: Block}}`. Resolve with game_state that has no `"mode"` key. Expected: returns DEFAULT entry (Reflection). Validates: §9.4 missing key fallback.
5. **`test_stage52_missing_key_fallback_first_entry`**: CategoricalResolver with no DEFAULT key in config_table, and game_state missing the state_key. Expected: returns first entry in table. Warning emitted in debug. Validates: §9.4 final fallback.
6. **`test_stage52_fixed_resolver_unchanged`**: Existing FixedResolver still works identically -- resolve ignores game_state, returns fixed left/right configs. Validates: §9.4 backward compatibility.
7. **`test_stage52_active_side_config_dispatches`**: Surface with CategoricalResolver. Call `surface.active_side_config(LEFT, game_state)`. Expected: returns resolver's resolved config. Validates: dispatch wiring.
8. **`test_stage52_resolver_string_keys`**: CategoricalResolver with `state_key = "door_state"`, `config_table = {"open": {left: null, right: null}, "closed": {left: Block, right: Block}, "broken": {left: null, right: null}}`. Set `game_state["door_state"] = "closed"`. Resolve: returns Block. Change to "open": returns null. Validates: non-boolean Variant keys work correctly.
_(GameState construction and copy tests are in Stage 12, TDD_02. Only CategoricalResolver-specific tests are included here.)_

### Interactive User Tests

- [ ] Press Play. Existing level still renders and behaves identically (no regressions from new resolver infrastructure).
- [ ] If a test level with a CategoricalResolver surface is available: observe the surface renders in the color matching its initial resolved effect.
- [ ] Verify the trajectory preview still works with all prior effect types.
- [ ] Verify plan construction, removal, and visibility are unaffected.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S16 | No NaN/Inf in output | Unit test: resolver returns valid configs | Inherited |
| S1 | Cache: carrier round-trip | GUT tests (inherited) | Inherited |
| S12 | Side determination consistent | GUT tests (inherited) | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | Direction, Ray, Segment, GeneralizedCircle, MobiusTransform, TransformCache all pass | GUT math tests |
| Effects | All 8 effect types (Reflection, CircleInversion, RigidMotion, LineNormalProjection, CircleNormalProjection, SemicircleDirectionalProjection, CompoundTransformative, Terminal) behave correctly | GUT effect tests |
| Trace | Physical trace loop produces correct steps | GUT trace tests |
| Planning | Plan construction, image chains, mixed planning all function | GUT planning tests |
| Visibility | Multi-step visibility, circle visibility, all-effects visibility correct | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual: fire arrow, observe flight |
| Checkpoints | Undo/reset, plan retained after shot | Manual: fire, undo, verify |
| UI | Arc rendering, surface colors (blue/purple/cyan/orange/red/gray) | Visual inspection |

### Expected Visual State

Identical to Stage 51. CategoricalResolver is infrastructure -- no new visual elements. Surfaces still render in their effect colors. If a test level uses a CategoricalResolver, the surface color reflects the resolved effect for the current game state.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--51) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (S16, S1, S12) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/game_state.gd` | Modify | Extend GameState with CategoricalResolver integration |
| `scripts/game/categorical_resolver.gd` | Create | CategoricalResolver with state_key, config_table, resolve() |
| `scripts/game/config_resolver.gd` | Modify | Ensure base ConfigResolver and FixedResolver accept game_state parameter |
| `scripts/game/surface.gd` | Modify | Add active_side_config(side, game_state) dispatch method |
| `tests/test_stage52_categorical_resolver.gd` | Create | CategoricalResolver and GameState unit tests |

---

## Stage 53: State Changes on Hit

### Overview
Implement `StateChange` -- an optional mutation attached to a surface side that modifies game state when hit during the trace. State changes are orthogonal to effects: a surface can have both an effect AND a state change on the same side. During trace, state changes are applied to a COPY of game state, preserving the real state for preview integrity. This is the foundation for multi-shot puzzles where one shot alters the level for subsequent shots.

### Prerequisites
Stage 52 (CategoricalResolver and GameState).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/game/state_change.gd` -- StateChange class with `key: StringName`, `value: Variant` | §10.6 |
| Property | `SideConfig.state_change: StateChange` (optional, may be null) | §10.6 |
| Behavior | On hit during trace: if `side_config.state_change` exists, apply to game_state copy | §10.6, §12.1 |
| Behavior | State changes applied sequentially along trace; last writer wins for same key | §10.6 |
| Behavior | A surface can have BOTH an effect AND a state_change on the same side | §10.6 |
| Behavior | state_change can write to ANY key (not just surface's own state_key) -- enables inter-surface mechanics | §10.6 |
| Behavior | trace() operates on a COPY of game_state; real state unchanged during preview | §12.1 |
| Invariant | S19: trace preserves real state | §29.3 |

### Unit Tests Added

1. **`test_stage53_state_change_construction`**: Create StateChange with `key="wall_intact"`, `value=false`. Expected: key and value stored correctly. Validates: §10.6 data structure.
2. **`test_stage53_state_change_applied_during_trace`**: Set up a surface with Block + StateChange("wall_intact", false) on left side. Trace arrow that hits it. Expected: the trace's game_state copy has `wall_intact = false` after the hit. Validates: §10.6 application.
3. **`test_stage53_S19_real_state_preserved`**: Set up game_state with `wall_intact = true`. Run trace that hits a surface with StateChange("wall_intact", false). After trace completes, check: real game_state still has `wall_intact = true`. Validates: S19.
4. **`test_stage53_sequential_state_changes`**: Two surfaces in trace order. Surface A writes `key_a = 1`. Surface B writes `key_b = 2`. After trace: copy has both `key_a = 1` and `key_b = 2`. Validates: sequential application.
5. **`test_stage53_last_writer_wins`**: Two surfaces both write to `key_x`. Surface A (hit first) writes `key_x = "alpha"`. Surface B (hit second) writes `key_x = "beta"`. After trace: copy has `key_x = "beta"`. Validates: §10.6 last writer wins.
6. **`test_stage53_effect_and_state_change_coexist`**: Surface with Reflection + StateChange on left side. Trace arrow hitting it. Expected: arrow reflects AND state change is applied to copy. Validates: §10.6 both effect and state_change.
7. **`test_stage53_cross_surface_state_change`**: Surface A has CategoricalResolver with state_key="switch". Surface B has StateChange writing to "switch". Trace hits B first, then A. Expected: A sees the updated "switch" value written by B. Validates: §10.6 inter-surface mechanics.
8. **`test_stage53_state_change_null`**: Surface side with an effect but no state_change (null). Trace hits it. Expected: game_state copy unchanged by this hit. Validates: null state_change is no-op.
9. **`test_stage53_conditional_surface_changes_behavior`**: CategoricalResolver surface with `state_key="wall_intact"`. Initial state: `wall_intact = true` (Block). Another surface earlier in trace writes `wall_intact = false` via StateChange. Expected: when trace reaches the CategoricalResolver surface, it resolves to the `false` config (PassThrough) and arrow passes through. Validates: state changes propagate within a single trace.
10. **`test_stage53_passthrough_with_state_change`**: Surface with null effect (pass-through) on left side AND a StateChange on the same side. Arrow hits it from the left. Expected: arrow passes through (no effect applied), AND state change fires correctly (game state copy updated). Validates: §10.6 "orthogonal to the three effect categories" — state changes work even with null effects.
11. **`test_stage53_player_solid_persists_after_effect_change`**: Surface has `player_solid=true`. CategoricalResolver changes effect from Block to null (pass-through). Arrow now passes through. Expected: player STILL collides with the surface (collision shape remains because `player_solid` is static, independent of effect). Validates: §25.1 `player_solid` is per-surface, not per-effect.

### Interactive User Tests

- [ ] Press Play. Existing levels behave identically (state changes are opt-in; surfaces without state_change are unaffected).
- [ ] All prior effect types still function correctly.
- [ ] Trajectory preview does not alter the real game state (verify by firing: preview should be repeatable before firing).
- [ ] If a test level with a state-changing surface exists: fire at it, observe the state change takes effect (surface color or behavior changes post-shot).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S19 | Trace preserves real state -- after preview, GameState.flags is unchanged | Unit test: compare flags before and after trace | Yes |
| S16 | No NaN/Inf in output | Unit test | Inherited |
| S8 | Forward-first hit ordering -- state changes applied in hit order | Unit test: sequential application matches hit order | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms pass | GUT math tests |
| Effects | All 8 effect types behave correctly | GUT effect tests |
| Trace | Physical trace loop (without state changes) unchanged | GUT trace tests |
| Planning | Plan construction, image chains, mixed planning | GUT planning tests |
| Visibility | All visibility computations correct | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Undo/reset, plan retained after shot | Manual verification |
| UI | Arc rendering, all surface colors | Visual inspection |
| Resolver | FixedResolver and CategoricalResolver dispatch correctly | GUT Stage 52 tests |

### Expected Visual State

Identical to Stage 52. State changes are logic-layer; no new visual elements are introduced. If a test level includes state-changing surfaces, their initial appearance matches the resolved effect for the initial game state.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--52) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (S19, S16, S8) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/state_change.gd` | Create | StateChange data class (key, value) |
| `scripts/game/side_config.gd` | Modify | Add optional state_change field |
| `scripts/math/tracer.gd` | Modify | Apply state changes to game_state copy during trace loop |
| `tests/test_stage53_state_changes.gd` | Create | State change unit tests including S19 |

---

## Stage 54: State Simulation During Planning

### Overview
Extend the planner to simulate state changes during plan construction. The planner walks the plan FORWARD, applying state changes to a temporary copy, producing per-entry state snapshots (`state_at[]`). This stage also implements Pass 0 of `plan_mixed` -- the iterative state convergence loop that resolves the circular dependency between state, effect, and geometry. Terminal pre-pass ensures entries after a terminal are unconditionally skipped.

### Prerequisites
Stages 52--53 (CategoricalResolver, GameState, StateChange, state changes during trace).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Planner forward-walks plan applying state changes to temp copy | §13.8 |
| Data | `state_at: Array[GameState]` -- per-entry state snapshots | §13.8 |
| Behavior | Pass 0 of plan_mixed: iterative state convergence | §13.4 (Pass 0) |
| Behavior | Terminal pre-pass: entries after terminal unconditionally skipped | §13.4 (Pass 0) |
| Behavior | Iterate until reachable set stabilizes (max 10 iterations) | §13.4 (Pass 0) |
| Behavior | Temporary state discarded after planning | §13.8 |
| Invariant | S7: per-entry state matches between planner and trace | §29.3 |

### Unit Tests Added

1. **`test_stage54_state_at_basic`**: Plan with 3 entries, no state changes. Expected: `state_at[0]`, `state_at[1]`, `state_at[2]` all equal the initial game_state. Validates: baseline state propagation.
2. **`test_stage54_state_at_with_changes`**: Plan entry 0 has StateChange("key_a", 1). Entry 1 reads "key_a" via CategoricalResolver. Expected: `state_at[0]` has original value, `state_at[1]` has `key_a = 1`. Validates: §13.8 forward propagation.
3. **`test_stage54_state_at_chain`**: Entry 0 writes "x" = 1. Entry 1 writes "y" = 2 (conditional on "x" = 1 via CategoricalResolver). Entry 2 reads "y". Expected: `state_at[2]` has both "x" = 1 and "y" = 2. Validates: chained state changes.
4. **`test_stage54_skipped_entries_skip_state_changes`**: Entry 0 is skipped (unreachable). Entry 0 has StateChange("key_a", 1). Entry 1 reads "key_a". Expected: `state_at[1]` does NOT have `key_a = 1` (skipped entry's state change not applied). Validates: §13.4 Pass 0 exclusion.
5. **`test_stage54_terminal_pre_pass`**: Plan: [Reflection, Terminal, Reflection]. Expected: entry 2 (after terminal) is unconditionally skipped. Validates: §13.4 terminal pre-pass.
6. **`test_stage54_state_convergence`**: Construct scenario where reachability depends on state: surface A writes "switch" = false. Surface B uses CategoricalResolver on "switch" (true -> Reflection, false -> PassThrough). B is planned after A. When A is reachable, B becomes PassThrough (skipped). When B is skipped, A's image chain changes. Expected: convergence within 2 iterations. Validates: §13.4 iterative convergence.
7. **`test_stage54_convergence_max_iterations`**: Pathological config that would loop forever. Expected: loop exits after 10 iterations with best-effort reachable set. Validates: safety limit.
8. **`test_stage54_S7_state_at_matches_trace`**: For aligned steps: planner's `state_at[i]` matches the physical trace's game state at step i. Set up a plan with state changes. Run both planned and physical traces. Compare state at each aligned step. Validates: S7.
9. **`test_stage54_temp_state_discarded`**: After planning, verify the real game_state is unchanged. Validates: §13.8 temporary state isolation.
10. **`test_stage54_planning_uses_resolved_effects`**: Plan entry with CategoricalResolver. State changes from prior entry cause it to resolve differently. Expected: planner uses the correctly resolved effect (not the initial one). Validates: state-aware effect resolution in planning.
11. **`test_stage54_convergence_max_iterations_behavior`**: Construct a pathological configuration where the reachable set doesn't converge within 10 iterations (e.g., 3+ surfaces with circular state dependencies). Expected: planner uses the last computed reachable set after 10 iterations (best-effort). No crash, no infinite loop. A debug warning is logged. Validates: §13.4 safety limit behavior after max iterations.
12. **`test_stage54_convergence_requires_multiple_iterations`**: Construct a scenario where the reachable set changes between iteration 1 and iteration 2 before stabilizing at iteration 2. Verify convergence takes exactly 2 iterations (not 1). Validates: the iterative loop is actually needed -- single-pass would produce wrong results.
13. **`test_stage54_convergence_cross_entry_dependency`**: Surface A's state change affects whether surface B is reachable. Surface B's state change affects whether surface A is reachable. Verify the loop resolves both correctly after convergence. Validates: cross-entry state dependency resolution.
14. **`test_stage54_reachability_affects_visibility_via_state`**: Surface A has a state change that modifies surface B's state_key. If A is unreachable, its state change doesn't fire, so B keeps its original behavior. If A IS reachable, B's behavior changes (e.g., B becomes pass-through instead of reflective). Set up geometry where reachability of A (determined by cursor position) changes whether B is reflective or pass-through, which changes the visibility region shape. Verify the visibility region matches the correct reachability determination. Validates: state simulation -> reachability -> visibility dependency chain.

### Interactive User Tests

- [ ] Set up a plan with state-changing surfaces. Observe the preview updates correctly as the plan changes.
- [ ] Plan a state-changing surface followed by a CategoricalResolver surface. Verify the preview shows the second surface's resolved behavior (post-state-change effect).
- [ ] Fire and undo. Verify the preview returns to its pre-shot state.
- [ ] Verify all prior planning behaviors (mixed chains, projective sub-chains) still work.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S7 | Per-entry state matches: planner's state_at[i] equals trace's state at step i | Unit test comparing state snapshots | Yes |
| S19 | Trace preserves real state | Unit test (inherited from Stage 53) | Inherited |
| S16 | No NaN/Inf in output | Unit test | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes | GUT trace tests + Stage 53 tests |
| Planning | Image chains, mixed planning (without state changes) | GUT planning tests |
| Planning | State changes during planning | GUT Stage 54 tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Undo/reset, plan retained | Manual verification |
| UI | Arc rendering, all surface colors | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT Stage 52 tests |
| State | State changes applied during trace, real state preserved | GUT Stage 53 tests |

### Expected Visual State

Preview now accounts for state changes along the plan. If a plan includes a surface that writes a state change and a subsequent surface whose effect depends on that state, the preview reflects the post-change effect. For example, planning through a wall-breaker followed by a wall shows the preview passing through the (now-broken) wall. All prior visual elements unchanged.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--53) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (S7, S19, S16) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/planner.gd` | Modify | Add Pass 0 (state simulation + convergence), state_at computation |
| `scripts/math/planner.gd` | Modify | Integrate state convergence loop (max 10 iterations) |
| `tests/test_stage54_state_simulation.gd` | Create | State simulation during planning tests including S7 |

---

## Stage 55: Target Surfaces and Hit Detection

### Overview
Introduce target surfaces -- surfaces the player must hit to complete a level. The `is_target` flag marks a surface as a goal. During trace, `targets_hit` (a set of surface IDs) is populated whenever the arrow contacts a target surface, regardless of the surface's effect (pass-through, reflective, or block). Target surfaces are rendered in gold with pulsing animation for unhit targets and dimmed appearance with checkmark for previously hit targets.

### Prerequisites
Stages 1--54 (full trace system with state changes, planning with state simulation).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Property | `Surface.is_target: bool` -- marks a surface as a goal | §24.1 |
| Data | `targets_hit: Set[int]` -- surface IDs of targets hit during trace | §24.2 |
| Behavior | Target hit registered at moment of contact regardless of effect | §24.2 |
| Behavior | Pass-through target: arrow passes through, hit registered | §24.2 |
| Behavior | Reflective target: arrow bounces, hit registered | §24.2 |
| Behavior | Block target: arrow stops, hit registered | §24.2 |
| Visual | Gold color for target surfaces (§22.2) | §22.2 |
| Visual | Gold endpoint markers or outline on target surfaces | §22.2 |
| Visual | Pulsing animation for unhit targets | §22.1 |
| Visual | Dimmed + checkmark for previously hit targets | §22.1 |
| Behavior | targets_hit NOT part of GameState -- tracked separately by game manager | §24.2 |

### Unit Tests Added

1. **`test_stage55_is_target_flag`**: Create surface with `is_target = true`. Expected: `surface.is_target == true`. Create surface with `is_target = false` (default). Expected: `surface.is_target == false`. Validates: §24.1.
2. **`test_stage55_target_hit_on_pass_through`**: Target surface with PassThrough effect. Trace arrow through it. Expected: `targets_hit` contains the surface's ID; arrow continues past. Validates: §24.2 pass-through target.
3. **`test_stage55_target_hit_on_reflection`**: Target surface with Reflection effect. Trace arrow hitting it. Expected: `targets_hit` contains the surface's ID; arrow reflects. Validates: §24.2 reflective target.
4. **`test_stage55_target_hit_on_block`**: Target surface with Block (Terminal) effect. Trace arrow hitting it. Expected: `targets_hit` contains the surface's ID; arrow stops. Validates: §24.2 block target.
5. **`test_stage55_non_target_not_tracked`**: Non-target surface. Trace arrow hitting it. Expected: `targets_hit` does NOT contain the surface's ID. Validates: only target surfaces tracked.
6. **`test_stage55_multiple_targets_single_trace`**: Two target surfaces in trace path. Expected: both surface IDs in `targets_hit`. Validates: multiple target tracking.
7. **`test_stage55_targets_hit_empty_initial`**: Before any trace, `targets_hit` is empty. Validates: initialization.
8. **`test_stage55_targets_hit_not_in_game_state`**: Verify `targets_hit` is separate from `GameState.flags`. Modifying GameState does not affect targets_hit and vice versa. Validates: §24.2 separate tracking.
9. **`test_stage55_target_with_state_change`**: Target surface with Block + StateChange. Trace hits it. Expected: target hit registered AND state change applied. Validates: target detection + state change coexist.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Load a level with target surfaces. Verify target surfaces are rendered in gold color.
- [ ] Verify unhit targets pulse or glow.
- [ ] Fire at a target surface. Verify the hit is registered (target dims and shows checkmark after shot).
- [ ] Fire at a non-target surface. Verify no target hit feedback.
- [ ] Target with pass-through: fire through it. Arrow continues; target shows as hit.
- [ ] Target with reflection: fire at it. Arrow bounces; target shows as hit.
- [ ] Target with block: fire at it. Arrow stops; target shows as hit.
- [ ] All prior interactions (plan, fire, undo, visibility) still work correctly.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX6 | All targets reachable: if all targets contacted, level is complete (no silent miss) | Unit test: every target surface hit by trace is in targets_hit | Yes |
| S19 | Trace preserves real state (targets_hit on copy, not real) | Unit test | Inherited |
| S16 | No NaN/Inf in output | Unit test | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes | GUT trace + state change tests |
| Planning | Image chains, mixed planning, state simulation | GUT planning tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Undo/reset, plan retained | Manual verification |
| UI | Arc rendering, all surface colors (blue/purple/cyan/orange/red/gray) | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT resolver tests |
| State | State changes during trace and planning | GUT state tests |

### Expected Visual State

Target surfaces rendered in gold with gold endpoint markers or outline. Unhit targets pulse/glow. Non-target surfaces retain their existing effect colors. After hitting a target (by firing), the target dims and displays a checkmark overlay. All other visual elements unchanged from Stage 54.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--54) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (UX6, S19, S16) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/surface.gd` | Modify | Add is_target flag |
| `scripts/math/tracer.gd` | Modify | Track targets_hit during trace |
| `scripts/visual/surface_renderer.gd` | Modify | Gold color for targets, pulsing animation, dimmed+checkmark for hit targets |
| `scripts/game/game_manager.gd` | Modify | Track targets_hit separately from GameState |
| `tests/test_stage55_targets.gd` | Create | Target surface and hit detection tests |

---

## Stage 56: Win Condition and Multi-Shot Puzzles

### Overview
Implement the win condition: a level is complete when ALL target surfaces have been hit across one or more shots. This stage adds cumulative `targets_hit` tracking across shots, post-fire state promotion (trace copy replaces real state), and `targets_hit` in `CheckpointData` for undo support. The breakable-wall multi-shot puzzle from §16.6 is reproduced as a full integration test.

### Prerequisites
Stage 55 (target surfaces, hit detection, targets_hit tracking during trace).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Level complete when ALL targets hit across one or more shots | §3.2, §24.3 |
| Behavior | Cumulative targets_hit: `existing.union(trace_result.targets_hit)` | §24.3 |
| Behavior | Post-fire state promotion: trace copy's game_state and targets_hit replace real ones | §16.7 (step 8) |
| Data | `targets_hit` in CheckpointData for undo | §3.3 |
| Behavior | targets_hit initialized empty on level load and full reset | §24.2 |
| Test | §16.6 breakable wall integration test (2-shot puzzle) | §16.6 |

### Unit Tests Added

1. **`test_stage56_win_condition_all_targets_hit`**: Level with 2 targets. Hit both in single trace. Expected: win condition met. Validates: §3.2.
2. **`test_stage56_win_condition_partial`**: Level with 3 targets. Hit 2 in a trace. Expected: win condition NOT met. Validates: §3.2 partial.
3. **`test_stage56_cumulative_targets_across_shots`**: Level with 2 targets. Shot 1 hits target A. Shot 2 hits target B. Expected: after shot 2, `targets_hit = {A, B}`, win condition met. Validates: §24.3 cumulative.
4. **`test_stage56_state_promotion_after_fire`**: Fire a shot that triggers StateChange("wall_intact", false). After shot completes and state is promoted: real game_state has `wall_intact = false`. Validates: §16.7 state promotion.
5. **`test_stage56_targets_hit_promotion`**: Fire a shot that hits target A. After promotion: persistent `targets_hit` contains A. Validates: targets_hit promotion.
6. **`test_stage56_UX5_undo_restores_targets_hit`**: Hit target A (shot 1). Hit target B (shot 2). Undo shot 2. Expected: `targets_hit = {A}` (B removed). Validates: UX5 checkpoint restores targets_hit.
7. **`test_stage56_targets_hit_on_full_reset`**: Hit some targets. Full reset. Expected: `targets_hit` is empty. Validates: reset clears targets.
8. **`test_stage56_targets_hit_on_level_load`**: Load a level. Expected: `targets_hit` is empty. Validates: initialization.
9. **`test_stage56_breakable_wall_multi_shot`**: Reproduce §16.6. Surface W: CategoricalResolver(state_key="wall_intact", {true: {left: Block+StateChange("wall_intact", false), right: Block}, false: {left: PassThrough, right: PassThrough}}). Target T behind wall. Initial flags: {"wall_intact": true}. Shot 1: fire at wall left side -> Block, state changes, arrow stops, target NOT hit. State promoted. Shot 2: fire toward target -> wall is now PassThrough -> arrow passes through -> hits target T -> level complete. Validates: §16.6 full multi-shot puzzle.
10. **`test_stage56_undo_after_wall_break`**: After shot 1 (wall broken), undo. Expected: wall_intact = true again, targets_hit = empty. Validates: UX5 with state changes.
11. **`test_stage56_duplicate_target_hit`**: Target already hit in prior shot. New shot hits same target again. Expected: no error, targets_hit unchanged (set semantics). Validates: idempotent target tracking.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Load §16.6 breakable wall level. Wall renders in red (Block). Target behind wall renders in gold with pulse.
- [ ] Fire at the wall. Arrow stops at wall. Wall visually changes (to gray or disappears) after state promotion. Target still pulsing.
- [ ] Fire again toward target. Arrow passes through broken wall. Hits target. Level complete indicator shown.
- [ ] Reset level. Wall reappears (Block). Target pulsing again. No targets hit.
- [ ] Fire at wall, then undo. Wall reappears, targets_hit empty.
- [ ] Multi-target level: hit targets across two shots. Verify level completes after all hit.
- [ ] All prior interactions (plan, visibility) still work.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX5 | Undo fully restores: targets_hit, game_state, plan, player position | Unit test: checkpoint restores targets_hit | Reinforced |
| UX6 | All targets reachable: contacted targets always registered | Unit test: §16.6 multi-shot test | Reinforced |
| S19 | Trace preserves real state (until promotion) | Unit test | Inherited |
| S7 | Per-entry state matches | Unit test | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes, target hit detection | GUT trace + target tests |
| Planning | Image chains, mixed planning, state simulation | GUT planning tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Undo restores targets_hit + game_state + plan + position | GUT Stage 56 tests + manual |
| UI | Surface colors including gold targets | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT resolver tests |
| State | State changes, promotion, isolation | GUT state tests |
| Targets | Hit detection for all effect types | GUT Stage 55 tests |

### Expected Visual State

Gold target surfaces with pulsing animation. After being hit (post-shot), targets dim and show checkmark. Breakable walls change color from red (Block) to gray (PassThrough) after being broken. Win condition triggers a level complete indicator (text overlay or similar). All other visual elements unchanged.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--55) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (UX5, UX6, S19, S7) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/game_manager.gd` | Modify | Win condition check, cumulative targets_hit, state promotion |
| `scripts/game/checkpoint.gd` | Modify | Include targets_hit in CheckpointData |
| `scripts/math/tracer.gd` | Modify | Return targets_hit in trace result |
| `resources/levels/test_breakable_wall.tres` | Create | §16.6 breakable wall test level |
| `tests/test_stage56_win_condition.gd` | Create | Win condition, multi-shot, and checkpoint tests |

---

## Stage 57: Game Loop and Level Loading

### Overview
Implement the full game loop and level loading system. `LevelData` resources are loaded from `.tres` files discovered by scanning `resources/levels/`. The shot lifecycle is formalized: fire -> checkpoint -> freeze -> trace -> plan -> merge -> animate -> promote -> win check -> unfreeze. This stage ties together all prior systems (trace, planning, state, targets, checkpoints, animation) into a cohesive shot-to-shot loop. When a surface the player was standing on breaks during a shot, the player falls when physics resume.

### Prerequisites
Stages 1--56 (all trace, planning, state, target, win condition, checkpoint systems).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Resource | `LevelData` -- name, surfaces, spawn_point, gravity, initial_flags, surface_names, bounds, cache_overrides, cached_carriers | §19.1 |
| Behavior | Level loading from .tres resources | §19.1 |
| Behavior | Level discovery: scan `resources/levels/` for .tres files | §19.1 |
| Behavior | Full shot lifecycle (10 steps from §16.7) | §16.7, §3.4 |
| Behavior | Game time freezes (`get_tree().paused = true`) during shot | §21.1 |
| Behavior | Arrow animator has `process_mode = PROCE§_MODE_ALWAYS` | §21.1 |
| Behavior | State promoted after animation completes | §16.7 |
| Behavior | Win check after state promotion | §16.7 |
| Behavior | Player falls when standing surface breaks during shot | §21.4 |
| Invariant | UX10: state changes visible during flight | §21.2 |
| Property | `LevelData.arrow_speed: float` -- default 1600, configurable per level | §21.2 |

### Unit Tests Added

1. **`test_stage57_level_data_construction`**: Create LevelData with name, surfaces, spawn_point, gravity=(0,0), initial_flags, surface_names, bounds. Expected: all fields stored correctly. Validates: §19.1.
2. **`test_stage57_level_data_defaults`**: Create LevelData with minimal fields. Expected: gravity defaults to (0,0), initial_flags defaults to empty, surface_names defaults to empty. Validates: §19.1 defaults.
3. **`test_stage57_level_loading_from_tres`**: Save a LevelData to .tres, then load it. Expected: all fields round-trip correctly. Validates: serialization.
4. **`test_stage57_level_discovery`**: Place multiple .tres files in resources/levels/. Call level discovery. Expected: returns list of all level file paths. Validates: §19.1 discovery.
5. **`test_stage57_shot_lifecycle_checkpoint_saved`**: Fire a shot. Expected: checkpoint saved before trace runs, containing pre-shot state. Validates: §16.7 step 1.
6. **`test_stage57_shot_lifecycle_state_promotion`**: Fire a shot with state change. Expected: after animation completes, real game_state updated. Validates: §16.7 step 8.
7. **`test_stage57_shot_lifecycle_win_check`**: Fire a shot that hits all targets. Expected: win condition detected after state promotion. Validates: §16.7 step 9.
8. **`test_stage57_shot_lifecycle_no_win`**: Fire a shot that misses targets. Expected: game continues in play phase. Validates: §16.7 no-win path.
9. **`test_stage57_game_time_freeze`**: During shot, verify game tree is paused. Arrow animator continues (process_mode ALWAYS). Validates: §21.1.
10. **`test_stage57_game_time_unfreeze`**: After shot completes, verify game tree is unpaused. Player can move again. Validates: §16.7 step 10.
11. **`test_stage57_level_bounds_auto`**: Load a level without explicit bounds. Expected: bounds computed from bounding box of all surfaces + 50 unit margin. Validates: §19.1 bounds default.
12. **`test_stage57_initial_flags_applied`**: Load a level with initial_flags {"wall_intact": true}. Expected: GameState.flags contains "wall_intact" = true. Validates: level initialization.
13. **`test_stage57_spawn_point`**: Load a level with spawn_point (100, 200). Expected: player spawns at (100, 200). Validates: §19.1 spawn.
14. **`test_stage57_per_level_arrow_speed`**: Load level with arrow_speed=400. Fire. Expected: arrow travels at 400 u/s. Validates: §21.2.
15. **`test_stage57_default_arrow_speed`**: Load level without explicit arrow_speed. Expected: arrow at 1600 u/s. Validates: §21.2 default.
16. **`test_stage57_level_bounds_arc_aabb`**: Level with arc surface (center=(200,200), r=100, 0 to pi/2 span). Auto-computed bounds include the arc's AABB (from center, radius, angular span), not just three defining points. Validates: §19.1.
17. **`test_stage57_state_change_visual_timing`**: Fire at breakable wall. Record frame when arrow reaches hit point (distance/speed). Record frame when surface visual changes. Expected: same frame (+/-1). Validates: §21.2 timing, UX10.
18. **`test_stage57_cache_cleared_between_shots`**: Fire shot 1 (populates TransformCache with entries). Fire shot 2. Expected: shot 2 starts with a fresh cache (no stale entries from shot 1). Verify by checking cache size is 0 (or contains only manual overrides) at the start of shot 2's trace. Validates: §17.5 "Persists for the duration of a shot. Cleared between shots."
19. **`test_stage57_collision_bodies_instantiated_on_load`**: Load a level with `collision_bodies: [{shape: "segment", start: (0,500), end: (1920,500)}]`. After load, verify a `StaticBody2D` with `SegmentShape2D` exists at the specified position in the scene tree. The player collides with it. Validates: collision bodies from LevelData are instantiated during gameplay level load, not just in the editor.
20. **`test_stage57_visibility_updates_after_state_promotion`**: Fire a shot that breaks a wall (state change → wall becomes pass-through). On the next frame after state promotion, compute visibility. Expected: the shadow behind the broken wall is gone — the visibility polygon now extends through where the wall was. Validates: §15.6 + Principle 14 (visibility shares the world, including post-shot state changes).
21. **`test_stage57_player_falls_when_surface_breaks`**: Create player standing on a breakable surface (`player_solid=true`, CategoricalResolver changes to null effect on state change). Fire a shot that triggers the state change. After state promotion and game unfreeze, verify player's y-position changes (falls) within 2 frames. Validates: §21.1 "When surface player was standing on breaks, player falls when physics resume."
22. **`test_stage57_manual_override_persists_across_shots`**: Load a level with a CycleOverride. Fire shot 1. Fire shot 2. Verify the override entry is still in the cache at the start of shot 2's trace. Validates: §17.5 "Manual overrides persist across shots."
23. **`test_stage57_golden_path_integration`**: Full game flow: load level -> construct plan (add 1 surface) -> fire -> hit target -> level complete -> return to level select -> verify completion saved -> load a different level -> verify it loads clean with no state leakage. Validates: end-to-end game loop integration.
24. **`test_stage57_load_rejects_null_interactive`**: Load a `.tres` file where a surface has null effect (pass-through) AND `interactive=true` on the same side. Expected: load-time enforcement catches this — either rejects the level or auto-corrects `interactive` to `false` with a warning. Validates: §9.1 "enforced at load time."
25. **`test_stage57_multi_shot_visibility_cumulative`**: Fire shot 1 (breaks wall A — shadow behind A disappears). Fire shot 2 (breaks wall B — shadow behind B also disappears). After both shots, verify visibility polygon reflects BOTH walls broken (cumulative state changes across shots). Validates: §15.6 visibility shares the world across multiple shots.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Load a level from a .tres file. Player appears at spawn point. Surfaces rendered correctly.
- [ ] Fire a shot. Observe: game freezes (player cannot move during flight), arrow animates, then game unfreezes.
- [ ] Fire a shot with a state-changing surface. Observe: surface visually changes at the moment the arrow reaches the hit point during animation.
- [ ] Fire a shot that hits all targets. Observe: level complete feedback.
- [ ] Fire a shot that misses targets. Observe: game continues, player can fire again.
- [ ] Undo a shot. Verify full restore (player position, state, plan, targets).
- [ ] Full reset. Verify return to initial state.
- [ ] Skip animation during flight (press any non-movement key). Verify instant completion.
- [ ] Stand on a breakable surface. Fire at it (breaking it). After shot: player falls.
- [ ] Verify WASD input is ignored during flight (no queued movement).
- [ ] Fire at a breakable wall. After the shot, observe the visibility region — the shadow behind the wall disappears immediately.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX10 | State changes visible during flight: surface updates at hit moment | Manual: observe surface color change during animation at correct time | Yes |
| UX3 | Physical preview matches arrow flight | Manual: compare preview with flight path | Inherited |
| UX4 | Same shot twice = same result | Unit test: fire same setup twice, compare traces | Inherited |
| UX5 | Undo fully restores | Unit test + manual | Inherited |
| UX6 | All targets reachable | Unit test | Inherited |
| S19 | Trace preserves real state (until promotion) | Unit test | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes, target detection | GUT trace tests |
| Planning | Image chains, mixed planning, state simulation | GUT planning tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Full checkpoint/undo/reset cycle | GUT + manual verification |
| Targets | Hit detection, cumulative tracking, win condition | GUT target + win tests |
| UI | Surface colors, target pulsing/dimming, arc rendering | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT resolver tests |
| State | State changes, promotion, simulation during planning | GUT state tests |

### Expected Visual State

Full game loop visible. Level loads from .tres: surfaces rendered, player at spawn, cursor tracking mouse. During shot: game freezes, arrow flies step-by-step, state-changing surfaces update visually at hit moment. After shot: game unfreezes, targets update (dim + checkmark if hit). Level complete feedback when all targets hit. Brief level name overlay on entry (1--2 seconds, fading).

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--56) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (UX10, UX3, UX4, UX5, UX6, S19) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/level_data.gd` | Create | LevelData resource class with all fields |
| `scripts/game/level_loader.gd` | Create | Level loading from .tres, level discovery |
| `scripts/game/game_manager.gd` | Modify | Full shot lifecycle (10 steps), game time freeze/unfreeze |
| `scripts/game/shot_controller.gd` | Create | Shot lifecycle orchestration (freeze, trace, animate, promote, check) |
| `scenes/main.tscn` | Modify | Wire game manager, shot controller, level loader |
| `resources/levels/test_simple.tres` | Create | Simple test level for integration testing |
| `tests/test_stage57_game_loop.gd` | Create | Game loop and level loading tests |

---

## Stage 58: Menus (Main, Level Select, Pause)

### Overview
Implement the three menu screens: main menu (Play, Level Editor, Settings, Quit), level select (grid/list of levels with completion status), and pause menu (Resume, Reset, Quit to Level Select). Transitions between menus and gameplay are immediate -- no transition animations. A brief level name overlay fades in/out on level entry.

### Prerequisites
Stage 57 (game loop, level loading, level discovery).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Scene | `scenes/ui/main_menu.tscn` -- Main menu with Play, Level Editor, Settings, Quit buttons | §26.2 |
| Scene | `scenes/ui/level_select.tscn` -- Level grid/list with completion status | §26.2 |
| Scene | `scenes/ui/pause_menu.tscn` -- Pause overlay with Resume, Reset, Quit to Level Select | §26.2 |
| Scene | `scenes/ui/settings.tscn` — settings screen with audio volume, display (fullscreen toggle), controls display | §26.2 |
| Behavior | Main menu -> Level Select -> Level Loaded flow | §3.4 |
| Behavior | Pause menu opens on Escape | §4.1 |
| Behavior | Resume returns to gameplay | §26.2 |
| Behavior | Reset from pause triggers full level reset | §26.2 |
| Behavior | Quit to Level Select returns to level selection | §26.2 |
| Behavior | Level name overlay on entry (1--2 seconds, fading) | §3.4 |
| Behavior | Level select shows completion checkmark and shot count | §26.2 |
| Visual | No transition animation -- immediate scene changes | §3.4 |
| Behavior | Level ordering: alphabetical by filename unless `resources/levels/manifest.cfg` specifies custom order | §19.1 |

### Unit Tests Added

1. **`test_stage58_main_menu_buttons_exist`**: Load main menu scene. Expected: Play, Level Editor, Settings, Quit buttons all present. Validates: §26.2 menu structure.
2. **`test_stage58_level_select_lists_levels`**: Level select scene populated from level discovery. Expected: all discovered levels appear with names. Validates: §26.2 level listing.
3. **`test_stage58_level_select_completion_status`**: Mark a level as completed in save data. Load level select. Expected: that level shows checkmark and shot count. Validates: §26.2 completion display.
4. **`test_stage58_level_select_incomplete_status`**: Level not completed. Load level select. Expected: no checkmark, no shot count shown. Validates: default incomplete state.
5. **`test_stage58_pause_menu_opens`**: During gameplay, simulate Escape press. Expected: pause menu appears. Validates: §4.1 pause input.
6. **`test_stage58_pause_resume`**: Open pause menu, press Resume. Expected: pause menu closes, gameplay resumes. Validates: §26.2 resume.
7. **`test_stage58_pause_reset`**: Open pause menu, press Reset. Expected: level resets to initial state (full reset). Validates: §26.2 reset.
8. **`test_stage58_pause_quit_to_select`**: Open pause menu, press Quit to Level Select. Expected: returns to level select screen. Validates: §26.2 quit.
9. **`test_stage58_level_name_overlay`**: Load a level. Expected: level name text appears and fades over 1--2 seconds. Validates: §3.4 overlay.
10. **`test_stage58_level_order_alphabetical`**: No manifest.cfg exists. Expected: levels in level select ordered alphabetically by filename. Validates: §19.1 default.
11. **`test_stage58_level_order_manifest`**: Create `resources/levels/manifest.cfg` with custom order [level_c, level_a, level_b]. Expected: level select shows that order. Validates: §19.1.
12. **`test_stage58_settings_screen_opens`**: Click "Settings" in main menu. Expected: settings screen appears with audio/display/controls sections.
13. **`test_stage58_settings_changes_persist`**: Change audio volume in settings UI. Return to main menu. Reopen settings. Expected: changed value persists.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Launch game. Main menu appears with Play, Level Editor, Settings, Quit.
- [ ] Click Play. Level select screen appears with available levels.
- [ ] Levels with completion data show checkmark + shot count. Incomplete levels show no checkmark.
- [ ] Select a level. Level loads immediately (no transition animation). Level name overlay fades in and out.
- [ ] Press Escape during gameplay. Pause menu appears with Resume, Reset, Quit to Level Select.
- [ ] Click Resume. Gameplay resumes from where it was paused.
- [ ] Open pause, click Reset. Level resets to initial state.
- [ ] Open pause, click Quit to Level Select. Returns to level select.
- [ ] From level select, verify back navigation to main menu works.
- [ ] Click Quit on main menu. Game closes.
- [ ] Verify gameplay is fully paused while pause menu is open (no player movement, no preview updates).
- [ ] Open Settings from main menu. Adjust audio volume slider. Toggle fullscreen. Return to menu. Reopen Settings — changes are preserved.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX5 | Undo fully restores (pause Reset = full reset) | Manual: reset from pause, verify initial state | Inherited |
| S19 | Trace preserves real state | Not affected by menus | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes, target detection | GUT trace tests |
| Planning | Image chains, mixed planning, state simulation | GUT planning tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Full checkpoint/undo/reset cycle | GUT + manual |
| Targets | Hit detection, cumulative tracking, win condition | GUT target + win tests |
| Game Loop | Shot lifecycle, level loading, state promotion | GUT Stage 57 tests |
| UI | Surface colors, target pulsing/dimming, arc rendering | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT resolver tests |
| State | State changes, promotion, simulation during planning | GUT state tests |

### Expected Visual State

Main menu: centered buttons on a dark background (Play, Level Editor, Settings, Quit). Level select: grid or list of level entries, each showing level name. Completed levels have a checkmark and shot count. Pause menu: semi-transparent overlay with Resume, Reset, Quit to Level Select buttons. Level name overlay: text appears at top/center on level load, fades out over 1--2 seconds.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--57) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (UX5, S19) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scenes/ui/main_menu.tscn` | Create | Main menu scene with buttons |
| `scenes/ui/level_select.tscn` | Create | Level select scene with grid/list |
| `scenes/ui/pause_menu.tscn` | Create | Pause menu overlay |
| `scenes/ui/settings.tscn` | Create | Settings screen scene |
| `scripts/game/main_menu.gd` | Create | Main menu logic (button handlers, navigation) |
| `scripts/game/level_select.gd` | Create | Level select logic (population, completion display, selection) |
| `scripts/game/pause_menu.gd` | Create | Pause menu logic (resume, reset, quit) |
| `scripts/visual/level_name_overlay.gd` | Create | Fading level name overlay |
| `scenes/main.tscn` | Modify | Wire menu navigation and pause handling |
| `tests/test_stage58_menus.gd` | Create | Menu unit tests |

---

## Stage 59: HUD

### Overview
Implement the in-game heads-up display showing real-time gameplay information: the plan list (ordered planned surfaces with their current resolved effects), shot counter, level name, target progress indicator, and reset hint. The plan list shows surface names from `LevelData.surface_names` and marks entries whose effects have changed due to state changes.

### Prerequisites
Stage 58 (menus, level loading with surface_names in LevelData).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Scene | `scenes/ui/hud.tscn` -- HUD overlay for gameplay | §26.1 |
| Element | Plan list: ordered planned surfaces by name with current effect | §26.1 |
| Element | Shot counter: number of shots fired this attempt | §26.1 |
| Element | Level name display | §26.1 |
| Element | Target progress: "Targets: X/Y hit" | §26.1 |
| Element | Reset hint: "R to reset" | §26.1 |
| Behavior | Plan list uses surface_names from LevelData (default "Surface N") | §19.1, §26.1 |
| Behavior | Plan list shows CURRENT effect (resolved via ConfigResolver) | §26.1 |
| Behavior | Changed effects visually marked (e.g., strikethrough, warning icon) | §26.1 |

### Unit Tests Added

1. **`test_stage59_hud_plan_list_empty`**: No plan entries. Expected: plan list is empty or shows "No plan" text. Validates: empty plan display.
2. **`test_stage59_hud_plan_list_populated`**: Plan with 3 entries. Surface names: "Mirror A", "Portal B", "Lens C". Expected: plan list shows "1. Mirror A (Reflection)", "2. Portal B (Rigid Motion)", "3. Lens C (Normal Projection)" or similar. Validates: §26.1 plan list.
3. **`test_stage59_hud_plan_list_default_names`**: Surface without a name in surface_names. Expected: displays "Surface N" (where N is the surface ID). Validates: §19.1 default name.
4. **`test_stage59_hud_plan_list_changed_effect`**: Plan entry whose effect changed due to state change (CategoricalResolver resolved differently). Expected: entry visually marked as changed. Validates: §26.1 changed effect marking.
5. **`test_stage59_hud_shot_counter`**: Fire 3 shots. Expected: shot counter shows "3". Validates: §26.1 shot counter.
6. **`test_stage59_hud_shot_counter_reset`**: Fire shots, then full reset. Expected: shot counter shows "0". Validates: counter reset.
7. **`test_stage59_hud_level_name`**: Load level "Puzzle Alpha". Expected: HUD shows "Puzzle Alpha". Validates: §26.1 level name.
8. **`test_stage59_hud_target_progress`**: Level with 3 targets, 1 hit. Expected: shows "Targets: 1/3 hit". Validates: §26.1 target progress.
9. **`test_stage59_hud_target_progress_all_hit`**: All targets hit. Expected: shows "Targets: 3/3 hit". Validates: complete progress.
10. **`test_stage59_hud_reset_hint`**: HUD visible. Expected: "R to reset" text present. Validates: §26.1 reset hint.
11. **`test_stage59_hud_plan_updates_on_add`**: Add a surface to the plan. Expected: plan list updates immediately with new entry. Validates: real-time plan display.
12. **`test_stage59_hud_plan_updates_on_remove`**: Remove a surface from the plan. Expected: plan list updates immediately. Validates: real-time removal.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Load a level. HUD appears with level name, shot counter (0), target progress (0/N), reset hint.
- [ ] Add surfaces to plan by clicking. Plan list updates in real time, showing surface names and effects.
- [ ] Remove a surface from the plan (right-click). Plan list updates.
- [ ] Clear the plan (C). Plan list empties.
- [ ] Fire a shot. Shot counter increments to 1.
- [ ] Hit a target. Target progress updates (e.g., "Targets: 1/2 hit").
- [ ] Fire again. Shot counter shows 2.
- [ ] Full reset (R). Shot counter resets to 0, target progress resets to 0/N.
- [ ] Level with state-changing surface: plan the state-changing surface + a CategoricalResolver surface. Fire shot 1 (changes state). Observe plan list updates entry effect labels to reflect new resolved effects.
- [ ] Verify HUD does not obstruct gameplay (positioned at edges/corners).
- [ ] Verify HUD is hidden during arrow flight animation.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX5 | Undo fully restores: shot counter and target progress revert on undo | Manual + unit test | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes, target detection | GUT trace tests |
| Planning | Image chains, mixed planning, state simulation | GUT planning tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Full checkpoint/undo/reset cycle | GUT + manual |
| Targets | Hit detection, cumulative tracking, win condition | GUT target + win tests |
| Game Loop | Shot lifecycle, level loading, state promotion | GUT Stage 57 tests |
| Menus | Main menu, level select, pause menu | GUT Stage 58 tests + manual |
| UI | Surface colors, target visuals, arc rendering | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT resolver tests |
| State | State changes, promotion, simulation during planning | GUT state tests |

### Expected Visual State

HUD overlay at screen edges. Top-left: level name. Top-right: shot counter ("Shots: 0") and target progress ("Targets: 0/3 hit"). Left side: plan list showing numbered entries with surface names and effect labels. Bottom: "R to reset" hint text. Plan list entries with changed effects (due to state changes) are marked with a visual indicator (strikethrough, warning icon, or color change). HUD is semi-transparent and does not obscruct the gameplay area. All prior visual elements unchanged.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--58) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (UX5) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scenes/ui/hud.tscn` | Create | HUD scene with all elements |
| `scripts/game/hud.gd` | Create | HUD logic (plan list, counters, target progress, reset hint) |
| `scenes/main.tscn` | Modify | Add HUD instance, wire signals for plan/shot/target updates |
| `tests/test_stage59_hud.gd` | Create | HUD unit tests |

---

## Stage 60: Save System

### Overview
Implement the save/load system using Godot's `ConfigFile` format (.cfg). The save system persists level completion data (per level: completed yes/no, best shot count), user settings (audio, controls, display), and the last played level for "continue" functionality. Data is loaded on startup and saved on level completion and settings changes.

### Prerequisites
Stages 58--59 (menus with completion display, HUD with shot counter, level loading).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/game/save_manager.gd` -- Save/load using ConfigFile | §27.1 |
| Data | Level completion: per level completed flag + best shot count | §27.1 |
| Data | Settings: audio volume, controls, display preferences | §27.1 |
| Data | Last played level identifier | §27.1 |
| Behavior | Save file uses Godot ConfigFile format (.cfg) -- human-readable | §27.1 |
| Behavior | Load on startup | §27.2 |
| Behavior | Save on level completion | §27.1 |
| Behavior | Save on settings change | §27.1 |
| File | `user://save_data.cfg` -- player progress | §27.1 |
| File | `user://settings.cfg` -- user preferences | §27.1 |

### Unit Tests Added

1. **`test_stage60_save_level_completion`**: Complete a level with 3 shots. Save. Expected: save file contains level ID with completed=true, best_shots=3. Validates: §27.1 level completion save.
2. **`test_stage60_load_level_completion`**: Write save data with level completed. Load. Expected: level_select shows completed status. Validates: load on startup.
3. **`test_stage60_best_shot_count_updates`**: Complete a level with 5 shots (saved). Complete same level with 3 shots. Expected: best_shots updated to 3. Validates: best shot tracking.
4. **`test_stage60_best_shot_count_no_downgrade`**: Complete a level with 3 shots (saved). Complete same level with 5 shots. Expected: best_shots remains 3 (better score preserved). Validates: best-only tracking.
5. **`test_stage60_save_settings`**: Change audio volume to 0.7. Save. Expected: settings file contains audio_volume=0.7. Validates: §27.1 settings save.
6. **`test_stage60_load_settings`**: Write settings with audio_volume=0.5. Load. Expected: audio volume is 0.5. Validates: settings load.
7. **`test_stage60_save_last_played_level`**: Play "level_03". Exit to menu. Expected: save data contains last_played="level_03". Validates: §27.1 last played.
8. **`test_stage60_load_last_played_level`**: Save data has last_played="level_03". Load game. Expected: "continue" option points to level_03. Validates: continue functionality.
9. **`test_stage60_no_save_file_defaults`**: Delete save file. Load. Expected: no crash, all levels show incomplete, settings at defaults. Validates: graceful missing-file handling.
10. **`test_stage60_save_file_format`**: Save data, read raw file. Expected: human-readable key-value format (.cfg). Validates: §27.1 ConfigFile format.
11. **`test_stage60_save_on_level_complete`**: Complete a level. Without explicit save call, verify save file was updated. Validates: auto-save on completion.
12. **`test_stage60_multiple_levels_saved`**: Complete levels A, B, C with different shot counts. Load. Expected: all three levels show correct completion status and shot counts. Validates: multi-level persistence.
13. **`test_stage60_corrupt_save_recovery`**: Create a `.cfg` save file with malformed content (truncated mid-write). Load it. Expected: graceful failure -- defaults used, no crash. Warning logged. Validates: save system handles corrupt files.

### Interactive User Tests

- [ ] Complete a level. Quit to level select. Verify level shows checkmark and shot count.
- [ ] Quit the game entirely. Relaunch. Verify level select still shows completion data.
- [ ] Complete a level with fewer shots than previous best. Verify shot count updates.
- [ ] Complete a level with more shots than previous best. Verify shot count does NOT update (best preserved).
- [ ] Change a setting (e.g., audio volume). Quit and relaunch. Verify setting persisted.
- [ ] Delete save file. Launch game. Verify no crash, all levels show incomplete, settings at defaults.
- [ ] Play a level, quit to menu. Verify "last played" level is remembered.
- [ ] Verify save file at `user://save_data.cfg` is human-readable (open in text editor).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX5 | Undo fully restores (save system does not interfere with undo) | Manual: undo after completion, verify no save corruption | Inherited |
| S19 | Trace preserves real state (save system does not affect trace) | Not affected by save system | Inherited |

### Regression Checklist

| System | Behavior | How to Verify |
|--------|----------|---------------|
| Math | All math primitives and transforms | GUT math tests |
| Effects | All 8 effect types | GUT effect tests |
| Trace | Physical trace with state changes, target detection | GUT trace tests |
| Planning | Image chains, mixed planning, state simulation | GUT planning tests |
| Visibility | All visibility computations | GUT visibility tests |
| Flight | Arrow shooting, animation, skip, camera tracking | Manual verification |
| Checkpoints | Full checkpoint/undo/reset cycle | GUT + manual |
| Targets | Hit detection, cumulative tracking, win condition | GUT target + win tests |
| Game Loop | Shot lifecycle, level loading, state promotion | GUT Stage 57 tests |
| Menus | Main menu, level select (now with save data), pause menu | GUT Stage 58 tests + manual |
| HUD | Plan list, shot counter, target progress, reset hint | GUT Stage 59 tests + manual |
| UI | Surface colors, target visuals, arc rendering | Visual inspection |
| Resolver | FixedResolver, CategoricalResolver | GUT resolver tests |
| State | State changes, promotion, simulation during planning | GUT state tests |

### Expected Visual State

No new in-game visual elements. Level select screen now shows persistent completion data: checkmark icons on completed levels, best shot counts displayed. Settings screen reflects loaded user preferences. All gameplay visuals unchanged from Stage 59.

### Feedback Loop Protocol

Reference standard protocol at top of document.

### Validation Summary

| # | Check | Status |
|---|-------|--------|
| 1 | All new unit tests pass | [ ] |
| 2 | All prior unit tests pass (Stages 1--59) | [ ] |
| 3 | Interactive test items verified by user | [ ] |
| 4 | Invariants verified (UX5, S19) | [ ] |
| 5 | User sign-off received | [ ] |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/save_manager.gd` | Create | Save/load system using ConfigFile |
| `scripts/game/level_select.gd` | Modify | Read completion data from save manager |
| `scripts/game/game_manager.gd` | Modify | Trigger save on level completion |
| `scripts/game/settings_manager.gd` | Create | Settings persistence (audio, controls, display) |
| `scenes/ui/main_menu.gd` | Modify | Wire "continue" to last played level from save |
| `tests/test_stage60_save_system.gd` | Create | Save/load unit tests |

---

## Appendix A: Invariant Introduction Map (All 29 Invariants, Status After Stage 60)

| Invariant | Full ID | Introduced | First Testable | Fully Testable | Status After Stage 60 |
|-----------|---------|-----------|----------------|----------------|----------------------|
| Carrier <-> via round-trip | S1 | Stage 8 | Stage 8 | Stage 65 | Tested (line + circle) |
| Transform round-trip | S2 | Stage 20 | Stage 20 | Stage 65 | Tested |
| Determinism | S3 | Stage 14 | Stage 14 | Stage 65 | Tested |
| Divergence monotonic | S4 | Stage 26 | Stage 26 | Stage 65 | Tested |
| Aligned provenance | S5 | Stage 25 | Stage 25 | Stage 65 | Tested |
| Aligned match | S6 | Stage 25 | Stage 25 | Stage 65 | Tested |
| Per-entry state | S7 | Stage 54 | Stage 54 | Stage 65 | Tested |
| Forward-first ordering | S8 | Stage 11 | Stage 11 | Stage 65 | Tested |
| Exclusion respected | S9 | Stage 16 | Stage 16 | Stage 65 | Tested |
| Projective resets frame | S10 | Stage 73 | Stage 73 | Stage 65 | Tested |
| Three points on carrier | S11 | Stage 7 | Stage 7 | Stage 65 | Tested |
| Side determination | S12 | Stage 7 | Stage 7 | Stage 65 | Tested |
| Visibility no self-intersect | S13 | Stage 35 | Stage 35 | Stage 65 | Tested |
| Visibility edges on geometry | S14 | Stage 35 | Stage 35 | Stage 65 | Tested |
| Visibility non-overlapping | S15 | Stage 35 | Stage 35 | Stage 65 | Tested |
| No NaN/Inf in output | S16 | Stage 4 | Stage 4 | Stage 65 | Tested |
| Provenance IDs unique | S17 | Stage 8 | Stage 8 | Stage 65 | Tested |
| Frame determinant non-zero | S18 | Stage 20 | Stage 20 | Stage 65 | Tested |
| Trace preserves real state | S19 | Stage 53 | Stage 53 | Stage 65 | Tested |
| Visibility predicts non-div. | UX1 | Stage 37 | Stage 37 | Stage 65 | Tested |
| Divergence -> outside vis. | UX2 | Stage 37 | Stage 37 | Stage 65 | Tested |
| Preview matches flight | UX3 | Stage 17 | Stage 17 | Stage 65 | Tested |
| Same shot = same result | UX4 | Stage 17 | Stage 17 | Stage 65 | Tested |
| Undo fully restores | UX5 | Stage 32 | Stage 32 | Stage 65 | Tested (targets_hit added Stage 56) |
| All targets reachable | UX6 | Stage 55 | Stage 55 | Stage 65 | Tested |
| Solid path to cursor | UX7 | Stage 5 | Stage 5 | Stage 65 | Tested |
| Block stops arrow | UX9 | Stage 13 | Stage 13 | Stage 65 | Tested |
| State changes visible | UX10 | Stage 57 | Stage 57 | Stage 65 | Tested |
| Empty plan = fire straight | UX11 | Stage 5 | Stage 15 | Stage 65 | Tested |

> **Stage 110 note:** the visibility rows here (S13–S15, UX1, UX2) describe the legacy design, which was never built. Stage 110 built the region on the beam solver; they are tested as VIS-SIMPLE, VIS-EDGE-ON-GEOMETRY, VIS-DISJOINT and VISIBILITY-EQUALS-NON-DIVERGENCE (docs/TDD_14_VISIBILITY.md).

---

## Appendix B: Cumulative Test Count After Stage 60

| Stage | New Unit Tests | New Interactive Items | Running Unit Total | Running Interactive Total |
|-------|---------------|----------------------|-------------------|-------------------------|
| 1--51 | ~210 | ~120 | ~210 | ~120 |
| 52 | 8 | 4 | ~218 | ~124 |
| 53 | 10 | 4 | ~228 | ~128 |
| 54 | 14 | 4 | ~242 | ~132 |
| 55 | 9 | 8 | ~251 | ~140 |
| 56 | 11 | 7 | ~262 | ~147 |
| 57 | 23 | 10 | ~285 | ~157 |
| 58 | 11 | 11 | ~296 | ~168 |
| 59 | 12 | 11 | ~308 | ~179 |
| 60 | 13 | 8 | ~321 | ~187 |

| Category | Count After Stage 60 |
|----------|---------------------|
| Unit tests | ~321 |
| Interactive test items | ~187 |
| Invariants actively tested | 21 (S1--S19, UX1--UX7, UX9--UX11 except those not yet introduced) |
| Invariants introduced this document | 4 (S7, S19, UX6, UX10) |
| Invariants reinforced this document | 1 (UX5) |
