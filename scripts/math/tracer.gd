class_name Tracer
extends RefCounted

# Trace types live in their own classes (TraceStep / TracePath); these
# aliases keep existing references working.
const Step = preload("res://scripts/math/trace_step.gd")
const TracedPath = preload("res://scripts/math/trace_path.gd")

class TraceState:
	var path: TracedPath
	var state_copy: GameState
	var transform_stack: Array = []
	var frame: MobiusTransform
	var shared_ray: Ray
	var ray: Ray
	var current_mode: int
	var plan_index: int = 0
	var plan_matched: bool = true
	var cursor_injected: bool = false
	var frame_dirty: bool = true
	var norm_surfaces: Array = []
	var norm_to_surface: Dictionary = {}
	var aim_point_pt: Point
	var aim_in_frame: Vector2
	var step_left_blocked: bool = false
	var step_right_blocked: bool = false
	var hit_count: int = 0
	var cursor_hp: Intersection.HitRecord
	var origin_hp: Intersection.HitRecord
	var origin_on_surface: Surface = null
	var transform_sources: Array = []
	var cursor_injected_at_zero_length: bool = false
	var portal_gap_pending: bool = false

enum TraceMode { PHYSICAL = 0, PLANNED = 1 }

const MAX_HITS := 32

static func trace_ray(initial_ray: Ray, surfaces: Array, game_state: GameState) -> TracedPath:
	return trace(initial_ray.origin.coords, initial_ray.direction, surfaces, game_state, initial_ray)

static func trace(origin: Vector2, direction: Direction, surfaces: Array, game_state: GameState, shared_ray: Ray = null, _target_dist: float = -1.0, mode: int = TraceMode.PHYSICAL, post_cursor_mode: int = TraceMode.PHYSICAL, plan_entries: Array = [], cache: TransformCache = null, cursor_pos: Vector2 = Vector2(INF, INF)) -> TracedPath:
	var s := TraceState.new()
	s.path = TracedPath.new()
	if direction.is_zero_length():
		return s.path
	if cache == null:
		cache = TransformCache.new()

	s.state_copy = game_state.copy()
	s.frame = MobiusTransform.identity()
	if shared_ray == null:
		shared_ray = Ray.from_coords(origin, direction)
	s.shared_ray = shared_ray
	s.ray = Ray.from_coords(origin, direction)
	s.current_mode = mode
	if cursor_pos.x != INF:
		s.aim_point_pt = Point.at(cursor_pos)
	else:
		s.aim_point_pt = Point.at(direction.end.coords)
	s.aim_in_frame = s.aim_point_pt.coords

	while s.hit_count < MAX_HITS:
		if s.frame_dirty:
			_recompute_frame(s, surfaces, cache)

		var hitpoints = _assemble_hitpoints(s, plan_entries)
		if hitpoints.is_empty():
			break


# --- Walk stage hitpoints ---
		var _arc: bool = s.frame.maps_lines_to_arcs()
		var step_origin_pos := s.ray.origin.coords
		var walk_t := 0.0
		var trace_done := false
		var stage_ended := false
		var has_wrapped := false

		for hp_idx in hitpoints.size():
			var hp: Intersection.HitRecord = hitpoints[hp_idx]
			s.hit_count += 1
			if s.hit_count > MAX_HITS:
				trace_done = true
				break

			var vis_start: Vector2 = s.frame.apply(step_origin_pos)
			var vis_end: Vector2 = s.frame.apply(hp.point.coords)
			var is_wrap := hp.t < walk_t
			var is_null_seg := hp.segment == null
			var is_cursor := hp == s.cursor_hp
			var is_origin := hp == s.origin_hp
			var step_hit: Intersection.HitRecord = null if is_null_seg else hp

			var _start_inf := is_inf(vis_start.x) or is_inf(vis_start.y)
			var _end_inf := is_inf(vis_end.x) or is_inf(vis_end.y)
			var step_is_arc := _arc and not _start_inf and not _end_inf
			var vis_via: Vector2
			if _start_inf or _end_inf:
				vis_via = Vector2(INF, INF)
			elif is_wrap:
				vis_via = s.frame.apply(Vector2(INF, INF))
			else:
				vis_via = s.frame.apply((step_origin_pos + hp.point.coords) / 2.0)
				if is_inf(vis_via.x) or is_inf(vis_via.y):
					vis_via = s.frame.apply(Vector2(INF, INF))

			# --- Zero-length skip ---
			if vis_start == vis_end:
				if is_origin:
					if hp_idx == 0 and not _start_inf:
						_try_escape(s, vis_start, step_origin_pos)
					trace_done = true
					break
				if is_cursor:
					_inject_cursor(s, post_cursor_mode)
				if not is_null_seg:
					_accumulate_blockage(s, hp)
					if s.step_left_blocked and s.step_right_blocked:
						var orig_surf_zl: Surface = s.norm_to_surface.get(hp.segment)
						if orig_surf_zl and orig_surf_zl.is_target and hp.on_segment:
							s.path.targets_hit[orig_surf_zl.id] = true
						var result_zl = _apply_effect(s, hp, true, orig_surf_zl, plan_entries, cache)
						if result_zl == 2:
							if s.cursor_injected_at_zero_length and hp.at_endpoint > 0:
								s.step_left_blocked = false
								s.step_right_blocked = false
							else:
								trace_done = true
								break
						if result_zl == 1:
							stage_ended = true
							break
				walk_t = hp.t
				step_origin_pos = hp.point.coords
				continue

			s.cursor_injected_at_zero_length = false

			if is_wrap and not is_null_seg:
				has_wrapped = true

			# --- Origin: apply effect if wrapped, otherwise escape ---
			if is_origin:
				if has_wrapped:
					s.path.steps.append(Step.new(vis_start, vis_end, s.frame.id, step_hit, s.shared_ray, s.frame, vis_via, step_is_arc))
					if s.portal_gap_pending:
						s.path.steps.back().after_portal = true
						s.portal_gap_pending = false
					if hp.segment != null:
						var origin_surf: Surface = s.norm_to_surface.get(hp.segment)
						if origin_surf:
							var last_step: Step = s.path.steps.back()
							last_step.surface_id = origin_surf.id
							last_step.hit_on_segment = hp.on_segment
							var ls: Side.Value = hp.side
							if s.frame.conjugating:
								ls = Side.Value.RIGHT if hp.side == Side.Value.LEFT else Side.Value.LEFT
							last_step.hit_side = ls
						var origin_result = _apply_effect(s, hp, true, origin_surf, plan_entries, cache)
						if origin_result == 1:
							stage_ended = true
							break
						elif origin_result == 2:
							trace_done = true
							break
					trace_done = true
					break
				elif not _start_inf:
					_try_escape(s, vis_start, step_origin_pos)
				trace_done = true
				break

			# --- Generate visual step ---
			s.path.steps.append(Step.new(vis_start, vis_end, s.frame.id, step_hit, s.shared_ray, s.frame, vis_via, step_is_arc))
			if s.portal_gap_pending:
				s.path.steps.back().after_portal = true
				s.portal_gap_pending = false

			# --- Cursor check ---
			if is_cursor:
				_inject_cursor(s, post_cursor_mode)
				walk_t = hp.t
				step_origin_pos = hp.point.coords
				continue

			# --- Target tracking ---
			var orig_surf: Surface = s.norm_to_surface.get(hp.segment)
			if orig_surf:
				var last_step: Step = s.path.steps.back()
				last_step.surface_id = orig_surf.id
				last_step.hit_on_segment = hp.on_segment
				var ls: Side.Value = hp.side
				if s.frame.conjugating:
					ls = Side.Value.RIGHT if hp.side == Side.Value.LEFT else Side.Value.LEFT
				last_step.hit_side = ls
				if orig_surf.is_target and hp.on_segment:
					s.path.targets_hit[orig_surf.id] = true

			# --- Blockage ---
			_accumulate_blockage(s, hp)
			var fully_blocked := s.step_left_blocked and s.step_right_blocked

			var result = _apply_effect(s, hp, fully_blocked, orig_surf, plan_entries, cache)
			if result == 2:
				trace_done = true
				break
			if result == 1:
				stage_ended = true
				break
			walk_t = hp.t
			step_origin_pos = hp.point.coords

		if trace_done:
			break
		if s.cursor_injected_at_zero_length:
			s.cursor_injected_at_zero_length = false
			continue
		if not stage_ended:
			break

	s.path.hit_count = s.hit_count
	return s.path

static func _inject_cursor(s: TraceState, post_cursor_mode: int) -> void:
	var was_planned := s.current_mode != TraceMode.PHYSICAL
	s.path.cursor_index = s.path.steps.size()
	s.cursor_injected = true
	s.current_mode = post_cursor_mode
	if was_planned:
		s.cursor_injected_at_zero_length = true

static func _try_escape(s: TraceState, vis_start: Vector2, step_origin_pos: Vector2) -> void:
	var p_dir := s.ray.direction.to_vector().normalized()
	var vis_shifted: Vector2 = s.frame.apply(step_origin_pos + p_dir)
	if not (is_inf(vis_shifted.x) or is_inf(vis_shifted.y)):
		var vis_dir: Vector2 = (vis_shifted - vis_start).normalized()
		_add_escape_steps(s.path, vis_start, vis_dir, s.frame, s.shared_ray, step_origin_pos, p_dir)

static func _accumulate_blockage(s: TraceState, hp: Intersection.HitRecord) -> void:
	s.step_left_blocked = s.step_left_blocked or hp.blocked_left
	s.step_right_blocked = s.step_right_blocked or hp.blocked_right
	if hp.at_endpoint > 0 and not (s.step_left_blocked and s.step_right_blocked):
		for ns in s.norm_surfaces:
			if ns.segment == hp.segment:
				continue
			var ep := Intersection.at_which_endpoint(hp.point.coords, ns.segment)
			if ep > 0:
				var sides := Intersection.endpoint_blocked_sides(
					hp.point.coords, ns.segment, s.ray, ep)
				s.step_left_blocked = s.step_left_blocked or sides[0]
				s.step_right_blocked = s.step_right_blocked or sides[1]

static func _apply_effect(s: TraceState, hp: Intersection.HitRecord, fully_blocked: bool, orig_surf: Surface, plan_entries: Array, _cache: TransformCache = null) -> int:
	var norm_surf: Surface = null
	for ns in s.norm_surfaces:
		if ns.segment == hp.segment:
			norm_surf = ns
			break

	var do_apply := false
	var effect_config: SideConfig = null
	var lookup_side: Side.Value = hp.side
	if s.frame.conjugating:
		lookup_side = Side.Value.RIGHT if hp.side == Side.Value.LEFT else Side.Value.LEFT

	if s.current_mode == TraceMode.PHYSICAL:
		if norm_surf and fully_blocked:
			effect_config = norm_surf.active_side_config(lookup_side, s.state_copy)
			if effect_config != null and effect_config.effect != null:
				match effect_config.effect.kind():
					Effect.Kind.TERMINAL:
						return 2
					Effect.Kind.TRANSFORMATIVE:
						do_apply = true
						if s.plan_index < plan_entries.size():
							if orig_surf.id == plan_entries[s.plan_index].surface_id:
								s.plan_index += 1
							else:
								s.plan_matched = false
						else:
							s.plan_matched = false
					Effect.Kind.PROJECTIVE:
						var visual_hit: Vector2 = s.frame.apply(hp.point.coords)
						var phys_cfg := orig_surf.active_side_config(lookup_side, s.state_copy)
						var out_ray: Ray = phys_cfg.effect.apply_forward(visual_hit, orig_surf.segment, lookup_side)
						s.transform_stack.clear()
						s.transform_sources.clear()
						s.ray = out_ray
						s.origin_on_surface = orig_surf
						s.portal_gap_pending = false
						s.frame_dirty = true
						if fully_blocked:
							s.step_left_blocked = false
							s.step_right_blocked = false
						if s.plan_index < plan_entries.size():
							if orig_surf.id == plan_entries[s.plan_index].surface_id:
								s.plan_index += 1
							else:
								s.plan_matched = false
						else:
							s.plan_matched = false
						return 1
	elif s.current_mode == TraceMode.PLANNED:
		if orig_surf and s.plan_index < plan_entries.size():
			var entry: PlanManager.PlanEntry = plan_entries[s.plan_index]
			if orig_surf.id == entry.surface_id and lookup_side == entry.side:
				effect_config = norm_surf.active_side_config(entry.side, s.state_copy) if norm_surf else null
				if effect_config != null and effect_config.effect != null and effect_config.effect.kind() == Effect.Kind.TRANSFORMATIVE:
					do_apply = true
				s.plan_index += 1

	if fully_blocked:
		s.step_left_blocked = false
		s.step_right_blocked = false

	if do_apply:
		var phys_config := orig_surf.active_side_config(lookup_side, s.state_copy)
		var tracked: TrackedTransform = phys_config.effect.get_tracked_transform()
		var should_pop := false
		if not s.transform_stack.is_empty():
			if s.transform_stack.back().is_inverse_of(tracked):
				should_pop = true
			elif tracked.inverse == tracked and s.transform_stack.back().inverse == s.transform_stack.back() and not s.transform_sources.is_empty() and s.transform_sources.back() == orig_surf:
				should_pop = true
		if should_pop:
			s.transform_stack.pop_back()
			s.transform_sources.pop_back()
		else:
			s.transform_stack.append(tracked)
			s.transform_sources.append(orig_surf)
		var new_origin: Vector2 = hp.point.coords
		if tracked.inverse != tracked:
			s.portal_gap_pending = true
		s.ray = Ray.from_coords(new_origin, s.ray.direction)
		if not should_pop and tracked.inverse != tracked:
			var found_target := false
			for ns in s.norm_surfaces:
				var ns_orig: Surface = s.norm_to_surface.get(ns.segment)
				if ns_orig != null and ns_orig != orig_surf:
					for side_val in [Side.Value.LEFT, Side.Value.RIGHT]:
						var cfg := ns_orig.active_side_config(side_val, s.state_copy)
						if cfg != null and cfg.effect != null and cfg.effect.kind() == Effect.Kind.TRANSFORMATIVE:
							if cfg.effect.get_tracked_transform() == tracked.inverse:
								s.origin_on_surface = ns_orig
								found_target = true
								break
					if found_target:
						break
			if not found_target:
				s.origin_on_surface = orig_surf
		else:
			s.origin_on_surface = orig_surf
		s.frame_dirty = true
		return 1

	return 0

static func _assemble_hitpoints(s: TraceState, plan_entries: Array) -> Array:
	var norm_segments: Array = []
	for ns in s.norm_surfaces:
		norm_segments.append(ns.segment)

	var origin_on_seg: Segment = null
	var origin_carrier: GeneralizedCircle = null
	if s.origin_on_surface != null:
		for ns in s.norm_surfaces:
			if s.norm_to_surface.get(ns.segment) == s.origin_on_surface:
				origin_on_seg = ns.segment
				origin_carrier = ns.segment.get_carrier()
				break
		s.origin_on_surface = null

	var visual_carriers: Dictionary = {}
	var pullback_frame: MobiusTransform = null
	if s.frame.id != MobiusTransform.IDENTITY_ID:
		pullback_frame = s.frame
		for ns in s.norm_surfaces:
			var orig_surf = s.norm_to_surface.get(ns.segment)
			if orig_surf != null:
				visual_carriers[ns.segment] = orig_surf.segment.get_carrier()

	var carrier_hits := Intersection.find_all_hits(s.ray, norm_segments, origin_on_seg, origin_carrier, pullback_frame, visual_carriers)
	var origin_hit_seg: Segment = null
	var origin_hit_side: Side.Value = Side.Value.LEFT
	var origin_hit_on_seg: bool = false
	if origin_on_seg != null:
		for h in carrier_hits:
			if h.segment == origin_on_seg and h.t == 0.0:
				origin_hit_seg = h.segment
				origin_hit_side = h.side
				origin_hit_on_seg = h.on_segment
				break
		carrier_hits = carrier_hits.filter(func(h: Intersection.HitRecord) -> bool:
			return h.segment != origin_on_seg or h.t != 0.0)
	s.origin_hp = Intersection.HitRecord.new(0.0, s.ray.origin.coords, origin_hit_seg, origin_hit_side, origin_hit_on_seg)

	var cursor_reachable := not s.cursor_injected and s.plan_index >= plan_entries.size() and s.plan_matched
	if carrier_hits.size() == 0 and not cursor_reachable:
		var vis_origin: Vector2 = s.frame.apply(s.ray.origin.coords)
		if is_inf(vis_origin.x) or is_inf(vis_origin.y):
			return []
		_try_escape(s, vis_origin, s.ray.origin.coords)
		return []

	var hitpoints: Array = carrier_hits.duplicate()
	hitpoints.append(s.origin_hp)
	s.cursor_hp = null
	if cursor_reachable:
		var cursor_t := Intersection.project_point_on_ray(s.ray, s.aim_in_frame)
		s.cursor_hp = Intersection.HitRecord.new(cursor_t, s.aim_in_frame, null, Side.Value.LEFT, false)
		hitpoints.append(s.cursor_hp)
	return Intersection.projective_sort(hitpoints)

static func _recompute_frame(s: TraceState, surfaces: Array, cache: TransformCache) -> void:
	s.frame = MobiusTransform.identity()
	for t in s.transform_stack:
		s.frame = cache.compose_cached(t.mobius, s.frame)
	var cached_norm = cache.get_normalized(s.frame.id)
	if cached_norm != null:
		s.norm_surfaces = cached_norm.surfaces
		s.norm_to_surface = cached_norm.mapping.duplicate()
	else:
		s.norm_surfaces = _build_normalized(surfaces, s.frame, s.norm_to_surface, cache, s.transform_stack)
		cache.set_normalized(s.frame.id, s.norm_surfaces, s.norm_to_surface.duplicate())
	s.frame_dirty = false
	if s.transform_stack.is_empty():
		s.aim_in_frame = s.aim_point_pt.coords
	else:
		var frame_inv := cache.invert_cached(s.frame)
		s.aim_in_frame = frame_inv.apply(s.aim_point_pt.coords)

static func _build_normalized(surfaces: Array, frame: MobiusTransform, out_mapping: Dictionary, cache: TransformCache = null, transform_stack: Array = []) -> Array:
	out_mapping.clear()
	if frame.id == MobiusTransform.IDENTITY_ID:
		for surf in surfaces:
			out_mapping[surf.segment] = surf
		return surfaces

	var frame_inv: MobiusTransform = cache.invert_cached(frame) if cache else frame.invert()

	var isometric: bool = _is_isometric_stack(transform_stack)

	var result: Array = []
	for surf in surfaces:
		var new_seg: Segment
		var carrier_fixed := _carrier_fixed_by_all(surf.segment, transform_stack)
		if carrier_fixed:
			new_seg = surf.segment
		else:
			new_seg = Segment.from_coords(
				frame_inv.apply(surf.segment.start.coords),
				frame_inv.apply(surf.segment.end.coords),
				frame_inv.apply(surf.segment.via.coords))
			new_seg.full = surf.segment.full
			if isometric:
				var orig_carrier: GeneralizedCircle = surf.segment.get_carrier()
				var direct: GeneralizedCircle = orig_carrier.transformed_by(frame_inv)
				if orig_carrier.is_line():
					direct = GeneralizedCircle.from_line(direct.b, direct.c, direct.d)
				elif not direct.is_line():
					direct = GeneralizedCircle.from_circle(direct.center(), orig_carrier.radius())
				new_seg._carrier = direct
		var state := GameState.new()
		var left := _normalize_config(surf.active_side_config(Side.Value.LEFT, state), new_seg)
		var right := _normalize_config(surf.active_side_config(Side.Value.RIGHT, state), new_seg)
		var new_surf := Surface.new(new_seg, left, right, surf.is_target, surf.player_solid)
		out_mapping[new_seg] = surf
		result.append(new_surf)
	return result

static func _carrier_fixed_by_all(seg: Segment, stack: Array) -> bool:
	var carrier := seg.get_carrier()
	for t in stack:
		var tt: TrackedTransform = t
		if not (tt.inverse == tt and tt.carrier != null and tt.carrier == carrier):
			return false
	return true

static func _is_isometric_stack(stack: Array) -> bool:
	for t in stack:
		var tt: TrackedTransform = t
		if tt.carrier == null or not tt.carrier.is_line():
			return false
	return true

static func _normalize_config(config: SideConfig, norm_seg: Segment) -> SideConfig:
	if config == null or config.effect == null:
		return config
	var norm_effect: Effect = config.effect.normalized(norm_seg.get_carrier())
	if norm_effect != config.effect:
		return SideConfig.new(norm_effect, config.interactive)
	return config

static func _add_escape_steps(path: TracedPath, vis_origin: Vector2, vis_dir: Vector2, frame: MobiusTransform, shared_ray: Ray, phys_origin: Vector2 = Vector2.ZERO, phys_dir: Vector2 = Vector2.ZERO) -> void:
	var _arc := frame.maps_lines_to_arcs()
	var escape_end: Vector2
	var return_start: Vector2
	var via_fwd: Vector2
	var via_ret: Vector2
	if _arc:
		var t_inf := frame.apply(Vector2(INF, INF))
		escape_end = t_inf
		return_start = t_inf
		if phys_dir != Vector2.ZERO:
			var pole_pos := frame.pole()
			var t_to_pole := (pole_pos - phys_origin).dot(phys_dir)
			var half_dist := absf(t_to_pole) / 2.0
			via_fwd = frame.apply(phys_origin + phys_dir * half_dist)
			via_ret = frame.apply(phys_origin - phys_dir * half_dist)
		else:
			via_fwd = t_inf
			via_ret = t_inf
	else:
		escape_end = Vector2(INF, INF)
		return_start = Vector2(INF, INF)
		via_fwd = vis_dir
		via_ret = -vis_dir
	if vis_origin != escape_end:
		path.steps.append(Step.new(vis_origin, escape_end, frame.id, null, shared_ray, frame, via_fwd, _arc))
	if return_start != vis_origin:
		path.steps.append(Step.new(return_start, vis_origin, frame.id, null, shared_ray, frame, via_ret, _arc))

