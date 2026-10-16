extends SceneTree

# Stage 100: the per-frame cost of a live scene, headless.
#
#   godot --headless --path . -s res://scripts/testing/frame_bench.gd -- --scene=res://scenes/main.tscn --frames=250
#   make frame-bench [SCENE=...] [FRAMES=...]
#
# Loads the scene, runs phases of FRAMES frames each and reports, per phase,
# the wall time per frame (hook to hook of the SceneTree's _process: every
# node's _process, the solver, the redraw flush) and how many times the
# PathRenderer recomputed its trace. Headless Godot paces frames with a
# ~7 ms sleep when it cannot draw; the bench turns that pacing off so a frame
# costs what its work costs. (Performance.TIME_PROCESS is a once-per-second
# maximum, useless here.) The static phases must not recompute at all after their first frame
# (the dirty flags), and the mean frame of every phase must stay under
# BUDGET_MS. The budget is generous on purpose: this guards against a per-frame
# regression on a possibly loaded machine; it is not a profiler.

const DEFAULT_SCENE := "res://scenes/main.tscn"
const BUDGET_MS := 8.0
const ORBIT_RADIUS := 420.0

var _frames := 250
var _scene: Node
var _player: Node2D
var _cursor: Node2D
var _renderer: Node2D
var _gm: Node
var _phases: Array = []       # {name, setup: Callable, step: Callable, static: bool}
var _phase := -1
var _frame := 0
var _times: Array[float] = []
var _version_start := 0
var _vis_start := 0   # Stage 110: the visibility renderer's recomputations at the phase's start
var _rows: Array = []
var _failed := false
var _base_player := Vector2.ZERO
var _base_cursor := Vector2.ZERO
var _scene_path := ""
var _booted := false
var _last_tick := 0

func _initialize() -> void:
	var scene_path := DEFAULT_SCENE
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--scene="):
			scene_path = arg.trim_prefix("--scene=")
		elif arg.begins_with("--frames="):
			_frames = int(arg.trim_prefix("--frames="))
	# no frame pacing: headless Godot otherwise sleeps to ~7 ms per frame
	OS.low_processor_usage_mode_sleep_usec = 0
	_scene_path = scene_path
	_scene = load(scene_path).instantiate()
	root.add_child(_scene)
	_player = _scene.get_node_or_null("Player")
	_cursor = _scene.get_node_or_null("Cursor")
	_renderer = _scene.get_node_or_null("PathRenderer")
	_gm = _scene.get_node_or_null("GameManager")
	if _player == null or _cursor == null or _renderer == null:
		push_error("frame_bench: the scene needs Player, Cursor and PathRenderer nodes")
		quit(2)
		return
	_phases = [
		{"name": "static", "static": true, "setup": _hold, "step": _no_step},
		{"name": "cursor_orbit", "static": false, "setup": _hold, "step": _orbit},
		{"name": "plan_static", "static": true, "setup": _plan_two, "step": _no_step},
		{"name": "plan_orbit", "static": false, "setup": _plan_two, "step": _orbit},
		{"name": "hover_static", "static": true, "setup": _hover_near_surface, "step": _no_step},
		{"name": "player_orbit", "static": false, "setup": _hold, "step": _orbit_player},
	]

# After the scene's _ready (which enables processing on the nodes): the
# headless cursor follows the absent mouse and the player falls under gravity;
# hold both where the bench puts them.
func _boot() -> void:
	_cursor.set_process(false)
	_player.set_physics_process(false)
	_base_player = _player.global_position
	_base_cursor = _base_player + Vector2(ORBIT_RADIUS, -120.0)
	_cursor.global_position = _base_cursor
	print("frame_bench: %s, %d frames per phase, %d surfaces" % [_scene_path, _frames, _surfaces().size()])

func _process(_delta: float) -> bool:
	var now := Time.get_ticks_usec()
	if not _booted:
		_boot()
		_booted = true
		_last_tick = now
		return false
	if _phase >= 0:
		if _frame > 0:
			_times.append(float(now - _last_tick) / 1000.0)
		_last_tick = now
		if _frame < _frames:
			_phases[_phase]["step"].call(_frame)
			_frame += 1
			return false
		_finish_phase()
	_last_tick = now
	_phase += 1
	if _phase >= _phases.size():
		_report()
		return true
	_times.clear()
	_frame = 0
	_player.global_position = _base_player
	_cursor.global_position = _base_cursor
	if _gm:
		_gm.plan.clear()
	_phases[_phase]["setup"].call()
	_version_start = _renderer.trace_version
	_vis_start = _vis_version()
	return false

# ---------------------------------------------------------------- phases

func _hold() -> void:
	pass

func _no_step(_i: int) -> void:
	pass

func _orbit(i: int) -> void:
	var a := TAU * float(i) / float(_frames)
	_cursor.global_position = _base_player + ORBIT_RADIUS * Vector2(cos(a), sin(a))

func _orbit_player(i: int) -> void:
	var a := TAU * float(i) / float(_frames)
	_player.global_position = _base_player + 40.0 * Vector2(cos(a), sin(a))

# A plan of the first two interactive sides, when the scene has them.
func _plan_two() -> void:
	if _gm == null:
		return
	var added := 0
	for s in _surfaces():
		for side in [Side.Value.LEFT, Side.Value.RIGHT]:
			var cfg: SideConfig = s.active_side_config(side, _gm.game_state)
			if cfg != null and cfg.interactive:
				_gm.plan.add_entry(s.id, side)
				added += 1
				break
		if added >= 2:
			return

# The cursor a few pixels from the first interactive surface: hover overlays on.
func _hover_near_surface() -> void:
	for s in _surfaces():
		for side in [Side.Value.LEFT, Side.Value.RIGHT]:
			var cfg: SideConfig = s.active_side_config(side, _gm.game_state if _gm else GameState.new())
			if cfg != null and cfg.interactive:
				var seg: Segment = s.segment
				var along := (seg.end.coords - seg.start.coords).normalized()
				for sign_val in [1.0, -1.0]:
					var p: Vector2 = seg.via.coords + Vector2(-along.y, along.x) * 6.0 * sign_val
					if seg.determine_side(p) == side:
						_cursor.global_position = p
						return

func _surfaces() -> Array:
	return _scene.surfaces if "surfaces" in _scene else []

func _vis_version() -> int:
	var vis := _scene.get_node_or_null("VisibilityRenderer")
	return vis.visibility_version if vis != null else 0

# ---------------------------------------------------------------- report

func _finish_phase() -> void:
	var ph: Dictionary = _phases[_phase]
	var sum := 0.0
	var mx := 0.0
	for t in _times:
		sum += t
		mx = maxf(mx, t)
	var mean := sum / maxi(1, _times.size())   # the setup frame is not sampled
	var sorted := _times.duplicate()
	sorted.sort()
	var p95: float = sorted[mini(sorted.size() - 1, int(sorted.size() * 0.95))] if sorted.size() > 0 else 0.0
	var recomputes: int = _renderer.trace_version - _version_start
	var vis_recomputes: int = _vis_version() - _vis_start
	var ok := true
	# the setup frame may recompute once; a static phase never again (the
	# visibility region: neither in a static phase nor while only the cursor moves)
	if ph["static"] and recomputes > 1:
		ok = false
	if (ph["static"] or ph["name"] == "cursor_orbit") and vis_recomputes > 1:
		ok = false
	if mean > BUDGET_MS:
		ok = false
	_failed = _failed or not ok
	_rows.append({"name": ph["name"], "mean": mean, "p95": p95, "max": mx, "recomputes": recomputes,
		"vis": vis_recomputes, "frames": _times.size(), "ok": ok})

func _report() -> void:
	print("")
	print("%-14s %8s %8s %8s %11s %8s %s" % ["phase", "mean_ms", "p95_ms", "max_ms", "recomputes", "regions", ""])
	for r in _rows:
		print("%-14s %8.3f %8.3f %8.3f %6d/%-4d %8d %s" % [r["name"], r["mean"], r["p95"], r["max"], r["recomputes"], r["frames"], r["vis"],
			"" if r["ok"] else "FAIL"])
	print("")
	if _failed:
		print("frame_bench: FAIL (a static phase recomputed the trace or the region, or a mean frame exceeded %.1f ms)" % BUDGET_MS)
	else:
		print("frame_bench: ok (budget %.1f ms per frame)" % BUDGET_MS)
	# a clean exit: drop the scene and the callables into this tree
	_phases.clear()
	root.remove_child(_scene)
	_scene.free()
	quit(1 if _failed else 0)
