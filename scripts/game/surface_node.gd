class_name SurfaceNode
extends Node2D

const LINE_WIDTH := 3.0
const SIDE_OFFSET := 2.0
const BLOCK_COLOR := Color.RED
const REFLECTION_COLOR := Color.BLUE
const INVERSION_COLOR := Color.PURPLE
const PASSTHROUGH_COLOR := Color.GRAY
const GHOST_TOGGLER_COLOR := Color(0.75, 0.95, 1.0)
const FLAG_TOGGLER_COLOR := Color(1.0, 0.85, 0.2)

# A side's colour: its effect's, or a toggler's (a pass with hit effects).
static func config_color(config: SideConfig) -> Color:
	if config == null:
		return PASSTHROUGH_COLOR
	if config.effect == null:
		if not config.hit_effects.is_empty():
			return GHOST_TOGGLER_COLOR if config.writes_key(BeamScene.GHOST(), -1) else FLAG_TOGGLER_COLOR
		return PASSTHROUGH_COLOR
	return config.effect.get_display_color()

const HOVER_COLOR := Color(1.0, 1.0, 1.0, 0.3)
const HOVER_WIDTH := ClickDetector.CLICK_TOLERANCE
const PLAN_VALID_COLOR := Color(1.0, 1.0, 1.0, 0.15)
const PLAN_INVALID_COLOR := Color(1.0, 0.0, 0.0, 0.15)

var surface: Surface
# Stage 108: the piece this node draws (GAME_SPEC 9.7) -- its segment, and its
# index among its design surface's pieces (-1: the whole surface)
var segment: Segment
var piece_index := -1
var _plan_indices: Array[int] = []
var _cached_left_outer: bool = true
var _hover_overlays: Array = []
var _plan_overlays: Array = []
# The arc parameters of the surface's segment (immutable after setup), computed once.
var _arc_cache: Dictionary = {}

func setup(p_surface: Surface, p_piece = null) -> void:
	surface = p_surface
	segment = p_piece.segment if p_piece != null else p_surface.segment
	piece_index = p_piece.index if p_piece != null else -1
	_arc_cache = {}
	add_to_group("surface_nodes")   # redrawn when the game state changes
	_cached_left_outer = _is_left_outer()
	queue_redraw()

func clear_hover() -> void:
	if not _hover_overlays.is_empty():
		_hover_overlays.clear()
		queue_redraw()

# Unchanged overlays leave the node alone (no redraw; Stage 100).
func set_hover_overlays(overlays: Array) -> void:
	if ChevronOverlay.same_list(_hover_overlays, overlays):
		return
	_hover_overlays = overlays
	queue_redraw()

func set_plan_overlays(overlays: Array, indices: Array[int]) -> void:
	if ChevronOverlay.same_list(_plan_overlays, overlays) and _plan_indices == indices:
		return
	_plan_overlays = overlays
	_plan_indices = indices
	queue_redraw()


const PRE_TRANSFORM_COLOR := Color(0.55, 0.45, 0.85, 0.35)

func _draw() -> void:
	if not surface:
		return
	if surface.pre_transform:
		# the frame the shot starts in (Stage 101): a faint circle, no sides, no chevrons
		var carrier := segment.get_carrier()
		draw_arc(carrier.center(), carrier.radius(), 0, TAU, VisualConverter.POINTS_PER_FULL_CIRCLE, PRE_TRANSFORM_COLOR, 1.0)
		return
	# the configs under the current game state (categorical surfaces)
	var gm := get_node_or_null("../GameManager")
	var state: GameState = gm.game_state if gm != null and "game_state" in gm else GameState.new()
	if not surface.present(state.flags):
		# absent (Stage 107, GAME_SPEC 9.6): a spent host or a group not current
		SpawnPreviewNode.dashed(self, segment, ABSENT_COLOR, 1.5)
		return
	var left_config := surface.active_side_config(Side.Value.LEFT, state)
	var right_config := surface.active_side_config(Side.Value.RIGHT, state)
	var left_color := _effect_color(left_config)
	var right_color := _effect_color(right_config)
	var is_arc := not segment.is_line()

	if left_color == right_color:
		if is_arc:
			_draw_surface_arc(left_color, LINE_WIDTH)
		else:
			draw_line(segment.start.coords, segment.end.coords, left_color, LINE_WIDTH)
	else:
		var left_alpha := 1.0 if left_config.interactive else 0.5
		var right_alpha := 1.0 if right_config.interactive else 0.5
		if is_arc:
			var ctr: Vector2
			var r: float
			var sa: float
			var ea: float
			var pc: int
			if segment.full:
				var carrier := segment.get_carrier()
				ctr = carrier.center()
				r = carrier.radius()
				sa = 0.0
				ea = TAU
				pc = VisualConverter.POINTS_PER_FULL_CIRCLE
			else:
				var p := _arc_params()
				ctr = p["center"]
				r = p["radius"]
				sa = p["start_angle"]
				ea = p["end_angle"]
				pc = p["point_count"]
			var outer_color := Color(left_color, left_alpha) if _cached_left_outer else Color(right_color, right_alpha)
			var inner_color := Color(right_color, right_alpha) if _cached_left_outer else Color(left_color, left_alpha)
			draw_arc(ctr, r + SIDE_OFFSET, sa, ea, pc, outer_color, LINE_WIDTH * 0.5)
			draw_arc(ctr, r - SIDE_OFFSET, sa, ea, pc, inner_color, LINE_WIDTH * 0.5)
		else:
			var left_offset := _line_side_offset(Side.Value.LEFT, SIDE_OFFSET)
			var right_offset := _line_side_offset(Side.Value.RIGHT, SIDE_OFFSET)
			draw_line(segment.start.coords + left_offset, segment.end.coords + left_offset, Color(left_color, left_alpha), LINE_WIDTH * 0.5)
			draw_line(segment.start.coords + right_offset, segment.end.coords + right_offset, Color(right_color, right_alpha), LINE_WIDTH * 0.5)

	var _hover_sides_set := {}
	for ov in _hover_overlays:
		var o: ChevronOverlay = ov
		_hover_sides_set[o.side] = true

	for ov in _plan_overlays:
		var o: ChevronOverlay = ov
		if o.has_incoming and _hover_sides_set.has(o.side):
			continue
		_draw_overlay(o, is_arc)

	for ov in _hover_overlays:
		_draw_overlay(ov, is_arc)

	if piece_index <= 0:
		_draw_hit_markers(left_config, right_config)   # the design surface's: once, on its first piece
	if piece_index >= 0:
		# a piece's crossing ends (Stage 108): a small dot where it meets another
		for end in [segment.start.coords, segment.end.coords]:
			if end != surface.segment.start.coords and end != surface.segment.end.coords:
				draw_circle(end, CROSSING_DOT, CROSSING_COLOR)

	if _plan_indices.size() > 0:
		var mid := segment.via.coords
		var label_text := ""
		for idx in _plan_indices:
			if label_text != "":
				label_text += ","
			label_text += str(idx)
		draw_string(ThemeDB.fallback_font, mid + Vector2(-5, -10), label_text, HORIZONTAL_ALIGNMENT_CENTER, -1, 14, Color.WHITE)

const ABSENT_COLOR := Color(0.6, 0.6, 0.6, 0.35)
const CROSSING_DOT := 2.5
const CROSSING_COLOR := Color(1.0, 1.0, 1.0, 0.6)
const MARKER_SIZE := 5.0
const MARKER_SPACING := 14.0
const TARGET_COLOR := Color(1.0, 0.85, 0.3)
const SPAWN_COLOR := Color(0.6, 0.95, 1.0)
const RESET_COLOR := Color(1.0, 0.55, 0.85)

# One marker per hit effect (Stage 107), beside the segment's via point on
# its side: a target ring, a spawn plus, a reset cross, a state-change
# diamond (a pass side's own state changes are its toggler colour already).
# A deferred effect is drawn hollow-centred (a dot in the middle).
func _draw_hit_markers(left_config: SideConfig, right_config: SideConfig) -> void:
	var via := segment.via.coords
	var tangent := _marker_tangent(via)
	for side in [Side.Value.LEFT, Side.Value.RIGHT]:
		var cfg: SideConfig = left_config if side == Side.Value.LEFT else right_config
		if cfg == null:
			continue
		var marks: Array[HitEffect] = []
		for h in cfg.hit_effects:
			if h is StateChangeHit and cfg.effect == null:
				continue
			marks.append(h)
		var normal := _marker_normal(side, via) * (MARKER_SIZE + 6.0)
		for i in marks.size():
			var at := via + normal + tangent * MARKER_SPACING * (i - (marks.size() - 1) / 2.0)
			_draw_marker(marks[i], at)

func _draw_marker(h: HitEffect, at: Vector2) -> void:
	var s := MARKER_SIZE
	if h is TargetHit:
		draw_arc(at, s, 0, TAU, 16, TARGET_COLOR, 1.5)
		draw_circle(at, 1.5, TARGET_COLOR)
	elif h is SpawnHit:
		draw_line(at - Vector2(s, 0), at + Vector2(s, 0), SPAWN_COLOR, 2.0)
		draw_line(at - Vector2(0, s), at + Vector2(0, s), SPAWN_COLOR, 2.0)
	elif h is ResetHit:
		draw_line(at - Vector2(s, s), at + Vector2(s, s), RESET_COLOR, 2.0)
		draw_line(at - Vector2(s, -s), at + Vector2(s, -s), RESET_COLOR, 2.0)
	else:
		draw_colored_polygon(PackedVector2Array([at + Vector2(0, -s), at + Vector2(s, 0), at + Vector2(0, s), at + Vector2(-s, 0)]),
			FLAG_TOGGLER_COLOR)
	if h.deferred:
		draw_circle(at, 2.0, Color(0.1, 0.1, 0.1))

# The unit tangent of the segment at a point on it (for the marker row).
func _marker_tangent(at: Vector2) -> Vector2:
	if segment.is_line():
		return (segment.end.coords - segment.start.coords).normalized()
	return SpawnFrame.rot90(at - segment.get_carrier().center()).normalized()

# The unit normal toward a side, at a point on the segment.
func _marker_normal(side: Side.Value, at: Vector2) -> Vector2:
	var n := SpawnFrame.rot90(_marker_tangent(at))
	var probe := at + n
	return n if segment.determine_side(probe) == side else -n

func _draw_surface_arc(color: Color, width: float) -> void:
	if segment.full:
		var carrier := segment.get_carrier()
		draw_arc(carrier.center(), carrier.radius(), 0, TAU, VisualConverter.POINTS_PER_FULL_CIRCLE, color, width)
		return
	var p := _arc_params()
	draw_arc(p["center"], p["radius"], p["start_angle"], p["end_angle"], p["point_count"], color, width)

const GRADIENT_STRIPS := 6
const CHEVRON_SIZE := 8.0
const CHEVRON_T_VALUES: Array[float] = [1.0 / 6.0, 0.5, 5.0 / 6.0]
const INNER_CHEVRON_DIST := 2.0
const OUTER_CHEVRON_DIST := 18.0

func _draw_gradient_for_side(side: int, is_arc: bool, base_color: Color) -> void:
	var strip_width := HOVER_WIDTH / GRADIENT_STRIPS
	var is_outer := (side == Side.Value.LEFT) == _cached_left_outer
	var sign_val := 1.0 if is_outer else -1.0

	if is_arc:
		var h_ctr: Vector2
		var h_r: float
		var h_sa: float
		var h_ea: float
		var h_pc: int
		if segment.full:
			var carrier := segment.get_carrier()
			h_ctr = carrier.center()
			h_r = carrier.radius()
			h_sa = 0.0
			h_ea = TAU
			h_pc = VisualConverter.POINTS_PER_FULL_CIRCLE
		else:
			var p := _arc_params()
			h_ctr = p["center"]
			h_r = p["radius"]
			h_sa = p["start_angle"]
			h_ea = p["end_angle"]
			h_pc = p["point_count"]
		for i in GRADIENT_STRIPS:
			var frac := (float(i) + 0.5) / GRADIENT_STRIPS
			var alpha := base_color.a * (1.0 - frac)
			var offset := frac * HOVER_WIDTH * sign_val
			draw_arc(h_ctr, h_r + offset, h_sa, h_ea, h_pc, Color(base_color, alpha), strip_width)
	else:
		var s := segment.start.coords
		var e := segment.end.coords
		for i in GRADIENT_STRIPS:
			var frac := (float(i) + 0.5) / GRADIENT_STRIPS
			var alpha := base_color.a * (1.0 - frac)
			var offset := _line_side_offset(side, frac * HOVER_WIDTH)
			draw_line(s + offset, e + offset, Color(base_color, alpha), strip_width)

func _draw_overlay(ov: ChevronOverlay, is_arc: bool) -> void:
	if ov.has_incoming:
		_draw_gradient_for_side(ov.side, is_arc, ov.gradient_color)
		_draw_directed_chevrons(ov.side, is_arc, ov.incoming_color, INNER_CHEVRON_DIST, false)
	if ov.has_outgoing:
		_draw_gradient_for_side(ov.outgoing_side, is_arc, ov.gradient_color)
		_draw_directed_chevrons(ov.outgoing_side, is_arc, ov.outgoing_color, OUTER_CHEVRON_DIST, true)

func _draw_directed_chevrons(side: int, is_arc: bool, base_color: Color, dist: float, point_away: bool) -> void:
	var is_outer := (side == Side.Value.LEFT) == _cached_left_outer
	var chevron_color := Color(base_color, base_color.a)
	var colors := PackedColorArray([chevron_color, chevron_color, chevron_color])

	if is_arc:
		var h_ctr: Vector2
		var h_r: float
		var h_sa: float
		var h_span: float
		var h_cw: bool
		if segment.full:
			var carrier := segment.get_carrier()
			h_ctr = carrier.center()
			h_r = carrier.radius()
			h_sa = 0.0
			h_span = TAU
			h_cw = false
		else:
			var p := _arc_params()
			h_ctr = p["center"]
			h_r = p["radius"]
			h_sa = p["start_angle"]
			h_span = p["span"]
			h_cw = p["clockwise"]
		for t in CHEVRON_T_VALUES:
			var sample := arc_sample(h_ctr, h_r, h_sa, h_span, h_cw, t)
			var pos: Vector2 = sample.position
			var outward: Vector2 = sample.outward
			var side_dir := outward if is_outer else -outward
			var dir := side_dir if point_away else -side_dir
			var tip := pos + side_dir * dist
			draw_polygon(chevron_vertices(tip, dir, CHEVRON_SIZE), colors)
	else:
		var s := segment.start.coords
		var e := segment.end.coords
		for t in CHEVRON_T_VALUES:
			var sample := line_sample(s, e, t)
			var pos: Vector2 = sample.position
			var normal: Vector2 = sample.normal
			var side_at_normal: Side.Value = segment.determine_side(pos + normal * SIDE_OFFSET)
			var side_dir := normal if side_at_normal == side else -normal
			var dir := side_dir if point_away else -side_dir
			var tip := pos + side_dir * dist
			draw_polygon(chevron_vertices(tip, dir, CHEVRON_SIZE), colors)

func _arc_params() -> Dictionary:
	if _arc_cache.is_empty():
		_arc_cache = VisualConverter.arc_params(segment.start.coords, segment.via.coords, segment.end.coords)
	return _arc_cache

func _is_left_outer() -> bool:
	var carrier := segment.get_carrier()
	if carrier.is_line():
		return true
	var ctr := carrier.center()
	var test_point := ctr + (segment.start.coords - ctr).normalized() * (carrier.radius() + SIDE_OFFSET)
	return segment.determine_side(test_point) == Side.Value.LEFT

func _line_side_offset(side: Side.Value, dist: float) -> Vector2:
	var seg_dir := (segment.end.coords - segment.start.coords).normalized()
	var normal := Vector2(-seg_dir.y, seg_dir.x)
	var mid := (segment.start.coords + segment.end.coords) / 2.0
	var side_at_normal: Side.Value = segment.determine_side(mid + normal * SIDE_OFFSET)
	var sign_val := 1.0 if side_at_normal == side else -1.0
	return normal * dist * sign_val

static func chevron_vertices(tip: Vector2, direction: Vector2, size: float) -> PackedVector2Array:
	return SurfaceGeom.chevron_vertices(tip, direction, size)

static func line_sample(start: Vector2, end: Vector2, t: float) -> Dictionary:
	return SurfaceGeom.line_sample(start, end, t)

static func arc_sample(center: Vector2, radius: float, start_angle: float, span: float, clockwise: bool, t: float) -> Dictionary:
	return SurfaceGeom.arc_sample(center, radius, start_angle, span, clockwise, t)

func _effect_color(config: SideConfig) -> Color:
	return config_color(config)

