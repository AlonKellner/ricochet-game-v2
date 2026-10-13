# Stage 84: test migration table

Every test that existed before Stage 84 and is no longer in the suite, with what it checked.
The last column names either the test that now covers that intent on the beam solver, or the
reason it was dropped. "Internal to the removed representation" means the test checked
machinery that no longer exists: normalized frames, Möbius coefficients, TrackedTransform
provenance, TransformCache, the legacy intersection module, or Planner internals.

Behavioural tests were ported, not deleted. They run on the beam solver through
`TestTrace` (`scripts/testing/test_trace.gd`), and they are not listed here.

| file | test | intent | covered by / reason |
|---|---|---|---|
| test_tracer | test_trace_ray_convenience | Tracer.trace_ray wrapper returns a path | internal to the removed representation (Tracer.trace_ray API; basic tracing covered by test_tracer.test_ray_hits_wall) |
| test_cursor_on_surface | test_sort_null_segment_before_real_at_same_t | Intersection.projective_sort puts the cursor (null segment) before a real hit at equal t | internal to the removed representation (legacy intersection sort; beam tie rule "cursor first" covered by test_side_hitpoints.test_aim_wins_tie_with_carrier / test_trace_ends_violation_2) |
| test_side_hitpoints | test_blocked_sides_interior | Interior hit blocks both sides (Intersection.endpoint_blocked_sides with which=0) | internal to the removed representation (BeamGeom.blocked_sides is endpoint-only; interior blocking covered by test_side_hitpoints.test_trace_interior_hit_blocks) |
| test_side_hitpoints | test_hitrecord_interior_fully_blocked | Legacy HitRecord from find_nearest: interior hit is fully blocked | internal to the removed representation (legacy intersection; beam TraceHit has no blocked flags; covered by test_trace_interior_hit_blocks, test_stage80_beam_geometry.test_endpoint_blocked_sides_ported) |
| test_side_hitpoints | test_hitrecord_endpoint_partial_blocked | Legacy HitRecord: endpoint hit is on-segment but only partially blocked | internal to the removed representation (covered by test_side_hitpoints.test_blocked_sides_start_endpoint (BeamGeom), test_trace_endpoint_open_continues, test_endpoint_reflection_skipped) |
| test_side_hitpoints | test_hitrecord_off_segment_not_blocked | Legacy HitRecord: off-segment carrier hit is not blocked | internal to the removed representation (covered by test_side_hitpoints.test_blocked_sides_off_segment, test_stage54.test_bug2_physical_off_seg_then_on_seg) |
| test_side_hitpoints | test_repro_first_hit_from_origin | Diagnostic: nearest legacy hit for a ray from (0,0) along the Planner aim | internal to the removed representation (Planner + Intersection.find_all_hits diagnostic; the scene is traced on the beam by test_repro_player_block_at_screen_corner_case1) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_construction | effect is conformal and transformative | internal to the removed representation (Möbius conjugating flag) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_conformal | forward and inverse Möbius are conformal | internal to the removed representation (Möbius) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_coefficients | Möbius coefficients of e^{iθ}z+d | internal to the removed representation (Möbius coefficients) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_apply | T(z)=e^{iθ}z+d on a point | test_stage71_portal_pair_rotated / _inverse_composition (closed form against create_portal_pair) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_pure_translation | translation map | test_stage71_portal_pair_translated |
| test_stage71_rigid_motion | test_stage71_rigid_motion_pure_rotation | rotation map | test_stage71_portal_pair_arc_rotated |
| test_stage71_rigid_motion | test_stage71_rigid_motion_identity_case | θ=0,d=0 is identity | internal to the removed representation (Möbius apply) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_inverse_coefficients | inverse Möbius coefficients | internal to the removed representation (Möbius coefficients) |
| test_stage71_rigid_motion | test_stage71_rigid_motion_not_self_inverse | portal map is not self-inverse, inverse round-trips | test_stage71_portal_pair_inverse_composition (target effect undoes source effect) |
| test_stage71_rigid_motion | test_stage71_get_inverse_mobius_correct | get_inverse_mobius returns the inverse, distinct id | internal to the removed representation (get_inverse_mobius / Möbius ids) |
| test_stage71_rigid_motion | test_stage71_portal_pair_tracked_linking | paired TrackedTransforms are mutual inverses | internal to the removed representation (TrackedTransform); pairing via partner() to be tested once it exists |
| test_stage71_rigid_motion | test_stage71_portal_pair_target_provenance | target segment points share provenance with source | internal to the removed representation (Point.same_origin) |
| test_stage71_rigid_motion | test_stage71_portal_pair_provenance_round_trip | exact round trip target->source via TrackedTransform | internal to the removed representation (Point.transformed / TrackedTransform) |
| test_stage71_rigid_motion | test_stage71_S2_transform_round_trip | forward then inverse is identity | test_stage71_portal_pair_inverse_composition, test_stage71_portal_pair_arc_inverse_composition |
| test_stage71_rigid_motion | test_stage71_S18_determinant_nonzero | Möbius determinant non-zero | internal to the removed representation (Möbius determinant) |
| test_stage71_rigid_motion | test_stage71_S16_no_nan_inf | Möbius apply has no NaN/Inf | internal to the removed representation (Möbius apply); trace-level S16 in test_stage72_S16_no_nan_in_trace |
| test_stage72_rigid_motion_integration | test_stage72_planner_image_chain | Planner image of cursor through portal | internal to the removed representation (Planner._compute_image); beam aim covered by test_stage72_planner_portal_aligned, test_stage82_portals |
| test_stage72_rigid_motion_integration | test_stage72_planner_bounce_point | Planner subchain bounce at source carrier | internal to the removed representation (Planner.plan_transformative_subchain); covered by test_stage72_S5_aligned_provenance |
| test_stage72_rigid_motion_integration | test_stage72_reflection_portal_formula_evidence | legacy frame-composition formula after reflection+portal | internal to the removed representation (Möbius frame composition); behaviour covered by test_stage72_reflection_then_portal_displacement |
| test_stage72_rigid_motion_integration | test_stage72_double_portal_planner_image | Planner image = cursor - 2d for double push | internal to the removed representation (Planner._compute_image); beam aim checked against cursor-2d in test_stage72_double_portal_trace_uses_aim |
| test_stage72_rigid_motion_integration | test_stage72_double_portal_aim_direction | Planner aim end = cursor - 2d | test_stage72_double_portal_trace_uses_aim, test_stage72_double_portal_direction_only (beam first step along player->cursor-2d) |
| test_stage72_rigid_motion_integration | test_stage72_cache_non_self_inverse_pair | TransformCache bidirectional caching of a non-self-inverse pair | internal to the removed representation (TransformCache) |
| test_portal_arc_shape | test_step2_frame_composition_order | legacy frame composition order I∘T vs T∘I | internal to the removed representation (step.frame / Möbius compose); shape preservation covered by test_step1_arc_radius_changes_after_portal |
| test_portal_arc_shape | test_step3_line_reflection_portal_shape_preserved | Möbius maps_lines_to_arcs of compositions | internal to the removed representation (Möbius maps_lines_to_arcs / compose) |
| test_portal_arc_shape | test_step4_arc_reflection_portal_same_bug | composition-order bug class for arc reflection + portal | internal to the removed representation (Möbius compose) |
| test_portal_refl_gap | test_frame_transitions_at_gaps | diagnostic dump of legacy frame transitions | internal to the removed representation (step.frame, legacy renderer pin) |
| test_portal_refl_gap | test_hitpoint_visual_end_consistency | step.end == frame.apply(hitpoint) | internal to the removed representation (step.frame / normalized hitpoints) |
| test_portal_refl_gap | test_root_cause_frame_discontinuity | diagnostic of legacy frame discontinuity | internal to the removed representation (step.frame) |
| test_portal_refl_gap | test_investigate_violations | diagnostic dump pinned to legacy tracer | internal to the removed representation (use_beam_tracer=false, step.frame) |
| test_stage43_inversion_planner | test_stage43_inversion_image_chain | Planner image of cursor through inversion | internal to the removed representation (Planner._compute_image); map covered by test_stage40_inversion_apply_point |
| test_stage43_inversion_planner | test_stage43_planned_step_count | Planner subchain has exactly 2 steps | internal to the removed representation (Planner.plan_transformative_subchain) |
| test_stage43_inversion_planner | test_stage43_inversion_unreachable | Planner falls back to direct aim when image degenerates | internal to the removed representation (Planner.compute_aim_direction) |
| test_stage43_inversion_planner | test_stage43_double_inversion_plan | Planner image of double inversion = cursor | internal to the removed representation (Planner._compute_image); involution covered by test_stage40_inversion_self_inverse |
| test_stage43_inversion_planner | test_stage43_full_circle_in_planner | Planner image through a full circle has no NaN | internal to the removed representation (Planner._compute_image) |
| test_stage43_inversion_planner | test_stage43_aim_line_two_forward_hits | aim line has 2 forward carrier hits, bounce picks the right one | test_stage43_bounce_point_on_carrier (beam planned bounce; see note on changed expectation) |
| test_stage42_inversion_trace | test_stage42_origin_advance_self_inverse | inverse Möbius fixes points on the circle | test_stage40_inversion_point_on_circle_fixed (BeamEffects.map_point) |
| test_stage42_inversion_trace | test_stage42_transform_all_line_becomes_circle | inversion maps a line to a circle via Möbius | internal to the removed representation (Möbius invert/apply); beam arcs after inversion covered by test_stage42_visual_path_contains_arc |
| test_stage40_circle_inversion | test_stage40_inversion_mobius_coefficients | Möbius coefficients of the inversion | internal to the removed representation (Möbius coefficients) |
| test_stage40_circle_inversion | test_stage40_S18_determinant_nonzero | Möbius determinant non-zero | internal to the removed representation (Möbius determinant) |
| test_arc_reflector_oscillation | test_step2_planned_isolated_cache | planned trace with an isolated cache has no zero-length steps | test_step1_planned_oscillation (cache no longer exists; identical beam trace) |
| test_arc_reflector_oscillation | test_step3_norm_cache_mapping_corruption | identity-frame norm cache maps segments to surfaces | internal to the removed representation (TransformCache.get_normalized) |
| test_inner_arc_direction | test_arc_via_is_circle_center | Inner-hit reflected arc's `via` equals the circle center T(inf) | Legacy via construction (beam `via` is the arc parameter midpoint, still on the arc); arc-through-center intent covered by test_inner_arc_direction.test_inner_arc_curves_through_center / test_inner_arc_passes_through_center |
| test_inner_arc_direction | test_wrapping_arc_via_at_center | Wrapping arc `via` placed at circle center | Legacy via construction; covered by test_inner_arc_direction.test_inner_arc_passes_through_center |
| test_inner_arc_direction | test_physical_ray_does_not_cross_surface | Back-transform arc step by frame.invert, physical chord stays on one side | internal to the removed representation (step.frame) |
| test_inner_arc_direction | test_midpoint_via_produces_correct_side | get_mobius maps inside midpoint outside | internal to the removed representation (Möbius) |
| test_inner_arc_direction | test_inner_physical_midpoint_is_inside_carrier | Physical (back-transformed) midpoint inside circle | internal to the removed representation (step.frame) |
| test_inner_arc_direction | test_conjugation_maps_inside_midpoint_outside | Conjugation frame maps inside midpoint outside | internal to the removed representation (step.frame) |
| test_inner_arc_direction | test_antipodal_via_is_inside_carrier | Flipped-via arc stays inside the surface | internal to the removed representation (legacy frame-based via fix) |
| test_inner_arc_direction | test_wrap_via_equals_t_inf | Wrap via == frame.apply(INF) | internal to the removed representation (step.frame) |
| test_inner_arc_direction | test_inner_wrap_arc_midpoint_backtransforms_to_infinity | Arc midpoint back-transforms to near-infinity | internal to the removed representation (step.frame) |
| test_inversion_bugs | test_step3_self_inversion_precision | Diagnostic print of inversion Möbius on-circle precision | internal to the removed representation (Möbius; assert_true(true) diagnostic) |
| test_inversion_bugs | test_cache_self_inverse_roundtrip_exact | TransformCache.apply_point self-inverse round trip exact | internal to the removed representation (cache) |
| test_inversion_bugs | test_cache_non_carrier_point_roundtrip | TransformCache round trip for off-carrier point | internal to the removed representation (cache) |
| test_inversion_bugs | test_bug2_aim_point_frame_analysis | Diagnostic: post-inversion step vs Möbius aim image | internal to the removed representation (Möbius aim image; diagnostic); inversion hit covered by test_inversion_bugs.test_bug2_reproduce_aim_ignores_surfaces |
| test_inversion_bugs | test_bug3_case3_arc_direction_flip | Diagnostic print of arc winding for two cursors | Diagnostic only (assert_true(true)); no assertion to port |
| test_inversion_bugs | test_step4_plan_matched_after_reflection | Diagnostic: player waypoint after reflection | Diagnostic only (assert_true(true)); no assertion to port |
| test_inversion_normalization | test_phase2_lines_become_circles_after_inversion | Normalized wall carriers become circles under inverse frame | internal to the removed representation (normalized carriers) |
| test_inversion_normalization | test_phase2_correct_carrier_is_circle | Correct via gives circle normalized carrier | internal to the removed representation (normalized carriers) |
| test_inversion_normalization | test_phase2_circle_carriers_find_hits | Normalized circle carriers intersect the ray (Intersection) | internal to the removed representation (normalized carriers / legacy intersection); post-inversion hits covered by test_inversion_normalization.test_phase1_post_inversion_hits_surfaces |
| test_reflection_after_inversion | test_conjugation_identity_composition_order | Right- vs left-compose of Möbius frames | internal to the removed representation (Möbius composition) |
| test_reflection_after_inversion | test_dual_compose_fixes_both_bugs | Dual-compose of TrackedTransform stack | internal to the removed representation (Möbius/TrackedTransform) |
| test_reflection_after_inversion | test_self_inverse_new_origin_frame_independent | Self-inverse new origin independent of frame | internal to the removed representation (frame back-transform) |
| test_stage45_arc_via_pole | test_midpoint_near_pole_produces_extreme_via | frame.apply(midpoint at pole) is extreme | internal to the removed representation (normalized-frame via construction) |
| test_stage45_arc_via_pole | test_near_pole_midpoint_produces_large_arc | Near-pole frame via gives large-radius arc | internal to the removed representation (normalized-frame via construction) |
| test_stage45_arc_via_pole | test_via_on_correct_image_circle | frame.apply of three points lie on one circle | internal to the removed representation (Möbius); arc consistency covered by test_stage45_circle_rehit.test_evidence_arc_step_via_is_on_its_own_carrier |
| test_stage45_circle_rehit | test_circle_segment_fixed_by_own_reflection | Segment.transformed by own reflection returns self | internal to the removed representation (Segment.transformed/TrackedTransform) |
| test_stage45_circle_rehit | test_wall_segment_not_fixed_by_circle_reflection | Segment.transformed of wall is a new segment | internal to the removed representation (Segment.transformed) |
| test_stage45_circle_rehit | test_wall_roundtrip_through_circle_reflection_preserves_carrier | Self-inverse round trip returns same carrier object | internal to the removed representation (provenance cancellation) |
| test_stage45_circle_rehit | test_double_reflection_frame_is_identity | After two circle hits the frame is identity and steps straight | test_stage45_circle_rehit.test_circle_grazing_double_bounce_is_straight_line (asserts frame_id == IDENTITY_FRAME and non-arc after the double hit) and test_full_circle_inner_hit_returns_to_identity |
| test_stage45_circle_rehit | test_diag_inner_hit_carrier_intersections_are_nonpositive | Intersection.intersect_line_with_carrier t<=0 for inner direction | internal to the removed representation (legacy intersection t semantics) |
| test_stage45_circle_rehit | test_diag_outer_hit_carrier_intersections_have_positive | intersect_line_with_carrier has t>0 for outer direction | internal to the removed representation (legacy intersection t semantics) |
| test_stage45_circle_rehit | test_diag_direction_dot_product_distinguishes_sides | Vector dot distinguishes inner/outer (legacy diagnosis) | Diagnostic of the legacy wrap bug (pure vector arithmetic, no solver behaviour); inner-hit behaviour covered by test_stage45_circle_rehit.test_full_circle_inner_hit_returns_to_identity |
| test_stage45_circle_rehit | test_proof_inner_direction_causes_empty_carrier_hits | find_all_hits + tracer self-hit filter leaves no hits | internal to the removed representation (legacy intersection/filter) |
| test_stage45_circle_rehit | test_proof_negated_direction_would_produce_positive_t | find_all_hits with negated direction has t>0 | internal to the removed representation (legacy intersection) |
| test_stage45_circle_rehit | test_proof_is_wrap_true_because_negative_t | Negative-t hit triggers legacy wrap steps | internal to the removed representation (legacy t<0 / wrap) |
| test_stage45_circle_rehit | test_proof_chord_clip_diverges_from_arc_clip | Chord clip of frame-mapped hitpoints is off the carrier | internal to the removed representation (Möbius frame / legacy wrap clipping) |
| test_stage45_circle_rehit | test_proof_three_point_carrier_wrong_when_clip_point_off_circle | Three-point carrier through frame(INF) differs from surface | internal to the removed representation (Möbius frame / legacy wrap clipping) |
| test_stage47_escape_via | test_pole_is_preimage_of_infinity | Möbius pole maps to infinity | internal to the removed representation (Möbius) |
| test_stage47_escape_via | test_pole_equals_circle_center | Möbius pole at circle center | internal to the removed representation (Möbius) |
| test_stage47_escape_via | test_always_normalize_small_entries | Composed Möbius coefficients normalized | internal to the removed representation (Möbius) |
| test_stage48_full_segment | test_is_on_segment_full_circle_all_angles | Intersection.is_on_segment true everywhere on a full circle | internal to the removed representation (legacy intersection); behaviour covered by test_stage48_full_segment.test_full_circle_no_slip_through_sweep |
| test_stage48_full_segment | test_is_on_segment_full_line | is_on_segment true everywhere on a full line | internal to the removed representation (legacy intersection) |
| test_stage48_full_segment | test_is_on_segment_non_full_unchanged | is_on_segment on ordinary segment midpoint | internal to the removed representation (legacy intersection; BeamGeom.on_segment is the replacement) |
| test_stage48_full_segment | test_detect_endpoints_empty_for_full_circle | No endpoint detections on a full segment | internal to the removed representation (legacy intersection); covered by test_stage48_full_segment.test_full_circle_reflects_at_old_gap |
| test_stage48_full_segment | test_at_which_endpoint_zero_for_full | at_which_endpoint == 0 for full segment | internal to the removed representation (legacy intersection); covered by test_stage48_full_segment.test_full_circle_reflects_at_old_gap |
| test_stage48_full_segment | test_transformed_propagates_full | Segment.transformed keeps `full` | internal to the removed representation (Segment.transformed) |
| test_stage48_full_segment | test_transformed_non_full_stays_non_full | Segment.transformed keeps non-full | internal to the removed representation (Segment.transformed) |
| test_provenance | test_self_inverse_inverse_is_self | TrackedTransform self-inverse links to itself | internal to the removed representation |
| test_provenance | test_pair_creates_linked_inverses | TrackedTransform pair links inverses | internal to the removed representation |
| test_provenance | test_identity_is_self_inverse | TrackedTransform identity | internal to the removed representation |
| test_provenance | test_different_self_inverse_are_distinct | distinct TrackedTransform objects | internal to the removed representation |
| test_provenance | test_self_inverse_stores_carrier | TrackedTransform stores carrier | internal to the removed representation |
| test_provenance | test_identity_has_null_carrier | TrackedTransform identity carrier | internal to the removed representation |
| test_provenance | test_pair_has_null_carrier | TrackedTransform pair carrier | internal to the removed representation |
| test_provenance | test_point_at_has_empty_transforms | Point.transforms empty | internal to the removed representation (Point.transforms) |
| test_provenance | test_point_at_frame_is_identity | Point.frame identity | internal to the removed representation (Point.frame) |
| test_provenance | test_transformed_returns_new_point | Point.transformed | internal to the removed representation |
| test_provenance | test_transformed_preserves_original | Point.original through transforms | internal to the removed representation |
| test_provenance | test_transformed_applies_mobius_to_coords | Point.transformed applies Möbius | internal to the removed representation |
| test_provenance | test_transformed_appends_to_sequence | Point transform sequence | internal to the removed representation |
| test_provenance | test_self_inverse_cancels_to_empty | provenance cancellation [R,R]->[] | internal to the removed representation (beam: stack pop on self-inverse re-hit, see test_stage51 single_transform_arc_exit) |
| test_provenance | test_pair_inverse_cancels | provenance cancellation [A,a]->[] | internal to the removed representation |
| test_provenance | test_nested_cancellation | nested cancellation | internal to the removed representation |
| test_provenance | test_no_false_cancellation | no false cancellation | internal to the removed representation |
| test_provenance | test_partial_cancellation | partial cancellation | internal to the removed representation |
| test_provenance | test_empty_sequence_exact_original | bit-exact roundtrip after cancellation | internal to the removed representation |
| test_provenance | test_aggregate_single_equals_mobius | Point.frame aggregation | internal to the removed representation |
| test_provenance | test_aggregate_two_equals_composition | Point.frame aggregation = compose | internal to the removed representation |
| test_provenance | test_same_origin_after_transform | Point.same_origin after transform | internal to the removed representation (same_origin removed; same_position covered by test_provenance.test_different_coords_not_same_position) |
| test_provenance | test_segment_transformed_applies_tracked | Segment.transformed | internal to the removed representation |
| test_provenance | test_segment_transformed_inf_via_preserved | Segment.transformed INF via | internal to the removed representation |
| test_provenance | test_segment_double_self_inverse_exact_roundtrip | Segment.transformed roundtrip | internal to the removed representation |
| test_provenance | test_segment_carrier_fixed_returns_self | Segment.transformed shortcut | internal to the removed representation |
| test_provenance | test_segment_roundtrip_preserves_carrier_object | carrier identity through roundtrip | internal to the removed representation |
| test_provenance | test_segment_pair_roundtrip_preserves_carrier | carrier identity through pair roundtrip | internal to the removed representation |
| test_provenance | test_hit_record_point_is_point_type | Intersection.find_all_hits HitRecord point type | internal to the removed representation (legacy intersection) |
| test_provenance | test_reflection_tracked_is_self_inverse | Effect.get_tracked_transform | internal to the removed representation |
| test_provenance | test_circle_inversion_tracked_is_self_inverse | Effect.get_tracked_transform | internal to the removed representation |
| test_provenance | test_reflection_tracked_has_carrier | tracked carrier | internal to the removed representation |
| test_provenance | test_inversion_tracked_has_carrier | tracked carrier | internal to the removed representation |
| test_provenance | test_different_effects_produce_different_tracked | tracked identity | internal to the removed representation |
| test_provenance | test_reflection_normalized_same_carrier_returns_self | Effect.normalized | internal to the removed representation |
| test_provenance | test_reflection_normalized_different_carrier_returns_new | Effect.normalized | internal to the removed representation |
| test_provenance | test_normalized_self_preserves_tracked_identity | Effect.normalized | internal to the removed representation |
| test_provenance | test_inversion_normalized_same_carrier_returns_self | Effect.normalized | internal to the removed representation |
| test_provenance | test_inversion_normalized_different_carrier_returns_new | Effect.normalized | internal to the removed representation |
| test_provenance | test_normalized_effect_produces_different_tracked | Effect.normalized | internal to the removed representation |
| test_provenance | test_build_normalized_preserves_effect_for_self_inverse | Tracer._build_normalized | internal to the removed representation |
| test_stage51_conjugation_side | test_winding_stable_under_conjugation_circle | winding sign under Segment.transformed | internal to the removed representation |
| test_stage51_conjugation_side | test_winding_stable_under_conjugation_line | winding sign under Segment.transformed | internal to the removed representation |
| test_stage51_conjugation_side | test_winding_identical_for_lines_after_unification | winding value under Segment.transformed | internal to the removed representation |
| test_stage51_conjugation_side | test_source_based_cancellation | TrackedTransform.is_inverse_of reference equality | internal to the removed representation |
| test_stage51_conjugation_side | test_diag_shortcut_triggers_single_transform | Segment.transformed self-inverse shortcut | internal to the removed representation |
| test_stage51_conjugation_side | test_diag_shortcut_fails_after_normalization | normalized-effect carrier mismatch | internal to the removed representation |
| test_stage51_conjugation_side | test_origin_carrier_matches_normalized_segment | Tracer._build_normalized carrier | internal to the removed representation |
| test_stage51_conjugation_side | test_norm_round_trip_accuracy | _build_normalized round-trip error | internal to the removed representation |
| test_stage52_norm_drift | test_line_reflected_through_line_stays_line | GeneralizedCircle.transformed_by | internal to the removed representation |
| test_stage52_norm_drift | test_circle_reflected_through_line_preserves_radius | GeneralizedCircle.transformed_by | internal to the removed representation |
| test_stage52_norm_drift | test_line_reflected_through_circle_becomes_circle | GeneralizedCircle.transformed_by | internal to the removed representation |
| test_stage52_norm_drift | test_transformed_by_identity_is_unchanged | transformed_by identity | internal to the removed representation |
| test_stage52_norm_drift | test_conformal_compose_equals_sequential | Möbius compose vs sequential carrier transform | internal to the removed representation |
| test_stage52_norm_drift | test_normalized_carrier_is_line_after_line_reflections | normalized carrier type drift | internal to the removed representation |
| test_stage52_norm_drift | test_normalized_carrier_preserves_circle_radius | normalized carrier radius drift | internal to the removed representation |
| test_stage52_norm_drift | test_normalized_carrier_through_circle_reflection_may_change_type | normalized carrier type change | internal to the removed representation |
| test_stage52_norm_drift | test_normalized_coords_from_aggregated_frame | normalized coords = frame_inv(original) | internal to the removed representation (visual intent covered by test_stage52_norm_drift.test_visual_endpoints_on_original_carriers) |
| test_stage46_epsilon_removal | test_incremental_norm_preserves_segment_identity | provenance identity under incremental normalization | internal to the removed representation |
| test_stage46_epsilon_removal | test_incremental_norm_preserves_carrier_identity | same | internal to the removed representation |
| test_stage46_epsilon_removal | test_incremental_norm_preserves_effect_identity | same | internal to the removed representation |
| test_stage46_epsilon_removal | test_incremental_norm_preserves_tracked_transform | same | internal to the removed representation |
| test_stage46_epsilon_removal | test_different_carrier_objects_do_not_cancel | is_inverse_of by reference | internal to the removed representation |
| test_stage46_epsilon_removal | test_different_surfaces_same_geometry_do_not_cancel | is_inverse_of by reference | internal to the removed representation |
| test_epsilon_removal | test_projective_sort_exact_zero_is_last | Intersection.projective_sort exact zero | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_projective_sort_near_zero_not_treated_as_zero | projective_sort | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_projective_sort_negative_near_zero_regular | projective_sort | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_endpoint_blocked_both_when_cross_exactly_zero | Intersection.endpoint_blocked_sides exact sign | internal to the removed representation (beam equivalent BeamGeom.blocked_sides, covered by test_stage80_beam_geometry) |
| test_epsilon_removal | test_endpoint_blocked_directional_for_tiny_cross | endpoint_blocked_sides tiny cross | internal to the removed representation (BeamGeom.blocked_sides) |
| test_epsilon_removal | test_identity_maps_lines_to_lines | MobiusTransform.maps_lines_to_arcs | internal to the removed representation |
| test_epsilon_removal | test_tiny_c_still_maps_to_arcs | maps_lines_to_arcs exact zero | internal to the removed representation |
| test_epsilon_removal | test_composed_identities_stay_linear | maps_lines_to_arcs after compose | internal to the removed representation |
| test_epsilon_removal | test_tier1_exact_match_origin_at_start | legacy three-tier endpoint detection | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_tier1_exact_match_origin_at_end | legacy endpoint detection | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_tier2_cross_zero_detects_endpoint | legacy endpoint detection | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_tier3_quadratic_interior_no_endpoint | legacy endpoint detection | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_at_which_endpoint_exact_equality | Intersection.at_which_endpoint | internal to the removed representation (BeamGeom.endpoint_at) |
| test_epsilon_removal | test_near_miss_not_detected | legacy endpoint near miss | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_vertex_coincidence_exact | legacy vertex coincidence | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_dedup_tier1_replaces_tier3 | legacy hit dedup | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_no_epsilon_sort_boundary | anti-epsilon guard for projective_sort | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_no_epsilon_blocked_sides_boundary | anti-epsilon guard for endpoint_blocked_sides | internal to the removed representation (legacy intersection) |
| test_epsilon_removal | test_no_epsilon_maps_lines_boundary | anti-epsilon guard for maps_lines_to_arcs | internal to the removed representation |
| test_intersection | test_on_segment_hit | legacy nearest hit on segment | internal to the removed representation (legacy intersection; BeamGeom.on_segment / beam hit_records) |
| test_intersection | test_off_segment_carrier_hit | legacy off-segment carrier hit | internal to the removed representation (legacy intersection) |
| test_intersection | test_nearest_wins | legacy nearest selection | internal to the removed representation (legacy intersection) |
| test_intersection | test_off_segment_nearer_than_on_segment | legacy nearest selection | internal to the removed representation (legacy intersection) |
| test_intersection | test_origin_hit_returned_as_beyond | legacy "beyond" origin hit | internal to the removed representation (legacy intersection) |
| test_intersection | test_parallel_no_hit | legacy parallel miss | internal to the removed representation (legacy intersection) |
| test_intersection | test_side_determination | legacy side | internal to the removed representation (legacy intersection; BeamGeom.side_left) |
| test_intersection | test_no_segments_returns_null | legacy empty input | internal to the removed representation (legacy intersection) |
| test_intersection | test_beyond_hit_used_as_fallback | legacy beyond fallback | internal to the removed representation (legacy intersection) |
| test_intersection | test_forward_preferred_over_beyond | legacy forward/beyond ordering | internal to the removed representation (legacy intersection) |
| test_intersection | test_is_on_segment_finite_line | Intersection.is_on_segment | internal to the removed representation (BeamGeom.on_segment, test_stage80_beam_geometry) |
| test_intersection | test_is_on_segment_inf | Intersection.is_on_segment INF | internal to the removed representation (BeamGeom.on_segment) |
| test_intersection | test_side_opposite_from_each_direction | legacy analytic side | internal to the removed representation (legacy intersection) |
| test_intersection | test_side_near_tangent | legacy side near tangent | internal to the removed representation (legacy intersection) |
| test_intersection | test_side_consistent_with_determine_side | legacy _determine_side | internal to the removed representation (legacy intersection) |
| test_intersection | test_project_direction_end_initial | project_point_on_ray | internal to the removed representation (legacy intersection) |
| test_intersection | test_project_origin | project_point_on_ray | internal to the removed representation (legacy intersection) |
| test_intersection | test_project_direction_end_after_advance | project_point_on_ray | internal to the removed representation (legacy intersection) |
| test_intersection | test_find_all_hits_single_segment | find_all_hits | internal to the removed representation (legacy intersection) |
| test_intersection | test_find_all_hits_two_segments | find_all_hits | internal to the removed representation (legacy intersection) |
| test_intersection | test_find_all_hits_parallel | find_all_hits | internal to the removed representation (legacy intersection) |
| test_intersection | test_find_all_hits_origin_included | find_all_hits | internal to the removed representation (legacy intersection) |
| test_intersection | test_projective_sort_mixed | projective_sort | internal to the removed representation (legacy intersection) |
| test_intersection | test_projective_sort_zero_last | projective_sort | internal to the removed representation (legacy intersection) |
| test_intersection | test_projective_sort_only_zero | projective_sort | internal to the removed representation (legacy intersection) |
| test_intersection | test_projective_sort_empty | projective_sort | internal to the removed representation (legacy intersection) |
| test_intersection | test_stage_hitpoints_basic | legacy stage hitpoint assembly | internal to the removed representation (legacy intersection) |
| test_intersection | test_stage_hitpoints_origin_always_last | legacy stage hitpoint assembly | internal to the removed representation (legacy intersection) |
| test_intersection | test_stage_hitpoints_projective_order | legacy stage hitpoint assembly | internal to the removed representation (legacy intersection) |
| test_intersection | test_circle_carrier_two_hits | legacy ray/circle hits | internal to the removed representation (legacy intersection) |
| test_intersection | test_circle_carrier_no_intersection | legacy ray/circle miss | internal to the removed representation (legacy intersection) |
| test_intersection | test_circle_carrier_hitpoint_on_carrier | legacy ray/circle hit on carrier | internal to the removed representation (legacy intersection) |
| test_intersection | test_on_segment_arc | Intersection.is_on_segment for arcs | internal to the removed representation (BeamGeom.on_segment) |
| test_intersection | test_circle_carrier_side_determination | legacy circle side | internal to the removed representation (legacy intersection) |
| test_intersection | test_circle_carrier_multiple_segments | legacy multi-segment circle hits | internal to the removed representation (legacy intersection) |
| test_circle_circle_intersection | test_two_circles_two_intersections | Intersection.intersect_circles | internal to the removed representation (intersect_circles had no production caller) |
| test_circle_circle_intersection | test_two_circles_tangent | intersect_circles | internal to the removed representation |
| test_circle_circle_intersection | test_two_circles_separated | intersect_circles | internal to the removed representation |
| test_circle_circle_intersection | test_two_circles_contained | intersect_circles | internal to the removed representation |
| test_circle_circle_intersection | test_two_circles_concentric | intersect_circles | internal to the removed representation |
| test_circle_circle_intersection | test_line_circle_two_intersections | intersect_circles line/circle | internal to the removed representation |
| test_circle_circle_intersection | test_line_circle_tangent | intersect_circles line/circle | internal to the removed representation |
| test_circle_circle_intersection | test_line_circle_no_intersection | intersect_circles line/circle | internal to the removed representation |
| test_circle_circle_intersection | test_circle_line_order_independent | intersect_circles symmetry | internal to the removed representation |
| test_circle_circle_intersection | test_two_lines_intersection | intersect_circles line/line | internal to the removed representation |
| test_circle_circle_intersection | test_two_lines_parallel | intersect_circles parallel | internal to the removed representation |
| test_circle_circle_intersection | test_diagonal_circles | intersect_circles | internal to the removed representation |
| test_circle_circle_intersection | test_equal_radius_symmetric | intersect_circles | internal to the removed representation |
| test_circle_circle_intersection | test_ray_to_line_basic | Intersection.ray_to_line | internal to the removed representation |
| test_circle_circle_intersection | test_ray_to_line_horizontal | ray_to_line | internal to the removed representation |
| test_circle_circle_intersection | test_inversive_pullback_identity_frame | inversive_pullback_intersect | internal to the removed representation |
| test_circle_circle_intersection | test_inversive_pullback_with_inversion | inversive_pullback_intersect | internal to the removed representation |
| test_circle_circle_intersection | test_inversive_pullback_with_two_transforms | inversive_pullback_intersect | internal to the removed representation |
| test_circle_circle_intersection | test_inversive_pullback_miss | inversive_pullback_intersect | internal to the removed representation |
| test_cache | test_compose_cached_same_result | TransformCache compose cache | internal to the removed representation (cache) |
| test_cache | test_compose_cached_different_inputs | compose cache | internal to the removed representation (cache) |
| test_cache | test_invert_cached_same_result | invert cache | internal to the removed representation (cache) |
| test_cache | test_invert_cached_bidirectional | invert cache | internal to the removed representation (cache) |
| test_cache | test_clear_empties_caches | cache clear | internal to the removed representation (cache) |
| test_cache | test_norm_cache_returns_null_initially | normalized surface cache | internal to the removed representation (cache) |
| test_cache | test_norm_cache_stores_and_retrieves | normalized surface cache | internal to the removed representation (cache) |
| test_cache | test_norm_cache_different_frame_ids | normalized surface cache | internal to the removed representation (cache) |
| test_cache | test_shared_cache_same_frame_ids | shared cache gives same frame ids across traces | internal to the removed representation (cache) |
| test_cache | test_apply_point_non_self_inverse_corruption | TransformCache.apply_point | internal to the removed representation (cache) |
| test_cache | test_apply_point_cross_direction_caching | TransformCache.apply_point | internal to the removed representation (cache) |
| test_stage8_cache | test_stage8_S1_carrier_via_roundtrip_line | TransformCache.derive_carrier | internal to the removed representation (cache) |
| test_stage8_cache | test_stage8_S1_carrier_via_roundtrip_circle | TransformCache.derive_carrier | internal to the removed representation (cache) |
| test_stage8_cache | test_stage8_cache_hit_on_second_lookup | carrier cache hit | internal to the removed representation (cache) |
| test_mobius_properties | test_composition_associativity | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_composition_associativity_with_projective | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_identity_composition_left | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_identity_composition_right | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_inverse_composition_is_identity | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_inverse_composition_reverse | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_double_inversion | Möbius algebra | internal to the removed representation (Möbius) |
| test_mobius_properties | test_determinant_product_conformal | Möbius determinant | internal to the removed representation (Möbius) |
| test_mobius_properties | test_determinant_product_mixed | Möbius determinant | internal to the removed representation (Möbius) |
| test_mobius_properties | test_conjugation_composition_table | Möbius conjugation flag | internal to the removed representation (Möbius) |
| test_mobius_properties | test_inversion_preserves_conjugation | Möbius conjugation flag | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_cmul | complex multiply | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_cconj | complex conjugate | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_cdiv | complex divide | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_cmod2 | complex modulus | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_apply_conformal_identity | Möbius apply | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_apply_conformal_rotation | Möbius apply | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_apply_anticonformal | Möbius apply | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_compose_conformal_conformal | Möbius compose | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_compose_conformal_anticonformal | Möbius compose | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_compose_anticonformal_anticonformal | Möbius compose | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_invert_identity | Möbius invert | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_compose_with_inverse | Möbius compose with inverse | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_S18_determinant_nonzero | Möbius determinant | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_transform_id_unique | Möbius id counter | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_identity_id_preserved | IDENTITY_ID | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_stage20a_S16_no_nan | Möbius no NaN | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_apply_infinity_with_nonzero_c | Möbius at infinity | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_apply_infinity_with_nonzero_c_offset_center | Möbius at infinity | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_apply_infinity_with_zero_c | Möbius at infinity | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_apply_to_infinity | Möbius pole to infinity | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_cdiv_zero_denominator | complex divide by zero | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_invert_anticonformal_self_inverse | Möbius invert anticonformal | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_invert_anticonformal_compose_identity | Möbius invert anticonformal | internal to the removed representation (Möbius) |
| test_stage20a_mobius | test_apply_infinity_no_nan | Möbius at infinity no NaN | internal to the removed representation (Möbius) |
| test_stage79_inversion_accumulation | test_exp1_reproduce_failure | diagnostic (pass_test) of 5-inversion chain via Planner/Tracer | internal to the removed representation (legacy diagnostic, no assertions) |
| test_stage79_inversion_accumulation | test_exp2_frame_composition_health | diagnostic of frame composition | internal to the removed representation (legacy diagnostic) |
| test_stage79_inversion_accumulation | test_exp3_aim_precision | diagnostic of aim_in_frame precision | internal to the removed representation (legacy diagnostic) |
| test_stage79_inversion_accumulation | test_exp4_normalized_surface_distortion | diagnostic of normalized surfaces | internal to the removed representation (legacy diagnostic) |
| test_stage79_inversion_accumulation | test_exp5_intersection_audit | diagnostic of inversive_pullback_intersect | internal to the removed representation (legacy diagnostic) |
| test_stage79_inversion_accumulation | test_exp6_pole_analysis | diagnostic of Möbius poles | internal to the removed representation (legacy diagnostic) |
| test_stage78_arc_midpoint_investigation | test_exp1_reproduce_violation | ARC-MIDPOINT investigation (legacy renderer) | internal to the removed representation (legacy diagnostic; check_ARC_MIDPOINT_ALIGNMENT removed) |
| test_stage78_arc_midpoint_investigation | test_exp2_arc_mid_vs_via | arc_mid vs via in normalized frame | internal to the removed representation |
| test_stage78_arc_midpoint_investigation | test_exp3_backtransform_f32_vs_f64 | frame back-transform precision | internal to the removed representation |
| test_stage78_arc_midpoint_investigation | test_exp4_algebraic_proof | Möbius algebraic proof | internal to the removed representation |
| test_stage78_arc_midpoint_investigation | test_exp5_via_on_arc_crosscheck | VIA_ON_ARC cross-check | internal to the removed representation (VIA_ON_ARC half is beam-valid but redundant with the invariant sweeps) |
| test_stage78_arc_midpoint_investigation | test_exp6_statistical_sweep | ARC-MIDPOINT statistics | internal to the removed representation |
| test_stage78_arc_midpoint_investigation | test_fix_arc_midpoint_zero_violations | zero ARC-MIDPOINT-ALIGNMENT violations | internal to the removed representation (check_ARC_MIDPOINT_ALIGNMENT deleted; it reads step.frame) |
| test_nogaps_fix | test_ray_through_pole_detected | InvariantChecker._is_ray_through_pole | internal to the removed representation (frame-based helper deleted) |
| test_nogaps_fix | test_ray_not_through_pole | _is_ray_through_pole | internal to the removed representation |
| test_nogaps_fix | test_circle_no_walls_no_post_inversion_arc_violations | POST-INVERSION-ARC check via step.frame | internal to the removed representation (check_POST_INVERSION_ARC deleted; beam marks arcs via is_arc_step) |
| test_direction_only | test_direction_only_degenerate_aim_point | Planner._compute_image returns null for unknown surface | internal to the removed representation (Planner) |
| test_solver_parity | test_parity_shallow_all_supported_scenes | Every legacy-vs-beam event difference on the scene corpus is one where the beam agrees with the mpmath oracle | internal to the removed representation (legacy Tracer/Planner comparison via SolverParity); beam vs oracle on the same corpus: test_stage81_trace_rules (bench/scenes_corpus) |
