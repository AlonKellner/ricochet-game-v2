extends GutTest

# Stage 116.4, a differential probe: the native crossings and pieces
# (rcg/geom.hpp) against Crossings / Pieces -- every level's pieces, and
# seeded random pairs of lines, arcs and full circles (with corners and
# T-junctions built in: endpoints exactly on the other segment), their
# crossings, positions, incidence and spans.

func _levels() -> Array[String]:
	var out: Array[String] = []
	for dir in ["", "test_levels/", "playground/"]:
		for f in DirAccess.get_files_at("res://data/levels/" + dir):
			if f.ends_with(".json"):
				out.append(dir + f.trim_suffix(".json"))
	out.sort()
	return out

func test_every_levels_pieces_are_the_games():
	for name in _levels():
		var native: Dictionary = RcgLevel.probe_pieces(FileAccess.get_file_as_string("res://data/levels/%s.json" % name))
		Surface.reset_id_counter()
		var scene: Node = load("res://scenes/%s.tscn" % name).instantiate()
		add_child(scene)
		var pieces: Pieces = scene.world.pieces()
		var rows := []
		for s: Surface in scene.world.surfaces:
			for p: Pieces.Piece in pieces.of(s):
				rows.append([s.id, p.index, p.segment.start.coords, p.segment.end.coords, p.segment.get_via().coords, p.span])
		assert_eq(native["pieces"], rows, name)
		assert_eq(native["crossings"], Array(pieces.crossings), name)
		assert_eq(native["portal_crossings"], pieces.portal_crossings.size(), name)
		remove_child(scene)
		scene.free()

var _rng := RandomNumberGenerator.new()

# Stage 119: RC_CAPTURE records each pair and the GDScript game's answers
# (tools/capture_gut_traces.py -> bench/gut_traces; rcg_tests "gut traces")
func _capture(row: Dictionary) -> void:
	var path := OS.get_environment("RC_CAPTURE")
	if path == "":
		return
	var f := FileAccess.open(path, FileAccess.READ_WRITE if FileAccess.file_exists(path) else FileAccess.WRITE)
	f.seek_end()
	row["key"] = OS.get_environment("RC_CAPTURE_KEY")
	row["kind"] = "crossings"
	f.store_string(SessionDigest.encode(row) + "\n")
	f.close()

func _pt() -> Vector2:
	return Vector2(_rng.randf_range(0, 1000), _rng.randf_range(0, 1000))

# A random segment: {start, end, via} (a line: via the midpoint) or {center, radius}.
func _spec(base := {}) -> Dictionary:
	var kind := _rng.randi_range(0, 2)
	var a: Vector2 = base.get("start", _pt())
	if kind == 0:
		var b := _pt()
		return {"start": a, "end": b, "via": Vector2((a.x + b.x) / 2.0, (a.y + b.y) / 2.0)}
	if kind == 1:
		return {"start": a, "end": _pt(), "via": _pt()}
	return {"center": _pt(), "radius": _rng.randf_range(20, 400)}

static func _seg(d: Dictionary) -> Segment:
	if d.has("center"):
		return Segment.full_from_carrier(GeneralizedCircle.from_circle(d["center"], d["radius"]))
	return Segment.from_coords(d["start"], d["end"], d["via"])

func test_random_crossings_are_the_games():
	_rng.seed = 1164
	var n := 0
	var crossings := 0
	for i in 3000:
		var a := _spec()
		var b := _spec()
		var sa := _seg(a)
		# a corner or a T-junction: b starts on a (its endpoint, or a crossing point of a and another segment)
		if i % 5 == 0 and not a.has("center"):
			b = _spec({"start": sa.end.coords})
		var sb := _seg(b)
		var points := PackedVector2Array([sa.start.coords, sa.end.coords, sa.get_via().coords, sb.start.coords, _pt()])
		var native: Dictionary = RcgLevel.probe_crossings(a, b, points)
		var gd_x := []
		for x in Crossings.of(sa, sb):
			gd_x.append([x["point"], x["ta"], x["tb"], x["enters"]])
		crossings += gd_x.size()
		var params := []
		var inc := []
		for p in points:
			params.append(Crossings.param(sa, p))
			inc.append(Crossings.incident(sa, p))
		var gd := {"crossings": gd_x, "params": params, "incident": inc, "span_a": Crossings.span(sa), "span_b": Crossings.span(sb)}
		_capture({"a": a, "b": b, "points": points, "gd": gd})
		if native != gd:
			assert_eq(native, gd, "pair %d: %s / %s" % [i, a, b])
			return
		n += 1
	assert_eq(n, 3000)
	assert_gt(crossings, 500, "the pairs cross often enough to matter")
