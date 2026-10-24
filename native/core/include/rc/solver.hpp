// The precision-generic trajectory solver (Stage 94): the port of
// scripts/math/precision/beam_solver.gd, one walker for every tier. The
// trace rules are those of the mpmath oracle (docs/research/oracle/trace2.py),
// listed in that file's header; the anchored planned path (B_j through
// P_j, X_j, Q_j) is the construction of GAME_SPEC 13.1.
//
// Tiers: F64 (the generic float64 walk), Dual (the shipped float64 walk with
// tangent-linear gains: the GDScript FastSolver), BigFloat (the ladder's high
// tiers). Backend<N> holds the few tier-specific rules (seeding, straightening,
// the display-safe float64 point of margins); everything else is written once.
// Beams are shared pointers: an event's beam identity (which events lie on one
// beam object) is part of the observable output.
#pragma once

#include "rc/beam_effects.hpp"
#include "rc/small_vec.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <bit>
#include <unordered_map>
#include <vector>

namespace rc {

// Stage 131 (GAME_SPEC 12.6): no loop detection -- two limits end every trace:
// events in all (every carrier crossing and passage of infinity; not the
// cursor), and significant events (applied effects, firing state changes,
// passages of infinity)
constexpr int MAX_HITS = 64;
constexpr int MAX_SIGNIFICANT = 10;
// Plan entry / event id of the point at infinity (Stage 88): the player
// clicked the level bounds' edge; the beam wraps through infinity there.
constexpr int INFINITY_ID = -3;
// Plan entry / event id of the ray's origin (Stage 132, GAME_SPEC 4.2, 12.6):
// the beam passes the player's image in the frame (a circle normal's centre).
constexpr int ORIGIN_ID = -4;
// Anchored-prefix gains are scaled to their own calibration constant
// (C_prefix = ErrorEstimate.C * PREFIX_GAIN_SCALE).
constexpr double PREFIX_GAIN_SCALE = 64.0 / 4096.0;
// Budget of the exact aim image (limbs of its homogeneous weight).
constexpr int EXACT_IMAGE_LIMBS = 320;
constexpr double INF_D = std::numeric_limits<double>::infinity();

enum class Mode { Physical = 0, Planned = 1 };

enum class EventKind { Hit, Cursor, Origin, Max, Infinity };
inline const char *event_kind_name(EventKind k) {
	switch (k) {
		case EventKind::Hit: return "hit";
		case EventKind::Cursor: return "cursor";
		case EventKind::Origin: return "origin";
		case EventKind::Max: return "max";
		case EventKind::Infinity: return "infinity";
	}
	return "";
}

// Stage 123: exact data a region's cast passes by construction: its member's
// point of interest, found after the plan's first j entries (Solver::aim_known)
struct AimKnown {
	int j = 0;
	double x = 0.0, y = 0.0;
	bool operator==(const AimKnown &) const = default;
};

struct PlanItem {
	int surface_id = 0;
	bool left = true;
	std::optional<effects::F32Point> hint; // where the player clicked (settles a fork)
	// Stage 108 (GAME_SPEC 9.7): a piece of the surface -- its span (start x, y,
	// end x, y, via x, y) on the surface's carrier; nullopt = the whole surface
	std::optional<std::array<double, 6>> span;
	// the infinity entry (no side): the beam wraps through infinity here
	bool is_infinity() const { return surface_id == INFINITY_ID; }
	// the origin entry (no side, Stage 132): the beam passes the ray's origin here
	bool is_origin() const { return surface_id == ORIGIN_ID; }
};

// Stage 132 (GAME_SPEC 4.2): the entries leave the walk behind the ray's origin --
// an odd number of infinity and origin entries (each passage turns the half of the
// ray's line the walk is on).
inline bool behind_origin(const std::vector<PlanItem> &plan, size_t n) {
	int k = 0;
	for (size_t i = 0; i < n && i < plan.size(); i++) {
		k += (plan[i].is_origin() || plan[i].is_infinity()) ? 1 : 0;
	}
	return k % 2 == 1;
}

template <Num N>
using BeamPtr = std::shared_ptr<const Beam<N>>;

template <Num N>
struct Event {
	EventKind kind = EventKind::Hit;
	int surface_id = -1;
	bool left = false;      // side the beam comes from: true = LEFT
	bool applied = false;   // the side's effect fired (terminal included)
	bool terminal = false;
	bool on_segment = false;
	bool rehit = false;     // the departure point, re-hit after the full loop
	bool popped = false;    // the applied effect popped the transform stack
	bool teleport = false;  // the applied effect was a portal
	bool projective = false;
	int epoch = 0;          // projectives start a new epoch
	std::optional<CN<N>> point;
	BeamPtr<N> beam;        // beam the arc leading to this event lies on
	std::optional<N> t_from;
	std::optional<N> t_to;  // nullopt = infinity
	double margin = INF_D;  // px gap to the nearest competing decision
	uint8_t why = 0;        // Stage 123: what set the margin (Why: an instrument, the ladder's stats)
	bool seeded = false;    // lies on the sensitive forward continuation
	bool state = false;     // the side's state change fired
	bool fired = false;     // Stage 107: the side fired, ghost aside (its hit effects fire)
	Flags flags;            // the trace's flags after this event (hits)
	bool anchored = false;  // cursor: reached on the anchored path
	int beam_view = -1;     // identity of the beam as the events see it (Backend::views_per_run)
	bool off_plan = false;  // Stage 129: the walk has left the plan by this event (GAME_SPEC 14.2: a part of its frame)
};

// Stage 123: the exact incidence of a point on a surface's carrier (Solver::
// incident: a known point -- an endpoint, the cursor -- against every surface,
// in every walk), memoized across walks and solves like PoleMemo: keyed on the
// scene, the point's float64 bits and the surface, so an equal key gives the
// identical answer and the walks are unchanged bit for bit.
struct IncidenceMemo {
	struct Key {
		const void *scene;
		uint64_t x, y;
		int surf;
		bool operator==(const Key &) const = default;
	};
	struct Hash {
		size_t operator()(const Key &k) const {
			uint64_t h = uint64_t(reinterpret_cast<uintptr_t>(k.scene)) * 0x9E3779B97F4A7C15ull;
			h = (h ^ k.x) * 0x100000001B3ull;
			h = (h ^ k.y) * 0x100000001B3ull;
			return size_t((h ^ uint64_t(uint32_t(k.surf))) * 0x100000001B3ull);
		}
	};
	// F22 (Stage 135): open addressing in one array -- a node per insertion made the
	// region's threads contend in the allocator; the table allocates only as it grows
	struct Slot {
		Key k{};
		bool v = false;
		bool used = false;
	};
	std::vector<Slot> slots;
	size_t count = 0;
	static size_t slot_of(const Key &k, size_t mask) {
		uint64_t h = Hash{}(k);
		h ^= h >> 29; // (the multiplies leave the low bits to the keys' low bits)
		return size_t(h) & mask;
	}
	bool *find(const Key &k) {
		if (slots.empty()) {
			return nullptr;
		}
		const size_t mask = slots.size() - 1;
		for (size_t i = slot_of(k, mask); slots[i].used; i = (i + 1) & mask) {
			if (slots[i].k == k) {
				return &slots[i].v;
			}
		}
		return nullptr;
	}
	void put(const Key &k, bool v) {
		if (count > (1u << 18)) { // bounded: a long session's cursors
			std::fill(slots.begin(), slots.end(), Slot{});
			count = 0;
		}
		if ((count + 1) * 2 > slots.size()) {
			std::vector<Slot> old = std::move(slots);
			slots.assign(old.empty() ? 1024 : old.size() * 2, Slot{});
			count = 0;
			for (const Slot &o : old) {
				if (o.used) {
					insert(o.k, o.v);
				}
			}
		}
		insert(k, v);
	}

private:
	void insert(const Key &k, bool v) {
		const size_t mask = slots.size() - 1;
		size_t i = slot_of(k, mask);
		for (; slots[i].used; i = (i + 1) & mask) {
			if (slots[i].k == k) {
				return; // (emplace: the first value stays)
			}
		}
		slots[i] = Slot{k, v, true};
		count++;
	}
};

// Stage 120: the exact predicates of a frame's infinity point that depend on Q
// alone -- its carrier incidences and its images under the circle inversions --
// memoized across walks and solves (one memo per SolveCache: per tracer tier,
// per defs). Keyed on Q's exact representation, the scene and the surface: an
// equal key gives the identical result, so the walks are unchanged bit for bit.
struct PoleMemo {
	struct Entry {
		exact::Point3 Q;
		const void *scene = nullptr;
		int surf = 0;
		std::optional<bool> on;                            // pole_on: the carrier passes Q
		std::optional<std::optional<exact::Point3>> image; // invert_point_h(Q, the surface's circle)
	};
	std::unordered_map<uint64_t, std::vector<Entry>> by_hash;
	size_t count = 0;

	static bool same(const BigFloat &a, const BigFloat &b) {
		if (a.sign != b.sign || a.exp != b.exp || a.limbs.size() != b.limbs.size()) {
			return false;
		}
		for (size_t i = 0; i < a.limbs.size(); i++) {
			if (a.limbs[i] != b.limbs[i]) {
				return false;
			}
		}
		return true;
	}
	Entry &entry(const exact::Point3 &Q, const void *scene, int surf) {
		uint64_t h = uint64_t(reinterpret_cast<uintptr_t>(scene)) * 0x9E3779B97F4A7C15ull ^ uint64_t(uint32_t(surf));
		for (const BigFloat &x : Q) {
			h = (h ^ uint64_t(int64_t(x.sign) + 7)) * 0x100000001B3ull;
			h = (h ^ uint64_t(x.exp)) * 0x100000001B3ull;
			for (size_t i = 0; i < x.limbs.size(); i++) {
				h = (h ^ uint64_t(x.limbs[i])) * 0x100000001B3ull;
			}
		}
		std::vector<Entry> &bucket = by_hash[h];
		for (Entry &e : bucket) {
			if (e.scene == scene && e.surf == surf && same(e.Q[0], Q[0]) && same(e.Q[1], Q[1]) && same(e.Q[2], Q[2])) {
				return e;
			}
		}
		if (++count > 65536) { // bounded: a long session's distinct frames
			by_hash.clear();
			count = 1;
			return by_hash[h].emplace_back(Entry{Q, scene, surf, std::nullopt, std::nullopt});
		}
		return bucket.emplace_back(Entry{Q, scene, surf, std::nullopt, std::nullopt});
	}
};

template <Num N>
struct Walk; // forward

// ---------------------------------------------------------------- tier rules

template <Num N>
struct Backend {
	static bool is_dual() { return std::is_same_v<N, Dual>; }

	// Tangent-linear seed (Dual only): unit perturbation of the beam's tangent
	// angle where the sensitive forward continuation starts. Returns the same
	// object when nothing changes.
	static BeamPtr<N> seed(const BeamPtr<N> &b) {
		if constexpr (std::is_same_v<N, Dual>) {
			return std::make_shared<const Beam<N>>(seeded(*b));
		} else {
			return b;
		}
	}
	// (Dual: the seeded beam's value -- Stage 128: prefix_gain without a heap beam)
	static Beam<N> seeded(const Beam<N> &b) {
		if constexpr (std::is_same_v<N, Dual>) {
			const double tx = b.tau.re.v;
			const double ty = b.tau.im.v;
			return Beam<N>{{Dual{b.p.re.v, 0.0}, Dual{b.p.im.v, 0.0}}, {Dual{tx, -ty}, Dual{ty, tx}}, Dual{b.kappa.v, 0.0}};
		} else {
			return b;
		}
	}
	// A beam restored from the unseeded (anchored) prefix gets a seed.
	static BeamPtr<N> reseed(const BeamPtr<N> &b) {
		if constexpr (std::is_same_v<N, Dual>) {
			if (b->p.re.d == 0.0 && b->p.im.d == 0.0 && b->tau.re.d == 0.0 && b->tau.im.d == 0.0 && b->kappa.d == 0.0) {
				return seed(b);
			}
			return b;
		} else {
			return b;
		}
	}
	// the same beam with curvature exactly zero (provenance: it is a line)
	static BeamPtr<N> straightened(const BeamPtr<N> &b) {
		if constexpr (std::is_same_v<N, Dual>) {
			return std::make_shared<const Beam<N>>(Beam<N>{b->p, b->tau, Dual{0.0, b->kappa.d}});
		} else {
			return std::make_shared<const Beam<N>>(Beam<N>{b->p, b->tau, b->kappa.sub(b->kappa)});
		}
	}
	static double gain(const Beam<N> &b, const N &t) {
		if constexpr (std::is_same_v<N, Dual>) {
			const CN<Dual> h = b.at(t);
			return std::sqrt(h.re.d * h.re.d + h.im.d * h.im.d);
		} else {
			(void)b;
			(void)t;
			return 0.0;
		}
	}
	// anchored prefix: sensitivity to the anchored beam's own angle
	static double prefix_gain(const Beam<N> &b, const N &t) {
		if constexpr (std::is_same_v<N, Dual>) {
			const Beam<N> s = seeded(b); // (the same arithmetic as seed(): a value, no allocation)
			const CN<Dual> h = s.at(Dual{t.v, 0.0});
			return std::sqrt(h.re.d * h.re.d + h.im.d * h.im.d);
		} else {
			(void)b;
			(void)t;
			return 0.0;
		}
	}
	// The parameter of a point: FastSolver's backend returns it without a
	// derivative (only roots carry one); the other tiers have none.
	static N param_of(const Beam<N> &b, const CN<N> &z) {
		if constexpr (std::is_same_v<N, Dual>) {
			return Dual{b.param_of(z).v, 0.0};
		} else {
			return b.param_of(z);
		}
	}
	// An exact hit point h on the beam at t: FastSolver takes the derivative
	// of the beam's own evaluation at t with h's values; the other tiers have no derivative.
	static CN<N> exact_point(const Beam<N> &b, const N &t, const CN<N> &h) {
		if constexpr (std::is_same_v<N, Dual>) {
			const CN<Dual> a = b.at(t);
			return CN<Dual>{Dual{h.re.v, a.re.d}, Dual{h.im.v, a.im.d}};
		} else {
			(void)b;
			(void)t;
			return h;
		}
	}
	// Event beam identity: the shipped float64 walk views the raw beam anew
	// whenever it changes from the previous event's (a returning beam gets a
	// new view); the generic tiers identify the beam object itself.
	static constexpr bool views_per_run() { return std::is_same_v<N, Dual>; }
	// float64 view of a point (decision margins only)
	static void f64_point(const Beam<N> &b, const N &t, double &x, double &y) {
		if constexpr (std::is_same_v<N, F64>) {
			const CN<N> z = b.at(t);
			x = z.fx();
			y = z.fy();
		} else {
			fast::at_view(b.p.fx(), b.p.fy(), b.tau.fx(), b.tau.fy(), b.kappa.to_float(), t.to_float(), x, y);
		}
	}
};

// ---------------------------------------------------------------- walk data

// A known point of the current beam: exact data the beam passes through.
template <Num N>
struct Known {
	CN<N> p;
	N t;
	int skip = -1; // the departure surface for a departure point
	std::shared_ptr<const std::vector<const Surf<N> *>> incident; // surfaces whose carrier contains p (cached; Scene::incidence_lists)
};

template <Num N>
struct StackEntry {
	BeamPtr<N> beam;
	int sid = 0;
	N t_hit;
	N other;
	std::vector<Known<N>> known;
	std::shared_ptr<const exact::Line> line;
	int pop_id = -1;      // the surface whose hit pops this entry (sid; a portal's pair)
	EffectType etype = EffectType::Pass; // the map pushed (a pop needs the same map)
	std::optional<CN<N>> q; // the frame's image of infinity before the push (restored by the pop)
	std::shared_ptr<const exact::PoleRec> pole; // the exact pole record before the push (Stage 105; lazy since A.3)
	int pole_serial = 0;
	std::shared_ptr<const exact::BeamRec> rec; // the exact beam before the push (Perf P2b)
	const Surf<N> *through_c = nullptr;        // K15: the provenance before the push (restored by the pop)
	std::optional<CN<N>> o;                    // Stage 131: the ray's origin before the push (restored by the pop)
};

template <Num N>
struct Candidate {
	N t;
	const Surf<N> *surf = nullptr; // nullptr = the cursor (or the wrap, see `inf`)
	bool rehit = false;
	std::optional<CN<N>> pt;       // the exact point, by provenance
	int tie = -1;                  // concurrent-tie group index
	bool inf = false;              // the wrap through t = infinity of a straight beam (Stage 88)
	bool at_q = false;             // pinned at the frame's infinity point (Stage 105)
	bool origin = false;           // Stage 131: the passage of the ray's origin (surf nullptr, pt the origin)
};

// Anchored beams B_0..B_K of a plan (flat: beams[j] is the beam after j
// consumed entries; a popped frame IS the earlier beam object).
// Stage 111: how the plan's realization was decided (GAME_SPEC 13.1).
enum class RealizationKind {
	NoEntry,    // no planned surface entry in the first epoch: today's rule (R3)
	NoCrossing, // no orientation fires the first entry (the aim parallel to its carrier, a line missing a circle): today's rule, the cast dead
	Entry,      // the first entry decided it (R1, R2)
	Alike       // both orientations fire it alike and the walk did not decide: today's rule (R3)
};

template <Num N>
struct AnchorChain {
	std::vector<BeamPtr<N>> beams;
	std::map<int, CN<N>> hits;                                 // projective entry index -> its back-propagated H
	std::map<int, CN<N>> exact_hits;                           // Stage 113 (K11): -> H when it is a data point of its segment (exact)
	std::map<int, std::shared_ptr<const exact::Line>> exit_lines; // projective entry index -> exact exit line
	bool reversed = false; // the first epoch runs the other way round (Stage 111: the plan's realization)
	RealizationKind kind = RealizationKind::NoEntry;
};

// B5 (Stage 110): the physical walk at one point -- a run of candidates with one
// parameter (surfaces on one carrier share their crossings, K6; ties and snaps
// give one parameter). The plan is matched by the frame the walk leaves the
// point in, whatever the order of the surfaces there: `flags` and `index` are
// the planned walk's (its shadow) over the point's candidates; a physical flag
// change out of plan leaves the point `pending` until the shadow's flags equal
// the walk's (the walk then takes the shadow's plan index); a point closed
// pending unmatches the plan. (trace2.py: _Point.)
template <Num N>
struct PointGroup {
	N t;
	bool rehit = false;
	bool wrap = false;     // the wrap through the real infinity: a point of its own
	Flags flags;
	int index = 0;
	bool broke = false;    // the shadow consumed a transformative or projective entry
	bool pending = false;
};

// Stage 124/128: a walk step's working vectors (its candidates, their margins
// and the margins' classes, the shared roots, the gaps' points), kept across
// steps and -- Stage 128 -- leased per thread for the duration of a walk: the
// region's thousands of short walks each grew their own, a fifth of all their
// allocations
// Stage 128: a few entries keyed by a small key, inline (no allocation up to C;
// a SmallVec past it), searched in order -- a walk's per-surface memos, which
// as std::maps took a heap node per entry in every one of the region's walks.
// The subset of std::map's interface they use.
template <class K, class V, size_t C = 16>
class SmallMap {
public:
	struct Item {
		K first;
		V second;
	};
	Item *find(const K &k) {
		for (Item &i : items_) {
			if (i.first == k) {
				return &i;
			}
		}
		return end();
	}
	Item *end() { return items_.end(); }
	V &operator[](const K &k) {
		if (Item *i = find(k); i != end()) {
			return i->second;
		}
		items_.push_back(Item{k, V{}});
		return items_.back().second;
	}
	void clear() { items_.clear(); }

private:
	SmallVec<Item, C> items_;
};
// (a point and a surface: Walk::incidence's key)
struct PointSurf {
	double x = 0.0, y = 0.0;
	int surf = 0;
	bool operator==(const PointSurf &) const = default;
};

template <Num N>
struct StepScratch {
	std::vector<const Surf<N> *> shared_surfs; // Stage 138: the surfaces holding a hit point
	std::vector<Candidate<N>> cands;
	std::vector<double> gaps;
	std::vector<uint8_t> why; // (Stage 123: the margins' classes, the instrument)
	std::vector<std::pair<int, InlineVec<N, 4>>> shared;
	std::vector<std::pair<double, double>> pts;
	std::vector<int> dbl;
};
template <Num N>
class ScratchLease {
public:
	ScratchLease() {
		std::vector<std::unique_ptr<StepScratch<N>>> &p = pool();
		if (p.empty()) {
			s_ = std::make_unique<StepScratch<N>>();
		} else {
			s_ = std::move(p.back());
			p.pop_back();
		}
	}
	~ScratchLease() { pool().push_back(std::move(s_)); } // (a walk nested in this one, on this thread, leased another)
	ScratchLease(const ScratchLease &) = delete;
	ScratchLease &operator=(const ScratchLease &) = delete;
	StepScratch<N> &get() { return *s_; }

private:
	static std::vector<std::unique_ptr<StepScratch<N>>> &pool() {
		thread_local std::vector<std::unique_ptr<StepScratch<N>>> p;
		return p;
	}
	std::unique_ptr<StepScratch<N>> s_;
};

template <Num N>
struct Walk {
	const Scene<N> *scene = nullptr;
	std::vector<PlanItem> plan;
	std::optional<CN<N>> cursor;
	Mode mode = Mode::Physical;
	Mode post_mode = Mode::Physical;
	int plan_index = 0;
	bool plan_matched = true;
	bool left_plan = false; // Stage 129: a surface fired out of plan while entries remained (GAME_SPEC 14.2; sticky)
	bool wrapped_off = false; // Stage 131: an unplanned passage of infinity or of the ray's origin before the cursor
	// Stage 133 (GAME_SPEC 9.7): the plan's pieces, per entry (nullopt: the whole
	// surface) -- an end that is a crossing read as the crossing itself
	std::vector<std::optional<Seg<N>>> spans;
	bool cursor_done = false;
	int count = 0;
	int max_events = MAX_HITS;
	int max_sig = MAX_SIGNIFICANT; // Stage 131
	int sig = 0;                    // ... the significant events so far
	// Workflow W2.0: a decision walk (realize, R2) needs the cursor event alone:
	// it stops there (the events up to it are the same walk's)
	bool stop_at_cursor = false;
	std::vector<Event<N>> events;
	std::vector<double> gains;
	std::vector<StackEntry<N>> stack;
	bool done = false;
	bool seeded = false;
	std::shared_ptr<const AnchorChain<N>> chain;
	bool anchored = false;
	int epoch = 0;
	std::vector<Known<N>> known;
	std::shared_ptr<const exact::Line> line;
	const Surf<N> *through_c = nullptr; // K15 (Stage 114): the circle normal whose centre the current beam passes by provenance (its exit)
	SmallMap<PointSurf, bool, 32> incidence;
	std::shared_ptr<const exact::Line> tangent_line;
	SmallMap<int, bool> tangent;
	// Stage 112: the line carriers the current exact line is parallel to (cached per line)
	std::shared_ptr<const exact::Line> parallel_line;
	SmallMap<int, bool> parallel;
	// Perf P2b: the current beam's exact generalized circle, lazily (from_line
	// of `line` whenever there is one; mapped by every effect; nullptr: no
	// exact claim), and its tangencies to carriers (cached per record)
	std::shared_ptr<const exact::BeamRec> rec;
	std::shared_ptr<const exact::BeamRec> rec_tangent_of;
	SmallMap<int, bool> rec_tangent;
	// K7: whether the record's circle passes the cursor (cached per record)
	std::shared_ptr<const exact::BeamRec> rec_cursor_of;
	bool rec_cursor_on = false;
	// Stage 113 (K11): the chain's exact hit points the current exact circle passes (cached per record)
	std::shared_ptr<const exact::BeamRec> rec_hits_of;
	std::vector<CN<N>> rec_hits;
	Flags flags;
	bool on_plan = true;
	// Stage 101: the current frame's image of infinity (nullopt = infinity):
	// the pre-transform's at the start, mapped forward by every applied effect,
	// restored by a pop, infinity again after a projective
	std::optional<CN<N>> Q;
	std::optional<CN<N>> O; // Stage 131: the ray's origin in the frame (the player's image; nullopt = infinity)
	// Stage 105: the exact pole record (Q exactly, and the beam's direction at
	// Q), or nullopt (Q at infinity, or the frame's data not exact); its serial
	// keys the tangency cache
	std::shared_ptr<const exact::PoleRec> pole; // the frame's infinity point as exact data, lazily (A.3: exact::PoleRec)
	// the current beam departs from the frame's infinity point (provenance: an
	// effect fired at a crossing pinned at Q): no pinning, no passage of Q
	bool depart_q = false;
	int pole_serial = 0;
	int pole_serials = 0;
	int pole_tangent_serial = -1;
	SmallMap<int, bool> pole_tangent;   // surf id -> tangent at Q
	SmallMap<int, bool> pole_on;        // surf id -> its carrier passes Q
	PoleMemo *pole_memo = nullptr;      // Stage 120: across walks (the solver's cache's), or none
	IncidenceMemo *incidence_memo = nullptr; // Stage 123: likewise
	// tests only: the record beside each beam loop (record_poles)
	bool record_poles = false;
	std::vector<std::pair<BeamPtr<N>, std::optional<exact::ExactPole>>> pole_trace;
	bool record_why = false;  // A.8: fill Event::why (the margins' instrument, LadderStats::why*; RC_LADDER_WHY=1)
	// A.7: candidate(w, s) per surface for the walk's state (its mode, plan index and
	// flags) -- a pure function of them, asked for every surface at every step
	std::vector<uint8_t> cand_mask;
	bool cand_valid = false;
	Mode cand_mode = Mode::Physical;
	int cand_index = 0;
	Flags cand_flags;
	size_t horizon_size = SIZE_MAX, horizon_value = 0; // A.8: read_horizon's value at horizon_size events (pure in the events)
	std::vector<std::vector<Known<N>>> known_trace; // and the known points (Stage 106)
	// and whether the frame is exact there: Q finite with a record, or the
	// beam straight (Q at infinity) on an exact line
	std::vector<bool> exact_trace;
	BeamPtr<N> hp_src;
	int hp_view = -1;
	int view_count = 0;
	std::vector<std::pair<const Beam<N> *, int>> beam_views; // (Stage 128: a few per walk, searched in order)
	std::optional<PointGroup<N>> point; // B5: the point being crossed (physical walk)
	StepScratch<N> *scratch = nullptr; // Stage 128: the step's working vectors while it walks (walk_from's lease)
	const std::function<size_t(const std::vector<Event<N>> &)> *read_horizon = nullptr; // Stage 124: Solver::read_horizon
};

// The exact memos (the cast-cost program, A.1): the exact aim images, the
// pre-transform starts, the pole predicates and the carrier incidences -- pure
// functions of exact inputs (float64 data and exact arithmetic), the same at
// every tier -- one per LadderContext, shared by its tiers' SolveCaches (a climb
// through the tiers computes each once).
struct ExactCache {
	// Stage 128: the aim image's key as exact bits (the %.17g text it was told
	// every number apart the same way, at a printf per cast)
	struct ImageKey {
		SmallVec<uint64_t, 48> words;
		bool operator==(const ImageKey &o) const {
			return words.size() == o.words.size() && std::equal(words.begin(), words.end(), o.words.begin());
		}
	};
	struct ImageHash {
		size_t operator()(const ImageKey &k) const {
			uint64_t h = 14695981039346656037ULL;
			for (uint64_t w : k.words) {
				h = (h ^ w) * 1099511628211ULL;
			}
			return size_t(h);
		}
	};
	std::unordered_map<ImageKey, std::optional<exact::Point3>, ImageHash> images;
	// Stage 128: an aimed shot's exact start in a pre-transform frame (its pole
	// and its circle through P, X0, Q0), pure functions of the three points: a
	// cast's two walks start alike. The last few, by the points' exact bits.
	struct Start {
		uint64_t px = 0, py = 0;
		exact::Point3 X0, Q0;
		bool has_pole = false, has_gc = false;
		std::optional<exact::ExactPole> pole;
		exact::GCircle gc;
	};
	std::vector<Start> starts;
	PoleMemo poles; // Stage 120
	IncidenceMemo incidence; // Stage 123
};

// Caches owned by the caller (one per solve / tracer): the anchored chains of
// the last inputs and the exact memos.
template <Num N>
struct SolveCache {
	struct ChainKey {
		const void *scene = nullptr;
		double px = 0, py = 0, cx = 0, cy = 0;
		std::string flags;
		std::vector<std::tuple<int, bool, bool, float, float>> plan;
		int force = 0; // Stage 111: 0 the plan's realization, 1 forced direct, 2 forced reversed
		bool operator==(const ChainKey &o) const {
			return scene == o.scene && px == o.px && py == o.py && cx == o.cx && cy == o.cy && flags == o.flags && plan == o.plan && force == o.force;
		}
	};
	// the last four chains (A.1: a split region's tie cast and own cast alternate
	// the force on one cursor; one entry evicted the other every cast)
	struct ChainEntry {
		ChainKey key;
		std::shared_ptr<const AnchorChain<N>> chain;
	};
	std::vector<ChainEntry> chains;
	std::shared_ptr<ExactCache> exact = std::make_shared<ExactCache>();
	// A.5: plan_effects per (plan, flags) -- a cast computed it 6-10 times -- and
	// piece_spans per plan's spans -- every walk and tier computed it, with the
	// carriers' crossings at the tier's precision. Pure functions; the last few.
	struct EffectsEntry {
		std::vector<std::pair<int, bool>> plan;
		Flags flags;
		std::vector<EffectType> effs;
	};
	std::vector<EffectsEntry> effects;
	struct SpansEntry {
		std::vector<std::pair<int, std::optional<std::array<double, 6>>>> plan;
		std::vector<std::optional<Seg<N>>> spans;
	};
	std::vector<SpansEntry> spans;
};

// ---------------------------------------------------------------- the solver

template <Num N>
class Solver {
public:
	Solver(const Scene<N> &scene, SolveCache<N> *cache = nullptr) : scene_(scene), cache_(cache) {}

	// tests only: walks record the exact pole beside each beam (Walk::pole_trace)
	void set_record_poles(bool on) { record_poles_ = on; }

	// Walk an explicit direction from the player (forward shooting).
	Walk<N> run_direction(const CN<N> &player, const CN<N> &dir, const std::vector<PlanItem> &plan,
			const std::optional<CN<N>> &cursor, Mode mode, int max_events = MAX_HITS, const Flags &flags = Flags()) {
		Walk<N> w = new_walk(plan, cursor, mode, max_events, flags);
		// a pre-transform frame: the circle through Q0 along the direction (no exact line)
		BeamPtr<N> b0 = std::make_shared<const Beam<N>>(w.Q ? Beam<N>::through_tangent(player, dir.unit(), *w.Q)
															 : Beam<N>{player, dir.unit(), player.re.sub(player.re)});
		if (mode == Mode::Physical) {
			b0 = Backend<N>::seed(b0);
			w.seeded = true;
		}
		w.known = {Known<N>{player, player.re.like(0.0), -1, nullptr}};
		if (b0->kappa.is_zero()) {
			w.line = exact::Line::from_exact(exact::line_from_dir(player.fx(), player.fy(), dir.fx(), dir.fy()));
			w.rec = exact::BeamRec::from_line(w.line);
			add_line_endpoints(w, *b0);
		}
		if (const std::optional<exact::Point3> q0 = pre_q_exact()) {
			set_pole(w, exact::PoleRec::start_direction(player.fx(), player.fy(), dir.fx(), dir.fy(), *q0));
		}
		w.O = b0->p; // (Stage 131: the first ray's origin, the player)
		walk_from(w, b0, -1, std::nullopt);
		return w;
	}

	std::vector<Event<N>> planned(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			int max_events = MAX_HITS, const Flags &flags = Flags()) {
		std::optional<Walk<N>> w = run(player, cursor, plan, Mode::Planned, max_events, flags);
		return w ? std::move(w->events) : std::vector<Event<N>>{};
	}

	std::vector<Event<N>> physical(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			int max_events = MAX_HITS, const Flags &flags = Flags()) {
		std::optional<Walk<N>> w = run(player, cursor, plan, Mode::Physical, max_events, flags);
		return w ? std::move(w->events) : std::vector<Event<N>>{};
	}

	// nullopt if the anchored construction is degenerate and player = cursor.
	std::optional<Walk<N>> run(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan, Mode mode,
			int max_events = MAX_HITS, const Flags &flags = Flags()) {
		std::shared_ptr<const AnchorChain<N>> chain = select_chain(player, cursor, plan, flags);
		if (!chain) {
			return run_degenerate(player, cursor, plan, mode, max_events, flags);
		}
		return walk_chain(player, cursor, plan, mode, max_events, flags, chain);
	}

	// Forward state simulation (GAME_SPEC 13.7): the effect each plan entry
	// resolves to, under the flags left by the entries before it.
	std::vector<EffectType> plan_effects(const std::vector<PlanItem> &plan, const Flags &flags) const {
		std::vector<std::pair<int, bool>> key;
		if (cache_ != nullptr) {
			key.reserve(plan.size());
			for (const PlanItem &item : plan) {
				key.emplace_back(item.surface_id, item.left);
			}
			for (const auto &e : cache_->effects) {
				if (e.plan == key && e.flags == flags) {
					return e.effs;
				}
			}
		}
		std::vector<EffectType> out = plan_effects_uncached(plan, flags);
		if (cache_ != nullptr) {
			if (cache_->effects.size() >= 8) {
				cache_->effects.erase(cache_->effects.begin());
			}
			cache_->effects.push_back({std::move(key), flags, out});
		}
		return out;
	}
	std::vector<EffectType> plan_effects_uncached(const std::vector<PlanItem> &plan, const Flags &flags) const {
		Flags f = flags;
		std::vector<EffectType> out;
		out.reserve(plan.size());
		for (const PlanItem &item : plan) {
			const Surf<N> *surf = scene_.find(item.surface_id);
			if (surf == nullptr || !surf->present(f)) {
				// absent (Stage 107): no effect, no state change
				out.push_back(EffectType::Pass);
				continue;
			}
			out.push_back(surf->effect(item.left, f));
			f = f.applied(surf->state_changes(item.left, f));
		}
		return out;
	}

	// The flags after the plan's entries, simulated forward from `flags`.
	// Stage 88: the plan's first epoch (its entries before the first projective)
	// holds an infinity entry: the aim is the second realization, away from the
	// aim image, reaching it through infinity (GAME_SPEC 13.1).
	// Stage 132: the first epoch's entries leave the walk behind the ray's origin
	// (an odd number of infinity and origin entries).
	bool via_infinity(const std::vector<PlanItem> &plan, const Flags &flags) const {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		size_t n = 0;
		while (n < plan.size() && !is_projective(effs[n])) {
			n++;
		}
		return behind_origin(plan, n);
	}

	// Stage 111: the index of the first planned entry of the first epoch that
	// names a present surface (infinity entries and absent surfaces skipped; a
	// pass side, a terminal, a projective count: the planned walk fires only on
	// an id and side match), or -1.
	int first_entry(const std::vector<PlanItem> &plan, const Flags &flags) const {
		for (size_t j = 0; j < plan.size(); j++) {
			if (plan[j].is_infinity() || plan[j].is_origin()) {
				continue;
			}
			const Surf<N> *surf = scene_.find(plan[j].surface_id);
			if (surf != nullptr && surf->present(flags)) {
				return int(j);
			}
		}
		return -1;
	}

	// Stage 111: the plan's realization -- whether B_0 (P -> X_0 -> Q_0, toward
	// the aim image) runs the other way round. Let e be the first planned entry
	// of the first epoch naming a present surface. R1: when only one
	// orientation of B_0 crosses e's carrier from e's side (a straight beam and
	// a line carrier, always), that one -- decided exactly: the sign of
	// dot(X_0 - P, grad) from the homogeneous aim image with rigorous bounds,
	// exactly when the bounds straddle zero, the tier's value when the image
	// is not exact data. R2: when both do (a circle carrier, a circle beam),
	// the one whose planned walk consumes the plan and reaches the cursor;
	// when both or neither do, the one that fires e before the passage of Q_0
	// -- after it when an infinity entry precedes e. R3: otherwise (no e, no
	// crossing, both orientations alike) today's rule: toward X_0, reversed by
	// an infinity entry in the first epoch (via_infinity). Infinity entries are
	// otherwise only matched by the walker (GAME_SPEC 13.1).
	struct Realization {
		bool reversed = false;
		RealizationKind kind = RealizationKind::NoEntry;
	};

	// first_firing: (q) of the first crossing of surf's carrier from `left`
	// along the beam from t = 0 in the walker's projective order (t > 0
	// ascending, the pole, t <= 0 ascending; at one parameter the hit before
	// the pole), q = the pole was passed first; nullopt when no crossing fires.
	std::optional<bool> first_firing(const Beam<N> &b, const Surf<N> &surf, bool left, const std::optional<N> &tq,
			const std::shared_ptr<const exact::Line> &line, const exact::PoleRec *pole, const std::optional<CN<N>> &Q) const {
		if (line && !surf.line) {
			const auto &ec = surf.exact_circle();
			if (line->tangent_to(ec.C, ec.rw2, ec.fcx, ec.fcy, ec.frr, ecc(ec), 8.0 * exact::U * std::fabs(ec.frr))) {
				return std::nullopt; // a tangent touch is not a crossing (exact)
			}
		}
		// a carrier through the frame's infinity point (Stage 105): tangent there,
		// a touch; else crossed exactly there -- its root nearest Q is Q's
		// parameter, so the hit and the passage tie exactly (the hit first)
		bool through_q = false;
		if (pole != nullptr && Q) {
			if (pole_rec_on(*pole, surf, nullptr)) {
				if (pole_rec_tangent(*pole, surf)) {
					return std::nullopt;
				}
				through_q = true;
			}
		}
		// (a crossing at t = 0 -- the player on the carrier -- is met last, after
		// the full loop; one at t = infinity -- the antipode of a circle beam -- at
		// the wrap: as the walker orders them)
		std::vector<N> rs = geom::hits(b, surf.seg, false);
		if (rs.size() == 2 && rs[0].cmp(rs[1]) == 0) {
			return std::nullopt; // a double root: a touch
		}
		if (through_q && tq && !rs.empty()) {
			size_t best = 0;
			double bd = INF_D;
			for (size_t i = 0; i < rs.size(); i++) {
				const double d = b.at(rs[i]).sub(*Q).abs2().to_float();
				if (d < bd) {
					bd = d;
					best = i;
				}
			}
			rs[best] = *tq;
		}
		struct Cand {
			N t;
			bool pole;
		};
		std::vector<Cand> cands;
		for (const N &t : rs) {
			cands.push_back(Cand{t, false});
		}
		if (tq) {
			cands.push_back(Cand{*tq, true});
		}
		const N t0 = b.p.re.like(0.0);
		std::sort(cands.begin(), cands.end(), [&](const Cand &a, const Cand &c) {
			const int cmp = a.t.cmp(c.t);
			if (cmp != 0) {
				return before(a.t, c.t, t0);
			}
			return c.pole && !a.pole;
		});
		bool q = false;
		static const bool trace = std::getenv("RC_REALIZE_TRACE") != nullptr;
		for (const Cand &c : cands) {
			if (c.pole) {
				if (trace) {
					std::fprintf(stderr, "  cand pole t %.9g\n", c.t.to_float());
				}
				q = true;
				continue;
			}
			const bool sl = geom::side_left(surf.seg, b.at(c.t), b.tangent_at(c.t));
			if (trace) {
				const CN<N> pt = b.at(c.t);
				std::fprintf(stderr, "  cand hit t %.9g at (%.6g, %.6g) left %d want %d kappa %.6g\n", c.t.to_float(), pt.fx(), pt.fy(), int(sl), int(left), b.kappa.to_float());
			}
			if (sl == left) {
				return q;
			}
		}
		return std::nullopt;
	}

	static bool eb_nonzero(const exact::EB &x) { return std::isfinite(x.v) && std::isfinite(x.e) && std::fabs(x.v) > x.e; }

	// R1 exactly: sign of dot(X_0 - P, grad(seg)) for a line carrier, 0 when
	// parallel or undecidable from exact data (nullopt: no exact image).
	std::optional<int> exact_aim_grad_sign(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags, const Surf<N> &surf) const {
		using exact::EB;
		const double gx = -(surf.ey - surf.sy);
		const double gy = surf.ex - surf.sx;
		const double px = player.fx();
		const double py = player.fy();
		if (const std::optional<std::array<EB, 3>> X = exact_image_approx(cursor, plan, flags)) {
			// dot((X - px w, Y - py w), g) and w
			const EB rx = exact::esub((*X)[0], exact::emul(EB{px, 0.0}, (*X)[2]));
			const EB ry = exact::esub((*X)[1], exact::emul(EB{py, 0.0}, (*X)[2]));
			const EB d = exact::eadd(exact::emul(rx, EB{gx, 0.0}), exact::emul(ry, EB{gy, 0.0}));
			if (eb_nonzero(d) && eb_nonzero((*X)[2])) {
				return (d.v < 0.0) != ((*X)[2].v < 0.0) ? -1 : 1;
			}
		}
		const std::optional<exact::Point3> Xh = exact_image_cached(cursor, plan, flags);
		if (!Xh) {
			return std::nullopt;
		}
		const BigFloat &w = (*Xh)[2];
		if (w.is_zero()) {
			return std::nullopt; // the image at infinity
		}
		const BigFloat rx = exact::sub((*Xh)[0], exact::mul(exact::of(px), w));
		const BigFloat ry = exact::sub((*Xh)[1], exact::mul(exact::of(py), w));
		const BigFloat d = exact::add(exact::mul(rx, exact::of(gx)), exact::mul(ry, exact::of(gy)));
		if (d.is_zero()) {
			return 0;
		}
		return (d.is_negative() != w.is_negative()) ? -1 : 1;
	}

	// The realization of the aim beam b0 (P -> X_0 -> Q_0). `walks`: the
	// anchored chain to walk both ways when both orientations fire e (R2);
	// nullptr (the degenerate aim) decides by the order against Q_0 alone.
	Realization realize(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags,
			const Beam<N> &b0, const std::shared_ptr<const AnchorChain<N>> &walks) {
		const Realization fallback{via_infinity(plan, flags), RealizationKind::Alike};
		const int j = first_entry(plan, flags);
		if (j < 0) {
			return Realization{via_infinity(plan, flags), RealizationKind::NoEntry};
		}
		const Surf<N> &surf = *scene_.find(plan[size_t(j)].surface_id);
		const bool left = plan[size_t(j)].left;
		const std::optional<CN<N>> q0 = effects::scene_pre_q(scene_);
		if (!q0 && surf.line) {
			// R1: a straight beam and a line carrier cross once per loop
			std::optional<int> sign = exact_aim_grad_sign(player, cursor, plan, flags, surf);
			if (!sign) {
				// no exact image (a rotation portal, a projective's target): the tier's value
				const N d = CN<N>::dot(b0.tau, CN<N>{b0.tau.re.like(-(surf.ey - surf.sy)), b0.tau.re.like(surf.ex - surf.sx)});
				sign = d.is_zero() ? 0 : (d.is_negative() ? -1 : 1);
			}
			if (*sign == 0) {
				return Realization{fallback.reversed, RealizationKind::NoCrossing}; // parallel: no realization fires e
			}
			// side_left of a line: LEFT iff dot(dir, grad) > 0 (its winding is negative)
			const bool left_direct = *sign > 0;
			return Realization{left_direct != left, RealizationKind::Entry};
		}
		// R2: both orientations may fire e
		const std::shared_ptr<const exact::Line> line = q0 ? nullptr : initial_line(player, cursor, plan, flags);
		// the exact pole record of a pre-transform start (Stage 105): the circle
		// through the player, the exact aim image and Q0
		std::shared_ptr<const exact::PoleRec> pole;
		if (q0) {
			if (const std::optional<exact::Point3> q0e = pre_q_exact()) {
				if (const std::optional<exact::Point3> X0 = exact_image_cached(cursor, plan, flags)) {
					pole = start_pole_rec(player.fx(), player.fy(), *X0, *q0e);
				}
			}
		}
		std::optional<N> tq;
		if (!q0) {
			if (b0.kappa.is_zero()) {
				tq = b0.p.re.like(INF_D);
			}
		} else {
			if (q0->sub(player).is_zero()) {
				return fallback; // the player at the frame's infinity: every aim degenerates
			}
			tq = Backend<N>::param_of(b0, *q0);
			if (tq->is_zero()) {
				return fallback;
			}
		}
		const Beam<N> rev{b0.p, b0.tau.neg(), b0.kappa.neg()};
		const std::optional<bool> qd = first_firing(b0, surf, left, tq, line, pole.get(), q0);
		std::optional<N> tq_rev = tq;
		if (tq && !tq->is_inf()) {
			tq_rev = Backend<N>::param_of(rev, *q0);
		}
		const std::optional<bool> qr = first_firing(rev, surf, left, tq_rev, line, pole.get(), q0);
		static const bool trace = std::getenv("RC_REALIZE_TRACE") != nullptr;
		if (trace) {
			std::fprintf(stderr, "realize: cursor (%.9g, %.9g) pole %d tq %.6g qd %s qr %s\n", cursor.fx(), cursor.fy(), int(pole && pole->exact().has_value()),
				tq ? tq->to_float() : -1.0, qd ? (*qd ? "after" : "before") : "none", qr ? (*qr ? "after" : "before") : "none");
		}
		if (qd.has_value() != qr.has_value()) {
			return Realization{qr.has_value(), RealizationKind::Entry};
		}
		if (!qd) {
			return Realization{fallback.reversed, RealizationKind::NoCrossing};
		}
		if (walks) {
			// the orientation whose planned walk consumes the plan and reaches the cursor
			bool reach[2] = {false, false};
			for (int o = 0; o < 2; o++) {
				std::shared_ptr<const AnchorChain<N>> ch = walks;
				if (o == 1) {
					auto rc = std::make_shared<AnchorChain<N>>(*walks);
					reverse_beams(rc->beams, 0, first_epoch_end(plan, flags));
					ch = rc;
				}
				// the plan's order is a question of geometry, not of the event budget:
				// a wide one, so a realization cut by MAX_HITS still counts as reaching
				const Walk<N> w = walk_chain(player, cursor, plan, Mode::Planned, 8 * MAX_HITS, flags, ch, true);
				for (const Event<N> &e : w.events) {
					if (e.kind == EventKind::Cursor) {
						reach[o] = true;
						break;
					}
				}
			}
			if (reach[0] != reach[1]) {
				return Realization{reach[1], RealizationKind::Entry};
			}
		}
		if (*qd != *qr) {
			// entries before e that leave the walk behind the ray's origin ask for the
			// passage of Q_0 first (Stage 132: the parity of infinity and origin entries;
			// with the switch off an infinity entry can never be consumed)
			const bool before_e = behind_origin(plan, size_t(j));
			return Realization{*qr == before_e, RealizationKind::Entry};
		}
		return fallback;
	}

	// one past the last beam of the first epoch (every beam up to and
	// including the one after the last pre-projective entry)
	size_t first_epoch_end(const std::vector<PlanItem> &plan, const Flags &flags) const {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		for (size_t j = 0; j < plan.size(); j++) {
			if (is_projective(effs[j])) {
				return j + 1;
			}
		}
		return plan.size() + 1;
	}

	// Stage 111: the region casts a member in a forced orientation (nullopt: the plan's realization).
	std::optional<bool> force_reversed;
	// Stage 123: a region's cast along a member through exact data (a segment's
	// endpoint; Stage 124: or an exact crossing, found after j plan entries): the
	// walk's beam after those entries passes it by construction, so it is a known
	// point there (add_aim_known), and a hit at it is decided by identity, never
	// by the ladder
	std::optional<AimKnown> aim_known;
	// Stage 124: the caller reads only the first events of a walk (a region's cast,
	// Ladder::horizon): the walk ends two events past them
	const std::function<size_t(const std::vector<Event<N>> &)> *read_horizon = nullptr;

	// Stage 111: the realization the order against Q_0 (or today's rule) picks
	// without the walk: what a cursor both planned walks reach gets; the region
	// reads it per member to split the cursors between the two orientations.
	Realization tie_choice(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags) {
		if (const std::shared_ptr<const AnchorChain<N>> chain = build_chain_direct(player, cursor, plan, flags)) {
			return realize(player, cursor, plan, flags, *chain->beams[0], nullptr);
		}
		const std::optional<std::pair<Beam<N>, std::shared_ptr<const exact::Line>>> db = degenerate_beam(player, cursor, plan, flags);
		if (!db) {
			return Realization{};
		}
		return realize(player, cursor, plan, flags, db->first, nullptr);
	}

	// The plan's realization for a cursor (public: the region and the invariants).
	Realization realization(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags) {
		if (const std::shared_ptr<const AnchorChain<N>> chain = select_chain(player, cursor, plan, flags)) {
			return Realization{chain->reversed, chain->kind};
		}
		const std::optional<std::pair<Beam<N>, std::shared_ptr<const exact::Line>>> db = degenerate_beam(player, cursor, plan, flags);
		if (!db) {
			return Realization{};
		}
		return realize(player, cursor, plan, flags, db->first, nullptr);
	}

	// Stage 101: the frame's image of infinity after `plan` (nullopt = infinity):
	// the pre-transform's, mapped through the transformative entries of the
	// last epoch on the provenance stack (a re-hit of the top pops it); a
	// projective starts the epoch at infinity. The point the next infinity
	// entry would pass; the UI's marker.
	std::optional<CN<N>> plan_pole(const std::vector<PlanItem> &plan, const Flags &flags) const {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		size_t start = 0;
		std::optional<CN<N>> q = effects::scene_pre_q(scene_);
		for (size_t j = 0; j < plan.size(); j++) {
			if (is_projective(effs[j])) {
				start = j + 1;
				q = std::nullopt;
			}
		}
		std::vector<const Surf<N> *> st;
		for (size_t j = start; j < plan.size(); j++) {
			const Surf<N> *surf = scene_.find(plan[j].surface_id);
			if (surf == nullptr || !is_transformative(effs[j])) {
				continue;
			}
			if (!st.empty() && pop_id_of(st.back()->id) == surf->id) {
				st.pop_back();
			} else {
				st.push_back(surf);
			}
		}
		for (const Surf<N> *s : st) {
			q = effects::map_point(q, *s, false);
		}
		return q;
	}

	// Stage 132 (GAME_SPEC 4.2): the ray's origin after the plan, and whether the
	// plan leaves the walk behind it (the origin entry is plannable there). The
	// origin: the player -- or the last projection's new ray's, a circle normal's
	// centre or a line projection's departure (its back-propagated hit, from the
	// cursor's anchor chain; unknown without a cursor) -- carried by the frame maps
	// after it. Behind: a circle normal projecting toward its centre, then turned
	// by each infinity and origin entry after the last projection.
	struct OriginState {
		std::optional<CN<N>> point; // nullopt: unknown (or at infinity)
		bool behind = false;
	};
	OriginState plan_origin(const CN<N> &player, const std::optional<CN<N>> &cursor, const std::vector<PlanItem> &plan, const Flags &flags) {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		OriginState o{player, false};
		size_t start = 0;
		for (size_t j = 0; j < plan.size(); j++) {
			if (!is_projective(effs[j])) {
				continue;
			}
			start = j + 1;
			const Surf<N> *surf = scene_.find(plan[j].surface_id);
			if (surf == nullptr) {
				o = OriginState{std::nullopt, false};
				continue;
			}
			if (effs[j] == EffectType::CircleNormalProjection) {
				const CN<N> &c = surf->seg.circ->c;
				// the ray heads for the centre when the beam comes from outside (through)
				// or from inside (back): the inside is where a ray leaving outward comes from
				const bool inside_left = geom::side_left(surf->seg, surf->seg.s, surf->seg.s.sub(c));
				const bool from_outside = plan[j].left != inside_left;
				o = OriginState{c, from_outside != surf->proj(plan[j].left).back};
			} else {
				o = OriginState{std::nullopt, false};
				if (cursor) {
					const std::vector<PlanItem> prefix(plan.begin(), plan.begin() + std::ptrdiff_t(j + 1));
					const std::shared_ptr<const AnchorChain<N>> ch = build_chain(player, *cursor, prefix, flags);
					if (ch) {
						if (auto it = ch->hits.find(int(j)); it != ch->hits.end()) {
							o.point = it->second;
						}
					}
				}
			}
		}
		// the half: the orientation's own parity (behind_origin, as the realization
		// reads it) over the entries since the last projection, from its start
		const std::vector<PlanItem> since(plan.begin() + std::ptrdiff_t(start), plan.end());
		o.behind = o.behind != behind_origin(since, since.size());
		std::vector<const Surf<N> *> st;
		for (size_t j = start; j < plan.size(); j++) {
			const Surf<N> *surf = scene_.find(plan[j].surface_id);
			if (surf == nullptr || !is_transformative(effs[j])) {
				continue;
			}
			if (!st.empty() && pop_id_of(st.back()->id) == surf->id) {
				st.pop_back();
			} else {
				st.push_back(surf);
			}
		}
		for (const Surf<N> *s : st) {
			if (o.point) {
				o.point = effects::map_point(o.point, *s, false);
			}
		}
		return o;
	}

	// Stage 133: the plan's pieces, ends that are crossings read as the crossings (the region's points of interest)
	std::vector<std::optional<Seg<N>>> plan_piece_spans(const std::vector<PlanItem> &plan) const { return piece_spans(plan); }

	Flags plan_flags(const std::vector<PlanItem> &plan, const Flags &flags) const {
		Flags f = flags;
		for (const PlanItem &item : plan) {
			const Surf<N> *surf = scene_.find(item.surface_id);
			if (surf == nullptr || !surf->present(f)) {
				continue;
			}
			f = f.applied(surf->state_changes(item.left, f));
		}
		return f;
	}

	// The map an effect applies: reflection on a circle carrier is the inversion in it.
	static EffectType map_kind(const Surf<N> &surf, EffectType etype) {
		if (etype == EffectType::Reflection && !surf.line) {
			return EffectType::Inversion;
		}
		return etype;
	}

	// Ids of target surfaces hit on-segment.
	std::vector<int> targets_of(const std::vector<Event<N>> &events) const {
		std::vector<int> out;
		for (const Event<N> &e : events) {
			if (e.kind == EventKind::Hit && e.on_segment) {
				const Surf<N> *s = scene_.find(e.surface_id);
				if (s != nullptr && s->is_target && std::find(out.begin(), out.end(), e.surface_id) == out.end()) {
					out.push_back(e.surface_id);
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	// Exact: every frame change of the plan fixes the cursor (it lies on the
	// carrier of each planned reflection / inversion; a portal fixes nothing).
	bool fixes_cursor(const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags) const {
		const double kx = cursor.fx();
		const double ky = cursor.fy();
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		for (EffectType e : effs) {
			if (is_projective(e)) {
				return false;
			}
		}
		for (size_t ji = 0; ji < plan.size(); ji++) {
			const Surf<N> *surf = scene_.find(plan[ji].surface_id);
			if (surf == nullptr || !is_transformative(effs[ji])) {
				continue;
			}
			if (surf->portal) {
				return false;
			}
			bool on;
			if (geom::endpoint_at(surf->seg, cursor) != 0) {
				on = true;
			} else if (surf->line) {
				on = exact::orient_zero(surf->sx, surf->sy, surf->ex, surf->ey, kx, ky);
			} else if (surf->full) {
				on = exact::on_circle_zero(surf->cx, surf->cy, surf->r, kx, ky);
			} else {
				on = exact::incircle_zero(surf->sx, surf->sy, surf->vx, surf->vy, surf->ex, surf->ey, kx, ky);
			}
			if (!on) {
				return false;
			}
		}
		return true;
	}

	// The exact inverse map of one transformative entry on an exact homogeneous
	// point: a line reflection, an inversion (exact circumcenter and radius²)
	// or a translation portal; nullopt for a rotation portal, an image at
	// infinity, or beyond max_limbs (0 = no budget).
	static std::optional<exact::Point3> exact_step_h(const exact::Point3 &X, const Surf<N> &surf, int max_limbs) {
		if (surf.portal) {
			if (surf.theta != 0.0) {
				return std::nullopt;
			}
			// T^-1(X) = X - d (homogeneous: X - d w)
			return exact::Point3{exact::sub(X[0], exact::mul(exact::of(surf.dx), X[2])), exact::sub(X[1], exact::mul(exact::of(surf.dy), X[2])), X[2]};
		}
		if (!surf.line) {
			const auto &ec = surf.exact_circle();
			std::optional<exact::Point3> inv = exact::invert_point_h(X, ec.C, ec.rw2);
			if (!inv || (max_limbs > 0 && (*inv)[2].precision_limbs() > max_limbs)) {
				return std::nullopt;
			}
			return inv;
		}
		return exact::reflect_point_h(X, surf.sx, surf.sy, surf.ex, surf.ey);
	}

	// Stage 113 (K11): the data point of surf's segment (an endpoint, the via)
	// that is the back-propagated hit point of the exact target X -- decided
	// exactly on the data, never by the rounding of c + R u: the foot of X on
	// the line (line normal), a point collinear with X and the exact centre
	// (circle normal), a point X + s n (directional); of several (antipodal
	// endpoints) the one the numeric H is at. nullopt for a full carrier (no
	// endpoints; its via plays no part in blocking) or when no data point is
	// the hit point. (Oracle: trace2._exact_data_hit.)
	std::optional<std::pair<CN<N>, exact::Point3>> exact_data_hit(const Surf<N> &surf, EffectType etype, bool left, const exact::Point3 &X,
			const CN<N> &H) const {
		if (surf.full) {
			return std::nullopt;
		}
		std::vector<std::pair<double, double>> pts = {{surf.sx, surf.sy}, {surf.ex, surf.ey}};
		if (surf.has_v) {
			pts.emplace_back(surf.vx, surf.vy);
		}
		std::optional<std::pair<CN<N>, exact::Point3>> best;
		double best_d = INF_D;
		for (const auto &[dx, dy] : pts) {
			// X - D w (homogeneous)
			const BigFloat vx = exact::sub(X[0], exact::mul(exact::of(dx), X[2]));
			const BigFloat vy = exact::sub(X[1], exact::mul(exact::of(dy), X[2]));
			bool ok;
			if (etype == EffectType::LineNormalProjection) {
				const BigFloat ux = exact::sub(exact::of(surf.ex), exact::of(surf.sx));
				const BigFloat uy = exact::sub(exact::of(surf.ey), exact::of(surf.sy));
				ok = exact::add(exact::mul(vx, ux), exact::mul(vy, uy)).is_zero();
			} else if (etype == EffectType::CircleNormalProjection) {
				// X, the centre C and D collinear: det [[x, y, w], [cx, cy, cw], [dx, dy, 1]] = 0
				const exact::Point3 &C = surf.exact_circle().C;
				const BigFloat D1 = exact::of(dx);
				const BigFloat D2 = exact::of(dy);
				const BigFloat t1 = exact::mul(X[0], exact::sub(C[1], exact::mul(C[2], D2)));
				const BigFloat t2 = exact::mul(X[1], exact::sub(C[0], exact::mul(C[2], D1)));
				const BigFloat t3 = exact::mul(X[2], exact::sub(exact::mul(C[0], D2), exact::mul(C[1], D1)));
				ok = exact::sub(exact::add(t1, t3), t2).is_zero();
			} else {
				const Projective &pr = surf.proj(left);
				ok = exact::sub(exact::mul(vx, exact::of(pr.ny)), exact::mul(vy, exact::of(pr.nx))).is_zero();
			}
			if (!ok) {
				continue;
			}
			const double d = std::hypot(H.fx() - dx, H.fy() - dy);
			if (d < best_d) {
				best_d = d;
				best = std::make_pair(CN<N>{H.re.like(dx), H.re.like(dy)}, exact::Point3{exact::of(dx), exact::of(dy), exact::of(1.0)});
			}
		}
		return best ? best : exact_float_hit(surf, etype, left, X, H);
	}

	// Stage 125 (K11): with no data point the hit point, the hit point computed
	// exactly from the exact target X -- a line normal's foot on its line, a
	// directional's crossing of its line carrier -- when both its coordinates are
	// floats exactly: exact data like a data point. nullopt for a circle carrier
	// (a root) or a point between floats. (Oracle: trace2._exact_float_hit.)
	std::optional<std::pair<CN<N>, exact::Point3>> exact_float_hit(const Surf<N> &surf, EffectType etype, bool left, const exact::Point3 &X,
			const CN<N> &H) const {
		using exact::add;
		using exact::mul;
		using exact::of;
		using exact::sub;
		if (etype == EffectType::CircleNormalProjection) {
			// Stage 133 (F21): a circle normal's H (a root) when the numeric H rounded
			// to floats is it exactly: on the circle and collinear with X and the
			// centre, both exactly
			if (surf.line) {
				return std::nullopt;
			}
			const double fx = H.fx();
			const double fy = H.fy();
			if (!std::isfinite(fx) || !std::isfinite(fy)) {
				return std::nullopt;
			}
			const auto &ec = surf.exact_circle();
			const BigFloat ux = sub(mul(of(fx), ec.C[2]), ec.C[0]);
			const BigFloat uy = sub(mul(of(fy), ec.C[2]), ec.C[1]);
			if (!sub(add(mul(ux, ux), mul(uy, uy)), ec.rw2).is_zero()) {
				return std::nullopt;
			}
			const exact::Point3 &C = ec.C;
			const BigFloat t1 = mul(X[0], sub(C[1], mul(C[2], of(fy))));
			const BigFloat t2 = mul(X[1], sub(C[0], mul(C[2], of(fx))));
			const BigFloat t3 = mul(X[2], sub(mul(C[0], of(fy)), mul(C[1], of(fx))));
			if (!sub(add(t1, t3), t2).is_zero()) {
				return std::nullopt;
			}
			return std::make_pair(CN<N>{H.re.like(fx), H.re.like(fy)}, exact::Point3{of(fx), of(fy), of(1.0)});
		}
		if (!surf.line) {
			return std::nullopt;
		}
		const BigFloat sx = of(surf.sx), sy = of(surf.sy);
		const BigFloat dx = sub(of(surf.ex), sx), dy = sub(of(surf.ey), sy);
		BigFloat hx, hy, hw; // the hit point, homogeneous
		if (etype == EffectType::LineNormalProjection) {
			// S + t d, t = ((X - S w) . d) / (w |d|^2)
			const BigFloat dd = add(mul(dx, dx), mul(dy, dy));
			const BigFloat num = add(mul(sub(X[0], mul(sx, X[2])), dx), mul(sub(X[1], mul(sy, X[2])), dy));
			hw = mul(X[2], dd);
			hx = add(mul(sx, hw), mul(num, dx));
			hy = add(mul(sy, hw), mul(num, dy));
		} else {
			// X + s n on the line: s w = cross(S w - X, d) / cross(n, d)
			const Projective &pr = surf.proj(left);
			const BigFloat nx = of(pr.nx), ny = of(pr.ny);
			const BigFloat den = sub(mul(nx, dy), mul(ny, dx));
			if (den.is_zero()) {
				return std::nullopt;
			}
			const BigFloat num = sub(mul(sub(mul(sx, X[2]), X[0]), dy), mul(sub(mul(sy, X[2]), X[1]), dx));
			hw = mul(X[2], den);
			hx = add(mul(X[0], den), mul(num, nx));
			hy = add(mul(X[1], den), mul(num, ny));
		}
		if (hw.is_zero()) {
			return std::nullopt;
		}
		const double fx = hx.div(hw).to_float(); // (the truncated quotient: exact when the point is floats)
		const double fy = hy.div(hw).to_float();
		if (!std::isfinite(fx) || !std::isfinite(fy) || !sub(mul(of(fx), hw), hx).is_zero() || !sub(mul(of(fy), hw), hy).is_zero()) {
			return std::nullopt; // (between floats: the quotient's rounding is not the point)
		}
		return std::make_pair(CN<N>{H.re.like(fx), H.re.like(fy)}, exact::Point3{of(fx), of(fy), of(1.0)});
	}

	// The backward pass (GAME_SPEC 13.3): the sub-chains' targets, and alongside
	// the cursor's exact image while every entry's inverse map is rational -- a
	// projective's when its hit point is a data point of its segment (Stage
	// 113, K11): H is then that point exactly, and the image behind it stays
	// exact data. (Oracle: trace2._backward.)
	struct Backward {
		std::map<size_t, CN<N>> hits;       // projective entry index -> H
		std::map<size_t, CN<N>> exact_hits; // -> H when exact data (K11)
		std::optional<exact::Point3> image; // the exact aim image (nullopt: not exact data)
		bool at_infinity = false;           // a transformative image at infinity: no aim
	};
	Backward backward_pass(const std::vector<PlanItem> &plan, const std::vector<EffectType> &effs, const CN<N> &cursor, int max_limbs,
			size_t from_index = 0) const {
		Backward out;
		std::optional<CN<N>> X = cursor;
		std::optional<exact::Point3> Xe = exact::Point3{exact::of(cursor.fx()), exact::of(cursor.fy()), exact::of(1.0)};
		int on_sid = -2; // the surface whose carrier X lies on (provenance)
		for (size_t jj = plan.size(); jj-- > from_index;) {
			const Surf<N> *surf = scene_.find(plan[jj].surface_id);
			if (surf == nullptr) {
				continue;
			}
			if (is_projective(effs[jj])) {
				const bool on = on_sid == surf->id || (on_sid == -2 && on_carrier(*surf, *X));
				const effects::F32Point *hint = plan[jj].hint ? &*plan[jj].hint : nullptr;
				const auto cands = effects::back_propagate_candidates(*surf, plan[jj].left, *X, on);
				if (cands.empty()) {
					Xe.reset();
					continue;
				}
				// Stage 114: a candidate that is a data point of the segment is that
				// point exactly (K11) before the fork rule's segment test
				std::vector<CN<N>> pool;
				std::vector<std::optional<std::pair<CN<N>, exact::Point3>>> snaps;
				for (const effects::Candidate<N> &k : cands) {
					std::optional<std::pair<CN<N>, exact::Point3>> d;
					if (Xe) {
						d = exact_data_hit(*surf, effs[jj], plan[jj].left, *Xe, k.h);
					}
					pool.push_back(d ? d->first : k.h);
					snaps.push_back(d);
				}
				const CN<N> Hc = effects::choose_candidate(*surf, pool, hint);
				size_t pick = 0;
				while (pick + 1 < pool.size() && !pool[pick].sub(Hc).is_zero()) {
					pick++;
				}
				{
					static const bool trace = std::getenv("RC_REALIZE_TRACE") != nullptr;
					if (trace) {
						std::fprintf(stderr, "backward: entry %zu surf %d on %d cands %zu pick %zu snapped %d Xe %d same %d limbs %d\n", jj, surf->id, int(on), cands.size(), pick,
							int(snaps[pick].has_value()), int(Xe.has_value()), int(Hc.sub(*X).is_zero()), max_limbs);
					}
				}
				if (Xe) {
					if (snaps[pick]) {
						out.exact_hits.emplace(jj, snaps[pick]->first);
						Xe = snaps[pick]->second;
					} else if (on && Hc.sub(*X).is_zero()) {
						// the target itself, exactly (provenance): an exact hit point, the
						// exact image through it unchanged (Stage 114, K12 for circles)
						out.exact_hits.emplace(jj, Hc);
					} else {
						Xe.reset();
					}
				}
				X = Hc;
				out.hits.emplace(jj, Hc);
				on_sid = surf->id;
			} else if (is_transformative(effs[jj])) {
				on_sid = -1;
				X = effects::map_point(X, *surf, true);
				if (Xe) {
					Xe = exact_step_h(*Xe, *surf, max_limbs);
				}
				if (!X) {
					out.at_infinity = true;
					out.image.reset();
					return out;
				}
			}
		}
		out.image = Xe;
		return out;
	}

	// The aim image F_K^-1(cursor) as an exact homogeneous point, when every
	// frame change of the plan is a rational map of exact data (Stage 113: a
	// projective's hit point that is a data point counts); nullopt for a
	// rotation portal, an image at infinity, a back-propagated hit point that
	// is not exact data, or beyond max_limbs (0 = no budget).
	std::optional<exact::Point3> exact_image_h(const CN<N> &cursor, const std::vector<PlanItem> &plan, int max_limbs,
			const Flags &flags, size_t from_index = 0) const {
		return backward_pass(plan, plan_effects(plan, flags), cursor, max_limbs, from_index).image;
	}

	// The aim image as float64 homogeneous coordinates with rigorous bounds,
	// along the same maps as exact_image_h; each step rescales by a power of two.
	std::optional<std::array<exact::EB, 3>> exact_image_approx(const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags) const {
		using exact::EB;
		std::array<EB, 3> X = {EB{cursor.fx(), 0.0}, EB{cursor.fy(), 0.0}, EB{1.0, 0.0}};
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		for (EffectType e : effs) {
			if (is_projective(e)) {
				return std::nullopt; // a back-propagated hit point is not exact data
			}
		}
		for (size_t jj = plan.size(); jj-- > 0;) {
			const Surf<N> *surf = scene_.find(plan[jj].surface_id);
			if (surf == nullptr || !is_transformative(effs[jj])) {
				continue;
			}
			if (surf->portal) {
				if (surf->theta != 0.0) {
					return std::nullopt;
				}
				X = {exact::esub(X[0], exact::emul(EB{surf->dx, 0.0}, X[2])), exact::esub(X[1], exact::emul(EB{surf->dy, 0.0}, X[2])), X[2]};
				continue;
			}
			if (surf->line) {
				X = reflect_approx(X, surf->sx, surf->sy, surf->ex, surf->ey);
			} else {
				const auto &ec = surf->exact_circle();
				const std::array<EB, 3> C = {approx(ec.C[0]), approx(ec.C[1]), approx(ec.C[2])};
				const EB rw2 = approx(ec.rw2);
				const EB w = X[2];
				const EB cw = C[2];
				const EB dx = exact::esub(exact::emul(X[0], cw), exact::emul(C[0], w));
				const EB dy = exact::esub(exact::emul(X[1], cw), exact::emul(C[1], w));
				const EB dd = exact::eadd(exact::emul(dx, dx), exact::emul(dy, dy));
				const EB rw = exact::emul(rw2, w);
				X = {exact::eadd(exact::emul(C[0], dd), exact::emul(rw, dx)), exact::eadd(exact::emul(C[1], dd), exact::emul(rw, dy)),
					exact::emul(cw, dd)};
			}
			// rescale by a power of two near 1 / |w| (exact on values and bounds)
			const double m = std::fabs(X[2].v);
			if (m == 0.0 || !std::isfinite(m)) {
				return std::nullopt;
			}
			const double k = std::pow(2.0, -std::floor(std::log(m) / std::log(2.0)));
			X = {EB{X[0].v * k, X[0].e * k}, EB{X[1].v * k, X[1].e * k}, EB{X[2].v * k, X[2].e * k}};
		}
		return X;
	}

	const Scene<N> &scene() const { return scene_; }

	// The anchored chain of a plan (public for the differential tests).
	std::shared_ptr<const AnchorChain<N>> anchor_chain(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags) {
		return build_chain(player, cursor, plan, flags);
	}

private:
	bool record_poles_ = false;
	const Scene<N> &scene_;
	SolveCache<N> *cache_;

	static exact::EB approx(const BigFloat &v) {
		const double f = v.to_float();
		return exact::EB{f, 2.0 * exact::U * std::fabs(f)};
	}

	static std::array<exact::EB, 3> reflect_approx(const std::array<exact::EB, 3> &X, double sx, double sy, double ex, double ey) {
		using exact::EB;
		const EB mx = exact::esub(EB{ex, 0.0}, EB{sx, 0.0});
		const EB my = exact::esub(EB{ey, 0.0}, EB{sy, 0.0});
		const EB mm = exact::eadd(exact::emul(mx, mx), exact::emul(my, my));
		const EB m2r = exact::esub(exact::emul(mx, mx), exact::emul(my, my));
		const EB m2i = exact::eadd(exact::emul(mx, my), exact::emul(my, mx));
		const EB w = X[2];
		const EB cx = exact::esub(X[0], exact::emul(EB{sx, 0.0}, w));
		const EB cy = exact::esub(exact::emul(EB{sy, 0.0}, w), X[1]);
		const EB swm = exact::emul(w, mm);
		const EB rx = exact::eadd(exact::emul(EB{sx, 0.0}, swm), exact::esub(exact::emul(m2r, cx), exact::emul(m2i, cy)));
		const EB ry = exact::eadd(exact::emul(EB{sy, 0.0}, swm), exact::eadd(exact::emul(m2r, cy), exact::emul(m2i, cx)));
		return {rx, ry, swm};
	}

	// The exact aim image within the budget (nullopt beyond it), cached per
	// (cursor, plan, plan surfaces' data). The cursor is keyed by its exact bits
	// (hex): a decimal key once merged cursors closer than 5e-7 px (Perf P1).
	std::optional<exact::Point3> exact_image_cached(const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags) const {
		if (cache_ == nullptr) {
			return exact_image_h(cursor, plan, EXACT_IMAGE_LIMBS, flags);
		}
		ExactCache::ImageKey key;
		auto put = [&key](double v) { key.words.push_back(std::bit_cast<uint64_t>(v)); };
		put(cursor.fx());
		put(cursor.fy());
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		for (size_t ji = 0; ji < plan.size(); ji++) {
			const PlanItem &item = plan[ji];
			const Surf<N> *s = scene_.find(item.surface_id);
			key.words.push_back(uint64_t(uint32_t(item.surface_id)) | uint64_t(item.left) << 32 | uint64_t(s != nullptr) << 33);
			if (s != nullptr) {
				key.words.push_back(uint64_t(int64_t(effs[ji])));
				for (double v : {s->sx, s->sy, s->ex, s->ey, s->has_v ? s->vx : 0.0, s->has_v ? s->vy : 0.0, s->cx, s->cy, s->r, s->dx, s->dy, s->theta}) {
					put(v);
				}
			}
		}
		auto it = cache_->exact->images.find(key);
		if (it != cache_->exact->images.end()) {
			return it->second;
		}
		if (cache_->exact->images.size() > 256) {
			cache_->exact->images.clear();
		}
		std::optional<exact::Point3> X = exact_image_h(cursor, plan, EXACT_IMAGE_LIMBS, flags);
		cache_->exact->images[std::move(key)] = X;
		return X;
	}

	// (the same exact value, held alike: sign, exponent and limbs)
	static bool same_bits(const BigFloat &a, const BigFloat &b) {
		return a.sign == b.sign && a.exp == b.exp && a.limbs.size() == b.limbs.size() && std::equal(a.limbs.begin(), a.limbs.end(), b.limbs.begin());
	}
	static bool same_bits(const exact::Point3 &a, const exact::Point3 &b) {
		return same_bits(a[0], b[0]) && same_bits(a[1], b[1]) && same_bits(a[2], b[2]);
	}
	// An aimed shot's exact start in a pre-transform frame (SolveCache::Start)
	ExactCache::Start *start_entry(double px, double py, const exact::Point3 &X0, const exact::Point3 &Q0) const {
		if (cache_ == nullptr) {
			return nullptr;
		}
		const uint64_t bx = std::bit_cast<uint64_t>(px);
		const uint64_t by = std::bit_cast<uint64_t>(py);
		for (auto &e : cache_->exact->starts) {
			if (e.px == bx && e.py == by && same_bits(e.X0, X0) && same_bits(e.Q0, Q0)) {
				return &e;
			}
		}
		if (cache_->exact->starts.size() >= 8) {
			cache_->exact->starts.erase(cache_->exact->starts.begin());
		}
		ExactCache::Start e;
		e.px = bx;
		e.py = by;
		e.X0 = X0;
		e.Q0 = Q0;
		cache_->exact->starts.push_back(std::move(e));
		return &cache_->exact->starts.back();
	}
	std::optional<exact::ExactPole> start_pole(double px, double py, const exact::Point3 &X0, const exact::Point3 &Q0) const {
		ExactCache::Start *e = start_entry(px, py, X0, Q0);
		if (e == nullptr) {
			return exact::pole_start_aimed(px, py, X0, Q0);
		}
		if (!e->has_pole) {
			e->pole = exact::pole_start_aimed(px, py, X0, Q0);
			e->has_pole = true;
		}
		return e->pole;
	}
	// A.3: the start's pole record (its exact pole from start_pole, memoized, when a filter asks)
	std::shared_ptr<const exact::PoleRec> start_pole_rec(double px, double py, const exact::Point3 &X0, const exact::Point3 &Q0) const {
		return exact::PoleRec::start_aimed(px, py, X0, Q0, [this, px, py, X0, Q0] { return start_pole(px, py, X0, Q0); });
	}
	// A.4: the start's exact circle as a lazy record (start_gc, memoized, when a decision asks)
	std::shared_ptr<const exact::BeamRec> start_rec(double px, double py, const exact::Point3 &X0, const exact::Point3 &Q0) const {
		return exact::BeamRec::from_gc_lazy([this, px, py, X0, Q0] { return start_gc(px, py, X0, Q0); });
	}
	exact::GCircle start_gc(double px, double py, const exact::Point3 &X0, const exact::Point3 &Q0) const {
		auto through = [&] { return exact::gc_through(exact::Point3{exact::of(px), exact::of(py), exact::of(1.0)}, X0, Q0); };
		ExactCache::Start *e = start_entry(px, py, X0, Q0);
		if (e == nullptr) {
			return through();
		}
		if (!e->has_gc) {
			e->gc = through();
			e->has_gc = true;
		}
		return e->gc;
	}

	// The exact implicit line of B_0, when it is exact data: lazy float64
	// coefficients with rigorous bounds; the exact homogeneous image only when a
	// predicate needs it.
	std::shared_ptr<const exact::Line> initial_line(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags) const {
		using exact::EB;
		if (!scene_.pre_surfaces.empty()) {
			return nullptr; // a pre-transform frame: the initial beam is a circle
		}
		std::optional<std::array<EB, 3>> X = exact_image_approx(cursor, plan, flags);
		const double px = player.fx();
		const double py = player.fy();
		if (!X) {
			// Stage 113 (K11): through a projective whose hit point is a data point
			// of its segment the image is exact data (never approximated): the
			// line through it, or none
			for (EffectType e : plan_effects(plan, flags)) {
				if (is_projective(e)) {
					if (const std::optional<exact::Point3> Xh = exact_image_cached(cursor, plan, flags)) {
						return exact::Line::from_exact(exact::line_through_h(px, py, *Xh));
					}
					break;
				}
			}
			return nullptr;
		}
		// a = -(y - py w), b = x - px w, c = a px + b py
		const EB a = exact::esub(exact::emul(EB{py, 0.0}, (*X)[2]), (*X)[1]);
		const EB b = exact::esub((*X)[0], exact::emul(EB{px, 0.0}, (*X)[2]));
		const EB c = exact::eadd(exact::emul(a, EB{px, 0.0}), exact::emul(b, EB{py, 0.0}));
		const Solver *self = this;
		return exact::Line::from_approx(a, b, c, [self, cursor, plan, flags, px, py]() -> std::optional<exact::Line3> {
			std::optional<exact::Point3> Xh = self->exact_image_cached(cursor, plan, flags);
			if (!Xh) {
				return std::nullopt;
			}
			return exact::line_through_h(px, py, *Xh);
		});
	}

	// ---- the anchored chain

	std::shared_ptr<const AnchorChain<N>> select_chain(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags) {
		if (cache_ == nullptr) {
			return build_chain(player, cursor, plan, flags);
		}
		typename SolveCache<N>::ChainKey key;
		key.scene = &scene_;
		key.px = player.fx();
		key.py = player.fy();
		key.cx = cursor.fx();
		key.cy = cursor.fy();
		key.flags = flags.canonical_key();
		for (const PlanItem &p : plan) {
			key.plan.emplace_back(p.surface_id, p.left, p.hint.has_value(), p.hint ? p.hint->x : 0.0f, p.hint ? p.hint->y : 0.0f);
		}
		key.force = force_reversed ? (*force_reversed ? 2 : 1) : 0;
		for (const auto &e : cache_->chains) {
			if (e.key == key) {
				return e.chain;
			}
		}
		if (cache_->chains.size() >= 4) {
			cache_->chains.erase(cache_->chains.begin());
		}
		cache_->chains.push_back({std::move(key), build_chain(player, cursor, plan, flags)});
		return cache_->chains.back().chain;
	}

	int pop_id_of(int sid) const {
		const Surf<N> *s = scene_.find(sid);
		return (s != nullptr && s->portal) ? s->pair : sid;
	}

	static std::string stack_key(const std::vector<int> &st) {
		std::string k;
		for (size_t i = 0; i < st.size(); i++) {
			if (i) {
				k += ",";
			}
			k += std::to_string(st[i]);
		}
		return k;
	}

	// The same circles run the other way round (path order is preserved by
	// every effect, so one orientation flip reverses a whole epoch); a frame
	// popped back to an earlier beam stays the same object.
	static void reverse_beams(std::vector<BeamPtr<N>> &beams, size_t from, size_t to) {
		std::map<const Beam<N> *, BeamPtr<N>> mapped;
		for (size_t i = from; i < to && i < beams.size(); i++) {
			BeamPtr<N> &bm = beams[i];
			auto it = mapped.find(bm.get());
			if (it == mapped.end()) {
				it = mapped.emplace(bm.get(), std::make_shared<const Beam<N>>(Beam<N>{bm->p, bm->tau.neg(), bm->kappa.neg()})).first;
			}
			bm = it->second;
		}
	}

	// Projective entries split the plan into sub-chains (GAME_SPEC 13.3/13.4).
	std::shared_ptr<const AnchorChain<N>> build_chain(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags) {
		std::shared_ptr<const AnchorChain<N>> chain = build_chain_direct(player, cursor, plan, flags);
		if (!chain) {
			return chain;
		}
		// Stage 111: the plan's realization (or the orientation the caller forces)
		const Realization r = force_reversed ? Realization{*force_reversed, RealizationKind::Entry}
											: realize(player, cursor, plan, flags, *chain->beams[0], chain);
		auto out = std::make_shared<AnchorChain<N>>(*chain);
		out->reversed = r.reversed;
		out->kind = r.kind;
		if (r.reversed) {
			// the second realization: the first epoch's beams reversed (every beam
			// up to and including the one after the last pre-projective entry)
			reverse_beams(out->beams, 0, first_epoch_end(plan, flags));
		}
		return out;
	}

	std::shared_ptr<const AnchorChain<N>> build_chain_direct(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags) const {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		std::vector<size_t> proj;
		proj.reserve(plan.size());
		for (size_t j = 0; j < plan.size(); j++) {
			if (is_projective(effs[j])) {
				proj.push_back(j);
			}
		}
		if (proj.empty()) {
			return build_simple(player, cursor, plan, effs, effects::scene_pre_q(scene_));
		}
		// backward: the targets of the sub-chains (a fork settled by the click)
		const Backward bw = backward_pass(plan, effs, cursor, EXACT_IMAGE_LIMBS);
		if (bw.at_infinity) {
			return nullptr;
		}
		for (size_t j : proj) {
			if (!bw.hits.count(j)) {
				return nullptr; // an entry without a hit point: the greedy aim
			}
		}
		return from_hits(player, cursor, plan, flags, effs, bw);
	}

	// Exact: the point k lies on surf's carrier.
	static bool on_carrier(const Surf<N> &surf, const CN<N> &k) {
		if (geom::endpoint_at(surf.seg, k) != 0) {
			return true;
		}
		if (surf.line) {
			return exact::orient_zero(surf.sx, surf.sy, surf.ex, surf.ey, k.fx(), k.fy());
		}
		if (surf.full) {
			return exact::on_circle_zero(surf.cx, surf.cy, surf.r, k.fx(), k.fy());
		}
		return exact::incircle_zero(surf.sx, surf.sy, surf.vx, surf.vy, surf.ex, surf.ey, k.fx(), k.fy());
	}

	// The backward pass: {projective entry index: H}, or nullopt when an image is at infinity.
	std::optional<std::map<size_t, CN<N>>> backward_hits(const std::vector<PlanItem> &plan, const std::vector<EffectType> &effs,
			const CN<N> &cursor) const {
		const Backward b = backward_pass(plan, effs, cursor, EXACT_IMAGE_LIMBS);
		if (b.at_infinity) {
			return std::nullopt;
		}
		return b.hits;
	}

	// The chain of sub-chains through the given projective hit points.
	std::shared_ptr<const AnchorChain<N>> from_hits(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const Flags &flags, const std::vector<EffectType> &effs, const Backward &bw) const {
		const std::map<size_t, CN<N>> &H = bw.hits;
		std::vector<size_t> proj;
		for (size_t j = 0; j < plan.size(); j++) {
			if (is_projective(effs[j])) {
				proj.push_back(j);
			}
		}
		auto chain = std::make_shared<AnchorChain<N>>();
		for (const auto &kv : bw.exact_hits) {
			chain->exact_hits.emplace(int(kv.first), kv.second);
		}
		size_t a = 0;
		CN<N> origin = player;
		std::optional<CN<N>> exit_dir; // the exit at origin of the previous projective
		const Surf<N> *through_c = nullptr; // K15: the circle normal the current epoch leaves
		std::vector<size_t> bounds = proj;
		bounds.push_back(plan.size());
		for (size_t b : bounds) {
			const CN<N> target = b < plan.size() ? H.at(b) : cursor;
			std::vector<PlanItem> sub_plan(plan.begin() + long(a), plan.begin() + long(b));
			std::vector<EffectType> sub_effs(effs.begin() + long(a), effs.begin() + long(b));
			std::vector<BeamPtr<N>> beams;
			if (exit_dir && sub_plan.empty() && target.sub(origin).is_zero()) {
				// Stage 113 (K12): the cursor exactly at the projective's departure
				// point (its own hit point): the sub-chain is the exit beam alone,
				// and the cursor on it is reached at once
				beams = {std::make_shared<const Beam<N>>(Beam<N>{origin, exit_dir->unit(), origin.re.like(0.0)})};
			} else {
				// the first epoch starts in the pre-transform's frame; after a projective the beam is straight
				std::shared_ptr<const AnchorChain<N>> sub = build_simple(origin, target, sub_plan, sub_effs,
					exit_dir ? std::nullopt : effects::scene_pre_q(scene_), through_c);
				if (!sub) {
					return nullptr;
				}
				beams = sub->beams;
			}
			if (exit_dir && !(sub_plan.empty() && target.sub(origin).is_zero())) {
				// after a projective the beam leaves along its exit; when that
				// points away from the target, the target is reached through
				// infinity: the sub-chain's beams run the other way round
				const BeamPtr<N> b0 = beams[0];
				const bool rev = CN<N>::dot(*exit_dir, b0->tau).is_negative();
				// the exit beam itself is exactly straight; a frame popped back to it is the same object
				const BeamPtr<N> straight = std::make_shared<const Beam<N>>(Beam<N>{origin, exit_dir->unit(), b0->kappa.sub(b0->kappa)});
				std::map<const Beam<N> *, BeamPtr<N>> mapped;
				for (BeamPtr<N> &bm : beams) {
					if (bm == b0) {
						bm = straight;
					} else if (rev) {
						auto it = mapped.find(bm.get());
						if (it == mapped.end()) {
							it = mapped.emplace(bm.get(), std::make_shared<const Beam<N>>(Beam<N>{bm->p, bm->tau.neg(), bm->kappa.neg()})).first;
						}
						bm = it->second;
					}
				}
			}
			chain->beams.insert(chain->beams.end(), beams.begin(), beams.end());
			if (b < plan.size()) {
				chain->hits.emplace(int(b), H.at(b));
				// the exit line is exact when the target behind the projective is
				// exact data (Stage 113: through later projectives at data points too)
				if (std::shared_ptr<const exact::Line> L = exit_line(cursor, plan, flags, b)) {
					chain->exit_lines.emplace(int(b), L);
				}
				origin = H.at(b);
				const Surf<N> *ps = scene_.find(plan[b].surface_id);
				exit_dir = effects::projective_exit(*ps, plan[b].left, H.at(b));
				through_c = effs[b] == EffectType::CircleNormalProjection ? ps : nullptr; // K15
			}
			a = b + 1;
		}
		return chain;
	}

	// through_c (K15, Stage 114): the circle normal whose centre the epoch's beam
	// passes by provenance (its exit); an inversion centred there keeps the line.
	std::shared_ptr<const AnchorChain<N>> build_simple(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan,
			const std::vector<EffectType> &effs, const std::optional<CN<N>> &base_q, const Surf<N> *through_c = nullptr) const {
		const size_t K = plan.size();
		// (F22, Stage 135: the frames as a trie -- each its parent and one surface,
		// identified by its stack -- in one array: no strings, map nodes or a
		// vector per stack per call; the same frames, met in the same order)
		struct Frame {
			int parent = -1;
			int sid = 0;
			int depth = 0;
			bool on_line = false;
			std::optional<effects::Poles<N>> poles;
			std::optional<std::optional<CN<N>>> seen;
			BeamPtr<N> beam;
		};
		std::vector<Frame> fr;
		fr.reserve(K + 1);
		fr.emplace_back();
		struct Step {
			int frame = 0;
			bool changes = false; // plan entry j changes the frame
		};
		std::vector<Step> at(K + 1); // at[j]: the frame before entry j (at[K]: after the plan)
		int cur = 0;
		for (size_t ji = 0; ji < K; ji++) {
			const Surf<N> *surf = scene_.find(plan[ji].surface_id);
			const bool t = surf != nullptr && is_transformative(effs[ji]);
			at[ji].changes = t;
			if (t) {
				if (fr[size_t(cur)].depth > 0 && pop_id_of(fr[size_t(cur)].sid) == plan[ji].surface_id) {
					cur = fr[size_t(cur)].parent;
				} else {
					int child = -1;
					for (size_t f = 0; f < fr.size() && child < 0; f++) {
						if (fr[f].parent == cur && fr[f].sid == plan[ji].surface_id) {
							child = int(f);
						}
					}
					if (child < 0) {
						Frame nf;
						nf.parent = cur;
						nf.sid = plan[ji].surface_id;
						nf.depth = fr[size_t(cur)].depth + 1;
						fr.push_back(std::move(nf));
						child = int(fr.size()) - 1;
					}
					cur = child;
				}
			}
			at[ji + 1].frame = cur;
		}
		// K15: the frames whose beam is the exit line itself (an inversion centred
		// on the passed centre, then line-keeping effects): Q is on the line, the
		// beam is straight
		for (size_t f = 1; f < fr.size(); f++) {
			const Surf<N> *ps = scene_.find(fr[f].sid);
			if (ps != nullptr && through_c != nullptr && fr[f].depth == 1 && !ps->line && !ps->portal && same_exact_center(*through_c, *ps)) {
				fr[f].on_line = true;
			} else if (ps != nullptr && fr[size_t(fr[f].parent)].on_line && (ps->line || ps->portal)) {
				fr[f].on_line = true;
			}
		}
		// pole chain: per frame [P, Q or null, delta or null]
		// the epoch's base poles: the player and the frame's image of infinity (Stage 101)
		fr[0].poles = effects::Poles<N>{player, base_q, base_q ? std::optional<CN<N>>(player.sub(*base_q)) : std::nullopt};
		for (size_t f = 1; f < fr.size(); f++) {
			const effects::Poles<N> &parent = *fr[size_t(fr[f].parent)].poles;
			if (!parent.P) {
				return nullptr;
			}
			fr[f].poles = effects::map_pole(parent, *scene_.find(fr[f].sid));
		}
		// image chain X_j, memoized by frame (exact provenance across pops)
		std::vector<std::optional<CN<N>>> X(K + 1);
		for (size_t jj = K + 1; jj-- > 0;) {
			Frame &f = fr[size_t(at[jj].frame)];
			if (f.seen) {
				X[jj] = *f.seen;
			} else if (jj == K) {
				X[jj] = cursor;
			} else if (!at[jj].changes) {
				X[jj] = X[jj + 1];
			} else {
				if (!X[jj + 1]) {
					return nullptr;
				}
				X[jj] = effects::map_point(X[jj + 1], *scene_.find(plan[jj].surface_id), true);
			}
			f.seen = X[jj];
		}
		auto chain = std::make_shared<AnchorChain<N>>();
		chain->beams.reserve(K + 1);
		for (size_t j = 0; j <= K; j++) {
			Frame &f = fr[size_t(at[j].frame)];
			if (!f.beam) {
				const effects::Poles<N> &pc = *f.poles;
				if (!pc.P || !X[j] || X[j]->sub(*pc.P).is_zero()) {
					return nullptr;
				}
				Beam<N> bm = Beam<N>::through(*pc.P, *X[j], pc.delta && !f.on_line ? &*pc.delta : nullptr);
				if (f.on_line && pc.Q) {
					// K15: the straight beam's orientation keeps the circle's order P -> X -> Q:
					// when Q lies between P and X on the line, X is reached the other way
					// round, through the real infinity
					const CN<N> d = X[j]->sub(*pc.P);
					const N tx = d.abs2();
					const N tq = CN<N>::dot(pc.Q->sub(*pc.P), d);
					if (!tq.is_negative() && !tq.is_zero() && tq.cmp(tx) < 0) {
						bm = Beam<N>{*pc.P, bm.tau.neg(), bm.kappa};
					}
				}
				f.beam = std::make_shared<const Beam<N>>(bm);
			}
			chain->beams.push_back(f.beam);
		}
		return chain;
	}

	// The exact line the last projective entry j of a plan leaves along.
	std::shared_ptr<const exact::Line> exit_line(const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags, size_t j) const {
		std::optional<exact::Point3> X = exact_image_h(cursor, plan, EXACT_IMAGE_LIMBS, flags, j + 1);
		if (!X) {
			return nullptr;
		}
		const Surf<N> &surf = *scene_.find(plan[j].surface_id);
		const EffectType etype = surf.effect(plan[j].left);
		exact::Line3 L;
		if (etype == EffectType::CircleNormalProjection) {
			// the line through the center C and X (homogeneous cross product)
			const exact::Point3 &C = surf.exact_circle().C;
			const BigFloat a = exact::sub(exact::mul(C[1], (*X)[2]), exact::mul((*X)[1], C[2]));
			const BigFloat b = exact::sub(exact::mul((*X)[0], C[2]), exact::mul(C[0], (*X)[2]));
			const BigFloat c = exact::sub(exact::mul((*X)[0], C[1]), exact::mul(C[0], (*X)[1]));
			if (a.is_zero() && b.is_zero()) {
				return nullptr;
			}
			L = {a, b, c};
		} else {
			double ax;
			double ay;
			if (etype == EffectType::LineNormalProjection) {
				ax = -(surf.ey - surf.sy);
				ay = surf.ex - surf.sx;
			} else {
				const Projective &pr = surf.proj(plan[j].left);
				ax = pr.nx;
				ay = pr.ny;
			}
			// through X = (x/w, y/w) along (ax, ay): -ay w x + ax w y = ax y - ay x
			const BigFloat &w = (*X)[2];
			L = {exact::mul(exact::of(-ay), w), exact::mul(exact::of(ax), w),
				exact::sub(exact::mul(exact::of(ax), (*X)[1]), exact::mul(exact::of(ay), (*X)[0]))};
		}
		return exact::Line::from_exact(L);
	}

	// ---- walks

	Walk<N> new_walk(const std::vector<PlanItem> &plan, const std::optional<CN<N>> &cursor, Mode mode, int max_events, const Flags &flags) const {
		Walk<N> w;
		w.flags = flags;
		w.scene = &scene_;
		w.plan = plan;
		w.cursor = cursor;
		w.mode = mode;
		w.post_mode = Mode::Physical;
		w.max_events = max_events;
		w.max_sig = max_events == MAX_HITS ? MAX_SIGNIFICANT : (max_events / MAX_HITS) * MAX_SIGNIFICANT; // (the realization's wide budget: 8 x each)
		w.spans = piece_spans(plan);
		w.Q = effects::scene_pre_q(scene_);
		w.record_poles = record_poles_;
		w.pole_memo = cache_ ? &cache_->exact->poles : nullptr;
		w.incidence_memo = cache_ ? &cache_->exact->incidence : nullptr;
		w.read_horizon = read_horizon;
		w.events.reserve(32); // (Stage 128: one allocation, not a growth per few events)
		w.gains.reserve(32);
		return w;
	}

	Walk<N> walk_chain(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan, Mode mode, int max_events,
			const Flags &flags, const std::shared_ptr<const AnchorChain<N>> &chain, bool stop_at_cursor = false) {
		Walk<N> w = new_walk(plan, cursor, mode, max_events, flags);
		w.stop_at_cursor = stop_at_cursor;
		w.chain = chain;
		w.anchored = true;
		BeamPtr<N> b0 = chain->beams[0];
		if (mode == Mode::Physical && plan.empty()) {
			// no plan: the physical trace is the forward continuation from the start
			b0 = Backend<N>::seed(b0);
			w.seeded = true;
			w.anchored = false;
		}
		w.known = {Known<N>{player, player.re.like(0.0), -1, nullptr}};
		w.line = initial_line(player, cursor, plan, flags);
		w.rec = exact::BeamRec::from_line(w.line);
		if (w.line) {
			// every frame change fixes the cursor: the aim image is the cursor itself
			if (!plan.empty() && fixes_cursor(cursor, plan, flags) && known_index(w.known, cursor) < 0) {
				w.known.push_back(Known<N>{cursor, Backend<N>::param_of(*b0, cursor), -1, nullptr});
			}
			add_line_endpoints(w, *b0);
		} else if (w.Q && !plan.empty() && fixes_cursor(cursor, plan, flags) && known_index(w.known, cursor) < 0) {
			w.known.push_back(Known<N>{cursor, Backend<N>::param_of(*b0, cursor), -1, nullptr});
		}
		if (const std::optional<exact::Point3> q0 = pre_q_exact()) {
			// an aimed shot in a pre-transform frame: the circle through P, X0, Q0
			if (const std::optional<exact::Point3> X0 = exact_image_cached(cursor, plan, flags)) {
				set_pole(w, start_pole_rec(player.fx(), player.fy(), *X0, *q0));
				if (!w.rec) {
					// K9: and its exact circle (A.4: computed when a decision asks)
					w.rec = start_rec(player.fx(), player.fy(), *X0, *q0);
				}
			}
		}
		add_aim_known(w, *b0);
		w.O = b0->p; // (Stage 131: the first ray's origin, the player)
		walk_from(w, b0, -1, std::nullopt);
		return w;
	}

	// A point at infinity in the anchored construction: forward walk from the
	// exact aim direction, or straight at the cursor when the aim image is at
	// infinity or is the player itself (a plan every direction solves: the
	// cursor's mirror image is the player, K3). The beam passes the aim point
	// (the image, else the cursor): in a pre-transform frame the circle through
	// the player, that point and Q0 (the oracle's beam_through), the straight
	// line toward it otherwise. nullopt only for player = cursor.
	// The degenerate aim's pulled-back point X (nullopt: at infinity), by the
	// same backward pass as the anchored chain's, projectives by back-propagation.
	std::optional<CN<N>> degenerate_image(const CN<N> &cursor, const std::vector<PlanItem> &plan, const Flags &flags) const {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		std::optional<CN<N>> X = cursor;
		int on_sid = -2;
		for (size_t jj = plan.size(); jj-- > 0;) {
			const Surf<N> *surf = scene_.find(plan[jj].surface_id);
			if (surf != nullptr && is_projective(effs[jj]) && X) {
				const bool on = on_sid == surf->id || (on_sid == -2 && on_carrier(*surf, *X));
				const effects::F32Point *hint = plan[jj].hint ? &*plan[jj].hint : nullptr;
				std::optional<CN<N>> Hh = effects::back_propagate(*surf, plan[jj].left, *X, on, hint);
				if (Hh) {
					X = Hh;
					on_sid = surf->id;
				}
				continue;
			}
			if (surf != nullptr && is_transformative(effs[jj])) {
				on_sid = -1;
			}
			if (surf == nullptr || !is_transformative(effs[jj])) {
				continue;
			}
			X = effects::map_point(X, *surf, true);
			if (!X) {
				break;
			}
		}
		return X;
	}

	// The degenerate aim's beam (P -> aim -> Q0) and exact line; nullopt for player = cursor.
	std::optional<std::pair<Beam<N>, std::shared_ptr<const exact::Line>>> degenerate_beam(const CN<N> &player, const CN<N> &cursor,
			const std::vector<PlanItem> &plan, const Flags &flags) const {
		const std::optional<CN<N>> X = degenerate_image(cursor, plan, flags);
		CN<N> aim;
		std::shared_ptr<const exact::Line> line;
		if (!X || X->sub(player).is_zero()) {
			aim = cursor;
			if (cursor.sub(player).is_zero()) {
				return std::nullopt;
			}
			line = exact::Line::from_exact(exact::line_through(player.fx(), player.fy(), cursor.fx(), cursor.fy()));
		} else {
			aim = *X;
			line = initial_line(player, cursor, plan, flags);
		}
		const std::optional<CN<N>> q0 = effects::scene_pre_q(scene_);
		const CN<N> delta = q0 ? player.sub(*q0) : player.sub(player);
		return std::make_pair(Beam<N>::through(player, aim, q0 ? &delta : nullptr), line);
	}

	std::optional<Walk<N>> run_degenerate(const CN<N> &player, const CN<N> &cursor, const std::vector<PlanItem> &plan, Mode mode,
			int max_events, const Flags &flags) {
		const std::vector<EffectType> effs = plan_effects(plan, flags);
		std::optional<CN<N>> X = cursor;
		int on_sid = -2;
		for (size_t jj = plan.size(); jj-- > 0;) {
			const Surf<N> *surf = scene_.find(plan[jj].surface_id);
			if (surf != nullptr && is_projective(effs[jj]) && X) {
				const bool on = on_sid == surf->id || (on_sid == -2 && on_carrier(*surf, *X));
				const effects::F32Point *hint = plan[jj].hint ? &*plan[jj].hint : nullptr;
				std::optional<CN<N>> Hh = effects::back_propagate(*surf, plan[jj].left, *X, on, hint);
				if (Hh) {
					X = Hh;
					on_sid = surf->id;
				}
				continue;
			}
			if (surf != nullptr && is_transformative(effs[jj])) {
				on_sid = -1;
			}
			if (surf == nullptr || !is_transformative(effs[jj])) {
				continue;
			}
			X = effects::map_point(X, *surf, true);
			if (!X) {
				break;
			}
		}
		CN<N> aim; // the point the beam passes
		std::shared_ptr<const exact::Line> line;
		if (!X || X->sub(player).is_zero()) {
			aim = cursor;
			if (cursor.sub(player).is_zero()) {
				return std::nullopt;
			}
			line = exact::Line::from_exact(exact::line_through(player.fx(), player.fy(), cursor.fx(), cursor.fy()));
		} else {
			aim = *X;
			line = initial_line(player, cursor, plan, flags);
		}
		Walk<N> w = new_walk(plan, cursor, mode, max_events, flags);
		const CN<N> delta = w.Q ? player.sub(*w.Q) : player.sub(player);
		Beam<N> b = Beam<N>::through(player, aim, w.Q ? &delta : nullptr);
		std::shared_ptr<const exact::BeamRec> start_circle; // (A.4: the start's exact circle, lazily)
		if (const std::optional<exact::Point3> q0 = pre_q_exact()) {
			// the exact pole record (Stage 105): the circle through the player, the aim
			// point and Q0; a shot straight at the cursor passes exact data
			std::optional<exact::Point3> X0;
			if (!X || X->sub(player).is_zero()) {
				X0 = exact::Point3{exact::of(cursor.fx()), exact::of(cursor.fy()), exact::of(1.0)};
			} else {
				X0 = exact_image_cached(cursor, plan, flags);
			}
			if (X0) {
				set_pole(w, start_pole_rec(player.fx(), player.fy(), *X0, *q0));
				start_circle = start_rec(player.fx(), player.fy(), *X0, *q0);
			}
		}
		if (force_reversed ? *force_reversed : realize(player, cursor, plan, flags, b, nullptr).reversed) {
			b = Beam<N>{b.p, b.tau.neg(), b.kappa.neg()}; // the second realization: the same circle the other way (Stages 88, 111)
		}
		BeamPtr<N> b0 = std::make_shared<const Beam<N>>(b);
		if (w.Q) {
			line = nullptr; // a circle beam has no exact line
		}
		if (mode == Mode::Physical) {
			b0 = Backend<N>::seed(b0);
			w.seeded = true;
		}
		w.known = {Known<N>{player, player.re.like(0.0), -1, nullptr}};
		w.line = line;
		w.rec = exact::BeamRec::from_line(w.line);
		if (!w.rec && start_circle) {
			w.rec = start_circle; // K9: a pre-transform start's exact circle
		}
		if (w.line) {
			if (X && !X->sub(player).is_zero() && !plan.empty() && fixes_cursor(cursor, plan, flags) && known_index(w.known, cursor) < 0) {
				w.known.push_back(Known<N>{cursor, Backend<N>::param_of(*b0, cursor), -1, nullptr});
			}
			add_line_endpoints(w, *b0);
		}
		add_aim_known(w, *b0);
		w.O = b0->p; // (Stage 131: the first ray's origin, the player)
		walk_from(w, b0, -1, std::nullopt);
		return w;
	}

	// Stage 123: the caller's known point (aim_known) of the beam after its plan
	// entries, when the beam passes it to rounding (the beam is the member's own,
	// rounded)
	void add_aim_known(Walk<N> &w, const Beam<N> &b) const {
		if (!aim_known || aim_known->j != w.plan_index) {
			return;
		}
		const CN<N> k{b.p.re.like(aim_known->x), b.p.re.like(aim_known->y)};
		if (known_index(w.known, k) >= 0) {
			return;
		}
		const N t = Backend<N>::param_of(b, k);
		double x, y;
		Backend<N>::f64_point(b, t, x, y);
		const double scale = gd_maxf(1.0, gd_maxf(std::fabs(aim_known->x), std::fabs(aim_known->y)));
		if (d64(x, y, aim_known->x, aim_known->y) <= 1e-6 * scale) {
			w.known.push_back(Known<N>{k, t, -1, nullptr});
		}
	}

	// Segment data points (endpoints, via) exactly on the current exact line are known points.
	static void add_line_endpoints(Walk<N> &w, const Beam<N> &b) {
		for (const auto &s : w.scene->surfaces) {
			if (s->seg.full) {
				continue;
			}
			const CN<N> *eps[3] = {&s->seg.s, &s->seg.e, s->seg.v ? &*s->seg.v : nullptr};
			for (const CN<N> *ep : eps) {
				if (ep == nullptr) {
					continue;
				}
				if (known_index(w.known, *ep) < 0 && w.line->contains(ep->fx(), ep->fy())) {
					w.known.push_back(Known<N>{*ep, Backend<N>::param_of(b, *ep), -1, nullptr});
				}
			}
		}
	}

	static int known_index(const std::vector<Known<N>> &known, const CN<N> &z) {
		for (size_t i = 0; i < known.size(); i++) {
			if (geom::eq(known[i].p, z)) {
				return int(i);
			}
		}
		return -1;
	}

	// The surfaces whose carrier contains the known point p, from the scene's lists
	// (computed once per point: Scene::incidence_lists).
	static std::shared_ptr<const std::vector<const Surf<N> *>> incident_list(Walk<N> &w, const CN<N> &p) {
		const typename Scene<N>::PointKey key{std::bit_cast<uint64_t>(p.fx()), std::bit_cast<uint64_t>(p.fy())};
		auto &lists = w.scene->incidence_lists;
		if (const auto it = lists.find(key); it != lists.end()) {
			return it->second;
		}
		auto inc = std::make_shared<std::vector<const Surf<N> *>>();
		for (const auto &s0 : w.scene->surfaces) {
			if (incident(w, p, *s0)) {
				inc->push_back(s0.get());
			}
		}
		if (lists.size() > (1u << 14)) {
			lists.clear(); // bounded: a long session's points
		}
		lists.emplace(key, inc);
		return inc;
	}

	// Exact: the known point k lies on surf's carrier (cached per walk).
	static bool incident(Walk<N> &w, const CN<N> &k, const Surf<N> &surf) {
		if (geom::endpoint_at(surf.seg, k) != 0) {
			return true;
		}
		const double kx = k.fx();
		const double ky = k.fy();
		const IncidenceMemo::Key mk{w.incidence_memo, std::bit_cast<uint64_t>(kx), std::bit_cast<uint64_t>(ky), surf.id}; // (A.1: one memo per defs, shared by the tiers: the memo is the token)
		if (w.incidence_memo) {
			if (const bool *hit = w.incidence_memo->find(mk)) {
				return *hit;
			}
		}
		const PointSurf key{kx, ky, surf.id};
		auto it = w.incidence.find(key);
		if (it != w.incidence.end()) {
			return it->second;
		}
		bool r;
		if (surf.line) {
			r = exact::orient_zero(surf.sx, surf.sy, surf.ex, surf.ey, kx, ky);
		} else if (surf.full) {
			r = exact::on_circle_zero(surf.cx, surf.cy, surf.r, kx, ky);
		} else {
			r = exact::incircle_zero(surf.sx, surf.sy, surf.vx, surf.vy, surf.ex, surf.ey, kx, ky);
		}
		if (w.incidence_memo) {
			w.incidence_memo->put(mk, r);
		} else {
			w.incidence[key] = r;
		}
		return r;
	}

	// Projective order along a beam starting at t0: true if a comes before b.
	static bool before(const N &a, const N &b, const N &t0) {
		const int ka = a.cmp(t0) > 0 ? 0 : 1;
		const int kb = b.cmp(t0) > 0 ? 0 : 1;
		if (ka != kb) {
			return ka < kb;
		}
		return a.cmp(b) < 0;
	}

	// ---- the exact pole (Stage 105)

	// Q0 of the pre-transform as exact data (nullopt: none, or infinity); a pure
	// function of the scene, computed once per scene (A.1; it was per walk)
	std::optional<exact::Point3> pre_q_exact() const {
		if (scene_.pre_q_exact) {
			return *scene_.pre_q_exact;
		}
		std::optional<exact::Point3> q;
		for (const auto &sp : scene_.pre_surfaces) {
			const auto &ec = sp->exact_circle();
			if (!q) {
				q = ec.C;
				continue;
			}
			q = exact::invert_point_h(*q, ec.C, ec.rw2);
			if (!q) {
				break;
			}
		}
		scene_.pre_q_exact = q;
		return q;
	}

	static void set_pole(Walk<N> &w, std::shared_ptr<const exact::PoleRec> p) {
		w.pole = std::move(p);
		w.pole_serial = ++w.pole_serials;
	}

	// The record after surf's map (w.Q, w.line, w.pole: before it). A.3: the maps
	// are recorded, not computed -- the exact pole only where a predicate's filter
	// cannot decide (exact::PoleRec); past the budget it makes no claim (there).
	static std::shared_ptr<const exact::PoleRec> pole_after(const Walk<N> &w, const Surf<N> &surf) {
		if (surf.portal) {
			if (!w.pole || surf.theta != 0.0) {
				return nullptr; // a rotation is not exact data
			}
			return w.pole->translated(surf.dx, surf.dy);
		}
		if (surf.line) {
			return w.pole ? w.pole->reflected(surf.sx, surf.sy, surf.ex, surf.ey) : nullptr;
		}
		const auto &ec = surf.exact_circle();
		if (!w.Q) {
			// a straight beam (Q at infinity) inverted: Q becomes the center
			if (!w.line) {
				return nullptr;
			}
			return exact::PoleRec::from_line(w.line, &ec.C);
		}
		if (!w.pole) {
			return nullptr;
		}
		// Q's image under the inversion, memoized by Q (PoleMemo) when the solve has one
		PoleMemo *memo = w.pole_memo;
		const Surf<N> *sp = &surf;
		return w.pole->inverted(&ec.C, &ec.rw2, [memo, sp](const exact::Point3 &Q) -> std::optional<exact::Point3> {
			const auto &c = sp->exact_circle();
			if (memo) {
				PoleMemo::Entry &e = memo->entry(Q, memo, sp->id);
				if (!e.image) {
					e.image = exact::invert_point_h(Q, c.C, c.rw2);
				}
				return *e.image;
			}
			return exact::invert_point_h(Q, c.C, c.rw2);
		});
	}

	// A.3: surf's carrier passes the record's Q -- the shadow's filter first, the
	// exact pole only where it cannot decide (memo: the predicate memoized by Q,
	// when the caller has one). A record without an exact pole: no.
	static bool pole_rec_on(const exact::PoleRec &rec, const Surf<N> &surf, PoleMemo *memo) {
		if (surf.line) {
			if (const std::optional<bool> f = rec.on_line(surf.sx, surf.sy, surf.ex, surf.ey)) {
				return *f;
			}
		} else {
			const auto &ec = surf.exact_circle();
			if (const std::optional<bool> f = rec.on_circle(ec.C, ec.rw2)) {
				return *f;
			}
		}
		const std::optional<exact::ExactPole> &p = rec.exact();
		if (!p) {
			return false;
		}
		PoleMemo::Entry *e = memo ? &memo->entry(p->Q, memo, surf.id) : nullptr;
		if (e && e->on) {
			return *e->on;
		}
		bool r;
		if (surf.line) {
			r = exact::pole_on_line(*p, surf.sx, surf.sy, surf.ex, surf.ey, &surf.exact_line());
		} else {
			const auto &ec = surf.exact_circle();
			r = exact::pole_on_circle(*p, ec.C, ec.rw2);
		}
		if (e) {
			e->on = r;
		}
		return r;
	}
	// ... and the beam is tangent to it there (given that it passes Q)
	static bool pole_rec_tangent(const exact::PoleRec &rec, const Surf<N> &surf) {
		if (surf.line) {
			if (const std::optional<bool> f = rec.tangent_line(surf.sx, surf.sy, surf.ex, surf.ey)) {
				return *f;
			}
		} else {
			const auto &ec = surf.exact_circle();
			if (const std::optional<bool> f = rec.tangent_circle(ec.C, ec.rw2)) {
				return *f;
			}
		}
		const std::optional<exact::ExactPole> &p = rec.exact();
		if (!p) {
			return false;
		}
		if (surf.line) {
			return exact::pole_tangent_line(*p, surf.sx, surf.sy, surf.ex, surf.ey, &surf.exact_line());
		}
		const auto &ec = surf.exact_circle();
		return exact::pole_tangent_circle(*p, ec.C, ec.rw2);
	}

	static void pole_cache(Walk<N> &w) {
		if (w.pole_tangent_serial != w.pole_serial) {
			w.pole_tangent.clear();
			w.pole_on.clear();
			w.pole_tangent_serial = w.pole_serial;
		}
	}

	// Exact: surf's carrier passes the frame's infinity point (cached per record).
	static bool pole_on(Walk<N> &w, const Surf<N> &surf) {
		pole_cache(w);
		auto it = w.pole_on.find(surf.id);
		if (it != w.pole_on.end()) {
			return it->second;
		}
		const bool r = pole_rec_on(*w.pole, surf, w.pole_memo);
		w.pole_on[surf.id] = r;
		return r;
	}

	// Exact: surf's carrier passes the frame's infinity point and is tangent
	// there to the beam (cached per record).
	static bool pole_tangent(Walk<N> &w, const Surf<N> &surf) {
		pole_cache(w);
		if (!pole_on(w, surf)) {
			return false;
		}
		auto it = w.pole_tangent.find(surf.id);
		if (it != w.pole_tangent.end()) {
			return it->second;
		}
		const bool r = pole_rec_tangent(*w.pole, surf);
		w.pole_tangent[surf.id] = r;
		return r;
	}

	// Exact (Stage 112, K10): the current exact line is parallel to surf's line
	// carrier (computed once per line, with the tangencies).
	static bool parallel(Walk<N> &w, const Surf<N> &surf) {
		if (w.parallel_line != w.line) {
			w.parallel.clear();
			for (const auto &s : w.scene->surfaces) {
				if (s->line) {
					w.parallel[s->id] = w.line->parallel_to(s->sx, s->sy, s->ex, s->ey);
				}
			}
			w.parallel_line = w.line;
		}
		auto it = w.parallel.find(surf.id);
		return it != w.parallel.end() && it->second;
	}

	// Exact: the current exact line is tangent to surf's circle (computed once per line).
	static bool tangent(Walk<N> &w, const Surf<N> &surf) {
		if (w.tangent_line != w.line) {
			w.tangent.clear();
			for (const auto &s : w.scene->surfaces) {
				if (!s->line) {
					const auto &ec = s->exact_circle();
					w.tangent[s->id] = w.line->tangent_to(ec.C, ec.rw2, ec.fcx, ec.fcy, ec.frr, ecc(ec), 8.0 * exact::U * std::fabs(ec.frr));
				}
			}
			w.tangent_line = w.line;
		}
		auto it = w.tangent.find(surf.id);
		return it != w.tangent.end() && it->second;
	}

	// Exact (K7): the current beam's exact circle passes the cursor. A float64
	// distance first (an exact pass is a rounding away; the filter only spares
	// the exact test), cached per record.
	// The chain's exact hit points the beam's exact circle passes (K11; cached per record).
	static const std::vector<CN<N>> &exact_hits_on_beam(Walk<N> &w, const Beam<N> &beam) {
		if (w.rec_hits_of == w.rec) {
			return w.rec_hits;
		}
		w.rec_hits_of = w.rec;
		w.rec_hits.clear();
		for (const auto &kv : w.chain->exact_hits) {
			const CN<N> &c = kv.second;
			const CN<N> q = beam.at(Backend<N>::param_of(beam, c));
			const double scale = gd_maxf(1.0, gd_maxf(std::fabs(c.fx()), std::fabs(c.fy())));
			if (!(q.sub(c).abs2().to_float() <= (1e-6 * scale) * (1e-6 * scale))) {
				continue;
			}
			if (w.rec->passes(c.fx(), c.fy())) {
				w.rec_hits.push_back(c);
			}
		}
		return w.rec_hits;
	}

	static bool cursor_on_beam(Walk<N> &w, const Beam<N> &beam) {
		if (w.rec_cursor_of == w.rec) {
			return w.rec_cursor_on;
		}
		w.rec_cursor_of = w.rec;
		w.rec_cursor_on = false;
		const CN<N> &c = *w.cursor;
		const CN<N> q = beam.at(Backend<N>::param_of(beam, c));
		const double scale = gd_maxf(1.0, gd_maxf(std::fabs(c.fx()), std::fabs(c.fy())));
		if (!(q.sub(c).abs2().to_float() <= (1e-6 * scale) * (1e-6 * scale))) {
			return false;
		}
		w.rec_cursor_on = w.rec->passes(c.fx(), c.fy()); // (Stage 124: the cursor pulled back, not the circle pushed forward)
		return w.rec_cursor_on;
	}

	// Exact (Perf P2b): the current beam touches surf's carrier, its two roots
	// there one point -- one decision, not two neighbours a margin separates.
	// False when the beam has no exact record.
	static bool beam_tangent(Walk<N> &w, const Surf<N> &surf) {
		if (!w.rec) {
			return false;
		}
		if (w.rec_tangent_of != w.rec) {
			w.rec_tangent.clear();
			w.rec_tangent_of = w.rec;
		}
		auto it = w.rec_tangent.find(surf.id);
		if (it != w.rec_tangent.end()) {
			return it->second;
		}
		bool r = false;
		if (const std::optional<exact::GCircle> g = w.rec->exact()) {
			if (surf.line) {
				r = exact::gc_tangent_line(*g, surf.sx, surf.sy, surf.ex, surf.ey, &surf.exact_line());
			} else {
				const auto &ec = surf.exact_circle();
				r = exact::gc_tangent_circle(*g, ec.C, ec.rw2);
			}
		}
		w.rec_tangent[surf.id] = r;
		return r;
	}

	// Bound on the float rounding of an exact circle's center (C / w, rounded parts).
	static double ecc(const typename Surf<N>::ExactCircle &ec) {
		return 6.0 * exact::U * gd_maxf(std::fabs(ec.fcx), std::fabs(ec.fcy));
	}

	// Candidates in walk order: [t, surf (nullptr = cursor), rehit, exact point].
	// keep_cursor (K4): a restart after a derivative-only seed keeps the cursor
	// candidate the loop listed (the oracle lists it once per loop).
	// Stage 134 (GAME_SPEC 12.1, the user): a surface whose crossing could matter to
	// the walk now -- the plan's remaining entries' surfaces; in PHYSICAL mode (the
	// planned walk past its cursor too) also a present surface with a side whose
	// effect is not a pass or which changes the state, and a target. A pure
	// pass-through (in ghost mode, what the ghost passes) is never computed.
	// (Oracle: trace2._candidate.)
	static bool candidate(const Walk<N> &w, const Surf<N> &s) {
		for (size_t i = size_t(std::max(w.plan_index, 0)); i < w.plan.size(); i++) {
			if (w.plan[i].surface_id == s.id) {
				return true;
			}
		}
		if (w.mode != Mode::Physical || !s.present(w.flags)) {
			return false;
		}
		if (s.is_target) {
			return true;
		}
		for (const bool left : {true, false}) {
			// (or which changes the game at the landing: a deferred effect, ghost or not)
			if (s.effect(left, w.flags) != EffectType::Pass || !s.state_changes(left, w.flags).empty() || s.landing(left, w.flags)) {
				return true;
			}
		}
		return false;
	}

	// The candidates' order along the walk from t0 (by parameter; at a tie the hits
	// by id, then the ray's origin, then the passage of infinity).
	static bool cand_order(const Candidate<N> &a, const Candidate<N> &b, const N &t0) {
		const int c = a.t.cmp(b.t);
		if (c != 0) {
			return before(a.t, b.t, t0);
		}
		if (a.inf != b.inf) {
			return b.inf; // at a tie the passage of infinity comes after the cursor and the hits
		}
		if (a.origin != b.origin) {
			return b.origin; // (and the ray's origin after them, before infinity)
		}
		const int ia = a.surf == nullptr ? -1 : a.surf->id;
		const int ib = b.surf == nullptr ? -1 : b.surf->id;
		return ia < ib;
	}

	// Stage 134: whether candidate c comes after prev on the walk's loop (the
	// departure re-hit last)
	static bool after_candidate(const Candidate<N> &prev, const Candidate<N> &c, const N &t0) {
		if (!prev.rehit && prev.surf == nullptr && !prev.inf && !prev.origin && prev.t.cmp(t0) == 0) {
			return true; // (the cursor at the departure point: reached first, as the sort puts it)
		}
		return c.rehit ? !prev.rehit : (!prev.rehit && cand_order(prev, c, t0));
	}

	// (into cands: the walk's step buffer, refilled)
	// (keep_shared: a rebuild within the loop reuses the loop's carrier roots, so a
	// carrier's crossing stays one point -- Stage 134)
	void candidates(Walk<N> &w, const Beam<N> &beam, int depart, bool fresh, const N &t0, bool keep_cursor, std::vector<Candidate<N>> &cands,
			bool keep_shared = false) const {
		cands.clear();
		std::vector<std::pair<int, InlineVec<N, 4>>> &shared = w.scratch->shared; // K6: carrier rep -> its roots this loop
		if (!keep_shared) {
			shared.clear();
		}
		const Surf<N> *dep_surf = depart >= 0 ? w.scene->find(depart) : nullptr;
		const int dep_rep = dep_surf != nullptr ? dep_surf->carrier_rep : -1;
		if (!w.cand_valid || w.cand_mode != w.mode || w.cand_index != w.plan_index || !(w.cand_flags == w.flags)) {
			w.cand_mask.resize(w.scene->surfaces.size());
			for (size_t si = 0; si < w.scene->surfaces.size(); si++) {
				w.cand_mask[si] = candidate(w, *w.scene->surfaces[si]) ? 1 : 0;
			}
			w.cand_valid = true;
			w.cand_mode = w.mode;
			w.cand_index = w.plan_index;
			w.cand_flags = w.flags;
		}
		for (size_t si = 0; si < w.scene->surfaces.size(); si++) {
			const Surf<N> &s = *w.scene->surfaces[si];
			if (!w.cand_mask[si]) {
				continue; // Stage 134: hitting it now could not matter
			}
			if (w.pole && pole_tangent(w, s)) {
				continue; // tangent at the frame's infinity point: a touch, not a crossing (exact, Stage 105)
			}
			if (w.line && !s.line && tangent(w, s)) {
				continue; // a tangent touch is not a crossing (exact)
			}
			if (w.line && s.line && parallel(w, s)) {
				continue; // Stage 112 (K10): an exact line never meets a parallel carrier (the tier's far root is an artefact)
			}
			// K6 (Stage 109): surfaces on one carrier share its roots (computed once
			// per loop -- on the departing member's own segment when the beam
			// departs that carrier), so simultaneous crossings tie exactly
			auto found = std::find_if(shared.begin(), shared.end(), [&](const auto &e) { return e.first == s.carrier_rep; });
			if (found != shared.end()) {
				for (const N &t : found->second) {
					cands.push_back(Candidate<N>{t, &s, false, std::nullopt, -1});
				}
				continue;
			}
			const bool departing = dep_rep >= 0 && dep_rep == s.carrier_rep;
			const Surf<N> &src = departing ? *w.scene->find(depart) : s;
			InlineVec<N, 4> rs;
			if (fresh) {
				rs = geom::hits(beam, src.seg, departing);
				if (departing) {
					InlineVec<N, 4> f;
					for (const N &t : rs) {
						if (!t.is_zero()) {
							f.push_back(t);
						}
					}
					rs = f;
				}
			} else {
				rs = geom::hits(beam, src.seg, false);
				if (departing && !rs.empty()) {
					// a resumed beam departs this carrier at t0: its root nearest t0
					// is the departure (identified, not compared for equality: the
					// roots are recomputed on the restored beam and can land an ulp
					// off t0; Stage 106)
					size_t best = 0;
					double best_d = INF_D;
					for (size_t i = 0; i < rs.size(); i++) {
						const double d = rs[i].is_inf() ? INF_D : std::fabs(rs[i].sub(t0).to_float());
						if (d < best_d) {
							best_d = d;
							best = i;
						}
					}
					rs.erase(rs.begin() + std::ptrdiff_t(best));
				}
			}
			for (const N &t : rs) {
				cands.push_back(Candidate<N>{t, &s, false, std::nullopt, -1});
			}
			shared.emplace_back(s.carrier_rep, std::move(rs));
		}
		bool cursor_now = false;
		// the cursor is on the beam only on the planned path
		if (!w.cursor_done && w.cursor && (keep_cursor || (w.plan_index >= int(w.plan.size()) && w.plan_matched && w.on_plan))) {
			N tc = Backend<N>::param_of(beam, *w.cursor);
			const int ki = known_index(w.known, *w.cursor);
			// the cursor exactly at the departure point is reached immediately
			if ((fresh && geom::eq(beam.p, *w.cursor))
					|| (ki >= 0 && w.known[size_t(ki)].skip == depart && depart >= 0 && w.known[size_t(ki)].t.cmp(t0) == 0)) {
				tc = t0;
				cursor_now = true;
			}
			{
				static const bool trace = std::getenv("RC_REALIZE_TRACE") != nullptr;
				if (trace) {
					std::fprintf(stderr, "cursor cand: tc %.9g t0 %.9g fresh %d ki %d skip %d depart %d now %d mode %d\n", tc.to_float(), t0.to_float(), int(fresh), ki,
						ki >= 0 ? w.known[size_t(ki)].skip : -9, depart, int(cursor_now), int(w.mode));
				}
			}
			// Stage 123: a cursor that is a known point (a segment's endpoint, a corner
			// two surfaces share) is that point, by provenance: its exact parameter and
			// position, so the carriers through it tie with it exactly (the cursor
			// first), never by the rounding of its own parameter
			std::optional<CN<N>> cursor_pt;
			if (ki >= 0 && !cursor_now) {
				tc = w.known[size_t(ki)].t;
				cursor_pt = w.known[size_t(ki)].p;
			}
			cands.push_back(Candidate<N>{tc, nullptr, false, cursor_pt, -1});
			if (ki < 0) {
				w.known.push_back(Known<N>{*w.cursor, tc, -1, nullptr});
			}
		}
		// K7: the cursor is a known point of every beam whose exact circle passes
		// it, so a carrier through the cursor is crossed exactly there (the
		// departure point after it is the cursor exactly: reached at once)
		if (w.cursor && w.rec && known_index(w.known, *w.cursor) < 0 && cursor_on_beam(w, beam)) {
			w.known.push_back(Known<N>{*w.cursor, Backend<N>::param_of(beam, *w.cursor), -1, nullptr});
		}
		// Stage 113 (K11): a planned projective's hit point that is a data point
		// of its segment is a known point of every beam whose exact circle
		// passes it (an exact line has it through add_line_endpoints): the
		// crossing there is the endpoint exactly, blocked on one side
		if (w.chain && w.rec && !w.chain->exact_hits.empty()) {
			for (const CN<N> &D : exact_hits_on_beam(w, beam)) {
				if (known_index(w.known, D) < 0) {
					w.known.push_back(Known<N>{D, Backend<N>::param_of(beam, D), -1, nullptr});
				}
			}
		}
		// Stage 131: the ray's origin, where the beam passes it -- not at its
		// departure (a loop's return there is the loop's end, walk_from). A known
		// point of the beam: a carrier exactly through it is crossed there, at its
		// parameter (the crossing first, then the passage)
		if (w.O && !geom::eq(*w.O, fresh ? beam.p : beam.at(t0))) {
			const int ki = known_index(w.known, *w.O);
			const N to = ki >= 0 ? w.known[size_t(ki)].t : Backend<N>::param_of(beam, *w.O);
			if (to.cmp(t0) != 0) {
				Candidate<N> oc{to, nullptr, false, w.O, -1};
				oc.origin = true;
				cands.push_back(oc);
				if (ki < 0) {
					w.known.push_back(Known<N>{*w.O, to, -1, nullptr});
				}
			}
		}
		for (Known<N> &kp : w.known) {
			snap(w, beam, cands, kp);
		}
		if (w.pole && w.Q) {
			// Stage 105: a carrier through the frame's infinity point is crossed
			// exactly there (the record makes Q exact): its root nearest Q becomes
			// Q's parameter, so simultaneous crossings at Q tie exactly (by id, the
			// passage of Q last) -- unless Q is the departure point
			// (departing from Q: another carrier through Q is crossed at the
			// departure point, at the end of the loop like a known departure point's)
			const N tq = w.depart_q ? t0 : Backend<N>::param_of(beam, *w.Q);
			if (w.depart_q || tq.cmp(t0) != 0) {
				for (const auto &sp : w.scene->surfaces) {
					if (!pole_on(w, *sp) || (w.depart_q && sp->id == depart)) {
						continue;
					}
					int best = -1;
					double best_d = INF_D;
					for (size_t i = 0; i < cands.size(); i++) {
						if (cands[i].surf != sp.get() || cands[i].inf) {
							continue;
						}
						const double d = beam.at(cands[i].t).sub(*w.Q).abs2().to_float();
						if (d < best_d) {
							best_d = d;
							best = int(i);
						}
					}
					if (best >= 0) {
						cands[size_t(best)] = Candidate<N>{tq, sp.get(), false, w.Q, -1, false, true};
					}
				}
			}
		}
		{ // (Stage 136: infinity is always on)
			if (!w.Q) {
				if (beam.kappa.is_zero()) {
					// the wrap through infinity: after every crossing ahead (t > t0),
					// before the wrapped ones (no root of a straight beam is at inf)
					cands.push_back(Candidate<N>{t0.like(INF_D), nullptr, false, std::nullopt, -1, true});
				}
			} else if (!w.depart_q) {
				// the frame's image of infinity is a finite point on this beam: an
				// ordinary crossing, exact by provenance (not at the departure point)
				const N tq = Backend<N>::param_of(beam, *w.Q);
				if (tq.cmp(t0) != 0) {
					cands.push_back(Candidate<N>{tq, nullptr, false, w.Q, -1, true});
				}
			}
		}
		std::sort(cands.begin(), cands.end(), [&](const Candidate<N> &a, const Candidate<N> &b) {
			// the cursor at the departure point comes first (the wrap candidate is not the cursor)
			const bool a_cursor = a.surf == nullptr && !a.inf && !a.origin;
			const bool b_cursor = b.surf == nullptr && !b.inf && !b.origin;
			if (cursor_now && a_cursor != b_cursor) {
				return a_cursor;
			}
			return cand_order(a, b, t0);
		});
		if (w.line) {
			concurrent_ties(w, cands);
		} else if (w.rec) {
			concurrent_ties_circle(w, beam, cands);
		}
		if (depart >= 0 && dep_surf != nullptr && candidate(w, *dep_surf)) {
			// the departure re-hit is at the departure point (exact when known)
			std::optional<CN<N>> dep_pt;
			for (const Known<N> &k : w.known) {
				if (k.skip == depart && k.t.cmp(t0) == 0) {
					dep_pt = k.p;
				}
			}
			cands.push_back(Candidate<N>{t0, w.scene->find(depart), true, dep_pt, -1});
		}
	}

	// On an exact line, adjacent crossings of two line carriers that meet the
	// line at the same point share one parameter, ordered by id.
	static void concurrent_ties(Walk<N> &w, std::vector<Candidate<N>> &cands) {
		for (size_t i = 0; i + 1 < cands.size(); i++) {
			Candidate<N> &x = cands[i];
			Candidate<N> &y = cands[i + 1];
			if (x.surf == nullptr || y.surf == nullptr || x.rehit || y.rehit || !x.surf->line || !y.surf->line) {
				continue;
			}
			const Surf<N> *p = x.surf;
			const Surf<N> *q = y.surf;
			// A.2: two surfaces on one carrier (carrier_rep: exact collinearity) are one
			// line, concurrent with any line that has exact coefficients -- what the
			// predicate answers for proportional rows (hit_effects' corners: three
			// surfaces on one line at every crossing, 36% of a region in BigFloat)
			const bool concurrent = p->carrier_rep == q->carrier_rep
				? w.line->has_exact()
				: w.line->concurrent(p->sx, p->sy, p->ex, p->ey, q->sx, q->sy, q->ex, q->ey, &p->exact_line(), &q->exact_line());
			if (!concurrent) {
				continue;
			}
			const Surf<N> *lo = p->id < q->id ? p : q;
			const Surf<N> *hi = p->id < q->id ? q : p;
			const N t = x.t;
			const std::optional<CN<N>> pt = x.pt ? x.pt : y.pt;
			const bool at_q = x.at_q || y.at_q; // concurrent at Q: both pinned there
			// Stage 123: three or more carriers at one point are one tie group (a pair
			// whose first is grouped joins it): no margin between them, never a climb
			const int group = x.tie >= 0 ? x.tie : int(i);
			cands[i] = Candidate<N>{t, lo, false, pt, group, false, at_q};
			cands[i + 1] = Candidate<N>{t, hi, false, pt, group, false, at_q};
		}
	}

	// Perf P2b: on a circular beam with an exact record, adjacent crossings of
	// two line carriers at their common point, which the beam passes exactly,
	// share one parameter: the id orders them, never rounding (as on an exact
	// line; the oracle's _concurrent_ties_gc). The roots at the point are told
	// from the carriers' other roots by distance; the decision itself is exact.
	static void concurrent_ties_circle(Walk<N> &w, const Beam<N> &beam, std::vector<Candidate<N>> &cands) {
		for (size_t i = 0; i + 1 < cands.size(); i++) {
			Candidate<N> &x = cands[i];
			Candidate<N> &y = cands[i + 1];
			if (x.surf == nullptr || y.surf == nullptr || x.rehit || y.rehit || !x.surf->line || !y.surf->line || x.tie >= 0 || y.tie >= 0) {
				continue;
			}
			double xx, xy, yx, yy;
			Backend<N>::f64_point(beam, x.t, xx, xy);
			Backend<N>::f64_point(beam, y.t, yx, yy);
			const double scale = gd_maxf(1.0, gd_maxf(std::fabs(xx), std::fabs(xy)));
			if (!(d64(xx, xy, yx, yy) <= 1e-6 * scale)) {
				continue; // (the cheap filter: a common point is a rounding apart)
			}
			const Surf<N> *p = x.surf;
			const Surf<N> *q = y.surf;
			if (p->carrier_rep == q->carrier_rep) {
				continue; // (A.2) one carrier: parallel, as Xw = 0 below says
			}
			const exact::Line3 &L1 = p->exact_line();
			const exact::Line3 &L2 = q->exact_line();
			using exact::add;
			using exact::mul;
			using exact::sub;
			// the common point, homogeneous: (c1 b2 - c2 b1, a1 c2 - a2 c1, a1 b2 - a2 b1)
			const BigFloat Xw = sub(mul(L1[0], L2[1]), mul(L2[0], L1[1]));
			if (Xw.is_zero()) {
				continue; // parallel carriers
			}
			const BigFloat Xx = sub(mul(L1[2], L2[1]), mul(L2[2], L1[1]));
			const BigFloat Xy = sub(mul(L1[0], L2[2]), mul(L2[0], L1[2]));
			const double fx = Xx.to_float() / Xw.to_float();
			const double fy = Xy.to_float() / Xw.to_float();
			if (!(d64(xx, xy, fx, fy) <= 1e-6 * scale && d64(yx, yy, fx, fy) <= 1e-6 * scale)) {
				continue; // the crossings near each other, but not at the common point
			}
			const std::optional<exact::GCircle> g = w.rec->exact();
			if (!g) {
				continue;
			}
			const BigFloat on = add(add(mul(g->A, add(mul(Xx, Xx), mul(Xy, Xy))), mul(add(mul(g->B, Xx), mul(g->C, Xy)), Xw)), mul(g->D, mul(Xw, Xw)));
			if (!on.is_zero()) {
				continue;
			}
			const Surf<N> *lo = p->id < q->id ? p : q;
			const Surf<N> *hi = p->id < q->id ? q : p;
			const N t = x.t;
			const std::optional<CN<N>> pt = x.pt ? x.pt : y.pt;
			const bool at_q = x.at_q || y.at_q;
			cands[i] = Candidate<N>{t, lo, false, pt, int(i), false, at_q};
			cands[i + 1] = Candidate<N>{t, hi, false, pt, int(i), false, at_q};
		}
	}

	// A surface whose carrier contains the known point kp is hit exactly there:
	// its root nearest kp becomes (kt, kp).
	static void snap(Walk<N> &w, const Beam<N> &beam, std::vector<Candidate<N>> &cands, Known<N> &rec) {
		if (!rec.incident) {
			rec.incident = incident_list(w, rec.p);
		}
		for (const Surf<N> *s : *rec.incident) {
			if (s->id == rec.skip) {
				continue;
			}
			int best = -1;
			double best_d = INF_D;
			for (size_t i = 0; i < cands.size(); i++) {
				if (cands[i].surf != s) {
					continue;
				}
				const double d = beam.at(cands[i].t).sub(rec.p).abs2().to_float();
				if (d < best_d) {
					best_d = d;
					best = int(i);
				}
			}
			if (best >= 0) {
				cands[size_t(best)] = Candidate<N>{rec.t, s, false, rec.p, -1};
			}
		}
	}

	static double d64(double ax, double ay, double x, double y) {
		const double dx = ax - x;
		const double dy = ay - y;
		return std::sqrt(dx * dx + dy * dy);
	}

	// Per candidate: px distance to its neighbours in the walk order (and, for
	// a segment, to its endpoints). The two roots of a carrier the beam touches
	// exactly (beam_tangent) are one decision: not each other's neighbour.
	// Stage 123: what set a candidate's margin (the ladder counts the declined
	// decisions by it): the term, and how close. An instrument: no decision reads it.
	enum Why : uint8_t {
		WHY_NONE = 0,
		WHY_NEIGHBOUR_CORNER = 1, // another surface's hit, at an endpoint the two share
		WHY_NEIGHBOUR = 2,        // another surface's hit, elsewhere
		WHY_END_CORNER = 3,       // its own segment's endpoint, which another surface shares
		WHY_END = 4,              // its own segment's endpoint, a free one
		WHY_SAME_SURFACE = 5,     // its own surface's other root (a beam touching its carrier, not found tangent)
		WHY_ZERO = 0 << 3, WHY_ROUNDING = 1 << 3, WHY_SMALL = 2 << 3, WHY_WIDE = 3 << 3 // how close
	};

	static void gaps(Walk<N> &w, const Beam<N> &beam, const std::vector<Candidate<N>> &cands, std::vector<double> &out, std::vector<uint8_t> *why = nullptr) {
		std::vector<std::pair<double, double>> &pts = w.scratch->pts;
		pts.clear();
		for (const Candidate<N> &cand : cands) {
			if (cand.inf && !cand.pt) {
				pts.emplace_back(INF_D, INF_D);
			} else if (cand.pt) {
				pts.emplace_back(cand.pt->fx(), cand.pt->fy());
			} else {
				double x, y;
				Backend<N>::f64_point(beam, cand.t, x, y);
				pts.emplace_back(x, y);
			}
		}
		// Stage 123: at a BigFloat tier a gap within float64's reach of 0 is measured
		// at the tier's precision (the points above are float64 views of the beam):
		// two hits a few 1e-13 px apart are told apart at 4 limbs, not left a
		// rounding apart at every tier (a climb to 64 limbs that never converges).
		// The float64 walks are untouched.
		std::vector<std::optional<CN<N>>> at_tier(std::is_same_v<N, BigFloat> ? cands.size() : 0); // (BigFloat tiers only)
		auto point_at_tier = [&](size_t i) -> const CN<N> & {
			if (!at_tier[i]) {
				at_tier[i] = cands[i].pt ? *cands[i].pt : beam.at(cands[i].t);
			}
			return *at_tier[i];
		};
		auto finer = [&](double d, size_t i, const CN<N> *other_pt, size_t k) -> double {
			if constexpr (std::is_same_v<N, BigFloat>) {
				const double scale = gd_maxf(1.0, gd_maxf(std::fabs(pts[i].first), std::fabs(pts[i].second)));
				if (std::isfinite(d) && d <= 1e-6 * scale && !(cands[i].inf && !cands[i].pt)) {
					const CN<N> &a = point_at_tier(i);
					const CN<N> &b = other_pt ? *other_pt : point_at_tier(k);
					return std::sqrt(a.sub(b).abs2().to_float());
				}
			}
			return d;
		};
		// double roots: consecutive candidates on one surface, a rounding apart,
		// where the beam touches its carrier exactly
		std::vector<int> &dbl = w.scratch->dbl;
		dbl.assign(cands.size(), -1);
		for (size_t i = 0; i < cands.size(); i++) {
			const Surf<N> *s = cands[i].surf;
			if (s == nullptr || cands[i].rehit) {
				continue;
			}
			size_t j = i + 1;
			while (j < cands.size() && cands[j].surf == nullptr) {
				j++;
			}
			if (j >= cands.size() || cands[j].surf != s || cands[j].rehit) {
				continue;
			}
			const double d = d64(pts[i].first, pts[i].second, pts[j].first, pts[j].second);
			const double scale = gd_maxf(1.0, gd_maxf(std::fabs(pts[i].first), std::fabs(pts[i].second)));
			if (d <= 1e-6 * scale && beam_tangent(w, *s)) {
				dbl[j] = dbl[i] >= 0 ? dbl[i] : int(i);
				dbl[i] = dbl[j];
			}
		}
		auto same_root = [&](size_t i, long k) { return dbl[i] >= 0 && dbl[size_t(k)] == dbl[i]; };
		// Stage 123: surfaces on one carrier cross it at one shared root (K6): the
		// same parameter, ordered by id -- one decision, not two neighbours
		auto shared_root = [&](size_t i, long k) {
			const Candidate<N> &a = cands[i];
			const Candidate<N> &b = cands[size_t(k)];
			return a.surf != nullptr && b.surf != nullptr && a.surf != b.surf && !a.rehit && !b.rehit && !a.inf && !b.inf
				&& a.surf->carrier_rep == b.surf->carrier_rep && a.t.cmp(b.t) == 0;
		};
		// (the instrument) an endpoint of `s` at (x, y) that another surface shares
		auto shared_end = [&](const Surf<N> *s, double x, double y, const Surf<N> *with) {
			const double tol = 1e-6 * gd_maxf(1.0, gd_maxf(std::fabs(x), std::fabs(y)));
			for (const auto &[px, py] : {std::pair{s->sx, s->sy}, std::pair{s->ex, s->ey}}) {
				if (s->full || d64(px, py, x, y) > tol) {
					continue;
				}
				for (const auto &o : w.scene->surfaces) {
					if (o.get() == s || o->full || (with != nullptr && o.get() != with)) {
						continue;
					}
					if (d64(o->sx, o->sy, px, py) <= tol || d64(o->ex, o->ey, px, py) <= tol) {
						return true;
					}
				}
			}
			return false;
		};
		auto closeness = [](double g, double x, double y) -> uint8_t {
			const double scale = gd_maxf(1.0, gd_maxf(std::fabs(x), std::fabs(y)));
			return g == 0.0 ? WHY_ZERO : g <= 1e-6 * scale ? WHY_ROUNDING : g <= 1e-2 ? WHY_SMALL : WHY_WIDE;
		};
		if (why) {
			why->assign(cands.size(), WHY_NONE);
		}
		out.clear();
		for (size_t i = 0; i < cands.size(); i++) {
			double g = INF_D;
			uint8_t term = WHY_NONE;
			const Surf<N> *other = nullptr; // (the instrument) the neighbour that set it
			const Surf<N> *s = cands[i].surf;
			if (s != nullptr) {
				const bool exact_i = cands[i].pt.has_value();
				const int tie_i = cands[i].tie;
				long l = long(i) - 1;
				while (l >= 0 && (cands[size_t(l)].surf == nullptr || (exact_i && cands[size_t(l)].pt) || (tie_i >= 0 && cands[size_t(l)].tie == tie_i) || same_root(i, l) || shared_root(i, l))) {
					l--;
				}
				if (l >= 0) {
					const double d = cands[size_t(l)].inf && !cands[size_t(l)].pt ? d64(pts[i].first, pts[i].second, pts[size_t(l)].first, pts[size_t(l)].second)
						: finer(d64(pts[i].first, pts[i].second, pts[size_t(l)].first, pts[size_t(l)].second), i, nullptr, size_t(l));
					if (d < g) {
						g = d;
						term = cands[size_t(l)].surf == s ? WHY_SAME_SURFACE : WHY_NEIGHBOUR;
						other = cands[size_t(l)].surf;
					}
				}
				size_t r = i + 1;
				while (r < cands.size() && (cands[r].surf == nullptr || (exact_i && cands[r].pt) || (tie_i >= 0 && cands[r].tie == tie_i) || same_root(i, long(r)) || shared_root(i, long(r)))) {
					r++;
				}
				if (r < cands.size()) {
					const double d = cands[r].inf && !cands[r].pt ? d64(pts[i].first, pts[i].second, pts[r].first, pts[r].second)
						: finer(d64(pts[i].first, pts[i].second, pts[r].first, pts[r].second), i, nullptr, r);
					if (d < g) {
						g = d;
						term = cands[r].surf == s ? WHY_SAME_SURFACE : WHY_NEIGHBOUR;
						other = cands[r].surf;
					}
				}
				if (!s->seg.full) {
					const double ds = finer(d64(pts[i].first, pts[i].second, s->sx, s->sy), i, &s->seg.s, 0);
					const double de = finer(d64(pts[i].first, pts[i].second, s->ex, s->ey), i, &s->seg.e, 0);
					// an exact endpoint hit is decided by provenance, not by a margin
					if (ds > 0.0 && ds < g) {
						g = ds;
						term = WHY_END;
						other = nullptr;
					}
					if (de > 0.0 && de < g) {
						g = de;
						term = WHY_END;
						other = nullptr;
					}
				}
			}
			if (why && term != WHY_NONE) {
				const uint8_t close = closeness(g, pts[i].first, pts[i].second);
				if (close <= WHY_SMALL) { // (a corner is looked for only where the margin is small)
					if (term == WHY_NEIGHBOUR && other && shared_end(s, pts[i].first, pts[i].second, other)) {
						term = WHY_NEIGHBOUR_CORNER;
					} else if (term == WHY_END && shared_end(s, pts[i].first, pts[i].second, nullptr)) {
						term = WHY_END_CORNER;
					}
				}
				(*why)[i] = uint8_t(term | close);
			}
			out.push_back(g);
		}
		return;
	}

	// Stage 138 (GAME_SPEC 11.1, the user): the other present surfaces holding the
	// hit point exactly -- a crossing at the same parameter (an exact tie, or one
	// known point) on its segment, or an exact endpoint equal to the point by data.
	// Never by distance. (The oracle's _shared_surfaces.)
	static void shared_surfaces(const Walk<N> &w, const std::vector<Candidate<N>> &cands, size_t ci, const N &t, const CN<N> &pt,
			const Surf<N> &surf, std::vector<const Surf<N> *> &out) {
		out.clear();
		for (size_t k = 0; k < cands.size(); k++) {
			const Candidate<N> &c = cands[k];
			if (k == ci || c.surf == nullptr || c.inf || c.origin || c.surf == &surf || c.t.cmp(t) != 0) {
				continue;
			}
			if (c.surf->present(w.flags) && geom::on_segment(c.surf->seg, pt) && std::find(out.begin(), out.end(), c.surf) == out.end()) {
				out.push_back(c.surf);
			}
		}
		for (const auto &sp : w.scene->surfaces) {
			const Surf<N> *s = sp.get();
			if (s == &surf || s->seg.full || !s->present(w.flags) || std::find(out.begin(), out.end(), s) != out.end()) {
				continue;
			}
			if (geom::endpoint_at(s->seg, pt) != 0) {
				out.push_back(s);
			}
		}
	}

	struct Classified {
		CN<N> pt;
		bool on_seg;
		bool left;
		int at_ep; // 0: not at an endpoint of surf; 1 its start, 2 its end (Stage 138: solid is decided with the shared surfaces)
	};

	// [point, on_segment, side LEFT, at endpoint] of the hit at t on surf.
	static Classified classify(const Scene<N> &scene, const Beam<N> &beam, const N &t, const Surf<N> &surf, bool at_origin,
			const std::optional<CN<N>> &known_pt, const Flags &flags) {
		(void)scene;
		(void)flags;
		const CN<N> pt = known_pt ? *known_pt : (at_origin ? beam.p : beam.at(t));
		const CN<N> tang = beam.tangent_at(t);
		const bool on_seg = geom::on_segment(surf.seg, pt);
		return Classified{pt, on_seg, geom::side_left(surf.seg, pt, tang), geom::endpoint_at(surf.seg, pt)};
	}

	// Stage 108: a plan item naming a piece accepts only points inside its span
	// (Stage 133: its ends that are crossings, the crossings themselves).
	static bool in_span(const Walk<N> &w, int i, const CN<N> &pt) {
		if (i < 0 || size_t(i) >= w.spans.size() || !w.spans[size_t(i)]) {
			return true;
		}
		return geom::on_segment(*w.spans[size_t(i)], pt);
	}

	// The points where two surfaces' carriers cross, at the tier's precision.
	static std::vector<CN<N>> carrier_crossings(const Surf<N> &a, const Surf<N> &b) {
		std::vector<CN<N>> out;
		const N two = a.seg.s.re.like(2.0);
		const N four = a.seg.s.re.like(4.0);
		if (a.line && b.line) {
			const CN<N> r = a.seg.e.sub(a.seg.s);
			const CN<N> q = b.seg.e.sub(b.seg.s);
			const N den = CN<N>::cross(r, q);
			if (den.is_zero()) {
				return out;
			}
			out.push_back(a.seg.s.add(r.scale(CN<N>::cross(b.seg.s.sub(a.seg.s), q).div(den))));
			return out;
		}
		if (a.line != b.line) {
			const Surf<N> &l = a.line ? a : b;
			const Surf<N> &c = a.line ? b : a;
			const CN<N> d = l.seg.e.sub(l.seg.s);
			const CN<N> w = l.seg.s.sub(c.seg.circ->c);
			const N A2 = d.abs2();
			const N B2 = two.mul(CN<N>::dot(w, d));
			const N C2 = w.abs2().sub(c.seg.circ->r2);
			const N disc = B2.mul(B2).sub(four.mul(A2).mul(C2));
			if (disc.is_negative() || A2.is_zero()) {
				return out;
			}
			const N sq = disc.sqrt_val();
			for (const N &u : {B2.neg().sub(sq).div(two.mul(A2)), B2.neg().add(sq).div(two.mul(A2))}) {
				out.push_back(l.seg.s.add(d.scale(u)));
			}
			return out;
		}
		const CN<N> c0 = a.seg.circ->c;
		const CN<N> c1 = b.seg.circ->c;
		const N dd = c1.sub(c0).abs_val();
		if (dd.is_zero()) {
			return out;
		}
		const N x = dd.mul(dd).add(a.seg.circ->r2).sub(b.seg.circ->r2).div(two.mul(dd));
		const N h2 = a.seg.circ->r2.sub(x.mul(x));
		if (h2.is_negative()) {
			return out;
		}
		const N h = h2.sqrt_val();
		const CN<N> e = c1.sub(c0).div_real(dd);
		const CN<N> base = c0.add(e.scale(x));
		out.push_back(base.add(e.mul_i().scale(h)));
		out.push_back(base.sub(e.mul_i().scale(h)));
		return out;
	}

	// Stage 133 (GAME_SPEC 9.7, the user): the plan's pieces as segments -- a piece's
	// end stored as the float32 rounding of a crossing of its carrier with another
	// surface's carrier is that crossing, by provenance (the nearest within an ulp
	// in each coordinate), at the tier's precision.
	std::vector<std::optional<Seg<N>>> piece_spans(const std::vector<PlanItem> &plan) const {
		std::vector<std::pair<int, std::optional<std::array<double, 6>>>> key;
		bool any = false;
		if (cache_ != nullptr) {
			key.reserve(plan.size());
			for (const PlanItem &item : plan) {
				key.emplace_back(item.surface_id, item.span);
				any = any || item.span.has_value();
			}
			if (any) {
				for (const auto &e : cache_->spans) {
					if (e.plan == key) {
						return e.spans;
					}
				}
			}
		}
		std::vector<std::optional<Seg<N>>> out = piece_spans_uncached(plan);
		if (cache_ != nullptr && any) {
			if (cache_->spans.size() >= 4) {
				cache_->spans.erase(cache_->spans.begin());
			}
			cache_->spans.push_back({std::move(key), out});
		}
		return out;
	}
	std::vector<std::optional<Seg<N>>> piece_spans_uncached(const std::vector<PlanItem> &plan) const {
		std::vector<std::optional<Seg<N>>> out(plan.size());
		for (size_t i = 0; i < plan.size(); i++) {
			if (!plan[i].span) {
				continue;
			}
			const std::array<double, 6> &a = *plan[i].span;
			const Surf<N> *me = scene_.find(plan[i].surface_id);
			if (me == nullptr) {
				continue; // (no such surface: nothing to match)
			}
			auto like = [&](double v) { return me->seg.s.re.like(v); };
			auto end = [&](double x, double y) {
				CN<N> best{like(x), like(y)};
				auto ulp = [](double v) { return v == 0.0 ? std::ldexp(1.0, -149) : std::ldexp(1.0, std::ilogb(v) - 23); };
				double best_d = INF_D;
				for (const auto &sp : scene_.surfaces) {
					if (sp->id == me->id) {
						continue;
					}
					for (const CN<N> &c : carrier_crossings(*me, *sp)) {
						const double dx = std::fabs(c.fx() - x);
						const double dy = std::fabs(c.fy() - y);
						if (dx <= ulp(x) && dy <= ulp(y) && std::hypot(dx, dy) < best_d) {
							best_d = std::hypot(dx, dy);
							best = c;
						}
					}
				}
				return best;
			};
			Seg<N> sg;
			sg.s = end(a[0], a[1]);
			sg.e = end(a[2], a[3]);
			sg.v = CN<N>{like(a[4]), like(a[5])};
			out[i] = sg;
		}
		return out;
	}

	// defer (B5): the event keeps the beam and only changes the flags -- a
	// mismatch then waits for the point's frame (point_sync).
	static void plan_bookkeeping(Walk<N> &w, int sid, const CN<N> &pt, bool defer = false) {
		if (w.plan_index < int(w.plan.size()) && sid == w.plan[size_t(w.plan_index)].surface_id && in_span(w, w.plan_index, pt)) {
			w.plan_index++;
			return;
		}
		if (defer && w.point) {
			w.point->pending = true;
			return;
		}
		w.left_plan = w.left_plan || w.plan_index < int(w.plan.size());
		w.plan_matched = false;
		w.on_plan = false; // a surface fired out of plan: off the planned beams
	}

	static void point_close(Walk<N> &w) {
		if (w.point && w.point->pending) {
			w.left_plan = w.left_plan || w.plan_index < int(w.plan.size());
			w.plan_matched = false;
			w.on_plan = false;
		}
		w.point.reset();
	}

	// The planned walk's decision at this candidate, on the shadow: the next
	// entry is consumed (its state changes applied under the shadow's flags); a
	// transformative or projective entry ends the shadow.
	static void point_step(Walk<N> &w, const Surf<N> *surf, bool inf, bool left, const CN<N> &pt, bool origin = false) {
		if (!w.point || w.point->broke || w.point->index >= int(w.plan.size())) {
			return;
		}
		PointGroup<N> &p = *w.point;
		const PlanItem &item = w.plan[size_t(p.index)];
		if (inf || origin) {
			if ((inf && item.is_infinity()) || (origin && item.is_origin())) {
				p.index++;
			}
			return;
		}
		if (item.is_infinity() || item.is_origin() || item.surface_id != surf->id || item.left != left || !in_span(w, p.index, pt)) {
			return;
		}
		const EffectType eff = surf->effect(left, p.flags);
		if (is_transformative(eff) || is_projective(eff)) {
			p.broke = true;
			return;
		}
		p.flags = p.flags.applied(surf->state_changes(left, p.flags));
		p.index++;
	}

	// A pending point resolves when the walk is in the shadow's frame.
	static bool point_sync(Walk<N> &w) {
		if (w.point && w.point->pending && !w.point->broke && w.point->flags == w.flags) {
			w.plan_index = w.point->index;
			w.point->pending = false;
			return true;
		}
		return false;
	}

	static Event<N> &emit(Walk<N> &w, EventKind kind, const BeamPtr<N> &beam, const N &t_from, const std::optional<N> &t_to, double margin) {
		Event<N> e;
		e.kind = kind;
		e.beam = beam;
		e.off_plan = w.left_plan;
		if (Backend<N>::views_per_run()) {
			if (w.hp_view < 0 || w.hp_src != beam) {
				w.hp_src = beam;
				w.hp_view = w.view_count++;
			}
			e.beam_view = w.hp_view;
		} else {
			auto it = std::find_if(w.beam_views.begin(), w.beam_views.end(), [&](const auto &v) { return v.first == beam.get(); });
			if (it == w.beam_views.end()) {
				w.beam_views.emplace_back(beam.get(), int(w.beam_views.size()));
				it = w.beam_views.end() - 1;
			}
			e.beam_view = it->second;
		}
		e.t_from = t_from;
		e.t_to = t_to;
		if (t_to) {
			e.point = beam->at(*t_to);
		}
		e.margin = margin;
		e.seeded = w.seeded;
		e.epoch = w.epoch;
		w.events.push_back(std::move(e));
		double g = 0.0;
		if (t_to) {
			if (w.seeded) {
				g = Backend<N>::gain(*beam, *t_to);
			} else if (w.anchored) {
				g = Backend<N>::prefix_gain(*beam, *t_to) * PREFIX_GAIN_SCALE;
			}
		}
		w.gains.push_back(g);
		return w.events.back();
	}

	// The other intersection of `beam` with surf, given the one applied at t.
	static N other_root(const Beam<N> &beam, const Surf<N> &surf, int depart, const N &t0, const N &t, const CN<N> &pt) {
		if (surf.id == depart) {
			return t0;
		}
		const std::vector<N> rs = geom::hits(beam, surf.seg, false);
		if (rs.size() < 2) {
			return t; // tangent / straight beam: a single root
		}
		N far = rs[0];
		double far_d = -1.0;
		for (const N &r : rs) {
			const double d = beam.at(r).sub(pt).abs2().to_float();
			if (d > far_d) {
				far_d = d;
				far = r;
			}
		}
		return far;
	}

	struct Next {
		BeamPtr<N> beam;
		int depart;
		N t0;
		bool fresh;
	};

	// Forward walk. depart: id of the surface the beam departs from (-1: none).
	// t_start nullopt: fresh (the beam's own point p is the departure, t = 0).
	// A.8: past what the caller reads (and two events more): read_horizon is pure in
	// the events, so its value is kept until an event is added
	static bool past_horizon(Walk<N> &w) {
		if (w.read_horizon == nullptr) {
			return false;
		}
		if (w.horizon_size != w.events.size()) {
			w.horizon_value = (*w.read_horizon)(w.events);
			w.horizon_size = w.events.size();
		}
		return w.horizon_value + 2 <= w.events.size();
	}

	void walk_from(Walk<N> &w, BeamPtr<N> beam, int depart, std::optional<N> t_start) {
		static const bool why = std::getenv("RC_LADDER_WHY") != nullptr;
		w.record_why = why;
		ScratchLease<N> lease; // (Stage 128: the step buffers, this thread's, while this walk runs)
		struct Unlease {
			Walk<N> &w;
			~Unlease() { w.scratch = nullptr; }
		} unlease{w};
		w.scratch = &lease.get();
		bool fresh = !t_start.has_value();
		N t0 = fresh ? beam->kappa.like(0.0) : *t_start;
		std::optional<Candidate<N>> resume; // the last candidate processed before an in-place reseed
		N t_prev = t0;
		bool keep_cursor = false; // K4: the seed restart keeps the listed cursor
		int sig0 = w.sig; // (every loop passes something significant: checked at its end)
		while (!w.done) {
			if (!resume) {
				sig0 = w.sig; // (a loop resumed in place after a reseed began before)
			}
			// Stage 124: past what the caller reads (and two events more): done (the
			// events so far are the whole walk's: it is decided step by step)
			if (past_horizon(w)) {
				w.done = true;
				return;
			}
			if (w.record_poles) {
				w.pole_trace.emplace_back(beam, w.pole ? w.pole->exact() : std::optional<exact::ExactPole>{});
				w.exact_trace.push_back(w.Q ? (w.pole && w.pole->exact().has_value()) : w.line != nullptr);
				w.known_trace.push_back(w.known);
			}
			std::vector<Candidate<N>> &cands = w.scratch->cands; // (Stage 124: the walk's buffers, refilled each step)
			candidates(w, *beam, depart, fresh, t0, keep_cursor, cands);
			keep_cursor = false;
			std::vector<uint8_t> &gw = w.scratch->why; // (reused across the walk's steps)
			std::vector<double> &gp = w.scratch->gaps;
			gaps(w, *beam, cands, gp, w.record_why ? &gw : nullptr);
			std::optional<Next> next;
			bool restart = false;
			// (Stage 134: resumed past the candidate processed last, by order -- the
			// recomputed list follows the walk's mode, so it may list others before it)
			size_t ci = 0;
			if (resume) {
				while (ci < cands.size() && !after_candidate(*resume, cands[ci], t0)) {
					ci++;
				}
				resume.reset();
			}
			Mode seen_mode = w.mode;
			int seen_index = w.plan_index;
			Flags seen_flags = w.flags;
			while (true) {
				// Stage 134: where the walk's state changed (its flags, an entry consumed,
				// the cursor), the candidates are rebuilt for the rest of the loop (after
				// its last candidate too: the cursor last in PLANNED mode)
				if (ci > 0 && (w.mode != seen_mode || w.plan_index != seen_index || !(w.flags == seen_flags))) {
					const Candidate<N> prev = cands[ci - 1];
					std::vector<Candidate<N>> rebuilt;
					candidates(w, *beam, depart, fresh, t0, false, rebuilt, true);
					cands.resize(ci);
					for (const Candidate<N> &c : rebuilt) {
						// (the cursor at the current point too: it is reached there, as late_cursor)
						const bool cursor = c.surf == nullptr && !c.inf && !c.origin;
						if (after_candidate(prev, c, t0) || (!c.rehit && cursor && !before(c.t, prev.t, t0))) {
							cands.push_back(c);
						}
					}
					gaps(w, *beam, cands, gp, w.record_why ? &gw : nullptr);
				}
				seen_mode = w.mode;
				seen_index = w.plan_index;
				seen_flags = w.flags;
				if (ci >= cands.size()) {
					break;
				}
				// Stage 127: past what the caller reads within a step too (a straight beam
				// crosses many carriers in one step; the events so far are complete)
				if (past_horizon(w)) {
					w.done = true;
					return;
				}
				const Candidate<N> cand = cands[ci];
				const N t = cand.t;
				const Surf<N> *surf = cand.surf;
				const bool rehit = cand.rehit;
				if (w.mode == Mode::Physical) {
					// B5: the point this candidate is at
					const bool wrap = cand.inf && !cand.pt;
					if (w.point && (wrap || w.point->wrap || cand.t.cmp(w.point->t) != 0 || cand.rehit != w.point->rehit)) {
						point_close(w);
					}
					if (!w.point && (surf != nullptr || cand.inf || cand.origin) && w.plan_matched && w.on_plan && !w.cursor_done) {
						w.point = PointGroup<N>{t, rehit, wrap, w.flags, w.plan_index, false, false};
					}
				}
				if (surf != nullptr && !cand.inf && !surf->present(w.flags)) {
					if (w.point && surf->present(w.point->flags)) {
						// absent for the walk, present for the planned shadow (B5)
						const Classified ca = classify(*w.scene, *beam, t, *surf, rehit && fresh, cand.pt, w.flags);
						point_step(w, surf, false, ca.left, ca.pt);
						if (point_sync(w)) {
							late_cursor(w, *beam, cands, gp, gw, ci, t, t0);
						}
					}
					ci++; // absent under the current flags (Stage 107): no crossing at all
					continue;
				}
				const bool is_cursor = surf == nullptr && !cand.inf && !cand.origin; // (Stage 131: the cursor counts toward neither limit)
				if (!is_cursor) {
					w.count++;
				}
				if (!is_cursor && (w.count > w.max_events || ((cand.inf || cand.origin) && w.sig + 1 > w.max_sig))) {
					// cut at the next crossing: drawn up to it, no effect (at the
					// wrap: a max at infinity, no point)
					Event<N> &me = emit(w, EventKind::Max, beam, t_prev, (cand.inf && !cand.pt) ? std::nullopt : std::optional<N>(t), INF_D);
					if (cand.inf) {
						me.surface_id = INFINITY_ID;
						if (cand.pt) {
							me.point = cand.pt;
						}
					} else if (cand.pt) {
						me.point = cand.pt;
					} else if (rehit && fresh) {
						me.point = beam->p;
					}
					w.done = true;
					return;
				}
				if (cand.origin) {
					// Stage 131: the passage of the ray's origin -- significant; unplanned
					// before the cursor, a divergence, like infinity's
					w.sig++;
					Event<N> &oe = emit(w, EventKind::Origin, beam, t_prev, t, gp[ci]);
					oe.why = w.record_why ? gw[ci] : uint8_t(0);
					oe.point = cand.pt;
					oe.flags = w.flags;
					t_prev = t;
					if (origin_passage(w, *beam)) {
						late_cursor(w, *beam, cands, gp, gw, ci, t, t0);
					}
					ci++;
					continue;
				}
				if (cand.inf) {
					w.sig++; // (Stage 131: the passage of infinity is significant)
					// the wrap through infinity (Stage 88): consumes the infinity
					// entry when it is next in the plan, in both modes
					bool consumed = w.plan_index < int(w.plan.size()) && w.plan[size_t(w.plan_index)].is_infinity();
					if (consumed) {
						w.plan_index++;
					}
					Event<N> &ie = emit(w, EventKind::Infinity, beam, t_prev, cand.pt ? std::optional<N>(t) : std::nullopt, gp[ci]);
					ie.why = w.record_why ? gw[ci] : uint8_t(0);
					ie.surface_id = INFINITY_ID;
					if (cand.pt) {
						ie.point = cand.pt; // the frame's image of infinity, exact by provenance
					}
					ie.applied = consumed;
					ie.flags = w.flags;
					t_prev = t;
					if (w.mode == Mode::Physical) {
						point_step(w, nullptr, true, false, beam->p);
						consumed = point_sync(w) || consumed;
						if (!consumed && !w.cursor_done) {
							// Stage 131 (the user): an unplanned passage of infinity before
							// the cursor is a divergence -- the walk leaves the plan there
							w.left_plan = true;
							w.wrapped_off = true;
							w.plan_matched = false;
							w.on_plan = false;
							w.events.back().off_plan = true;
						}
					}
					if (consumed) {
						late_cursor(w, *beam, cands, gp, gw, ci, t, t0);
					}
					ci++;
					continue;
				}
				if (surf == nullptr && w.mode == Mode::Physical && w.wrapped_off) {
					ci++; // (Stage 131: the walk left the plan by an unplanned wrap: no cursor after it)
					continue;
				}
				if (surf == nullptr) {
					Event<N> &ce = emit(w, EventKind::Cursor, beam, t_prev, t, gp[ci]);
					ce.why = w.record_why ? gw[ci] : uint8_t(0);
					if (cand.pt) {
						ce.point = cand.pt; // (a known point: exact by provenance)
					}
					ce.anchored = w.anchored;
					t_prev = t;
					w.cursor_done = true;
					if (w.stop_at_cursor) {
						w.done = true;
						return;
					}
					w.mode = w.post_mode;
					// the post-cursor continuation is the sensitive part
					w.anchored = false;
					if (!w.seeded) {
						w.seeded = true;
						BeamPtr<N> sb = Backend<N>::seed(beam);
						if (sb != beam) {
							beam = sb;
							resume = cand;
							restart = true;
							break;
						}
					}
					ci++;
					continue;
				}
				const Classified cl = classify(*w.scene, *beam, t, *surf, rehit && fresh, cand.pt, w.flags);
				const CN<N> pt = cl.pt;
				bool on_seg = cl.on_seg;
				const bool left = cl.left;
				// Stage 138 (the user): a point two or more present surfaces hold -- a
				// junction, a proper crossing, a planned piece's end -- is solid; if two
				// or more of them have a consequence there (an effect, a state change, a
				// landing effect, a target: a candidate's side), no one owns the hit: the
				// physical walk stops, nothing fires, the event carries the lowest id (the
				// planned walk computes its entries' surfaces alone: it follows its plan,
				// and the walks diverge). One alone owns it. A lone endpoint is a graze.
				std::vector<const Surf<N> *> &others = w.scratch->shared_surfs;
				if (on_seg) {
					shared_surfaces(w, cands, ci, t, pt, *surf, others);
				} else {
					others.clear();
				}
				const bool fb = on_seg && (cl.at_ep == 0 || !others.empty());
				if (!others.empty() && w.mode == Mode::Physical) {
					const CN<N> tang = beam->tangent_at(t);
					const Surf<N> *stop = nullptr;
					bool stop_left = false;
					int owners = 0;
					for (size_t k = 0; k <= others.size(); k++) {
						const Surf<N> *s = k == 0 ? surf : others[k - 1];
						const bool sl = k == 0 ? left : geom::side_left(s->seg, pt, tang);
						const bool consequence = s->is_target || s->effect(sl, w.flags) != EffectType::Pass || !s->state_changes(sl, w.flags).empty()
							|| s->landing(sl, w.flags);
						if (consequence) {
							owners++;
							if (stop == nullptr || s->id < stop->id) {
								stop = s;
								stop_left = sl;
							}
						}
					}
					if (owners >= 2) {
						if (w.sig + 1 > w.max_sig) {
							Event<N> &me = emit(w, EventKind::Max, beam, t_prev, t, INF_D);
							me.point = pt;
							w.done = true;
							return;
						}
						w.sig++;
						Event<N> &se = emit(w, EventKind::Hit, beam, t_prev, t, gp[ci]);
						se.why = w.record_why ? gw[ci] : uint8_t(0);
						se.surface_id = stop->id;
						se.left = stop_left;
						se.state = false;
						se.fired = false;
						se.flags = w.flags;
						se.applied = true;
						se.terminal = true;
						se.on_segment = true;
						se.rehit = rehit;
						if ((rehit && fresh) || cand.pt) {
							se.point = pt;
						}
						if (w.mode == Mode::Physical && w.point) {
							point_close(w);
						}
						w.done = true;
						return;
					}
				}
				const EffectType eff = surf->effect(left, w.flags);
				const std::vector<StateChangeDef> &changes = surf->state_changes(left, w.flags);
				const bool has_change = !changes.empty();
				const bool plan_hit = w.plan_index < int(w.plan.size()) && w.plan[size_t(w.plan_index)].surface_id == surf->id
					&& w.plan[size_t(w.plan_index)].left == left;
				// Stage 108: a planned piece -- physically, only a hit inside its span is
				// the planned one; on the planned path the entry is consumed as before
				// and its hit is off its segment outside the span
				const bool in_piece = plan_hit && in_span(w, w.plan_index, pt);
				const bool plan_match = w.mode == Mode::Physical ? in_piece : plan_hit;
				if (w.mode != Mode::Physical && plan_hit && !in_piece) {
					on_seg = false;
				}
				const EffectType plan_do = (plan_match && (is_transformative(eff) || is_projective(eff))) ? eff : EffectType::Pass;
				const EffectType phys_do = fb ? eff : EffectType::Pass;
				const bool plan_state = plan_match && has_change;
				const bool phys_state = fb && has_change;
				EffectType do_ = EffectType::Pass;
				bool fires_state = false;
				bool diverged = false;
				const bool ghosted = surf->ghosted(left, w.flags);
				bool fired = false;
				if (w.mode == Mode::Physical) {
					do_ = phys_do;
					fires_state = phys_state;
					fired = fb && !ghosted;
					// B1 (Stage 110): what counts is the frame each walk leaves -- the beam
					// and the flags. A write of the value a flag already has changes no frame.
					const Flags phys_after = phys_state ? w.flags.applied(changes) : w.flags;
					const Flags plan_after = plan_state ? w.flags.applied(changes) : w.flags;
					if (w.anchored && !(phys_do == plan_do && phys_after == plan_after)) {
						diverged = true;
					}
					if (is_transformative(do_) || is_projective(do_)) {
						plan_bookkeeping(w, surf->id, pt);
					} else if (!(phys_after == w.flags)) {
						// B5: a flag change keeps the beam -- matched by the point's frame
						plan_bookkeeping(w, surf->id, pt, true);
					} else if (do_ == EffectType::Pass && plan_match && plan_do == EffectType::Pass && plan_after == w.flags) {
						// the planned walk consumes this entry without a frame change (a pass
						// side, a terminal crossed off its segment, a write of the current
						// value): so does the physical one, at the same event
						w.plan_index++;
					}
				} else {
					if (plan_match) {
						w.plan_index++;
					}
					do_ = plan_do;
					fires_state = plan_state;
					fired = plan_match && !ghosted;
				}
				if (do_ != EffectType::Pass || fires_state) {
					// Stage 131: a significant event past the limit -- cut here (drawn up to it, no effect)
					if (w.sig + 1 > w.max_sig) {
						Event<N> &me = emit(w, EventKind::Max, beam, t_prev, t, INF_D);
						me.point = pt;
						w.done = true;
						return;
					}
					w.sig++;
				}
				if (fires_state) {
					w.flags = w.flags.applied(changes);
				}
				Event<N> &e = emit(w, EventKind::Hit, beam, t_prev, t, gp[ci]);
				e.why = w.record_why ? gw[ci] : uint8_t(0);
				e.surface_id = surf->id;
				e.left = left;
				e.state = fires_state;
				e.fired = fired;
				e.flags = w.flags;
				e.applied = do_ != EffectType::Pass;
				e.terminal = do_ == EffectType::Terminal;
				e.on_segment = on_seg;
				e.rehit = rehit;
				if ((rehit && fresh) || cand.pt) {
					e.point = pt;
				}
				t_prev = t;
				if (w.mode == Mode::Physical && w.point) {
					if (is_transformative(do_) || is_projective(do_)) {
						point_close(w);
					} else {
						point_step(w, surf, false, left, pt);
						point_sync(w);
					}
				}
				if (do_ == EffectType::Terminal) {
					w.done = true;
					return;
				}
				if (diverged) {
					w.anchored = false;
					if (do_ == EffectType::Pass && !w.seeded) {
						// physical passes where planned applies: the continuation is this same beam
						w.seeded = true;
						BeamPtr<N> sb = Backend<N>::seed(beam);
						if (sb != beam) {
							beam = sb;
							resume = cand;
							// K4: the recomputed loop keeps a cursor this loop still lists
							for (size_t k = ci + 1; k < cands.size(); k++) {
								keep_cursor = keep_cursor || (cands[k].surf == nullptr && !cands[k].inf && !cands[k].origin);
							}
							restart = true;
							break;
						}
					}
				}
				if (is_transformative(do_)) {
					const bool pt_exact = (cand.pt || (rehit && fresh)) && known_index(w.known, pt) >= 0;
					const size_t depth = w.stack.size();
					next = apply(w, beam, t, *surf, do_, depart, fresh, t0, rehit, pt, pt_exact);
					// the beam departs from Q: an effect fired at Q (a pinned crossing,
					// or the departure re-hit when the beam had departed from Q)
					w.depart_q = (cand.at_q || (rehit && w.depart_q)) && w.pole && w.pole->exact().has_value(); // (A.3: the exact pole forced only at Q)
					Event<N> &ev = w.events.back();
					ev.popped = w.stack.size() < depth;
					ev.teleport = surf->portal || (ev.popped && w.scene->find(next->depart)->portal);
					if (diverged && !w.seeded) {
						w.seeded = true;
						next->beam = Backend<N>::seed(next->beam);
					}
					break;
				}
				if (is_projective(do_)) {
					const bool pt_exact2 = (cand.pt || (rehit && fresh)) && known_index(w.known, pt) >= 0;
					next = project(w, beam, t, *surf, left, pt, pt_exact2);
					w.depart_q = false;
					w.events.back().projective = true;
					if (!w.anchored && !w.seeded) {
						w.seeded = true;
						next->beam = Backend<N>::seed(next->beam);
					}
					break;
				}
				late_cursor(w, *beam, cands, gp, gw, ci, t, t0);
				ci++;
			}
			if (restart) {
				continue; // (a seed restart continues the same point)
			}
			if (w.mode == Mode::Physical) {
				point_close(w);
			}
			if (!next) {
				// nothing fired on the full loop (or the loop only changed the state:
				// the next one differs, e.g. a ghost toggler re-hit). Stage 131: no loop
				// detection -- the arrow goes round again and the limits end it (every
				// loop passes infinity and the ray's origin; Stage 136: always on).
				// back at the departure: when it is the ray's origin (the player, a
				// projected ray's start), its passage -- a significant event (the user);
				// unplanned before the cursor, a divergence: the walk leaves the plan.
				// Another departure is no event: the beam goes round again. Stage 134 (the
				// user): no loop detection at all -- every loop passes the ray's origin and
				// the frame's infinity, both significant, so the limits end the walk
				if (!w.O || !geom::eq(*w.O, fresh ? beam->p : beam->at(t0))) {
					if (w.sig == sig0) {
						RC_ASSERT(false, "a loop that passed nothing significant (the origin and infinity are on every beam)");
						w.done = true; // (release: recover, the walk ends)
						return;
					}
					// (the next event runs on from the last one, across the departure)
					continue;
				}
				w.count++;
				if (w.count > w.max_events || w.sig + 1 > w.max_sig) {
					Event<N> &me = emit(w, EventKind::Max, beam, t_prev, t0, INF_D);
					if (fresh) {
						me.point = beam->p;
					}
					w.done = true;
					return;
				}
				w.sig++;
				Event<N> &oe = emit(w, EventKind::Origin, beam, t_prev, t0, INF_D);
				if (fresh) {
					oe.point = beam->p;
				}
				oe.flags = w.flags;
				origin_passage(w, *beam);
				t_prev = t0; // (the next arc runs on from the origin)
				continue;
			}
			beam = next->beam;
			if (aim_known && aim_known->j > 0) {
				add_aim_known(w, *beam); // (Stage 124: the beam after the member's own entries)
			}
			depart = next->depart;
			t0 = next->t0;
			fresh = next->fresh;
			t_prev = t0;
		}
	}

	// Stage 132: the passage of the ray's origin (the walk's last event) consumes an
	// origin entry next in the plan, in both walks (applied, like infinity's);
	// unplanned before the cursor it is a divergence (Stage 131). True: consumed.
	static bool origin_passage(Walk<N> &w, const Beam<N> &beam) {
		bool consumed = w.plan_index < int(w.plan.size()) && w.plan[size_t(w.plan_index)].is_origin();
		if (consumed) {
			w.plan_index++;
		}
		w.events.back().applied = consumed;
		if (w.mode == Mode::Physical) {
			point_step(w, nullptr, false, false, beam.p, true);
			consumed = point_sync(w) || consumed;
			if (!consumed && !w.cursor_done) {
				w.left_plan = true;
				w.wrapped_off = true;
				w.plan_matched = false;
				w.on_plan = false;
				w.events.back().off_plan = true;
			}
		}
		return consumed;
	}

	// The plan completed at the event at t (index ci) without a frame change
	// (an entry that resolves to a pass / state change, an unapplied
	// projective, or the infinity entry): the cursor is on this same beam -- a
	// candidate if it lies ahead, inserted in order among the remaining ones.
	static void late_cursor(Walk<N> &w, const Beam<N> &beam, std::vector<Candidate<N>> &cands, std::vector<double> &gp, std::vector<uint8_t> &gw,
			size_t ci, const N &t, const N &t0) {
		if (!w.cursor_done && w.cursor && w.plan_index >= int(w.plan.size()) && w.plan_matched && w.on_plan && !(w.point && w.point->pending)
				&& std::none_of(cands.begin(), cands.end(), [](const Candidate<N> &c) { return c.surf == nullptr && !c.inf && !c.origin; })) {
			const N tc = Backend<N>::param_of(beam, *w.cursor);
			if (!before(tc, t, t0)) {
				size_t j = ci + 1;
				// after everything strictly before it; ties: the cursor first
				while (j < cands.size() && !cands[j].rehit && before(cands[j].t, tc, t0)) {
					j++;
				}
				cands.insert(cands.begin() + long(j), Candidate<N>{tc, nullptr, false, std::nullopt, -1});
				gp.insert(gp.begin() + long(j), INF_D);
				if (w.record_why) {
					gw.insert(gw.begin() + long(j), uint8_t(WHY_NONE)); // (Stage 123: the margins' classes stay aligned)
				}
				if (known_index(w.known, *w.cursor) < 0) {
					w.known.push_back(Known<N>{*w.cursor, tc, -1, nullptr});
				}
			}
		}
	}

	// Stage 106: the pop point is the restored beam's departure point, tagged
	// like a push's. A known point that is the pop point -- by data (pt_exact
	// and equal to q) or by its parameter on this beam (t == t_res: the same
	// beam and the same parameter are the same point) -- gets skip = sid and
	// t = t_res; an exact pop point not yet known is added (not for a portal:
	// its mapped-back point is a rounded computation). Without the tag a known
	// point at the pop point snaps the departing carrier's other root onto it (K2).
	static void tag_departure(std::vector<Known<N>> &known, const CN<N> &q, const N &t_res, int sid, bool pt_exact, bool portal) {
		bool found = false;
		for (Known<N> &k : known) {
			if ((pt_exact && geom::eq(k.p, q)) || k.t.cmp(t_res) == 0) {
				k.skip = sid;
				k.t = t_res;
				found = true;
			}
		}
		if (!found && pt_exact && !portal) {
			known.push_back(Known<N>{q, t_res, sid, nullptr});
		}
	}

	// Apply surf's effect at t: the continuation.
	Next apply(Walk<N> &w, const BeamPtr<N> &beam, const N &t, const Surf<N> &surf, EffectType etype, int depart, bool fresh,
			const N &t0, bool rehit, const CN<N> &pt, bool pt_exact) const {
		if (!w.stack.empty() && w.stack.back().pop_id == surf.id && w.stack.back().etype == map_kind(surf, etype)) {
			StackEntry<N> top = std::move(w.stack.back());
			w.stack.pop_back();
			const Surf<N> &src = *w.scene->find(top.sid);
			// the point on the restored beam: T^-1(pt) for a portal, pt itself otherwise
			const CN<N> q = src.portal ? effects::portal_point(pt, src, true) : pt;
			N t_res = top.t_hit;
			if (rehit) {
				t_res = top.t_hit;
			} else if (fresh && depart == surf.id) {
				t_res = top.other;
			} else {
				const double dh = top.beam->at(top.t_hit).sub(q).abs2().to_float();
				const double dd = top.beam->at(top.other).sub(q).abs2().to_float();
				t_res = dh < dd ? top.t_hit : top.other;
			}
			w.known = std::move(top.known);
			tag_departure(w.known, q, t_res, top.sid, pt_exact, src.portal);
			w.line = top.line;
			w.rec = top.rec;
			w.Q = top.q;
			w.O = top.o;
			w.pole = top.pole;
			w.pole_serial = top.pole_serial;
			w.through_c = top.through_c;
			return Next{w.seeded ? Backend<N>::reseed(top.beam) : top.beam, top.sid, t_res, false};
		}
		const N other = other_root(*beam, surf, depart, t0, t, pt);
		const int pop_id = surf.portal ? surf.pair : surf.id;
		w.stack.push_back(StackEntry<N>{beam, surf.id, t, other, w.known, w.line, pop_id, map_kind(surf, etype), w.Q, w.pole, w.pole_serial, w.rec,
			w.through_c, w.O});
		set_pole(w, pole_after(w, surf));
		w.Q = effects::map_point(w.Q, surf, false);
		w.O = effects::map_point(w.O, surf, false);
		const std::vector<Known<N>> old_known = w.known;
		// the surface the new beam departs from, and the departure point
		const Surf<N> &dsurf = surf.portal ? *w.scene->find(surf.pair) : surf;
		const CN<N> exit = surf.portal ? effects::portal_point(pt, surf) : pt;
		Next res{nullptr, dsurf.id, beam->kappa.like(0.0), true};
		if (w.anchored && w.chain) {
			// anchored: the exact beam of the next plan frame; it departs dsurf at
			// the root nearest the exit point
			const BeamPtr<N> nb = w.chain->beams[size_t(w.plan_index)];
			std::optional<N> best;
			double best_d = INF_D;
			for (const N &r : geom::hits(*nb, dsurf.seg, false)) {
				const double d = nb->at(r).sub(exit).abs2().to_float();
				if (d < best_d) {
					best_d = d;
					best = r;
				}
			}
			if (!best) {
				best = Backend<N>::param_of(*nb, exit);
			}
			res = Next{nb, dsurf.id, *best, false};
		} else {
			const CN<N> hp = Backend<N>::exact_point(*beam, t, pt);
			res = Next{std::make_shared<const Beam<N>>(effects::apply(*beam, t, surf, &hp)), dsurf.id, beam->kappa.like(0.0), true};
		}
		// K15 (Stage 114): an inversion centred on a point the beam passes by
		// provenance (a circle normal's centre, its exit) maps the line to itself:
		// straight, the centre becoming the frame's image of infinity -- never the
		// circle of enormous radius the rounded beam inverts into. The anchored
		// beam is built straight by build_simple.
		const Surf<N> *through_c = w.through_c;
		w.through_c = nullptr;
		if (through_c != nullptr && !surf.line && !surf.portal && !w.anchored && same_exact_center(*through_c, surf)) {
			res.beam = std::make_shared<const Beam<N>>(Beam<N>{res.beam->p, res.beam->tau, res.beam->kappa.like(0.0)});
		}
		if (surf.portal) {
			portal_known(w, surf, pt, pt_exact, old_known, res);
			return res;
		}
		// known points of the new beam: its departure point (if exact), and the
		// old beam's known points on surf's carrier (fixed by the effect)
		w.known.clear();
		if (pt_exact) {
			w.known.push_back(Known<N>{pt, res.t0, surf.id, nullptr});
		}
		for (const Known<N> &k : old_known) {
			if (!geom::eq(k.p, pt) && incident(w, k.p, surf)) {
				w.known.push_back(Known<N>{k.p, Backend<N>::param_of(*res.beam, k.p), -1, k.incident}); // (the same point: the same carriers)
			}
		}
		if (w.line && surf.line) {
			w.line = w.line->reflected(surf.sx, surf.sy, surf.ex, surf.ey);
			w.rec = exact::BeamRec::from_line(w.line);
			add_line_endpoints(w, *res.beam);
		} else if (w.line && line_through_center(*w.line, surf)) {
			res.beam = Backend<N>::straightened(res.beam);
			std::vector<Known<N>> kept;
			for (const Known<N> &k : w.known) {
				if (k.skip == surf.id) {
					kept.push_back(k);
				}
			}
			w.known = kept;
			add_line_endpoints(w, *res.beam);
		} else {
			w.line = nullptr;
			// the circle the beam becomes: the old one under the map (Perf P2b)
			if (w.rec) {
				w.rec = surf.line ? w.rec->reflected(surf.sx, surf.sy, surf.ex, surf.ey) : w.rec->inverted(surf.exact_circle().C, surf.exact_circle().rw2);
			}
			if (!surf.line) {
				for (const Known<N> &k : old_known) {
					if (is_center(k.p, surf)) {
						// the old beam passes exactly through the inversion center: its image is exactly a line
						res.beam = Backend<N>::straightened(res.beam);
						break;
					}
				}
			}
		}
		return res;
	}

	// Exact: the point is the center of surf's circle.
	static bool is_center(const CN<N> &k, const Surf<N> &surf) {
		const double kx = k.fx();
		const double ky = k.fy();
		if (surf.full) {
			return kx == surf.cx && ky == surf.cy;
		}
		const auto &ec = surf.exact_circle();
		const double e = 2.0 * ecc(ec);
		if (std::fabs(kx - ec.fcx) > e || std::fabs(ky - ec.fcy) > e) {
			return false;
		}
		return exact::sub(exact::mul(exact::of(kx), ec.C[2]), ec.C[0]).is_zero()
			&& exact::sub(exact::mul(exact::of(ky), ec.C[2]), ec.C[1]).is_zero();
	}

	// Exact: two circles share their centre (on the data: the homogeneous circumcentres).
	static bool same_exact_center(const Surf<N> &a, const Surf<N> &b) {
		const auto &ea = a.exact_circle();
		const auto &eb = b.exact_circle();
		return exact::sub(exact::mul(ea.C[0], eb.C[2]), exact::mul(eb.C[0], ea.C[2])).is_zero()
			&& exact::sub(exact::mul(ea.C[1], eb.C[2]), exact::mul(eb.C[1], ea.C[2])).is_zero();
	}

	static bool line_through_center(const exact::Line &L, const Surf<N> &surf) {
		if (surf.full) {
			return L.contains(surf.cx, surf.cy);
		}
		const auto &ec = surf.exact_circle();
		return L.contains_h(ec.C, ec.fcx, ec.fcy, ecc(ec));
	}

	// The projective fired at its back-propagated hit point H: of the beam's
	// crossings of surf, the one at pt is the one nearest H.
	static bool at_planned_hit(const Beam<N> &beam, const Surf<N> &surf, const CN<N> &pt, const CN<N> *H) {
		if (H == nullptr) {
			return false;
		}
		const std::vector<N> rs = geom::hits(beam, surf.seg, false);
		if (rs.empty()) {
			return false;
		}
		int near_pt = -1;
		int near_h = -1;
		double d_pt = INF_D;
		double d_h = INF_D;
		for (size_t i = 0; i < rs.size(); i++) {
			const CN<N> q = beam.at(rs[i]);
			const double a = q.sub(pt).abs2().to_float();
			const double b = q.sub(*H).abs2().to_float();
			if (a < d_pt) {
				d_pt = a;
				near_pt = int(i);
			}
			if (b < d_h) {
				d_h = b;
				near_h = int(i);
			}
		}
		return near_pt == near_h;
	}

	Next project(Walk<N> &w, const BeamPtr<N> &beam, const N &t, const Surf<N> &surf, bool left, const CN<N> &pt, bool pt_exact) const {
		w.stack.clear();
		w.Q = std::nullopt;
		set_pole(w, nullptr);
		w.epoch++;
		const CN<N> *H = nullptr;
		if (w.chain) {
			auto it = w.chain->hits.find(w.plan_index - 1);
			if (it != w.chain->hits.end()) {
				H = &it->second;
			}
		}
		const bool at_h = w.on_plan && w.chain && at_planned_hit(*beam, surf, pt, H);
		{
			static const bool trace = std::getenv("RC_REALIZE_TRACE") != nullptr;
			if (trace) {
				std::fprintf(stderr, "project: pt (%.6g, %.6g) H %s on_plan %d chain %d anchored %d at_h %d exact %d known %zu mode %d plan_index %d cursor (%.6g, %.6g)\n", pt.fx(), pt.fy(),
					H ? (std::to_string(H->fx()) + "," + std::to_string(H->fy())).c_str() : "none", int(w.on_plan), int(w.chain != nullptr), int(w.anchored), int(at_h),
					int(pt_exact), w.known.size(), int(w.mode), w.plan_index, w.cursor ? w.cursor->fx() : 0.0, w.cursor ? w.cursor->fy() : 0.0);
			}
		}
		if (!at_h) {
			w.on_plan = false;
		}
		if (at_h && w.anchored && w.plan_index < int(w.chain->beams.size())) {
			// planned through the projective (Stage 87): the next sub-chain's anchored beam
			const BeamPtr<N> ab = w.chain->beams[size_t(w.plan_index)];
			std::optional<N> best;
			double best_d = INF_D;
			for (const N &r : geom::hits(*ab, surf.seg, false)) {
				const double d = ab->at(r).sub(pt).abs2().to_float();
				if (d < best_d) {
					best_d = d;
					best = r;
				}
			}
			if (!best) {
				best = Backend<N>::param_of(*ab, pt);
			}
			w.known.clear();
			if (pt_exact) {
				w.known.push_back(Known<N>{pt, *best, surf.id, nullptr});
			}
			auto el = w.chain->exit_lines.find(w.plan_index - 1);
			w.line = el == w.chain->exit_lines.end() ? nullptr : el->second;
			w.rec = exact::BeamRec::from_line(w.line);
			if (w.line) {
				add_line_endpoints(w, *ab);
			}
			// (a new ray: its origin is its departure -- a circle normal's, the circle's centre)
			w.O = surf.effect(left) == EffectType::CircleNormalProjection ? surf.seg.circ->c : ab->at(*best);
			return Next{ab, surf.id, *best, false};
		}
		const CN<N> hp = Backend<N>::exact_point(*beam, t, pt);
		BeamPtr<N> nb = std::make_shared<const Beam<N>>(effects::project(*beam, t, surf, left, &hp));
		// (a new ray: its origin is its departure -- a circle normal's, the circle's
		// centre, which every ray of the projection passes: Stage 131, the user)
		w.O = surf.effect(left) == EffectType::CircleNormalProjection ? surf.seg.circ->c : nb->p;
		w.anchored = false;
		w.known.clear();
		if (pt_exact) {
			w.known.push_back(Known<N>{pt, beam->kappa.like(0.0), surf.id, nullptr});
		}
		w.line = nullptr;
		w.rec = nullptr;
		w.through_c = surf.effect(left) == EffectType::CircleNormalProjection ? &surf : nullptr; // K15
		if (pt_exact) {
			// an exact data line: through pt along the exact axis
			const EffectType etype = surf.effect(left);
			const Projective &pr = surf.proj(left);
			const double px = pt.fx();
			const double py = pt.fy();
			exact::Line3 L;
			if (etype == EffectType::LineNormalProjection) {
				const BigFloat a = exact::sub(exact::of(surf.ex), exact::of(surf.sx));
				const BigFloat b = exact::sub(exact::of(surf.ey), exact::of(surf.sy));
				L = {a, b, exact::add(exact::mul(a, exact::of(px)), exact::mul(b, exact::of(py)))};
			} else if (etype == EffectType::CircleNormalProjection) {
				L = exact::line_through_h(px, py, surf.exact_circle().C);
			} else {
				L = exact::line_from_dir(px, py, pr.nx, pr.ny);
			}
			w.line = exact::Line::from_exact(L);
			w.rec = exact::BeamRec::from_line(w.line);
			add_line_endpoints(w, *nb);
		}
		if (at_h) {
			auto el = w.chain->exit_lines.find(w.plan_index - 1);
			if (el != w.chain->exit_lines.end()) {
				w.line = el->second;
				w.rec = exact::BeamRec::from_line(w.line);
				add_line_endpoints(w, *nb);
			}
		}
		return Next{nb, surf.id, beam->kappa.like(0.0), true};
	}

	// T(k) of a portal as exact data, or nullopt.
	static std::optional<CN<N>> portal_image(const Walk<N> &w, const Surf<N> &surf, const CN<N> &k) {
		const Surf<N> &pair = *w.scene->find(surf.pair);
		const int which = geom::endpoint_at(surf.seg, k);
		if (which == 1) {
			return pair.seg.s;
		}
		if (which == 2) {
			return pair.seg.e;
		}
		if (surf.theta != 0.0) {
			return std::nullopt;
		}
		const BigFloat ex = exact::add(exact::of(k.fx()), exact::of(surf.dx));
		const BigFloat ey = exact::add(exact::of(k.fy()), exact::of(surf.dy));
		const double fx = ex.to_float();
		const double fy = ey.to_float();
		if (exact::of(fx).cmp(ex) != 0 || exact::of(fy).cmp(ey) != 0) {
			return std::nullopt;
		}
		return CN<N>{k.re.like(fx), k.re.like(fy)};
	}

	// Known points and exact line after a portal push (the rigid image).
	static void portal_known(Walk<N> &w, const Surf<N> &surf, const CN<N> &pt, bool pt_exact, const std::vector<Known<N>> &old_known,
			const Next &res) {
		std::vector<Known<N>> known;
		if (pt_exact) {
			std::optional<CN<N>> h2 = portal_image(w, surf, pt);
			if (h2) {
				known.push_back(Known<N>{*h2, res.t0, surf.pair, nullptr});
			}
		}
		for (const Known<N> &k : old_known) {
			if (geom::eq(k.p, pt)) {
				continue;
			}
			std::optional<CN<N>> k2 = portal_image(w, surf, k.p);
			if (k2 && known_index(known, *k2) < 0) {
				known.push_back(Known<N>{*k2, Backend<N>::param_of(*res.beam, *k2), -1, nullptr});
			}
		}
		w.known = known;
		if (w.line && surf.theta == 0.0) {
			w.line = w.line->translated(surf.dx, surf.dy);
			w.rec = exact::BeamRec::from_line(w.line);
			add_line_endpoints(w, *res.beam);
		} else {
			w.line = nullptr;
			w.rec = (w.rec && surf.theta == 0.0) ? w.rec->translated(surf.dx, surf.dy) : nullptr;
		}
	}
};

} // namespace rc
