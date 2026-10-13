class_name SurfaceNode
extends Node2D

const LINE_WIDTH := 3.0
const SIDE_OFFSET := 2.0
const BLOCK_COLOR := Color.RED
const REFLECTION_COLOR := Color.BLUE
const INVERSION_COLOR := Color.PURPLE
const PASSTHROUGH_COLOR := Color.GRAY

const HOVER_COLOR := Color(1.0, 1.0, 1.0, 0.3)
const HOVER_WIDTH := ClickDetector.CLICK_TOLERANCE
const PLAN_VALID_COLOR := Color(1.0, 1.0, 1.0, 0.15)
const PLAN_INVALID_COLOR := Color(1.0, 0.0, 0.0, 0.15)

var surface: Surface
var _plan_indices: Array[int] = []
var _cached_left_outer: bool = true
var _hover_overlays: Array = []
var _plan_overlays: Array = []

func setup(p_surface: Surface) -> void:
	surface = p_surface
	add_to_group("surface_nodes")   # redrawn when the game state changes
	_cached_left_outer = _is_left_outer()
	if surface.player_solid:
		if surface.segment.is_line():
			_add_collision_shape()
		else:
			_add_arc_collision_shape()
	queue_redraw()

func clear_hover() -> void:
	if not _hover_overlays.is_empty():
		_hover_overlays.clear()
		queue_redraw()

func set_hover_overlays(overlays: Array) -> void:
	_hover_overlays = overlays
	queue_redraw()

func set_plan_overlays(overlays: Array, indices: Array[int]) -> void:
	_plan_overlays = overlays
	_plan_indices = indices
	queue_redraw()


func _draw() -> void:
	if not surface:
		return
	# the configs under the current game state (categorical surfaces)
	var gm := get_node_or_null("../GameManager")
	var state: GameState = gm.game_state if gm != null and "game_state" in gm else GameState.new()
	var left_config := surface.active_side_config(Side.Value.LEFT, state)
	var right_config := surface.active_side_config(Side.Value.RIGHT, state)
	var left_color := _effect_color(left_config)
	var right_color := _effect_color(right_config)
	var is_arc := not surface.segment.is_line()

	if left_color == right_color:
		if is_arc:
			_draw_surface_arc(left_color, LINE_WIDTH)
		else:
			draw_line(surface.segment.start.coords, surface.segment.end.coords, left_color, LINE_WIDTH)
	else:
		var left_alpha := 1.0 if left_config.interactive else 0.5
		var right_alpha := 1.0 if right_config.interactive else 0.5
		if is_arc:
			var ctr: Vector2
			var r: float
			var sa: float
			var ea: float
			var pc: int
			if surface.segment.full:
				var carrier := surface.segment.get_carrier()
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
			draw_line(surface.segment.start.coords + left_offset, surface.segment.end.coords + left_offset, Color(left_color, left_alpha), LINE_WIDTH * 0.5)
			draw_line(surface.segment.start.coords + right_offset, surface.segment.end.coords + right_offset, Color(right_color, right_alpha), LINE_WIDTH * 0.5)

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

	if _plan_indices.size() > 0:
		var mid := surface.segment.via.coords
		var label_text := ""
		for idx in _plan_indices:
			if label_text != "":
				label_text += ","
			label_text += str(idx)
		draw_string(ThemeDB.fallback_font, mid + Vector2(-5, -10), label_text, HORIZONTAL_ALIGNMENT_CENTER, -1, 14, Color.WHITE)

func _draw_surface_arc(color: Color, width: float) -> void:
	if surface.segment.full:
		var carrier := surface.segment.get_carrier()
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
		if surface.segment.full:
			var carrier := surface.segment.get_carrier()
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
		var s := surface.segment.start.coords
		var e := surface.segment.end.coords
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
		if surface.segment.full:
			var carrier := surface.segment.get_carrier()
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
		var s := surface.segment.start.coords
		var e := surface.segment.end.coords
		for t in CHEVRON_T_VALUES:
			var sample := line_sample(s, e, t)
			var pos: Vector2 = sample.position
			var normal: Vector2 = sample.normal
			var side_at_normal: Side.Value = surface.segment.determine_side(pos + normal * SIDE_OFFSET)
			var side_dir := normal if side_at_normal == side else -normal
			var dir := side_dir if point_away else -side_dir
			var tip := pos + side_dir * dist
			draw_polygon(chevron_vertices(tip, dir, CHEVRON_SIZE), colors)

func _arc_params() -> Dictionary:
	return VisualConverter.arc_params(surface.segment.start.coords, surface.segment.via.coords, surface.segment.end.coords)

func _is_left_outer() -> bool:
	var carrier := surface.segment.get_carrier()
	if carrier.is_line():
		return true
	var ctr := carrier.center()
	var test_point := ctr + (surface.segment.start.coords - ctr).normalized() * (carrier.radius() + SIDE_OFFSET)
	return surface.segment.determine_side(test_point) == Side.Value.LEFT

func _line_side_offset(side: Side.Value, dist: float) -> Vector2:
	var seg_dir := (surface.segment.end.coords - surface.segment.start.coords).normalized()
	var normal := Vector2(-seg_dir.y, seg_dir.x)
	var mid := (surface.segment.start.coords + surface.segment.end.coords) / 2.0
	var side_at_normal: Side.Value = surface.segment.determine_side(mid + normal * SIDE_OFFSET)
	var sign_val := 1.0 if side_at_normal == side else -1.0
	return normal * dist * sign_val

static func chevron_vertices(tip: Vector2, direction: Vector2, size: float) -> PackedVector2Array:
	var dir := direction.normalized()
	var base_left := tip - dir.rotated(0.5) * size
	var base_right := tip - dir.rotated(-0.5) * size
	return PackedVector2Array([tip, base_left, base_right])

static func line_sample(start: Vector2, end: Vector2, t: float) -> Dictionary:
	var pos := start.lerp(end, t)
	var seg_dir := (end - start).normalized()
	var normal := Vector2(-seg_dir.y, seg_dir.x)
	return {"position": pos, "normal": normal}

static func arc_sample(center: Vector2, radius: float, start_angle: float, span: float, clockwise: bool, t: float) -> Dictionary:
	var angle := start_angle + t * span * (-1.0 if clockwise else 1.0)
	var pos := center + Vector2(cos(angle), sin(angle)) * radius
	var outward := (pos - center).normalized()
	return {"position": pos, "outward": outward}

func _effect_color(config: SideConfig) -> Color:
	if config == null or config.effect == null:
		return PASSTHROUGH_COLOR
	return config.effect.get_display_color()

func _add_arc_collision_shape() -> void:
	var ctr: Vector2
	var r: float
	var span: float
	var seg_count: int
	var sa: float
	var ccw: bool
	if surface.segment.full:
		var carrier := surface.segment.get_carrier()
		ctr = carrier.center()
		r = carrier.radius()
		span = TAU
		seg_count = 16
		sa = 0.0
		ccw = true
	else:
		var p := _arc_params()
		ctr = p["center"]
		r = p["radius"]
		span = p["span"]
		seg_count = maxi(3, int(16.0 * span / TAU))
		sa = (surface.segment.start.coords - ctr).angle()
		ccw = not p["clockwise"]
	var body := StaticBody2D.new()
	for i in seg_count:
		var t0 := float(i) / seg_count
		var t1 := float(i + 1) / seg_count
		var a0 := sa + (t0 * span * (1.0 if ccw else -1.0))
		var a1 := sa + (t1 * span * (1.0 if ccw else -1.0))
		var p0 := ctr + Vector2(cos(a0), sin(a0)) * r
		var p1 := ctr + Vector2(cos(a1), sin(a1)) * r
		var collision := CollisionShape2D.new()
		var shape := SegmentShape2D.new()
		shape.a = p0
		shape.b = p1
		collision.shape = shape
		body.add_child(collision)
	add_child(body)

func _add_collision_shape() -> void:
	var body := StaticBody2D.new()
	var collision := CollisionShape2D.new()
	var shape := SegmentShape2D.new()
	shape.a = surface.segment.start.coords
	shape.b = surface.segment.end.coords
	collision.shape = shape
	body.add_child(collision)
	add_child(body)
