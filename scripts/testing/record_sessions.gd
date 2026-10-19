extends SceneTree

# Records the session replay golden (docs/research/session_digest.md): plays
# every session script (data/sessions/*.json, rc-session/1) in its level and
# writes one SessionDigest record per step to bench/sessions/<name>.jsonl.
#
#   godot --headless --fixed-fps 60 --path . -s res://scripts/testing/record_sessions.gd \
#       -- [--in data/sessions] [--out bench/sessions] [--only <substring>] [--shard i/n] [--full]
#
# --shard i/n plays the sessions with sorted index % n == i (make sessions runs
# the shards in parallel processes).
#
# Input goes through Input.parse_input_event (the game's own input path); the
# cursor's and (in teleport sessions) the player's processing are stopped and
# their positions set by the script; the visibility region is computed in the
# frame. After each step the scene settles: at least SETTLE_MIN frames, then
# until two consecutive records agree (at most SETTLE_MAX). A fire step plays
# the whole flight, sampling the arrow every frame. The player's physics runs
# only inside a hold step, for exactly its number of physics ticks (where a
# level load falls in the frame loop varies with the shard: nothing else may
# depend on it).

const SETTLE_MIN := 3
const SETTLE_MAX := 12
const MAX_FLIGHT_FRAMES := 20000
const KEYS := {
	"undo": KEY_Z, "reset": KEY_R, "clear_plan": KEY_C, "cycle_display": KEY_TAB,
	"toggle_gravity": KEY_G, "toggle_visibility": KEY_V, "any": KEY_X,
}
const ACTIONS := ["move_up", "move_down", "move_left", "move_right"]

var _full := false
var _errors := 0

func _initialize() -> void:
	_run.call_deferred()

func _run() -> void:
	var args := _args()
	var in_dir: String = args.get("in", "data/sessions")
	var out_dir: String = args.get("out", "bench/sessions")
	var only: String = args.get("only", "")
	var shard: PackedStringArray = String(args.get("shard", "0/1")).split("/")
	_full = args.has("full")
	DirAccess.make_dir_recursive_absolute(_abs(out_dir))
	var names := Array(DirAccess.get_files_at(_abs(in_dir))).filter(func(f): return f.ends_with(".json"))
	names.sort()
	var n := 0
	for k in names.size():
		var f: String = names[k]
		var name := f.trim_suffix(".json")
		if (only != "" and not name.contains(only)) or k % int(shard[1]) != int(shard[0]):
			continue
		var session = JSON.parse_string(FileAccess.get_file_as_string(_abs(in_dir).path_join(f)))
		if session == null or session.get("schema") != "rc-session/1":
			_fail("%s: not an rc-session/1 script" % f)
			continue
		var lines: PackedStringArray = await _play(name, session)
		var out := FileAccess.open(_abs(out_dir).path_join(name + ".jsonl"), FileAccess.WRITE)
		out.store_string("\n".join(lines) + "\n")
		out.close()
		n += 1
		print("%s: %d records" % [name, lines.size()])
	print("recorded %d sessions, %d errors" % [n, _errors])
	quit(1 if _errors > 0 else 0)

func _args() -> Dictionary:
	var out := {}
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		var k: String = a[i].trim_prefix("--")
		if i + 1 < a.size() and not a[i + 1].begins_with("--"):
			out[k] = a[i + 1]
			i += 2
		else:
			out[k] = true
			i += 1
	return out

func _abs(p: String) -> String:
	return p if p.is_absolute_path() else ProjectSettings.globalize_path("res://").path_join(p)

func _fail(msg: String) -> void:
	_errors += 1
	push_error(msg)

# ---------------------------------------------------------------- a session

func _play(name: String, session: Dictionary) -> PackedStringArray:
	var lines := PackedStringArray()
	Surface.reset_id_counter()
	var switches: Dictionary = session.get("switches", {})
	FeatureFlags.infinity_event_override = switches.get("infinity", null)
	FeatureFlags.visibility_override = switches.get("visibility", null)
	var level: Node = load("res://scenes/%s.tscn" % session["level"]).instantiate()
	root.add_child(level)
	var player: Node2D = level.get_node_or_null("Player")
	var cursor: Node2D = level.get_node_or_null("Cursor")
	if cursor:
		cursor.set_process(false)
	if player:
		player.set_physics_process(false)
	var vis := level.get_node_or_null("VisibilityRenderer")
	if vis:
		vis.async_compute = false
	lines.append(await _settled(level, {"i": 0, "op": "start"}))
	var steps: Array = session["steps"]
	for i in steps.size():
		var step: Dictionary = steps[i]
		var extra := {"i": i + 1, "op": step["op"]}
		match step["op"]:
			"teleport":
				player.global_position = _vec(step["at"], name)
				if player is CharacterBody2D:
					player.velocity = Vector2.ZERO
			"cursor":
				cursor.global_position = _vec(step["at"], name)
			"click":
				_mouse(MOUSE_BUTTON_LEFT if step["button"] == "left" else MOUSE_BUTTON_RIGHT)
			"key":
				extra["action"] = step["action"]
				_key(KEYS[step["action"]])
			"fire":
				extra["flight"] = await _fire(level, int(step.get("speedup_at", -1)))
			"hold":
				extra["trajectory"] = await _hold(level, step["actions"], int(step["ticks"]))
			_:
				_fail("%s: unknown op %s" % [name, step["op"]])
		lines.append(await _settled(level, extra))
	root.remove_child(level)
	level.free()
	FeatureFlags.infinity_event_override = null
	FeatureFlags.visibility_override = null
	return lines

# Session coordinates are multiples of 1/4 px: exact in any decimal parser.
func _vec(a: Array, name: String) -> Vector2:
	for x in a:
		if float(x) * 4.0 != roundf(float(x) * 4.0):
			_fail("%s: %s is not a multiple of 1/4" % [name, x])
	return Vector2(float(a[0]), float(a[1]))

func _mouse(button: int) -> void:
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = button
		ev.pressed = pressed
		Input.parse_input_event(ev)

func _key(code: Key) -> void:
	for pressed in [true, false]:
		var ev := InputEventKey.new()
		ev.physical_keycode = code
		ev.pressed = pressed
		Input.parse_input_event(ev)

# The settled record: SETTLE_MIN frames, then until two consecutive records agree.
func _settled(level: Node, extra: Dictionary) -> String:
	for k in SETTLE_MIN:
		await process_frame
	var last := SessionDigest.encode(SessionDigest.record(level, extra, _full))
	for k in SETTLE_MAX:
		await process_frame
		var now := SessionDigest.encode(SessionDigest.record(level, extra, _full))
		if now == last:
			return now
		last = now
	_fail("step %s did not settle" % str(extra))
	return last

# Fire, then the whole flight: the arrow sampled after every frame.
func _fire(level: Node, speedup_at: int) -> Dictionary:
	var arrow: Node2D = level.get_node_or_null("ArrowAnimator")
	_key(KEY_SPACE)
	var stream := []
	var frames := 0
	await process_frame
	while arrow != null and arrow.is_flying() and frames < MAX_FLIGHT_FRAMES:
		stream.append([arrow._arrow_position, arrow._arrow_direction])
		frames += 1
		if frames == speedup_at:
			_key(KEY_X)
		await process_frame
	if arrow != null and arrow.is_flying():
		_fail("flight did not land in %d frames" % MAX_FLIGHT_FRAMES)
	return {"frames": frames, "stream": stream if _full else SessionDigest.sha(stream),
		"end": stream.back()[0] if not stream.is_empty() else null}

# Movement actions held for `ticks` physics ticks (kinematic sessions): the
# player's position after every tick, whole (the kinematics judge reads it).
# physics_frame is emitted before the nodes' physics: the actions are pressed
# inside the first tick (a jump registers), and the player's physics stops at
# the start of tick ticks + 1.
func _hold(level: Node, actions: Array, ticks: int) -> Array:
	var player: Node2D = level.get_node("Player")
	player.set_physics_process(true)
	await physics_frame
	for a: String in actions:
		Input.action_press(a)
	var out := []
	for k in ticks:
		await physics_frame
		out.append(player.global_position)
	player.set_physics_process(false)
	for a: String in actions:
		Input.action_release(a)
	return out
