# TDD Document 8: Editor and Polish

**Stages 61--67** | Level editor, test levels (worked examples), invariant sweep testing (160k combos), performance, visual polish

> **Status: PLANNED** — All stages in this document depend on TDD_07 (game systems) being complete. Note: invariant sweep testing (Stage 65) is partially implemented ahead of schedule — `InvariantChecker` and `SweepRunner` exist with 24 invariant checks, but the full 160k-combo per-level sweep described here has not been run.

### Stage Status

| Stage | Topic | Status |
|-------|-------|--------|
| 61 | Basic Level Editor -- Surface Placement | Planned |
| 62 | Level Editor -- Effects, Targets, Spawn | Planned |
| 63 | Level Editor -- Validation and Test Mode | Planned |
| 64 | Test Levels for Worked Examples | Planned |
| 65 | Invariant Sweep Testing | Partially implemented |
| 66 | Performance Optimization | Planned |
| 67 | Visual Polish | Planned |

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

## Stage 61: Basic Level Editor -- Surface Placement

### Overview
Introduce the level editor scene with core surface placement tools. The editor supports click-based creation of line segments (2 clicks: start, end) and arc segments (3 clicks: start, end, via), deletion of surfaces, and drag-based endpoint repositioning. Surface IDs use a monotonically incrementing counter that is persisted in the level file and never reused, ensuring stable references across edits.

### Prerequisites
Stage 60 (full game loop, level loading, menus, save system -- all gameplay systems complete).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Scene | `scenes/editor/editor.tscn` -- main editor scene with viewport, toolbar, and surface canvas | §19.2 |
| Script | `scripts/editor/editor_main.gd` -- editor scene controller, mode management | §19.2 |
| Script | `scripts/editor/surface_placer.gd` -- click-based surface creation (line: 2 clicks, arc: 3 clicks) | §19.2 |
| Script | `scripts/editor/surface_selector.gd` -- select, delete, move/resize surfaces | §19.2 |
| Script | `scripts/editor/editor_surface_renderer.gd` -- renders surfaces in editor using same color-coding as gameplay | §19.2 |
| Behavior | Line surface placement: click start point, click end point (2 clicks) | §19.2 |
| Behavior | Arc surface placement: click start, click end, click via (3 clicks) | §19.2 |
| Behavior | Delete surface: select and press Delete key | §19.2 |
| Behavior | Move/resize: drag endpoints; via stays at absolute position, carrier recomputed; if via no longer valid, editor snaps to nearest valid position | §19.2 |
| Behavior | Surface IDs: monotonically incrementing counter persisted in level file, never reused | §19.2 |

### Unit Tests Added

1. **`test_stage61_line_placement_two_clicks`**: Simulate two clicks at (100, 200) and (300, 200). Expected: a line surface is created with `start == Vector2(100, 200)` and `end == Vector2(300, 200)`, carrier `is_line() == true`. Validates: 2-click line creation.
2. **`test_stage61_arc_placement_three_clicks`**: Simulate three clicks at (100, 200), (300, 200), (200, 100). Expected: an arc surface is created with the three points, carrier `is_line() == false`, and all three points evaluate to ~0 on the carrier. Validates: 3-click arc creation, S11.
3. **`test_stage61_surface_id_monotonic`**: Create 5 surfaces in sequence. Expected: each surface ID is strictly greater than the previous. Validates: monotonic counter.
4. **`test_stage61_surface_id_never_reused`**: Create 3 surfaces (IDs 1, 2, 3), delete surface 2, create a new surface. Expected: new surface ID is 4 (not 2). Validates: IDs never reused.
5. **`test_stage61_delete_surface`**: Create a surface, select it, delete it. Expected: surface is removed from the editor's surface list. Surface count decreases by 1. Validates: deletion works.
6. **`test_stage61_move_endpoint_recomputes_carrier`**: Create a line surface, drag the end point to a new position. Expected: carrier is recomputed from new three-point configuration. Old carrier != new carrier. Validates: move updates geometry.
7. **`test_stage61_arc_via_stays_absolute_on_move`**: Create an arc surface with via at (200, 100). Move the start point. Expected: via position is still (200, 100) in absolute coordinates. Carrier is recomputed. Validates: via stability.
8. **`test_stage61_invalid_via_snaps`**: Create an arc surface. Move start point such that via becomes collinear with start and end. Expected: editor snaps via to the nearest valid (non-collinear) position. Validates: degenerate recovery.
9. **`test_stage61_surface_rendering_matches_gameplay`**: Create a surface with Reflection effect. Expected: rendered color in editor matches the Reflection color used in gameplay. Validates: visual consistency.
10. **`test_stage61_id_counter_persisted`**: Create surfaces (counter reaches 5), save level, reload. Create another surface. Expected: new ID is 6 (counter was persisted). Validates: counter persistence.

### Interactive User Tests

- [ ] Open the editor from the main menu. The editor scene loads with an empty canvas, toolbar visible.
- [ ] Select "Line" tool, click at one point, then click at another. A line surface appears between the two points.
- [ ] Select "Arc" tool, click start, click end, click via. A curved arc surface appears passing through all three points.
- [ ] Click on a surface to select it. Selection is visually indicated (highlight or handles at endpoints).
- [ ] Press Delete with a surface selected. The surface disappears.
- [ ] Drag an endpoint of a line surface. The surface stretches/moves accordingly.
- [ ] Drag an endpoint of an arc surface. The arc updates; the via point remains at its absolute position.
- [ ] Create and delete multiple surfaces. Verify that new surfaces always get higher IDs than any previously used (check via inspector or debug output).
- [ ] Surfaces are rendered with the same effect-based color coding as in gameplay.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S11 | Three points on carrier evaluate to ~0 | Unit test: newly placed arc surfaces | Inherited |
| S16 | No NaN/Inf in output | Unit test: all editor operations produce finite coordinates | Inherited |
| S17 | Provenance IDs unique | Unit test: surface IDs are unique and never reused | Inherited |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 60 | Full game loop: main menu -> level select -> play -> pause -> back | Play through a level |
| Stage 60 | Save/load game progress | Save, quit, reload, verify progress |
| Stage 59 | Camera tracking follows player/arrow | Play a level, observe camera |
| Stage 58 | HUD displays shot count, divergence status | Play a level, observe HUD |
| Stage 55 | Win condition: hit all targets | Complete a level |
| Stage 53 | State changes persist across shots | Multi-shot level |

### Expected Visual State

Editor scene with a dark canvas area and a toolbar along the top or side. Surfaces appear as colored lines/arcs when placed. Selected surfaces show endpoint handles (small squares or circles). The canvas is zoomable/pannable. No player, no cursor, no trajectory preview -- this is the editor, not gameplay.

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `scenes/editor/editor.tscn` | Create | Main editor scene |
| `scripts/editor/editor_main.gd` | Create | Editor scene controller |
| `scripts/editor/surface_placer.gd` | Create | Click-based surface creation |
| `scripts/editor/surface_selector.gd` | Create | Select, delete, move/resize surfaces |
| `scripts/editor/editor_surface_renderer.gd` | Create | Surface rendering in editor |
| `scenes/ui/main_menu.tscn` | Modify | Add "Editor" button to main menu |
| `tests/test_stage61_editor_placement.gd` | Create | Editor surface placement tests |

---

## Stage 62: Level Editor -- Effects, Targets, Spawn

### Overview
Extend the level editor with a properties panel for configuring per-side effects on surfaces, setting target status, placing the spawn point, and editing initial game state flags. Implement serialization to `.tres` (Godot Resource format, human-readable) where the three points (start, end, via) are the source of truth, with carrier coefficients stored as a serialized cache that is validated on load.

### Prerequisites
Stage 61 (basic editor with surface placement/deletion/movement).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Scene | `scenes/editor/properties_panel.tscn` -- side panel for surface properties | §19.2 |
| Script | `scripts/editor/properties_panel.gd` -- per-side effect assignment, target toggle, flag editing | §19.2, §19.3 |
| Script | `scripts/editor/effect_config_panel.gd` -- compound effect sub-panel with ordered list of elementary effects | §19.2 |
| Script | `scripts/editor/spawn_placer.gd` -- click to place spawn point | §19.2 |
| Script | `scripts/editor/level_serializer.gd` -- serialize/deserialize level to/from `.tres` | §19.3 |
| Behavior | Per-side effect assignment: Reflection, CircleInversion, RigidMotion, LineNormalProjection, CircleNormalProjection, SemicircleDirectionalProjection, Block, null (pass-through) | §19.2 |
| Behavior | Interactive flag toggle per side | §19.2 |
| Behavior | Compound effect sub-panel: ordered list of elementary transformative effects | §19.2 |
| Behavior | Rigid motion fields: rotation angle (theta) and displacement vector (dx, dy) | §19.2 |
| Behavior | Target toggle (`is_target`) | §19.2 |
| Behavior | Spawn point placement (click to place) | §19.2 |
| Behavior | Initial game state flags editing (key-value pairs for `GameState.flags`) | §19.2 |
| Behavior | `.tres` serialization: three points as source of truth + per-side effect configs + state key + is_target + player_solid + serialized cache | §19.3 |
| Data | `is_line_by_construction: bool` -- construction provenance flag stored in serialized cache. Line vs arc determined by editor placement, not coordinate comparison. | §31.3 |
| Data | `LevelData.collision_bodies: Array[CollisionBodyData]` serialized to `.tres` — segment floors and rectangle platforms for player-only collision | §19.1 |
| Field | `LevelData.arrow_speed` editable in editor properties panel (default 1600) | §21.2 |

### Unit Tests Added

1. **`test_stage62_assign_reflection_left_side`**: Select a surface, assign Reflection to left side. Expected: `surface.left_effect.type == EffectType.REFLECTION`. Right side remains null. Validates: per-side effect assignment.
2. **`test_stage62_assign_block_right_side`**: Assign Block to right side. Expected: `surface.right_effect.type == EffectType.BLOCK`. Left side unaffected. Validates: independent side assignment.
3. **`test_stage62_all_effect_types_available`**: Verify that the effect dropdown contains all 8 options: Reflection, CircleInversion, RigidMotion, LineNormalProjection, CircleNormalProjection, SemicircleDirectionalProjection, Block, null. Validates: complete effect palette.
4. **`test_stage62_interactive_flag_toggle`**: Toggle interactive flag on left side to false. Expected: `surface.left_interactive == false`. Toggle back to true. Expected: `surface.left_interactive == true`. Validates: interactive flag editing.
5. **`test_stage62_compound_effect_ordering`**: Create compound effect with [Reflection, CircleInversion]. Expected: ordered list matches input order. Reorder to [CircleInversion, Reflection]. Expected: updated. Validates: compound effect sub-panel.
6. **`test_stage62_rigid_motion_parameters`**: Assign RigidMotion to a side, set theta=PI/4, dx=50, dy=-30. Expected: effect stores these exact values. Validates: rigid motion config.
7. **`test_stage62_target_toggle`**: Toggle `is_target` on a surface. Expected: `surface.is_target == true`. Toggle off. Expected: `surface.is_target == false`. Validates: target toggle.
8. **`test_stage62_spawn_placement`**: Click at (400, 300) with spawn tool active. Expected: spawn point is set to `Vector2(400, 300)`. Validates: spawn placement.
9. **`test_stage62_game_state_flags`**: Add flag "wall_broken" with value false to initial state. Expected: `level.initial_flags["wall_broken"] == false`. Validates: flag editing.
10. **`test_stage62_serialize_roundtrip`**: Create a level with 3 surfaces (line, arc, various effects), a target, a spawn, and flags. Save to `.tres`. Load the `.tres`. Expected: all properties match exactly (positions, effects, target, spawn, flags). Validates: serialization fidelity.
11. **`test_stage62_tres_source_of_truth_is_points`**: Save a level, manually edit the cached carrier coefficients in the `.tres` file, reload. Expected: carrier is recomputed from the three points (source of truth), cached values are corrected. Validates: §19.3 source of truth.
12. **`test_stage62_cache_validated_on_load`**: Save a level with valid cache. Reload. Expected: cached carrier coefficients match recomputed values within machine epsilon. Validates: cache validation.
13. **`test_stage62_S1_roundtrip_after_serialize`**: Save and reload a level. Derive carrier from the loaded three points, then derive via from the carrier. Expected: via matches the original within machine epsilon. Validates: S1 through serialization.
14. **`test_stage62_construction_provenance_line`**: Editor places three collinear points. Serialized level stores `is_line_by_construction = true`. On load, carrier type is line regardless of near-collinearity numerical effects. Validates: §31.3.
15. **`test_stage62_construction_provenance_arc`**: Editor places three non-collinear points. `is_line_by_construction = false`. On load, carrier type is circle. Validates: §31.3.
16. **`test_stage62_collision_body_serialization_roundtrip`**: Create a level with collision bodies (segment floor, rectangle platform). Serialize to `.tres`. Deserialize. Expected: collision bodies preserved with correct shapes and positions. Validates: §19.1 CollisionBodyData serialization.
17. **`test_stage62_collision_body_placement`**: Editor click to place a collision body (segment: 2 clicks for endpoints, rectangle: 2 clicks for corners). Expected: collision body appears in scene and in LevelData.collision_bodies. Validates: editor support for collision body placement.
18. **`test_stage62_arrow_speed_editable`**: Set arrow_speed to 400 in editor properties panel. Save level. Reload. Expected: arrow_speed == 400. Validates: §21.2 per-level arrow speed configuration in editor.

### Interactive User Tests

- [ ] Select a surface. A properties panel appears on the right showing left-side and right-side effect dropdowns.
- [ ] Assign Reflection to the left side. The surface color updates to reflect the Reflection color on the left.
- [ ] Assign Block to the right side. The surface shows Block color on the right.
- [ ] Set a side's effect to null (pass-through). The side shows as transparent/dimmed.
- [ ] Toggle interactive flag off for a side. Visual indicator shows non-interactive (e.g., 50% opacity).
- [ ] Create a compound effect by adding multiple elementary effects in order. The sub-panel shows the ordered list.
- [ ] Assign RigidMotion and enter rotation and displacement values. Values are accepted and displayed.
- [ ] Toggle `is_target` on a surface. Target indicator appears on the surface.
- [ ] Select spawn tool, click on the canvas. A spawn point marker appears at the clicked position.
- [ ] Edit initial game state: add a key-value flag pair. The flag appears in the flags list.
- [ ] Save the level (File > Save). A `.tres` file is created.
- [ ] Close the editor, reopen, load the `.tres` file. All surfaces, effects, target, spawn, and flags are restored exactly.
- [ ] In the editor, place a floor collision body (segment) and a platform collision body (rectangle). Save the level. Reload the level. Both collision bodies are present with correct positions.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S1 | Carrier <-> via round-trip exact | Unit test: roundtrip survives serialization | Inherited |
| S11 | Three points on carrier | Unit test: reloaded surfaces still satisfy S11 | Inherited |
| S12 | Side determination consistent | Unit test: reloaded surfaces produce same side results | Inherited |
| S16 | No NaN/Inf in output | Unit test: serialization produces finite values | Inherited |
| S17 | Provenance IDs unique | Unit test: surface IDs survive serialization | Inherited |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 61 | Line placement (2 clicks), arc placement (3 clicks) | Create surfaces in editor |
| Stage 61 | Surface IDs never reused | Delete and recreate, verify IDs |
| Stage 61 | Move/resize surfaces | Drag endpoints |
| Stage 60 | Game plays normally from level files | Play a level |
| Stage 58 | HUD displays correct info | Play a level |
| Stage 55 | Win condition triggers | Complete a level |

### Expected Visual State

Editor with canvas showing placed surfaces. Properties panel on the right side when a surface is selected, displaying two columns (left side / right side) with effect dropdowns, interactive toggles, and target toggle. Spawn point shown as a small icon (e.g., arrow or player silhouette). Flags shown as a key-value list in the properties panel or a separate panel.

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `scenes/editor/properties_panel.tscn` | Create | Properties panel scene |
| `scripts/editor/properties_panel.gd` | Create | Properties panel controller |
| `scripts/editor/effect_config_panel.gd` | Create | Compound effect sub-panel |
| `scripts/editor/spawn_placer.gd` | Create | Spawn point placement |
| `scripts/editor/level_serializer.gd` | Create | Level serialization to/from .tres |
| `scenes/editor/editor.tscn` | Modify | Integrate properties panel and spawn placer |
| `tests/test_stage62_editor_effects.gd` | Create | Effect assignment and serialization tests |

---

## Stage 63: Level Editor -- Validation and Test Mode

### Overview
Add validation checks to the editor that enforce all constraints from §23 on save, with real-time feedback during editing (invalid surfaces highlighted in red). Introduce cache override definitions for cycle testing, and a test mode that runs the full game simulation within the editor for previewing level behavior.

### Prerequisites
Stage 62 (effects, targets, spawn, serialization).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/editor/level_validator.gd` -- validates all §23 constraints on save and in real-time | §23 |
| Script | `scripts/editor/cache_override_editor.gd` -- UI for defining CycleOverride entries | §17.4 |
| Script | `scripts/editor/test_mode.gd` -- embeds full game simulation within editor | §19.2 |
| Behavior | Validation: min segment length > 0 | §23 |
| Behavior | Validation: Reflection carrier must be a line (a=0) | §23 |
| Behavior | Validation: Inversion carrier must be a circle (a!=0) | §23 |
| Behavior | Validation: frame determinant != 0 for all Mobius matrices | §23 |
| Behavior | Validation: pass-through sides must have interactive=false | §23 |
| Behavior | Validation: no epsilon decisions in topology-changing code | §23 |
| Behavior | Real-time validation: invalid surfaces highlighted in red | §19.2 |
| Data | CycleOverride: surface_ids, sides, expected_result, test_positions | §17.4 |
| Behavior | Cache override: computed composition checked against override, discrepancy flags error | §17.4 |
| Behavior | Test mode: play the level within the editor with full simulation (preview, plan, fire) | §19.2 |
| Behavior | Deleting a surface referenced in a cache override shows a warning | §19.2 |

### Unit Tests Added

1. **`test_stage63_validate_zero_length_segment`**: Create a surface with start == end (zero-length). Expected: validation fails with "zero-length segment" error. Validates: §23 min segment length.
2. **`test_stage63_validate_reflection_on_line`**: Assign Reflection to a line surface. Expected: validation passes. Assign Reflection to a circle surface. Expected: validation fails with "Reflection carrier must be a line" error. Validates: §23 carrier constraint.
3. **`test_stage63_validate_inversion_on_circle`**: Assign CircleInversion to a circle surface. Expected: validation passes. Assign CircleInversion to a line surface. Expected: validation fails with "Inversion carrier must be a circle" error. Validates: §23 carrier constraint.
4. **`test_stage63_validate_frame_determinant`**: Create a surface whose Mobius matrix has determinant approaching 0. Expected: validation fails with "frame determinant near zero" error. Validates: §23 determinant check, S18.
5. **`test_stage63_validate_passthrough_not_interactive`**: Set a side to null (pass-through) with interactive=true. Expected: validation fails. Set interactive=false. Expected: validation passes. Validates: §23 pass-through constraint.
6. **`test_stage63_realtime_invalid_highlight`**: Create a valid surface, then move its endpoint to create a zero-length segment. Expected: surface highlight changes to red in real-time (before save). Validates: real-time validation.
7. **`test_stage63_save_blocked_on_invalid`**: Create a level with an invalid surface. Attempt save. Expected: save is blocked with error message listing all validation failures. Validates: save-time validation.
8. **`test_stage63_cache_override_valid`**: Define a CycleOverride with surface_ids [1, 2], sides [LEFT, RIGHT], expected_result = identity. Compute the actual composition. If it matches within tolerance, no error. Validates: §17.4 override definition.
9. **`test_stage63_cache_override_discrepancy`**: Define a CycleOverride with expected_result that does NOT match the actual composition. Expected: validation flags a "cache override discrepancy" error with details. Validates: §17.4 discrepancy detection.
10. **`test_stage63_delete_surface_override_warning`**: Create a CycleOverride referencing surface ID 3. Delete surface 3. Expected: a warning dialog appears mentioning the override reference. Validates: referential integrity.
11. **`test_stage63_test_mode_launches`**: Enter test mode from the editor. Expected: game simulation starts with the current level layout. Player appears at spawn point. Preview line is drawn from player toward cursor. Validates: test mode activation.
12. **`test_stage63_test_mode_full_simulation`**: In test mode, fire an arrow at a reflective surface. Expected: arrow bounces as per game mechanics. Trajectory matches what gameplay would produce. Validates: test mode uses full simulation.
13. **`test_stage63_test_mode_exit`**: Exit test mode. Expected: editor returns to editing state. No changes from test mode are persisted to the level data. Validates: test mode isolation.

### Interactive User Tests

- [ ] Create a zero-length surface (start == end). It is immediately highlighted in red.
- [ ] Assign Reflection to a circle carrier surface. Surface is highlighted in red with a tooltip or error indicator.
- [ ] Assign CircleInversion to a line carrier surface. Surface is highlighted in red.
- [ ] Fix the invalid surface (e.g., change effect to one compatible with carrier). Red highlight disappears in real-time.
- [ ] Attempt to save a level with invalid surfaces. Save is blocked; error list is shown.
- [ ] Fix all errors, save again. Save succeeds.
- [ ] Open cache override panel. Define a cycle with two surfaces and an expected identity result. Validation runs and shows pass/fail.
- [ ] Define a cache override with an intentionally wrong expected result. Validation shows discrepancy error with numeric details.
- [ ] Delete a surface referenced by a cache override. A warning dialog appears.
- [ ] Press "Test" button. The editor enters test mode: player appears at spawn, surfaces are interactive.
- [ ] In test mode, move with WASD, aim with mouse, fire (left click). Arrow travels and bounces correctly.
- [ ] Press Escape or a "Back to Editor" button. Returns to editing mode. No test-mode changes persist.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S1 | Carrier <-> via round-trip exact | Unit test: test mode surfaces maintain round-trip | Inherited |
| S2 | Transform round-trip exact | Unit test: test mode transforms are reversible | Inherited |
| S11 | Three points on carrier | Unit test: validated by editor validation | Inherited |
| S16 | No NaN/Inf in output | Unit test: validation catches degenerate cases | Inherited |
| S18 | Frame determinant non-zero | Unit test: validation enforces non-zero determinant | Inherited |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 62 | Per-side effect assignment | Select surface, assign effects |
| Stage 62 | Serialization roundtrip | Save and reload level |
| Stage 62 | Spawn placement | Place spawn point |
| Stage 61 | Surface placement and deletion | Create and delete surfaces |
| Stage 61 | Surface IDs never reused | Create/delete/create, check IDs |
| Stage 60 | Full game plays normally | Play a level from main menu |

### Expected Visual State

Editor with surfaces on canvas. Invalid surfaces glow red or have red outlines with small error icons. Valid surfaces display in their normal effect colors. A "Test" button in the toolbar. When test mode is active, the editor canvas transforms into a playable area with the player visible at the spawn point, preview lines visible, and surfaces responding to arrow interactions. A clear visual indicator shows "TEST MODE" (e.g., a banner or border color change).

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `scripts/editor/level_validator.gd` | Create | Level validation (§23 constraints) |
| `scripts/editor/cache_override_editor.gd` | Create | CycleOverride definition UI |
| `scripts/editor/test_mode.gd` | Create | Test mode controller (embeds game simulation) |
| `scenes/editor/editor.tscn` | Modify | Add validation indicators, test mode button, cache override panel |
| `scenes/editor/cache_override_panel.tscn` | Create | Cache override editing panel |
| `tests/test_stage63_editor_validation.gd` | Create | Validation and test mode unit tests |

---

## Stage 64: Test Levels for Worked Examples

### Overview
Create four or more test levels that replicate the worked examples from §16. Each level includes hardcoded expected outputs (step coordinates, step types, bounce points) and automated comparison tests that trace the level at specified player/cursor positions and verify the output matches the expected values. This stage transforms the specification's worked examples into executable regression tests.

### Prerequisites
Stage 63 (editor with validation -- levels can be created and validated), Stage 60 (full trace/game system for automated testing).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Resource | `resources/levels/test_s16_1_two_mirror.tres` -- §16.1 simple two-mirror reflection | §16.1 |
| Resource | `resources/levels/test_s16_2_circle_inversion.tres` -- §16.2 circle inversion | §16.2 |
| Resource | `resources/levels/test_s16_3_divergence.tres` -- §16.3 divergence | §16.3 |
| Resource | `resources/levels/test_s16_4_projective.tres` -- §16.4 projective break point | §16.4 |
| Resource | `resources/levels/test_s16_5_double_hit.tres` -- §16.5 same mirror planned twice | §16.5 |
| Resource | `resources/levels/test_s16_6_state_change.tres` -- §16.6 state change (breakable wall + target) | §16.6 |
| Resource | `resources/levels/test_all_effects.tres` -- **7th test level:** contains at least one surface of every effect type (reflection, circle inversion, rigid motion, line-normal projection, circle-normal projection, semicircle-directional projection, block, pass-through, compound transformative, state-conditional with CycleOverride). Includes a solvable puzzle path whose solution requires the arrow to interact with every effect type (not just that every type exists in the level — the solution path must touch reflection, inversion, rigid motion, at least one projective effect, a state-conditional surface, and a compound effect). Included in the Stage 65 sweep. | §16, §17.4 |
| Script | `tests/test_stage64_worked_examples.gd` -- automated comparison tests for all worked examples | §29.4 |
| Data | Expected outputs per test level: step coordinates, step types, bounce points | §16.1--§16.6 |

### Unit Tests Added

1. **`test_stage64_s16_1_two_mirror_reflection`**: Load `test_s16_1_two_mirror.tres`. Surface A: vertical line at x=100 with left=Reflection. Surface B: horizontal line at y=100 with right=Reflection. Target T. Player at (50, 450). Fire toward mirror A. Expected: trace produces Reflection step off A, then Reflection step off B, then hits target T. Step coordinates match §16.1 expected values within epsilon. Validates: §16.1.
2. **`test_stage64_s16_1_step_types`**: Same level. Expected: all steps are `StepType.ALIGNED` (no divergence in simple reflection). Validates: step type assignment.
3. **`test_stage64_s16_2_circle_inversion`**: Load `test_s16_2_circle_inversion.tres`. Surface C: circle r=100 at (200, 200) with left=CircleInversion. Player at (50, 200). Fire toward circle. Expected: trace produces CircleInversion step. Output ray direction inverted through the circle. Step coordinates match §16.2 within epsilon. Validates: §16.2.
4. **`test_stage64_s16_2_carrier_is_circle`**: Same level. Expected: Surface C carrier `is_line() == false`, center == (200, 200), radius == 100. Validates: S11.
5. **`test_stage64_s16_3_divergence`**: Load `test_s16_3_divergence.tres`. Mirror M at x=200 (left=Reflection) + Wall W at x=150 (Block). Player at (50, 300). Plan includes M, but W blocks the path. Expected: divergence is detected. Step type includes `DIVERGED_PHYSICAL`. The physical trace hits W (Block) before reaching M. Validates: §16.3.
6. **`test_stage64_s16_3_block_stops_arrow`**: Same level. Expected: the arrow is stopped by the Block wall W. No steps beyond W. Validates: UX9.
7. **`test_stage64_s16_4_projective_break`**: Load `test_s16_4_projective.tres`. M1 at x=100 (Reflection), P at x=250 (LineNormalProjection), M2 at x=400 (Reflection). Expected: trace shows Reflection off M1, Projection at P (resets frame), Reflection off M2. Frame reset is visible in step data. Validates: §16.4, S10.
8. **`test_stage64_s16_4_frame_reset`**: Same level. Expected: frame after P is the identity frame (projective resets frame). Step at P has `step_type` involving projection. Validates: S10.
9. **`test_stage64_s16_5_double_hit`**: Load `test_s16_5_double_hit.tres`. Same mirror planned twice. Expected: first hit is planned, second hit occurs in a subsequent step (the mirror is hit again). Both hits are visible. Validates: §16.5.
10. **`test_stage64_s16_6_state_change`**: Load `test_s16_6_state_change.tres`. Breakable wall + target behind it. Expected: first shot breaks the wall (state change). Second shot passes through broken wall and hits target. State change is recorded. Validates: §16.6, S19.
11. **`test_stage64_all_levels_load_without_errors`**: Load all 6 test levels. Expected: all load successfully, validation passes for each. Validates: editor serialization correctness.
12. **`test_stage64_S3_determinism_per_level`**: For each test level, run the trace twice with identical inputs. Expected: both runs produce identical step trees. Validates: S3.
13. **`test_stage64_cache_override_affects_trace`**: Create a test level with a 4-mirror cycle where floating-point composition produces a near-identity but not exact identity. Add a CycleOverride declaring the composition = identity. Trace a ray through the cycle. Verify the trace uses the override (exact identity) rather than the computed value. Compare with a trace without the override to confirm different (incorrect) results without it. Validates: §17.4 manual overrides actually matter in gameplay tracing.
14. **`test_stage64_all_effects_level`**: Load `test_all_effects.tres`. Trace through it at several known positions. All effects interact correctly — no NaN, no crash, step tree is valid. Validates: cross-effect interaction in a realistic level with every effect type.
15. **`test_stage64_cache_override_in_planned_trace`**: The all-effects test level has a CycleOverride. Run both planned and physical traces through the cycle. Both should use the override. Validates: §17.4 overrides apply to planned traces, not just physical.

### Interactive User Tests

- [ ] Load `test_s16_1_two_mirror.tres` in the editor's test mode. Fire toward the first mirror. Arrow bounces off both mirrors and hits the target. Level completes.
- [ ] Load `test_s16_2_circle_inversion.tres`. Fire toward the circle. Arrow's path is inverted through the circle. Visual confirms the curved-to-straight (or vice versa) transformation.
- [ ] Load `test_s16_3_divergence.tres`. Plan to hit mirror M, but wall W is in the way. Preview shows divergence (red/orange path segments). Arrow hits the wall and stops.
- [ ] Load `test_s16_4_projective.tres`. Arrow bounces off M1, projects at P, bounces off M2. Frame reset at P is visually apparent (preview color or style change).
- [ ] Load `test_s16_5_double_hit.tres`. Plan the same mirror twice. Arrow bounces off the mirror, travels, and hits the mirror again. Both hits are visible in the preview.
- [ ] Load `test_s16_6_state_change.tres`. First shot hits breakable wall -- wall breaks. Second shot passes through and hits target. Level completes.
- [ ] Run full GUT suite. All worked example tests pass alongside all prior tests.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S1 | Carrier <-> via round-trip exact | Unit test: test levels maintain S1 | Inherited |
| S3 | Determinism | Unit test: same inputs produce identical step trees | Inherited |
| S10 | Projective resets frame | Unit test: §16.4 level verifies frame reset | Inherited |
| S11 | Three points on carrier | Unit test: test level surfaces satisfy S11 | Inherited |
| S16 | No NaN/Inf in output | Unit test: all trace outputs are finite | Inherited |
| S19 | Trace preserves real state | Unit test: §16.6 state change test | Inherited |
| UX9 | Block stops arrow | Unit test: §16.3 block test | Inherited |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 63 | Editor validation catches invalid surfaces | Create invalid surface, verify red highlight |
| Stage 63 | Test mode runs full simulation | Enter test mode, fire arrow |
| Stage 62 | Serialization roundtrip | Save and reload a level |
| Stage 61 | Surface placement | Create surfaces in editor |
| Stage 60 | Full game loop | Play a level from main menu |
| Stage 55 | Win condition | Complete a level |
| Stage 53 | State changes | Play multi-shot level |

### Expected Visual State

No new visual changes to the game itself. In the editor, loading any test level shows the specific surface configuration described in §16. In test mode, the worked example scenarios play out visually: bounces, inversions, divergences, projections, and state changes are all visible and match the spec's descriptions.

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `resources/levels/test_s16_1_two_mirror.tres` | Create | §16.1 two-mirror reflection test level |
| `resources/levels/test_s16_2_circle_inversion.tres` | Create | §16.2 circle inversion test level |
| `resources/levels/test_s16_3_divergence.tres` | Create | §16.3 divergence test level |
| `resources/levels/test_s16_4_projective.tres` | Create | §16.4 projective break point test level |
| `resources/levels/test_s16_5_double_hit.tres` | Create | §16.5 double-hit test level |
| `resources/levels/test_s16_6_state_change.tres` | Create | §16.6 state change test level |
| `resources/levels/test_all_effects.tres` | Create | All-effects test level (every effect type + CycleOverride) |
| `tests/test_stage64_worked_examples.gd` | Create | Automated comparison tests for worked examples |

---

## Stage 65: Invariant Sweep Testing

### Overview
This is the most critical testing stage in the entire project. Implement a grid sweep that tests ALL 29 invariants across 160,000 player-position/cursor-position combinations per test level (20x20 player grid times 20x20 cursor grid), plus custom near-degenerate positions. Every invariant that has been introduced across Stages 1--64 must now be fully and automatically verified at scale. Failures produce detailed reports with full context for debugging.

The sweep includes 7 test levels: the 6 worked-example levels (§16.1-16.6) plus the all-effects test level from Stage 64.

### Prerequisites
Stage 64 (test levels exist with known-good configurations), Stage 60 (all game systems complete -- all 29 invariants are implementable and testable).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `tests/test_stage65_invariant_sweep.gd` -- grid sweep test harness | §29.3 |
| Script | `scripts/testing/invariant_checker.gd` -- checks all 29 invariants for a given (player_pos, cursor_pos, plan) configuration | §29.3 |
| Script | `scripts/testing/sweep_reporter.gd` -- generates detailed failure reports | §29.3 |
| Behavior | 20x20 player grid x 20x20 cursor grid = 160,000 combinations per level | §29.3 |
| Behavior | Custom positions: per-level `test_positions` array (surface endpoints, near-degenerate points) used as BOTH additional player AND cursor positions | §29.3 |
| Behavior | All 29 invariants checked at every combination | §29.3 |
| Behavior | Failure report: first failure with full context (player pos, cursor pos, plan, which invariant, actual vs expected) | §29.3 |

### Unit Tests Added

> **Stage 110 note:** the visibility rows here (S13–S15, UX1, UX2) describe the legacy design, which was never built. Stage 110 built the region on the beam solver; they are tested as VIS-SIMPLE, VIS-EDGE-ON-GEOMETRY, VIS-DISJOINT and VISIBILITY-EQUALS-NON-DIVERGENCE (docs/TDD_14_VISIBILITY.md).

1. **`test_stage65_sweep_s16_1_two_mirror`**: Run full 160,000-combination sweep on `test_s16_1_two_mirror.tres`. All 29 invariants checked at every combination. Expected: zero failures. Validates: all invariants hold at scale for simple reflection.
2. **`test_stage65_sweep_s16_2_circle_inversion`**: Full sweep on `test_s16_2_circle_inversion.tres`. Expected: zero failures. Validates: all invariants for inversion geometry.
3. **`test_stage65_sweep_s16_3_divergence`**: Full sweep on `test_s16_3_divergence.tres`. Expected: zero failures. Validates: all invariants including divergence-related (S4, UX1, UX2).
4. **`test_stage65_sweep_s16_4_projective`**: Full sweep on `test_s16_4_projective.tres`. Expected: zero failures. Validates: all invariants including projective (S10, S18).
5. **`test_stage65_sweep_s16_5_double_hit`**: Full sweep on `test_s16_5_double_hit.tres`. Expected: zero failures. Validates: all invariants including double-hit scenarios.
6. **`test_stage65_sweep_s16_6_state_change`**: Full sweep on `test_s16_6_state_change.tres`. Expected: zero failures. Validates: all invariants including state (S7, S19, UX10).
7. **`test_stage65_custom_positions_included`**: Verify that per-level `test_positions` are tested as both player and cursor positions, in addition to the 20x20 grid. Expected: total combinations > 160,000 when custom positions are present. Validates: custom position coverage.
8. **`test_stage65_failure_report_format`**: Inject a known invariant violation (e.g., NaN in output). Expected: failure report includes player_pos, cursor_pos, plan, invariant ID ("S16"), actual value ("NaN"), expected condition ("finite"). Validates: report quality.
9. **`test_stage65_UX1_cursor_in_visibility`**: Across all sweep positions where cursor is inside the visibility region: verify no divergence occurs in the trace. Expected: zero violations. Validates: UX1 at scale.
10. **`test_stage65_UX2_divergence_outside_visibility`**: Across all sweep positions where divergence occurs: verify cursor is outside the visibility region. Expected: zero violations. Validates: UX2 at scale.
11. **`test_stage65_UX3_preview_matches_flight`**: For a sample of 1,000 sweep positions with non-red preview: fire the arrow and compare flight path to preview. Expected: identical paths. Validates: UX3 at scale.
12. **`test_stage65_UX4_determinism`**: For a sample of 1,000 sweep positions: run the same trace twice. Expected: identical step trees both times. Validates: UX4 at scale.
13. **`test_stage65_UX5_undo_restores`**: For the state-change test level, fire a shot, undo. Expected: full state restoration (player position, game state flags, targets). Validates: UX5.
14. **`test_stage65_UX6_targets_reachable`**: For each test level with targets: verify there exists at least one (player_pos, cursor_pos) in the sweep that hits all targets (possibly across multiple shots). Validates: UX6.
15. **`test_stage65_UX7_solid_path`**: Across all sweep positions: if a preview is generated, it starts as a solid segment from the player toward the cursor. Expected: zero violations. Validates: UX7.
16. **`test_stage65_UX11_empty_plan_straight`**: Across all sweep positions with empty plan: fire direction is a straight line from player toward cursor. Expected: zero violations. Validates: UX11.
17. **`test_stage65_S16_no_nan_inf`**: Across ALL 160,000+ combinations: no NaN or Inf values appear anywhere in the step tree, visibility regions, or preview data. Expected: zero violations. Validates: S16 at exhaustive scale.
18. **`test_stage65_S18_determinant_nonzero`**: Across all sweep positions: every Mobius frame matrix encountered during tracing has non-zero determinant. Expected: zero violations. Validates: S18 at scale.
19. **`test_stage65_UX1_UX2_varying_plans`**: For each test level and a subset of (player, cursor) pairs (100 pairs): test with the empty plan AND with each single-surface plan (one entry per interactive surface side). UX1 and UX2 must hold for ALL plans, not just the fixed plan. Validates: UX1/UX2 across plan space.
20. **`test_stage65_fuzz_optional`**: Generate 1000 random (player, cursor) pairs within level bounds. Check all 29 invariants for each. Expected: zero violations. *(Optional for CI, required for release.)*

> **Note:** This test extends the fixed-plan sweep to cover the combinatorial plan space. While exhaustive coverage of all possible plans is computationally prohibitive, testing the empty plan plus all single-entry plans provides good coverage of plan-dependent UX invariants.

**Edge case note:** If a level has many pass-through surfaces, the physical trace may hit the 32-hit limit while the planned trace (which skips pass-throughs in image chains) has few steps. This causes divergence due to the hit limit, not geometry. The sweep should verify that this divergence correctly triggers UX2 (divergence → outside visibility) rather than producing an inconsistent state.

**Performance budget:** The full sweep (20x20 x 20x20 = 160K combinations per level) targets completion in under 10 minutes per level on a mid-range CPU. For CI/quick verification, a reduced sweep (5x5 x 5x5 = 625 combinations) is acceptable. The test infrastructure should support a `--quick-sweep` flag that uses the reduced grid. Full sweeps are required for release verification only.

**Test position strategy:** In addition to the 20x20 grid and per-level `test_positions`, add epsilon-offset positions near visibility boundaries. For each surface endpoint (x,y), include (x+0.01, y) and (x-0.01, y) as test positions. These catch narrow boundary transitions the grid spacing might miss.

**Optional fuzz test mode:** In addition to the grid sweep, an optional fuzz test generates random player/cursor positions within level bounds and checks all invariants. Run for a configurable time limit (e.g., 60 seconds). Enable with `--fuzz` flag. Low-effort, high-value for finding edge cases the regular grid misses.

### Interactive User Tests

- [ ] Run the full sweep test suite from GUT. (Note: this will take significant time -- potentially minutes.) GUT reports pass/fail per test level.
- [ ] If any failures occur, examine the failure report output. Verify it contains: player position, cursor position, plan state, which invariant failed, actual vs expected values.
- [ ] Verify that the sweep covers the full 20x20 x 20x20 grid (check test output for "160,000 combinations tested" or similar).
- [ ] Verify custom positions are included (test output mentions additional positions beyond the grid).
- [ ] All 29 invariants show as tested in the sweep report summary.

### Invariants That Must Hold

This stage validates ALL 29 invariants at scale. Every invariant transitions to "fully testable" status.

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| S1 | Carrier <-> via round-trip exact | Sweep: checked at every combination | Fully testable |
| S2 | Transform round-trip exact | Sweep: checked at every combination | Fully testable |
| S3 | Determinism | Sweep: same inputs produce identical trees | Fully testable |
| S4 | Divergence monotonic | Sweep: divergence never decreases along step chain | Fully testable |
| S5 | Aligned provenance | Sweep: aligned steps share provenance chain | Fully testable |
| S6 | Aligned match (surface ID, side, frame ID) | Sweep: aligned steps match on all three fields | Fully testable |
| S7 | Per-entry state matches | Sweep: state at each entry is consistent | Fully testable |
| S8 | Forward-first ordering | Sweep: hits are in forward distance order | Fully testable |
| S9 | Exclusion respected | Sweep: excluded surfaces are not re-hit | Fully testable |
| S10 | Projective resets frame | Sweep: frame is identity after projection | Fully testable |
| S11 | Three points on carrier | Sweep: all surface points evaluate to ~0 | Fully testable |
| S12 | Side determination consistent | Sweep: side is stable for same point/surface | Fully testable |
| S13 | Visibility no self-intersection | Sweep: visibility polygon edges do not cross | Fully testable |
| S14 | Visibility edges on geometry | Sweep: visibility edges lie on surface geometry or view rays | Fully testable |
| S15 | Visibility non-overlapping | Sweep: visibility regions do not overlap | Fully testable |
| S16 | No NaN/Inf in output | Sweep: all outputs are finite | Fully testable |
| S17 | Provenance IDs unique | Sweep: all point IDs in trace are unique | Fully testable |
| S18 | Frame determinant non-zero | Sweep: all Mobius matrices have det != 0 | Fully testable |
| S19 | Trace preserves real state | Sweep: game state after trace matches expected | Fully testable |
| UX1 | Cursor in visibility -> no divergence | Sweep: tested at every combination | Fully testable |
| UX2 | Divergence -> cursor outside visibility | Sweep: tested at every combination | Fully testable |
| UX3 | Non-red preview matches arrow flight | Sweep: sampled comparisons | Fully testable |
| UX4 | Same shot twice -> same result | Sweep: duplicate runs compared | Fully testable |
| UX5 | Undo fully restores | Sweep: state-change level undo tested | Fully testable |
| UX6 | All targets reachable | Sweep: at least one valid path exists per level | Fully testable |
| UX7 | Solid path from player toward cursor | Sweep: first segment is solid toward cursor | Fully testable |
| UX9 | Block stops arrow | Sweep: block surfaces terminate trace | Fully testable |
| UX10 | State changes visible during flight | Sweep: state-change level verified | Fully testable |
| UX11 | Empty plan = fire straight | Sweep: empty plan produces straight line | Fully testable |

> **Stage 110 note:** the visibility rows here (S13–S15, UX1, UX2) describe the legacy design, which was never built. Stage 110 built the region on the beam solver; they are tested as VIS-SIMPLE, VIS-EDGE-ON-GEOMETRY, VIS-DISJOINT and VISIBILITY-EQUALS-NON-DIVERGENCE (docs/TDD_14_VISIBILITY.md).

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 64 | All worked example tests pass | Run Stage 64 tests |
| Stage 63 | Editor validation works | Create invalid surface, verify rejection |
| Stage 63 | Test mode works | Enter test mode, fire arrow |
| Stage 62 | Serialization roundtrip | Save and reload a level |
| Stage 61 | Editor surface placement | Create surfaces |
| Stage 60 | Full game loop | Play a level |
| Stage 55 | Win condition | Complete a level |

### Expected Visual State

No new visual changes. This stage is purely about automated testing infrastructure. The GUT test runner shows sweep progress and results. Failure reports (if any) are printed to the test output console.

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `tests/test_stage65_invariant_sweep.gd` | Create | Grid sweep test harness (GUT test cases) |
| `scripts/testing/invariant_checker.gd` | Create | Checks all 29 invariants for a given configuration |
| `scripts/testing/sweep_reporter.gd` | Create | Generates detailed failure reports |
| `resources/levels/test_s16_1_two_mirror.tres` | Modify | Add `test_positions` array for custom sweep positions |
| `resources/levels/test_s16_2_circle_inversion.tres` | Modify | Add `test_positions` array |
| `resources/levels/test_s16_3_divergence.tres` | Modify | Add `test_positions` array |
| `resources/levels/test_s16_4_projective.tres` | Modify | Add `test_positions` array |
| `resources/levels/test_s16_5_double_hit.tres` | Modify | Add `test_positions` array |
| `resources/levels/test_s16_6_state_change.tres` | Modify | Add `test_positions` array |
| `resources/levels/test_all_effects.tres` | Modify | Add `test_positions` array |

### Failure Mode Tests

The following tests verify graceful degradation under error conditions:

1. **`test_failure_invariant_violation_release`**: In release mode, trigger an invariant violation (e.g., force a NaN into a point coordinate). Expected: error logged, computation skips the problematic step, no crash. Validates: S31.8 release error handling.

2. **`test_failure_corrupted_tres_graceful`**: Load a `.tres` file with invalid surface data (e.g., zero-length segment). Expected: validation catches it at load, error reported, level not loaded. Validates: S23 constraints + S31.8.

3. **`test_failure_32_limit_in_planned_trace`**: Plan causes the planned trace to accumulate 32 steps (many pass-throughs in sequence). Expected: trace terminates cleanly, truncation marker shown. Validates: S12.6 in planned mode.

4. **`test_failure_degenerate_mobius_determinant`**: Construct a MobiusTransform with near-zero determinant (|ad-bg|^2 near machine epsilon but still positive). Apply it to a point. Expected: result is finite (no division-by-near-zero blowup). Validates: S31 numerical stability.

5. **`test_failure_missing_state_key_no_default`**: CategoricalResolver with state_key not in game_state, and no DEFAULT key in config_table. Expected: falls back to first entry in table. Warning logged in debug builds. Validates: S9.4 missing key behavior.

6. **`test_stage65_invariant_checker_catches_violation`**: Intentionally inject a known invariant violation (e.g., set a Step's frame_id to a value different from the planned trace's frame_id, violating S5). Run the invariant checker on this corrupted step tree. Expected: the checker reports the S5 violation with correct details (which invariant, which step, expected vs actual). Validates: the invariant checker itself is correct — not just that the code doesn't violate invariants.

7. **`test_stage65_checker_catches_S13_violation`**: Inject a self-intersecting visibility region. Expected: checker reports S13 violation.
8. **`test_stage65_checker_catches_UX1_violation`**: Place cursor inside visibility but inject a divergence. Expected: checker reports UX1 violation.
9. **`test_stage65_checker_catches_S4_violation`**: Re-converge after divergence (monotonicity violation). Expected: checker reports S4 violation.
10. **`test_stage65_checker_catches_S7_violation`**: Inject a state mismatch between planner's state_at[i] and physical trace's game state at step i. Expected: checker reports S7 violation.
11. **`test_stage65_checker_catches_S19_violation`**: Modify real GameState during a preview trace (break copy isolation). Expected: checker reports S19 violation.
12. **`test_stage65_checker_catches_UX3_violation`**: Make the physical trace produce a different path than the preview showed. Expected: checker reports UX3 violation.
13. **`test_stage65_checker_catches_S2_violation`**: Break a transform round-trip (apply then inverse returns different Point ID). Expected: checker reports S2 violation.

14. **`test_stage65_debug_assert_fires`**: In debug mode, trigger a condition that should fire an assert (e.g., create a MobiusTransform with zero determinant). Expected: the assert fires and is caught by the test framework. Validates: §31.8 debug-mode crash behavior — asserts actually fire, not just silently pass.

15. **`test_stage65_checkpoint_memory_bounded`**: Fire 100 shots (accumulating 100 checkpoints). Measure memory usage (via Godot's Performance monitor or OS.get_static_memory_usage()). Expected: memory growth is approximately linear in checkpoint count — no unexpected quadratic growth from deep-copy aliasing. *(Sanity check, not a hard limit.)*

---

## Stage 66: Performance Optimization

### Overview
Profile the preview computation pipeline and optimize to meet the target of less than 5ms per preview update on a mid-range CPU. Implement incremental recomputation (reuse unchanged steps when only the cursor moves), frame transform caching, batch math via `PackedFloat64Array`, and spatial indexing for levels with many surfaces. All optimizations must preserve every invariant -- verified by re-running the full sweep from Stage 65.

If the full sweep (Stage 65) exceeds the 10-minute-per-level budget, consider moving the intersection kernel to a GDExtension (C++) module. The math layer's zero-Godot-dependency design makes this migration straightforward (S20.1).

### Prerequisites
Stage 65 (invariant sweep provides the regression baseline for verifying optimizations are correctness-preserving).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Script | `scripts/math/batch_intersection.gd` -- PackedFloat64Array-based batch intersection kernel | §20.1 |
| Script | `scripts/math/spatial_index.gd` -- grid or BVH spatial index for surface lookup | §30.2 |
| Behavior | Incremental recomputation: if only cursor moved, recompute from last unchanged step | §30.3 |
| Behavior | Frame transform caching: avoid recomputing M^-1 for unchanged frames | §30.3 |
| Behavior | PackedFloat64Array for hot loops (intersection kernel: 32 steps x N surfaces) | §20.1 |
| Behavior | Spatial indexing when N > 100 surfaces | §30.2 |
| Behavior | Target: preview computation < 5ms on mid-range CPU | §30.1 |

### Unit Tests Added

1. **`test_stage66_incremental_recompute_same_result`**: Compute a full trace. Move cursor slightly. Compute incremental trace. Expected: results are identical to a fresh full trace at the new cursor position. Validates: incremental correctness.
2. **`test_stage66_incremental_recompute_partial`**: Set up a 5-step trace. Move cursor such that only the last 2 steps change. Expected: incremental recompute only recomputes 2 steps (verified by instrumentation counter). First 3 steps are reused. Validates: §30.3 incremental reuse.
3. **`test_stage66_frame_cache_hit`**: Compute a trace, then recompute with the same plan. Expected: frame transform cache hits for all unchanged frames (verified by cache hit counter). Validates: §30.3 frame caching.
4. **`test_stage66_batch_intersection_matches_scalar`**: Run the batch intersection kernel on 10 surfaces. Compare results to the scalar (one-at-a-time) intersection computation. Expected: results match within machine epsilon for all surfaces. Validates: batch math correctness.
5. **`test_stage66_spatial_index_finds_all`**: Create 200 surfaces. Query spatial index for surfaces near a point. Expected: returns the same set as a brute-force scan (no false negatives). Validates: spatial index correctness.
6. **`test_stage66_spatial_index_no_false_negatives`**: For 1,000 random query points, verify spatial index returns a superset of the brute-force result. Expected: zero missed surfaces. Validates: §30.2 correctness.
7. **`test_stage66_preview_under_5ms`**: Measure preview computation time for test levels with 10 surfaces, averaged over 100 runs. Expected: average < 5ms. Validates: §30.1 performance target. (This test may be skipped on slow CI machines; marked as performance-only.)
8. **`test_stage66_sweep_regression`**: Re-run the full Stage 65 invariant sweep on all test levels AFTER applying all optimizations. Expected: zero failures. Validates: optimizations preserve all 29 invariants.
9. **`test_stage66_incremental_vs_full_sweep`**: For 1,000 sweep positions: compute full trace and incremental trace (with small cursor delta). Expected: identical results for all 1,000 positions. Validates: incremental correctness at scale.
10. **`test_stage66_packed_float64_no_precision_loss`**: Compare PackedFloat64Array batch results against standard float64 scalar results for known edge cases (very large coordinates, very small deltas, near-tangent intersections). Expected: identical within machine epsilon. Validates: no precision degradation.
11. **`test_stage66_numerical_large_coordinates`**: Surfaces and player at coordinates > 10,000 units. All invariants hold. No NaN/Inf. Validates: §31.
12. **`test_stage66_numerical_small_separation`**: Two surfaces separated by < 0.01 units. Intersection correctly distinguishes them. Validates: §31.
13. **`test_stage66_numerical_near_tangent`**: Ray nearly tangent to a circle (discriminant near 0 but positive). Returns valid intersection result. Validates: §31.
14. **`test_stage66_numerical_near_zero_determinant`**: Mobius transform with determinant |ad-bc|^2 near machine epsilon but still positive. Transform applies correctly. Validates: §31, S18.
15. **`test_stage66_numerical_parallel_ray`**: Ray nearly parallel to a surface (within 1e-10). Correctly returns no intersection or distant intersection. Validates: §31.
16. **`test_stage66_stress_200_surfaces`**: Create a scene with 200 surfaces. Compute trace + preview. Expected: completes without timeout, no invariant violations, computation time logged. Validates: §30.2 scalability for large scenes.

### Interactive User Tests

- [ ] Play a level with 10+ surfaces. Move the cursor rapidly. Preview updates smoothly with no visible lag or stutter.
- [ ] Open the Godot profiler (Debugger > Profiler). Observe the preview computation function. Verify it stays under 5ms per frame on average.
- [ ] Play a level with the cursor stationary. Move it slightly. Verify the preview updates are faster than a full recompute (check profiler -- incremental path should be shorter).
- [ ] Create a test level in the editor with 100+ surfaces (stress test). Play in test mode. Verify preview still updates at interactive frame rates (>30fps).
- [ ] Play through all 6 worked example test levels after optimization. Verify behavior is identical to pre-optimization (same bounces, same divergence, same everything).

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| ALL (S1--S19, UX1--UX11) | All 29 invariants | Full Stage 65 sweep re-run after optimization | Inherited (regression) |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 65 | Full invariant sweep passes | Re-run sweep (test_stage66_sweep_regression) |
| Stage 64 | All worked example tests pass | Run Stage 64 tests |
| Stage 63 | Editor validation and test mode | Use editor |
| Stage 62 | Serialization | Save/load level |
| Stage 61 | Surface placement | Create surfaces |
| Stage 60 | Full game loop | Play a level |

### Expected Visual State

No visible changes. The game should look and behave identically to pre-optimization. The only observable difference is performance: smoother preview updates, especially with many surfaces or rapid cursor movement.

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `scripts/math/batch_intersection.gd` | Create | PackedFloat64Array batch intersection kernel |
| `scripts/math/spatial_index.gd` | Create | Grid/BVH spatial index for surface lookup |
| `scripts/math/trace.gd` | Modify | Integrate incremental recomputation |
| `scripts/math/transform_cache.gd` | Modify | Add frame transform caching |
| `scripts/math/intersection.gd` | Modify | Use batch intersection kernel in hot path |
| `tests/test_stage66_performance.gd` | Create | Performance and optimization correctness tests |

---

## Stage 67: Visual Polish

### Overview
Apply all visual polish specified in the design document: surface side indicators with per-side coloring, arrow visuals with tangent-following rotation, target animations (pulse/glow for unhit, dimmer with checkmark for hit), hit event flashes, truncation markers, escape ray rendering, placeholder sounds, line widths, z-ordering, and step tree draw ordering. This is the final stage -- after this, the game is visually complete.

Escape ray rendering for the PREVIEW was introduced in Stage 15. This stage adds the flight-animation-specific behavior: during arrow flight, when the arrow reaches an escape step, it flies to the viewport edge and disappears (§21.3).

### Prerequisites
Stage 66 (performance optimized, all invariants verified -- visual polish must not break correctness).

### What Is Introduced

| Category | Item | Spec Reference |
|----------|------|----------------|
| Behavior | Surface side indicators: 2px offset per side from carrier center, total 5px width (1px center + 2px each side), each side drawn in effect color, non-interactive sides at 50% opacity | §22.3 |
| Behavior | Arrow visual: 40-unit line + triangular head (30-degree angle, ~16 units), single draw call, rotates to follow path tangent (arc tangent = perpendicular to position - arc_center) | §22.1 |
| Behavior | Target animations: unhit targets pulse/glow; hit targets drawn dimmer with checkmark overlay | §22.1 |
| Behavior | Hit event visuals: brief flash at bounce points during arrow flight | §21.2 |
| Behavior | State change surfaces update visually at hit moment | §21.2 |
| Behavior | Truncation marker: small stop icon at final hit point when 32-hit limit is reached | §12.6 |
| Behavior | Escape ray rendering: arrow to viewport edge, then disappears | §21.3 |
| Script | `scripts/visual/visual_escape_segment.gd` -- VisualEscapeSegment: line from start in direction, extending to viewport edge | §12.3 |
| Resource | `resources/sounds/fire.wav` -- placeholder fire sound | §28 |
| Resource | `resources/sounds/bounce.wav` -- placeholder bounce sound | §28 |
| Resource | `resources/sounds/break.wav` -- placeholder break sound | §28 |
| Resource | `resources/sounds/target_hit.wav` -- placeholder target hit sound | §28 |
| Resource | `resources/sounds/level_complete.wav` -- placeholder level complete sound | §28 |
| Script | `scripts/game/sound_manager.gd` -- plays sounds at game events | §28 |
| Behavior | Line widths: surfaces 3px, trajectory/preview 2px | §22.4 |
| Behavior | Z-order: surfaces=0, visibility=10, step tree preview=20, arrow=30, player=40 | §20.4 |
| Behavior | Step tree draw order (back to front): DIVERGED_PLANNED, DIVERGED_POST_PLANNED, ALIGNED, ALIGNED_POST_PLANNED, DIVERGED_PHYSICAL | §20.4 |

### Unit Tests Added

1. **`test_stage67_surface_side_indicator_width`**: Render a surface with two different effects (left=Reflection, right=Block). Expected: total rendered width is 5px (1px center + 2px left + 2px right). Validates: §22.3 width spec.
2. **`test_stage67_surface_side_colors`**: Render a surface with Reflection (left) and Block (right). Expected: left 2px strip is in Reflection color, right 2px strip is in Block color. Validates: §22.3 per-side coloring.
3. **`test_stage67_noninteractive_side_opacity`**: Render a surface with left side non-interactive. Expected: left side is drawn at 50% opacity. Right side at full opacity. Validates: §22.3 opacity rule.
4. **`test_stage67_arrow_dimensions`**: Spawn the arrow visual. Expected: line length is 40 units, head angle is 30 degrees, head length is ~16 units. Validates: §22.1 arrow spec.
5. **`test_stage67_arrow_rotation_on_line`**: Arrow traveling along a horizontal line. Expected: arrow rotation is 0 degrees (pointing right). Arrow traveling vertically: rotation is 90 degrees. Validates: §22.1 tangent following.
6. **`test_stage67_arrow_rotation_on_arc`**: Arrow traveling along a circular arc. At a given position on the arc, expected rotation equals the angle of the tangent vector (perpendicular to position - arc_center). Validates: §22.1 arc tangent.
7. **`test_stage67_target_unhit_pulse`**: Unhit target visual includes a pulsing/glowing animation. Expected: target's modulate or shader parameter oscillates over time. Validates: §22.1 target animation.
8. **`test_stage67_target_hit_dimmer`**: After a target is hit, its visual is drawn at reduced brightness. Expected: modulate alpha or value < 1.0. A checkmark overlay is visible. Validates: §22.1 hit target visual.
9. **`test_stage67_hit_flash`**: During arrow flight, when the arrow hits a surface, a brief flash appears at the hit point. Expected: flash node spawns at hit position with duration < 0.3s. Validates: §21.2.
10. **`test_stage67_state_change_visual_update`**: Arrow hits a breakable wall. Expected: wall visual updates (e.g., disappears or changes appearance) at the exact moment of the hit during flight animation. Validates: §21.2 state change visual.
11. **`test_stage67_truncation_marker`**: Generate a trace that hits the 32-step limit. Expected: a small stop icon is rendered at the final hit point. Validates: §12.6 truncation marker.
12. **`test_stage67_escape_flight_animation`**: During arrow flight animation, when the arrow reaches an escape step (no more surfaces to hit), the arrow flies along the escape direction to the viewport edge and disappears. This tests flight-animation-specific behavior; escape ray preview rendering is covered by Stage 15 (TDD_02). Validates: §21.3.
13. **`test_stage67_line_width_surfaces`**: Surface lines are rendered at 3px width. Expected: draw call uses width=3. Validates: §22.4.
14. **`test_stage67_line_width_preview`**: Trajectory/preview lines are rendered at 2px width. Expected: draw call uses width=2. Validates: §22.4.
15. **`test_stage67_z_order`**: Check z_index values. Expected: surfaces=0, visibility=10, step tree preview=20, arrow=30, player=40. Validates: §20.4.
16. **`test_stage67_step_tree_draw_order`**: Render a step tree with all 5 step types present. Expected: draw order (back to front) is DIVERGED_PLANNED, DIVERGED_POST_PLANNED, ALIGNED, ALIGNED_POST_PLANNED, DIVERGED_PHYSICAL. Validates: §20.4.
17. **`test_stage67_sound_fire`**: Fire an arrow. Expected: `fire.wav` plays. Validates: §28 sound.
18. **`test_stage67_sound_bounce`**: Arrow bounces off a reflective surface. Expected: `bounce.wav` plays at the moment of bounce. Validates: §28 sound.
19. **`test_stage67_sound_break`**: Arrow hits a breakable wall. Expected: `break.wav` plays. Validates: §28 sound.
20. **`test_stage67_sound_target_hit`**: Arrow hits a target. Expected: `target_hit.wav` plays. Validates: §28 sound.
21. **`test_stage67_sound_level_complete`**: All targets hit, level complete. Expected: `level_complete.wav` plays. Validates: §28 sound.
22. **`test_stage67_invariant_sweep_regression`**: Re-run the full Stage 65 invariant sweep. Expected: zero failures. Visual changes must not affect trace/math correctness. Validates: all 29 invariants still hold.

### Interactive User Tests **[BEHAVIORAL -- USER SIGN-OFF REQUIRED]**

- [ ] Play a level. Surfaces show dual-colored side indicators (left color and right color visible as separate strips).
- [ ] Observe a surface with one non-interactive side. That side is visibly dimmer (50% opacity).
- [ ] Fire an arrow. The arrow visual is a line with a triangular head, rotating to follow the path direction.
- [ ] Arrow bounces off a curved surface. Arrow head rotates smoothly to follow the arc tangent.
- [ ] Observe an unhit target. It pulses or glows.
- [ ] Hit a target with an arrow. Target dims and shows a checkmark.
- [ ] Fire an arrow that bounces. At each bounce point, a brief flash is visible.
- [ ] Fire a shot that causes a state change (breaks a wall). The wall visually updates at the moment of impact during the flight animation.
- [ ] Create a scenario with 32+ potential hits (if possible). Observe the truncation marker (stop icon) at the final hit.
- [ ] Fire an arrow that escapes (no surface in path). Arrow flies to the viewport edge and disappears.
- [ ] Listen for sounds: fire (on click), bounce (at each reflection), break (wall break), target_hit (target hit), level_complete (all targets hit).
- [ ] Verify surface line thickness appears heavier (3px) than trajectory lines (2px).
- [ ] Verify layering: surfaces behind visibility regions, visibility behind preview, preview behind arrow, arrow behind player.
- [ ] Fire an arrow where the preview shows multiple step types. Verify the draw order: diverged planned paths are behind aligned paths, which are behind diverged physical paths.

### Invariants That Must Hold

| Invariant ID | Description | How Verified | New This Stage? |
|-------------|-------------|-------------|----------------|
| ALL (S1--S19, UX1--UX11) | All 29 invariants | Full Stage 65 sweep re-run after visual polish | Inherited (regression) |

### Regression Checklist

| Prior Stage | Behavior | How to Verify |
|-------------|----------|---------------|
| Stage 66 | Performance target < 5ms | Profiler check |
| Stage 66 | Incremental recompute correct | Profiler + correctness tests |
| Stage 65 | Full invariant sweep passes | Re-run sweep |
| Stage 64 | All worked example tests pass | Run Stage 64 tests |
| Stage 63 | Editor validation and test mode | Use editor |
| Stage 62 | Serialization roundtrip | Save/load level |
| Stage 61 | Surface placement | Create surfaces |
| Stage 60 | Full game loop | Play a level |
| Stage 55 | Win condition | Complete a level |

### Expected Visual State

The game is now visually complete:
- Surfaces are 3px wide with dual-colored side indicators (2px per side, 1px center line), non-interactive sides at half opacity.
- Arrow is a 40-unit line with a triangular head that rotates to follow path tangent along both straight and curved segments.
- Unhit targets pulse with a gentle glow. Hit targets are dimmed with checkmark overlays.
- Bounce points flash briefly during arrow flight animation.
- State-change surfaces update visually at the moment of impact.
- Escaped arrows render a line to the viewport edge and vanish.
- If truncation occurs, a stop icon marks the final point.
- Z-ordering is correct: surfaces are the bottom layer, visibility on top, then preview, arrow on top of preview, player on top of everything.
- Step tree branches are drawn in correct back-to-front order.
- All game events produce appropriate placeholder sounds.

### Feedback Loop Protocol

See standard protocol at top of document.

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
| `scripts/visual/surface_renderer.gd` | Modify | Add side indicators (2px offset, per-side color, 50% opacity for non-interactive) |
| `scripts/visual/arrow_renderer.gd` | Modify | Arrow visual (40u line + 30-degree head), tangent-following rotation |
| `scripts/visual/target_renderer.gd` | Modify | Pulse/glow for unhit, dimmer + checkmark for hit |
| `scripts/visual/hit_flash.gd` | Create | Brief flash at bounce points during flight |
| `scripts/visual/visual_escape_segment.gd` | Create | Escape ray rendering to viewport edge |
| `scripts/visual/truncation_marker.gd` | Create | Stop icon at 32-hit truncation point |
| `scripts/visual/path_renderer.gd` | Modify | Line widths (2px preview), step tree draw order |
| `scripts/game/sound_manager.gd` | Create | Sound playback for game events |
| `resources/sounds/fire.wav` | Create | Placeholder fire sound |
| `resources/sounds/bounce.wav` | Create | Placeholder bounce sound |
| `resources/sounds/break.wav` | Create | Placeholder break sound |
| `resources/sounds/target_hit.wav` | Create | Placeholder target hit sound |
| `resources/sounds/level_complete.wav` | Create | Placeholder level complete sound |
| `scenes/main.tscn` | Modify | Z-index assignments (surfaces=0, visibility=10, preview=20, arrow=30, player=40) |
| `resources/sounds/` | Create dir | Placeholder sound files directory |
| `tests/test_stage67_visual_polish.gd` | Create | Visual polish unit tests |

---

## Post-Implementation: Visual Regression Testing

**Future Enhancement (§29.5):** Screenshot comparison for known test scenes at known player/cursor positions should be implemented as a post-launch quality gate. Each test level should have a reference screenshot. The test captures a screenshot at the same position and compares pixel-by-pixel with a tolerance threshold. This catches rendering bugs (wrong arc direction, missing segments, incorrect colors) that unit tests cannot detect.

This is not a blocking requirement for initial implementation but should be added before release.

---

## Open Design Decisions (§33)

The following spec features are intentionally deferred per GAME_SPEC.md §33 and have no TDD stages: level progression structure, scoring/par system, art direction beyond wireframe, sound design/audio architecture, advanced editor features (undo/redo, multi-select, snap-to-grid), settings rebinding, multiplayer, mobile/touch input, accessibility, localization, level load transitions, plan undo (Ctrl+Z), gamepad plan construction, moving/animated surfaces, and Godot signal architecture. These are content/product design decisions, not engine requirements.

§3.2 opt-in failure conditions (e.g., a breakable surface that cannot be restored) are a per-level content design feature, not an engine requirement. No TDD stage implements this. When needed, add `LevelData.failure_condition` and corresponding game manager logic.

---

## Appendix A: Invariant Introduction Map (All 30 Invariants)

All 29 invariants reach "fully testable" status by Stage 65 (invariant sweep testing). Stage 66 and Stage 67 re-run the full sweep to confirm no regressions.

| # | Invariant | Full ID | Introduced | First Unit-Tested | First Manually Tested | Fully Sweep-Tested |
|---|-----------|---------|-----------|-------------------|----------------------|-------------------|
| 1 | Carrier <-> via round-trip exact | S1 | Stage 8 | Stage 8 | Stage 8 (GUT) | Stage 65 |
| 2 | Transform round-trip exact | S2 | Stage 20 | Stage 20 | Stage 20 (GUT) | Stage 65 |
| 3 | Determinism | S3 | Stage 14 | Stage 14 | Stage 17 | Stage 65 |
| 4 | Divergence monotonic | S4 | Stage 26 | Stage 26 | Stage 26 | Stage 65 |
| 5 | Aligned provenance | S5 | Stage 25 | Stage 25 | Stage 25 | Stage 65 |
| 6 | Aligned match (surface ID, side, frame ID) | S6 | Stage 25 | Stage 25 | Stage 25 | Stage 65 |
| 7 | Per-entry state matches | S7 | Stage 54 | Stage 54 | Stage 54 | Stage 65 |
| 8 | Forward-first ordering | S8 | Stage 11 | Stage 11 | Stage 11 | Stage 65 |
| 9 | Exclusion respected | S9 | Stage 16 | Stage 16 | Stage 16 | Stage 65 |
| 10 | Projective resets frame | S10 | Stage 73 | Stage 73 | Stage 73 | Stage 65 |
| 11 | Three points on carrier | S11 | Stage 7 | Stage 7 | Stage 7 (GUT) | Stage 65 |
| 12 | Side determination consistent | S12 | Stage 7 | Stage 7 | Stage 7 (GUT) | Stage 65 |
| 13 | Visibility no self-intersection | S13 | Stage 35 | Stage 35 | Stage 37 | Stage 65 |
| 14 | Visibility edges on geometry | S14 | Stage 35 | Stage 35 | Stage 37 | Stage 65 |
| 15 | Visibility non-overlapping | S15 | Stage 35 | Stage 35 | Stage 37 | Stage 65 |
| 16 | No NaN/Inf in output | S16 | Stage 4 | Stage 4 | Stage 5 | Stage 65 |
| 17 | Provenance IDs unique | S17 | Stage 8 | Stage 8 | Stage 8 (GUT) | Stage 65 |
| 18 | Frame determinant non-zero | S18 | Stage 20 | Stage 20 | Stage 20 | Stage 65 |
| 19 | Trace preserves real state | S19 | Stage 53 | Stage 53 | Stage 54 | Stage 65 |
| 20 | Cursor in visibility -> no divergence | UX1 | Stage 37 | Stage 37 | Stage 37 | Stage 65 |
| 21 | Divergence -> cursor outside visibility | UX2 | Stage 37 | Stage 37 | Stage 37 | Stage 65 |
| 22 | Non-red preview matches arrow flight | UX3 | Stage 17 | Stage 17 | Stage 17 | Stage 65 |
| 23 | Same shot twice -> same result | UX4 | Stage 17 | Stage 17 | Stage 17 | Stage 65 |
| 24 | Undo fully restores | UX5 | Stage 32 | Stage 32 | Stage 32 | Stage 65 |
| 25 | All targets reachable | UX6 | Stage 55 | Stage 55 | Stage 55 | Stage 65 |
| 26 | Solid path from player toward cursor | UX7 | Stage 5 | Stage 5 | Stage 5 | Stage 65 |
| 27 | Block stops arrow | UX9 | Stage 13 | Stage 13 | Stage 13 | Stage 65 |
| 28 | State changes visible during flight | UX10 | Stage 57 | Stage 57 | Stage 57 | Stage 65 |
| 29 | Empty plan = fire straight | UX11 | Stage 5 (partial) | Stage 15 | Stage 15 | Stage 65 |

---

## Appendix B: Cumulative Test Count (Final Totals After Stage 67)

| Category | Count |
|----------|-------|
| Unit tests (Stages 1--60, prior documents) | ~480 |
| Unit tests (Stage 61) | 10 |
| Unit tests (Stage 62) | 17 |
| Unit tests (Stage 63) | 13 |
| Unit tests (Stage 64) | 15 |
| Unit tests (Stage 65) | 27 |
| Unit tests (Stage 66) | 16 |
| Unit tests (Stage 67) | 22 |
| **Total unit tests** | **~600** |
| Interactive test items (Stages 1--60) | ~185 |
| Interactive test items (Stages 61--67) | ~62 |
| **Total interactive test items** | **~247** |
| Invariants fully sweep-tested | **29 / 29** |
| Sweep combinations per level | 160,000+ |
| Test levels in sweep | 7 |
| **Total sweep checks** | **~1,120,000+ x 29 invariants = ~32.5M invariant checks** |

---

## Appendix C: Complete Invariant Cross-Reference (All 30 Invariants Across All 67 Stages)

This table provides the definitive cross-reference for every invariant across the entire project. For each invariant, it lists: where it was introduced, in which stages it is actively tested or reinforced, and where it reaches full coverage.

### S1: Carrier <-> via round-trip exact

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 8 | TransformCache with provenance-keyed bidirectional lookup |
| First unit-tested | 8 | `test_stage8_S1_carrier_via_roundtrip` (line + circle) |
| Reinforced | 10, 12, 14, 18, 34 | Every stage that derives carriers tests round-trip |
| Serialization round-trip | 62 | `test_stage62_S1_roundtrip_after_serialize` |
| Fully sweep-tested | 65 | All 160,000+ combos per level |
| Regression-verified | 66, 67 | Full sweep re-run after optimization and visual polish |

### S2: Transform round-trip exact

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 20 | Mobius transform with exact inverse by construction |
| First unit-tested | 20 | Forward/inverse composition tests |
| Reinforced | 34, 47 | Each effect type tests round-trip |
| Fully sweep-tested | 65 | All 160,000+ combos per level |
| Regression-verified | 66, 67 | Full sweep re-run |

### S3: Determinism (same inputs -> identical step trees)

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 14 | First complete trace produces deterministic output |
| First unit-tested | 14 | Duplicate trace comparison |
| Reinforced | 17, 25, 26, 53, 64 | `test_stage64_S3_determinism_per_level` |
| Fully sweep-tested | 65 | `test_stage65_UX4_determinism` (sampled 1,000 positions) |
| Regression-verified | 66, 67 | Full sweep re-run |

### S4: Divergence monotonic

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 26 | Divergence system: divergence never decreases along step chain |
| First unit-tested | 26 | Divergence monotonicity assertion in trace |
| Reinforced | 29, 37, 64 | Divergence test level (§16.3) |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S5: Aligned steps share provenance

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 25 | Step tree alignment: aligned steps trace to same plan entry |
| First unit-tested | 25 | Provenance chain comparison |
| Reinforced | 26, 29, 37 | Expanded with divergence |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S6: Aligned steps match (surface ID, side, frame ID)

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 25 | Aligned planned/physical steps match on surface ID, side, frame ID |
| First unit-tested | 25 | Triple-field comparison |
| Reinforced | 26, 29, 34, 47 | Each new step type validates alignment |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S7: Per-entry state matches

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 54 | State-conditional surfaces: game state at each entry is consistent |
| First unit-tested | 54 | State snapshot comparison at entries |
| Reinforced | 57, 64 | State change test level (§16.6) |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S8: Forward-first hit ordering

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 11 | Intersection sorting: first hit in forward direction |
| First unit-tested | 11 | Multi-surface intersection order test |
| Reinforced | 14, 16, 18, 25, 47 | Every trace validates hit ordering |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S9: Exclusion respected

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 16 | Just-hit surface excluded from next intersection |
| First unit-tested | 16 | Re-hit prevention test |
| Reinforced | 18, 25, 29 | Exclusion management |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S10: Projective resets frame

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 47 | Projective effects (LineNormalProjection, etc.) reset to identity frame |
| First unit-tested | 47 | Frame identity check after projection |
| Reinforced | 64 | `test_stage64_s16_4_frame_reset` |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S11: Three points on carrier evaluate to ~0

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Infrastructure | 6 | GeneralizedCircle.evaluate() |
| Introduced | 7 | Segment derives carrier; three points tested |
| First unit-tested | 7 | `test_stage7_S11_three_points_on_carrier` |
| Reinforced | 8, 10, 12, 14, 61, 62 | Every carrier derivation validates S11 |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S12: Side determination consistent

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 7 | Side enum + cross-product formula |
| First unit-tested | 7 | `test_stage7_S12_side_consistent` |
| Reinforced | 10, 12, 14, 25, 47 | Every effect application validates side |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S13: Visibility polygon has no self-intersection

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 35 | Visibility computation: polygon boundary does not self-cross |
| First unit-tested | 35 | Edge crossing detection |
| First manually tested | 37 | Visual inspection of visibility region |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S14: Visibility edges lie on geometry or view rays

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 35 | Visibility edges constrained to surface geometry or player sight lines |
| First unit-tested | 35 | Edge provenance check |
| First manually tested | 37 | Visual inspection |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S15: Visibility regions non-overlapping

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 35 | Visibility regions for different plan entries do not overlap |
| First unit-tested | 35 | Overlap detection algorithm |
| First manually tested | 37 | Visual inspection |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S16: No NaN/Inf in output

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 4 | Direction/Ray construction validates finite values |
| First unit-tested | 4 | `is_finite()` checks on all outputs |
| Reinforced | 6, 7, 8, 10, 11, 12, 14, 18, 25, 26, 34, 35, 47, 53, 63 | Every math operation checks finiteness |
| Fully sweep-tested | 65 | `test_stage65_S16_no_nan_inf` -- exhaustive check |
| Regression-verified | 66, 67 | Full sweep re-run |

### S17: Provenance IDs unique

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 8 | Point.id global monotonic counter |
| First unit-tested | 8 | `test_stage8_point_id_unique` |
| Reinforced | 14, 25, 53 | Trace point IDs checked |
| Surface IDs | 61 | Surface IDs also monotonic, never reused |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### S18: Frame determinant non-zero

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 20 | Mobius frame matrices must have non-zero determinant |
| First unit-tested | 20 | Determinant check on all constructed frames |
| Reinforced | 34, 47, 63 | Editor validation enforces S18 |
| Fully sweep-tested | 65 | `test_stage65_S18_determinant_nonzero` |
| Regression-verified | 66, 67 | Full sweep re-run |

### S19: Trace preserves real state

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 53 | Game state after trace matches computed expected state |
| First unit-tested | 53 | State snapshot before/after comparison |
| Reinforced | 54, 57, 64 | State change test level (§16.6) |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX1: Cursor in visibility region -> no divergence

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 37 | Visibility region predicts non-divergence |
| First unit-tested | 37 | Point-in-polygon + trace divergence cross-check |
| First manually tested | 37 | Move cursor inside/outside visibility, observe divergence |
| Fully sweep-tested | 65 | `test_stage65_UX1_cursor_in_visibility` |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX2: Divergence -> cursor outside visibility

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 37 | Converse of UX1: divergence implies cursor is outside |
| First unit-tested | 37 | Divergence detection + visibility boundary check |
| First manually tested | 37 | Same as UX1 manual test |
| Fully sweep-tested | 65 | `test_stage65_UX2_divergence_outside_visibility` |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX3: Non-red preview matches arrow flight

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 17 | Preview system: aligned (green) segments must match actual flight |
| First unit-tested | 17 | Compare preview path to simulated flight |
| First manually tested | 17 | Fire arrow, observe path matches preview |
| Fully sweep-tested | 65 | `test_stage65_UX3_preview_matches_flight` (sampled) |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX4: Same shot twice -> same result

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 17 | Determinism from player perspective |
| First unit-tested | 17 | Duplicate fire comparison |
| First manually tested | 17 | Fire same shot twice, observe identical result |
| Fully sweep-tested | 65 | `test_stage65_UX4_determinism` (sampled) |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX5: Undo fully restores

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 32 | Checkpoint system: undo restores all state |
| First unit-tested | 32 | State snapshot before/after undo comparison |
| First manually tested | 32 | Fire, undo, verify player/state restored |
| Fully sweep-tested | 65 | `test_stage65_UX5_undo_restores` |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX6: All targets reachable

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 55 | Level design constraint: win condition is achievable |
| First unit-tested | 55 | Existence check for valid solution |
| First manually tested | 55 | Complete a level |
| Fully sweep-tested | 65 | `test_stage65_UX6_targets_reachable` |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX7: Solid path from player toward cursor

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 5 (partial) | First preview: solid green line from player to cursor |
| First unit-tested | 5 | `test_stage5_preview_exists_when_cursor_differs` |
| Expanded | 15, 17, 25 | Preview solidness extends to planned paths |
| Fully sweep-tested | 65 | `test_stage65_UX7_solid_path` |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX9: Block stops arrow

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 13 | Block effect: arrow terminates on impact |
| First unit-tested | 13 | Arrow trace ends at block surface |
| First manually tested | 13 | Fire at block surface, arrow stops |
| Reinforced | 64 | §16.3 divergence test level includes block |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX10: State changes visible during flight

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 57 | State-change surfaces update visually during arrow flight animation |
| First unit-tested | 57 | Visual state change assertion at hit moment |
| First manually tested | 57 | Fire at breakable wall, observe visual change during flight |
| Reinforced | 64, 67 | §16.6 test level, hit event visuals |
| Fully sweep-tested | 65 | Checked at every combination |
| Regression-verified | 66, 67 | Full sweep re-run |

### UX11: Empty plan = fire straight

| Aspect | Stage(s) | Notes |
|--------|----------|-------|
| Introduced | 5 (partial) | Green line from player to cursor with no plan |
| First full test | 15 | Full trace with empty plan fires straight |
| First unit-tested | 15 | Direction comparison: fire direction == player-to-cursor |
| First manually tested | 15 | Fire with no plan, arrow goes straight toward cursor |
| Fully sweep-tested | 65 | `test_stage65_UX11_empty_plan_straight` |
| Regression-verified | 66, 67 | Full sweep re-run |

---

*End of TDD Document 7. This completes the 7-document TDD specification covering all 67 stages of Ricochet Game v2.*
