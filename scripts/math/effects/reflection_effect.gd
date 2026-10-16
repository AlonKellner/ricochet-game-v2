class_name ReflectionEffect
extends TransformativeEffect

func _init(carrier: GeneralizedCircle) -> void:
	_carrier = carrier

func is_self_inverse() -> bool:
	return true

func get_display_name() -> String:
	return "reflect"

func get_display_color() -> Color:
	return Color.BLUE

func to_spec() -> Dictionary:
	return {"type": "reflection"}
