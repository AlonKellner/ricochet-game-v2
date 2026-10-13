class_name RigidMotionEffect
extends TransformativeEffect

var _theta: float
var _d: Vector2

func _init(theta: float, d: Vector2, carrier: GeneralizedCircle = null, p_tracked: TrackedTransform = null) -> void:
	_theta = theta
	_d = d
	_carrier = carrier

	var e_itheta := Vector2(cos(theta), sin(theta))
	var alpha := e_itheta
	var beta := d
	var gamma := Vector2.ZERO
	var delta := Vector2(1.0, 0.0)
	_mobius = MobiusTransform.new(alpha, beta, gamma, delta, false)

	if p_tracked != null:
		_tracked = p_tracked
	else:
		var e_neg_itheta := Vector2(cos(theta), -sin(theta))
		var inv_beta := Vector2(
			-(e_neg_itheta.x * _d.x - e_neg_itheta.y * _d.y),
			-(e_neg_itheta.x * _d.y + e_neg_itheta.y * _d.x))
		var inv_mobius := MobiusTransform.new(e_neg_itheta, inv_beta, Vector2.ZERO, Vector2(1.0, 0.0), false)
		_tracked = TrackedTransform.from_pair(_mobius, inv_mobius)
		if carrier != null:
			_tracked.carrier = carrier

# Stage 84 stub
func partner() -> RigidMotionEffect:
	return null

func get_inverse_mobius() -> MobiusTransform:
	return _tracked.inverse.mobius

func _create_normalized(carrier: GeneralizedCircle) -> Effect:
	return RigidMotionEffect.new(_theta, _d, carrier, _tracked)

func get_display_name() -> String:
	return "portal"

func get_display_color() -> Color:
	return Color.CYAN

static func create_portal_pair(source_seg: Segment, theta: float, d: Vector2) -> Dictionary:
	var e_itheta := Vector2(cos(theta), sin(theta))
	var fwd_alpha := e_itheta
	var fwd_beta := d
	var fwd_mobius := MobiusTransform.new(fwd_alpha, fwd_beta, Vector2.ZERO, Vector2(1.0, 0.0), false)

	var e_neg_itheta := Vector2(cos(theta), -sin(theta))
	var inv_beta := Vector2(
		-(e_neg_itheta.x * d.x - e_neg_itheta.y * d.y),
		-(e_neg_itheta.x * d.y + e_neg_itheta.y * d.x))
	var inv_mobius := MobiusTransform.new(e_neg_itheta, inv_beta, Vector2.ZERO, Vector2(1.0, 0.0), false)

	var tracked_ab := TrackedTransform.from_pair(fwd_mobius, inv_mobius)
	var tracked_ba := tracked_ab.inverse

	var target_start := source_seg.start.transformed(tracked_ab)
	var target_end := source_seg.end.transformed(tracked_ab)
	var target_via := source_seg.via.transformed(tracked_ab)
	var target_seg := Segment.new(target_start, target_end, target_via)

	var carrier_a := source_seg.get_carrier()
	tracked_ab.carrier = carrier_a

	var carrier_b := target_seg.get_carrier()
	tracked_ba.carrier = carrier_b

	var eff_a := RigidMotionEffect.new(theta, d, carrier_a, tracked_ab)
	var inv_theta := -theta
	var inv_d := Vector2(
		-(e_neg_itheta.x * d.x - e_neg_itheta.y * d.y),
		-(e_neg_itheta.x * d.y + e_neg_itheta.y * d.x))
	var eff_b := RigidMotionEffect.new(inv_theta, inv_d, carrier_b, tracked_ba)

	return {
		source_effect = eff_a,
		target_effect = eff_b,
		target_segment = target_seg,
	}
