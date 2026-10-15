extends CharacterBody2D

const SPEED := 200.0
const COLLISION_RADIUS := 12.0
const JUMP_VELOCITY := 400.0

func get_level_gravity() -> Vector2:
	var parent := get_parent()
	if parent and parent.has_method("get") and "gravity" in parent:
		return parent.gravity
	return Vector2.ZERO

func has_level_gravity() -> bool:
	return get_level_gravity().length_squared() > 0.0

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

func _physics_process(delta: float) -> void:
	var gravity_vec := get_level_gravity()

	if has_level_gravity():
		if not is_on_floor():
			velocity += gravity_vec * delta
		else:
			velocity.y = 0.0

		var horizontal := Input.get_axis("move_left", "move_right")
		velocity.x = horizontal * SPEED

		if is_on_floor() and Input.is_action_just_pressed("move_up"):
			velocity.y = -JUMP_VELOCITY
	else:
		var input_dir := Vector2(
			Input.get_axis("move_left", "move_right"),
			Input.get_axis("move_up", "move_down"),
		)
		if input_dir.length_squared() > 0.0:
			input_dir = input_dir.normalized()
		velocity = input_dir * SPEED

	move_and_slide()
