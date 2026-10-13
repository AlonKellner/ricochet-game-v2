class_name StateChange
extends RefCounted

# A side's state change (GAME_SPEC 10.6): writes a game-state flag when the
# side fires. Stage 85 stub.

enum Op { SET, TOGGLE }

var key: String
var op: int
var value

func _init(p_key: String, p_op: int = Op.SET, p_value = true) -> void:
	key = p_key
	op = p_op
	value = p_value

func apply(flags: Dictionary) -> Dictionary:
	return flags

static func flag_key(_v) -> String:
	return ""

func to_def() -> Dictionary:
	return {}
