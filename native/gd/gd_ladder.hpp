// Bindings of the precision ladder (Stage 95): ErrorEstimate (+ Sensitivity),
// PrecisionLadder (+ Result). Float constants (C, SCALE, MARGIN_SAFETY, TOL)
// and the TIERS array are static methods: GDExtension has no static
// properties, so `ErrorEstimate.C` becomes `ErrorEstimate.C()` at the flip.
#pragma once

#include "gd_solver.hpp"
#include "rc/ladder.hpp"

class ErrorEstimateSensitivity : public godot::RefCounted {
	GDCLASS(ErrorEstimateSensitivity, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rc::estimate::Sensitivity sens;
	godot::Array events; // the f64 (dual value) events, when the sensitivity came from a walk

	static godot::Ref<ErrorEstimateSensitivity> wrap(rc::estimate::Sensitivity s, godot::Array ev);
	godot::Array get_gain() const;
	godot::Array get_margin() const;
	double get_max_gain() const { return sens.max_gain; }
	int get_unreliable_from() const { return sens.unreliable_from; }
	godot::Array get_events() const { return events; }
};

class ErrorEstimate : public godot::RefCounted {
	GDCLASS(ErrorEstimate, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static double C() { return rc::estimate::C; }
	static double SCALE() { return rc::estimate::SCALE; }
	static double MARGIN_SAFETY() { return rc::estimate::MARGIN_SAFETY; }
	static godot::Ref<ErrorEstimateSensitivity> physical_sensitivity(const godot::Array &defs, const godot::Array &player, const godot::Array &cursor,
			const godot::Array &plan, int max_events, const godot::Variant &fsc = godot::Variant(), const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<ErrorEstimateSensitivity> planned_sensitivity(const godot::Array &defs, const godot::Array &player, const godot::Array &cursor,
			const godot::Array &plan, int max_events, const godot::Variant &fsc = godot::Variant(), const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<ErrorEstimateSensitivity> replay(const godot::Array &events, const godot::Array &defs, const godot::Variant &fsc = godot::Variant());
	static int unreliable_index(const godot::Ref<ErrorEstimateSensitivity> &sens, double ctx_eps);
	static godot::Array estimate(const godot::Ref<ErrorEstimateSensitivity> &sens, double ctx_eps);
	static int required_limbs(const godot::Ref<ErrorEstimateSensitivity> &sens, double tol, const godot::Array &tiers);
};

class PrecisionLadderResult : public godot::RefCounted {
	GDCLASS(PrecisionLadderResult, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	godot::Array planned;
	godot::Array planned_err;
	int planned_tier = 0;
	godot::Array physical;
	godot::Array physical_err;
	int physical_tier = 0;
	bool converged = false;
	godot::Array tiers_tried;
	int64_t usec = 0;
	int64_t usec_planned = 0;
	int64_t usec_physical = 0;
	godot::Ref<ErrorEstimateSensitivity> physical_sens;
	bool planned_from_physical = false;

	static godot::Ref<PrecisionLadderResult> wrap(const rc::LadderResult &r);

	godot::Array get_planned() const { return planned; }
	godot::Array get_planned_err() const { return planned_err; }
	int get_planned_tier() const { return planned_tier; }
	godot::Array get_physical() const { return physical; }
	godot::Array get_physical_err() const { return physical_err; }
	int get_physical_tier() const { return physical_tier; }
	bool get_converged() const { return converged; }
	godot::Array get_tiers_tried() const { return tiers_tried; }
	int64_t get_usec() const { return usec; }
	int64_t get_usec_planned() const { return usec_planned; }
	int64_t get_usec_physical() const { return usec_physical; }
	godot::Ref<ErrorEstimateSensitivity> get_physical_sens() const { return physical_sens; }
	bool get_planned_from_physical() const { return planned_from_physical; }
};

class PrecisionLadder : public godot::RefCounted {
	GDCLASS(PrecisionLadder, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Array tiers();
	static godot::Ref<PrecisionLadderResult> solve(const godot::Array &defs, const godot::Array &player, const godot::Array &cursor,
			const godot::Array &plan, int max_planned, int max_physical, double tol, bool verify = false,
			const godot::Variant &fsc = godot::Variant(), const godot::Dictionary &flags = godot::Dictionary());
	static godot::Ref<PrecisionLadderResult> solve_physical(const godot::Array &defs, const godot::Array &player, const godot::Array &cursor,
			const godot::Array &plan, int max_physical, double tol, const godot::Variant &fsc = godot::Variant(),
			const godot::Dictionary &flags = godot::Dictionary());
	static godot::Array planned_residuals(const godot::Array &events, const godot::Ref<BeamScene> &scene);
	static godot::Array event_distance(const godot::Array &a, const godot::Array &b);
};

// Climb (events of one tier) -> Array of BeamSolverEvent.
godot::Array climb_events(const rc::Climb &c);
godot::Array doubles_to_array(const std::vector<double> &v);
