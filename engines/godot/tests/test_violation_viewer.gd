extends GutTest

# Stage 129: the violation viewer (RicochetGame.violations_path; make violations
# writes the sweeps' violations): each violation's level, player, plan (a piece
# by its span) and cursor, fixed; the arrow keys step through them.

const PATH := "user://test_violations.json"

var _game: RicochetGame

func before_each() -> void:
	var v := [
		{"level": "test_levels/crossings", "manifest": "test", "player_pos": [1380.0, 496.0], "cursor_pos": [1612.0, 354.0],
		 "plan": [{"surface_id": 17, "side": 0, "click": [1300.0, 345.0],
			"span": {"start": [1400.0, 390.0], "end": [1400.0, 210.0], "via": [1310.0, 300.0]}}],
		 "violation": "TEST: the first"},
		{"level": "main", "manifest": "test", "player_pos": [300.0, 300.0], "cursor_pos": [500.0, 300.0], "plan": [],
		 "violation": "TEST: the second"},
	]
	var f := FileAccess.open(PATH, FileAccess.WRITE)
	f.store_string(JSON.stringify(v))
	f.close()
	_game = RicochetGame.new()
	_game.violations_path = PATH
	add_child_autofree(_game)
	await wait_physics_frames(2)

func _record() -> Dictionary:
	return JSON.parse_string(_game.record())

# a record's float64 ("f" + the little-endian bytes in hex)
func _f64(h: String) -> float:
	return h.substr(1).hex_decode().decode_double(0)

func _key(code: Key) -> void:
	var ev := InputEventKey.new()
	ev.physical_keycode = code
	ev.keycode = code
	ev.pressed = true
	Input.parse_input_event(ev)
	Input.flush_buffered_events()

func test_a_violation_is_shown_as_recorded() -> void:
	var r := _record()
	assert_eq(r.plan.size(), 1)
	assert_eq(int(r.plan[0][0]), 17, "the planned surface")
	assert_true(int(r.plan[0][3]) >= 0, "the span is one of the world's pieces")
	assert_eq(_f64(r.player[0]), 1380.0)
	assert_eq(_f64(r.cursor[0]), 1612.0)
	await wait_physics_frames(3)
	assert_eq(_f64(_record().player[1]), 496.0, "the player stays put")

func test_the_arrow_keys_step_through_them() -> void:
	_key(KEY_RIGHT)
	await wait_physics_frames(2)
	var r := _record()
	assert_eq(r.plan.size(), 0, "the second: main, no plan")
	assert_eq(_f64(r.player[0]), 300.0)
	_key(KEY_LEFT)
	await wait_physics_frames(2)
	assert_eq(_record().plan.size(), 1, "back to the first")
