extends GutTest

# Stage 100: the per-frame work is driven by dirty keys. The PathRenderer
# recomputes its traces only when an input changed (positions, plan, flags,
# surfaces), the GameManager rebuilds hover and plan overlays only when their
# inputs changed, a SurfaceNode ignores unchanged overlays, and SceneDefsCache
# builds the solver's defs once per surfaces array. Every dirty path is
# compared with a fresh computation: the flags must never change what is shown.

const H := preload("res://tests/test_helpers.gd")
const SCENE := "res://scenes/main.tscn"
const SurfaceNodeScript = preload("res://scripts/game/surface_node.gd")

const PLAYER := Vector2(700, 560)
const CURSOR := Vector2(1130, 470)
const CURSORS := [Vector2(1130, 480), Vector2(300, 300), Vector2(1500, 900), Vector2(700, 100),
	Vector2(1250, 560), Vector2(400, 700), Vector2(1800, 200), Vector2(960, 1000)]

var _scene: Node
var _player: Node2D
var _cursor: Node2D
var _renderer: Node2D
var _gm: Node

func before_each() -> void:
	Surface.reset_id_counter()
	_scene = load(SCENE).instantiate()
	add_child_autofree(_scene)
	_player = _scene.get_node("Player")
	_cursor = _scene.get_node("Cursor")
	_renderer = _scene.get_node("PathRenderer")
	_gm = _scene.get_node("GameManager")
	# headless: the cursor follows the absent mouse and the player falls under
	# gravity; hold both where the test puts them
	_cursor.set_process(false)
	_player.set_physics_process(false)
	_player.global_position = PLAYER
	_cursor.global_position = CURSOR
	await get_tree().process_frame
	await get_tree().process_frame

func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame

# The renderer's traces computed independently: a fresh tracer, the game's
# defs, the same display preparation.
func _fresh_pair() -> Dictionary:
	var surfaces: Array = _scene.surfaces
	var tracer := BeamTracer.new()
	var pair: BeamTracerPair = tracer.trace_pair(SceneDefs.defs_from_surfaces(surfaces), _player.global_position,
		_cursor.global_position, _gm.plan.entries, _gm.game_state.flags, SceneDefs.segments_by_id(surfaces))
	if pair == null:
		return {"physical": null, "planned": null}
	var bounds: Rect2 = _scene.level_bounds
	return {"physical": VisualConverter.prepare_for_display(pair.physical, bounds, surfaces),
		"planned": VisualConverter.prepare_for_display(pair.planned, bounds, surfaces)}

func _assert_same_path(a: TracePath, b: TracePath, what: String) -> void:
	if a == null or b == null:
		assert_true(a == null and b == null, "%s: both null or neither" % what)
		return
	assert_eq(a.steps.size(), b.steps.size(), "%s: step count" % what)
	assert_eq(a.cursor_index, b.cursor_index, "%s: cursor_index" % what)
	for i in mini(a.steps.size(), b.steps.size()):
		var x: TraceStep = a.steps[i]
		var y: TraceStep = b.steps[i]
		assert_true(x.start == y.start and x.end == y.end and x.via == y.via, "%s: step %d geometry" % [what, i])
		assert_eq(x.surface_id, y.surface_id, "%s: step %d surface" % [what, i])
		assert_eq(x.frame_id, y.frame_id, "%s: step %d frame" % [what, i])
		assert_eq(x.ghost, y.ghost, "%s: step %d ghost" % [what, i])

func _assert_renderer_fresh(what: String) -> void:
	var fresh := _fresh_pair()
	_assert_same_path(_renderer.get_traced_path(), fresh["physical"], what + " physical")
	_assert_same_path(_renderer.get_planned_path(), fresh["planned"], what + " planned")

func _surface_at_x(x: float) -> Surface:
	for s in _scene.surfaces:
		if s.segment.is_line() and s.segment.start.coords.x == x and s.segment.end.coords.x == x:
			return s
	return null

# A point 6 px from the surface's via on an interactive side.
func _hover_point(surf: Surface) -> Vector2:
	var seg: Segment = surf.segment
	var along := (seg.end.coords - seg.start.coords).normalized()
	for sign_val in [1.0, -1.0]:
		var p: Vector2 = seg.via.coords + Vector2(-along.y, along.x) * 6.0 * sign_val
		var cfg: SideConfig = surf.active_side_config(seg.determine_side(p), _gm.game_state)
		if cfg != null and cfg.interactive:
			return p
	fail_test("no interactive side on surface %d" % surf.id)
	return seg.via.coords

func _node_of(surf: Surface) -> Node2D:
	for child in _scene.get_children():
		if "surface" in child and child.surface == surf:
			return child
	return null

# ---------------------------------------------------------------- the renderer

func test_static_frames_do_not_recompute():
	var v: int = _renderer.trace_version
	var physical = _renderer.get_traced_path()
	assert_not_null(physical, "a trace exists after the first frames")
	await _frames(5)
	assert_eq(_renderer.trace_version, v, "no recomputation while nothing changes")
	assert_true(is_same(_renderer.get_traced_path(), physical), "the same path object is kept")

func test_cursor_moves_recompute_once_and_match_fresh():
	for c in CURSORS:
		var v: int = _renderer.trace_version
		_cursor.global_position = c
		await _frames(1)
		assert_eq(_renderer.trace_version, v + 1, "one recomputation for cursor %s" % c)
		_assert_renderer_fresh("cursor %s" % c)
		await _frames(1)
		assert_eq(_renderer.trace_version, v + 1, "no second recomputation for cursor %s" % c)

func test_player_move_recomputes_and_matches_fresh():
	var v: int = _renderer.trace_version
	_player.global_position = PLAYER + Vector2(30, -20)
	await _frames(1)
	assert_eq(_renderer.trace_version, v + 1)
	_assert_renderer_fresh("player moved")

func test_plan_changes_recompute_and_match_fresh():
	var mirror := _surface_at_x(1400.0)
	assert_not_null(mirror, "main.tscn has the mirror at x = 1400")
	var v: int = _renderer.trace_version
	_gm.plan.add_entry(mirror.id, Side.Value.LEFT)
	await _frames(1)
	assert_eq(_renderer.trace_version, v + 1, "a plan entry recomputes")
	_assert_renderer_fresh("one entry")
	await _frames(2)
	assert_eq(_renderer.trace_version, v + 1, "then nothing until the plan changes")
	_gm.plan.clear()
	await _frames(1)
	assert_eq(_renderer.trace_version, v + 2, "clearing the plan recomputes")
	_assert_renderer_fresh("cleared")

func test_flag_changes_recompute():
	var v: int = _renderer.trace_version
	_gm.game_state = GameState.new({"open": true})
	await _frames(1)
	assert_eq(_renderer.trace_version, v + 1, "a replaced state recomputes")
	_gm.game_state.flags["open"] = false
	await _frames(1)
	assert_eq(_renderer.trace_version, v + 2, "flags edited in place recompute (the key copies them)")
	await _frames(1)
	assert_eq(_renderer.trace_version, v + 2)

func test_direct_compute_always_recomputes():
	# the invariant checker positions the nodes and calls _compute_trace() itself
	var v: int = _renderer.trace_version
	_renderer._compute_trace()
	_renderer._compute_trace()
	assert_eq(_renderer.trace_version, v + 2)

func test_player_on_cursor_is_stable():
	_cursor.global_position = PLAYER
	await _frames(1)
	var v: int = _renderer.trace_version
	assert_null(_renderer.get_traced_path(), "no line for player = cursor (UX7)")
	await _frames(3)
	assert_eq(_renderer.trace_version, v, "the empty trace is not recomputed every frame")

func test_renderer_bounds_are_the_level_bounds():
	assert_eq(_renderer._level_bounds(), _scene.level_bounds)
	assert_eq(_scene.level_bounds, VisualConverter.DEFAULT_BOUNDS(), "every shipped scene uses the default bounds")

# ---------------------------------------------------------------- overlays and hover

func test_plan_overlays_untouched_while_unchanged():
	var mirror := _surface_at_x(1400.0)
	var node := _node_of(mirror)
	assert_not_null(node)
	_gm.plan.add_entry(mirror.id, Side.Value.LEFT)
	await _frames(1)
	var ov: Array = node._plan_overlays
	assert_gt(ov.size(), 0, "the planned mirror shows plan chevrons")
	await _frames(3)
	assert_true(is_same(node._plan_overlays, ov), "unchanged overlays are not replaced")
	_cursor.global_position = Vector2(300, 300)
	await _frames(1)
	# the overlays were rebuilt (the physical hits changed) but equal ones are kept
	assert_true(ChevronOverlay.same_list(node._plan_overlays, ov) == is_same(node._plan_overlays, ov),
		"a node keeps its overlays exactly when the rebuilt ones are equal")

func test_hover_untouched_while_unchanged():
	var mirror := _surface_at_x(1400.0)
	var node := _node_of(mirror)
	_cursor.global_position = _hover_point(mirror)
	await _frames(1)
	assert_true(is_same(_gm._hovered_node, node), "the mirror is hovered")
	var ov: Array = node._hover_overlays
	assert_gt(ov.size(), 0)
	await _frames(3)
	assert_true(is_same(node._hover_overlays, ov), "the hover is not rebuilt while the cursor rests")
	_cursor.global_position = Vector2(300, 300)
	await _frames(1)
	assert_null(_gm._hovered_node, "the hover ends when the cursor leaves")
	assert_eq(node._hover_overlays.size(), 0)

func test_surface_node_ignores_equal_overlays():
	var mirror := _surface_at_x(1400.0)
	var node := _node_of(mirror)
	var a := [ChevronOverlay.hover(Side.Value.LEFT, true, Side.Value.LEFT, Color.WHITE)]
	var b := [ChevronOverlay.hover(Side.Value.LEFT, true, Side.Value.LEFT, Color.WHITE)]
	var c := [ChevronOverlay.hover(Side.Value.RIGHT, true, Side.Value.RIGHT, Color.WHITE)]
	node.set_hover_overlays(a)
	node.set_hover_overlays(b)
	assert_true(is_same(node._hover_overlays, a), "equal overlays leave the node's list alone")
	node.set_hover_overlays(c)
	assert_true(is_same(node._hover_overlays, c), "different overlays replace it")
	var idx: Array[int] = [1]
	node.set_plan_overlays(a, idx)
	node.set_plan_overlays(b, [1] as Array[int])
	assert_true(is_same(node._plan_overlays, a))
	node.set_plan_overlays(b, [2] as Array[int])
	assert_true(is_same(node._plan_overlays, b), "changed indices replace the list")

func test_overlay_equality():
	var a := ChevronOverlay.plan(0, true, 0, Color.RED, Color.BLUE, Color.GREEN)
	var b := ChevronOverlay.plan(0, true, 0, Color.RED, Color.BLUE, Color.GREEN)
	var c := ChevronOverlay.plan(0, true, 0, Color.RED, Color.BLUE, Color.WHITE)
	assert_true(a.equals(b))
	assert_false(a.equals(c))
	assert_false(a.equals(null))
	assert_true(ChevronOverlay.same_list([a], [b]))
	assert_false(ChevronOverlay.same_list([a], [c]))
	assert_false(ChevronOverlay.same_list([a], []))
	assert_true(ChevronOverlay.same_list([], []))

func test_surface_node_caches_arc_params():
	for child in _scene.get_children():
		if "surface" in child and not child.surface.segment.is_line() and not child.surface.segment.full:
			var p1: Dictionary = child._arc_params()
			var p2: Dictionary = child._arc_params()
			assert_true(is_same(p1, p2), "the arc parameters are computed once")
			var seg: Segment = child.surface.segment
			assert_eq(p1, VisualConverter.arc_params(seg.start.coords, seg.via.coords, seg.end.coords))
			return
	fail_test("main.tscn has an inversion arc")

# ---------------------------------------------------------------- the caches

func test_scene_defs_cache_is_per_array():
	H.reset_counters()
	var arr: Array = [H.mirror(400.0)]
	var cache := SceneDefsCache.new()
	var d1 := cache.defs(arr)
	var d2 := cache.defs(arr)
	assert_true(is_same(d1, d2), "the same array gives the cached defs")
	assert_eq(cache.version, 1)
	assert_eq(d1, SceneDefs.defs_from_surfaces(arr))
	assert_eq(cache.segments(arr), SceneDefs.segments_by_id(arr))
	arr.append(H.wall(800.0))
	var d3 := cache.defs(arr)
	assert_eq(cache.version, 2, "a grown array rebuilds")
	assert_eq(d3.size(), 2)
	assert_eq(d3, SceneDefs.defs_from_surfaces(arr))
	var copy: Array = arr.duplicate()
	cache.defs(copy)
	assert_eq(cache.version, 3, "a different array object rebuilds (identity key)")
	assert_eq(cache.version_of(copy), 3)

func test_plan_manager_version_counts_changes():
	var plan := PlanManager.new()
	assert_eq(plan.version, 0)
	plan.add_entry(1, Side.Value.LEFT)
	assert_eq(plan.version, 1)
	plan.remove_last_of(7)
	assert_eq(plan.version, 1, "removing a missing surface changes nothing")
	plan.remove_last_of(1)
	assert_eq(plan.version, 2)
	plan.clear()
	assert_eq(plan.version, 3)
	plan.restore_from([PlanManager.PlanEntry.new(2, Side.Value.RIGHT)])
	assert_eq(plan.version, 4)

func test_level_walls_follow_default_bounds():
	# the edge walls built from level_bounds equal the former 1920x1080 ones
	var found := 0
	var expected := [[Vector2(0, 0), Vector2(1920, 0)], [Vector2(1920, 0), Vector2(1920, 1080)],
		[Vector2(1920, 1080), Vector2(0, 1080)], [Vector2(0, 1080), Vector2(0, 0)]]
	for s in _scene.surfaces:
		for e in expected:
			if s.segment.start.coords == e[0] and s.segment.end.coords == e[1]:
				found += 1
				assert_false(s.active_side_config(Side.Value.LEFT, GameState.new()).interactive, "edge walls are not interactive")
	assert_eq(found, 4, "the four edge walls")
