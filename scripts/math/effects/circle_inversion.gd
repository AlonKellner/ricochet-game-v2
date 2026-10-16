class_name CircleInversionEffect
extends TransformativeEffect

func _init(carrier: GeneralizedCircle) -> void:
	assert(not carrier.is_line(), "CircleInversionEffect requires a circle carrier (a != 0)")
	_carrier = carrier

func is_self_inverse() -> bool:
	return true

func get_display_name() -> String:
	return "inversion"

func get_display_color() -> Color:
	return Color.PURPLE

func to_spec() -> Dictionary:
	return {"type": "inversion"}
