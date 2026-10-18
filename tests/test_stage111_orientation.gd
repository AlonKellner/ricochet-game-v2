extends GutTest
# Stage 111: the first epoch's realization is fixed by the plan's first entry
# (GAME_SPEC 13.1, R1-R3), not by where the cursor's image falls. On the
# mirror-and-wall level (the four F12 captures): a line mirror, reflect on the
# player's side, pass on the other; solid walls; pass edges. The cursors beyond
# the mirror -- in front of the player's image or behind it -- are reached by
# aiming at the mirror, reflecting and wrapping through infinity (the wrap
# unplanned, or consumed by [5 L, inf]); a cursor on the player's side with
# [5 L, inf] is not reached; [inf, 5 L] from the reflective side diverges at the
# mirror; the region covers both sides of the player's image and excludes the
# aims parallel to the mirror.
const H := preload("res://tests/test_helpers.gd")
const BOUNDS := Rect2(0, 0, 1920, 1080)
const PLAYER := Vector2(1306.67, 540.0)
const IMAGE := Vector2(293.33, 540.0) # the player's mirror image
var _surfaces: Array
var _mirror: Surface

func before_each() -> void:
	H.reset_counters()
	FeatureFlags.infinity_event_override = true
	_surfaces = []
	_surfaces.append(H.wall_between(Vector2(560, 240), Vector2(1360, 240)))
	_surfaces.append(H.wall_between(Vector2(1360, 840), Vector2(560, 840)))
	_surfaces.append(H.wall_between(Vector2(560, 840), Vector2(560, 240)))
	_surfaces.append(H.wall_between(Vector2(600, 300), Vector2(600, 780)))
	_mirror = _one_sided_mirror(Vector2(800, 300), Vector2(800, 780))
	_surfaces.append(_mirror)
	for e in [[Vector2(0, 0), Vector2(1920, 0)], [Vector2(1920, 0), Vector2(1920, 1080)],
			[Vector2(1920, 1080), Vector2(0, 1080)], [Vector2(0, 1080), Vector2(0, 0)]]:
		_surfaces.append(_edge(e[0], e[1]))

func after_each() -> void:
	FeatureFlags.infinity_event_override = null

# reflect on its LEFT side (the player's, x > 800), pass on its RIGHT
func _one_sided_mirror(a: Vector2, b: Vector2) -> Surface:
	var seg := Segment.from_coords(a, b, (a + b) / 2.0)
	var refl := SideConfig.new(ReflectionEffect.new(seg.get_carrier()), true)
	var pass_cfg := SideConfig.new(null, false)
	return Surface.new(seg, refl, pass_cfg, false, false)

func _edge(a: Vector2, b: Vector2) -> Surface:
	var cfg := SideConfig.new(null, false)
	return Surface.new(Segment.from_coords(a, b, (a + b) / 2.0), cfg, cfg, false, false)

func _plan(with_infinity: bool, infinity_first := false) -> Array:
	var m := PlanManager.PlanEntry.new(_mirror.id, Side.Value.LEFT)
	var inf := PlanManager.PlanEntry.new(BeamSolver.INFINITY_ID, Side.Value.LEFT)
	if not with_infinity:
		return [m]
	return [inf, m] if infinity_first else [m, inf]

func _aligned_to_cursor(pair: BeamTracerPair) -> bool:
	var ci := pair.planned.cursor_index
	if ci < 0:
		return false
	var merged := StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, ci)
	for i in ci:
		if merged[i].type != StepTypes.Type.ALIGNED:
			return false
	return true

func _events(pair: BeamTracerPair) -> Array:
	return pair.result.planned

# the four captures: in front of the image and behind it, with and without the entry
func test_cursors_beyond_the_mirror_are_reached_through_infinity():
	for cursor in [Vector2(480.5005, 524.9722), Vector2(166.1731, 535.9927)]:
		for with_inf in [false, true]:
			var pair := TestTrace.trace_pair(_surfaces, PLAYER, cursor, _plan(with_inf))
			assert_not_null(pair)
			assert_true(_aligned_to_cursor(pair), "%s inf=%s: aligned up to the cursor" % [cursor, with_inf])
			var first: TraceStep = pair.planned.steps[0]
			assert_lt(first.end.x, PLAYER.x, "%s inf=%s: aimed at the mirror" % [cursor, with_inf])
			assert_not_null(first.hit)
			assert_eq(first.surface_id, _mirror.id, "the first hit is the mirror")
			var ev := _events(pair)
			var wrap := -1
			var at_cursor := -1
			for i in ev.size():
				if ev[i].kind == "infinity" and wrap < 0:
					wrap = i
				if ev[i].kind == "cursor":
					at_cursor = i
			assert_true(wrap >= 0 and at_cursor > wrap, "the wrap comes before the cursor")
			assert_eq(ev[wrap].applied, with_inf, "the wrap is consumed only when planned")

func test_direct_cursor_with_infinity_planned_is_not_reached():
	var direct := TestTrace.trace_pair(_surfaces, PLAYER, Vector2(1000, 600), _plan(false))
	assert_true(_aligned_to_cursor(direct), "without the entry: reached at once")
	var pending := TestTrace.trace_pair(_surfaces, PLAYER, Vector2(1000, 600), _plan(true))
	assert_eq(pending.planned.cursor_index, -1, "with the entry: passed with the wrap pending, unreached")
	assert_lt(pending.planned.steps[0].end.x, PLAYER.x, "still aimed at the mirror")

func test_infinity_before_the_mirror_diverges_at_the_mirror():
	var pair := TestTrace.trace_pair(_surfaces, PLAYER, Vector2(480.5005, 524.9722), _plan(true, true))
	assert_eq(pair.planned.cursor_index, -1, "the entry is unmet: the cursor is not reached")
	var ev := _events(pair)
	assert_eq(ev[0].surface_id, _mirror.id)
	assert_false(ev[0].applied, "the planned walk passes the mirror: the wrap is next")
	var phys: Array = pair.result.physical
	assert_true(phys[0].applied, "the physical shot reflects: a divergence at the mirror")

func test_region_covers_both_sides_of_the_image_and_excludes_the_parallel_aims():
	var region := TestTrace.fresh_tracer().visibility(SceneDefs.defs_from_surfaces(_surfaces), PLAYER, _plan(false), {}, BOUNDS)
	assert_true(region.is_supported())
	assert_eq(region.contains(Vector2(480.5005, 524.9722)), 1, "in front of the image")
	assert_eq(region.contains(Vector2(166.1731, 535.9927)), 1, "behind the image")
	assert_eq(region.contains(Vector2(1000, 600)), 1, "the player's side")
	assert_eq(region.contains(Vector2(IMAGE.x, 900)), 0, "the aim parallel to the mirror never fires it")
	var with_inf := TestTrace.fresh_tracer().visibility(SceneDefs.defs_from_surfaces(_surfaces), PLAYER, _plan(true), {}, BOUNDS)
	assert_eq(with_inf.contains(Vector2(1000, 600)), 0, "with the entry: the player's side is passed with the wrap pending")
	assert_eq(with_inf.contains(Vector2(166.1731, 535.9927)), 1, "with the entry: behind the image")
