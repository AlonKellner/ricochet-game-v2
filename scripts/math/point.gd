class_name Point
extends RefCounted

var coords: Vector2

static func at(position: Vector2) -> Point:
	var p := Point.new()
	p.coords = position
	return p

func same_position(other: Point) -> bool:
	return coords == other.coords
