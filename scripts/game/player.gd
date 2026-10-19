extends Node2D

# Stage 117: the player moves by the native controller (rcg/kinematics.hpp,
# RcgKinematics): a circle of COLLISION_RADIUS against the present
# player-solid surfaces and the level's colliders, top-down or with gravity
# (GAME_SPEC 25). The velocity and the floor state are the player's own.

const SPEED := 200.0
const COLLISION_RADIUS := 12.0
const JUMP_VELOCITY := 400.0

var velocity := Vector2.ZERO
var _on_floor := false
var _defs_cache := SceneDefsCache.new()

func get_level_gravity() -> Vector2:
	var parent := get_parent()
	if parent and parent.has_method("get") and "gravity" in parent:
		return parent.gravity
	return Vector2.ZERO

func has_level_gravity() -> bool:
	return get_level_gravity().length_squared() > 0.0

func is_on_floor() -> bool:
	return _on_floor

# The player faces the way the arrow will leave: the preview's first step
# (a plan's second realization or a curved frame may point away from the
# cursor); the cursor only when there is no preview.
func _process(_delta: float) -> void:
	var visual := $Visual as Node2D
	var renderer := get_node_or_null("../PathRenderer")
	if renderer and renderer.has_method("get_planned_path"):
		var path = renderer.get_planned_path()
		if path != null and path.steps.size() > 0:
			var d: Vector2 = ArrowAdvance.start_direction(path.steps[0])
			if d.length_squared() > 0.0:
				visual.rotation = d.angle()
				return
	var cursor := get_node_or_null("../Cursor")
	if cursor:
		visual.rotation = (cursor.global_position - global_position).angle()

func _physics_process(_delta: float) -> void:
	var level := get_parent()
	var surfaces: Array = level.surfaces if level and "surfaces" in level else []
	var gm := get_node_or_null("../GameManager")
	var flags: Dictionary = gm.game_state.flags if gm != null and "game_state" in gm else {}
	var colliders: PackedVector2Array = level.player_colliders() if level and level.has_method("player_colliders") else PackedVector2Array()
	var input := {
		"axis_x": Input.get_axis("move_left", "move_right"),
		"axis_y": Input.get_axis("move_up", "move_down"),
		"jump": Input.is_action_just_pressed("move_up"),
	}
	var body: Dictionary = RcgKinematics.step({"pos": global_position, "vel": velocity, "on_floor": _on_floor}, input,
		get_level_gravity(), _defs_cache.defs(surfaces), flags, colliders)
	global_position = body["pos"]
	velocity = body["vel"]
	_on_floor = body["on_floor"]
