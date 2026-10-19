extends GutTest

# Stage 116.3, a differential probe: the native surface model (rcg/model.hpp)
# against the GDScript game's surfaces, on every level and every flag value
# the level mentions (its resolvers, presence and flag writes): per surface,
# presence; per side, the active config (scene defs), its landing writes and
# its overlay link.

func _levels() -> Array[String]:
	var out: Array[String] = []
	for dir in ["", "test_levels/", "playground/"]:
		for f in DirAccess.get_files_at("res://data/levels/" + dir):
			if f.ends_with(".json"):
				out.append(dir + f.trim_suffix(".json"))
	out.sort()
	return out

# A flag value from its table spelling.
static func _value(token: String):
	if token == "true":
		return true
	if token == "false":
		return false
	if token.is_valid_float():
		return float(token)
	return token

# Every flag key the level mentions, with the values it mentions.
static func _mentions(data: Dictionary) -> Dictionary:
	var out := {}
	var add := func(k: String, v) -> void:
		if not out.has(k):
			out[k] = []
		if v is String and v == "DEFAULT":
			return
		if not out[k].has(v):
			out[k].append(v)
	var side := func(sd) -> void:
		if sd == null:
			return
		for h in sd.get("hits", []):
			match h["kind"]:
				"state":
					add.call(h["key"], h.get("value", true) if h.get("op", "set") == "set" else true)
				"reset":
					for c in h["channels"]:
						add.call("chan:" + c, "armed")
				"spawn":
					add.call("chan:" + h["channel"], "pending")
					add.call("chan:" + h["channel"], "g1")
	for s: Dictionary in data["surfaces"]:
		side.call(s.get("left"))
		side.call(s.get("right"))
		if s.get("resolver") != null:
			for row in s["resolver"]["table"]:
				add.call(s["resolver"]["key"], _value(row[0]))
				side.call(row[1].get("left"))
				side.call(row[1].get("right"))
		for c in s.get("presence", []):
			for v in c["values"]:
				add.call(c["key"], _value(v))
	return out

static func _flag_sets(mentions: Dictionary) -> Array:
	var sets: Array = [{}]
	var all := {}
	for k in mentions:
		for v in mentions[k]:
			sets.append({k: v})
		if not mentions[k].is_empty():
			all[k] = mentions[k][0]
	sets.append(all)
	return sets

static func _ref(link: SideLink):
	return null if link == null else [link.surface.id, int(link.side)]

func _gd_probe(surfaces: Array, flags: Dictionary) -> Array:
	var state := GameState.new(flags)
	var out := []
	for s: Surface in surfaces:
		var sides := []
		for side in [Side.Value.LEFT, Side.Value.RIGHT]:
			var cfg := s.active_side_config(side, state)
			var landing := []
			if cfg != null:
				for h: HitEffect in cfg.hit_effects:
					for w: StateChange in h.landing_writes(s.id):
						landing.append(w.to_def())
			var link := s.overlay_link(side, state)
			sides.append({"active": SceneDefs.describe_config(cfg, s.id), "landing": landing,
				"link": null if link == null else [_ref(link), _ref(link.outgoing)]})
		out.append({"id": s.id, "present": s.present(flags), "sides": sides})
	return out

func test_the_native_surface_model_is_the_games():
	var n := 0
	for name in _levels():
		var text := FileAccess.get_file_as_string("res://data/levels/%s.json" % name)
		var data: Dictionary = RcgLevel.parse(text)["level"]
		Surface.reset_id_counter()
		var scene: Node = load("res://scenes/%s.tscn" % name).instantiate()
		add_child(scene)
		for flags in _flag_sets(_mentions(data)):
			var native: Array = RcgLevel.probe_model(text, flags)
			var gd := _gd_probe(scene.world.surfaces, flags)
			assert_eq(native.size(), gd.size(), name)
			for i in mini(native.size(), gd.size()):
				if native[i] != gd[i]:
					assert_eq(native[i], gd[i], "%s flags %s" % [name, flags])
					break
			n += 1
		remove_child(scene)
		scene.free()
	assert_gt(n, 60, "probed flag sets")
