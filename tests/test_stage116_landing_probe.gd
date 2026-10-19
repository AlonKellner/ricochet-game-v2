extends GutTest

# Stage 116.4, a differential probe: the native landing fold (rcg/landing.hpp:
# the deferred writes, the spawns placed through the spawn frame and mapped,
# trimmed by what is present, the generations) against LandingFold.fold, on the
# levels that spawn, with seeded random hit records on their hosts (random
# points, travel tangents and sides; the shot's final flags from the hits'
# own in-shot writes). The placed specs are compared as the session digest
# records them (SessionDigest.data, encoded).

const LEVELS := ["test_levels/hit_effects", "test_levels/crossings"]

var _rng := RandomNumberGenerator.new()

func _unit() -> Vector2:
	return Vector2.from_angle(_rng.randf_range(-PI, PI))

func test_the_native_landing_is_the_games():
	_rng.seed = 1165
	var shots := 0
	var placed := 0
	for name in LEVELS:
		var text := FileAccess.get_file_as_string("res://data/levels/%s.json" % name)
		Surface.reset_id_counter()
		var scene: Node = load("res://scenes/%s.tscn" % name).instantiate()
		add_child(scene)
		var world: World = scene.world
		# the interactive hosts (their sides fire), spawning ones first
		var hosts: Array = world.surfaces.filter(func(s: Surface): return s.all_side_configs().any(
			func(c): return c != null and c.interactive))
		for shot in 150:
			var hits := []
			var flags := {}
			for k in _rng.randi_range(1, 4):
				var s: Surface = hosts[_rng.randi_range(0, hosts.size() - 1)]
				var side := _rng.randi_range(0, 1)
				var p: Vector2 = s.segment.start.coords.lerp(s.segment.end.coords, _rng.randf_range(0.1, 0.9))
				hits.append({"surface_id": s.id, "side": side, "fired": _rng.randf() < 0.9, "point": p, "tangent": _unit(),
					"flags_before": flags.duplicate(true)})
				var cfg := s.active_side_config(Side.Value.LEFT if side == 0 else Side.Value.RIGHT, GameState.new(flags))
				if cfg != null:
					for w: StateChange in cfg.in_shot_writes(s.id):
						flags = w.apply(flags)
			var gens := {"a": 1} if shot % 3 == 0 else {}
			var gd := LandingFold.fold(hits, world.by_id(), flags, gens)
			var gd_placed := []
			for p: Dictionary in gd["placed"]:
				gd_placed.append({"channel": p["channel"], "generation": p["generation"], "spec": SessionDigest.data(p["spec"])})
			var native: Dictionary = RcgLevel.probe_fold(text, hits, flags, gens)
			if native["placed"] != SessionDigest.encode(gd_placed) or native["flags"] != gd["flags"] or native["generations"] != gd["generations"]:
				assert_eq(native["placed"], SessionDigest.encode(gd_placed), "%s shot %d: placed" % [name, shot])
				assert_eq(native["flags"], gd["flags"], "%s shot %d: flags" % [name, shot])
				assert_eq(native["generations"], gd["generations"], "%s shot %d" % [name, shot])
				return
			shots += 1
			placed += gd_placed.size()
		remove_child(scene)
		scene.free()
	assert_eq(shots, 300)
	assert_gt(placed, 60, "spawns placed")
