class_name LevelData
extends RefCounted

# Stage 115.2: a level's data (rc-level/1, docs/LEVEL_FORMAT.md) as the game's
# specs. The text is parsed natively (RcgLevel: correctly rounded numbers,
# the schema and the builder's checks); this turns the data into SurfaceSpec /
# SideSpec / SpawnTemplate resources in build order.

# The level's data (a Dictionary as in the file); push_error and {} when it
# cannot be read.
static func load_data(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		push_error("level data: no file %s" % path)
		return {}
	var r: Dictionary = RcgLevel.parse(FileAccess.get_file_as_string(path))
	if r.has("error"):
		push_error("level data %s: %s" % [path, r["error"]])
		return {}
	return r["level"]

static func vec(a: Array) -> Vector2:
	return Vector2(a[0], a[1])

static func rect(a: Array) -> Rect2:
	return Rect2(a[0], a[1], a[2], a[3])

# The surfaces in build order: [{"label": String or null, "spec": SurfaceSpec,
# "pre_transform": bool, "ids": Array}].
static func surfaces(data: Dictionary) -> Array:
	var out := []
	for d: Dictionary in data["surfaces"]:
		out.append({"label": d.get("label"), "spec": spec(d), "pre_transform": d.get("pre_transform", false),
			"ids": d["ids"]})
	return out

static func spec(d: Dictionary) -> SurfaceSpec:
	var s := SurfaceSpec.new()
	if d.has("circle"):
		s.full_circle = true
		s.center = vec(d["circle"]["center"])
		s.radius = d["circle"]["radius"]
	else:
		s.start = vec(d["start"])
		s.end = vec(d["end"])
		s.via = vec(d["via"])
	s.straight = d.get("straight", false)
	s.left = side(d.get("left"))
	s.right = side(d.get("right"))
	if d.get("resolver") != null:
		s.resolver_key = d["resolver"]["key"]
		for row: Array in d["resolver"]["table"]:
			s.resolver_table[row[0]] = [side(row[1].get("left")), side(row[1].get("right"))]
	for c: Dictionary in d.get("presence", []):
		s.presence.append({"key": c["key"], "values": c["values"]})
	s.player_solid = d.get("player_solid", false)
	return s

static func side(d) -> SideSpec:
	if d == null:
		return null
	var hits := []
	for h: Dictionary in d.get("hits", []):
		var hit := h.duplicate()
		if hit.get("template") != null:
			hit["template"] = template(hit["template"])
		hits.append(hit)
	return SideSpec.make(d.get("effect", {"type": "pass"}), d.get("interactive", false), hits)

static func template(d: Dictionary) -> SpawnTemplate:
	var t := SpawnTemplate.make(spec(d["surface"]), SpawnFrame.Mode[d.get("orientation", "CARRIER_TANGENT")],
		d.get("mirror_with_side", false), vec(d.get("offset", [0.0, 0.0])))
	t.trim = d.get("trim", true)
	return t
