// Bindings of the invariant kernel and the native sweep (Stage 98).
// InvariantKernel.check_all takes what InvariantChecker.check_all read from
// the renderer and the scene; the GDScript InvariantChecker is the thin driver
// that gathers them from a live scene. SweepKernel.run checks whole sweeps
// without a scene tree.
#pragma once

#include "gd_ladder.hpp"
#include "gd_tracer.hpp"
#include "gd_visual.hpp"
#include "rc/invariants.hpp"
#include "rc/sweep.hpp"

#include <godot_cpp/variant/packed_vector2_array.hpp>

class InvariantKernel : public godot::RefCounted {
	GDCLASS(InvariantKernel, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// surfaces: [{id, segment: Segment, has_effect: bool}] (InvariantChecker.surface_infos)
	static godot::Array check_all(const godot::Vector2 &player, const godot::Vector2 &cursor, const godot::Ref<TracePath> &physical,
			const godot::Ref<TracePath> &planned, const godot::Array &typed, const godot::Ref<PrecisionLadderResult> &result,
			const godot::Array &surfaces, const godot::Array &defs, const godot::Dictionary &flags, const godot::Array &plan_entries,
			bool has_renderer, bool plan_applied, bool emit_infinity = false);
	// Stage 110: VISIBILITY-EQUALS-NON-DIVERGENCE at the cursor (region: a VisibilityRegion)
	static godot::Array check_visibility_pin(const godot::Ref<godot::RefCounted> &region, const godot::Vector2 &cursor,
			const godot::Ref<TracePath> &planned, const godot::Array &typed);
	static godot::Array check_direction_only(const godot::Vector2 &player, const godot::Vector2 &cursor, const godot::Ref<PrecisionLadderResult> &result,
			const godot::Array &defs, const godot::Dictionary &flags, const godot::Array &plan_entries, bool emit_infinity = false);
	static godot::Array check_effect_law(const godot::Array &events, const godot::Ref<BeamScene> &scene);
	static godot::Array check_event_on_beam(const godot::Array &events, const godot::Ref<BeamScene> &scene);
	static godot::Array check_beam_order(const godot::Array &events);
	static godot::Array check_reapply_in_place(const godot::Array &events);
	static godot::Array check_plan_effects_applied_events(const godot::Array &events, const godot::Array &plan, bool cursor_reached);
	static bool is_infinity_gap(const godot::Vector2 &prev_end, const godot::Vector2 &curr_start);
	static bool is_at_bounds(const godot::Vector2 &p, const godot::Rect2 &bounds);
	static double geometric_carrier_dist(const godot::Vector2 &point, const godot::Ref<GeneralizedCircle> &carrier);
	static godot::Array extract_trace_geometry(const godot::Ref<TracePath> &path);
};

class SweepKernel : public godot::RefCounted {
	GDCLASS(SweepKernel, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// plans: [[PlanEntry, ...], ...]; returns the failing combos:
	// [{plan_index, player, cursor, violations: [String]}]
	static godot::Array run(const godot::Array &defs, const godot::Array &surfaces, const godot::PackedVector2Array &positions, const godot::Array &plans,
			const godot::Dictionary &flags, int shard_i, int shard_n, bool skip_equal, bool emit_infinity = false, bool visibility = false);
};

// shared converters (gd_invariants.cpp)
std::vector<rc::inv::EvInfo> ev_infos_of(const godot::Array &events);
std::vector<rc::inv::SurfaceInfo> surface_infos_of(const godot::Array &surfaces);
rc::TracePath core_path_of(const godot::Ref<TracePath> &path);
godot::Array render_violations(const rc::inv::Violations &vs);
