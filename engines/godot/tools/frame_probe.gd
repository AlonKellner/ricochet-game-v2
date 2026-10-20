extends SceneTree

# Stage 120: what a frame of the Godot host costs (docs/TDD_25_*): a level's
# RicochetGame through phases of frames -- still, the cursor circling, the
# player circling, walking -- timing each frame from one _process to the next
# (the host's sync, the game, the redraws). Frame pacing is off, so a frame
# costs what its work costs. Evidence, not a gate.
#
#   godot --headless --path engines/godot -s res://tools/frame_probe.gd -- res://data/levels/main.json [frames]

var _game: RicochetGame
var _frames := 120
var _phase := -1
var _k := 0
var _last := 0
var _ms: Array[float] = []
var _home := Vector2.ZERO
const PHASES := ["still", "cursor_orbit", "player_orbit", "walk"]

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	OS.low_processor_usage_mode_sleep_usec = 0
	Engine.max_fps = 0
	_game = RicochetGame.new()
	_game.level_path = args[0] if args.size() > 0 else "res://data/levels/main.json"
	_frames = int(args[1]) if args.size() > 1 else 120
	_game.aim(Vector2(1200, 400))
	root.add_child(_game)
	print("%-13s %8s %8s %8s" % ["phase", "mean ms", "p95", "max"])

func _process(_delta: float) -> bool:
	var now := Time.get_ticks_usec()
	if _phase >= 0 and _k > 0:
		_ms.append((now - _last) / 1000.0)
	_last = now
	if _phase < 0 or _k >= _frames:
		_report()
		_phase += 1
		_k = 0
		_ms.clear()
		Input.action_release("move_right")
		if _phase >= PHASES.size():
			return true
		_home = Vector2(960, 540)
		_game.teleport(_home)
		_game.physics = PHASES[_phase] == "walk"
		if PHASES[_phase] == "walk":
			Input.action_press("move_right")
	var a := TAU * _k / _frames
	match PHASES[maxi(_phase, 0)]:
		"cursor_orbit":
			_game.aim(_home + Vector2(cos(a), sin(a)) * 420.0)
		"player_orbit":
			_game.teleport(_home + Vector2(cos(a), sin(a)) * 60.0)
	_k += 1
	return false

func _report() -> void:
	if _phase < 0 or _ms.is_empty():
		return
	var s := _ms.duplicate()
	s.sort()
	var mean := 0.0
	for x in s:
		mean += x / s.size()
	print("%-13s %8.3f %8.3f %8.3f" % [PHASES[_phase], mean, s[int((s.size() - 1) * 0.95)], s[-1]])
