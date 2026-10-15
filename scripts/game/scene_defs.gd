class_name SceneDefs
extends RefCounted

# Scene defs (the JSON-able Dictionaries BeamScene.from_defs consumes) from the
# game's Surface objects, and the id -> Segment map the tracer attaches to its
# hits. This is the only place the beam solver's inputs are read from the game
# layer; the solver itself (native BeamScene / BeamTracer) never sees a Surface.

static func defs_from_surfaces(game_surfaces: Array) -> Array:
	var out: Array = []
	for s in game_surfaces:
		out.append(describe_surface(s))
	# portal pairs: the surface whose transform is this one's inverse
	var state := GameState.new()
	for i in game_surfaces.size():
		for key in ["left", "right"]:
			var cfg = out[i][key]
			if cfg == null or cfg["type"] != "portal":
				continue
			var e: Effect = game_surfaces[i].active_side_config(Side.Value.LEFT if key == "left" else Side.Value.RIGHT, state).effect
			for j in game_surfaces.size():
				var c2: SideConfig = game_surfaces[j].active_side_config(Side.Value.LEFT, state)
				if j != i and c2 != null and c2.effect != null and c2.effect == e.partner():
					cfg["pair"] = game_surfaces[j].id
					break
	return out

static func _v(p: Vector2):
	if is_inf(p.x) or is_inf(p.y):
		return null
	return [p.x, p.y]

static func describe_surface(s: Surface) -> Dictionary:
	var state := GameState.new()
	var carrier := s.segment.get_carrier()
	var circle = null
	if s.segment.full and not carrier.is_line():
		# exact carrier of a full circle (float64): center and radius
		circle = {"center": [-carrier.b / (2.0 * carrier.a), -carrier.c / (2.0 * carrier.a)], "radius": carrier.radius()}
	var out := {
		"circle": circle,
		"id": s.id,
		"start": _v(s.segment.start.coords),
		"end": _v(s.segment.end.coords),
		"via": _v(s.segment.via.coords),
		"full": s.segment.full,
		"line": carrier.is_line(),
		"left": null if s.resolver is CategoricalResolver else describe_config(s.active_side_config(Side.Value.LEFT, state)),
		"right": null if s.resolver is CategoricalResolver else describe_config(s.active_side_config(Side.Value.RIGHT, state)),
		"is_target": s.is_target,
		"player_solid": s.player_solid,
	}
	if s.resolver is CategoricalResolver:
		out["resolver"] = describe_resolver(s.resolver)
	return out

# A categorical resolver as scene defs: {key, table: {flag_key: {left, right}}}
# (null for a fixed surface).
static func describe_resolver(r: ConfigResolver):
	if not (r is CategoricalResolver):
		return null
	var table := {}
	for k in r.table:
		table[k] = {"left": describe_config(r.table[k][0]), "right": describe_config(r.table[k][1])}
	return {"key": r.state_key, "table": table}

static func describe_config(cfg: SideConfig):
	if cfg == null:
		return null
	var e: Effect = cfg.effect
	var d := {"interactive": cfg.interactive}
	if e == null:
		d["type"] = "pass"
	elif e is TerminalEffect:
		d["type"] = "terminal"
	elif e is CircleInversionEffect:
		d["type"] = "inversion"
	elif e is ReflectionEffect:
		d["type"] = "reflection"
	elif e is RigidMotionEffect:
		d["type"] = "portal"
		d["theta"] = e._theta
		d["d"] = [e._d.x, e._d.y]
	elif e is LineNormalProjection:
		d["type"] = "line_normal_projection"
		d["back"] = e._project_back
	elif e is CircleNormalProjection:
		d["type"] = "circle_normal_projection"
		d["back"] = e._project_back
	elif e is LineDirectionalProjection:
		d["type"] = "line_directional_projection"
		d["back"] = e._project_back
		d["normal"] = [e._normal.x, e._normal.y]
	elif e is CircleDirectionalProjection:
		d["type"] = "circle_directional_projection"
		d["back"] = e._project_back
		d["normal"] = [e._normal.x, e._normal.y]
	else:
		d["type"] = "unknown:" + str(e.get_script().get_global_name())
	if cfg.state_change != null:
		d["state"] = cfg.state_change.to_def()
	return d

# The game Segment of each surface, by id (TraceHit.segment).
static func segments_by_id(game_surfaces: Array) -> Dictionary:
	var out := {}
	for s in game_surfaces:
		out[s.id] = s.segment
	return out
