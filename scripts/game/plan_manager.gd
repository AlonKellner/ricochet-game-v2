class_name PlanManager
extends RefCounted

class PlanEntry extends RefCounted:
	var surface_id: int
	var side: Side.Value
	# where the player clicked the surface (INF: unknown); settles a fork of
	# the planned path at a circle projective (the hit point nearer the click)
	var click: Vector2 = Vector2(INF, INF)
	# Stage 108: the piece planned (GAME_SPEC 9.7): its index (-1: the whole
	# surface) and its span on the surface's carrier (the solver matches hits
	# inside it only; null: the whole surface)
	var piece := -1
	var span = null

	func _init(p_surface_id: int, p_side: Side.Value, p_click: Vector2 = Vector2(INF, INF), p_piece := -1, p_span = null) -> void:
		surface_id = p_surface_id
		side = p_side
		click = p_click
		piece = p_piece
		span = p_span

	func copy() -> PlanEntry:
		return PlanEntry.new(surface_id, side, click, piece, span)

var entries: Array = []
# Bumped by every change (the per-frame dirty keys compare it; Stage 100).
var version := 0

func add_entry(surface_id: int, side: Side.Value, click: Vector2 = Vector2(INF, INF), piece: Pieces.Piece = null) -> void:
	entries.append(PlanEntry.new(surface_id, side, click, piece.index if piece != null else -1,
		piece.span if piece != null else null))
	version += 1

func remove_last_of(surface_id: int) -> void:
	for i in range(entries.size() - 1, -1, -1):
		var entry: PlanEntry = entries[i]
		if entry.surface_id == surface_id:
			entries.remove_at(i)
			version += 1
			return

# Drops every entry whose surface id fails `keep` (surfaces that left the
# world, Stage 107); the infinity entry stays.
func retain(keep: Callable) -> void:
	var kept := entries.filter(func(e: PlanEntry): return e.surface_id == BeamSolver.INFINITY_ID or keep.call(e.surface_id))
	if kept.size() != entries.size():
		entries = kept
		version += 1

func clear() -> void:
	entries.clear()
	version += 1

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
		entries.append(entry.copy())
	version += 1
