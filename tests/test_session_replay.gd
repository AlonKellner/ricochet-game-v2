extends GutTest

# The Godot host plays the session golden (data/sessions -> bench/sessions,
# docs/research/session_digest.md) through Godot's own input: mouse clicks
# and key presses parsed as InputEvents reach RicochetGame._input, which maps
# them onto the game's events -- every record equals the golden's (but the
# step's own fields: the flight, the trajectory). One teleport session in 4
# (the native replays check every session: ctest capi_sessions_*, make pyhost).

const STEP_FIELDS := ["i", "op", "action", "flight", "trajectory"]
const KEYS := {"undo": KEY_Z, "reset": KEY_R, "clear_plan": KEY_C, "cycle_display": KEY_TAB,
	"toggle_gravity": KEY_G, "toggle_visibility": KEY_V, "any": KEY_X, "fire": KEY_SPACE}

func _sessions() -> Array[String]:
	var out: Array[String] = []
	var names := Array(DirAccess.get_files_at("res://data/sessions")).filter(func(f): return f.ends_with(".json"))
	names.sort()
	for i in names.size():
		var script: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/sessions/" + names[i]))
		if script["kind"] == "teleport" and i % 4 == 0:
			out.append(names[i].trim_suffix(".json"))
	return out

func _press(ev: InputEvent) -> void:
	ev.pressed = true
	Input.parse_input_event(ev)
	Input.flush_buffered_events()
	var up: InputEvent = ev.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	Input.flush_buffered_events()

func _key(code: Key) -> void:
	var ev := InputEventKey.new()
	ev.physical_keycode = code
	ev.keycode = code
	_press(ev)

func _click(right: bool) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_RIGHT if right else MOUSE_BUTTON_LEFT
	_press(ev)

func _record(game: RicochetGame) -> Dictionary:
	var r: Dictionary = JSON.parse_string(game.record())
	for k in STEP_FIELDS:
		r.erase(k)
	return r

func _golden(line: String) -> Dictionary:
	var r: Dictionary = JSON.parse_string(line)
	for k in STEP_FIELDS:
		r.erase(k)
	return r

func test_the_host_plays_the_golden() -> void:
	var names := _sessions()
	assert_gt(names.size(), 5, "sessions sampled")
	for name in names:
		var script: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/sessions/%s.json" % name))
		var golden := FileAccess.get_file_as_string("res://bench/sessions/%s.jsonl" % name).strip_edges().split("\n")
		var game := RicochetGame.new()
		game.level_path = "res://data/levels/%s.json" % script["level"]
		var sw: Dictionary = script.get("switches", {})
		game.infinity = int(sw["infinity"]) if sw.has("infinity") else -1
		game.visibility = int(sw["visibility"]) if sw.has("visibility") else -1
		game.physics = false   # teleport sessions: the player moves only by teleport
		game.aim(Vector2.ZERO)
		add_child(game)
		await wait_process_frames(1)
		var bad := ""
		if _record(game) != _golden(golden[0]):
			bad = "start"
		var steps: Array = script["steps"]
		for i in steps.size():
			if bad != "":
				break
			var step: Dictionary = steps[i]
			match step["op"]:
				"teleport":
					game.teleport(Vector2(step["at"][0], step["at"][1]))
				"cursor":
					game.aim(Vector2(step["at"][0], step["at"][1]))
				"click":
					_click(step["button"] == "right")
				"key":
					_key(KEYS[step["action"]])
				"fire":
					_key(KEY_SPACE)
					while game.is_flying():
						await wait_process_frames(1)
			if _record(game) != _golden(golden[i + 1]):
				bad = "step %d (%s)" % [i + 1, step["op"]]
		assert_eq(bad, "", "%s: the host's record equals the golden's" % name)
		remove_child(game)
		game.free()
