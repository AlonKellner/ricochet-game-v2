// The game-facing adapter (Stage 96): the port of
// scripts/math/precision/beam_tracer.gd. Runs the PrecisionLadder on Variant-
// free scene defs and converts its events into TracePath / TraceStep in
// float32 visual space, with one trace cache per Tracer (no statics).
#pragma once

#include "rc/arc_geom.hpp"
#include "rc/ladder.hpp"

#include <memory>
#include <string>

namespace rc {

constexpr double TRACE_TOL = 0.01; // px, PHYSICAL-PREVIEW-MATCH tolerance

struct TracePair {
	TracePath planned;
	TracePath physical;
	LadderResult result;
	Flags final_flags; // the physical trace's flags at its end (promoted after a shot)
};

class Tracer {
public:
	Tracer() = default;

	// The scene's defs: a changed set rebuilds the solve context (the caches).
	void set_defs(const std::vector<SurfaceDef> &defs) {
		if (ctx_ && defs_ == defs) {
			return;
		}
		defs_ = defs;
		ctx_ = std::make_unique<LadderContext>(defs);
		cache_valid_ = false;
	}
	const std::vector<SurfaceDef> &defs() const { return defs_; }

	// Whether planning `surface_id`'s side after `plan` is refused: inside a
	// ghost stretch of the plan only ghost togglers can be planned (GAME_SPEC
	// 10.6). The plan starts solid: ghost is per shot.
	bool plan_refuses(const std::vector<PlanItem> &plan, int surface_id, bool left, const Flags &flags) {
		if (!ctx_) {
			return false;
		}
		const Scene<Dual> &scene = ctx_->dual_scene();
		const Surf<Dual> *surf = scene.find(surface_id);
		if (surf == nullptr) {
			return false;
		}
		Flags start = flags;
		start.erase(GHOST);
		Solver<Dual> solver(scene, nullptr);
		return surf->ghosted(left, solver.plan_flags(plan, start));
	}

	// Planned + physical traces, or nullopt if the scene is not supported or the
	// input is degenerate (player = cursor, a pole / image at infinity).
	std::optional<TracePair> trace_pair(V2 player, V2 cursor, const std::vector<PlanItem> &plan, Flags flags) {
		if (!ctx_ || !scene_supported(defs_)) {
			return std::nullopt;
		}
		if (player == cursor) {
			return std::nullopt;
		}
		// ghost is per shot: every trace starts solid
		flags.erase(GHOST);
		CacheKey key{player, cursor, plan_key(plan), flags.canonical_key()};
		if (cache_valid_ && key == cache_key_) {
			return cache_pair_;
		}
		Ladder ladder(*ctx_, clock_);
		LadderResult res = ladder.solve(player.x, player.y, cursor.x, cursor.y, plan, MAX_HITS, MAX_HITS, TRACE_TOL, false, flags);
		if (res.planned.empty() || res.physical.empty() || !finite(res.planned) || !finite(res.physical)) {
			return std::nullopt;
		}
		// Identical up to and including the cursor -> one trajectory: share it
		const int ci = cursor_index(res.planned);
		bool shared = res.planned_from_physical;
		if (!shared && ci >= 0 && same_prefix(res.planned, res.physical, size_t(ci) + 1)) {
			res.planned = res.physical;
			shared = true;
		}
		TracePair pair;
		pair.final_flags = flags;
		for_each_event(res.physical, [&](const auto &e) {
			if (e.kind == EventKind::Hit) {
				pair.final_flags = e.flags;
			}
		});
		// one trajectory from one aim: every step references the same aim ray
		const Ray aim{player, player, cursor};
		std::map<std::string, int> frames;
		pair.physical = to_path(res.physical, frames, aim, flags);
		if (shared) {
			pair.planned = pair.physical;
		} else {
			pair.planned = to_path(res.planned, frames, aim, flags);
		}
		pair.result = std::move(res);
		cache_key_ = key;
		cache_pair_ = pair;
		cache_valid_ = true;
		return pair;
	}

	void clear_cache() {
		cache_valid_ = false;
		if (!defs_.empty()) {
			ctx_ = std::make_unique<LadderContext>(defs_);
		}
	}

	void set_clock(Ladder::Clock clock) { clock_ = std::move(clock); }

	// ---------------------------------------------------------------- conversion

	// Steps are in visual space; frame_id identifies the (epoch, transform stack, flags).
	TracePath to_path(const Climb &climb, std::map<std::string, int> &frames, const Ray &aim, const Flags &flags) const {
		if (climb.limbs == 0) {
			return to_path_events(climb.f64_events, climb.err, climb.limbs, frames, aim, flags);
		}
		return to_path_events(climb.big_events, climb.err, climb.limbs, frames, aim, flags);
	}

	template <Num N>
	TracePath to_path_events(const std::vector<Event<N>> &events, const std::vector<double> &errs, int limbs,
			std::map<std::string, int> &frames, const Ray &aim, const Flags &flags) const {
		TracePath path;
		std::vector<int> stack;
		const int bits = tier_bits(limbs);
		std::optional<V2> prev_pt;
		bool gap = false; // the previous event teleported (portal)
		const std::string init_flags = flags.canonical_key();
		std::string cur_flags = init_flags;
		bool ghost = flags.is_true(GHOST);
		const Scene<Dual> &scene = ctx_->dual_scene();
		for (size_t i = 0; i < events.size(); i++) {
			const Event<N> &e = events[i];
			const int fid = frame_id(stack, frames, e.epoch, cur_flags, init_flags);
			std::vector<TraceStep> steps = steps_for(e, fid, gap ? std::nullopt : prev_pt);
			bool gap_pending = gap;
			gap = false;
			if (e.point) {
				prev_pt = V2::of(e.point->fx(), e.point->fy());
			}
			const Surf<Dual> *surf = scene.find(e.surface_id);
			bool appended = false;
			for (TraceStep &s : steps) {
				// zero-length steps are not emitted; the event's effect still applies
				if (s.start == s.end && !std::isinf(s.start.x)) {
					continue;
				}
				if (gap_pending) {
					s.after_portal = true;
					gap_pending = false;
				}
				// a finite step on a curved beam is an arc step (curvature is exact)
				if (!std::isinf(s.start.x) && !std::isinf(s.end.x)) {
					s.is_arc_step = !e.beam->kappa.is_zero();
				}
				s.ray = aim;
				s.ghost = ghost;
				s.est_error = i < errs.size() ? errs[i] : -1.0;
				s.precision_bits = bits;
				path.steps.push_back(std::move(s));
				appended = true;
			}
			if (e.kind == EventKind::Hit && e.applied && !e.terminal) {
				if (e.projective) {
					stack.clear();
				} else if (e.popped) {
					stack.pop_back();
				} else {
					stack.push_back(e.surface_id);
				}
				gap = e.teleport;
			}
			if (e.kind == EventKind::Hit && appended) {
				TraceStep &last = path.steps.back();
				const Side side = e.left ? Side::Left : Side::Right;
				last.hit = TraceHit{e.t_to->to_float(), last.end, surf != nullptr ? surf->id : -1, side, e.on_segment};
				last.surface_id = e.surface_id;
				last.hit_side = int(side);
				last.hit_on_segment = e.on_segment;
				if (surf != nullptr && surf->is_target && e.on_segment
						&& std::find(path.targets_hit.begin(), path.targets_hit.end(), surf->id) == path.targets_hit.end()) {
					path.targets_hit.push_back(surf->id);
				}
			}
			if (e.kind == EventKind::Hit) {
				cur_flags = e.flags.canonical_key();
				ghost = e.flags.is_true(GHOST);
			}
			if (e.kind == EventKind::Cursor) {
				path.cursor_index = int(path.steps.size());
			}
			if (e.kind == EventKind::Max) {
				break;
			}
		}
		path.hit_count = int(events.size());
		return path;
	}

private:
	struct CacheKey {
		V2 player;
		V2 cursor;
		std::string plan;
		std::string flags;
		bool operator==(const CacheKey &o) const { return player == o.player && cursor == o.cursor && plan == o.plan && flags == o.flags; }
	};

	std::vector<SurfaceDef> defs_;
	std::unique_ptr<LadderContext> ctx_;
	Ladder::Clock clock_ = Ladder::steady_usec;
	bool cache_valid_ = false;
	CacheKey cache_key_;
	std::optional<TracePair> cache_pair_;

	static std::string plan_key(const std::vector<PlanItem> &plan) {
		std::string k;
		for (const PlanItem &p : plan) {
			k += std::to_string(p.surface_id) + (p.left ? "L" : "R");
			if (p.hint) {
				char buf[64];
				std::snprintf(buf, sizeof buf, "@%a,%a", double(p.hint->x), double(p.hint->y));
				k += buf;
			}
			k += ";";
		}
		return k;
	}

	template <class F>
	static void for_each_event(const Climb &c, F &&f) {
		if (c.limbs == 0) {
			for (const auto &e : c.f64_events) {
				f(e);
			}
		} else {
			for (const auto &e : c.big_events) {
				f(e);
			}
		}
	}

	static bool finite(const Climb &c) {
		bool ok = true;
		for_each_event(c, [&](const auto &e) {
			if (e.point) {
				const double x = e.point->fx();
				const double y = e.point->fy();
				if (std::isnan(x) || std::isnan(y) || std::isinf(x) || std::isinf(y)) {
					ok = false;
				}
			}
		});
		return ok;
	}

	static int cursor_index(const Climb &c) {
		int idx = -1;
		int i = 0;
		for_each_event(c, [&](const auto &e) {
			if (idx < 0 && e.kind == EventKind::Cursor) {
				idx = i;
			}
			i++;
		});
		return idx;
	}

	template <Num A, Num B>
	static bool same_prefix_ev(const std::vector<Event<A>> &a, const std::vector<Event<B>> &b, size_t n) {
		if (a.size() < n || b.size() < n) {
			return false;
		}
		for (size_t i = 0; i < n; i++) {
			if (a[i].kind != b[i].kind || a[i].surface_id != b[i].surface_id || a[i].left != b[i].left || a[i].applied != b[i].applied
					|| a[i].state != b[i].state) {
				return false;
			}
		}
		return true;
	}
	static bool same_prefix(const Climb &a, const Climb &b, size_t n) {
		if (a.limbs == 0 && b.limbs == 0) {
			return same_prefix_ev(a.f64_events, b.f64_events, n);
		}
		if (a.limbs == 0) {
			return same_prefix_ev(a.f64_events, b.big_events, n);
		}
		if (b.limbs == 0) {
			return same_prefix_ev(a.big_events, b.f64_events, n);
		}
		return same_prefix_ev(a.big_events, b.big_events, n);
	}

	// frame id per (epoch, transform stack, flags): a projective starts a new
	// epoch, and a state change a new frame
	static int frame_id(const std::vector<int> &stack, std::map<std::string, int> &frames, int epoch, const std::string &flags_key,
			const std::string &init_key) {
		if (stack.empty() && epoch == 0 && flags_key == init_key) {
			return IDENTITY_FRAME;
		}
		std::string key = std::to_string(epoch) + "|";
		for (size_t i = 0; i < stack.size(); i++) {
			if (i) {
				key += ",";
			}
			key += std::to_string(stack[i]);
		}
		key += "|" + flags_key;
		auto it = frames.find(key);
		if (it == frames.end()) {
			it = frames.emplace(key, int(frames.size()) + 1).first;
		}
		return it->second;
	}

	// Direction.from_coords(origin, origin + tangent * 100)
	static Ray ray_of(const V2 &origin, const V2 &tg) {
		return Ray{origin, origin, origin + tg * 100.0f};
	}

	static V2 at_view_v2(const Beam<F64> &bf, double t) {
		double x, y;
		fast::at_view(bf.p.re.v, bf.p.im.v, bf.tau.re.v, bf.tau.im.v, bf.kappa.v, t, x, y);
		return V2::of(x, y);
	}

	// start -> infinity along the start tangent, infinity -> end along the end
	// tangent (escape-step convention: `via` holds the direction).
	static std::vector<TraceStep> through_infinity(const Beam<F64> &bf, double tf, double tt, V2 start, V2 end, int fid) {
		const CN<F64> tg0 = bf.tangent_at(F64{tf});
		TraceStep out_step = TraceStep::make(start, V2::inf(), fid, std::nullopt, ray_of(start, V2::of(tg0.re.v, tg0.im.v)), V2::of(tg0.re.v, tg0.im.v), false);
		const CN<F64> tg1 = bf.tangent_at(F64{tt});
		TraceStep in_step = TraceStep::make(V2::inf(), end, fid, std::nullopt, ray_of(end, V2::of(tg1.re.v, tg1.im.v)), -V2::of(tg1.re.v, tg1.im.v), false);
		return {out_step, in_step};
	}

	// One event = the arc from its t_from to t_to on its beam. Arcs that wrap
	// through t = infinity on a straight beam are split at infinity.
	template <Num N>
	static std::vector<TraceStep> steps_for(const Event<N> &e, int fid, const std::optional<V2> &prev_pt) {
		const Beam<N> &b = *e.beam;
		const Beam<F64> bf{{F64{b.p.fx()}, F64{b.p.fy()}}, {F64{b.tau.fx()}, F64{b.tau.fy()}}, F64{b.kappa.to_float()}};
		const double tf = e.t_from->to_float();
		const V2 start = prev_pt ? *prev_pt : at_view_v2(bf, tf);
		const bool is_line = bf.kappa.v == 0.0;
		if (!e.t_to) {
			// escape to infinity along a straight beam
			const CN<F64> tg = bf.tangent_at(F64{tf});
			return {TraceStep::make(start, V2::inf(), fid, std::nullopt, ray_of(start, V2::of(tg.re.v, tg.im.v)), V2::of(tg.re.v, tg.im.v), false)};
		}
		const double tt = e.t_to->to_float();
		const V2 end = V2::of(e.point->fx(), e.point->fy());
		V2 via;
		if (!is_line && (std::isinf(tt) || std::isinf(tf))) {
			// an end at the antipode (t = inf): any parameter strictly between the ends is on the arc
			const double span = 4.0 / std::fabs(bf.kappa.v);
			const double tm = std::isinf(tt) ? (gd_maxf(tf, 0.0) + span) : (gd_minf(tt, 0.0) - span);
			via = at_view_v2(bf, tm);
		} else if (tt > tf || tt == tf) {
			via = at_view_v2(bf, (tf + tt) * 0.5);
		} else if (!is_line) {
			// wraps through t = infinity: the arc passes the antipode of p
			const CN<N> inf_pt = *b.at_infinity();
			via = V2::of(inf_pt.fx(), inf_pt.fy());
		}
		if (!is_line) {
			// an arc the renderer cannot draw as an arc is drawn as the chord; when
			// the path goes the long way round, draw it like a straight beam
			// wrapping through infinity instead
			if (arc::is_arc(start, via, end) || (start - via).dot(end - via) <= 0.0f) {
				return {TraceStep::make(start, end, fid, std::nullopt, std::nullopt, via, true)};
			}
			return through_infinity(bf, tf, tt, start, end, fid);
		}
		if (tt > tf || tt == tf) {
			return {TraceStep::make(start, end, fid, std::nullopt, std::nullopt, via, false)};
		}
		return through_infinity(bf, tf, tt, start, end, fid);
	}
};

} // namespace rc
