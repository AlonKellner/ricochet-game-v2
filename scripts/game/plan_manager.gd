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

# B7 (Stage 113 follow-up, GAME_SPEC 4.2): a circle directional's two hit points
# are the ends of the chord along its normal, and the click picks the end
# nearer it -- the same end for every target exactly when the click is off the
# diameter across the normal. A click within the UI tolerance of that diameter
# is moved off it, to the side of the arc's own midpoint (the via), so no plan
# rests on float32 noise.
const SNAP_PX := 8.0

static func snap_click(surface: Surface, side: Side.Value, click: Vector2, game_state: GameState) -> Vector2:
	if surface == null or surface.segment.is_line():
		return click
	var cfg := surface.active_side_config(side, game_state)
	if cfg == null or not (cfg.effect is CircleDirectionalProjection):
		return click
	var dir: CircleDirectionalProjection = cfg.effect
	var n := dir.normal()
	var c: Vector2 = surface.segment.get_carrier().center()
	var along := (click - c).dot(n)
	if absf(along) >= SNAP_PX:
		return click
	var sign := 1.0 if (surface.segment.get_via().coords - c).dot(n) >= 0.0 else -1.0
	return click + n * (sign * SNAP_PX - along)
