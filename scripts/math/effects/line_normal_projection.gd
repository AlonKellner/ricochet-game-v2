class_name LineNormalProjection
extends Effect

var _project_back: bool

func _init(project_back: bool = false) -> void:
	_project_back = project_back

func kind() -> int:
	return Kind.PROJECTIVE

func get_display_name() -> String:
	return "line_normal_back" if _project_back else "line_normal"

func get_display_color() -> Color:
	return Color(1.0, 0.4, 0.1) if _project_back else Color(1.0, 0.8, 0.2)

func to_spec() -> Dictionary:
	return {"type": "line_normal_projection", "back": _project_back}
