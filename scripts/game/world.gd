class_name World
extends RefCounted

# The level's surfaces over time (Stage 107, GAME_SPEC 10.6): the static
# surfaces (hosts included -- presence hides spent ones) and, per channel,
# the group the last landed spawn placed. A group stays while its channel's
# flag names its generation; a later spawn or a reset drops it (undo brings
# it back from a snapshot: the same Surface instances, so ids and plans hold).
# `surfaces` is a NEW array after every structural change: the scene defs
# cache keys on the array's identity (SceneDefsCache).

var surfaces: Array[Surface] = []
var generations := {}   # channel -> the last generation landed (monotonic)
var _static: Array[Surface] = []
var _groups := {}       # channel -> {"generation": int, "surfaces": Array[Surface]}
var _initial := {}      # the world as built (a checkpoint taken before it existed)

func _init(p_static: Array[Surface] = []) -> void:
	_static = p_static.duplicate()
	_rebuild()
	_initial = snapshot()

func initial_snapshot() -> Dictionary:
	return _initial

func by_id() -> Dictionary:
	var out := {}
	for s in surfaces:
		out[s.id] = s
	return out

func group_of(channel: String) -> Array:
	return _groups[channel]["surfaces"] if _groups.has(channel) else []

# Applies a landing (LandingFold.fold): the placed groups join, the groups no
# longer current under the landed flags leave. -> {"added", "removed"}.
func land(landing: Dictionary) -> Dictionary:
	var before := surfaces.duplicate()
	generations = landing["generations"].duplicate()
	for p: Dictionary in landing["placed"]:
		var spec: SurfaceSpec = p["spec"]
		var errors := spec.validate()
		if not errors.is_empty():
			assert(false, "spawn into %s: %s" % [p["channel"], ", ".join(errors)])
			push_error("spawn into %s: %s" % [p["channel"], ", ".join(errors)])
			continue
		_groups[p["channel"]] = {"generation": p["generation"], "surfaces": spec.build()}
	var flags: Dictionary = landing["flags"]
	for ch in _groups.keys():
		if flags.get(Channel.key(ch)) != Channel.generation_value(_groups[ch]["generation"]):
			_groups.erase(ch)
	_rebuild()
	return _diff(before)

func snapshot() -> Dictionary:
	var groups := {}
	for ch in _groups:
		groups[ch] = {"generation": _groups[ch]["generation"], "surfaces": _groups[ch]["surfaces"].duplicate()}
	return {"groups": groups, "generations": generations.duplicate()}

func restore(snap: Dictionary) -> Dictionary:
	var before := surfaces.duplicate()
	_groups = {}
	for ch in snap["groups"]:
		var g: Dictionary = snap["groups"][ch]
		_groups[ch] = {"generation": g["generation"], "surfaces": g["surfaces"].duplicate()}
	generations = snap["generations"].duplicate()
	_rebuild()
	return _diff(before)

func _rebuild() -> void:
	var out: Array[Surface] = []
	out.append_array(_static)
	var channels := _groups.keys()
	channels.sort()
	for ch in channels:
		out.append_array(_groups[ch]["surfaces"])
	surfaces = out

func _diff(before: Array) -> Dictionary:
	var added: Array[Surface] = []
	var removed: Array[Surface] = []
	for s in surfaces:
		if not before.has(s):
			added.append(s)
	for s in before:
		if not surfaces.has(s):
			removed.append(s)
	return {"added": added, "removed": removed}
