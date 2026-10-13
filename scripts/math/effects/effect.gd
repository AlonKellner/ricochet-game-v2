class_name Effect
extends RefCounted

enum Kind { PASS, TERMINAL, TRANSFORMATIVE, PROJECTIVE }

func kind() -> int:
	return Kind.PASS

func is_terminal() -> bool:
	return kind() == Kind.TERMINAL

func is_transformative() -> bool:
	return kind() == Kind.TRANSFORMATIVE

# Stage 84 stub
func is_self_inverse() -> bool:
	return false

func get_mobius() -> MobiusTransform:
	return MobiusTransform.identity()

func get_inverse_mobius() -> MobiusTransform:
	return get_mobius().invert()

func get_tracked_transform() -> TrackedTransform:
	return TrackedTransform.identity()

func apply_forward(_hit_point: Vector2, _segment: Segment, _side: int) -> Ray:
	return null

func normalized(_carrier: GeneralizedCircle) -> Effect:
	return self

func get_display_name() -> String:
	return "pass"

func get_display_color() -> Color:
	return Color.GRAY
