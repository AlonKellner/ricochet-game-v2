class_name GdPoint
extends RefCounted

var coords: Vector2

static func at(position: Vector2) -> GdPoint:
	var p := GdPoint.new()
	p.coords = position
	return p

func same_position(other: GdPoint) -> bool:
	return coords == other.coords
