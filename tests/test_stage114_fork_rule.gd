extends GutTest

# Stage 114: the fork rule (GAME_SPEC 13.3) through the binding -- of a fork's
# candidates those on the segment are preferred, then the click-nearest.

const BOUNDS := Rect2(0, 0, 1000, 600)

func before_each() -> void:
	FeatureFlags.visibility_override = null

func after_each() -> void:
	FeatureFlags.visibility_override = null

func _room() -> Array:
	return RoomBuilder.create_room_surfaces(BOUNDS)

func _region(surfaces: Array, player: Vector2, plan: Array) -> VisibilityRegion:
	return TestTrace.fresh_tracer().visibility(SceneDefs.defs_from_surfaces(surfaces), player, plan, {}, BOUNDS)

func _circle_normal(start: Vector2, end_v: Vector2, via: Vector2) -> Surface:
	var cfg := SideConfig.new(CircleNormalProjection.new(false), true)
	return Surface.new(Segment.from_coords(start, end_v, via), cfg, cfg, false, false)

# the preview at the cursor: every merged step before it ALIGNED, the cursor reached
func _visible(surfaces: Array, player: Vector2, cursor: Vector2, plan: Array) -> bool:
	var pair := TestTrace.trace_pair(surfaces, player, cursor, plan)
	if pair == null or pair.planned.cursor_index < 0 or not pair.cursor_point_agrees:
		return false
	var merged := StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, pair.planned.cursor_index)
	for i in pair.planned.cursor_index:
		if merged[i].type != StepTypes.Type.ALIGNED:
			return false
	return true

func _entry(sid: int, side: Side.Value, click: Vector2) -> PlanManager.PlanEntry:
	var e := PlanManager.PlanEntry.new(sid, side)
	e.click = click
	return e

func _side_hit_first(surfaces: Array, player: Vector2, toward: Vector2, sid: int) -> Side.Value:
	for st in TestTrace.trace_pair(surfaces, player, toward).physical.steps:
		if st.hit != null and st.surface_id == sid:
			return st.hit_side
	return Side.Value.LEFT

# three quarters of the circle centred (600, 300), r 100: from angle 0 the long
# way round to angle 90 (y down); the gap is the open quarter between them
func test_over_a_semicircle_the_click_decides_only_where_both_antipodes_are_on_the_arc():
	var arc := _circle_normal(Vector2(700, 300), Vector2(600, 400), Vector2(500, 300))
	var surfaces := [arc] + _room()
	var player := Vector2(900, 500)
	var side := _side_hit_first(surfaces, player, Vector2(650, 350), arc.id)
	# the cursor (740, 440): its radial at 45 degrees runs through the gap; the
	# antipode at 225 is on the arc and wins, whatever the click
	var in_gap := Vector2(740, 440)
	for click in [Vector2(670, 370), Vector2(530, 230), Vector2(500, 300)]:
		var pair := TestTrace.trace_pair(surfaces, player, in_gap, [_entry(arc.id, side, click)])
		assert_not_null(pair, "a trace for the click %s" % click)
		var h := Vector2.INF
		for st in pair.planned.steps:
			if st.hit != null and st.surface_id == arc.id and st.hit_on_segment:
				h = st.end
				break
		assert_lt(h.x, 600.0, "the hit point is the on-arc antipode (left) for the click %s" % click)
		assert_lt(h.y, 300.0, "... above the centre, for the click %s" % click)
	# the cursor (500, 400): its radial at 135 / 315 degrees has both antipodes on the arc: the click decides
	var both := Vector2(500, 400)
	var r_a := _region(surfaces, player, [_entry(arc.id, side, Vector2(530, 370))])
	var r_b := _region(surfaces, player, [_entry(arc.id, side, Vector2(670, 230))])
	assert_true(r_a.is_supported() and r_b.is_supported(), "regions")
	assert_eq(r_a.contains(both), 1 if _visible(surfaces, player, both, [_entry(arc.id, side, Vector2(530, 370))]) else 0,
		"the region follows the preview (click near 135)")
	assert_eq(r_b.contains(both), 1 if _visible(surfaces, player, both, [_entry(arc.id, side, Vector2(670, 230))]) else 0,
		"the region follows the preview (click near 315)")
	assert_eq(r_a.check(), [], "the structure checks hold (a)")
	assert_eq(r_b.check(), [], "the structure checks hold (b)")

# the right half of the circle centred (600, 300): under a semicircle the click never matters
func test_under_a_semicircle_the_click_is_irrelevant():
	var arc := _circle_normal(Vector2(600, 200), Vector2(600, 400), Vector2(700, 300))
	var surfaces := [arc] + _room()
	var player := Vector2(900, 500)
	var side := _side_hit_first(surfaces, player, Vector2(655, 383), arc.id)
	var cursor := Vector2(620, 330)
	var want := _visible(surfaces, player, cursor, [_entry(arc.id, side, Vector2(650, 370))])
	for click in [Vector2(650, 230), Vector2(500, 300), Vector2(700, 300)]:
		assert_eq(_visible(surfaces, player, cursor, [_entry(arc.id, side, click)]), want, "the same answer for the click %s" % click)
		var region := _region(surfaces, player, [_entry(arc.id, side, click)])
		assert_eq(region.contains(cursor), 1 if want else 0, "the region agrees for the click %s" % click)

# a cursor exactly on the segment is its own candidate: reached at once when the click prefers it
func test_a_cursor_on_the_segment_is_its_own_hit_point():
	var arc := _circle_normal(Vector2(600, 200), Vector2(600, 400), Vector2(700, 300))
	var surfaces := [arc] + _room()
	var player := Vector2(900, 300)
	var cursor := Vector2(700, 300)   # the via: on the arc, facing the player
	var side := _side_hit_first(surfaces, player, Vector2(710, 300), arc.id) # (probed short of the arc: a cursor on it is a cursor step, not a hit)
	var pair := TestTrace.trace_pair(surfaces, player, cursor, [_entry(arc.id, side, Vector2(710, 300))])
	assert_not_null(pair, "a trace")
	assert_eq(pair.planned.cursor_index, 1, "the cursor right after the firing step")
	assert_true(_visible(surfaces, player, cursor, [_entry(arc.id, side, Vector2(710, 300))]), "visible")
