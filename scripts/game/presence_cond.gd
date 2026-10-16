class_name PresenceCond
extends RefCounted

# A presence condition (GAME_SPEC 9.6): the surface exists while the flag
# `key`'s value (its StateChange.flag_key) is one of `values`; DEFAULT
# matches a missing key. A surface's conditions are all required.

const DEFAULT := "DEFAULT"

var key: String
var values: PackedStringArray

func _init(p_key: String = "", p_values: PackedStringArray = PackedStringArray()) -> void:
	key = p_key
	values = p_values

func holds(flags: Dictionary) -> bool:
	var v := StateChange.flag_key(flags[key]) if flags.has(key) else DEFAULT
	return values.has(v)

static func all_hold(conds: Array, flags: Dictionary) -> bool:
	for c: PresenceCond in conds:
		if not c.holds(flags):
			return false
	return true

# Scene-def form (oracle format).
func to_def() -> Dictionary:
	return {"key": key, "values": Array(values)}
