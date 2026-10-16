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

var surfaces: Array[Surface] = []

func _ready() -> void:
	if build_room:
		surfaces = RoomBuilder.add_room_to_scene(self, room_rect)
	for line_def in block_lines:
		var surf := RoomBuilder.create_block_surface(
			Vector2(line_def.x, line_def.y),
			Vector2(line_def.z, line_def.w),
			Vector2((line_def.x + line_def.z) / 2.0, (line_def.y + line_def.w) / 2.0))
		_add_surface(surf, "Block")
	for line_def in mirror_lines:
		var seg := _seg_from_v4(line_def)
		var reflection := ReflectionEffect.new(seg.get_carrier())
		var surf := Surface.new(seg, SideConfig.new(reflection, true), SideConfig.new(null, false), false, false)
		_add_surface(surf, "Mirror")
	for line_def in mirror_right_lines:
		var seg := _seg_from_v4(line_def)
		var reflection := ReflectionEffect.new(seg.get_carrier())
		var surf := Surface.new(seg, SideConfig.new(null, false), SideConfig.new(reflection, true), false, false)
		_add_surface(surf, "MirrorR")
	for line_def in mirror_both_lines:
		var seg := _seg_from_v4(line_def)
		var reflection := ReflectionEffect.new(seg.get_carrier())
		var surf := Surface.new(seg, SideConfig.new(reflection, true), SideConfig.new(reflection, true), false, false)
		_add_surface(surf, "MirrorB")
	for i in range(0, inversion_left_arcs.size(), 6):
		var seg := Segment.from_coords(
			Vector2(inversion_left_arcs[i], inversion_left_arcs[i + 1]),
			Vector2(inversion_left_arcs[i + 2], inversion_left_arcs[i + 3]),
			Vector2(inversion_left_arcs[i + 4], inversion_left_arcs[i + 5]))
		var inversion := CircleInversionEffect.new(seg.get_carrier())
		var surf := Surface.new(seg, SideConfig.new(inversion, true), SideConfig.new(null, false), false, true)
		_add_surface(surf, "Inversion")
	for i in range(0, full_inversion_arcs.size(), 3):
		var center := Vector2(full_inversion_arcs[i], full_inversion_arcs[i + 1])
		var r := full_inversion_arcs[i + 2]
		var carrier := GeneralizedCircle.from_circle(center, r)
		var seg2 := Segment.full_from_carrier(carrier)
		var inv := CircleInversionEffect.new(carrier)
		var surf2 := Surface.new(seg2, SideConfig.new(inv, true), SideConfig.new(inv, true), false, true)
		_add_surface(surf2, "FullInversion")
	for i in range(0, semi_reflective_arcs.size(), 6):
		var seg := Segment.from_coords(
			Vector2(semi_reflective_arcs[i], semi_reflective_arcs[i + 1]),
			Vector2(semi_reflective_arcs[i + 2], semi_reflective_arcs[i + 3]),
			Vector2(semi_reflective_arcs[i + 4], semi_reflective_arcs[i + 5]))
		var reflection := ReflectionEffect.new(seg.get_carrier())
		var surf := Surface.new(seg, SideConfig.new(reflection, true), SideConfig.new(null, false), false, false)
		_add_surface(surf, "SemiReflArc")
	for i in range(0, reflective_arcs.size(), 6):
		var seg := Segment.from_coords(
			Vector2(reflective_arcs[i], reflective_arcs[i + 1]),
			Vector2(reflective_arcs[i + 2], reflective_arcs[i + 3]),
			Vector2(reflective_arcs[i + 4], reflective_arcs[i + 5]))
		var reflection := ReflectionEffect.new(seg.get_carrier())
		var surf := Surface.new(seg, SideConfig.new(reflection, true), SideConfig.new(reflection, true), false, false)
		_add_surface(surf, "ReflArc")
	for i in range(0, full_reflective_arcs.size(), 3):
		var center := Vector2(full_reflective_arcs[i], full_reflective_arcs[i + 1])
		var r := full_reflective_arcs[i + 2]
		var carrier := GeneralizedCircle.from_circle(center, r)
		var seg := Segment.full_from_carrier(carrier)
		var reflection := ReflectionEffect.new(carrier)
		var surf := Surface.new(seg, SideConfig.new(reflection, true), SideConfig.new(reflection, true), false, false)
		_add_surface(surf, "FullReflArc")
	for line_def in normal_projection_lines:
		var seg := _seg_from_v4(line_def)
		var projection := LineNormalProjection.new()
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "Projection")
	for line_def in normal_projection_back_lines:
		var seg := _seg_from_v4(line_def)
		var projection := LineNormalProjection.new(true)
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "ProjectionBack")
	for i in range(0, normal_projection_arcs.size(), 6):
		var seg := Segment.from_coords(
			Vector2(normal_projection_arcs[i], normal_projection_arcs[i + 1]),
			Vector2(normal_projection_arcs[i + 2], normal_projection_arcs[i + 3]),
			Vector2(normal_projection_arcs[i + 4], normal_projection_arcs[i + 5]))
		var projection := CircleNormalProjection.new()
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "CircleProjection")
	for i in range(0, normal_projection_back_arcs.size(), 6):
		var seg := Segment.from_coords(
			Vector2(normal_projection_back_arcs[i], normal_projection_back_arcs[i + 1]),
			Vector2(normal_projection_back_arcs[i + 2], normal_projection_back_arcs[i + 3]),
			Vector2(normal_projection_back_arcs[i + 4], normal_projection_back_arcs[i + 5]))
		var projection := CircleNormalProjection.new(true)
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "CircleProjectionBack")
	for i in range(0, circle_directional_arcs.size(), 8):
		var seg := Segment.from_coords(
			Vector2(circle_directional_arcs[i], circle_directional_arcs[i + 1]),
			Vector2(circle_directional_arcs[i + 2], circle_directional_arcs[i + 3]),
			Vector2(circle_directional_arcs[i + 4], circle_directional_arcs[i + 5]))
		var normal := Vector2(circle_directional_arcs[i + 6], circle_directional_arcs[i + 7])
		var projection := CircleDirectionalProjection.new(normal)
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "CircleDir")
	for i in range(0, circle_directional_back_arcs.size(), 8):
		var seg := Segment.from_coords(
			Vector2(circle_directional_back_arcs[i], circle_directional_back_arcs[i + 1]),
			Vector2(circle_directional_back_arcs[i + 2], circle_directional_back_arcs[i + 3]),
			Vector2(circle_directional_back_arcs[i + 4], circle_directional_back_arcs[i + 5]))
		var normal := Vector2(circle_directional_back_arcs[i + 6], circle_directional_back_arcs[i + 7])
		var projection := CircleDirectionalProjection.new(normal, true)
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "CircleDirBack")
	for i in range(0, directional_projection_lines.size(), 6):
		var seg := Segment.from_coords(
			Vector2(directional_projection_lines[i], directional_projection_lines[i + 1]),
			Vector2(directional_projection_lines[i + 2], directional_projection_lines[i + 3]),
			Vector2((directional_projection_lines[i] + directional_projection_lines[i + 2]) / 2.0,
				(directional_projection_lines[i + 1] + directional_projection_lines[i + 3]) / 2.0))
		var normal := Vector2(directional_projection_lines[i + 4], directional_projection_lines[i + 5])
		var projection := LineDirectionalProjection.new(normal)
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "LineDir")
	for i in range(0, directional_projection_back_lines.size(), 6):
		var seg := Segment.from_coords(
			Vector2(directional_projection_back_lines[i], directional_projection_back_lines[i + 1]),
			Vector2(directional_projection_back_lines[i + 2], directional_projection_back_lines[i + 3]),
			Vector2((directional_projection_back_lines[i] + directional_projection_back_lines[i + 2]) / 2.0,
				(directional_projection_back_lines[i + 1] + directional_projection_back_lines[i + 3]) / 2.0))
		var normal := Vector2(directional_projection_back_lines[i + 4], directional_projection_back_lines[i + 5])
		var projection := LineDirectionalProjection.new(normal, true)
		var surf := Surface.new(seg, SideConfig.new(projection, true), SideConfig.new(projection, true), false, false)
		_add_surface(surf, "LineDirBack")
	for i in range(0, portal_lines.size(), 7):
		var seg := Segment.from_coords(
			Vector2(portal_lines[i], portal_lines[i + 1]),
			Vector2(portal_lines[i + 2], portal_lines[i + 3]),
			Vector2((portal_lines[i] + portal_lines[i + 2]) / 2.0,
				(portal_lines[i + 1] + portal_lines[i + 3]) / 2.0))
		var result := RigidMotionEffect.create_portal_pair(seg, portal_lines[i + 4],
			Vector2(portal_lines[i + 5], portal_lines[i + 6]))
		_add_portal_pair(seg, result)
	for i in range(0, portal_arcs.size(), 9):
		var seg := Segment.from_coords(
			Vector2(portal_arcs[i], portal_arcs[i + 1]),
			Vector2(portal_arcs[i + 2], portal_arcs[i + 3]),
			Vector2(portal_arcs[i + 4], portal_arcs[i + 5]))
		var result := RigidMotionEffect.create_portal_pair(seg, portal_arcs[i + 6],
			Vector2(portal_arcs[i + 7], portal_arcs[i + 8]))
		_add_portal_pair(seg, result)
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
	for line_def in toggle_lines:
		var tog := SideConfig.new(null, true, [StateChangeHit.new(state_key, StateChange.Op.TOGGLE)])
		_add_surface(Surface.new(_seg_from_v4(line_def), tog, tog, false, false), "Toggle")
	for line_def in ghost_toggler_lines:
		var g := SideConfig.new(null, true, [StateChangeHit.new(BeamScene.GHOST(), StateChange.Op.TOGGLE)])
		_add_surface(Surface.new(_seg_from_v4(line_def), g, g, false, false), "GhostToggle")
	for line_def in gate_lines:
		var block := SideConfig.new(TerminalEffect.new(), false)
		var open := SideConfig.new(null, false)
		var gate := CategoricalResolver.new(state_key, {"true": [open, open], "DEFAULT": [block, block]})
		_add_surface(Surface.with_resolver(_seg_from_v4(line_def), gate, false, false), "Gate")
	for line_def in flag_mirror_lines:
		var seg := _seg_from_v4(line_def)
		var refl := SideConfig.new(ReflectionEffect.new(seg.get_carrier()), true)
		var off := SideConfig.new(null, false)
		var fm := CategoricalResolver.new(state_key, {"true": [refl, refl], "DEFAULT": [off, off]})
		_add_surface(Surface.with_resolver(seg, fm, false, false), "FlagMirror")
	for line_def in passthrough_lines:
		var config := SideConfig.new(null, false)
		var surf := Surface.new(_seg_from_v4(line_def), config, config, false, false)
		_add_surface(surf, "PassThrough")
	for i in range(0, pre_transform_inversions.size(), 3):
		var pre := Surface.pre_transform_circle(Vector2(pre_transform_inversions[i], pre_transform_inversions[i + 1]),
			pre_transform_inversions[i + 2])
		_add_surface(pre, "PreTransform")
	# the level bounds' edge: hover band and planned infinity entries (Stage 88)
	var edge := Node2D.new()
	edge.set_script(load("res://scripts/game/edge_overlay_node.gd"))
	edge.name = "EdgeOverlay"
	add_child(edge)
	edge.setup(level_bounds)

func _add_surface(surf: Surface, prefix: String) -> void:
	surfaces.append(surf)
	var node := Node2D.new()
	node.set_script(load("res://scripts/game/surface_node.gd"))
	node.name = "%s_%d" % [prefix, surf.id]
	add_child(node)
	node.setup(surf)

func _add_portal_pair(source_seg: Segment, result: Dictionary) -> void:
	var src_cfg := SideConfig.new(result.source_effect, true)
	var src_surf := Surface.new(source_seg, src_cfg, src_cfg, false, false)
	_add_surface(src_surf, "PortalSrc")
	var tgt_cfg := SideConfig.new(result.target_effect, true)
	var tgt_surf := Surface.new(result.target_segment, tgt_cfg, tgt_cfg, false, false)
	_add_surface(tgt_surf, "PortalTgt")
	var link_l := SideLink.from_pair(src_surf, Side.Value.LEFT, tgt_surf, Side.Value.RIGHT)
	src_surf.set_side_link(Side.Value.LEFT, link_l)
	tgt_surf.set_side_link(Side.Value.RIGHT, link_l.outgoing)
	var link_r := SideLink.from_pair(src_surf, Side.Value.RIGHT, tgt_surf, Side.Value.LEFT)
	src_surf.set_side_link(Side.Value.RIGHT, link_r)
	tgt_surf.set_side_link(Side.Value.LEFT, link_r.outgoing)

static func _seg_from_v4(v: Vector4) -> Segment:
	return Segment.from_coords(
		Vector2(v.x, v.y), Vector2(v.z, v.w),
		Vector2((v.x + v.z) / 2.0, (v.y + v.w) / 2.0))
