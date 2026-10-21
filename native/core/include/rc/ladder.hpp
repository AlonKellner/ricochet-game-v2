// Error estimation and the dynamic-precision ladder (Stage 95): the port of
// scripts/math/precision/error_estimate.gd and precision_ladder.gd.
//
// A priori (tangent-linear): the Dual walk carries a unit seed on the beam's
// tangent angle where the sensitive forward continuation starts; G_i =
// |d p_i / d phi| is the amplification of a rounding error injected there,
// taken as a running maximum (envelope):  est_i(tier) = C * eps(tier) * max_{j<=i} G_j.
// A posteriori: the distance between two tiers (event_distance).
//
// The ladder: one Dual pass gives the float64 trace, its estimate and a
// reliability flag (every decision margin must exceed its error estimate);
// float64 is accepted when the estimate is <= tol and the trace is reliable,
// otherwise BigFloat tiers are climbed (replay_climb re-estimates along the
// high-precision trajectory; climb compares consecutive tiers).
#pragma once

#include "rc/solver.hpp"

#include <chrono>
#include <functional>

namespace rc {

// Events of the shipped float64 walk, carried on as the trace's event list
// (Dual values; points and parameters read as float64).
using F64Events = std::vector<Event<Dual>>;

// The ladder's tiers (limbs; 0 = float64) and the eps of a tier.
inline const std::vector<int> TIERS = {0, 4, 8, 16, 32, 64};
inline int tier_bits(int limbs) { return limbs <= 0 ? 53 : limbs * BigFloat::BITS; }
inline double tier_eps(int limbs) { return std::pow(2.0, -tier_bits(limbs)); }

namespace estimate {

constexpr double C = 4096.0;      // calibrated on the oracle corpus
constexpr double SCALE = 4096.0;  // coordinate magnitude bound for the unamplified floor
constexpr double MARGIN_SAFETY = 16.0;

struct Sensitivity {
	std::vector<double> gain;  // per event G_i envelope (0 where not seeded)
	double max_gain = 0.0;
	std::vector<double> margin; // the events' decision margins
	int unreliable_from = -1;   // first event whose margin is not comfortably above its estimate
};

inline std::vector<double> estimate(const Sensitivity &sens, double ctx_eps) {
	std::vector<double> out;
	const double floor_err = ctx_eps * SCALE * 4.0;
	for (double g : sens.gain) {
		out.push_back(gd_maxf(C * ctx_eps * g, floor_err));
	}
	return out;
}

inline int unreliable_index(const Sensitivity &sens, double ctx_eps) {
	const std::vector<double> est = estimate(sens, ctx_eps);
	for (size_t i = 0; i < sens.margin.size(); i++) {
		if (sens.margin[i] <= MARGIN_SAFETY * est[i]) {
			return int(i);
		}
	}
	return -1;
}

// The envelope of a float64 walk's gains, with its reliability at float64.
template <Num N>
Sensitivity collect(const std::vector<Event<N>> &ev, const std::vector<double> &gains) {
	Sensitivity s;
	double envelope = 0.0;
	for (size_t i = 0; i < ev.size(); i++) {
		envelope = gd_maxf(envelope, gains[i]);
		s.gain.push_back(envelope);
		s.margin.push_back(ev[i].margin);
	}
	s.max_gain = envelope;
	s.unreliable_from = unreliable_index(s, tier_eps(0));
	return s;
}

// Tangent-linear gains along an arbitrary-precision trajectory, computed in
// float64: the dual beam's values are reset at every step to the float64
// rounding of the high-precision beam, while the derivatives keep propagating.
template <Num N>
Sensitivity replay(const std::vector<Event<N>> &events, const Scene<Dual> &scene) {
	Sensitivity s;
	std::optional<Beam<Dual>> db; // the dual beam, or none before the seed
	int cur_view = -1;
	double envelope = 0.0;
	for (const Event<N> &e : events) {
		double g = 0.0;
		if (e.seeded) {
			if (!db || e.beam_view != cur_view) {
				const Beam<Dual> vals{{Dual{e.beam->p.fx(), 0.0}, Dual{e.beam->p.fy(), 0.0}},
					{Dual{e.beam->tau.fx(), 0.0}, Dual{e.beam->tau.fy(), 0.0}}, Dual{e.beam->kappa.to_float(), 0.0}};
				if (!db) {
					db = *Backend<Dual>::seed(std::make_shared<const Beam<Dual>>(vals));
				} else {
					db->p.re.v = vals.p.re.v;
					db->p.im.v = vals.p.im.v;
					db->tau.re.v = vals.tau.re.v;
					db->tau.im.v = vals.tau.im.v;
					db->kappa.v = vals.kappa.v;
				}
				cur_view = e.beam_view;
			}
			if (e.point && e.t_to) {
				const double tf = e.t_to->to_float();
				double dt = 0.0;
				const Surf<Dual> *surf = e.kind == EventKind::Hit ? scene.find(e.surface_id) : nullptr;
				if (surf != nullptr) {
					// the root this event is (nearest in t), with its derivative
					const std::vector<Dual> hs = geom::hits(*db, surf->seg, false);
					double best = INF_D;
					for (const Dual &r : hs) {
						if (r.v == tf) {
							dt = r.d;
							break;
						}
						if (std::fabs(r.v - tf) < best) {
							best = std::fabs(r.v - tf);
							dt = r.d;
						}
					}
				}
				const CN<Dual> h = db->at(Dual{tf, dt});
				g = std::sqrt(h.re.d * h.re.d + h.im.d * h.im.d);
				if (e.kind == EventKind::Hit && e.applied && !e.terminal && surf != nullptr) {
					// the map the trace applied (the hit point from the beam itself): a
					// projective's exit (B2, fixed in Stage 102: it used to get the
					// surface's reflection / inversion map); else the surface's own map,
					// which at a pop is the inverse (self-inverse sides; a portal pair)
					db = e.projective ? effects::project<Dual>(*db, Dual{tf, dt}, *surf, e.left, nullptr)
									  : effects::apply<Dual>(*db, Dual{tf, dt}, *surf, nullptr);
				}
			}
		}
		envelope = gd_maxf(envelope, g);
		s.gain.push_back(envelope);
		s.margin.push_back(e.margin);
	}
	s.max_gain = envelope;
	return s;
}

// Smallest ladder tier (limbs; 0 = float64) whose a-priori estimate is <= tol.
inline int required_limbs(const Sensitivity &sens, double tol, const std::vector<int> &tiers) {
	for (int limbs : tiers) {
		const double eps = tier_eps(limbs);
		if (C * eps * sens.max_gain <= tol && eps * SCALE * 4.0 <= tol) {
			return limbs;
		}
	}
	return tiers.back();
}

} // namespace estimate

// ---------------------------------------------------------------- the ladder

// A trace accepted at one tier: its events (float64 Dual events for tier 0,
// BigFloat events above), per-event error estimate, and the climb record.
struct Climb {
	F64Events f64_events;                 // tier 0
	std::vector<Event<BigFloat>> big_events; // tiers > 0
	std::vector<double> err;
	int limbs = 0;
	bool converged = false;
	std::vector<int> tried;
	bool empty() const { return limbs == 0 ? f64_events.empty() : big_events.empty(); }
	size_t size() const { return limbs == 0 ? f64_events.size() : big_events.size(); }
};

struct LadderResult {
	Climb planned;
	Climb physical;
	bool planned_from_physical = false;
	estimate::Sensitivity physical_sens; // float64 tangent-linear gains of the physical pass
	long long usec = 0;
	long long usec_planned = 0;
	long long usec_physical = 0;
};

// What the ladder did (counters for the benches; they never steer a solve).
struct LadderStats {
	long long walks = 0;      // decisions taken (one per walk solved)
	long long f64 = 0;        // walks accepted at float64
	long long unreliable = 0; // declined: a decision margin within its error estimate
	long long over_tol = 0;   // declined: the estimate above tol
	long long verify = 0;     // verified by a climb
	long long tier_solves[6] = {}; // BigFloat walks computed per tier (index into TIERS)
	long long big_usec = 0;   // time in those walks
};

// The scenes of one solve at every tier, built on demand, plus the solve caches.
class LadderContext {
public:
	explicit LadderContext(std::vector<SurfaceDef> defs) : defs_(std::move(defs)) {}

	const std::vector<SurfaceDef> &defs() const { return defs_; }
	// Stage 88: the `infinity` event switch of every scene this context builds
	// (a change drops the scenes and the solve caches).
	bool emit_infinity() const { return emit_infinity_; }
	void set_emit_infinity(bool on) {
		if (on == emit_infinity_) {
			return;
		}
		emit_infinity_ = on;
		dual_.reset();
		big_.clear();
		dual_cache_ = SolveCache<Dual>();
		big_caches_.clear();
	}
	Scene<Dual> &dual_scene() {
		if (!dual_) {
			dual_ = std::make_unique<Scene<Dual>>(Scene<Dual>::from_defs(defs_, Dual{1.0, 0.0}));
			dual_->emit_infinity = emit_infinity_;
		}
		return *dual_;
	}
	Scene<BigFloat> &big_scene(int limbs) {
		auto it = big_.find(limbs);
		if (it == big_.end()) {
			it = big_.emplace(limbs, std::make_unique<Scene<BigFloat>>(Scene<BigFloat>::from_defs(defs_, BigFloat::from_int(1, limbs)))).first;
			it->second->emit_infinity = emit_infinity_;
		}
		return *it->second;
	}
	SolveCache<Dual> &dual_cache() { return dual_cache_; }
	SolveCache<BigFloat> &big_cache(int limbs) { return big_caches_[limbs]; }
	LadderStats &stats() { return stats_; }

private:
	LadderStats stats_;
	std::vector<SurfaceDef> defs_;
	bool emit_infinity_ = false;
	std::unique_ptr<Scene<Dual>> dual_;
	std::map<int, std::unique_ptr<Scene<BigFloat>>> big_;
	SolveCache<Dual> dual_cache_;
	std::map<int, SolveCache<BigFloat>> big_caches_;
};

class Ladder {
public:
	// Stage 121: a provisional solve stops at float64 -- a walk the ladder would
	// climb returns its float64 events instead (not converged) and sets
	// needed_climb; a walk accepted at float64 is the exact solve's, bit for bit.
	bool provisional = false;
	bool needed_climb = false;
	// A clock for the usec fields (injectable: tests pass a constant one).
	using Clock = std::function<long long()>;
	static long long steady_usec() {
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	explicit Ladder(LadderContext &ctx, Clock clock = steady_usec) : ctx_(ctx), clock_(std::move(clock)) {}

	// player / cursor: float64 (never float32-rounded here). tol: px.
	LadderResult solve(double px, double py, double cx, double cy, const std::vector<PlanItem> &plan, int max_planned,
			int max_physical, double tol, bool verify = false, const Flags &flags = Flags(), std::optional<bool> force = std::nullopt) {
		force_ = force;
		const long long t0 = clock_();
		LadderResult r;
		// ---- physical
		solve_physical_into(r, px, py, cx, cy, plan, max_physical, tol, verify, flags);
		const Climb &cp = r.physical;
		const long long t1 = clock_();
		r.usec_physical = t1 - t0;
		if (cp.empty()) {
			return r;
		}
		// ---- planned: the physical walk that reached the cursor anchored decided
		// as the planned walk decides: one trajectory, no planned pass needed
		const int ci = cursor_index(cp);
		if (!verify && !plan.empty() && ci >= 0 && cp.limbs == 0 && cp.f64_events[size_t(ci)].anchored && max_planned == max_physical) {
			r.planned = cp;
			r.planned_from_physical = true;
			r.usec = clock_() - t0;
			return r;
		}
		if (!verify && !plan.empty() && ci >= 0 && cp.limbs > 0 && cp.big_events[size_t(ci)].anchored && max_planned == max_physical) {
			r.planned = cp;
			r.planned_from_physical = true;
			r.usec = clock_() - t0;
			return r;
		}
		Solver<Dual> fast(ctx_.dual_scene(), &ctx_.dual_cache());
		fast.force_reversed = force_;
		const CN<Dual> P{Dual{px, 0.0}, Dual{py, 0.0}};
		const CN<Dual> Cc{Dual{cx, 0.0}, Dual{cy, 0.0}};
		std::optional<Walk<Dual>> wq = fast.run(P, Cc, plan, Mode::Planned, max_planned, flags);
		if (!wq || wq->events.empty()) {
			r.physical = Climb{};
			return r;
		}
		estimate::Sensitivity sq = estimate::collect(wq->events, wq->gains);
		std::vector<double> est_q = estimate::estimate(sq, tier_eps(0));
		const std::vector<double> res_q = planned_residuals(wq->events, ctx_.dual_scene());
		for (size_t i = 0; i < est_q.size(); i++) {
			est_q[i] = gd_maxf(est_q[i], res_q[i]);
		}
		// the planned prefix must be reliable in float64 and the accepted physical
		// trace must still agree with it through the cursor
		const int cq_i = cursor_index_of(wq->events);
		if (!verify && cq_i >= 0 && (sq.unreliable_from < 0 || sq.unreliable_from > cq_i) && same_structure(wq->events, cp, size_t(cq_i) + 1)) {
			r.planned = cp;
		} else {
			auto planned_fn = [&](int limbs) -> std::vector<Event<BigFloat>> {
				Solver<BigFloat> big(ctx_.big_scene(limbs), &ctx_.big_cache(limbs));
				big.force_reversed = force_;
				const BigFloat l = BigFloat::from_int(1, limbs);
				return big.planned(CN<BigFloat>{l.like(px), l.like(py)}, CN<BigFloat>{l.like(cx), l.like(cy)}, plan, max_planned, flags);
			};
			r.planned = decide(sq, std::move(wq->events), est_q, tol, verify, planned_fn);
		}
		r.usec_planned = clock_() - t1;
		r.usec = clock_() - t0;
		return r;
	}

	// The physical trace alone.
	LadderResult solve_physical(double px, double py, double cx, double cy, const std::vector<PlanItem> &plan, int max_physical,
			double tol, const Flags &flags = Flags()) {
		const long long t0 = clock_();
		LadderResult r;
		solve_physical_into(r, px, py, cx, cy, plan, max_physical, tol, false, flags);
		r.usec_physical = clock_() - t0;
		r.usec = r.usec_physical;
		return r;
	}

	// Per-event distance between two traces; INF from the first structural difference on.
	template <Num A, Num B>
	static std::vector<double> event_distance(const std::vector<Event<A>> &a, const std::vector<Event<B>> &b) {
		std::vector<double> out;
		bool broken = false;
		for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
			if (broken || i >= a.size() || i >= b.size()) {
				out.push_back(INF_D);
				broken = true;
				continue;
			}
			const Event<A> &ea = a[i];
			const Event<B> &eb = b[i];
			if (ea.kind != eb.kind || ea.surface_id != eb.surface_id || ea.left != eb.left || ea.applied != eb.applied) {
				out.push_back(INF_D);
				broken = true;
				continue;
			}
			if (!ea.point || !eb.point) {
				out.push_back(0.0);
				continue;
			}
			const double dx = ea.point->fx() - eb.point->fx();
			const double dy = ea.point->fy() - eb.point->fy();
			out.push_back(std::sqrt(dx * dx + dy * dy));
		}
		return out;
	}

	// A-posteriori residual estimate for the anchored planned trace: every event
	// point must lie on its surface's carrier and, for applied non-portal hits,
	// on the next beam (constructed independently). float64 only.
	static std::vector<double> planned_residuals(const F64Events &events, const Scene<Dual> &scene) {
		std::vector<double> out;
		const double floor_err = tier_eps(0) * estimate::SCALE * 4.0;
		for (size_t i = 0; i < events.size(); i++) {
			const Event<Dual> &e = events[i];
			double res = 0.0;
			if (e.point && e.kind == EventKind::Hit) {
				const Surf<Dual> &s = *scene.find(e.surface_id);
				const double x = e.point->fx();
				const double y = e.point->fy();
				if (s.line) {
					const double dx = s.ex - s.sx;
					const double dy = s.ey - s.sy;
					res = std::fabs(dx * (y - s.sy) - dy * (x - s.sx)) / std::sqrt(dx * dx + dy * dy);
				} else {
					res = std::fabs(std::sqrt((x - s.cx) * (x - s.cx) + (y - s.cy) * (y - s.cy)) - s.r);
				}
				// (a portal's next beam starts at T(point), elsewhere: no shared point)
				if (e.applied && !e.terminal && !e.teleport && i + 1 < events.size()) {
					const Event<Dual> &nx = events[i + 1];
					// FastSolver.at(rb, FastSolver.param_of(rb, x, y), 0.0) on the raw beam
					const Beam<Dual> &rb = *nx.beam;
					const CN<Dual> q = rb.at(Dual{rb.param_of(CN<Dual>{Dual{x, 0.0}, Dual{y, 0.0}}).v, 0.0});
					const double qx = q.re.v;
					const double qy = q.im.v;
					res = gd_maxf(res, std::sqrt((qx - x) * (qx - x) + (qy - y) * (qy - y)));
				}
			}
			out.push_back(gd_maxf(res * 4.0, floor_err));
		}
		return out;
	}

private:
	LadderContext &ctx_;
	Clock clock_;
	std::optional<bool> force_; // Stage 111: the orientation the caller forces on every tier

	using Compute = std::function<std::vector<Event<BigFloat>>(int)>;

	// (Stage 120: the walk's events moved, not copied, into the estimate and the result)
	void solve_physical_into(LadderResult &r, double px, double py, double cx, double cy, const std::vector<PlanItem> &plan,
			int max_physical, double tol, bool verify, const Flags &flags) {
		Solver<Dual> fast(ctx_.dual_scene(), &ctx_.dual_cache());
		fast.force_reversed = force_;
		const CN<Dual> P{Dual{px, 0.0}, Dual{py, 0.0}};
		const CN<Dual> Cc{Dual{cx, 0.0}, Dual{cy, 0.0}};
		std::optional<Walk<Dual>> wp = fast.run(P, Cc, plan, Mode::Physical, max_physical, flags);
		F64Events ev = wp ? std::move(wp->events) : F64Events{};
		std::vector<double> gains = wp ? std::move(wp->gains) : std::vector<double>{};
		const estimate::Sensitivity sp = estimate::collect(ev, gains);
		const std::vector<double> est_p = estimate::estimate(sp, tier_eps(0));
		auto physical_fn = [&](int limbs) -> std::vector<Event<BigFloat>> {
			Solver<BigFloat> big(ctx_.big_scene(limbs), &ctx_.big_cache(limbs));
			big.force_reversed = force_;
			const BigFloat l = BigFloat::from_int(1, limbs);
			return big.physical(CN<BigFloat>{l.like(px), l.like(py)}, CN<BigFloat>{l.like(cx), l.like(cy)}, plan, max_physical, flags);
		};
		r.physical = decide(sp, std::move(ev), est_p, tol, verify, physical_fn);
		r.physical_sens = sp;
	}

	static int cursor_index_of(const F64Events &events) {
		for (size_t i = 0; i < events.size(); i++) {
			if (events[i].kind == EventKind::Cursor) {
				return int(i);
			}
		}
		return -1;
	}
	static int cursor_index(const Climb &c) {
		if (c.limbs == 0) {
			return cursor_index_of(c.f64_events);
		}
		for (size_t i = 0; i < c.big_events.size(); i++) {
			if (c.big_events[i].kind == EventKind::Cursor) {
				return int(i);
			}
		}
		return -1;
	}

	// Same event structure (kind, surface, side, applied, state) for the first n events.
	template <Num A, Num B>
	static bool same_structure_ev(const std::vector<Event<A>> &a, const std::vector<Event<B>> &b, size_t n) {
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
	static bool same_structure(const F64Events &a, const Climb &c, size_t n) {
		return c.limbs == 0 ? same_structure_ev(a, c.f64_events, n) : same_structure_ev(a, c.big_events, n);
	}

	static double worst(const std::vector<double> &a) {
		double w = 0.0;
		for (double x : a) {
			w = gd_maxf(w, x);
		}
		return w;
	}

	Climb decide(const estimate::Sensitivity &sens, F64Events events, const std::vector<double> &est, double tol, bool verify,
			const Compute &compute) {
		const bool reliable = sens.unreliable_from < 0 && !events.empty();
		LadderStats &st = ctx_.stats();
		st.walks++;
		if (verify) {
			st.verify++;
		} else if (!reliable) {
			st.unreliable++;
		} else if (worst(est) > tol) {
			st.over_tol++;
		} else {
			st.f64++;
		}
		if (provisional && !verify && !(reliable && worst(est) <= tol)) {
			needed_climb = true;
			Climb c;
			c.f64_events = std::move(events);
			c.err = est;
			c.limbs = 0;
			c.converged = false;
			c.tried = {0};
			return c;
		}
		if (reliable && worst(est) <= tol && !verify) {
			Climb c;
			c.f64_events = std::move(events);
			c.err = est;
			c.limbs = 0;
			c.converged = true;
			c.tried = {0};
			return c;
		}
		int start_limbs = 4;
		if (reliable || sens.unreliable_from > 0) {
			start_limbs = std::max(4, required_for_prefix(sens, reliable ? int(sens.gain.size()) : sens.unreliable_from, tol));
		}
		if (verify) {
			const bool start_f64 = reliable && worst(est) <= tol;
			return climb(start_f64 ? 0 : start_limbs, start_f64 ? &events : nullptr, tol, compute);
		}
		return replay_climb(start_limbs, tol, compute);
	}

	static int required_for_prefix(const estimate::Sensitivity &sens, int n, double tol) {
		estimate::Sensitivity prefix;
		prefix.gain.assign(sens.gain.begin(), sens.gain.begin() + n);
		for (double g : prefix.gain) {
			prefix.max_gain = gd_maxf(prefix.max_gain, g);
		}
		return estimate::required_limbs(prefix, tol, std::vector<int>(TIERS.begin() + 1, TIERS.end()));
	}

	static int tier_index(int limbs) {
		for (size_t i = 0; i < TIERS.size(); i++) {
			if (TIERS[i] == limbs) {
				return int(i);
			}
		}
		return -1;
	}

	// compute(limbs), counted in the context's stats
	std::vector<Event<BigFloat>> timed(int limbs, const Compute &compute) {
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<Event<BigFloat>> ev = compute(limbs);
		LadderStats &st = ctx_.stats();
		const int ti = tier_index(limbs);
		if (ti >= 0) {
			st.tier_solves[ti]++;
		}
		st.big_usec += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
		return ev;
	}

	// Compute at `limbs`, re-estimate along that trajectory with replay, accept
	// when the estimate meets tol and every decision is reliable at that tier;
	// otherwise jump to the tier the replay asks for.
	Climb replay_climb(int limbs, double tol, const Compute &compute) {
		Climb c;
		int ti = tier_index(limbs);
		while (ti >= 0 && ti < int(TIERS.size())) {
			const int L = TIERS[size_t(ti)];
			std::vector<Event<BigFloat>> ev = timed(L, compute);
			c.tried.push_back(L);
			c.big_events = ev;
			c.limbs = L;
			const double eps = tier_eps(L);
			const estimate::Sensitivity rs = estimate::replay(ev, ctx_.dual_scene());
			c.err = estimate::estimate(rs, eps);
			const int bad = estimate::unreliable_index(rs, eps);
			if (bad < 0 && worst(c.err) <= tol && !ev.empty()) {
				c.converged = true;
				return c;
			}
			// next tier: what the (reliable prefix of the) replay asks for, at least +1
			const int need = required_for_prefix(rs, bad < 0 ? int(rs.gain.size()) : std::max(bad, 1), tol);
			ti = std::max(ti + 1, tier_index(need));
		}
		return c;
	}

	// Climb tiers from `limbs` until two consecutive tiers agree within tol.
	Climb climb(int limbs, const F64Events *first_events, double tol, const Compute &compute) {
		Climb c;
		int ti = tier_index(limbs);
		// the previous tier's events, as float64 points for the distance
		std::vector<Event<BigFloat>> prev_big;
		F64Events prev_f64;
		bool prev_is_f64 = first_events != nullptr;
		if (first_events) {
			prev_f64 = *first_events;
			c.f64_events = prev_f64;
		} else {
			prev_big = timed(limbs, compute);
			c.big_events = prev_big;
		}
		c.tried.push_back(limbs);
		c.limbs = limbs;
		while (ti + 1 < int(TIERS.size())) {
			const int nxt = TIERS[size_t(ti) + 1];
			std::vector<Event<BigFloat>> ev = timed(nxt, compute);
			c.tried.push_back(nxt);
			const std::vector<double> d = prev_is_f64 ? event_distance(prev_f64, ev) : event_distance(prev_big, ev);
			c.big_events = ev;
			c.f64_events.clear();
			c.limbs = nxt;
			const double ratio = tier_eps(nxt) / tier_eps(TIERS[size_t(ti)]);
			c.err.clear();
			for (double x : d) {
				c.err.push_back(x * ratio);
			}
			if (worst(d) <= tol && !ev.empty()) {
				c.converged = true;
				return c;
			}
			prev_big = ev;
			prev_is_f64 = false;
			ti++;
		}
		return c;
	}
};

} // namespace rc
