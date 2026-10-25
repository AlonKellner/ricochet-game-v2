// The game-facing adapter (Stage 96): the port of
// scripts/math/precision/beam_tracer.gd. Runs the PrecisionLadder on Variant-
// free scene defs and converts its events into TracePath / TraceStep in
// float32 visual space, with one trace cache per Tracer (no statics).
#pragma once

#include "rc/arc_geom.hpp"
#include "rc/ladder.hpp"

#include <algorithm>
#include <bit>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>

namespace rc {

constexpr double TRACE_TOL = 0.01; // px, PHYSICAL-PREVIEW-MATCH tolerance

struct TracePair {
	TracePath planned;
	TracePath physical;
	LadderResult result;
	Flags final_flags; // the physical trace's flags at its end (promoted after a shot)
	bool shared = false; // one trajectory: planned is the physical path (the GDScript shares the step objects)
	// B4 (Stage 110), Stage 134: both walks reach the cursor with the same
	// significant events before it and leave its point in one frame (V1-V3)
	bool cursor_point_agrees = true;
};

// One projective firing's mark in an epoch key: the surface and the side
// (K14, Stage 113: two walks firing different projectives at one point are in
// different frames; a count of firings would not tell them apart).
inline std::string firing_key(int sid, bool left, bool first) {
	return (first ? "" : ",") + std::to_string(sid) + (left ? "L" : "R");
}

namespace vis {

// (moved from rc/visibility.hpp: the tracer compares the walks as the region does)
struct Pt {
	double x = 0.0;
	double y = 0.0;
	bool operator==(const Pt &) const = default;
	bool operator<(const Pt &o) const { return x < o.x || (x == o.x && y < o.y); }
	Pt operator+(const Pt &o) const { return {x + o.x, y + o.y}; }
	Pt operator-(const Pt &o) const { return {x - o.x, y - o.y}; }
	Pt operator*(double s) const { return {x * s, y * s}; }
	bool finite() const { return std::isfinite(x) && std::isfinite(y); }
};

struct CastEvent {
	EventKind kind = EventKind::Hit;
	int sid = -1;
	bool left = false;
	bool applied = false;
	bool rehit = false; // the departure point, re-hit after the full loop
	bool terminal = false;
	std::string flags;
	std::string frame;  // the frame after the event: epoch, transform stack, flags, off the plan (Tracer::frame_id)
	bool off_plan = false; // Stage 129: the walk has left the plan by this event
	bool has_pt = false;
	Pt pt;
};

template <Num N>
std::vector<CastEvent> cast_events(const std::vector<Event<N>> &events, const std::string &init_flags) {
	std::vector<CastEvent> out;
	out.reserve(events.size());
	std::vector<int> stack;
	std::string epoch_key; // K14: the projective firings so far
	std::string flags_now = init_flags;
	for (const Event<N> &e : events) {
		CastEvent c;
		c.kind = e.kind;
		c.sid = e.surface_id;
		c.left = e.left;
		c.applied = e.applied;
		c.rehit = e.rehit;
		c.terminal = e.terminal;
		c.off_plan = e.off_plan;
		c.flags = e.flags.canonical_key();
		// the frame as Tracer::to_path_events keeps it
		if (e.kind == EventKind::Hit && e.applied && !e.terminal) {
			if (e.projective) {
				stack.clear();
				epoch_key += firing_key(e.surface_id, e.left, epoch_key.empty());
			} else if (e.popped) {
				if (!stack.empty()) stack.pop_back();
			} else {
				stack.push_back(e.surface_id);
			}
		}
		if (e.kind == EventKind::Hit) {
			flags_now = c.flags;
		}
		// (the projective's own event leaves in the frame its firing opens, as event_frames reads it)
		c.frame = epoch_key + "|";
		for (int sid : stack) {
			c.frame += std::to_string(sid) + ",";
		}
		c.frame += "|" + flags_now + (e.off_plan ? "|off" : "");
		std::optional<CN<N>> p = e.point;
		if (!p && e.beam && e.kind != EventKind::Origin) {
			p = e.t_to ? std::optional<CN<N>>(e.beam->at(*e.t_to)) : e.beam->at_infinity();
		}
		if (p) {
			c.has_pt = true;
			c.pt = Pt{p->fx(), p->fy()};
		}
		out.push_back(std::move(c));
	}
	return out;
}

inline std::vector<CastEvent> cast_events(const Climb &c, const std::string &init_flags) {
	return c.limbs == 0 ? cast_events(c.f64_events, init_flags) : cast_events(c.big_events, init_flags);
}

// Decisions equal at an event: both walks leave it in the same frame.
inline bool same_decision(const CastEvent &a, const CastEvent &b) {
	return a.kind == b.kind && a.sid == b.sid && (a.kind != EventKind::Hit || a.left == b.left) && a.applied == b.applied
		&& (a.kind != EventKind::Hit || a.flags == b.flags) && a.off_plan == b.off_plan;
}


// Two events at one drawn point (float32, as drawn), or both without one
inline bool same_drawn(const CastEvent &a, const CastEvent &b) {
	return a.has_pt == b.has_pt && (!a.has_pt || (float(a.pt.x) == float(b.pt.x) && float(a.pt.y) == float(b.pt.y)));
}

// Stage 134 (GAME_SPEC 12.6, 14.5): the significant points of a walk -- the runs
// of events at one drawn point holding a significant event (an applied effect, a
// firing state change, a passage of infinity or of the ray's origin), each by
// the index of its last event (the frame the run leaves in). The walks compute
// different candidates, so they are compared by these, not event by event; by
// point, as surfaces crossed at one point in another order leave it in one
// frame (B5; visibility.py _significant)
inline std::vector<int> significant_events(const std::vector<CastEvent> &ev, const std::string &flags0) {
	std::vector<int> out;
	out.reserve(ev.size());
	const std::string *prev = &flags0;
	bool run_sig = false;
	for (size_t i = 0; i < ev.size(); i++) {
		const CastEvent &e = ev[i];
		bool sig = e.kind == EventKind::Infinity || e.kind == EventKind::Origin;
		if (e.kind == EventKind::Hit) {
			sig = e.applied || e.flags != *prev;
			prev = &e.flags;
		}
		run_sig = run_sig || sig;
		if (i + 1 == ev.size() || !e.has_pt || !same_drawn(ev[i + 1], e)) {
			if (run_sig) {
				out.push_back(int(i));
			}
			run_sig = false;
		}
	}
	return out;
}

// Two significant events alike: the drawn point and the frame after
inline bool same_significant(const CastEvent &a, const CastEvent &b) {
	return same_drawn(a, b) && a.frame == b.frame;
}

// The frame after the run of events at event i's drawn point (visibility.py _leave)
inline const std::string &leave_frame(const std::vector<CastEvent> &ev, size_t i) {
	size_t j = i;
	while (ev[i].has_pt && j + 1 < ev.size() && same_drawn(ev[j + 1], ev[i])) {
		j++;
	}
	return ev[j].frame;
}

inline int cursor_event(const std::vector<CastEvent> &ev) {
	for (size_t i = 0; i < ev.size(); i++) {
		if (ev[i].kind == EventKind::Cursor) {
			return int(i);
		}
	}
	return -1;
}

// The first n significant events of both walks alike
inline bool significant_alike(const std::vector<CastEvent> &pl, const std::vector<int> &spl, const std::vector<CastEvent> &ph,
		const std::vector<int> &sph, size_t n) {
	if (n > spl.size() || n > sph.size()) {
		return false;
	}
	for (size_t k = 0; k < n; k++) {
		if (!same_significant(pl[size_t(spl[k])], ph[size_t(sph[k])])) {
			return false;
		}
	}
	return true;
}

// The significant events before index i
inline size_t significant_before(const std::vector<int> &sig, int i) {
	return size_t(std::lower_bound(sig.begin(), sig.end(), i) - sig.begin());
}

// Stage 134 (GAME_SPEC 15.1, visibility.py visible): both walks reach the
// cursor (V1) with the same significant events before it (V2), and leave its
// point in one frame (V3)
inline bool walks_agree_at_cursor(const std::vector<CastEvent> &pl, const std::vector<CastEvent> &ph, const std::string &flags0) {
	const int cpl = cursor_event(pl);
	int cph = cursor_event(ph);
	if (cpl >= 0 && cph < 0 && !ph.empty() && ph.back().kind == EventKind::Hit && ph.back().terminal && same_drawn(ph.back(), pl[size_t(cpl)])) {
		cph = int(ph.size()) - 1; // (a physical walk that ends at the cursor's point -- a terminal there -- reaches it)
	}
	if (cpl < 0 || cph < 0) {
		return false;
	}
	const std::vector<int> spl = significant_events(pl, flags0);
	const std::vector<int> sph = significant_events(ph, flags0);
	const size_t n = significant_before(spl, cpl);
	return n == significant_before(sph, cph) && significant_alike(pl, spl, ph, sph, n)
		&& leave_frame(pl, size_t(cpl)) == leave_frame(ph, size_t(cph));
}

} // namespace vis

class Tracer {
public:
	Tracer() = default;

	// The scene's defs: a changed set rebuilds the solve context (the caches).
	void set_defs(const std::vector<SurfaceDef> &defs) {
		if (ctx_ && defs_ == defs) {
			return;
		}
		tie_memo_.clear();
		realization_memo_.clear();
		defs_ = defs;
		supported_ = scene_supported(defs_); // (B.4: asked per decision; a function of the defs)
		ctx_ = std::make_unique<LadderContext>(defs);
		cache_valid_ = false;
	}
	const std::vector<SurfaceDef> &defs() const { return defs_; }


	// Whether planning `surface_id`'s side after `plan` is refused: inside a
	// ghost stretch of the plan only ghost togglers can be planned (GAME_SPEC
	// 10.6), and an absent surface cannot be planned (9.6). The plan starts
	// solid: ghost is per shot.
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
		Solver<Dual> solver(scene, &ctx_->dual_cache());
		const Flags after = solver.plan_flags(plan, start);
		return !surf->present(after) || surf->ghosted(left, after);
	}

	// Stage 158 (GAME_SPEC 10.6, the user): frost planned and active -- whether frost turns the side after the
	// plan's entries (simulated from `flags`): a stopping side then plans as a reflection (a line) or inversion (a
	// circle); and per entry, whether frost turned it as planned.
	bool plan_frost_turns(const std::vector<PlanItem> &plan, int surface_id, bool left, const Flags &flags) {
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
		Solver<Dual> solver(scene, &ctx_->dual_cache());
		const Flags after = solver.plan_flags(plan, start);
		return surf->present(after) && !surf->ghosted(left, after) && surf->frosted(left, after);
	}
	std::vector<bool> plan_frosted(const std::vector<PlanItem> &plan, const Flags &flags) {
		std::vector<bool> out(plan.size(), false);
		if (!ctx_) {
			return out;
		}
		const Scene<Dual> &scene = ctx_->dual_scene();
		Flags f = flags;
		f.erase(GHOST);
		for (size_t i = 0; i < plan.size(); i++) {
			const Surf<Dual> *surf = scene.find(plan[i].surface_id);
			if (surf == nullptr || !surf->present(f)) {
				continue;
			}
			out[i] = !surf->ghosted(plan[i].left, f) && surf->frosted(plan[i].left, f);
			f = f.applied(surf->state_changes(plan[i].left, f));
		}
		return out;
	}

	// Planned + physical traces, or nullopt if the scene is not supported or the
	// input is degenerate (player = cursor, a pole / image at infinity).
	std::optional<TracePair> trace_pair(V2 player, V2 cursor, const std::vector<PlanItem> &plan, Flags flags) {
		return trace_pair_opt(player, cursor, plan, std::move(flags), nullptr);
	}

	// Stage 121: the float64 tier only -- *exact tells whether the result is the
	// exact one (the ladder accepted float64) or provisional (it would climb).
	std::optional<TracePair> trace_pair_provisional(V2 player, V2 cursor, const std::vector<PlanItem> &plan, Flags flags, bool *exact) {
		return trace_pair_opt(player, cursor, plan, std::move(flags), exact);
	}

private:
	std::optional<TracePair> trace_pair_opt(V2 player, V2 cursor, const std::vector<PlanItem> &plan, Flags flags, bool *exact) {
		if (exact != nullptr) {
			*exact = true;
		}
		if (!ctx_ || !supported_) {
			return std::nullopt;
		}
		if (player == cursor) {
			return std::nullopt;
		}
		// ghost is per shot: every trace starts solid
		flags.erase(GHOST);
		CacheKey key;
		if (pair_cache_ && exact == nullptr) {
			key = CacheKey{player, cursor, plan_key(plan), flags.canonical_key()};
			if (cache_valid_ && key == cache_key_) {
				return cache_pair_;
			}
		}
		Ladder ladder(*ctx_, clock_);
		ladder.provisional = exact != nullptr;
		LadderResult res = ladder.solve(player.x, player.y, cursor.x, cursor.y, plan, MAX_HITS, MAX_HITS, TRACE_TOL, false, flags);
		if (exact != nullptr) {
			*exact = !ladder.needed_climb;
		}
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
		pair.shared = shared;
		// Stage 134: the walks compared by their significant events (V1-V3)
		const std::string flags0 = flags.canonical_key();
		pair.cursor_point_agrees = shared || vis::walks_agree_at_cursor(vis::cast_events(res.planned, flags0), vis::cast_events(res.physical, flags0), flags0);
		pair.result = std::move(res);
		if (pair_cache_ && exact == nullptr) {
			cache_key_ = key;
			cache_pair_ = pair;
			cache_valid_ = true;
		}
		return pair;
	}

public:
	// Stage 110: one cast of the visibility region -- the ladder's planned and
	// physical events for a float64 player and cursor, as trace_pair solves them
	// (ghost erased, the same tolerance), without the display conversion or the
	// cache; nullopt where trace_pair returns none. Stage 123: stops, what the
	// cast reads (Ladder::solve's horizon); known, Solver::aim_known; reads_cursor
	// (Stage 127), whether it reads the planned walk's cursor.
	std::optional<LadderResult> solve(double px, double py, double cx, double cy, const std::vector<PlanItem> &plan, Flags flags,
			std::optional<bool> force_reversed = std::nullopt, int stops = -1, const std::optional<AimKnown> &known = std::nullopt,
			bool reads_cursor = true) {
		if (!ctx_ || !supported_ || (px == cx && py == cy)) {
			return std::nullopt;
		}
		flags.erase(GHOST);
		Ladder ladder(*ctx_, clock_);
		LadderResult res = ladder.solve(px, py, cx, cy, plan, MAX_HITS, MAX_HITS, TRACE_TOL, false, flags, force_reversed, stops, known, reads_cursor);
		if (res.planned.empty() || res.physical.empty() || !finite(res.planned) || !finite(res.physical)) {
			return std::nullopt;
		}
		return res;
	}

	// Stage 111: the realization the plan's first entry picks for a cursor when
	// the walk does not decide (Solver::tie_choice): reversed or not; nullopt
	// where no solve is possible.
	std::optional<bool> tie_choice(double px, double py, double cx, double cy, const std::vector<PlanItem> &plan, Flags flags) {
		if (!ctx_ || !supported_ || (px == cx && py == cy)) {
			return std::nullopt;
		}
		flags.erase(GHOST);
		const DecisionKey key = decision_key(px, py, cx, cy, plan, flags);
		if (const auto it = tie_memo_.find(key); it != tie_memo_.end()) {
			return it->second;
		}
		Solver<Dual> solver(ctx_->dual_scene(), &ctx_->dual_cache()); // (the cast-cost program, A.1: the exact memos, pure, shared with the solves)
		const std::optional<bool> q = solver.tie_choice(CN<Dual>{Dual{px, 0.0}, Dual{py, 0.0}}, CN<Dual>{Dual{cx, 0.0}, Dual{cy, 0.0}}, plan, flags).reversed;
		remember(tie_memo_, key, q);
		return q;
	}

	// Stage 111: the plan's realization for a cursor (the solver's own choice):
	// reversed or not; nullopt where no solve is possible.
	std::optional<bool> realization_reversed(double px, double py, double cx, double cy, const std::vector<PlanItem> &plan, Flags flags) {
		if (!ctx_ || !supported_ || (px == cx && py == cy)) {
			return std::nullopt;
		}
		flags.erase(GHOST);
		const DecisionKey key = decision_key(px, py, cx, cy, plan, flags);
		if (const auto it = realization_memo_.find(key); it != realization_memo_.end()) {
			return it->second;
		}
		Solver<Dual> solver(ctx_->dual_scene(), &ctx_->dual_cache()); // (the cast-cost program, A.1: the exact memos, pure, shared with the solves)
		const std::optional<bool> q = solver.realization(CN<Dual>{Dual{px, 0.0}, Dual{py, 0.0}}, CN<Dual>{Dual{cx, 0.0}, Dual{cy, 0.0}}, plan, flags).reversed;
		remember(realization_memo_, key, q);
		return q;
	}

	// Workflow W2.0: the two decisions are pure functions of (player, cursor,
	// plan, flags) on this tracer's defs; memoized per tracer (cleared with the
	// defs, bounded), so a region's determinism rebuild and its per-cell
	// prefix builders do not walk them again.
	// B.4: the decisions' memo key as the inputs' bits (it was a string of %a's and the
	// plan's spelling, printed per decision: equal keys are the same equal inputs)
	struct DecisionKey {
		uint64_t px = 0, py = 0, cx = 0, cy = 0;
		std::vector<uint64_t> plan; // per item: id | left << 32 | hint << 33, then the hint's bits, then the span's six
		std::string flags;          // canonical
		bool operator==(const DecisionKey &) const = default;
	};
	struct DecisionHash {
		size_t operator()(const DecisionKey &k) const {
			uint64_t h = 14695981039346656037ULL;
			for (uint64_t w : {k.px, k.py, k.cx, k.cy}) {
				h = (h ^ w) * 1099511628211ULL;
			}
			for (uint64_t w : k.plan) {
				h = (h ^ w) * 1099511628211ULL;
			}
			return size_t(h ^ std::hash<std::string>{}(k.flags));
		}
	};
	static DecisionKey decision_key(double px, double py, double cx, double cy, const std::vector<PlanItem> &plan, const Flags &flags) {
		DecisionKey k;
		k.px = std::bit_cast<uint64_t>(px);
		k.py = std::bit_cast<uint64_t>(py);
		k.cx = std::bit_cast<uint64_t>(cx);
		k.cy = std::bit_cast<uint64_t>(cy);
		for (const PlanItem &p : plan) {
			k.plan.push_back(uint64_t(uint32_t(p.surface_id)) | uint64_t(p.left) << 32 | uint64_t(p.hint.has_value()) << 33 | uint64_t(p.span.has_value()) << 34);
			if (p.hint) {
				k.plan.push_back(std::bit_cast<uint64_t>(double(p.hint->x)));
				k.plan.push_back(std::bit_cast<uint64_t>(double(p.hint->y)));
			}
			if (p.span) {
				for (const double v : *p.span) {
					k.plan.push_back(std::bit_cast<uint64_t>(v));
				}
			}
		}
		k.flags = flags.canonical_key();
		return k;
	}
	using DecisionMemo = std::unordered_map<DecisionKey, std::optional<bool>, DecisionHash>;
	static void remember(DecisionMemo &memo, const DecisionKey &key, std::optional<bool> v) {
		if (memo.size() >= DECISION_MEMO_MAX) {
			memo.clear();
		}
		memo.emplace(key, v);
	}
	static constexpr size_t DECISION_MEMO_MAX = 1 << 16;
	DecisionMemo tie_memo_;
	DecisionMemo realization_memo_;
	bool supported_ = false; // scene_supported(defs_)

	// Stage 110: a cast's two paths, as trace_pair converts them (one frame-id
	// map, the physical walk first), without the shared-trajectory shortcut.
	std::pair<TracePath, TracePath> cast_paths(const LadderResult &res, double px, double py, double cx, double cy, Flags flags) const {
		flags.erase(GHOST);
		const V2 player = V2::of(px, py);
		const Ray aim{player, player, V2::of(cx, cy)};
		std::map<std::string, int> frames;
		TracePath physical = to_path(res.physical, frames, aim, flags, true);
		TracePath planned = to_path(res.planned, frames, aim, flags, true);
		return {std::move(planned), std::move(physical)};
	}

	// Stage 101: where the next infinity entry of `plan` would be passed: the
	// frame's image of infinity after the plan (nullopt = infinity: the edge).
	std::optional<V2> infinity_point(const std::vector<PlanItem> &plan, const Flags &flags) {
		if (!ctx_) {
			return std::nullopt;
		}
		Solver<Dual> solver(ctx_->dual_scene(), &ctx_->dual_cache()); // (the cast-cost program, A.1: the exact memos, pure, shared with the solves)
		const std::optional<CN<Dual>> q = solver.plan_pole(plan, flags);
		if (!q) {
			return std::nullopt;
		}
		return V2::of(q->fx(), q->fy());
	}

	// Stage 132: the ray's origin after the plan (GAME_SPEC 4.2) and whether the plan
	// leaves the walk behind it (the origin entry plannable); the cursor gives a line
	// projection's departure.
	struct OriginPoint {
		std::optional<V2> point;
		bool behind = false;
	};
	OriginPoint origin_point(V2 player, std::optional<V2> cursor, const std::vector<PlanItem> &plan, const Flags &flags) {
		if (!ctx_) {
			return {};
		}
		Solver<Dual> solver(ctx_->dual_scene(), &ctx_->dual_cache()); // (the cast-cost program, A.1: the exact memos, pure, shared with the solves)
		auto cn = [](V2 p) { return CN<Dual>{Dual{double(p.x), 0.0}, Dual{double(p.y), 0.0}}; };
		const auto o = solver.plan_origin(cn(player), cursor ? std::optional<CN<Dual>>(cn(*cursor)) : std::nullopt, plan, flags);
		OriginPoint out;
		out.behind = o.behind;
		if (o.point) {
			out.point = V2::of(o.point->fx(), o.point->fy());
		}
		return out;
	}

	void clear_cache() {
		cache_valid_ = false;
		if (!defs_.empty()) {
			ctx_ = std::make_unique<LadderContext>(defs_);
		}
	}

	void set_clock(Ladder::Clock clock) { clock_ = std::move(clock); }

	// Stage 120: trace_pair keeps its last result for an identical call (on by
	// default); a caller that never repeats a call (it caches by its own inputs)
	// turns it off and saves the copy.
	void set_pair_cache(bool on) {
		pair_cache_ = on;
		cache_valid_ = false;
	}

	// The ladder's counters since the context was built or reset (benches),
	// the helpers' included.
	LadderStats ladder_stats() const {
		LadderStats s = ctx_ ? ctx_->stats() : LadderStats{};
		for (const auto &h : helpers_) {
			const LadderStats x = h->ladder_stats();
			s.walks += x.walks;
			s.f64 += x.f64;
			s.unreliable += x.unreliable;
			s.over_tol += x.over_tol;
			s.verify += x.verify;
			for (size_t q = 0; q < 6; q++) {
				s.tier_solves[q] += x.tier_solves[q];
			}
			s.big_usec += x.big_usec;
			for (size_t q = 0; q < 32; q++) {
				s.why[q] += x.why[q];
			}
			for (size_t q = 0; q < 8; q++) {
				s.why_kind[q] += x.why_kind[q];
			}
			s.why_zero += x.why_zero;
			s.why_unbounded += x.why_unbounded;
			s.why_empty += x.why_empty;
			s.why_beyond += x.why_beyond;
			s.unconverged += x.unconverged;
		}
		return s;
	}
	void reset_ladder_stats() {
		if (ctx_) {
			ctx_->stats() = LadderStats{};
		}
		for (auto &h : helpers_) {
			h->reset_ladder_stats();
		}
	}

	// The i-th helper tracer (Perf P3: the visibility region's parallel casts):
	// the same defs and switches, caches of its own, kept across calls. Not
	// thread-safe itself: take the helpers before starting the threads.
	Tracer &helper(size_t i) {
		while (helpers_.size() <= i) {
			helpers_.push_back(std::make_unique<Tracer>());
		}
		Tracer &h = *helpers_[i];
		h.set_defs(defs_);
		return h;
	}

	// ---------------------------------------------------------------- conversion

	// Steps are in visual space; frame_id identifies the (epoch, transform stack, flags).
	// lean (Stage 124, a region's cast): the steps, their frames and the events'
	// steps and frames only (no hit records, targets or display fields)
	TracePath to_path(const Climb &climb, std::map<std::string, int> &frames, const Ray &aim, const Flags &flags, bool lean = false) const {
		if (climb.limbs == 0) {
			return to_path_events(climb.f64_events, climb.err, climb.limbs, frames, aim, flags, lean);
		}
		return to_path_events(climb.big_events, climb.err, climb.limbs, frames, aim, flags, lean);
	}

	template <Num N>
	TracePath to_path_events(const std::vector<Event<N>> &events, const std::vector<double> &errs, int limbs,
			std::map<std::string, int> &frames, const Ray &aim, const Flags &flags, bool lean = false) const {
		TracePath path;
		std::vector<int> stack;
		const int bits = tier_bits(limbs);
		std::optional<V2> prev_pt;
		bool gap = false; // the previous event teleported (portal)
		bool after_inf = false; // Stage 131: the previous drawn event passed infinity
		const std::string init_flags = flags.canonical_key();
		std::string cur_flags = init_flags;
		path.frame_keys.push_back(frame_key({}, "", init_flags));
		// (Stage 128: the frame -- its key, its id -- changes only at an applied
		// hit or a flags change; rebuilt then, not per event)
		bool fid_stale = false, key_stale = false;
		bool off_plan = false; // Stage 129: the walk has left the plan (a part of its frame)
		int fid = IDENTITY_FRAME;
		path.steps.reserve(events.size() + 1);
		path.event_steps.reserve(events.size());
		path.event_frames.reserve(events.size());
		std::string epoch_key; // the projective firings so far (K14: an epoch is its firing, not a count)
		bool ghost = flags.is_true(GHOST);
		Flags before = flags; // the flags before the next hit (Stage 107)
		const Flags *keyed = &flags; // (Stage 124: cur_flags is the key of these flags)
		const Scene<Dual> &scene = ctx_->dual_scene();
		for (size_t i = 0; i < events.size(); i++) {
			const Event<N> &e = events[i];
			if (fid_stale) {
				fid = frame_id(stack, frames, epoch_key, cur_flags, init_flags, off_plan);
				fid_stale = false;
			}
			Steps steps = steps_for(e, fid, gap ? std::nullopt : prev_pt);
			bool gap_pending = gap;
			bool inf_pending = after_inf; // (Stage 131: the step after a passage of infinity)
			after_inf = false;
			gap = false;
			if (e.point) {
				prev_pt = V2::of(e.point->fx(), e.point->fy());
			} else if (e.kind == EventKind::Infinity) {
				prev_pt = std::nullopt; // the next arc starts at infinity
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
				if (inf_pending) {
					s.after_infinity = true;
					inf_pending = false;
				}
				// a finite step on a curved beam is an arc step (curvature is exact)
				if (!std::isinf(s.start.x) && !std::isinf(s.end.x)) {
					s.is_arc_step = !e.beam->kappa.is_zero();
				}
				if (!lean) {
					s.ray = aim;
					s.ghost = ghost;
					s.est_error = i < errs.size() ? errs[i] : -1.0;
					s.precision_bits = bits;
				}
				path.steps.push_back(std::move(s));
				appended = true;
			}
			// K5: a teleport's mark waits for the next drawn step (an event at the
			// departure point, e.g. a carrier crossing there, draws nothing)
			gap = gap_pending;
			after_inf = inf_pending || (e.kind == EventKind::Infinity && appended);
			if (e.kind == EventKind::Hit && e.applied && !e.terminal) {
				if (e.projective) {
					stack.clear();
					epoch_key += firing_key(e.surface_id, e.left, epoch_key.empty());
				} else if (e.popped) {
					stack.pop_back();
				} else {
					stack.push_back(e.surface_id);
				}
				gap = gap || e.teleport;
				fid_stale = key_stale = true;
			}
			if (e.kind == EventKind::Hit && appended && !lean) {
				TraceStep &last = path.steps.back();
				const Side side = e.left ? Side::Left : Side::Right;
				last.hit = TraceHit{e.t_to->to_float(), last.end, surf != nullptr ? surf->id : -1, side, e.on_segment};
				last.significant = e.applied || e.state; // (Stage 131: an applied effect or a firing state change)
				last.teleports = e.applied && e.teleport;
				last.surface_id = e.surface_id;
				last.hit_side = int(side);
				last.hit_on_segment = e.on_segment;
				if (surf != nullptr && surf->is_target && e.on_segment
						&& std::find(path.targets_hit.begin(), path.targets_hit.end(), surf->id) == path.targets_hit.end()) {
					path.targets_hit.push_back(surf->id);
				}
			}
			if (e.kind == EventKind::Hit && !appended && !lean && (e.applied || e.state) && e.point && !path.steps.empty()
					&& path.steps.back().end == V2::of(e.point->fx(), e.point->fy())) {
				// Stage 134: a significant event at the end of the step drawn last (one
				// at the same point draws nothing): the step ends at a significant point
				path.steps.back().significant = true;
			}
			if (e.kind == EventKind::Hit && !lean) {
				HitRecord h;
				h.surface_id = e.surface_id;
				h.side = e.left ? Side::Left : Side::Right;
				h.on_segment = e.on_segment;
				h.fired = e.fired;
				h.state = e.state;
				if (e.point) {
					h.point = V2::of(e.point->fx(), e.point->fy());
				}
				if (e.t_to) {
					const auto tg = e.beam->tangent_at(*e.t_to);
					const double tx = tg.re.to_float();
					const double ty = tg.im.to_float();
					const double len = std::sqrt(tx * tx + ty * ty);
					if (len > 0.0) {
						h.tangent = V2::of(tx / len, ty / len);
					}
				}
				h.flags_before = before;
				h.step_index = int(path.steps.size()) - 1;
				path.hits.push_back(std::move(h));
				before = e.flags;
			}
			if (e.kind == EventKind::Origin && appended && !lean) {
				TraceStep &last = path.steps.back();
				last.significant = true; // (Stage 131: the passage of the ray's origin)
				if (e.applied) {
					// Stage 132: a consumed origin entry carries its id, like infinity's
					last.surface_id = ORIGIN_ID;
					last.hit_on_segment = true;
				}
			}
			if (e.kind == EventKind::Infinity && appended && !lean) {
				// the outgoing half of the wrap carries the infinity id: plan
				// highlighting and PLAN_EFFECTS_APPLIED find the entry there
				TraceStep &last = path.steps.back();
				last.surface_id = INFINITY_ID;
				last.hit_on_segment = true;
				last.significant = true; // (Stage 131: a passage of infinity)
				last.to_infinity = true;
			}
			if (e.kind == EventKind::Hit) {
				if (!(e.flags == *keyed)) {
					cur_flags = e.flags.canonical_key(); // (a pure function of the flags: rebuilt when they change)
					keyed = &e.flags;
					fid_stale = key_stale = true;
				}
				ghost = e.flags.is_true(GHOST);
			}
			if (e.off_plan != off_plan) {
				off_plan = e.off_plan; // (Stage 129: the walk left the plan at this event)
				fid_stale = key_stale = true;
			}
			if (e.kind == EventKind::Cursor) {
				path.cursor_index = int(path.steps.size());
			}
			path.event_steps.push_back(int(path.steps.size()));
			if (key_stale) {
				path.frame_keys.push_back(frame_key(stack, epoch_key, cur_flags, off_plan));
				key_stale = false;
			}
			path.event_frames.push_back(int(path.frame_keys.size()) - 1); // (the frame the event leaves in: its firing counted)
			if (e.kind == EventKind::Max) {
				path.truncated = true;
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
	std::vector<std::unique_ptr<Tracer>> helpers_;
	Ladder::Clock clock_ = Ladder::steady_usec;
	bool cache_valid_ = false;
	bool pair_cache_ = true;
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
			if (p.span) {
				// Stage 108: a piece's span changes the trace (the anchor chain does not)
				for (const double v : *p.span) {
					char buf[32];
					std::snprintf(buf, sizeof buf, "|%a", v);
					k += buf;
				}
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

	// The frame's key: epoch (its firings) | transform stack | flags, and
	// "|off" once the walk has left the plan (Stage 129, GAME_SPEC 14.2;
	// frame_id interns it).
	static std::string frame_key(const std::vector<int> &stack, const std::string &epoch, const std::string &flags_key, bool off_plan = false) {
		std::string key = epoch + "|";
		for (size_t i = 0; i < stack.size(); i++) {
			if (i) {
				key += ",";
			}
			key += std::to_string(stack[i]);
		}
		return key + "|" + flags_key + (off_plan ? "|off" : "");
	}

	// frame id per (epoch, transform stack, flags, off the plan): a projective
	// starts a new epoch, and a state change a new frame
	static int frame_id(const std::vector<int> &stack, std::map<std::string, int> &frames, const std::string &epoch, const std::string &flags_key,
			const std::string &init_key, bool off_plan) {
		if (stack.empty() && epoch.empty() && flags_key == init_key && !off_plan) {
			return IDENTITY_FRAME;
		}
		const std::string key = frame_key(stack, epoch, flags_key, off_plan);
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

	// (Stage 128: an event's at most two steps, inline)
	struct Steps {
		std::array<TraceStep, 2> s;
		int n = 0;
		Steps() = default;
		Steps(const TraceStep &a) : s{a, TraceStep{}}, n(1) {}
		Steps(const TraceStep &a, const TraceStep &b) : s{a, b}, n(2) {}
		TraceStep *begin() { return s.data(); }
		TraceStep *end() { return s.data() + n; }
	};

	// start -> infinity along the start tangent, infinity -> end along the end
	// tangent (escape-step convention: `via` holds the direction).
	static Steps through_infinity(const Beam<F64> &bf, double tf, double tt, V2 start, V2 end, int fid) {
		const CN<F64> tg0 = bf.tangent_at(F64{tf});
		TraceStep out_step = TraceStep::make(start, V2::inf(), fid, std::nullopt, ray_of(start, V2::of(tg0.re.v, tg0.im.v)), V2::of(tg0.re.v, tg0.im.v), false);
		const CN<F64> tg1 = bf.tangent_at(F64{tt});
		TraceStep in_step = TraceStep::make(V2::inf(), end, fid, std::nullopt, ray_of(end, V2::of(tg1.re.v, tg1.im.v)), -V2::of(tg1.re.v, tg1.im.v), false);
		return {out_step, in_step};
	}

	// One event = the arc from its t_from to t_to on its beam. Arcs that wrap
	// through t = infinity on a straight beam are split at infinity.
	template <Num N>
	static Steps steps_for(const Event<N> &e, int fid, const std::optional<V2> &prev_pt) {
		const Beam<N> &b = *e.beam;
		const Beam<F64> bf{{F64{b.p.fx()}, F64{b.p.fy()}}, {F64{b.tau.fx()}, F64{b.tau.fy()}}, F64{b.kappa.to_float()}};
		const double tf = e.t_from->to_float();
		const bool is_line = bf.kappa.v == 0.0;
		if (is_line && std::isinf(tf)) {
			// the arc after an `infinity` event (Stage 88): it starts at infinity.
			// The event itself drew the outgoing half; this is the incoming half
			// (nothing to draw when the beam only escapes again).
			if (!e.t_to) {
				return {};
			}
			const double tt1 = e.t_to->to_float();
			const V2 end1 = V2::of(e.point->fx(), e.point->fy());
			const CN<F64> tg1 = bf.tangent_at(F64{tt1});
			return {TraceStep::make(V2::inf(), end1, fid, std::nullopt, ray_of(end1, V2::of(tg1.re.v, tg1.im.v)), -V2::of(tg1.re.v, tg1.im.v), false)};
		}
		const V2 start = prev_pt ? *prev_pt : at_view_v2(bf, tf);
		if (!e.t_to) {
			// escape to infinity along a straight beam (also the `infinity`
			// event's outgoing half, and a max cut at the wrap)
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
		if (!is_line && start == end && (via - start).length() < float(1.0 / std::fabs(bf.kappa.v))) {
			// K9: two events at one drawn point (a tie at an exact common point):
			// a step of zero length. Its middle, recomputed on the beam, is a rounding
			// away; only a middle at the antipode (a diameter away) is the full loop.
			return {TraceStep::make(start, end, fid, std::nullopt, std::nullopt, start, true)};
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

// Stage 116: the physical hits by surface (TracePath.physical_hits: the plan
// overlays read them): for every step ending on a surface -- or on the wrap
// through infinity (INFINITY_ID, the infinity plan entry's record) -- its side,
// whether on the segment, whether the path goes on, and where.
struct PhysicalHit {
	int side = -1;
	bool on_segment = false;
	bool has_continuation = false;
	V2 point;
};

// (the surfaces in the order first hit, as the GDScript Dictionary kept them)
inline std::vector<std::pair<int, std::vector<PhysicalHit>>> physical_hits(const std::vector<TraceStep> &steps) {
	std::vector<std::pair<int, std::vector<PhysicalHit>>> out;
	for (size_t i = 0; i < steps.size(); i++) {
		const TraceStep &s = steps[i];
		if (s.surface_id < 0 && s.surface_id != INFINITY_ID) {
			continue;
		}
		auto it = std::find_if(out.begin(), out.end(), [&](const auto &e) { return e.first == s.surface_id; });
		if (it == out.end()) {
			out.push_back({s.surface_id, {}});
			it = out.end() - 1;
		}
		it->second.push_back({s.hit_side, s.hit_on_segment, i + 1 < steps.size(), s.end});
	}
	return out;
}

} // namespace rc
