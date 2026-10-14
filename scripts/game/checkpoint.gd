class_name CheckpointData
extends RefCounted

var player_position: Vector2
var player_velocity: Vector2
var game_state: GameState
var plan_entries: Array
var targets_hit: Dictionary

func _init(
	p_position: Vector2,
	p_velocity: Vector2,
	p_game_state: GameState,
	p_plan_entries: Array,
	p_targets_hit: Dictionary,
) -> void:
	player_position = p_position
	player_velocity = p_velocity
	game_state = p_game_state.copy()
	plan_entries = _copy_entries(p_plan_entries)
	targets_hit = p_targets_hit.duplicate()

static func _copy_entries(entries: Array) -> Array:
	var result: Array = []
	for entry in entries:
		result.append(PlanManager.PlanEntry.new(entry.surface_id, entry.side, entry.click))
	return result
