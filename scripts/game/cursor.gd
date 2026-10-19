extends Node2D

const CROSSHAIR_SIZE := 10.0
const CROSSHAIR_COLOR := Color.WHITE
const CROSSHAIR_WIDTH := 1.5

func _process(_delta: float) -> void:
	# moving a CanvasItem does not need a redraw: the crosshair is drawn in local coordinates
	global_position = get_global_mouse_position()

func _draw() -> void:
	DrawRec.begin(self)
	DrawRec.line(self, Vector2(-CROSSHAIR_SIZE, 0), Vector2(CROSSHAIR_SIZE, 0), CROSSHAIR_COLOR, CROSSHAIR_WIDTH)
	DrawRec.line(self, Vector2(0, -CROSSHAIR_SIZE), Vector2(0, CROSSHAIR_SIZE), CROSSHAIR_COLOR, CROSSHAIR_WIDTH)
