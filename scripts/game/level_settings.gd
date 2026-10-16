extends Node2D

@export var gravity := Vector2(0, 0)
@export var room_rect := Rect2(560, 240, 800, 600)
@export var level_bounds := Rect2(0, 0, 1920, 1080)
@export var build_room := true

@export var passthrough_lines: Array[Vector4] = []
@export var block_lines: Array[Vector4] = []
@export var mirror_lines: Array[Vector4] = []
@export var mirror_right_lines: Array[Vector4] = []
@export var mirror_both_lines: Array[Vector4] = []
@export var inversion_left_arcs: PackedFloat64Array = PackedFloat64Array()
@export var full_inversion_arcs: PackedFloat64Array = PackedFloat64Array()
@export var semi_reflective_arcs: PackedFloat64Array = PackedFloat64Array()
@export var reflective_arcs: PackedFloat64Array = PackedFloat64Array()
@export var full_reflective_arcs: PackedFloat64Array = PackedFloat64Array()
@export var normal_projection_lines: Array[Vector4] = []
@export var normal_projection_back_lines: Array[Vector4] = []
@export var normal_projection_arcs: PackedFloat64Array = PackedFloat64Array()
@export var normal_projection_back_arcs: PackedFloat64Array = PackedFloat64Array()
@export var circle_directional_arcs: PackedFloat64Array = PackedFloat64Array()
@export var circle_directional_back_arcs: PackedFloat64Array = PackedFloat64Array()
@export var directional_projection_lines: PackedFloat64Array = PackedFloat64Array()
@export var directional_projection_back_lines: PackedFloat64Array = PackedFloat64Array()
@export var portal_lines: PackedFloat64Array = PackedFloat64Array()
@export var portal_arcs: PackedFloat64Array = PackedFloat64Array()
# State flags (Stage 85): togglers toggle `state_key` (both sides, when hit
# on-segment; plannable); gates block unless it is true; flag mirrors reflect
# (both sides) only while it is true.
@export var state_key := "open"
@export var toggle_lines: Array[Vector4] = []
@export var gate_lines: Array[Vector4] = []
@export var flag_mirror_lines: Array[Vector4] = []
# Ghost togglers (Stage 86): each hit toggles the per-shot ghost flag; while
# ghosted the arrow passes every other surface, walls included.
@export var ghost_toggler_lines: Array[Vector4] = []
# Pre-transform (Stage 101): inversion circles (cx, cy, r triples) the shot
# starts "after": the arrow's natural paths are circles through their image of
# infinity, which is drawn and plannable like a surface when on screen.
@export var pre_transform_inversions: PackedFloat64Array = PackedFloat64Array()
# Stage 107: surfaces as data -- any carrier, path effect, hit effects and
# presence (SurfaceSpec), built after the legacy arrays.
@export var surface_specs: Array[SurfaceSpec] = []

var surfaces: Array[Surface] = []
# Stage 107: the surfaces over time (spawned groups join and leave); `surfaces`
# is always world.surfaces once the level is built
var world: World = null
# Stage 108: one SurfaceNode per piece (GAME_SPEC 9.7); a surface without
# crossings keeps its single node, named as before
var _nodes := {}    # surface id -> Array[SurfaceNode]
var _prefix := {}   # surface id -> its node name prefix (no entry: not drawn)

func _ready() -> void:
	if build_room:
		for wall in RoomBuilder.create_room_surfaces(room_rect):
			_add_surface(wall, "Surface")
	for line_def in block_lines:
		var surf := RoomBuilder.create_block_surface(
			Vector2(line_def.x, line_def.y),
			Vector2(line_def.z, line_def.w),
			Vector2((line_def.x + line_def.z) / 2.0, (line_def.y + line_def.w) / 2.0))
		_add_surface(surf, "Block")
	# every legacy array is a row of surface specs (Stage 107), in the legacy
	# order (the ids follow it)
	for row in _specs_before_edge():
		_add_spec(row[1], row[0])
	# the level's edge walls (pass-through, not interactive), along level_bounds
	var b := level_bounds
	var screen_bounds: Array[Vector4] = [
		Vector4(b.position.x, b.position.y, b.end.x, b.position.y),
		Vector4(b.end.x, b.position.y, b.end.x, b.end.y),
		Vector4(b.end.x, b.end.y, b.position.x, b.end.y),
		Vector4(b.position.x, b.end.y, b.position.x, b.position.y),
	]
	for line_def in screen_bounds:
		var config := SideConfig.new(null, false)
		var surf := Surface.new(_seg_from_v4(line_def), config, config, false, false)
		surfaces.append(surf)
	for row in _specs_after_edge():
		_add_spec(row[1], row[0])
	for spec in surface_specs:
		_add_spec(spec, "Spec")
	for i in range(0, pre_transform_inversions.size(), 3):
		var pre := Surface.pre_transform_circle(Vector2(pre_transform_inversions[i], pre_transform_inversions[i + 1]),
			pre_transform_inversions[i + 2])
		_add_surface(pre, "PreTransform")
	world = World.new(surfaces)
	surfaces = world.surfaces
	_sync_nodes()
	if not world.pieces().portal_crossings.is_empty():
		push_error("crossing a portal (not split, GAME_SPEC 9.7): " + ", ".join(world.pieces().portal_crossings))
	# the level bounds' edge: hover band and planned infinity entries (Stage 88)
	var edge := Node2D.new()
	edge.set_script(load("res://scripts/game/edge_overlay_node.gd"))
	edge.name = "EdgeOverlay"
	add_child(edge)
	edge.setup(level_bounds)
	# the visibility region (Stage 110) under the preview
	if get_node_or_null("PathRenderer") != null:
		var vis := VisibilityRenderer.new()
		vis.name = "VisibilityRenderer"
		add_child(vis)
	# the spawn preview (Stage 107) where something can spawn
	if surface_specs.any(func(s: SurfaceSpec): return not s.spawn_channels().is_empty()):
		var preview := SpawnPreviewNode.new()
		preview.name = "SpawnPreview"
		add_child(preview)

func _add_surface(surf: Surface, prefix: String) -> void:
	surfaces.append(surf)
	_prefix[surf.id] = prefix

# The nodes follow the world's pieces: a surface whose pieces changed (a
# crossing came or went) gets new nodes, one that left the world loses them.
func _sync_nodes() -> void:
	var pieces := world.pieces()
	var live := {}
	for s: Surface in world.surfaces:
		if not _prefix.has(s.id):
			continue
		live[s.id] = true
		var want: Array = pieces.of(s)
		var have: Array = _nodes.get(s.id, [])
		if _same_pieces(have, want):
			continue
		_free_nodes(s.id)
		var made: Array = []
		for p: Pieces.Piece in want:
			var node := Node2D.new()
			node.set_script(load("res://scripts/game/surface_node.gd"))
			node.name = "%s_%d" % [_prefix[s.id], s.id] if p.index < 0 else "%s_%d_p%d" % [_prefix[s.id], s.id, p.index]
			add_child(node)
			node.setup(s, p)
			made.append(node)
		_nodes[s.id] = made
	for id in _nodes.keys():
		if not live.has(id):
			_free_nodes(id)

static func _same_pieces(have: Array, want: Array) -> bool:
	if have.size() != want.size():
		return false
	for i in have.size():
		var p: Pieces.Piece = want[i]
		if have[i].piece_index != p.index or have[i].segment.start.coords != p.segment.start.coords \
				or have[i].segment.end.coords != p.segment.end.coords:
			return false
	return true

func _free_nodes(id: int) -> void:
	for node: Node in _nodes.get(id, []):
		remove_child(node)
		node.queue_free()
	_nodes.erase(id)

# A structural change of the world (World.land / restore): spawned surfaces
# get nodes, dropped ones lose theirs, pieces follow; `surfaces` is the
# world's new array.
func apply_world_change(change: Dictionary) -> void:
	for s: Surface in change["added"]:
		if not _prefix.has(s.id):
			_prefix[s.id] = "Spawned"
	surfaces = world.surfaces
	_sync_nodes()

# A spec's surfaces: one, or a portal's pair.
func _add_spec(spec: SurfaceSpec, prefix: String) -> void:
	var errors := spec.validate()
	if not errors.is_empty():
		assert(false, "%s: %s" % [prefix, ", ".join(errors)])
		push_error("%s: %s" % [prefix, ", ".join(errors)])
		return
	var built := spec.build()
	if built.size() == 2:
		_add_surface(built[0], "PortalSrc")
		_add_surface(built[1], "PortalTgt")
	else:
		_add_surface(built[0], prefix)

static func _side(type := "pass", interactive := false, params := {}, hits: Array = []) -> SideSpec:
	var e := {"type": type}
	e.merge(params)
	return SideSpec.make(e, interactive, hits)

static func _a(v: Vector4) -> Vector2:
	return Vector2(v.x, v.y)

static func _b(v: Vector4) -> Vector2:
	return Vector2(v.z, v.w)

# Six floats: start, end, via.
static func _arc6(arr: PackedFloat64Array, i: int, l: SideSpec, r: SideSpec, solid := false) -> SurfaceSpec:
	return SurfaceSpec.arc(Vector2(arr[i], arr[i + 1]), Vector2(arr[i + 2], arr[i + 3]), Vector2(arr[i + 4], arr[i + 5]), l, r, solid)

# Four floats: start, end; via at their midpoint (taken in float64).
static func _line4(arr: PackedFloat64Array, i: int, l: SideSpec, r: SideSpec) -> SurfaceSpec:
	return SurfaceSpec.arc(Vector2(arr[i], arr[i + 1]), Vector2(arr[i + 2], arr[i + 3]),
		Vector2((arr[i] + arr[i + 2]) / 2.0, (arr[i + 1] + arr[i + 3]) / 2.0), l, r)

# Three floats: a full circle (center, radius).
static func _circle3(arr: PackedFloat64Array, i: int, l: SideSpec, r: SideSpec, solid := false) -> SurfaceSpec:
	return SurfaceSpec.circle(Vector2(arr[i], arr[i + 1]), arr[i + 2], l, r, solid)

# The legacy arrays before the edge walls: [prefix, spec] in build order.
func _specs_before_edge() -> Array:
	var out := []
	var off := _side()
	var refl := _side("reflection", true)
	var inv := _side("inversion", true)
	for v in mirror_lines:
		out.append(["Mirror", SurfaceSpec.line(_a(v), _b(v), refl, off)])
	for v in mirror_right_lines:
		out.append(["MirrorR", SurfaceSpec.line(_a(v), _b(v), off, refl)])
	for v in mirror_both_lines:
		out.append(["MirrorB", SurfaceSpec.line(_a(v), _b(v), refl, refl)])
	for i in range(0, inversion_left_arcs.size(), 6):
		out.append(["Inversion", _arc6(inversion_left_arcs, i, inv, off, true)])
	for i in range(0, full_inversion_arcs.size(), 3):
		out.append(["FullInversion", _circle3(full_inversion_arcs, i, inv, inv, true)])
	for i in range(0, semi_reflective_arcs.size(), 6):
		out.append(["SemiReflArc", _arc6(semi_reflective_arcs, i, refl, off)])
	for i in range(0, reflective_arcs.size(), 6):
		out.append(["ReflArc", _arc6(reflective_arcs, i, refl, refl)])
	for i in range(0, full_reflective_arcs.size(), 3):
		out.append(["FullReflArc", _circle3(full_reflective_arcs, i, refl, refl)])
	for back in [false, true]:
		var lnp := _side("line_normal_projection", true, {"back": back})
		for v in (normal_projection_back_lines if back else normal_projection_lines):
			out.append(["ProjectionBack" if back else "Projection", SurfaceSpec.line(_a(v), _b(v), lnp, lnp)])
	for back in [false, true]:
		var cnp := _side("circle_normal_projection", true, {"back": back})
		var arr := normal_projection_back_arcs if back else normal_projection_arcs
		for i in range(0, arr.size(), 6):
			out.append(["CircleProjectionBack" if back else "CircleProjection", _arc6(arr, i, cnp, cnp)])
	for back in [false, true]:
		var arr := circle_directional_back_arcs if back else circle_directional_arcs
		for i in range(0, arr.size(), 8):
			var cd := _side("circle_directional_projection", true, {"back": back, "normal": [arr[i + 6], arr[i + 7]]})
			out.append(["CircleDirBack" if back else "CircleDir", _arc6(arr, i, cd, cd)])
	for back in [false, true]:
		var arr := directional_projection_back_lines if back else directional_projection_lines
		for i in range(0, arr.size(), 6):
			var ld := _side("line_directional_projection", true, {"back": back, "normal": [arr[i + 4], arr[i + 5]]})
			out.append(["LineDirBack" if back else "LineDir", _line4(arr, i, ld, ld)])
	for i in range(0, portal_lines.size(), 7):
		var pl := _side("portal", true, {"theta": portal_lines[i + 4], "d": [portal_lines[i + 5], portal_lines[i + 6]]})
		out.append(["Portal", _line4(portal_lines, i, pl, pl)])
	for i in range(0, portal_arcs.size(), 9):
		var pa := _side("portal", true, {"theta": portal_arcs[i + 6], "d": [portal_arcs[i + 7], portal_arcs[i + 8]]})
		out.append(["Portal", _arc6(portal_arcs, i, pa, pa)])
	return out

# The legacy arrays after the edge walls (state flags, Stages 85-86).
func _specs_after_edge() -> Array:
	var out := []
	var off := _side()
	var tog := _side("pass", true, {}, [{"kind": "state", "key": state_key, "op": "toggle", "value": true}])
	var ghost := _side("pass", true, {}, [{"kind": "state", "key": BeamScene.GHOST(), "op": "toggle", "value": true}])
	for v in toggle_lines:
		out.append(["Toggle", SurfaceSpec.line(_a(v), _b(v), tog, tog)])
	for v in ghost_toggler_lines:
		out.append(["GhostToggle", SurfaceSpec.line(_a(v), _b(v), ghost, ghost)])
	for v in gate_lines:
		var gate := SurfaceSpec.line(_a(v), _b(v), null, null)
		gate.resolver_key = state_key
		gate.resolver_table = {"true": [off, off], "DEFAULT": [_side("terminal"), _side("terminal")]}
		out.append(["Gate", gate])
	for v in flag_mirror_lines:
		var fm := SurfaceSpec.line(_a(v), _b(v), null, null)
		fm.resolver_key = state_key
		fm.resolver_table = {"true": [_side("reflection", true), _side("reflection", true)], "DEFAULT": [off, off]}
		out.append(["FlagMirror", fm])
	for v in passthrough_lines:
		out.append(["PassThrough", SurfaceSpec.line(_a(v), _b(v), off, off)])
	return out

static func _seg_from_v4(v: Vector4) -> Segment:
	return Segment.from_coords(
		Vector2(v.x, v.y), Vector2(v.z, v.w),
		Vector2((v.x + v.z) / 2.0, (v.y + v.w) / 2.0))
