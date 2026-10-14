class_name GdRay
extends RefCounted

var origin: GdPoint
var direction: GdDirection

func _init(p_origin: GdPoint, p_direction: GdDirection) -> void:
	origin = p_origin
	direction = p_direction

static func from_coords(p_origin: Vector2, p_direction: GdDirection) -> GdRay:
	return GdRay.new(GdPoint.at(p_origin), p_direction)
