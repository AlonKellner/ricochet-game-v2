class_name ChevronOverlayBuilder
extends RefCounted

const HOVER_COLOR := Color(1.0, 1.0, 1.0, 0.3)
const PLAN_VALID_COLOR := Color(1.0, 1.0, 1.0, 0.15)
const PLAN_INVALID_COLOR := Color(1.0, 0.0, 0.0, 0.15)

static func build_hover_overlays(surface: Surface, side: int, game_state: GameState = null) -> Dictionary:
	var link := surface.overlay_link(side, game_state)
	if link == null:
		return {}
	var result := {}
	if link.outgoing == null:
		_add_overlay(result, surface.id,
			ChevronOverlay.hover(side, false, -1, HOVER_COLOR))
	elif link.outgoing == link:
		_add_overlay(result, surface.id,
			ChevronOverlay.hover(side, true, side, HOVER_COLOR))
	else:
		_add_overlay(result, surface.id,
			ChevronOverlay.hover(side, false, -1, HOVER_COLOR))
		_add_overlay(result, link.outgoing.surface.id,
			ChevronOverlay.hover(link.outgoing.side, true, link.outgoing.side, HOVER_COLOR, false))
	return result

static func build_plan_overlays(plan: PlanManager, physical_hits: Dictionary,
		get_surface: Callable, game_state: GameState = null) -> Dictionary:
	var result := {}
	for i in plan.entries.size():
		var entry: PlanManager.PlanEntry = plan.entries[i]
		var surface: Surface = get_surface.call(entry.surface_id)
		if surface == null:
			continue
		var link := surface.overlay_link(entry.side, game_state)
		if link == null:
			continue
		var incoming_valid := _check_incoming(entry, physical_hits, surface)
		var outgoing_valid := _check_outgoing(entry, physical_hits, surface)
		var in_color := PLAN_VALID_COLOR if incoming_valid else PLAN_INVALID_COLOR
		var out_color := PLAN_VALID_COLOR if outgoing_valid else PLAN_INVALID_COLOR
		var grad_color := in_color

		# the entry's piece (Stage 108); the outgoing side of the same surface is
		# the same piece, a portal partner's is its whole surface
		var out_piece: int = entry.piece if link.outgoing != null and link.outgoing.surface == surface else -1
		if link.outgoing == null:
			_add_overlay(result, surface.id,
				_on(ChevronOverlay.plan(entry.side, false, -1, in_color, out_color, grad_color), entry.piece))
		elif link.outgoing == link:
			_add_overlay(result, surface.id,
				_on(ChevronOverlay.plan(entry.side, true, entry.side, in_color, out_color, grad_color), entry.piece))
		else:
			_add_overlay(result, surface.id,
				_on(ChevronOverlay.plan(entry.side, false, -1, in_color, out_color, grad_color), entry.piece))
			_add_overlay(result, link.outgoing.surface.id,
				_on(ChevronOverlay.plan(link.outgoing.side, true, link.outgoing.side,
					in_color, out_color, grad_color, false), out_piece))
	return result

static func _on(ov: ChevronOverlay, piece: int) -> ChevronOverlay:
	ov.piece = piece
	return ov

# A physical hit on the entry's piece (Stage 108: inside its span, if it names one).
static func _on_piece(entry: PlanManager.PlanEntry, h: Dictionary, surface: Surface) -> bool:
	return entry.span == null or Pieces.span_contains(surface, entry.span, h.get("point", Vector2.INF))

static func _check_incoming(entry: PlanManager.PlanEntry, physical_hits: Dictionary, surface: Surface = null) -> bool:
	var hits: Array = physical_hits.get(entry.surface_id, [])
	for h in hits:
		if h.side == entry.side and h.on_segment and _on_piece(entry, h, surface):
			return true
	return false

static func _check_outgoing(entry: PlanManager.PlanEntry, physical_hits: Dictionary, surface: Surface = null) -> bool:
	var hits: Array = physical_hits.get(entry.surface_id, [])
	for h in hits:
		if h.side == entry.side and h.on_segment and h.has_continuation and _on_piece(entry, h, surface):
			return true
	return false

static func _add_overlay(result: Dictionary, surface_id: int, overlay: ChevronOverlay) -> void:
	if not result.has(surface_id):
		result[surface_id] = []
	result[surface_id].append(overlay)
