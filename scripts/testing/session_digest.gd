class_name SessionDigest
extends RefCounted

# The session replay golden's record (docs/research/session_digest.md): the
# game's observable state after a session step, as canonical JSON that any
# host can reproduce byte for byte -- sorted keys, no whitespace, every float
# as "f" + the hex of its IEEE-754 float64 bytes (little-endian; a Vector2
# component is its float32 value widened), ints and floats kept apart, Vector2
# and Color as arrays. Large parts are their sha256 (of the same encoding);
# `full` keeps them whole (a debugging dump: the judge diffs those).

static func encode(v) -> String:
	match typeof(v):
		TYPE_NIL:
			return "null"
		TYPE_BOOL:
			return "true" if v else "false"
		TYPE_INT:
			return str(v)
		TYPE_FLOAT:
			return "\"f" + PackedFloat64Array([v]).to_byte_array().hex_encode() + "\""
		TYPE_STRING, TYPE_STRING_NAME:
			return quote(String(v))
		TYPE_VECTOR2:
			return "[%s,%s]" % [encode(float(v.x)), encode(float(v.y))]
		TYPE_COLOR:
			return "[%s,%s,%s,%s]" % [encode(float(v.r)), encode(float(v.g)), encode(float(v.b)), encode(float(v.a))]
		TYPE_ARRAY, TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, \
				TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY, TYPE_PACKED_STRING_ARRAY, TYPE_PACKED_COLOR_ARRAY:
			var parts := PackedStringArray()
			for x in v:
				parts.append(encode(x))
			return "[" + ",".join(parts) + "]"
		TYPE_DICTIONARY:
			var keys: Array = v.keys()
			keys.sort_custom(func(a, b): return str(a) < str(b))
			var parts := PackedStringArray()
			for k in keys:
				parts.append(quote(str(k)) + ":" + encode(v[k]))
			return "{" + ",".join(parts) + "}"
	assert(false, "SessionDigest.encode: unsupported %s" % type_string(typeof(v)))
	return "\"?%s\"" % type_string(typeof(v))

# A JSON string: only the quote, the backslash and the control characters are
# escaped (\n, \t, \r by name, the others as \u00XX); everything else is UTF-8.
static func quote(s: String) -> String:
	var out := "\""
	for i in s.length():
		var c := s.unicode_at(i)
		if c == 0x22:
			out += "\\\""
		elif c == 0x5c:
			out += "\\\\"
		elif c == 0x0a:
			out += "\\n"
		elif c == 0x09:
			out += "\\t"
		elif c == 0x0d:
			out += "\\r"
		elif c < 0x20:
			out += "\\u%04x" % c
		else:
			out += String.chr(c)
	return out + "\""

static func sha(v) -> String:
	return encode(v).sha256_text()

# ---------------------------------------------------------------- the record

# The record of a scene (a level with a GameManager) after a step. `extra`
# holds the step's own fields (op, flight); `full` keeps the large parts whole.
static func record(level: Node, extra: Dictionary = {}, full := false) -> Dictionary:
	var gm: Node = level.get_node("GameManager")
	var player: Node2D = level.get_node_or_null("Player")
	var cursor: Node2D = level.get_node_or_null("Cursor")
	var renderer: Node2D = level.get_node_or_null("PathRenderer")
	var arrow: Node2D = level.get_node_or_null("ArrowAnimator")
	var camera: Node2D = level.get_node_or_null("Camera")
	var hud: Node = level.get_node_or_null("PlanHUD")
	var flying: bool = arrow != null and arrow.is_flying()
	var world: World = level.world
	var r := {
		"player": player.global_position if player else null,
		"cursor": cursor.global_position if cursor else null,
		"facing": float((player.get_node("Visual") as Node2D).rotation) if player else null,
		"camera": camera.global_position if camera != null and not flying else null,
		"display_mode": renderer.display_mode if renderer else -1,
		"gravity": level.gravity,
		"infinity": FeatureFlags.infinity_event(),
		"visibility_on": FeatureFlags.visibility(),
		"flying": flying,
		"plan": _plan(gm.plan),
		"hud": hud._label.text if hud != null else null,
		"flags": gm.game_state.flags,
		"targets": _sorted_keys(gm.targets_hit),
		"checkpoints": gm._checkpoints.depth(),
		"pole": gm._infinity_point(),
		"hover": _hover(gm) if not flying else null,
	}
	r["world"] = _world(world, full)
	var preview := _preview(renderer)
	r["preview_n"] = preview["n"]
	r["preview"] = preview["all"] if full else sha(preview["all"])
	var nodes := _nodes(level)
	r["overlays"] = nodes if full else sha(nodes)
	r["edge"] = _edge(level.get_node_or_null("EdgeOverlay"))
	r["visibility"] = _visibility(level.get_node_or_null("VisibilityRenderer"), full)
	var spawn := _spawn(level.get_node_or_null("SpawnPreview"))
	r["spawn_n"] = spawn["n"]
	r["spawn"] = spawn["all"] if full else sha(spawn["all"])
	if DrawRec.enabled:
		var d := draw(level)
		r["draw"] = d if full else sha(d)
	r.merge(extra, true)
	return r

# Stage 118: what the level's nodes draw (DrawRec's logs and the nodes drawn by
# their properties), each [key, z_index, position, modulate, calls], sorted by
# (z_index, key): the native painters' reference.
static func draw(level: Node) -> Array:
	var out := []
	for child in level.get_children():
		if not (child is CanvasItem) or child.is_queued_for_deletion() or not child.visible:
			continue
		var key := []
		var calls: Array = child.get_meta("draw_log", [])
		if child is SurfaceNode:
			key = ["surface", child.surface.id, child.piece_index]
		elif child is EdgeOverlayNode:
			key = ["edge"]
		elif child is VisibilityRenderer:
			key = ["visibility"]
		elif child is SpawnPreviewNode:
			key = ["spawn"]
		elif child is PathRenderer:
			key = ["path"]
		elif child.name == "ArrowAnimator":
			key = ["arrow"]
		elif child.name == "Cursor":
			key = ["cursor"]
		elif child.name == "Player":
			var v: Polygon2D = child.get_node("Visual")
			key = ["player"]
			calls = [["polygon2d", v.polygon, v.color, float(v.rotation)]]
		elif child is ColorRect:
			key = ["background"]
			calls = [["rect", child.position, child.size, child.color]]
		elif child is Control and child.has_node("Label"):
			continue
		else:
			continue
		out.append([key, child.z_index, child.position, child.modulate, calls])
	var hud := level.get_node_or_null("PlanHUD")
	if hud != null:
		out.append([["hud"], 0, Vector2.ZERO, Color.WHITE, [["label", hud._label.text]]])
	out.sort_custom(func(a, b): return a[1] < b[1] or (a[1] == b[1] and encode(a[0]) < encode(b[0])))
	return out

static func _sorted_keys(d: Dictionary) -> Array:
	var ks: Array = d.keys()
	ks.sort()
	return ks

static func _plan(plan: PlanManager) -> Array:
	var out := []
	for e: PlanManager.PlanEntry in plan.entries:
		out.append([e.surface_id, int(e.side), e.click, e.piece, e.span])
	return out

# What the cursor is over (the click detector's answer).
static func _hover(gm: Node) -> Dictionary:
	if gm._cursor == null:
		return {}
	var res: Dictionary = gm._detect_click()
	if res.is_empty():
		return {}
	if res.has("infinity"):
		return {"infinity": true}
	var piece = res.get("piece")
	return {"surface": res.surface.id, "side": int(res.side), "piece": piece.index if piece != null else -1}

static func _world(world: World, full: bool) -> Dictionary:
	if world == null:
		return {}
	var ids := []
	for s: Surface in world.surfaces:
		ids.append(s.id)
	var defs := SceneDefs.defs_from_surfaces(world.surfaces)
	var pieces := world.pieces()
	var ps := []
	for s: Surface in world.surfaces:
		for p: Pieces.Piece in pieces.of(s):
			ps.append([s.id, p.index, p.segment.start.coords, p.segment.end.coords, p.segment.get_via().coords, p.span])
	var all_pieces := {"pieces": ps, "crossings": pieces.crossings}
	return {"ids": ids, "generations": world.generations, "defs": defs if full else sha(defs),
		"pieces": all_pieces if full else sha(all_pieces)}

static func _steps(steps: Array) -> Array:
	var out := []
	for s: TraceStep in steps:
		out.append([s.start, s.end, s.via, s.is_arc_step, s.type, s.surface_id, s.hit_side, s.hit_on_segment,
			s.after_portal, s.ghost])
	return out

static func _path(p: TracePath):
	if p == null:
		return null
	return {"steps": _steps(p.steps), "cursor_index": p.cursor_index, "truncated": p.truncated}

static func _events(events: Array) -> Array:
	var out := []
	for e: BeamSolverEvent in events:
		out.append([e.kind, e.surface_id, e.applied])
	return out

static func _preview(renderer: Node) -> Dictionary:
	if renderer == null:
		return {"n": [], "all": null}
	var physical: TracePath = renderer._traced_path
	var planned: TracePath = renderer._planned_path
	var merged: Array = renderer._merged_steps
	var pair: BeamTracerPair = renderer.last_pair
	var result: PrecisionLadderResult = renderer.last_beam_result
	var all := {
		"physical": _path(physical),
		"planned": _path(planned),
		"merged": _steps(merged),
		"hits": renderer.get_physical_hits() if physical != null else {},
		"final_flags": pair.final_flags if pair != null else null,
		"cursor_point_agrees": pair.cursor_point_agrees if pair != null else null,
		"events": [_events(result.physical), _events(result.planned)] if result != null else null,
		"stops": renderer.truncation_markers(),
	}
	var n := [physical.steps.size() if physical else -1, planned.steps.size() if planned else -1, merged.size(),
		planned.cursor_index if planned else -1]
	return {"n": n, "all": all}

static func _overlay(o: ChevronOverlay) -> Array:
	return [o.side, o.has_incoming, o.has_outgoing, o.outgoing_side, o.incoming_color, o.outgoing_color,
		o.gradient_color, o.piece]

# Every surface node (one per piece), keyed by (surface id, piece index): its
# hover overlays, plan overlays and plan entry numbers.
static func _nodes(level: Node) -> Array:
	var rows := []
	for child in level.get_children():
		if not (child is SurfaceNode) or child.is_queued_for_deletion():
			continue
		var n: SurfaceNode = child
		rows.append([n.surface.id, n.piece_index, n._hover_overlays.map(_overlay), n._plan_overlays.map(_overlay),
			Array(n._plan_indices)])
	rows.sort_custom(func(a, b): return a[0] < b[0] or (a[0] == b[0] and a[1] < b[1]))
	return rows

static func _edge(edge: Node) -> Variant:
	if edge == null:
		return null
	return {"hovered": edge.is_hovered(), "point": edge.get_point(), "marks": edge.get_marks()}

static func _visibility(vis: Node, full: bool) -> Variant:
	if vis == null:
		return null
	var region = vis.region
	if region == null or not region.is_supported():
		return {"region": region != null, "supported": false}
	var fill := {"points": vis._fill_points, "indices": vis._fill_indices}
	var whiskers: Array = vis._whiskers
	return {"region": true, "supported": true, "n": [vis._fill_points.size(), vis._fill_indices.size(), whiskers.size()],
		"fill": fill if full else sha(fill), "whiskers": whiskers if full else sha(whiskers)}

static func _spawn(preview: Node) -> Dictionary:
	if preview == null:
		return {"n": [], "all": null}
	var placed := []
	for p: Dictionary in preview.placed:
		placed.append({"channel": p["channel"], "generation": p["generation"], "spec": data(p["spec"])})
	var vanishing := []
	for s: Surface in preview.vanishing:
		vanishing.append(s.id)
	return {"n": [placed.size(), vanishing.size()], "all": {"placed": placed, "vanishing": vanishing}}

# Specs as plain data (the level format's spelling; building a Surface would
# consume ids): SurfaceSpec, SideSpec and SpawnTemplate by their exported
# fields, recursively through dictionaries and arrays.
static func data(v) -> Variant:
	if v is SurfaceSpec:
		var s: SurfaceSpec = v
		return {"start": s.start, "end": s.end, "via": s.via, "full_circle": s.full_circle, "straight": s.straight,
			"center": s.center, "radius": s.radius, "left": data(s.left), "right": data(s.right),
			"resolver_key": s.resolver_key, "resolver_table": data(s.resolver_table), "presence": data(s.presence),
			"player_solid": s.player_solid}
	if v is SideSpec:
		return {"effect": data(v.effect), "hits": data(v.hits), "interactive": v.interactive}
	if v is SpawnTemplate:
		return {"surface": data(v.surface), "orientation": int(v.orientation), "mirror_with_side": v.mirror_with_side,
			"offset": v.offset, "trim": v.trim}
	if v is Dictionary:
		var out := {}
		for k in v:
			out[k] = data(v[k])
		return out
	if v is Array:
		return v.map(data)
	return v
