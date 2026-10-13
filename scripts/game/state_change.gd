class_name StateChange
extends RefCounted

# A side's state change (GAME_SPEC 10.6): when the side fires, it writes a
# game-state flag -- SET a value, or TOGGLE its truth value (a missing key is
# false). Any key may be written (inter-surface mechanics).

enum Op { SET, TOGGLE }

var key: String
var op: int
var value

func _init(p_key: String, p_op: int = Op.SET, p_value = true) -> void:
	key = p_key
	op = p_op
	value = p_value

# The flags after this change (a new dictionary; the input is not modified).
func apply(flags: Dictionary) -> Dictionary:
	var out := flags.duplicate()
	if op == Op.TOGGLE:
		out[key] = not bool(flags.get(key, false))
	else:
		out[key] = value
	return out

# Canonical table key of a flag value: its JSON spelling (true / false, an
# integral number without a fraction, a string as is).
static func flag_key(v) -> String:
	if v is bool:
		return "true" if v else "false"
	if v is float and is_finite(v) and v == floorf(v):
		return str(int(v))
	return str(v)

# Scene-def form (oracle format).
func to_def() -> Dictionary:
	return {"key": key, "op": "toggle" if op == Op.TOGGLE else "set", "value": true if op == Op.TOGGLE else value}
