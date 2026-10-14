class_name GdSurfaceGeom
extends RefCounted

# Test-support copy of SurfaceNode's sampling statics (chevron_vertices,
# line_sample, arc_sample) for tests/native/test_diff_visual.gd; the game uses
# SurfaceGeom (native). Deleted in Stage 99.

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

