class_name SceneDefsCache
extends RefCounted

# The beam solver's scene defs and the id -> Segment map of a surfaces array,
# built once per array instead of once per frame (Stage 100). The key is the
# Array object itself and its size: scenes append their surfaces in _ready and
# never edit a Surface afterwards, and a structural change (a spawn landing, a
# group leaving, undo; Stage 107) replaces the array (World.surfaces), so the
# same array with the same count means the same defs. `version` counts the
# rebuilds (the dirty keys use it).

var version := 0
var _src: Array = []
var _n := -1
var _defs: Array = []
var _segments: Dictionary = {}

func defs(surfaces: Array) -> Array:
	_refresh(surfaces)
	return _defs

func segments(surfaces: Array) -> Dictionary:
	_refresh(surfaces)
	return _segments

# The version of the defs of `surfaces` (rebuilding them when needed).
func version_of(surfaces: Array) -> int:
	_refresh(surfaces)
	return version

func _refresh(surfaces: Array) -> void:
	if _n == surfaces.size() and is_same(surfaces, _src):
		return
	_src = surfaces
	_n = surfaces.size()
	_defs = SceneDefs.defs_from_surfaces(surfaces)
	_segments = SceneDefs.segments_by_id(surfaces)
	version += 1
