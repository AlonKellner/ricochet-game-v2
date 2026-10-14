class_name PlanManager
extends RefCounted

class PlanEntry extends RefCounted:
	var surface_id: int
	var side: Side.Value
	# where the player clicked the surface (INF: unknown); settles a fork of
	# the planned path at a circle projective (the hit point nearer the click)
	var click: Vector2 = Vector2(INF, INF)

	func _init(p_surface_id: int, p_side: Side.Value, p_click: Vector2 = Vector2(INF, INF)) -> void:
		surface_id = p_surface_id
		side = p_side
		click = p_click

var entries: Array = []

func add_entry(surface_id: int, side: Side.Value, click: Vector2 = Vector2(INF, INF)) -> void:
	entries.append(PlanEntry.new(surface_id, side, click))

func remove_last_of(surface_id: int) -> void:
	for i in range(entries.size() - 1, -1, -1):
		var entry: PlanEntry = entries[i]
		if entry.surface_id == surface_id:
			entries.remove_at(i)
			return

func clear() -> void:
	entries.clear()

func is_empty() -> bool:
	return entries.size() == 0

func has_surface(surface_id: int) -> bool:
	for entry in entries:
		if entry.surface_id == surface_id:
			return true
	return false

func restore_from(p_entries: Array) -> void:
	entries.clear()
	for entry in p_entries:
		entries.append(PlanEntry.new(entry.surface_id, entry.side, entry.click))
