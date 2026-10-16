class_name RigidMotionEffect
extends TransformativeEffect

# Portal: the rigid motion T(z) = e^{iθ} z + d. Portals come in pairs (A -> B
# and its inverse B -> A, built by create_portal_pair); `partner()` is the
# paired effect (held weakly: the two reference each other).

var _theta: float
var _d: Vector2
var _partner: WeakRef = null

func _init(theta: float, d: Vector2, carrier: GeneralizedCircle = null) -> void:
	_theta = theta
	_d = d
	_carrier = carrier

func partner() -> RigidMotionEffect:
	return null if _partner == null else _partner.get_ref()

# T(z) = e^{iθ} z + d
static func apply_motion(z: Vector2, theta: float, d: Vector2) -> Vector2:
	var c := cos(theta)
	var s := sin(theta)
	return Vector2(c * z.x - s * z.y + d.x, s * z.x + c * z.y + d.y)

func get_display_name() -> String:
	return "portal"

func get_display_color() -> Color:
	return Color.CYAN

# The pair A -> B (theta, d) and B -> A (-theta, -e^{-iθ} d); B's segment is
# T(A's segment), point by point.
static func create_portal_pair(source_seg: Segment, theta: float, d: Vector2) -> Dictionary:
	# a line's image is a line, exactly (rotated float32 points are almost
	# never exactly collinear: Segment.straight)
	var target_seg: Segment
	if source_seg.is_line() and not source_seg.full:
		target_seg = Segment.straight(apply_motion(source_seg.start.coords, theta, d), apply_motion(source_seg.end.coords, theta, d),
			apply_motion(source_seg.via.coords, theta, d))
	else:
		target_seg = Segment.from_coords(apply_motion(source_seg.start.coords, theta, d),
			apply_motion(source_seg.end.coords, theta, d), apply_motion(source_seg.via.coords, theta, d))
	target_seg.full = source_seg.full
	var eff_a := RigidMotionEffect.new(theta, d, source_seg.get_carrier())
	var eff_b := RigidMotionEffect.new(-theta, -apply_motion(d, -theta, Vector2.ZERO), target_seg.get_carrier())
	eff_a._partner = weakref(eff_b)
	eff_b._partner = weakref(eff_a)
	return {
		source_effect = eff_a,
		target_effect = eff_b,
		target_segment = target_seg,
	}

func to_spec() -> Dictionary:
	return {"type": "portal", "theta": _theta, "d": [_d.x, _d.y]}
