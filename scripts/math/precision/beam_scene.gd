class_name BeamScene
extends RefCounted

# STUB (Stage 81 red): scene of segment surfaces with per-side effect types.

class Surf extends RefCounted:
	var id: int
	var seg: BeamGeom.Seg
	var left_type: String = ""     # "" (pass) | terminal | reflection | inversion
	var right_type: String = ""
	var is_target: bool = false

var surfaces: Array = []
var by_id: Dictionary = {}

static func from_defs(_ctx: NumCtx, _defs: Array) -> BeamScene:
	return null

static func defs_from_surfaces(_game_surfaces: Array) -> Array:
	return []

static func supported(_defs: Array) -> bool:
	return false
