class_name SweepRunner
extends RefCounted

# Positions (grid, fuzz, points of interest) of the invariant sweeps and the
# sweep itself: SweepKernel checks every combo natively; the positions stay
# here (Godot's RandomNumberGenerator).

var _bounds_min := Vector2(0, 0)
var _bounds_max := Vector2(1920, 1080)
var _grid_size := 5
var _fuzz_count := 10
var _fuzz_seed := 42

# Sharding for parallel test runs (run_tests_parallel.sh): SWEEP_SHARD="i/n"
# makes a sweep check only the player positions with index % n == i. The n
# shards together cover exactly the unsharded sweep. Unset: everything.
static func shard() -> Vector2i:
	var s := OS.get_environment("SWEEP_SHARD")
	if s == "" or not s.contains("/"):
		return Vector2i(0, 1)
	var n := s.get_slice("/", 1).to_int()
	var i := s.get_slice("/", 0).to_int()
	assert(n >= 1 and i >= 0 and i < n, "SWEEP_SHARD must be i/n with 0 <= i < n")
	return Vector2i(i, n)

static func in_shard(index: int) -> bool:
	var sh := shard()
	return index % sh.y == sh.x

# violations output of a sweep (tag) in this shard (shards are merged into
# violations.json by run_tests_parallel.sh)
static func violations_path(tag: String) -> String:
	var sh := shard()
	return "res://violations.json" if sh.y == 1 else "res://violations.%s.shard%d.json" % [tag, sh.x]

func configure(grid_size: int = 5, fuzz_count: int = 10, fuzz_seed: int = 42) -> SweepRunner:
	_grid_size = grid_size
	_fuzz_count = fuzz_count
	_fuzz_seed = fuzz_seed
	return self

func set_bounds(bounds_min: Vector2, bounds_max: Vector2) -> SweepRunner:
	_bounds_min = bounds_min
	_bounds_max = bounds_max
	return self

func build_positions(custom_positions: Array[Vector2] = []) -> Array[Vector2]:
	var positions: Array[Vector2] = []

	for y in _grid_size:
		for x in _grid_size:
			var px: float
			var py: float
			if _grid_size > 1:
				px = _bounds_min.x + (_bounds_max.x - _bounds_min.x) * x / (_grid_size - 1)
				py = _bounds_min.y + (_bounds_max.y - _bounds_min.y) * y / (_grid_size - 1)
			else:
				px = (_bounds_min.x + _bounds_max.x) / 2.0
				py = (_bounds_min.y + _bounds_max.y) / 2.0
			positions.append(Vector2(px, py))

	var rng := RandomNumberGenerator.new()
	rng.seed = _fuzz_seed
	for i in _fuzz_count:
		var fx := rng.randf_range(_bounds_min.x, _bounds_max.x)
		var fy := rng.randf_range(_bounds_min.y, _bounds_max.y)
		positions.append(Vector2(fx, fy))

	positions.append_array(custom_positions)

	return positions

func sweep(scene: Node, custom_positions: Array[Vector2] = []) -> Dictionary:
	var has_room: bool = "room_rect" in scene and "build_room" in scene and scene.build_room
	var has_blocks: bool = "block_lines" in scene and scene.block_lines.size() > 0
	if has_room or has_blocks:
		var rect: Rect2 = scene.room_rect if "room_rect" in scene else Rect2(560, 240, 800, 600)
		var margin := 10.0
		_bounds_min = rect.position + Vector2(margin, margin)
		_bounds_max = rect.position + rect.size - Vector2(margin, margin)

	var poi := _extract_points_of_interest(scene)
	var all_custom: Array[Vector2] = []
	all_custom.append_array(poi)
	all_custom.append_array(custom_positions)

	var positions := build_positions(all_custom)
	var total := positions.size() * positions.size()
	var failures: Array[Dictionary] = []
	for f in sweep_native(scene, positions, [[]], false):
		failures.append({
			"player_pos": f.player_pos,
			"cursor_pos": f.cursor_pos,
			"violations": f.violations,
		})

	return {
		"total_combos": total,
		"position_count": positions.size(),
		"pass_count": total - failures.size(),
		"fail_count": failures.size(),
		"failures": failures,
	}

# The native sweep of one scene (SweepKernel, no scene tree in the loop):
# every (player, cursor) combo of `positions` in this shard under each plan
# (PlanEntry arrays), as PathRenderer + InvariantChecker.check_all would
# check it. Returns the failing combos:
# [{player_pos, cursor_pos, plan_index, violations: Array[String]}].
static func sweep_native(scene: Node, positions: Array[Vector2], plans: Array, skip_equal: bool) -> Array[Dictionary]:
	var renderer: Node = scene.get_node_or_null("PathRenderer")
	var flags: Dictionary = renderer.get_flags() if renderer and renderer.has_method("get_flags") else {}
	var surfaces: Array = scene.surfaces if "surfaces" in scene else []
	var defs := SceneDefs.defs_from_surfaces(surfaces)
	var infos := InvariantChecker.surface_infos(surfaces, flags)
	var sh := shard()
	var out: Array[Dictionary] = []
	for raw in SweepKernel.run(defs, infos, PackedVector2Array(positions), plans, flags, sh.x, sh.y, skip_equal):
		var violations: Array[String] = []
		violations.assign(raw.violations)
		out.append({"player_pos": raw.player, "cursor_pos": raw.cursor, "plan_index": raw.plan_index, "violations": violations})
	return out

# Combos of `positions` in this shard: the count the native sweep checks per plan.
static func combo_count(positions: Array[Vector2], skip_equal: bool) -> int:
	var n := 0
	for pi in positions.size():
		if in_shard(pi):
			n += positions.size() - (1 if skip_equal else 0)
	return n

func _extract_points_of_interest(scene: Node) -> Array[Vector2]:
	var points: Array[Vector2] = []
	var seen: Dictionary = {}

	if "room_rect" in scene:
		var r: Rect2 = scene.room_rect
		for corner in [r.position, Vector2(r.end.x, r.position.y), r.end, Vector2(r.position.x, r.end.y)]:
			_add_unique(points, seen, corner)

	var screen := VisualConverter.DEFAULT_BOUNDS()
	for corner in [screen.position, Vector2(screen.end.x, screen.position.y),
			screen.end, Vector2(screen.position.x, screen.end.y)]:
		_add_unique(points, seen, corner)

	if "surfaces" in scene:
		for surf in scene.surfaces:
			var s: Surface = surf
			_add_unique(points, seen, s.segment.start.coords)
			_add_unique(points, seen, s.segment.end.coords)
			if not is_inf(s.segment.via.coords.x) and not is_inf(s.segment.via.coords.y):
				_add_unique(points, seen, s.segment.via.coords)

	return points

func _add_unique(points: Array[Vector2], seen: Dictionary, point: Vector2) -> void:
	var key := "%f,%f" % [point.x, point.y]
	if not seen.has(key):
		seen[key] = true
		points.append(point)
