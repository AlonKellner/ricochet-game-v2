class_name CircleDirectionalProjection
extends Effect

var _normal: Vector2
var _project_back: bool

func _init(normal_dir: Vector2, project_back: bool = false) -> void:
	_normal = normal_dir.normalized()
	_project_back = project_back

func normal() -> Vector2:
	return _normal

func kind() -> int:
	return Kind.PROJECTIVE

func get_display_name() -> String:
	return "circle_directional_back" if _project_back else "circle_directional"

func get_display_color() -> Color:
	return Color(1.0, 0.4, 0.1) if _project_back else Color(1.0, 0.8, 0.2)

func to_spec() -> Dictionary:
	return {"type": "circle_directional_projection", "back": _project_back, "normal": [_normal.x, _normal.y]}
