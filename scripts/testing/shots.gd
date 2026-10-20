extends SceneTree

# Stage 118: screenshots of every played level (the ones with a tour session),
# the GDScript scene against the Godot adapter (RicochetGame on the C API), in
# the same state: the player where the tour first puts it (else the level's
# start), the cursor where the tour first aims, the physics off; then with the
# cursor where the tour first plans, after a left click there (a plan entry). Writes <out>/<level>__<state>__{old,new}.png,
# a diff image where they differ, and report.md (differing pixels per pair).
# Needs a window, kept visible (the headless renderer draws nothing; an
# occluded window draws no frames on macOS, and the run waits for them):
#
#   godot --path . --fixed-fps 60 --resolution 1920x1080 -s res://scripts/testing/shots.gd -- --out <dir> [--only <substring>]

const SETTLE_FRAMES := 150   # the camera's smoothing comes to rest

var _out := ""

func _initialize() -> void:
	_run.call_deferred()

func _args() -> Dictionary:
	var out := {}
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		if a[i].begins_with("--"):
			var k := a[i].substr(2)
			if i + 1 < a.size() and not a[i + 1].begins_with("--"):
				out[k] = a[i + 1]
				i += 1
			else:
				out[k] = true
		i += 1
	return out

func _run() -> void:
	var args := _args()
	_out = String(args.get("out", "user://shots"))
	var only := String(args.get("only", ""))
	DirAccess.make_dir_recursive_absolute(_out)
	var rows: Array[String] = ["| level | state | differing pixels | of |", "|---|---|---|---|"]
	var names := Array(DirAccess.get_files_at("res://data/sessions")).filter(func(f): return f.ends_with("__tour.json"))
	names.sort()
	var worst := 0
	for f: String in names:
		var script: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/sessions/" + f))
		var level: String = script["level"]
		if only != "" and not level.contains(only):
			continue
		# the player where the tour first puts it; the cursor where it first aims,
		# and where it first plans (the cursor before its first left click)
		var at = null
		var aim = null
		var plan_at = null
		var last = null
		for s: Dictionary in script["steps"]:
			if s["op"] == "teleport" and at == null and aim == null:
				at = Vector2(s["at"][0], s["at"][1])
			if s["op"] == "cursor":
				last = Vector2(s["at"][0], s["at"][1])
				aim = last if aim == null else aim
			if s["op"] == "click" and s["button"] == "left" and plan_at == null and last != null:
				plan_at = last
		var states := {"aim": aim if aim != null else Vector2(960, 300)}
		if plan_at != null:
			states["planned"] = plan_at
		for state: String in states:
			var cur: Vector2 = states[state]
			var old := await _shot_old(level, at, cur, state == "planned")
			var new := await _shot_new(level, at, cur, state == "planned")
			var stem := "%s/%s__%s" % [_out, level.replace("/", "__"), state]
			old.save_png(stem + "__old.png")
			new.save_png(stem + "__new.png")
			var d := _diff(old, new)
			if d[0] > 0:
				d[1].save_png(stem + "__diff.png")
			worst = max(worst, d[0])
			rows.append("| %s | %s | %d | %d |" % [level, state, d[0], old.get_width() * old.get_height()])
			print("%s %s: %d differing pixels" % [level, state, d[0]])
	rows.append("")
	rows.append("Largest: %d differing pixels." % worst)
	var report := FileAccess.open(_out + "/report.md", FileAccess.WRITE)
	report.store_string("# The GDScript scenes against the Godot adapter (screenshots)\n\n" + "\n".join(rows) + "\n")
	report.close()
	quit(0)

func _click() -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_LEFT
	ev.pressed = true
	Input.parse_input_event(ev)
	Input.flush_buffered_events()
	var up := ev.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	Input.flush_buffered_events()

func _capture(node: Node) -> Image:
	for i in SETTLE_FRAMES:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	node.queue_free()
	await process_frame
	return img

func _shot_old(level: String, at, aim: Vector2, planned: bool) -> Image:
	Surface.reset_id_counter()
	var scene: Node = load("res://scenes/%s.tscn" % level).instantiate()
	root.add_child(scene)
	var player := scene.get_node("Player")
	player.set_physics_process(false)
	if at != null:
		player.global_position = at
	var cursor := scene.get_node("Cursor")
	cursor.set_process(false)
	cursor.global_position = aim
	await process_frame
	if planned:
		_click()
	return await _capture(scene)

func _shot_new(level: String, at, aim: Vector2, planned: bool) -> Image:
	var game := RicochetGame.new()
	game.level_path = "res://data/levels/%s.json" % level
	game.physics = false
	root.add_child(game)
	if at != null:
		game.teleport(at)
	game.aim(aim)
	await process_frame
	if planned:
		_click()
	return await _capture(game)

# [differing pixels, an image: the old shot dimmed, red where they differ]
func _diff(a: Image, b: Image) -> Array:
	var w := mini(a.get_width(), b.get_width())
	var h := mini(a.get_height(), b.get_height())
	var out := Image.create(w, h, false, Image.FORMAT_RGBA8)
	var n := absi(a.get_width() * a.get_height() - b.get_width() * b.get_height())
	for y in h:
		for x in w:
			var ca := a.get_pixel(x, y)
			if ca != b.get_pixel(x, y):
				n += 1
				out.set_pixel(x, y, Color.RED)
			else:
				out.set_pixel(x, y, ca.darkened(0.7))
	return [n, out]
