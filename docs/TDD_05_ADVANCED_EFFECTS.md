# TDD Document 5: Advanced Effects

**Stages 40--51, 70--78** | Circle inversion (arcs), bug fixes & architectural improvements (44--51), planned: rigid motion (portals), projective effects, mixed planning, compound effects (70--78)

### Stage Status

| Stage | Topic | Status |
|-------|-------|--------|
| 40 | Circle Inversion Effect | Done |
| 41 | Arc Segment Rendering | Done |
| 42 | Circle Inversion in Physical Trace | Done |
| 42.5 | Arc Collision Shapes for Player Physics | Done |
| 43 | Circle Inversion in Planner | Done |
| 44 | Reflection formula fix | Done |
| 45 | Arc via pole / circle rehit / grazing fixes | Done |
| 46 | Epsilon removal | Done |
| 47 | Escape via fix | Done |
| 48 | Full segments | Done |
| 49 | Mid-air termination | Done |
| 50 | Spurious reflection fix | Done |
| 51 | Conjugation side fix | Done |
| 70 | Visibility with Circles | Planned |
| 71 | Rigid Motion Effect (Portals) | Done |
| 72 | Rigid Motion in Trace/Planner/Visibility | Planned |
| 73 | Line Normal Projection Effect | Planned |
| 74 | Circle Normal Projection Effect | Planned |
| 75 | Semi-Circle Directional Projection Effect | Planned |
| 76 | Parallel-Source Visibility Mode | Planned |
| 77 | Mixed Planning Chain | Planned |
| 78 | Compound Transformative Effect | Planned |

**Visibility deferral:** Stages 70 (Visibility with Circles) and 76 (Parallel-Source Visibility) are deferred until after all effect stages (71–78) are complete, because the visibility system does not exist yet. Visibility-related tests within stages 72–78 are also deferred accordingly.

**Effect hierarchy note:** The effect hierarchy uses an `Effect` base class with an `Effect.Kind` enum (`TERMINAL`, `TRANSFORMATIVE`) for dispatch. `TransformativeEffect` is a base class providing `get_mobius()`, `get_inverse_mobius()`, and `normalized()` methods. All effects extend `Effect`. Dispatch uses `kind` enum comparison, not type checks.

**Regression Test Policy:** After implementing Stage N, run ALL tests from Stages 1 through N. The full test suite must pass before proceeding to Stage N+1. No exceptions.

**Fractional stage ordering:** Stages 42.5 and 49.5 were inserted after the initial plan. They are ordered between their integer neighbors: Stage 42 → Stage 42.5 → Stage 43 and Stage 49 → Stage 49.5 → Stage 50.

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

**Note:** Stage 39 does not exist — the numbering skips from 38 (TDD_04) to 40 (this document). This is a residual gap from renumbering during plan revisions. All references to Stage 39 have been corrected to Stage 38.

### Stage Reconciliation: Stages 44–51

During implementation, stages 44–51 were used for bug fixes, edge cases, and architectural improvements that arose organically, rather than the originally planned features (RigidMotion, Projective effects, etc.). The original planned content has been renumbered to stages 70–78 (see "Planned Future Stages" at the end of this document).

| Stage # | Original TDD Topic | Actual Implementation | Test File |
|---------|-------------------|----------------------|-----------|
| 44 | Visibility with circles | Reflection formula fix | `test_stage44_reflection_formula.gd` |
| 45 | RigidMotionEffect | Arc via pole / circle rehit / grazing | `test_stage45_*.gd` |
| 46 | Rigid motion integration | Epsilon removal | `test_stage46_epsilon_removal.gd` |
| 47 | LineNormalProjection | Escape via fix | `test_stage47_escape_via.gd` |
| 48 | CircleNormalProjection | Full segments | `test_stage48_full_segment.gd` |
| 49 | SemicircleDirectional | Mid-air termination | `test_stage49_mid_air_termination.gd` |
| 50 | Mixed planning chain | Spurious reflection fix | `test_stage50_spurious_reflection.gd` |
| 51 | Compound effect | Conjugation side fix | `test_stage51_conjugation_side.gd` |

The original stage content for these features has been moved to stages 70–78 below.

---

## Stage 40: Circle Inversion Effect

### Overview
Implement `CircleInversionEffect`, the first anti-conformal Mobius transformation that operates on circles rather than lines. The carrier must be a circle (`a != 0`), and the effect is self-inverse: applying the inversion twice returns to the original position (scaled by r squared). This stage validates the effect in isolation using the worked example from §16.2.

### Prerequisites
Stages 1--38 (full math layer, intersection, surfaces, FixedResolver, TerminalEffect, ReflectionEffect, physical/planned trace, step tree, visibility, camera tracking).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/effects/circle_inversion.gd` -- CircleInversionEffect class | §10.2, §10.8 |
| Method | `CircleInversionEffect.get_mobius() -> MobiusTransform` | §10.2 |
| Method | `CircleInversionEffect.get_inverse_mobius() -> MobiusTransform` | §10.2 |
| Validation | Carrier must be a circle (`a != 0`), enforced at construction | S23 |
| Formula | alpha=center, beta=r^2-|center|^2, gamma=1, delta=-conj(center), conjugating=true | §10.8 |
| Property | Self-inverse: M^2 = r^2 * I | §10.8 |

### Unit Tests Added

1. **`test_stage40_inversion_construction_valid`**: Construct CircleInversionEffect with a circle carrier (center=(200,200), r=100). Expected: construction succeeds, `get_mobius()` returns a MobiusTransform with `conjugating == true`. Validates: §10.8 anti-conformal flag.
2. **`test_stage40_inversion_construction_rejects_line`**: Attempt to construct CircleInversionEffect with a line carrier (`a == 0`). Expected: assertion or error. Validates: S23 carrier constraint.
3. **`test_stage40_inversion_mobius_coefficients`**: For carrier center=(200,200), r=100: verify alpha=(200,-200) as complex, beta=r^2-|center|^2=10000-80000=-70000, gamma=1, delta=-conj(center)=(-200,-200) as complex. Validates: §10.8 formula.
4. **`test_stage40_inversion_apply_point`**: Apply inversion (center=(200,200), r=100) to z=(400,200). Expected: w=(250,200). This is the §16.2 example. Validates: §10.8 formula correctness.
5. **`test_stage40_inversion_self_inverse`**: Apply inversion to z=(400,200), get w=(250,200). Apply inversion again to w. Expected: result = (400,200) (original point). Validates: self-inverse property.
6. **`test_stage40_inversion_point_on_circle_fixed`**: Apply inversion (center=(200,200), r=100) to a point on the circle, e.g., z=(300,200). Expected: w=(300,200) (fixed point). Validates: points on the inversion circle are fixed.
7. **`test_stage40_S2_transform_round_trip`**: For point P, apply inversion via cache, then apply inverse. Result has same Point ID as original P. Validates: S2.
8. **`test_stage40_S18_determinant_nonzero`**: Verify `|alpha*delta - beta*gamma|^2 > 0` for the constructed Mobius matrix. Validates: S18.
9. **`test_stage40_inversion_center_maps_to_infinity`**: Apply inversion (center=(200,200), r=100) to z=(200,200). Expected: result is infinity (division by zero in the Mobius formula). Validates: §31.6 degenerate case.
10. **`test_stage40_S16_no_nan_inf`**: Apply inversion to multiple finite test points away from center. All results are finite (no NaN/Inf). Validates: S16.

### Interactive User Tests

- [ ] No visual change expected at this stage. Press Play and verify the game runs identically to Stage 38.
- [ ] Run GUT. All prior tests plus new Stage 40 tests pass.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S2 | Transform round-trip: inversion inverse returns same Point ID | Unit test | Reinforced (circle inversion) |
| S18 | Frame determinant non-zero for inversion Mobius | Unit test | Reinforced (circle inversion) |
| S16 | No NaN/Inf in output for finite points away from center | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--8 | Direction, Ray, GeneralizedCircle, Segment, TransformCache | GUT tests |
| Math layer | Stage 9 | MobiusTransform apply, compose, invert | GUT tests |
| Math layer | Stage 10 | Intersection system | GUT tests |
| Visual | Stage 5 | Green line from player to cursor | Move mouse |
| Visual | Stages 34--38 | Visibility polygon renders correctly | Visual inspection |
| Interaction | Stage 3 | Cursor follows mouse | Move mouse |
| Interaction | Stage 2 | Player moves with WASD | Press WASD |
| Trace | Stages 11--14 | Physical trace with reflection/block | GUT tests |
| Planning | Stages 21--27 | Plan construction, image chain | GUT tests |
| Visibility | Stages 34--38 | Multi-step visibility computation | GUT tests |

### Expected Visual State

Identical to Stage 38. No visual changes -- this stage introduces the effect class in isolation without wiring it to surfaces or trace.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/effects/circle_inversion.gd` | Create | CircleInversionEffect class implementing TransformativeEffect |
| `tests/test_stage40_circle_inversion.gd` | Create | Circle inversion unit tests |

---

## Stage 41: Arc Segment Rendering

### Overview
Extend the visual conversion pipeline to produce `VisualArcSegment` when a segment's carrier (after frame transform) is a circle. This introduces arc rendering via Godot's `draw_arc()`, including clockwise handling, point count scaling, and the escape step guard for segments ending at infinity. Angles are computed from the segment's three points during visual conversion only (§7.3).

NOTE: Godot 4.x `draw_arc()` behavior — verify whether the clockwise swap (swapping start_angle and end_angle) is still necessary in Godot 4.6. The Godot API may handle winding direction natively. Test with a known arc and confirm visual correctness before implementing the swap logic.

### Prerequisites
Stage 40 (CircleInversionEffect exists to motivate arc rendering, though not yet wired).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Data class | `VisualArcSegment` -- center, radius, start_angle, end_angle, clockwise | §7.2 |
| Script update | `scripts/visual/visual_converter.gd` -- produces VisualArcSegment when carrier is circle | §7.3 |
| Behavior | Clockwise flag: cross-product sign of (start-center) x (end-center) relative to via winding | §7.2 |
| Behavior | draw_arc() clockwise handling: swap start_angle and end_angle when clockwise=true | §7.2 |
| Behavior | Point count: 64 per full circle, scaled by arc span | §22.4 |
| Behavior | Escape step guard: end=INF renders as line to viewport edge, not arc | §12.3 |

### Unit Tests Added

1. **`test_stage41_visual_converter_line_carrier`**: Segment with collinear three points, identity frame. VisualConverter produces VisualLineSegment (not arc). Validates: line path unchanged.
2. **`test_stage41_visual_converter_circle_carrier`**: Segment with non-collinear three points (e.g., start=(200,100), end=(200,300), via=(300,200) from §16.2), identity frame. VisualConverter produces VisualArcSegment with center=(200,200), radius=100. Validates: §7.3.
3. **`test_stage41_arc_angles_from_three_points`**: For the §16.2 arc: start=(200,100), end=(200,300), via=(300,200). Verify start_angle and end_angle are computed correctly from the three points. Validates: §7.3 angle computation.
4. **`test_stage41_clockwise_flag_ccw_winding`**: For CCW winding (positive cross-product of (start-center) x (via-center)): clockwise=false. Validates: §7.2 clockwise determination.
5. **`test_stage41_clockwise_flag_cw_winding`**: For CW winding (negative cross-product): clockwise=true. Validates: §7.2.
6. **`test_stage41_draw_arc_clockwise_swap`**: When clockwise=true, the angles passed to draw_arc() are swapped (start_angle and end_angle exchanged). Validates: §7.2.
7. **`test_stage41_point_count_full_circle`**: Full circle arc (360 degrees) uses 64 points. Validates: §22.4.
8. **`test_stage41_point_count_quarter_circle`**: Quarter circle (90 degrees) uses 16 points. Validates: §22.4 scaling.
9. **`test_stage41_escape_step_guard`**: Segment with end=Vector2(INF,INF) renders as line to viewport edge, not arc. Validates: §12.3 escape guard.
10. **`test_stage41_S16_arc_no_nan`**: Arc conversion produces no NaN/Inf in center, radius, start_angle, end_angle. Validates: S16.

### Interactive User Tests

- [ ] No visual arc rendering expected yet (circle inversion not wired to trace). Press Play and verify the game runs identically to Stage 38.
- [ ] Run GUT. All tests pass including new arc conversion tests.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S16 | No NaN/Inf in visual arc output | Unit test | Reinforced (arc segments) |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Visual | Stage 5 | Green line from player to cursor | Move mouse |
| Visual | Stages 15--18 | Preview with all 5 step types | GUT tests + visual |
| Visual | Stages 34--38 | Visibility polygon | Visual inspection |
| Interaction | Stage 3 | Cursor tracks mouse | Move mouse |
| Trace | Stages 11--14 | Physical trace loop | GUT tests |
| Planning | Stages 21--27 | Image chain | GUT tests |

### Expected Visual State

Identical to Stage 38. Arc rendering code exists but is not triggered because no circle inversion surfaces exist in the trace yet.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/visual/visual_converter.gd` | Modify | Add VisualArcSegment production for circle carriers |
| `scripts/visual/visual_arc_segment.gd` | Create | VisualArcSegment data class |
| `scripts/visual/path_renderer.gd` | Modify | Add draw_arc() rendering for VisualArcSegment |
| `tests/test_stage41_arc_rendering.gd` | Create | Arc rendering unit tests |

---

## Stage 42: Circle Inversion in Physical Trace

### Overview
Wire CircleInversionEffect to surfaces so that the physical trace loop correctly handles circle inversion hits. After an inversion hit, the Mobius frame composes with the inversion transform, the ray origin advances to the inverse of the hit point, and the visual path contains arc segments. This stage reproduces the §16.2 worked example end-to-end with physical tracing.

### Prerequisites
Stages 40--41 (CircleInversionEffect class, arc rendering pipeline).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Surface with arc segment, left side = CircleInversionEffect | §9.1 |
| Behavior | Trace loop: on transformative hit, frame = frame.compose(effect.get_mobius()) | §12.1, §12.4 |
| Behavior | Origin advances to T_inverse(hit_point) -- for self-inverse, invert(hit_point) | §12.4 |
| Behavior | Visual path contains arc segments after inversion | §12.3 |
| Visual | Purple rendering for inversion surfaces | §22.2 |
| Worked example | §16.2 reproduction: player at (50,200), arc surface, arrow inverts | §16.2 |

### Unit Tests Added

1. **`test_stage42_inversion_surface_creation`**: Create a surface with arc segment (start=(200,100), end=(200,300), via=(300,200)), left side = CircleInversionEffect. Expected: surface created with circle carrier, center=(200,200), r=100. Validates: surface wiring.
2. **`test_stage42_trace_through_inversion`**: Player at (50,200), inversion surface as above. Ray direction toward (400,200). Physical trace hits arc at (300,200), inverts. Expected: hit recorded, frame updated, ray continues. Validates: §12.1 trace loop.
3. **`test_stage42_frame_after_inversion`**: After inversion hit, verify frame = identity.compose(inversion_mobius). Frame is non-identity, conjugating=true. Validates: §12.4 frame update.
4. **`test_stage42_origin_advance_self_inverse`**: After hit at (300,200) on inversion circle center=(200,200) r=100, new origin = invert(300,200) = (300,200) (point on circle maps to itself). Validates: §12.4 origin advance for self-inverse.
5. **`test_stage42_visual_path_contains_arc`**: Convert the traced path to visual. The second segment (post-inversion) is a VisualArcSegment. Validates: §12.3 arc in visual path.
6. **`test_stage42_S16_2_full_reproduction`**: Full §16.2 setup. Player at (50,200), surface C with inversion, cursor at (400,200). Trace produces: step 1 from (50,200) to hit at (300,200) [line], step 2 from (300,200) continuing toward cursor direction [arc in visual frame]. Validates: §16.2.
7. **`test_stage42_inversion_direction_unchanged`**: After inversion (transformative), the ray's Direction object is the same reference as before. Validates: §10.7 -- transformative effects do not change Direction.
8. **`test_stage42_purple_rendering`**: Inversion surface is rendered in purple color. Validates: §22.2.
9. **`test_stage42_S16_no_nan_in_trace`**: Full trace through inversion produces no NaN/Inf in any step start/end coordinates. Validates: S16.
10. **`test_stage42_arrow_arc_interpolation`**: Fire arrow through inversion surface, producing an arc path. During animation, sample the arrow position at t=0.25, t=0.5, t=0.75 along the arc step. Expected: each sampled position lies ON the arc (within 1 unit of the mathematical arc path), NOT on the straight line between arc start and arc end. Validates: §21.2 slerp interpolation, not linear interpolation.
11. **`test_stage42_state_change_with_inversion`**: Surface has CircleInversion effect AND a StateChange on the same side. Arrow hits it. Expected: state change fires correctly, AND inversion frame update applies. Both work correctly. Validates: §10.6 coexistence of effect + state change.
12. **`test_stage42_beyond_infinity_inversion_center`**: After circle inversion, a ray escapes through infinity. In the visual frame, infinity maps to the inversion center. Expected: the escape step's visual rendering shows the arrow converging toward the inversion center (not flying to the viewport edge as in the identity-frame case). Validates: §12.5 case 2 (M involves inversion).
13. **`test_stage42_arc_constant_visual_speed`**: Fire arrow through inversion, producing an arc step. Sample the arrow position at t=0, 0.25, 0.5, 0.75, 1.0 during the arc animation. Compute arc-length distances between consecutive samples. Expected: all distances are approximately equal (within 5% of each other). Confirms constant visual speed (equal arc-length per time unit), not constant parametric speed (equal angle per time unit). Validates: §21.2 "constant visual speed along the path."
14. **`test_stage42_arc_winding_direction`**: Fire arrow through inversion, producing an arc. The arc goes from start to end passing through the via point. During animation, sample the midpoint position (t=0.5). Expected: the midpoint is near the via point (within 10 units), NOT diametrically opposite on the circle. Validates: winding direction in slerp interpolation — the arrow traverses the correct arc, not the complementary arc.
15. **`test_stage42_transform_all_line_becomes_circle`**: Line carrier transformed by circle inversion (anti-conformal) becomes a circle in the normalized frame. Validates: carrier type change under inversion in transform_all context. *(Moved from Stage 21 where inversion was not yet available.)*
16. **`test_stage42_skip_during_arc_animation`**: Fire arrow through inversion surface producing an arc step. During the arc animation (mid-arc), simulate animation skip. Expected: animation completes instantly, all state changes from the arc step's hit are applied, next step begins correctly. No intermediate position artifacts. Validates: §21.1 animation skip works correctly for arc steps.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Press Play. Add an inversion surface (arc) to the test level. The surface renders in purple.
- [ ] Move the cursor. When the preview line passes through the inversion surface, the post-inversion path renders as a curved arc (not a straight line).
- [ ] Fire (spacebar). The arrow travels along the straight line to the inversion surface, then continues along the curved arc path.
- [ ] Verify the arc visually matches the straight-line portion before the hit (smooth transition at the hit point).
- [ ] Move the player (WASD) while observing the preview. The arc updates in real time.
- [ ] Fire through the inversion circle. Watch arrow flight. The arrow follows the curved arc path smoothly — it does NOT cut through the interior of the arc.
- [ ] After firing through an inversion circle, if the arrow escapes, observe it converging toward the inversion center rather than flying off-screen.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S2 | Transform round-trip for inversion | Unit test (Stage 40) | Inherited |
| S18 | Frame determinant non-zero after inversion compose | Unit test | Inherited |
| S16 | No NaN/Inf in trace output | Unit test | Inherited |
| UX3 | Preview matches flight (now with arcs) | Interactive test | Reinforced |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math classes | GUT tests |
| Visual | Stage 5 | Green line preview | Move mouse |
| Visual | Stage 41 | Arc rendering pipeline | GUT tests |
| Interaction | Stages 2--3 | Player movement, cursor tracking | Press WASD, move mouse |
| Trace | Stages 11--14 | Physical trace with reflection/block | GUT tests |
| Trace | Stage 42 | Inversion in trace | GUT tests + visual |
| Planning | Stages 21--27 | Plan construction, image chain | GUT tests |
| Visibility | Stages 34--38 | Visibility polygon | Visual inspection |

### Expected Visual State

Test level with an arc surface rendered in purple. When the cursor is positioned such that the preview passes through the inversion surface, the post-inversion path is a visible arc (curved line). Surfaces retain their prior colors (blue for reflection, red for block, gray for pass-through). The arc is smooth with appropriate point count.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/surface_node.gd` | Modify | Support CircleInversionEffect on surface sides |
| `scripts/math/tracer.gd` | Modify | Handle transformative hit with inversion (frame compose, origin advance) |
| `scripts/visual/path_renderer.gd` | Modify | Render inversion surfaces in purple |
| `tests/test_stage42_inversion_trace.gd` | Create | Inversion trace integration tests |

---

## Stage 42.5: Arc Collision Shapes for Player Physics

### Overview
Implement `ConcavePolygonShape2D` approximation for arc surfaces with `player_solid = true`. Line segments approximate the arc using 16 segments per full circle, scaled by arc span, with a minimum of 3 segments per arc. This ensures the player cannot walk through arc-shaped surfaces such as inversion circles or arc walls.

### Prerequisites
Stage 42 (circle inversion in physical trace -- arc surfaces now exist in the game).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | `ConcavePolygonShape2D` for arc surfaces with `player_solid = true` | §25.1 |
| Algorithm | Line segment approximation: 16 per full circle, scaled by arc span, minimum 3 segments | §25.1 |
| Behavior | Player collides with arc surfaces and slides along them | §25.1 |

### Unit Tests Added

1. **`test_stage42_5_arc_collision_shape_type`**: Create a surface with arc segment and `player_solid = true`. Expected: StaticBody2D child has `ConcavePolygonShape2D` (not `SegmentShape2D`). Validates: §25.1.
2. **`test_stage42_5_arc_segment_count_full_circle`**: Full circle arc (360°). Expected: 16 line segments in the ConcavePolygonShape2D. Validates: §25.1.
3. **`test_stage42_5_arc_segment_count_quarter`**: Quarter circle (90°). Expected: `max(3, floor(16 * 90/360))` = `max(3, 4)` = 4 segments. Validates: §25.1 scaling.
4. **`test_stage42_5_arc_segment_count_minimum`**: Very small arc (10°). Expected: 3 segments (minimum). Validates: §25.1 minimum.
5. **`test_stage42_5_player_collides_with_arc`**: Simulate player moving toward an arc surface. After `move_and_slide()`, player does not pass through. Validates: collision works.
6. **`test_stage42_5_player_slides_along_arc`**: Player moving along an arc surface. Expected: player slides smoothly (no jitter or sticking). Validates: slide behavior.
7. **`test_stage42_5_player_solid_false_arc`**: Arc surface with `player_solid = false`. Player walks through it. Arrow tracing still interacts with it. Validates: §9.1, §25.1.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Place an arc surface with `player_solid = true` (e.g., the inversion circle). Walk into it. Player stops and cannot pass through.
- [ ] Walk along the arc surface. Player slides smoothly along the curve.
- [ ] Set `player_solid = false` on the arc surface. Walk through it -- player passes through. Fire arrow at it -- arrow interacts normally (inverts, reflects, etc. based on effect).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX9 | Block surfaces stop the arrow | Inherited | No |
| All prior | All math, trace, plan, visibility invariants | Run full test suite | No |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 42 | Circle inversion in trace works | Fire at inversion surface |
| Stage 13 | Line surface collision works | Walk into room walls |
| Stage 2.5 | Gravity and jump work | Set gravity, jump |

### Expected Visual State

Identical to Stage 42 visually. The change is in collision behavior: the player now collides with arc surfaces, not just line surfaces. Arc surfaces that are blocking (or any effect with `player_solid = true`) stop the player.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/surface_node.gd` | Modify | Add ConcavePolygonShape2D generation for arc segments |
| `tests/test_stage42_5_arc_collision.gd` | Create | Arc collision shape unit tests |

---

## Stage 43: Circle Inversion in Planner

### Overview
Integrate circle inversion into the planning algorithm so that the image chain works correctly with inversion surfaces. Since circle inversion is self-inverse, the inverse transform of the cursor through an inversion surface is the inversion itself. Bounce points are found by intersecting the aim line with the circle carrier. This stage fully reproduces §16.2 with both planned and physical traces aligned.

### Prerequisites
Stage 42 (circle inversion wired to physical trace).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Image chain with circle inversion: inverse = itself (self-inverse) | §13.1, §13.2 |
| Behavior | Image of cursor = invert(cursor) through the circle | §13.2 |
| Behavior | Bounce point = intersect aim line with circle carrier | §13.2 |
| Behavior | Planned and physical traces align for valid inversion plans | §13.2 |
| Worked example | §16.2 full reproduction with plan=[{C, left}] | §16.2 |

### Unit Tests Added

1. **`test_stage43_inversion_image_chain`**: Plan=[{C, left}] with inversion surface C (center=(200,200), r=100). Cursor at (400,200). Image = invert(400,200) = (250,200). Validates: §13.2 image computation.
2. **`test_stage43_bounce_point_on_carrier`**: Aim line from player (50,200) to image (250,200). Intersect with circle carrier center=(200,200) r=100. Expected: bounce at (300,200). Validates: §13.2 bounce computation.
3. **`test_stage43_S16_2_plan_physical_aligned`**: Full §16.2: player=(50,200), cursor=(400,200), plan=[{C,left}]. Run planned trace and physical trace. All steps ALIGNED (divergence_index == null). Validates: §16.2.
4. **`test_stage43_planned_step_count`**: §16.2 setup produces exactly 2 planned steps (origin to bounce, bounce to cursor). Validates: step structure.
5. **`test_stage43_planned_visual_path_has_arc`**: The visual conversion of the planned path produces at least one VisualArcSegment. Validates: arcs appear in planned preview.
6. **`test_stage43_inversion_unreachable`**: Plan with inversion surface where the cursor is inside the circle (e.g., cursor at (200,200), which is the center). Image = infinity. Entry is unreachable. Validates: §13.5 unreachable entry handling.
7. **`test_stage43_double_inversion_plan`**: Plan=[{C,left},{C,left}] (same inversion twice). First inversion inverts, second un-inverts. Both should be reachable (unlike line reflection planned twice). Validates: §13.5 duplicate entry handling.
8. **`test_stage43_S5_aligned_provenance`**: For aligned steps in §16.2, planned and physical steps share identical start.id and frame_id. Validates: S5.
9. **`test_stage43_S6_aligned_match`**: Before divergence, same hit surface ID, side, and frame ID. Validates: S6.
10. **`test_stage43_full_circle_in_planner`**: Plan includes a full-circle inversion surface (start == end after full traversal). Image chain computes correctly — all carrier points are valid hits. Validates: §11.1 full-circle containment in planning algorithm.
11. **`test_stage43_aim_line_two_forward_hits_on_circle`**: Set up a plan where the aim line intersects a circle carrier at two forward points (both t > 0). The planner must select the correct one — the one that produces a valid image-chain continuation (not necessarily the nearest). Verify the planned trace matches the physical trace. Validates: §13.2 carrier intersection selection when multiple forward hits exist.

### Interactive User Tests

- [ ] Press Play. Create a plan by clicking the left (outer) side of the inversion surface. A numbered "1" appears on the surface.
- [ ] Move the cursor to various positions. The preview shows the planned path: a straight line to the inversion surface, then a curved arc continuing to the cursor.
- [ ] Verify the entire preview is solid green (ALIGNED) when the cursor is within the valid aim region.
- [ ] Move the cursor to a position where divergence occurs. Verify the preview shows the appropriate red/yellow diverged segments.
- [ ] Fire. The arrow follows the preview path exactly (solid green portions match flight).
- [ ] Plan the same inversion surface twice. Verify both entries are active and the preview shows a double-inversion path.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S5 | Aligned steps share provenance | Unit test | Reinforced (inversion) |
| S6 | Aligned steps match surface/side/frame | Unit test | Reinforced (inversion) |
| S2 | Transform round-trip | Unit test (Stage 40) | Inherited |
| S16 | No NaN/Inf in output | Unit test | Inherited |
| UX3 | Preview matches flight | Interactive test | Reinforced |
| UX11 | Empty plan = fire straight (still works) | Interactive test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math, Mobius transforms | GUT tests |
| Visual | Stages 5, 41 | Line preview, arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player movement, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42 | Physical trace with reflection/block/inversion | GUT tests |
| Planning | Stages 21--27 | Image chain with reflection, divergence | GUT tests |
| Planning | Stage 43 | Image chain with inversion | GUT tests |
| Visibility | Stages 34--38 | Visibility polygon (line surfaces) | Visual inspection |

### Expected Visual State

With a plan including an inversion surface: solid green preview from player to cursor, with a straight-line segment before the inversion hit and a curved arc segment after. The inversion surface is purple. Plan number overlay visible on the surface.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/planner.gd` | Modify | Support CircleInversionEffect in image chain |
| `tests/test_stage43_inversion_planner.gd` | Create | Inversion planning tests including §16.2 |

---

## Stage 44: Reflection Formula Fix

> **Note:** This stage was originally planned for "Visibility with Circles." The original content has been moved to Stage 70.

Test file: `test_stage44_reflection_formula.gd`

Fixed the reflection Mobius formula to correctly handle general-position carriers. Validates the mathematical correctness of `ReflectionEffect.normalized()` against known reflection identities.

---

## Stage 45: Arc Via Pole, Circle Rehit, and Grazing Fixes

> **Note:** This stage was originally planned for "Rigid Motion Effect (Portals)." The original content has been moved to Stage 71.

Test files: `test_stage45_arc_via_pole.gd`, `test_stage45_circle_rehit.gd`, `test_stage45_grazing.gd`

Fixed three classes of edge cases in the tracer:
- **Arc via pole:** Correct via-point computation when the Mobius transform maps the segment through the pole (infinity).
- **Circle rehit:** Prevent the tracer from immediately re-hitting the same circle surface after an inversion effect.
- **Grazing:** Handle rays that pass tangentially near a surface without a proper intersection.

---

## Stage 46: Epsilon Removal

> **Note:** This stage was originally planned for "Rigid Motion in Trace/Planner/Visibility." The original content has been moved to Stage 72.

Test file: `test_stage46_epsilon_removal.gd`

Removed all epsilon-based topology decisions from the intersection and tracing pipeline. Replaced with three-tier provenance endpoint detection (S31.3.1): (1) exact coordinate match via provenance, (2) structural collinearity (cross-product == 0.0), (3) quadratic solver. This was a foundational architectural improvement.

---

## Stage 47: Escape Via Fix

> **Note:** This stage was originally planned for "Line Normal Projection Effect." The original content has been moved to Stage 73.

Test file: `test_stage47_escape_via.gd`

Fixed the via-point computation for escape steps (rays that reach infinity). The via point for beyond-infinity segments is now correctly computed through the Mobius pole, ensuring proper arc rendering when the frame involves circle inversion.

---

## Stage 48: Full Segments

> **Note:** This stage was originally planned for "Circle Normal Projection Effect." The original content has been moved to Stage 74.

Test file: `test_stage48_full_segment.gd`

Added `Segment.full` boolean for unbounded segments (full circles, full lines with no endpoints). When `full = true`, all intersection points on the carrier are valid hits regardless of arc containment. Created via `Segment.full_from_carrier()`.

---

## Stage 49: Mid-Air Termination

> **Note:** This stage was originally planned for "Semi-Circle Directional Projection Effect." The original content has been moved to Stage 75.

Test file: `test_stage49_mid_air_termination.gd`

Fixed trace termination when a ray terminates mid-air (e.g., hitting a terminal surface that only blocks one side while the other side is pass-through). Ensured correct step generation for partial blockage scenarios.

---

## Stage 50: Spurious Reflection Fix

> **Note:** This stage was originally planned for "Mixed Planning Chain." The original content has been moved to Stage 77.

Test file: `test_stage50_spurious_reflection.gd`

Fixed spurious reflection effects firing when they shouldn't -- specifically cases where blockage accumulation at endpoints incorrectly triggered an effect on the wrong surface. Tightened the blockage accumulation logic in the hitpoint walk.

---

## Stage 51: Conjugation Side Fix

> **Note:** This stage was originally planned for "Compound Transformative Effect." The original content has been moved to Stage 78.

Test file: `test_stage51_conjugation_side.gd`

Fixed side determination under anti-conformal (conjugating) Mobius transforms. The conjugation flag was not being accounted for when determining LEFT/RIGHT sides after circle inversion, causing incorrect side assignment and wrong effect selection.

---

# Planned Future Stages

> **Status: PLANNED** -- The stages below describe features that have not yet been implemented. They were originally numbered as Stages 44--51 and 49.5, but those numbers were repurposed during development (see the Stage Reconciliation table above). The content has been renumbered to stages 70--78 to avoid confusion with the actual test files.

## Stage 70: Visibility with Circles

### Overview
Extend the visibility system to handle circular arc surfaces. This requires tangent point computation (from an external point to a circle), filtering tangent points to arc bounds, adding tangent points to points of interest, and propagating visibility through inversion surfaces. Curved region boundaries become possible in the visual frame after inversion. After a CircleNormalProjection effect, the visibility origin shifts to the circle center -- this is DISTINCT from circle inversion propagation, where the origin reflects through the circle. Inversion is transformative (origin = inverse(player) through circle); circle-normal-projection is projective (origin = circle center, point-source from center).

### Prerequisites
Stage 43 (circle inversion integrated into planner).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Tangent point computation: from external point P to circle (C,r) | §15.2 |
| Behavior | Tangent points exist when |P-C| > r, satisfy |T-C| = r and perpendicular to radius | §15.2 |
| Behavior | Filter tangent points to arc bounds (cross-product containment §11.1) | §15.2 |
| Behavior | Tangent points added to points of interest in visibility | §15.2 |
| Behavior | Visibility propagation through inversion surface: frame compose, origin shift | §15.2, §15.3 |
| Behavior | Curved region boundaries in visual frame | §15.2 |

### Unit Tests Added

1. **`test_stage44_tangent_points_external`**: Point P=(400,200), circle center=(200,200) r=100. |P-C|=200 > r=100. Two tangent points exist. Verify they satisfy |T-C|=100 and (T-P) dot (T-C) = 0. Validates: §15.2 tangent computation.
2. **`test_stage44_tangent_points_on_circle`**: Point P on the circle (|P-C|=r). Expected: exactly one tangent point (P itself). Validates: degenerate case.
3. **`test_stage44_tangent_points_inside`**: Point P inside circle (|P-C| < r). Expected: no tangent points. Validates: §15.2 existence condition.
4. **`test_stage44_tangent_filter_to_arc`**: Two tangent points computed for a full circle. Only one lies on the arc (start=(200,100), end=(200,300), via=(300,200)). Expected: only the one within arc bounds is retained. Validates: §15.2 arc filtering.
5. **`test_stage44_visibility_with_arc_surface`**: Scene with one arc surface (inversion). Player inside the arc's convex side. Compute visibility. Expected: tangent points appear in points of interest. Visibility region is correctly bounded by the arc. Validates: §15.2.
6. **`test_stage44_visibility_propagation_inversion`**: Plan=[{C, left}] with inversion surface. Visibility propagates through inversion: frame composes, origin shifts. Post-inversion visibility region exists. Validates: §15.2.
7. **`test_stage44_S13_visibility_no_self_intersect`**: Visibility region boundaries with arc edges do not self-intersect. Validates: S13.
8. **`test_stage44_S14_edges_on_geometry`**: Every visibility polygon edge lies on a surface carrier or a ray from the origin. Validates: S14.
9. **`test_stage44_S15_non_overlapping`**: Distinct visibility regions do not overlap. Validates: S15.
10. **`test_stage44_UX1_visibility_predicts_alignment`**: Cursor within visibility region with inversion plan. Planned and physical traces agree (no divergence). Validates: UX1.
11. **`test_stage44_UX2_divergence_outside_visibility`**: Cursor outside visibility region. Traces diverge. Validates: UX2.
12. **`test_stage44_circle_normal_projection_origin`**: Plan=[{arc_surface, LEFT}] with CircleNormalProjection. After visibility processing, origin = circle center, NOT inverse of player. Validates: §15.2.
13. **`test_stage44_inversion_vs_projection_origin_differs`**: Same arc configured as (a) CircleInversion and (b) CircleNormalProjection. Origins differ after visibility propagation. Validates: §15.2 distinction.
14. **`test_stage44_visibility_arc_tessellation_fidelity`**: Visibility region with curved (arc) edge. Sample 100 points along arc edge. All within 1 unit of mathematical arc. Validates: rendering fidelity.
15. **`test_stage44_point_in_arc_region`**: Visibility region has a curved (arc) boundary edge. Test point-in-region for points near the arc boundary. Expected: points on the convex side of the arc are inside the region; points on the concave side are outside. The query correctly handles curved edges, not just straight-edge polygon containment. Validates: UX1/UX2 reliability with arc-bounded regions.
16. **`test_stage44_full_circle_visibility_tangents`**: Visibility cast toward a full-circle surface. Tangent points exist at two locations on the circle. Both are valid POIs. Validates: full-circle segments in visibility computation.

### Interactive User Tests

- [ ] Press Play with a level containing an arc (inversion) surface and a plan including it.
- [ ] Observe the visibility region. It should be bounded by the arc surface's geometry (curved boundary).
- [ ] Move the cursor within the highlighted region. Preview is solid green (aligned).
- [ ] Move the cursor outside the highlighted region. Preview shows divergence (red/yellow).
- [ ] Add multiple arc surfaces. Visibility correctly accounts for all of them.
- [ ] Verify tangent edges are smooth and match arc curvature.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S13 | Visibility: no self-intersection | Unit test | Reinforced (circles) |
| S14 | Visibility: edges on geometry | Unit test | Reinforced (circles) |
| S15 | Visibility: non-overlapping | Unit test | Reinforced (circles) |
| UX1 | Visibility predicts non-divergence | Unit test | Reinforced (circles) |
| UX2 | Divergence implies outside visibility | Unit test | Reinforced (circles) |
| S16 | No NaN/Inf in visibility output | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math | GUT tests |
| Visual | Stages 5, 41 | Line and arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42 | Physical trace | GUT tests |
| Planning | Stages 21--27, 43 | Planning with reflection and inversion | GUT tests |
| Visibility | Stages 34--38 | Visibility with line surfaces only | GUT tests |
| Visibility | Stage 44 | Visibility with circle surfaces | GUT tests |

### Expected Visual State

Visibility region bounded by arc surfaces appears as a curved-edge polygon. The highlighted region correctly identifies valid aim positions for plans involving inversion surfaces. The boundary smoothly follows the arc curvature.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/visibility.gd` | Modify | Add tangent point computation, arc filtering, inversion propagation |
| `scripts/visual/visibility_renderer.gd` | Modify | Render curved visibility region boundaries |
| `tests/test_stage44_visibility_circles.gd` | Create | Visibility with circle surface tests |

---

## Stage 71: Rigid Motion Effect (Portals)

### Overview
Implement `RigidMotionEffect`, a conformal Mobius transformation that combines rotation by angle theta and translation by displacement d. Unlike reflection and circle inversion, rigid motion is NOT self-inverse -- the inverse must be precomputed and stored. Rigid motion enables portal/teleport mechanics. Surfaces with rigid motion render in cyan.

### Prerequisites
Stage 44 (visibility with circles complete; full circle inversion pipeline provides the pattern for integrating a new transformative effect).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/effects/rigid_motion.gd` -- RigidMotionEffect class | §10.2, §10.8 |
| Formula | alpha=e^{i*theta}, beta=d, gamma=0, delta=1, conjugating=false | §10.8 |
| Property | Conformal (conjugating=false) | §10.2 |
| Property | NOT self-inverse; inverse precomputed | §10.8 |
| Formula (inverse) | alpha=e^{-i*theta}, beta=-e^{-i*theta}*d, gamma=0, delta=1, conjugating=false | §10.8 |
| Visual | Cyan rendering for rigid motion surfaces | §22.2 |

### Unit Tests Added

1. **`test_stage45_rigid_motion_construction`**: Construct RigidMotionEffect with theta=PI/2, d=(100,50). Expected: construction succeeds, `get_mobius()` returns MobiusTransform with conjugating=false. Validates: §10.8 conformal flag.
2. **`test_stage45_rigid_motion_coefficients`**: For theta=PI/2, d=(100,50): verify alpha=e^{i*PI/2}=(0,1), beta=(100,50), gamma=(0,0), delta=(1,0). Validates: §10.8 formula.
3. **`test_stage45_rigid_motion_apply`**: Apply rigid motion (theta=PI/2, d=(100,0)) to z=(1,0). Expected: w = e^{i*PI/2}*(1,0) + (100,0) = (0,1) + (100,0) = (100,1). Validates: §10.8 application.
4. **`test_stage45_rigid_motion_inverse_coefficients`**: For theta=PI/2, d=(100,50): inverse alpha=e^{-i*PI/2}=(0,-1), inverse beta=-e^{-i*PI/2}*(100,50). Validates: §10.8 inverse formula.
5. **`test_stage45_rigid_motion_not_self_inverse`**: Apply forward then inverse. Forward(z) != z in general. But inverse(forward(z)) == z. Validates: NOT self-inverse but inverse is correct.
6. **`test_stage45_S2_transform_round_trip`**: For point P, apply rigid motion via cache, then apply inverse. Result has same Point ID as original P. Validates: S2.
7. **`test_stage45_S18_determinant_nonzero`**: Verify |alpha*delta - beta*gamma|^2 > 0 for the constructed Mobius matrix. |e^{i*theta}*1 - d*0| = 1 > 0. Validates: S18.
8. **`test_stage45_rigid_motion_pure_translation`**: theta=0, d=(50,0). Apply to z=(10,20). Expected: w=(60,20). Validates: pure translation case.
9. **`test_stage45_rigid_motion_pure_rotation`**: theta=PI, d=(0,0). Apply to z=(1,0). Expected: w=(-1,0). Validates: pure rotation case.
10. **`test_stage45_rigid_motion_identity`**: theta=0, d=(0,0). Apply to z=(5,7). Expected: w=(5,7). Validates: §31.6 identity case.
11. **`test_stage45_S16_no_nan`**: Apply rigid motion to multiple test points. All results finite. Validates: S16.

### Interactive User Tests

- [ ] No trace/visual change expected yet (rigid motion not wired to surfaces). Press Play and verify the game runs identically to Stage 44.
- [ ] Run GUT. All prior tests plus new Stage 45 tests pass.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S2 | Transform round-trip: rigid motion inverse returns same Point ID | Unit test | Reinforced (rigid motion) |
| S18 | Frame determinant non-zero for rigid motion Mobius | Unit test | Reinforced (rigid motion) |
| S16 | No NaN/Inf in output | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math, Mobius transforms | GUT tests |
| Math layer | Stage 40 | CircleInversionEffect | GUT tests |
| Visual | Stages 5, 41--42 | Line and arc rendering, purple surfaces | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42 | Physical trace with all effects so far | GUT tests |
| Planning | Stages 21--27, 43 | Planning with reflection, inversion | GUT tests |
| Visibility | Stages 34--38, 44 | Visibility with line and circle surfaces | GUT tests |

### Expected Visual State

Identical to Stage 44. Rigid motion effect class exists but is not wired to any surface yet.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/effects/rigid_motion.gd` | Create | RigidMotionEffect class implementing TransformativeEffect |
| `tests/test_stage45_rigid_motion.gd` | Create | Rigid motion unit tests |

---

## Stage 72: Rigid Motion in Trace/Planner

### Overview
Verify that RigidMotionEffect integrates end-to-end with the tracer and planner via the generic TransformativeEffect handling. Rigid motion acts as a portal: the arrow enters one surface and exits at a rotated and translated position. Unlike reflection and inversion (anti-conformal), rigid motion is conformal, so the Mobius composition follows the conformal-conformal case. Surfaces render in cyan.

**Note:** No source file modifications are needed — the tracer and planner already handle `TransformativeEffect` generically. This stage is pure integration testing. Visibility tests (tests 6, 10, 11, 13 in the original spec) are deferred until Stage 70 (Visibility with Circles) is implemented.

### Prerequisites
Stage 71 (RigidMotionEffect class).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Surface with rigid motion effect (portal mechanic) | §9.1 |
| Behavior | Physical trace: arrow enters portal, exits at rotated+translated position | §12.1 |
| Behavior | Origin advances to rigid_inverse(hit_point) | §12.4 |
| Behavior | Planner: image chain through rigid motion (inverse_transform cursor) | §13.2 |
| Behavior | Visibility: propagation through rigid motion surface | §15.2 |
| Visual | Cyan surface rendering | §22.2 |

### Unit Tests Added

1. **`test_stage46_rigid_motion_surface`**: Create surface with left side = RigidMotionEffect(theta=PI/2, d=(200,0)). Renders in cyan. Validates: surface wiring.
2. **`test_stage46_trace_through_portal`**: Player at (50,200), portal surface at x=100 with rigid motion (theta=0, d=(300,0)). Arrow hits portal at (100,200), exits at (400,200). Continues in same direction. Validates: §12.1.
3. **`test_stage46_trace_rotation_portal`**: Portal with theta=PI/2, d=(0,0). Arrow traveling right hits portal, exits traveling upward. Validates: rotation in rigid motion.
4. **`test_stage46_origin_advance_rigid`**: After hit at (100,200), rigid_inverse(100,200) used as new origin. For theta=0, d=(300,0): inverse = translate by (-300,0), so new origin = (-200,200). In normalized frame, ray continues from (-200,200). Validates: §12.4.
5. **`test_stage46_planner_rigid_motion`**: Plan=[{portal, left}], cursor at (500,200). Image = rigid_inverse(cursor). Aim line from player to image, bounce on portal carrier. Planned and physical traces align. Validates: §13.2.
6. **`test_stage46_visibility_through_portal`**: Visibility propagates through rigid motion surface. Post-portal visibility region exists and is shifted/rotated. Validates: §15.2.
7. **`test_stage46_conformal_composition`**: Compose identity frame with rigid motion Mobius. Result is conformal (conjugating=false). Then compose with reflection. Result is anti-conformal (conjugating=true). Validates: §5.2 composition table.
8. **`test_stage46_portal_chain`**: Two portals in sequence. Arrow enters first, exits, enters second, exits. All steps computed correctly. Validates: chained rigid motions.
9. **`test_stage46_S5_aligned_provenance`**: For aligned portal plan, planned and physical steps share identical provenance. Validates: S5.
10. **`test_stage46_UX1_visibility_with_portal`**: Cursor in visibility region with portal plan. No divergence. Validates: UX1.
11. **`test_stage46_UX1_UX2_resweep_with_portal`**: Grid sweep (5x5 player x 5x5 cursor = 625 combinations) with plan=[{portal, LEFT}]. For each: if cursor in visibility, verify no divergence (UX1). If divergence, verify cursor outside visibility (UX2). Validates: UX1/UX2 after portal integration.
12. **`test_stage46_state_change_with_rigid_motion`**: Surface has RigidMotion effect AND a StateChange on the same side. Arrow hits. Expected: state change fires correctly AND rigid motion applies. Validates: §10.6 coexistence.
13. **`test_stage46_rigid_motion_visibility_origin_coords`**: Plan includes a rigid motion surface (rotation θ=π/2, translation d=(100,0)). After visibility propagation through the portal, verify the new origin is at the exact expected coordinates: `rigid_inverse(player_position)`. Compare computed origin to analytically derived expected position. Validates: §15.2 visibility propagation geometric precision for rigid motion.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Press Play with a portal (rigid motion) surface. Surface renders in cyan.
- [ ] Click the portal to add it to the plan. Preview shows the arrow entering the portal and exiting at the displaced/rotated position.
- [ ] Fire. The arrow travels to the portal and exits at the correct position with the correct direction.
- [ ] Observe visibility region. It correctly propagates through the portal.
- [ ] Set up two portals in sequence. Preview shows the arrow teleporting twice. Fire and verify.
- [ ] Create a plan mixing reflection and portal. Verify correct behavior.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S2 | Transform round-trip for rigid motion | Unit test (Stage 45) | Inherited |
| S5 | Aligned provenance with portal plan | Unit test | Reinforced |
| S18 | Frame determinant non-zero | Unit test (Stage 45) | Inherited |
| UX1 | Visibility predicts non-divergence with portals | Unit test | Reinforced |
| UX3 | Preview matches flight with portals | Interactive test | Reinforced |
| S16 | No NaN/Inf | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math | GUT tests |
| Math layer | Stages 40, 45 | Circle inversion, rigid motion effects | GUT tests |
| Visual | Stages 5, 41--42 | Line/arc rendering, purple inversion surfaces | Move mouse, visual |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42 | Physical trace with reflection/block/inversion | GUT tests |
| Planning | Stages 21--27, 43 | Planning with reflection/inversion | GUT tests |
| Visibility | Stages 34--38, 44 | Visibility with line/circle surfaces | GUT tests |

### Expected Visual State

Cyan portal surfaces visible. Preview shows teleportation paths through portals. Visibility region extends through portal boundaries. All prior surface types (blue reflection, purple inversion, red block, gray pass-through) still render correctly.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/game/surface_node.gd` | Modify | Support RigidMotionEffect on surface sides |
| `scripts/math/tracer.gd` | Modify | Handle rigid motion in trace loop (already handles TransformativeEffect generically) |
| `scripts/math/planner.gd` | Modify | Handle rigid motion in image chain (already handles TransformativeEffect generically) |
| `scripts/math/visibility.gd` | Modify | Handle rigid motion propagation (already handles TransformativeEffect generically) |
| `scripts/visual/path_renderer.gd` | Modify | Add cyan rendering for rigid motion surfaces |
| `tests/test_stage72_rigid_motion_integration.gd` | Create | Rigid motion trace/planner integration tests (12 tests) |

---

## Stage 73: Line Normal Projection Effect

### Overview
Implement `LineNormalProjection`, the first projective effect. Projective effects differ fundamentally from transformative effects: the outgoing ray's direction depends only on the hit point (not the incoming direction), the Mobius frame resets to identity, and a new Direction is created. For line normal projection, the outgoing ray is perpendicular to the surface line at the hit point. Back-propagation finds the orthogonal projection of the target onto the surface line. Projective surfaces render in orange.

### Prerequisites
Stage 46 (all three transformative effects integrated; the planner already has the projective break point structure from §13.3--13.4, but it has not been exercised until now).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/effects/line_normal_projection.gd` -- LineNormalProjection class | §10.4 |
| Method | `apply_forward(hit_point, surface, side) -> Ray` -- outgoing ray perpendicular to surface line | §10.4 |
| Method | `back_propagate(target, surface, side) -> Vector2?` -- orthogonal projection of target onto line; null if outside segment | §10.4 |
| Behavior | Frame resets to identity after projective effect | §10.7 |
| Behavior | New Direction created (direction changes) | §10.7 |
| Visual | Orange rendering for projective surfaces | §22.2 |

### Unit Tests Added

1. **`test_stage47_line_normal_apply_forward`**: Surface with vertical line segment (x=250). Hit at (250,200). Expected: outgoing ray perpendicular to line, horizontal direction. apply_forward returns Ray with origin=(250,200) and Direction perpendicular to the line. Validates: §10.4.
2. **`test_stage47_line_normal_apply_forward_direction`**: Outgoing direction is perpendicular to the surface line. For vertical line, outgoing is horizontal. For horizontal line, outgoing is vertical. Validates: §10.4.
3. **`test_stage47_line_normal_back_propagate`**: Target at (400,300). Surface is vertical line at x=250, segment from y=100 to y=400. H = orthogonal projection of (400,300) onto x=250 = (250,300). (250,300) is within segment bounds (100 <= 300 <= 400). Expected: returns (250,300). Validates: §10.4 back-propagation.
4. **`test_stage47_line_normal_back_propagate_outside`**: Target at (400,500). Projection onto segment (y=100 to y=400) is (250,500). 500 > 400, outside segment. Expected: returns null. Validates: §10.4 null return.
5. **`test_stage47_S10_frame_resets`**: After projective hit, the frame is identity (frame_id == IDENTITY_ID). Validates: S10.
6. **`test_stage47_new_direction_created`**: After projective hit, the ray has a NEW Direction object (different reference from the incoming Direction). Validates: §10.7 direction change.
7. **`test_stage47_trace_through_projective`**: Physical trace hits a line normal projection surface. Frame resets, direction changes, ray continues perpendicular to the surface. Validates: §12.1 trace loop with projective.
8. **`test_stage47_orange_rendering`**: Projective surface renders in orange. Validates: §22.2.
9. **`test_stage47_S16_no_nan`**: apply_forward and back_propagate produce no NaN/Inf for valid inputs. Validates: S16.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

**Note:** Adding this projective surface to the plan will show incomplete preview behavior — the full mixed planning algorithm (`plan_mixed` with projective break points) is not implemented until Stage 50. The preview may not show the correct planned path through this surface until then.

- [ ] Press Play with a line normal projection surface (vertical line). Surface renders in orange.
- [ ] Move cursor. When preview passes through the projective surface, the post-projection path is perpendicular to the surface (sharp direction change, not a bounce). (PHYSICAL TRACE ONLY — plan preview for projective effects is incomplete until Stage 50)
- [ ] Fire. Arrow hits the projective surface and exits perpendicular, regardless of the incoming angle.
- [ ] Try different incoming angles. Output direction is always perpendicular to the surface. Incoming angle does not matter.
- [ ] Verify frame resets: if there was an inversion or portal before the projective, the post-projective path is a straight line (no arc curvature).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S10 | Projective resets frame to identity | Unit test | Yes |
| S16 | No NaN/Inf in output | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math | GUT tests |
| Math layer | Stages 40, 45 | Transformative effects | GUT tests |
| Visual | Stages 5, 41--42 | Line/arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42, 46 | Physical trace with all transformative effects | GUT tests |
| Planning | Stages 21--27, 43, 46 | Planning with transformative effects | GUT tests |
| Visibility | Stages 34--38, 44, 46 | Visibility with all surface types so far | GUT tests |

### Expected Visual State

Orange projective surface visible. When the preview passes through it, the path changes direction to be perpendicular to the surface line. Post-projective path is always a straight line (frame reset).

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/effects/line_normal_projection.gd` | Create | LineNormalProjection class implementing ProjectiveEffect |
| `scripts/math/tracer.gd` | Modify | Handle ProjectiveEffect in trace loop (frame reset, new Direction) |
| `scripts/visual/path_renderer.gd` | Modify | Add orange rendering for projective surfaces |
| `tests/test_stage47_line_normal_projection.gd` | Create | Line normal projection tests |

---

## Stage 74: Circle Normal Projection Effect

### Overview
Implement `CircleNormalProjection`, a projective effect for arc surfaces. The outgoing ray travels along the radius at the hit point (from the circle center through the hit point). Back-propagation finds the intersection of line(center, target) with the arc segment. Like all projective effects, the frame resets to identity and a new Direction is created.

### Prerequisites
Stage 73 (line normal projection establishes the projective pattern in the trace loop).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/effects/circle_normal_projection.gd` -- CircleNormalProjection class | §10.4 |
| Method | `apply_forward(hit_point, surface, side) -> Ray` -- outgoing ray along radius | §10.4 |
| Method | `back_propagate(target, surface, side) -> Vector2?` -- intersection of line(center, target) with arc; null if outside segment | §10.4 |
| Behavior | Frame resets to identity | §10.7 |

### Unit Tests Added

1. **`test_stage48_circle_normal_apply_forward`**: Arc surface centered at (200,200), r=100. Hit at (300,200). Expected: outgoing ray from (300,200) in the direction (300,200)-(200,200) = (1,0), i.e., radially outward. Validates: §10.4.
2. **`test_stage48_circle_normal_apply_forward_angled`**: Hit at (200,300) (top of circle). Expected: outgoing ray direction (0,1) (radially outward, upward in Godot coords). Validates: §10.4 for non-axis-aligned hit.
3. **`test_stage48_circle_normal_back_propagate`**: Target at (400,200). Center=(200,200). Line from center to target is horizontal y=200. Intersect with arc (start=(200,100), end=(200,300), via=(300,200)). Intersection at (300,200), which is on the arc. Expected: returns (300,200). Validates: §10.4.
4. **`test_stage48_circle_normal_back_propagate_outside_arc`**: Target positioned such that line(center, target) intersects the circle but NOT the arc segment. Expected: returns null. Validates: §10.4 segment bounds check.
5. **`test_stage48_S10_frame_resets`**: After circle normal projection hit, frame is identity. Validates: S10.
6. **`test_stage48_trace_through_circle_projection`**: Physical trace hits a circle normal projection surface. Frame resets, direction becomes radial, ray continues outward from the hit point. Validates: §12.1.
7. **`test_stage48_S16_no_nan`**: All outputs finite for valid inputs. Validates: S16.
8. **`test_stage48_back_propagate_target_at_center`**: Target position is exactly at the circle center. `line(center, target)` is degenerate (zero-length). Expected: `back_propagate` returns null (geometrically impossible). No crash, no NaN. Validates: degenerate back-propagation handling.

### Interactive User Tests

**Note:** Adding this projective surface to the plan will show incomplete preview behavior — the full mixed planning algorithm (`plan_mixed` with projective break points) is not implemented until Stage 50. The preview may not show the correct planned path through this surface until then.

- [ ] Press Play with an arc surface assigned circle normal projection effect. Surface renders in orange.
- [ ] Move cursor. Preview shows the path entering the arc surface and exiting radially (away from center). (PHYSICAL TRACE ONLY — plan preview for projective effects is incomplete until Stage 50)
- [ ] Try different hit points on the arc. Each time, the exit direction points radially outward from the center.
- [ ] Fire and verify the arrow follows the radial exit path.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S10 | Projective resets frame to identity | Unit test | Reinforced (circle normal) |
| S16 | No NaN/Inf in output | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math | GUT tests |
| Math layer | Stages 40, 45 | Transformative effects | GUT tests |
| Math layer | Stage 73 | Line normal projection | GUT tests |
| Visual | Stages 5, 41--42 | Line/arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42, 46--47 | Physical trace with all effects | GUT tests |
| Planning | Stages 21--27, 43, 46 | Planning with transformative/projective | GUT tests |
| Visibility | Stages 34--38, 44, 46 | Visibility | GUT tests |

### Expected Visual State

Arc surface with circle normal projection in orange. Preview shows radial exit direction from hit points on the arc. Same as line normal projection but for circular geometry.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/effects/circle_normal_projection.gd` | Create | CircleNormalProjection class implementing ProjectiveEffect |
| `tests/test_stage48_circle_normal_projection.gd` | Create | Circle normal projection tests |

---

## Stage 75: Semi-Circle Directional Projection Effect

### Overview
Implement `SemicircleDirectionalProjection`, a projective effect where the outgoing direction is always normal to the semicircle's diameter line, regardless of hit position. The diameter is determined by the segment's start and end points. The exit direction (which of the two normals) depends on the approach side -- the ray exits toward the side opposite the approach. Back-propagation traces a line from the target in the negative normal direction to find the intersection with the arc.

### Prerequisites
Stage 48 (circle normal projection; all three projective effects follow the same pattern).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/effects/semicircle_directional_projection.gd` -- SemicircleDirectionalProjection class | §10.4 |
| Method | `apply_forward(hit_point, surface, side) -> Ray` -- outgoing ray normal to diameter line | §10.4 |
| Property | Diameter determined by segment start and end points | §10.4 |
| Property | Exit direction depends on approach side (opposite) | §10.4 |
| Method | `back_propagate(target, surface, side) -> Vector2?` -- intersection of line(target, -normal) with arc | §10.4 |
| Behavior | Frame resets to identity | §10.7 |

### Unit Tests Added

1. **`test_stage49_semicircle_direction_horizontal_diameter`**: Arc with start=(100,200) end=(300,200) (horizontal diameter). Normal to diameter is vertical. Outgoing direction is vertical (up or down depending on approach side). Validates: §10.4 diameter normal.
2. **`test_stage49_semicircle_direction_approach_left`**: Approach from left side. Exit direction is opposite (toward right side normal). Validates: §10.4 approach side rule.
3. **`test_stage49_semicircle_direction_approach_right`**: Approach from right side. Exit direction is opposite (toward left side normal). Validates: §10.4.
4. **`test_stage49_semicircle_hit_position_independent`**: Hit at different positions on the arc. Outgoing direction is the SAME (always normal to diameter, same side). Validates: §10.4 position independence.
5. **`test_stage49_semicircle_back_propagate`**: Target at (200,400). Normal direction is (0,1) (downward). Back-propagation: line from (200,400) in direction (0,-1). Intersect with arc. Expected: point on the arc at the intersection. Validates: §10.4 back-propagation.
6. **`test_stage49_semicircle_back_propagate_null`**: Target positioned such that line(target, -normal) does not intersect the arc segment. Expected: returns null. Validates: §10.4.
7. **`test_stage49_S10_frame_resets`**: After semicircle directional projection hit, frame is identity. Validates: S10.
8. **`test_stage49_trace_through_semicircle`**: Physical trace hits semicircle directional projection surface. Direction changes to diameter normal. Frame resets. Validates: §12.1.
9. **`test_stage49_S16_no_nan`**: All outputs finite. Validates: S16.

### Interactive User Tests

**Note:** Adding this projective surface to the plan will show incomplete preview behavior — the full mixed planning algorithm (`plan_mixed` with projective break points) is not implemented until Stage 50. The preview may not show the correct planned path through this surface until then.

- [ ] Press Play with a semicircle directional projection surface. Surface renders in orange.
- [ ] Move cursor. Preview shows the path entering the arc and exiting in a fixed direction (normal to diameter), regardless of where on the arc the hit occurs. (PHYSICAL TRACE ONLY — plan preview for projective effects is incomplete until Stage 50)
- [ ] Hit the arc from different sides. Exit direction flips (normal changes sign).
- [ ] Fire and verify the arrow exits in the fixed direction.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S10 | Projective resets frame to identity | Unit test | Reinforced (semicircle) |
| S16 | No NaN/Inf in output | Unit test | Inherited |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math | GUT tests |
| Math layer | Stages 40, 45 | Transformative effects | GUT tests |
| Math layer | Stages 47--48 | Line/circle normal projection | GUT tests |
| Visual | Stages 5, 41--42 | Line/arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42, 46--48 | Physical trace with all effects | GUT tests |
| Planning | Stages 21--27, 43, 46 | Planning | GUT tests |
| Visibility | Stages 34--38, 44, 46 | Visibility | GUT tests |

### Expected Visual State

Semicircle directional projection surface in orange. Preview exits in a fixed direction normal to the diameter line. Direction depends on approach side but not hit position.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/effects/semicircle_directional_projection.gd` | Create | SemicircleDirectionalProjection class implementing ProjectiveEffect |
| `tests/test_stage49_semicircle_projection.gd` | Create | Semicircle directional projection tests |

---

## Stage 76: Parallel-Source Visibility Mode

### Overview
Implement parallel-source visibility for plans containing line-normal or semicircle-directional projection effects. After such a projective effect, visibility uses parallel rays emanating from the lit sub-segment in the fixed outgoing direction, rather than point-source radial casting. This requires `cast_parallel_source()`, `determine_obstruction_linear()`, and `project_point_onto_segment_along_direction()`.

### Prerequisites
Stage 49 (all projective effects implemented).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Function | `cast_parallel_source(direction, lit_segments, scene, game_state) -> Array[Region]` | §15.2 |
| Function | `determine_obstruction_linear(hit, poi) -> Obstruction` | §15.2 |
| Function | `project_point_onto_segment_along_direction(point, segment, direction) -> Vector2?` | §15.2 |
| Behavior | After LineNormalProjection or SemicircleDirectionalProjection in plan, visibility switches to parallel-source mode | §15.2 |
| Behavior | Parallel rays originate from different points on the lit sub-segment, all sharing the same direction | §15.2 |
| Behavior | POIs sorted by linear position along the lit sub-segment (not radially) | §15.2 |

### Unit Tests Added

1. **`test_stage49_5_parallel_source_basic`**: Plan=[{P, left}] where P is LineNormalProjection. After projection, cast parallel rays perpendicular to P. Expected: visibility region is a strip of parallel rays. Validates: §15.2.
2. **`test_stage49_5_project_point_onto_segment`**: Point at (400, 300), segment from (250,100) to (250,400), direction=(1,0). Expected: returns (250, 300). Validates: §15.2.
3. **`test_stage49_5_project_point_outside_segment`**: Projection falls outside segment bounds. Expected: returns null. Validates: bounds check.
4. **`test_stage49_5_determine_obstruction_linear`**: Hit creates above/below obstruction. Validate correct classification. Validates: §15.2.
5. **`test_stage49_5_parallel_cast_with_block`**: Parallel rays from lit segment hit a block surface. Shadow region behind block correctly computed. Validates: parallel-mode obstruction.
6. **`test_stage49_5_UX1_with_projective_plan`**: Plan=[{mirror, LEFT}, {projective, LEFT}]. Cursor in parallel-source visibility region. Expected: no divergence. Validates: UX1.
7. **`test_stage49_5_UX2_with_projective_plan`**: Cursor outside parallel-source visibility region. Expected: divergence. Validates: UX2.
8. **`test_stage49_5_see_through_in_parallel`**: Parallel rays use `see_through=lit_segments`. Rays pass through the projective surface itself. Validates: §11.6 integration.
9. **`test_stage49_5_parallel_rays_graze_endpoint`**: Parallel rays pass exactly through a surface endpoint. Expected: no crash, correct obstruction determination at the endpoint. Validates: parallel-source edge case.
10. **`test_stage49_5_parallel_rays_through_gap`**: Parallel rays pass through a gap between two surfaces. Expected: the gap is correctly identified as unobstructed in the parallel-source region. Validates: gap handling.
11. **`test_stage49_5_parallel_source_curved_lit_segment`**: The lit segment from a prior stage is an arc (after circle inversion). Parallel rays originate from points along the arc. Expected: parallel rays originate from points along the arc (not from a straight-line approximation). The resulting visibility region accounts for the arc curvature of the source segment. Validates: arc-sourced parallel casting.
12. **`test_stage49_5_parallel_rays_multiple_surfaces_same_position`**: Two surfaces at the same linear position along the lit segment. Expected: deterministic handling (tie-break by surface ID). Validates: tie-breaking in parallel mode.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Plan a line-normal projection surface. Visibility region changes shape -- it becomes a parallel strip, not a radial cone.
- [ ] Move cursor inside the parallel visibility region. Preview is green (aligned).
- [ ] Move cursor outside the parallel visibility region. Preview shows divergence (red/yellow).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| UX1 | Visibility predicts non-divergence (now including projective) | Unit test: cursor in parallel region -> no divergence | Reinforced |
| UX2 | Divergence implies outside visibility (now including projective) | Unit test: divergence -> cursor outside parallel region | Reinforced |
| S13 | Visibility no self-intersection | Unit test | Inherited |
| S14 | Visibility edges on geometry | Unit test | Inherited |
| S15 | Visibility non-overlapping | Unit test | Inherited |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 49 | All projective effects work correctly | Fire at each projective type |
| Stage 38 | Multi-step visibility (non-projective) | Plan reflections, verify visibility |
| Stage 37 | Visibility predicts non-divergence (basic) | Cursor in/out of region |

### Expected Visual State

When a projective surface is in the plan, the visibility region shape changes from a radial cone (emanating from a point) to a parallel strip (rays going in the same direction from the lit segment). The region may be narrower or wider depending on obstructions.

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/visibility.gd` | Modify | Add parallel-source casting mode |
| `tests/test_stage49_5_parallel_visibility.gd` | Create | Parallel-source visibility unit tests |

---

## Stage 77: Mixed Planning Chain

### Overview
Implement the full `plan_mixed` algorithm from §13.4, handling plans that contain both transformative and projective effects. The algorithm has three passes: Pass 0 (iterative state simulation), Pass 1 (backward geometry -- walk plan in reverse, partition at projective break points), and Pass 2 (forward origin fill -- solve each transformative sub-chain). This stage reproduces the §16.4 worked example with mirrors and a line normal projection.

**Mid-project checkpoint:** After all effects are integrated, run a reduced sweep (5x5 player × 5x5 cursor = 625 combinations) on the §16.1 two-mirror test level. Check all currently-testable invariants. This catches integration bugs 15 stages before the full sweep at Stage 65.

**Post-cursor frame note:** The post-cursor planned continuation is a physical trace from the cursor in the planned frame (§14.10). Ensure `plan_mixed` correctly passes the accumulated frame from the last planned step to the post-cursor continuation, not the identity frame.

### Prerequisites
Stages 47--49 (all three projective effects implemented).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Algorithm | `plan_mixed` -- full mixed planning algorithm | §13.3, §13.4 |
| Algorithm | Pass 0: iterative state simulation | §13.4 |
| Algorithm | Pass 1: backward geometry -- reverse walk, partition at projective break points | §13.4 |
| Algorithm | Pass 2: forward origin fill -- solve each sub-chain with plan_transformative_subchain | §13.4 |
| Behavior | Transformative entries added to buffer | §13.4 |
| Behavior | Projective entries: save sub-chain, back_propagate, reset buffer | §13.4 |
| Behavior | Terminal entries: clear buffer, set target to terminal hit point | §13.4 |
| Worked example | §16.4 reproduction | §16.4 |

### Unit Tests Added

1. **`test_stage50_mixed_plan_simple`**: Plan=[{M1,left},{P,left},{M2,right}] from §16.4. M1 at x=100 (Reflection), P at x=250 (LineNormalProjection), M2 at x=400 (Reflection). Player at (50,300), cursor at (500,200). Expected: two sub-chains {M1} and {M2}, separated by projective break P. Validates: §13.4.
2. **`test_stage50_backward_pass_partition`**: Same setup. Pass 1 produces: sub-chain 2 = {M2} targeting cursor (500,200); projective P back-propagates (500,200) to (250,200); sub-chain 1 = {M1} targeting (250,200). Validates: §13.4 backward geometry.
3. **`test_stage50_S16_4_bounce_points`**: §16.4 full: sub-chain 2 (M2): image of cursor reflected across x=400 = (300,200). Sub-chain 1 (M1): image of back-propagated point (250,200) reflected across x=100 = (-50,200). Aim from (50,300) toward (-50,200). Bounce on M1 at intersection. Validates: §16.4 bounce computation.
4. **`test_stage50_mixed_plan_all_aligned`**: Run planned and physical traces for §16.4 setup. All steps ALIGNED (divergence_index == null). Validates: plan-physical agreement.
5. **`test_stage50_frame_reset_at_projective`**: After the projective break point, the frame is identity. Each sub-chain starts with identity frame. Validates: §10.7.
6. **`test_stage50_state_convergence`**: Setup where reachability depends on state changes. Iterate until reachable set stabilizes. Verify convergence within 10 iterations. Validates: §13.4 Pass 0.
7. **`test_stage50_projective_unreachable`**: Plan with a projective entry whose back_propagate returns null. Entry is unreachable. Remaining sub-chains computed correctly. Validates: §13.5.
8. **`test_stage50_terminal_in_mixed_plan`**: Plan=[{M1,left},{T,left},{M2,right}] where T is terminal (block). M2 is post-terminal and unreachable. Sub-chain before T targets the terminal hit point. Validates: §13.4 terminal handling.
9. **`test_stage50_empty_plan_mixed`**: Empty plan. plan_mixed returns empty steps. Physical trace fires straight. Validates: §13.6.
10. **`test_stage50_all_transformative_plan`**: Plan with only transformative entries (no projective). plan_mixed produces one sub-chain. Result matches plan_transformative_subchain. Validates: pure transformative case.
11. **`test_stage50_S3_determinism`**: Run plan_mixed twice with identical inputs. Identical step trees. Validates: S3.
12. **`test_stage50_S4_divergence_monotonic`**: If divergence occurs, all subsequent steps are diverged. No re-convergence. Validates: S4.
13. **`test_stage50_consecutive_projective_effects`**: Plan = [{Projective_A, left}, {Projective_B, left}, {Mirror, left}]. Two consecutive projective effects with no transformative between them. Expected: back_propagate on B produces a target, back_propagate on A consumes it. Frame is identity after each projective. The planned trace produces correct bounce points through both projectives and the mirror. Validates: consecutive projective breaks in §13.4.
14. **`test_stage50_reflection_then_inversion_conformal`**: Plan = [{Mirror, left}, {InversionCircle, left}]. Reflection (anti-conformal) composed with inversion (anti-conformal) yields conformal. Expected: the combined frame produces a straight line (not an arc) in the visual frame after both bounces. Validates: §5.2 composition table row 4 in a real trace.
15. **`test_stage50_portal_exit_inside_inversion`**: Portal exits the arrow inside an inversion circle. Arrow hits the inversion surface from the inside (RIGHT side). Expected: side determination is correct, effect resolves to the RIGHT side's config. Validates: inside-out hit side determination with portal + inversion interaction.
16. **`test_stage50_projective_arc_then_transformative_line`**: Plan = [{ArcProjective, left}, {LineMirror, left}]. Projective resets frame to identity. The subsequent line reflection is applied in the identity frame. Expected: correct geometry -- no carryover from the arc frame. Validates: frame reset at projective cleanly transitions between arc and line geometry.
17. **`test_stage50_reduced_sweep_checkpoint`**: Run 625-combination sweep on a test level with all effect types. Check S1-S6, S8-S12, S16-S18, UX3-UX4, UX7, UX9, UX11. Expected: zero violations. Validates: mid-project integration correctness.
18. **`test_stage50_32_limit_post_cursor_continuation`**: Plan with a reflection. After the cursor, the post-cursor continuation (physical trace in planned frame) encounters 32 surfaces (many pass-throughs). Expected: continuation terminates at 32 total hits, truncation marker shown. Validates: §12.6 limit applies to post-cursor planned continuation.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

**RE-VALIDATION REQUIRED:** The plan preview for projective effects was marked incomplete in Stages 47-49. Now that `plan_mixed` is implemented, re-validate all projective interactive tests with plan active:

- [ ] Add a LineNormalProjection surface to plan. Preview shows correct perpendicular exit path. *(Re-validates Stage 73 with complete planner.)*
- [ ] Add a CircleNormalProjection surface to plan. Preview shows correct radial exit path. *(Re-validates Stage 48.)*
- [ ] Add a SemicircleDirectionalProjection surface to plan. Preview shows correct fixed-direction exit. *(Re-validates Stage 49.)*
- [ ] Press Play with the §16.4 setup: two mirrors and a line normal projection.
- [ ] Build plan=[{M1,left},{P,left},{M2,right}]. Preview shows the full mixed path: straight to M1, reflect, straight to P, project perpendicular, straight to M2, reflect, straight to cursor.
- [ ] All segments are solid green (ALIGNED).
- [ ] Fire. Arrow follows the planned path exactly through all three surfaces.
- [ ] Move the cursor to cause divergence. Verify red/yellow divergence indicators.
- [ ] Remove M2 from the plan. Verify the plan updates correctly (now just M1 and P).
- [ ] Plan a terminal surface between two transformative ones. Verify post-terminal entries are unreachable.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S3 | Determinism: same inputs = same step trees | Unit test | Reinforced (mixed plans) |
| S4 | Divergence is monotonic | Unit test | Reinforced (mixed plans) |
| S10 | Projective resets frame at break points | Unit test | Reinforced |
| S5 | Aligned provenance in mixed plan | Unit test | Reinforced |
| S6 | Aligned match in mixed plan | Unit test | Reinforced |
| S16 | No NaN/Inf | Unit test | Inherited |
| UX3 | Preview matches flight for mixed plans | Interactive test | Reinforced |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math | GUT tests |
| Math layer | Stages 40, 45 | Transformative effects (inversion, rigid motion) | GUT tests |
| Math layer | Stages 47--49 | Projective effects (line, circle, semicircle) | GUT tests |
| Visual | Stages 5, 41--42 | Line/arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42, 46--49 | Physical trace with all effect types | GUT tests |
| Planning | Stages 21--27, 43, 46 | Pure transformative planning | GUT tests |
| Planning | Stage 50 | Mixed planning | GUT tests |
| Visibility | Stages 34--38, 44, 46 | Visibility | GUT tests |

### Expected Visual State

Mixed plan preview: path flows through transformative surfaces (with bounces/arcs) and projective surfaces (with sharp direction changes). Each sub-chain segment is correctly colored. Projective break points show the direction change visually. All aligned segments are solid green.

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/planner.gd` | Modify | Implement plan_mixed algorithm (Pass 0, 1, 2) |
| `tests/test_stage50_mixed_planning.gd` | Create | Mixed planning chain tests including §16.4 |

---

## Stage 78: Compound Transformative Effect

### Overview
Implement `CompoundTransformativeEffect`, which stores an ordered array of elementary `TransformativeEffect` instances and precomputes their combined Mobius transform (and its inverse). It IS-A `TransformativeEffect`, implementing `get_mobius()` and `get_inverse_mobius()` to return the precomputed combined matrices. Composition uses the four-case table from §5.2 to handle mixed conformal/anti-conformal combinations correctly.

### Prerequisites
Stage 50 (mixed planning provides the full infrastructure; compound effects are a composition mechanism for transformative effects).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/effects/compound_transformative.gd` -- CompoundTransformativeEffect class | §10.3 |
| Property | `elementary: Array[TransformativeEffect]` -- ordered list of elementary effects | §10.3 |
| Property | `combined_mobius: MobiusTransform` -- precomputed product | §10.3 |
| Property | `combined_inverse: MobiusTransform` -- precomputed inverse product | §10.3 |
| Behavior | IS-A TransformativeEffect (implements get_mobius(), get_inverse_mobius()) | §10.3 |
| Behavior | Composition uses §5.2 four-case table (conformal x conformal, etc.) | §5.2 |
| Examples | reflect+translate = glide reflection; reflect+rotate = rotated reflection | §10.3 |

### Unit Tests Added

1. **`test_stage51_compound_construction`**: Create CompoundTransformativeEffect with [ReflectionEffect, RigidMotionEffect(translate)]. Construction succeeds. get_mobius() returns a valid MobiusTransform. Validates: §10.3.
2. **`test_stage51_compound_is_transformative`**: CompoundTransformativeEffect is a subtype of TransformativeEffect. It can be stored in a SideConfig.effect that expects TransformativeEffect. Validates: §10.3 IS-A relationship.
3. **`test_stage51_glide_reflection`**: reflect across x-axis + translate by (100,0). Apply to z=(5,3). Expected: reflect gives (5,-3), then translate gives (105,-3). Combined Mobius applied once gives (105,-3). Validates: §10.3 combined result.
4. **`test_stage51_rotated_reflection`**: reflect across x-axis + rotate by PI/2. Apply to z=(1,0). Expected: reflect gives (1,0), rotate gives (0,1). Combined Mobius applied once gives (0,1). Validates: §10.3.
5. **`test_stage51_composition_conformal_conformal`**: Two conformal effects (two rigid motions). Composition result is conformal. Validates: §5.2 table row 1.
6. **`test_stage51_composition_conformal_anticonformal`**: Conformal (rigid motion) composed with anti-conformal (reflection). Result is anti-conformal. Validates: §5.2 table row 2.
7. **`test_stage51_composition_anticonformal_conformal`**: Anti-conformal (reflection) composed with conformal (rigid motion). Composition uses conj(M2). Result is anti-conformal. Validates: §5.2 table row 3.
8. **`test_stage51_composition_anticonformal_anticonformal`**: Two anti-conformal (two reflections). Composition uses conj(M2). Result is conformal. Validates: §5.2 table row 4.
9. **`test_stage51_combined_inverse_correct`**: For compound [A, B]: combined_mobius = A.compose(B). combined_inverse = B_inv.compose(A_inv). Apply combined then inverse = identity. Validates: §10.3 inverse precomputation.
10. **`test_stage51_S2_transform_round_trip`**: Apply compound effect to point P via cache, then apply inverse. Result has same Point ID as P. Validates: S2.
11. **`test_stage51_S18_determinant_nonzero`**: Combined Mobius has non-zero determinant. Validates: S18.
12. **`test_stage51_compound_in_trace`**: Surface with left side = CompoundTransformativeEffect. Physical trace hits it. Frame composes with combined Mobius. Origin advances with combined inverse. Works the same as any other transformative. Validates: integration.
13. **`test_stage51_compound_in_planner`**: Plan with compound effect surface. Image chain uses combined inverse. Planned and physical traces align. Validates: planner integration.
14. **`test_stage51_three_element_compound`**: Compound of three effects [reflect, translate, rotate]. Combined result matches sequential application. Validates: multi-element composition.
15. **`test_stage51_compound_effect_renders_visible`**: Surface with a CompoundTransformativeEffect (e.g., Reflection + RigidMotion). Expected: the surface renders with a visible color matching the first elementary effect's type (blue for Reflection). Not invisible, not crash. Validates: compound effects have defined visual rendering.

### Interactive User Tests

- [ ] Press Play with a surface assigned a compound effect (e.g., reflect + translate = glide reflection).
- [ ] Add the surface to the plan. Preview shows the combined effect: the arrow bounces and shifts simultaneously.
- [ ] Fire. Arrow behavior matches the preview.
- [ ] Try a compound of two reflections across different lines. The result is a rotation -- verify the arrow rotates.
- [ ] Try a compound of reflection and rigid motion. Verify correct conformal/anti-conformal behavior.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S2 | Transform round-trip for compound | Unit test | Reinforced (compound) |
| S18 | Frame determinant non-zero for compound | Unit test | Reinforced (compound) |
| S16 | No NaN/Inf | Unit test | Inherited |
| UX3 | Preview matches flight with compound effects | Interactive test | Reinforced |

### Regression Checklist

| Category | Prior Stage | Behavior | How to Verify |
|----------|-------------|----------|---------------|
| Math layer | Stages 4--10 | Core math, Mobius transforms, composition | GUT tests |
| Math layer | Stages 40, 45 | Elementary transformative effects | GUT tests |
| Math layer | Stages 47--49 | Projective effects | GUT tests |
| Visual | Stages 5, 41--42 | Line/arc rendering | Move mouse |
| Interaction | Stages 2--3 | Player, cursor | Press WASD, move mouse |
| Trace | Stages 11--14, 42, 46--49 | Physical trace with all effects | GUT tests |
| Planning | Stages 21--27, 43, 46, 50 | Pure transformative, mixed planning | GUT tests |
| Visibility | Stages 34--38, 44, 46 | Visibility | GUT tests |

### Expected Visual State

Surface with compound effect behaves as a single transformative surface. Preview and flight show the combined transformation applied in one step. Color follows the first elementary effect's type (or a dedicated compound color if desired -- not prescribed by spec).

### Feedback Loop Protocol

Reference standard protocol (top of document).

### Validation Summary (filled in after implementation)

| Check | Status | Notes |
|-------|--------|-------|
| All unit tests pass | [ ] | |
| All prior regression tests pass | [ ] | |
| User interactive sign-off | [ ] | |
| Failing interactive tests automated | [ ] | |
| Stage complete | [ ] | |

### Files Modified/Created

| File | Action | Purpose |
|------|--------|---------|
| `scripts/math/effects/compound_transformative.gd` | Create | CompoundTransformativeEffect class extending TransformativeEffect |
| `tests/test_stage51_compound_effect.gd` | Create | Compound transformative effect tests |

---

### Effect Interaction Tests (Post-Stage 51)

These tests verify effect combinations beyond those covered by the worked examples. Each test creates a plan with two consecutive surfaces of different effect types and verifies the trace and preview produce correct results.

1. **`test_effects_inversion_then_inversion`**: Two different inversion circles in sequence. Frame composes two anti-conformal → conformal. Visual output is a straight line after both inversions. Validates: §5.2 composition row 4.
2. **`test_effects_rigid_then_inversion`**: RigidMotion followed by CircleInversion. Frame is conformal × anti-conformal = anti-conformal. Path curves after the inversion. Validates: mixed composition.
3. **`test_effects_semicircle_proj_then_inversion`**: SemicircleDirectionalProjection resets frame, then CircleInversion applies. Post-projective path is a line that then curves through the inversion. Validates: projective reset + transformative.
4. **`test_effects_circle_normal_proj_then_rigid`**: CircleNormalProjection (radial exit) followed by RigidMotion. Frame resets at projection, then rigid motion applies. Validates: projective → transformative transition.
5. **`test_effects_compound_then_projective`**: CompoundTransformativeEffect followed by LineNormalProjection. Compound frame is cleared by the projective reset. Validates: compound + projective interaction.

---

## Appendix A: Invariant Introduction Map (All 29 Invariants, Projected Status After All Planned Stages)

> **Note:** Stages 44--51 were repurposed for bug fixes (see Stage Reconciliation table above). Original planned content was renumbered to Stages 70--78 (PLANNED). Entries referencing Stage 73+ reflect projected status after planned stages are implemented, not current status.

| Invariant | Full ID | Introduced | First Testable | Fully Testable | Projected Status |
|-----------|---------|-----------|----------------|----------------|----------------------|
| Carrier <-> via round-trip | S1 | Stage 8 | Stage 8 | Stage 65 | Tested (line + circle) |
| Transform round-trip | S2 | Stage 20 | Stage 20 | Stage 65 | Tested (reflection, inversion, rigid motion, compound) |
| Determinism | S3 | Stage 14 | Stage 14 | Stage 65 | Tested (mixed plans) |
| Divergence monotonic | S4 | Stage 26 | Stage 26 | Stage 65 | Tested (mixed plans) |
| Aligned provenance | S5 | Stage 25 | Stage 25 | Stage 65 | Tested (inversion, rigid motion, mixed) |
| Aligned match | S6 | Stage 25 | Stage 25 | Stage 65 | Tested (inversion, rigid motion, mixed) |
| Per-entry state | S7 | Stage 54+ | Stage 54+ | Stage 65 | Not yet introduced |
| Forward-first ordering | S8 | Stage 11 | Stage 11 | Stage 65 | Tested |
| Exclusion respected | S9 | Stage 16 | Stage 16 | Stage 65 | Tested |
| Projective resets frame | S10 | Stage 73 | Stage 73 | Stage 65 | Tested (line, circle, semicircle projective) |
| Three points on carrier | S11 | Stage 7 | Stage 7 | Stage 65 | Tested |
| Side determination | S12 | Stage 7 | Stage 7 | Stage 65 | Tested |
| Visibility no self-intersect | S13 | Stage 35 | Stage 35 | Stage 65 | Tested (line + circle surfaces) |
| Visibility edges on geometry | S14 | Stage 35 | Stage 35 | Stage 65 | Tested (line + circle surfaces) |
| Visibility non-overlapping | S15 | Stage 35 | Stage 35 | Stage 65 | Tested (line + circle surfaces) |
| No NaN/Inf in output | S16 | Stage 4 | Stage 4 | Stage 65 | Tested (all effects, arcs, traces) |
| Provenance IDs unique | S17 | Stage 8 | Stage 8 | Stage 65 | Tested |
| Frame determinant non-zero | S18 | Stage 20 | Stage 20 | Stage 65 | Tested (reflection, inversion, rigid motion, compound) |
| Trace preserves real state | S19 | Stage 53+ | Stage 53+ | Stage 65 | Not yet introduced |
| Visibility predicts non-div. | UX1 | Stage 37 | Stage 37 | Stage 65 | Tested (line + circle + portal + parallel-source surfaces) |
| Divergence -> outside vis. | UX2 | Stage 37 | Stage 37 | Stage 65 | Tested (line + circle + portal + parallel-source surfaces) |
| Preview matches flight | UX3 | Stage 17 | Stage 17 | Stage 65 | Tested (arcs, portals, projective, mixed, compound) |
| Same shot = same result | UX4 | Stage 17 | Stage 17 | Stage 65 | Tested |
| Undo fully restores | UX5 | Stage 32 | Stage 32 | Stage 65 | Tested |
| All targets reachable | UX6 | Stage 55+ | Stage 55+ | Stage 65 | Not yet introduced |
| Solid path to cursor | UX7 | Stage 5 | Stage 5 | Stage 65 | Tested (mixed plans with arcs) |
| Block stops arrow | UX9 | Stage 13 | Stage 13 | Stage 65 | Tested |
| State changes visible | UX10 | Stage 57+ | Stage 57+ | Stage 65 | Not yet introduced |
| Empty plan = fire straight | UX11 | Stage 5 | Stage 15 | Stage 65 | Tested |

> **Stage 110 note:** the visibility rows here (S13–S15, UX1, UX2) describe the legacy design, which was never built. Stage 110 built the region on the beam solver; they are tested as VIS-SIMPLE, VIS-EDGE-ON-GEOMETRY, VIS-DISJOINT and VISIBILITY-EQUALS-NON-DIVERGENCE (docs/TDD_14_VISIBILITY.md).

---

## Appendix B: Projected Cumulative Test Count (All Stages in This Document)

> **Note:** Stages 44--51 were repurposed for bug fixes. The entries below for stages 44--51 reflect the ORIGINAL planned content (now renumbered to 70--78), not the actual implemented test counts. Actual test counts for the repurposed stages differ. See Stage Reconciliation table above.

| Category | Count |
|----------|-------|
| Unit tests (Stages 1--38, prior) | ~210 |
| Unit tests (Stage 40: circle inversion) | +10 |
| Unit tests (Stage 41: arc rendering) | +10 |
| Unit tests (Stage 42: inversion in trace) | +11 |
| Unit tests (Stage 42.5: arc collision shapes) | +7 |
| Unit tests (Stage 43: inversion in planner) | +9 |
| Unit tests (Stage 44: visibility with circles) | +14 |
| Unit tests (Stage 45: rigid motion effect) | +11 |
| Unit tests (Stage 46: rigid motion integration) | +11 |
| Unit tests (Stage 73: line normal projection) | +9 |
| Unit tests (Stage 48: circle normal projection) | +7 |
| Unit tests (Stage 49: semicircle projection) | +9 |
| Unit tests (Stage 49.5: parallel-source visibility) | +12 |
| Unit tests (Stage 50: mixed planning) | +16 |
| Unit tests (Stage 51: compound effect) | +14 |
| **Total unit tests after Stage 51** | **~360** |
| Interactive test items (Stages 1--38, prior) | ~120 |
| Interactive test items (Stages 40--51) | +60 |
| **Total interactive test items after Stage 51** | **~180** |
| Invariants actively tested | 21 (S1--S6, S8--S16, S17, S18, UX1--UX5, UX7, UX9, UX11) |
| Invariants reinforced this document | S2, S5, S6, S10, S13--S15, S16, S18, UX1--UX3, UX9 |
| Invariants introduced this document | S10 (Stage 73) |
| Invariants not yet introduced | 4 (S7, S19, UX6, UX10) |
