extends RefCounted
## Hand-built traces that violate exactly the invariant they are named after
## (plus a clean one), shared by tests/test_invariant_detects.gd (the native
## kernel detects each) and tests/native/test_diff_invariants.gd (the GDScript
## checker and the kernel agree). A case feeds a fake PathRenderer: the
## display paths, the merged typed steps, the ladder result, the surfaces.

const H := preload("res://tests/test_helpers.gd")

## A stand-in for the scene parent holding the surfaces.
class FakeParent extends Node:
	var surfaces: Array = []

## A stand-in for PathRenderer: what the checker reads after _compute_trace().
class FakeRenderer extends Node2D:
	var physical = null
	var planned = null
	var typed: Array = []
	var last_beam_result = null
	var flags: Dictionary = {}
	func _compute_trace() -> void:
		pass
	func get_traced_path():
		return physical
	func get_planned_path():
		return planned
	func get_typed_steps() -> Array:
		return typed
	func has_line() -> bool:
		return typed.size() > 0
	func get_line_from() -> Vector2:
		return typed[0].start if typed.size() > 0 else Vector2.ZERO
	func get_line_direction() -> Vector2:
		if typed.size() > 0:
			return (typed[0].end - typed[0].start).normalized()
		return Vector2.ZERO
	func get_flags() -> Dictionary:
		return flags


## The surfaces of the case being built (hits carry their surface's Segment).
static var _surfaces: Array = []


static func segment_of(surface_id: int):
	for s in _surfaces:
		if s.id == surface_id:
			return s.segment
	return null


static func hit(end: Vector2, surface_id: int, t: float = 10.0, on_segment: bool = true) -> TraceHit:
	var h := TraceHit.make(t, end, segment_of(surface_id), Side.Value.LEFT, on_segment)
	h.surface_id = surface_id
	return h


static func step(a: Vector2, b: Vector2, with_hit: bool, surface_id: int = 1, frame_id: int = 0) -> TraceStep:
	var s := TraceStep.make(a, b, frame_id, hit(b, surface_id) if with_hit else null, null)
	if with_hit:
		s.surface_id = surface_id
		s.hit_side = Side.Value.LEFT
		s.hit_on_segment = true
	return s


static func path(steps: Array, cursor_index: int = -1) -> TracePath:
	var p := TracePath.new()
	p.steps = steps
	p.cursor_index = cursor_index
	return p


static func typed(steps: Array, types: Array) -> Array:
	var out: Array = []
	for i in steps.size():
		out.append(steps[i].with_type(types[i]))
	return out


## A vertical wall at x = 1800 the clean traces end on (TRACE-ENDS and
## VISUAL-ON-CARRIER need a scene to agree with).
static func far_surfaces() -> Array:
	H.reset_counters()
	return [H.wall_between(Vector2(1800, 0), Vector2(1800, 600))]


## Every case: {name, player, cursor, physical, planned, typed, surfaces, expect}.
## `expect` lists the invariants the case must trip (a case may trip more);
## the clean case trips none.
static func cases() -> Array[Dictionary]:
	var A := StepTypes.Type.ALIGNED
	var AP := StepTypes.Type.ALIGNED_POST_PLANNED
	var DY := StepTypes.Type.DIVERGED_PHYSICAL
	var DR := StepTypes.Type.DIVERGED_PLANNED
	var out: Array[Dictionary] = []
	var player := Vector2(100, 100)
	var cursor := Vector2(300, 100)

	# a clean straight trace: player -> hit on a wall at the right edge
	var wall := far_surfaces()
	_surfaces = wall
	var clean_steps := [step(player, Vector2(1800, 100), true, wall[0].id)]
	var clean := path(clean_steps, 1)
	out.append({"name": "clean", "player": player, "cursor": cursor, "physical": clean, "planned": clean,
		"typed": typed(clean_steps, [A]), "surfaces": wall, "expect": []})

	# NOGAPS / PHYSICAL-CONTINUITY: a 20 px gap between two hit steps
	var gap_steps := [step(player, Vector2(500, 100), true, wall[0].id), step(Vector2(520, 100), Vector2(1800, 100), true, wall[0].id)]
	var gap := path(gap_steps, 2)
	out.append({"name": "gap", "player": player, "cursor": cursor, "physical": gap, "planned": gap,
		"typed": typed(gap_steps, [A, A]), "surfaces": wall, "expect": ["NOGAPS", "PHYSICAL-CONTINUITY"]})

	# S16: a NaN coordinate
	var nan_steps := [step(player, Vector2(NAN, 100), false)]
	var nanp := path(nan_steps)
	out.append({"name": "nan", "player": player, "cursor": cursor, "physical": nanp, "planned": nanp,
		"typed": typed(nan_steps, [A]), "surfaces": wall, "expect": ["S16", "TRACE-ENDS"]})

	# GREEN-FROM-PLAYER + SINGLE-DIVERGENCE: yellow first, then green again
	out.append({"name": "yellow_then_green", "player": player, "cursor": cursor, "physical": clean, "planned": clean,
		"typed": typed(gap_steps, [DY, A]), "surfaces": wall, "expect": ["GREEN-FROM-PLAYER", "SINGLE-DIVERGENCE", "PHYSICAL-PREVIEW-MATCH"]})

	# ORIGIN-NOT-REHIT: a zero-length step carrying a hit with t > 0
	var zero_steps := [step(player, player, true, wall[0].id), step(player, Vector2(1800, 100), true, wall[0].id)]
	var zero := path(zero_steps, 2)
	out.append({"name": "zero_length_rehit", "player": player, "cursor": cursor, "physical": zero, "planned": zero,
		"typed": typed(zero_steps, [A, A]), "surfaces": wall, "expect": ["ORIGIN-NOT-REHIT", "UX7"]})

	# NO-POST-CURSOR-DIVERGENCE: aligned to the cursor, then a planned-only red step
	var post_steps := [step(player, Vector2(300, 100), false), step(Vector2(300, 100), Vector2(1800, 100), true, wall[0].id)]
	var post := path(post_steps, 1)
	out.append({"name": "post_cursor_divergence", "player": player, "cursor": cursor, "physical": post, "planned": post,
		"typed": typed(post_steps, [A, AP]) + typed([step(Vector2(300, 100), Vector2(300, 900), false)], [StepTypes.Type.DIVERGED_POST_PLANNED]),
		"surfaces": wall, "expect": ["NO-POST-CURSOR-DIVERGENCE"]})

	# NO-POST-CURSOR-DIVERGENCE (Stage 110): aligned to the cursor, diverging right
	# at it -- the old check read the first diverged step as one before the cursor
	out.append({"name": "divergence_at_cursor", "player": player, "cursor": cursor, "physical": post, "planned": post,
		"typed": typed([post_steps[0]], [A]) + typed([step(Vector2(300, 100), Vector2(300, 900), false)], [StepTypes.Type.DIVERGED_POST_PLANNED])
			+ typed([post_steps[1]], [StepTypes.Type.DIVERGED_PHYSICAL]),
		"surfaces": wall, "expect": ["NO-POST-CURSOR-DIVERGENCE"]})

	# SOLID-PATH-TO-CURSOR: the first solid step does not start at the player
	var off_steps := [step(Vector2(120, 100), Vector2(1800, 100), true, wall[0].id)]
	var off := path(off_steps, 1)
	out.append({"name": "solid_not_from_player", "player": player, "cursor": cursor, "physical": off, "planned": off,
		"typed": typed(off_steps, [A]), "surfaces": wall, "expect": ["UX7", "SOLID-PATH-TO-CURSOR"]})

	# TRACE-ENDS: the trace stops mid-air
	var mid_steps := [step(player, Vector2(600, 400), false)]
	var mid := path(mid_steps)
	out.append({"name": "ends_mid_air", "player": player, "cursor": cursor, "physical": mid, "planned": mid,
		"typed": typed(mid_steps, [A]), "surfaces": wall, "expect": ["TRACE-ENDS"]})

	# VISUAL-ON-CARRIER: a hit step ending away from every carrier
	var vis_steps := [step(player, Vector2(600, 400), true, wall[0].id)]
	var vis := path(vis_steps, 1)
	out.append({"name": "visual_off_carrier", "player": player, "cursor": cursor, "physical": vis, "planned": vis,
		"typed": typed(vis_steps, [A]), "surfaces": wall, "expect": ["VISUAL-ON-CARRIER"]})

	# PHYSICS-COMPLIANCE: an identity-frame straight step crossing a mirror
	var mirror := H.mirror(400.0, 0.0, 600.0)
	var cross_steps := [step(player, Vector2(1800, 100), true, wall[0].id)]
	var cross := path(cross_steps, 1)
	out.append({"name": "crosses_mirror", "player": player, "cursor": cursor, "physical": cross, "planned": cross,
		"typed": typed(cross_steps, [A]), "surfaces": wall + [mirror], "expect": ["PHYSICS-COMPLIANCE"]})

	# UX7: no line while the cursor differs from the player
	var empty := path([])
	out.append({"name": "no_line", "player": player, "cursor": cursor, "physical": empty, "planned": empty,
		"typed": [], "surfaces": wall, "expect": ["UX7"]})

	# UX7: the aim does not point at the cursor (no plan)
	var away_steps := [step(player, Vector2(100, 900), false)]
	var away := path(away_steps)
	out.append({"name": "aim_away", "player": player, "cursor": cursor, "physical": away, "planned": away,
		"typed": typed(away_steps, [A]), "surfaces": wall, "expect": ["UX7", "TRACE-ENDS"]})

	# SOLID-PATH-TO-CURSOR: a gap between solid steps away from the bounds
	var solid_gap_steps := [step(player, Vector2(500, 100), false), step(Vector2(540, 100), Vector2(1800, 100), true, wall[0].id)]
	var solid_gap := path(solid_gap_steps, 2)
	out.append({"name": "solid_gap", "player": player, "cursor": cursor, "physical": solid_gap, "planned": solid_gap,
		"typed": typed(solid_gap_steps, [A, DR]), "surfaces": wall, "expect": ["SOLID-PATH-TO-CURSOR", "PHYSICAL-PREVIEW-MATCH"]})
	return out
