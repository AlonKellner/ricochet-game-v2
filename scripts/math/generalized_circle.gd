class_name GeneralizedCircle
extends RefCounted

var a: float
var b: float
var c: float
var d: float

func _init(p_a: float, p_b: float, p_c: float, p_d: float) -> void:
	a = p_a
	b = p_b
	c = p_c
	d = p_d

func is_line() -> bool:
	return a == 0.0

func center() -> Vector2:
	assert(a != 0.0, "center() called on a line (a == 0)")
	return Vector2(-b / (2.0 * a), -c / (2.0 * a))

func radius() -> float:
	assert(a != 0.0, "radius() called on a line (a == 0)")
	return sqrt((b * b + c * c - 4.0 * a * d) / (4.0 * a * a))

func same_circle(other: GeneralizedCircle) -> bool:
	return self == other or (a == other.a and b == other.b and c == other.c and d == other.d)

func evaluate(point: Vector2) -> float:
	return a * (point.x * point.x + point.y * point.y) + b * point.x + c * point.y + d

static func from_line(p_b: float, p_c: float, p_d: float) -> GeneralizedCircle:
	return GeneralizedCircle.new(0.0, p_b, p_c, p_d)

static func from_circle(p_center: Vector2, p_radius: float) -> GeneralizedCircle:
	var p_a := 1.0
	var p_b := -2.0 * p_center.x
	var p_c := -2.0 * p_center.y
	var p_d := p_center.x * p_center.x + p_center.y * p_center.y - p_radius * p_radius
	return GeneralizedCircle.new(p_a, p_b, p_c, p_d)
