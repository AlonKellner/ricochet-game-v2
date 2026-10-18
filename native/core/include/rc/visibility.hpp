// The visibility region (Stage 110, GAME_SPEC 15): the cursor positions at
// which the preview's planned and physical paths agree up to the cursor -- the
// merged steps before the cursor are ALIGNED and the cursor is reached.
//
// Construction. A cursor's aim at the player is one direction d (the tangent
// of the aim beam at the player); every cursor with the same aim lies on one
// beam of the plan's final frame, the member of d: the circle through the
// player's image P_K, the cursor and the frame's infinity Q_K (a line when
// Q_K is infinity), run the way the plan's realization runs. The cursors of a
// member that are visible form intervals along it, from the departure point of
// the last plan entry (the player, for an empty plan) to the first event at
// which the physical walk changes the frame (the stop) or the event budget
// runs out, inside the member's arc owned by d (P_K to Q_K).
//
// Casts are traces: Tracer::solve with the cursor on a member, read by its
// events. Members through the points where the walk can change (segment
// endpoints, tangencies, proper crossings, every plan frame's points mapped to
// the final frame) bound cells: between two of them the walk's shape is the
// same, so a cell is bounded by its two members, the carrier its stops lie on
// and the carrier its departures lie on. Cells of adjacent members are merged
// into regions: closed loops of member arcs and carrier arcs.
//
// Supported plans: transformative, infinity, pass, terminal and state entries
// (projective entries: not yet).
#pragma once

#include "rc/beam_geom.hpp"
#include "rc/earcut.hpp"
#include "rc/exact.hpp"
#include "rc/solver.hpp"
#include "rc/tracer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rc {
namespace vis {

// Threads a region build casts on (Perf P3): RC_VIS_THREADS=<n> (1: in order on
// the caller's thread), else every core up to 8. The region is the same for any
// count (the casts are independent; the visibility digest pins it).
inline int vis_threads() {
	static const int n = [] {
		if (const char *e = std::getenv("RC_VIS_THREADS")) {
			return std::max(1, std::atoi(e));
		}
		const unsigned hw = std::thread::hardware_concurrency();
		return int(std::clamp(hw == 0 ? 1u : hw, 1u, 8u));
	}();
	return n;
}

constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double PI = 3.14159265358979323846;

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

inline double cross(const Pt &a, const Pt &b) { return a.x * b.y - a.y * b.x; }
inline double dot(const Pt &a, const Pt &b) { return a.x * b.x + a.y * b.y; }
inline double norm(const Pt &a) { return std::hypot(a.x, a.y); }
inline Pt unit(const Pt &a) {
	const double n = norm(a);
	return n > 0.0 ? Pt{a.x / n, a.y / n} : a;
}
inline Pt perp(const Pt &a) { return {-a.y, a.x}; }
inline Pt pt_of(const CN<F64> &z) { return {z.re.v, z.im.v}; }
inline CN<F64> cn_of(const Pt &p) { return CN<F64>{F64{p.x}, F64{p.y}}; }

// Where a visible interval of a member begins or ends.
enum class End : int {
	Near = 0, // the departure point of the last plan entry (on its surface)
	Stop,     // the first event at which the physical walk changes the frame
	Budget,   // the last event the hit budget leaves for a cursor
	PoleP,    // P_K, the player's image (excluded: a cursor there aims at nothing)
	PoleQ,    // Q_K, the frame's infinity (excluded)
	Loop,     // the walk's full loop back to its departure point
	TieNear,  // Stage 111: the other orientation's departure point: from there on its planned walk reaches the cursors, and it is the plan's realization
};

inline const char *end_name(End e) {
	switch (e) {
		case End::Near: return "near";
		case End::Stop: return "stop";
		case End::Budget: return "budget";
		case End::PoleP: return "P";
		case End::PoleQ: return "Q";
		case End::Loop: return "loop";
		case End::TieNear: return "tie-near";
	}
	return "?";
}

// Parameters on a member's beam ordered as its walk meets them from t0 (the
// solver's projective order: t > t0 increasing, the wrap at t = infinity, then
// t <= t0 increasing; t0 itself first).
using Key = std::pair<int, double>;
inline Key walk_key(double t, double t0) {
	if (t == t0) {
		return {-1, 0.0};
	}
	if (std::isinf(t)) {
		return {0, INF};
	}
	return t > t0 ? Key{0, t} : Key{1, t};
}

// The parameter's position on the projective circle, for picking a point
// between two parameters (never for a decision): s(t) = 2 atan(t) / pi in
// (-1, 1], infinity at 1.
inline double circ_pos(double t) { return std::isinf(t) ? 1.0 : 2.0 * std::atan(t) / PI; }
inline double from_circ_pos(double s) {
	s = std::fmod(s + 1.0, 2.0);
	if (s < 0.0) {
		s += 2.0;
	}
	s -= 1.0; // (-1, 1]
	if (s >= 1.0 || s <= -1.0) {
		return INF;
	}
	return std::tan(PI * s / 2.0);
}
// The parameter a fraction f of the way from ta to tb, walking forward.
inline double along(double ta, double tb, double f, bool tb_loop = false) {
	const double sa = circ_pos(ta);
	double sb = tb_loop ? sa + 2.0 : circ_pos(tb);
	if (!tb_loop && sb <= sa) {
		sb += 2.0;
	}
	return from_circ_pos(sa + f * (sb - sa));
}
// A parameter strictly between ta and tb, walking forward from ta (tb_loop:
// tb is ta after the full loop).
inline double between(double ta, double tb, bool tb_loop = false) {
	const double sa = circ_pos(ta);
	double sb = tb_loop ? sa + 2.0 : circ_pos(tb);
	if (!tb_loop && sb <= sa) {
		sb += 2.0;
	}
	return from_circ_pos((sa + sb) / 2.0);
}

// A visible interval: from mark a to mark b in walk order.
struct Mark {
	End kind = End::Near;
	double t = 0.0;
};
struct Interval {
	Mark a;
	Mark b;
};

// What one cast says about its member.
struct Cast {
	bool valid = false;     // the solver traced it
	bool alive = false;     // the plan is met (every entry consumed, both walks in one frame)
	bool cursor_ok = false; // the cast's own cursor: reached, both walks in one frame
	// both walks reach the last entry's crossing in one frame, and its own
	// decision diverges: a cursor at the departure point is still reached (at
	// once, before the divergence shows) -- the region's whisker on that carrier
	bool whisker = false;
	// Stage 111: the departure point of the last consumed surface entry (the
	// beam's own start; the near end when no infinity entry follows it)
	bool has_depart = false;
	Pt depart_pt;
	int depart_sid = -1;
	// Stage 111: in a half that is not the member's tie-choice, the other
	// orientation's planned reach -- its whole loop from its departure, or,
	// after a planned infinity last, from the frame's infinity round to its
	// departure -- is the plan's realization there: cut out of this cast
	bool chosen = true;       // Stage 111: this cast's orientation is the one the solver picks for its cursor
	int ek = -1;              // the planned event that consumed the last entry (debugging)
	bool met = false;
	bool has_tie = false;
	bool tie_after_q = false; // its reach begins at Q_K (an infinity entry consumed last)
	Pt tie_depart_pt;
	int tie_depart_sid = -1;
	End near_kind = End::PoleP;
	int near_sid = -1;
	Pt near_pt;
	End end_kind = End::Loop;
	int end_sid = -1;
	Pt end_pt;
	bool end_rehit = false;   // the stop is the departure point's re-hit: after the full loop
	std::vector<Interval> iv; // the member's visible intervals (walk order)
	Pt cursor;                // the cast's cursor
	Pt dir;                   // its aim direction at the player

	// the walk's shape: equal shapes bound one cell
	bool visible() const { return alive && !iv.empty(); }
	bool same_shape(const Cast &o) const {
		if (!visible() && !o.visible()) {
			// nothing visible but the departure points (if any) of the last entry
			return whisker == o.whisker && (!whisker || near_sid == o.near_sid);
		}
		if (valid != o.valid || visible() != o.visible() || iv.size() != o.iv.size()) {
			return false;
		}
		// the intervals' ends, and the carriers of the ends that bound one (a
		// stop beyond the owned arc changes nothing visible)
		for (size_t i = 0; i < iv.size(); i++) {
			if (iv[i].a.kind != o.iv[i].a.kind || iv[i].b.kind != o.iv[i].b.kind) {
				return false;
			}
			for (const End k : {iv[i].a.kind, iv[i].b.kind}) {
				if ((k == End::Near || k == End::Loop) && near_sid != o.near_sid) {
					return false;
				}
				if ((k == End::Stop || k == End::Budget) && end_sid != o.end_sid) {
					return false;
				}
			}
		}
		return true;
	}
	std::string shape() const {
		std::string s = valid ? (alive ? "alive" : (whisker ? "whisker" : "dead")) : "invalid";
		s += " near=" + std::string(end_name(near_kind)) + "@" + std::to_string(near_sid);
		s += " end=" + std::string(end_name(end_kind)) + "@" + std::to_string(end_sid) + " [";
		for (const Interval &i : iv) {
			s += std::string(end_name(i.a.kind)) + "-" + end_name(i.b.kind) + " ";
		}
		return s + "]";
	}
};

// A solver event as a cast reads it (float64).
struct CastEvent {
	EventKind kind = EventKind::Hit;
	int sid = -1;
	bool left = false;
	bool applied = false;
	bool rehit = false; // the departure point, re-hit after the full loop
	bool terminal = false;
	std::string flags;
	std::string frame;  // the frame after the event: epoch, transform stack, flags (Tracer::frame_id)
	bool has_pt = false;
	Pt pt;
};

template <Num N>
std::vector<CastEvent> cast_events(const std::vector<Event<N>> &events, const std::string &init_flags) {
	std::vector<CastEvent> out;
	out.reserve(events.size());
	std::vector<int> stack;
	std::string flags_now = init_flags;
	for (const Event<N> &e : events) {
		CastEvent c;
		c.kind = e.kind;
		c.sid = e.surface_id;
		c.left = e.left;
		c.applied = e.applied;
		c.rehit = e.rehit;
		c.terminal = e.terminal;
		c.flags = e.flags.canonical_key();
		// the frame as Tracer::to_path_events keeps it
		if (e.kind == EventKind::Hit && e.applied && !e.terminal) {
			if (e.projective) {
				stack.clear();
			} else if (e.popped) {
				if (!stack.empty()) stack.pop_back();
			} else {
				stack.push_back(e.surface_id);
			}
		}
		if (e.kind == EventKind::Hit) {
			flags_now = c.flags;
		}
		c.frame = std::to_string(e.epoch) + "|";
		for (int sid : stack) {
			c.frame += std::to_string(sid) + ",";
		}
		c.frame += "|" + flags_now;
		std::optional<CN<N>> p = e.point;
		if (!p && e.beam && e.kind != EventKind::Escape && e.kind != EventKind::Origin) {
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
		&& (a.kind != EventKind::Hit || a.flags == b.flags);
}


// A point where a cell's boundary turns: on member `member` at parameter t, or
// a pole, or where an unbounded member leaves the clip rectangle.
struct Vertex {
	Pt p;
	int member = -1;
	double t = 0.0;
	bool pole = false; // P_K or a finite Q_K: never in the region
	bool inf = false;  // Q_K at infinity on a straight member (resolved by clipping)
	int inf_sign = 0;  // an infinite crossing with a parallel carrier: the member's end it lies at (+1, -1)
	bool clip = false; // on the clip rectangle
};

enum class EdgeKind : int { Member = 0, Carrier, Bounds };

// A boundary piece: the segment a -> b, or the circular arc a -> via -> b.
struct Edge {
	EdgeKind kind = EdgeKind::Member;
	int member = -1;  // Member: the member it lies on
	int surface = -1; // Carrier: the surface whose carrier it lies on
	End role = End::Stop; // Carrier: Near, Stop, Budget or Loop
	Pt a;
	Pt via;
	Pt b;
	bool arc = false;
	bool inclusive = false; // its interior points are in the region
	bool incl_a = false;    // its start vertex is in the region
	bool incl_b = false;    // a whisker's end vertex is in the region (K7: decided like incl_a)
	int layer = 0;
	double ta = 0.0; // Member: the parameters of a and b on the member's beam
	double tb = 0.0;
	// the carrier's own geometry (shared by every edge on it, so their
	// crossings with the clip rectangle are the same points): a line through
	// gp along gd, or the circle (gc, gr)
	bool gline = true;
	Pt gp;
	Pt gd;
	Pt gc;
	double gr = 0.0;
	bool connector = false; // runs outside the clip rectangle (through infinity)
};

// A cell: the curvilinear polygon between two adjacent members (closed loop
// of edges), and the interval of the members it fills.
struct Cell {
	int left = -1;  // member index
	int right = -1; // member index
	int run = -1;
	int half = 0;   // the Half it was built in (Stage 111)
	std::vector<std::vector<Edge>> loops; // closed loops (a cell clipped by the rectangle can split)
	Pt inside; // a point strictly inside (from the mid cast)
	Cast mid;
	Beam<F64> mid_beam; // the middle member (its intervals' parameters are on it)
	std::vector<Edge> edges() const {
		std::vector<Edge> out;
		for (const auto &l : loops) out.insert(out.end(), l.begin(), l.end());
		return out;
	}
};

struct Loop {
	std::vector<Edge> edges;
	double area = 0.0; // signed, of the tessellated loop
	int half = 0;      // the Half it was built in (Stage 111)
};

// What a cast is for (the benches count them by kind).
enum class CastKind { Crit, Side, Mid, Refine, Probe, Other };

// Stage 111: the plan's first entry fixes the realization per cursor
// (GAME_SPEC 13.1): the cursors whose aim runs toward their image and those
// whose aim runs the other way round are two regions, built by today's direct
// and through-infinity machinery, disjoint, joined at the aims no orientation
// fires the entry for. Single: one realization for the whole plan (no entry).
enum class Half : int { Single = 0, Direct = 1, Reversed = 2 };

// A member: the beam of the final frame that every cursor aiming along `dir`
// lies on, and the casts that read its walk.
struct Member {
	Pt dir;               // unit aim direction at the player
	Pt cursor;            // the cursor of its own cast (a point of interest, or an aim point's image)
	bool poi = false;     // the cursor is a point of interest (exact data when it is a frame-K point)
	std::string why;      // what put it here (for messages)
	Beam<F64> beam;       // the member, p = P_K
	bool straight = false; // the line through P_K and a finite Q_K: it passes the real infinity
	int tangent_sid = -1;  // tangent in the final frame to this surface's carrier at its cursor (a double root)
	int tangent_q_sid = -1; // tangent at a finite Q_K to this surface's carrier (they meet at Q_K only)
	bool tangent_pole = false; // tangent at some frame's finite infinity point to a carrier through it (VIS-REGULAR)
	bool exact = false;    // its cursor is exact data of the final frame (a segment's endpoint, a piece's end)
	Cast crit;            // the member itself
	Cast minus;           // just before it (direction rotated by -eps)
	Cast plus;            // just after it
	double tq = INF;      // Q_K's parameter on the beam
	bool own_a = true;    // the owned arc runs forward from P_K to Q_K (else from Q_K to P_K)
};

enum class Membership : int { Outside = 0, Inside = 1, Unclassified = -1 };

struct Rect {
	double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	bool has(const Pt &p) const { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
	bool strictly(const Pt &p) const { return p.x > x0 && p.x < x1 && p.y > y0 && p.y < y1; }
	Rect grown(double g) const { return {x0 - g, y0 - g, x1 + g, y1 + g}; }
	void add(const Pt &p) {
		x0 = std::min(x0, p.x);
		y0 = std::min(y0, p.y);
		x1 = std::max(x1, p.x);
		y1 = std::max(y1, p.y);
	}
};

struct Result {
	bool supported = false;
	std::string reason; // why unsupported
	Pt player;
	Pt pk;              // P_K
	bool qk_inf = true; // Q_K at infinity
	Pt qk;
	Rect domain;        // the cursors classified: the level bounds grown by 50
	Rect clip;          // where the cells are cut (it holds every finite corner)
	int orientation = 1; // +1: the cells wind counterclockwise (x right, y up); -1: clockwise
	Half half = Half::Single;     // Stage 111: the half this result was built as (Single: the whole, or one realization)
	bool split = false;           // Stage 111: the join of the two halves of a split plan (every cell and loop tagged by its half; the reversed half's cells wind the other way)
	bool near_transforms = false; // the last plan entry changes the transform: its departures lie on its segment
	struct Carrier {
		bool line = true;
		Pt p, d; // a line: a point and its unit direction
		Pt c;    // a circle: its centre and radius
		double r = 0.0;
		double dist(const Pt &q) const { return line ? std::abs(cross(d, q - p)) : std::abs(norm(q - c) - r); }
	};
	std::vector<Carrier> plan_carriers; // the planned surfaces' carriers (and their portal pairs')
	std::vector<Member> members; // in aim-direction order
	std::vector<Cell> cells;
	std::vector<Loop> loops;
	std::vector<Edge> whiskers; // carrier pieces of departure points only (no area)
	int casts = 0;
	int refinements = 0;
	std::vector<std::string> notes; // construction anomalies (reported by the checks)
	// the benches: casts per CastKind, and the build's wall time
	std::array<int, 6> casts_by_kind{};
	long long build_usec = 0;
};

// Angular order of aim directions, counterclockwise from the fixed reference
// +x: the diamond pseudo-angle in [0, 4) -- monotone in the angle, a real key
// (a strict total order even for nearly equal float directions), no trigonometry.
inline int half_of(const Pt &d) { return (d.y < 0.0 || (d.y == 0.0 && d.x < 0.0)) ? 1 : 0; }
inline double dir_key(const Pt &d) {
	const double s = std::abs(d.x) + std::abs(d.y);
	if (s == 0.0) {
		return 0.0;
	}
	const double x = d.x / s;
	const double y = d.y / s;
	if (y >= 0.0) {
		return x >= 0.0 ? y : 1.0 - x;
	}
	return x < 0.0 ? 2.0 - y : 3.0 + x;
}
inline bool dir_less(const Pt &a, const Pt &b) { return dir_key(a) < dir_key(b); }
// One direction (to 1e-12 of a quarter turn: two images of one point computed
// apart, about 1e-9 px on screen); the key wraps at 4.
inline bool dir_equal(const Pt &a, const Pt &b) {
	double d = std::abs(dir_key(a) - dir_key(b));
	d = std::min(d, 4.0 - d);
	return d <= 1e-12 || (half_of(a) == half_of(b) && cross(a, b) == 0.0);
}
// a rotated by eps (radians)
inline Pt rotate(const Pt &a, double eps) {
	const double c = std::cos(eps);
	const double s = std::sin(eps);
	return {a.x * c - a.y * s, a.x * s + a.y * c};
}
// the counterclockwise angle from a to b in (0, 2 pi] (a == b: 2 pi), for picking points only
inline double ccw_angle(const Pt &a, const Pt &b) {
	double t = std::atan2(cross(a, b), dot(a, b));
	if (t <= 0.0) {
		t += 2.0 * PI;
	}
	return t;
}

// ---- edge geometry (testing-layer and drawing tolerances only; no solver decision)

struct ArcGeom {
	bool line = true;
	Pt c;
	double r = 0.0;
	bool ccw = true; // a -> via -> b turns counterclockwise (x right, y up as numbers)
};

inline ArcGeom arc_geom(const Edge &e) {
	ArcGeom g;
	if (!e.arc) {
		return g;
	}
	if (!e.gline && e.gr > 0.0) {
		g.line = false;
		g.c = e.gc;
		g.r = e.gr;
		g.ccw = cross(e.via - e.a, e.b - e.via) > 0.0;
		return g;
	}
	const Pt ab = e.via - e.a;
	const Pt ac = e.b - e.a;
	const double d = 2.0 * cross(ab, ac);
	if (d == 0.0 || !std::isfinite(d)) {
		return g;
	}
	const double ab2 = dot(ab, ab);
	const double ac2 = dot(ac, ac);
	const Pt u{(ac.y * ab2 - ab.y * ac2) / d, (ab.x * ac2 - ac.x * ab2) / d};
	g.line = false;
	g.c = e.a + u;
	g.r = norm(u);
	g.ccw = cross(e.via - e.a, e.b - e.via) > 0.0;
	return g;
}

// X on the circle of arc e lies on the arc a -> via -> b (or is an end).
inline bool on_arc_side(const Edge &e, const Pt &X) {
	const Pt ch = e.b - e.a;
	if (ch.x == 0.0 && ch.y == 0.0) {
		return true; // a full circle
	}
	const double sv = cross(ch, e.via - e.a);
	const double sx = cross(ch, X - e.a);
	return sx == 0.0 || (sx > 0.0) == (sv > 0.0);
}

// The arc's sweep from a to b (signed: counterclockwise positive), for drawing
// and areas.
inline double arc_sweep(const Edge &e, const ArcGeom &g) {
	const double a0 = std::atan2(e.a.y - g.c.y, e.a.x - g.c.x);
	double sw = std::atan2(e.b.y - g.c.y, e.b.x - g.c.x) - a0;
	if (g.ccw) {
		while (sw <= 0.0) sw += 2.0 * PI;
	} else {
		while (sw >= 0.0) sw -= 2.0 * PI;
	}
	return sw;
}

// Workflow W2.0: the solves of a split plan's two halves, shared. A half's
// tie cast (the other orientation's reach) is the other half's own cast of the
// same cursor, and the tie-choice and the realization of a cursor are the
// same in both; each is a pure function of (player, cursor, plan, flags,
// orientation), so the memo changes no result (the halves run in turn; the
// cast threads share it under the mutex).
struct SolveMemo {
	struct Key {
		uint64_t x = 0, y = 0;
		int force = 2; // 0 direct, 1 reversed, 2 the solver's own choice
		bool operator==(const Key &o) const { return x == o.x && y == o.y && force == o.force; }
	};
	struct Hash {
		size_t operator()(const Key &k) const { return size_t((k.x * 1099511628211ULL) ^ (k.y * 14695981039346656037ULL) ^ uint64_t(k.force)); }
	};
	static Key key(const Pt &c, std::optional<bool> force) {
		return Key{std::bit_cast<uint64_t>(c.x), std::bit_cast<uint64_t>(c.y), force ? (*force ? 1 : 0) : 2};
	}
	std::mutex mutex;
	std::unordered_map<Key, std::optional<LadderResult>, Hash> solves;
	std::unordered_map<Key, std::optional<bool>, Hash> ties;
	std::unordered_map<Key, std::optional<bool>, Hash> realizations;
	long hits = 0;
	// RC_VIS_MEMO_STATS=1: the time each kind of call took (misses), printed by compute
	double solve_ms = 0, tie_ms = 0, realization_ms = 0;
	long solve_n = 0, tie_n = 0, realization_n = 0;
};

class Builder {
public:
	Builder(Tracer &tracer, Pt player, std::vector<PlanItem> plan, Flags flags, Rect bounds, Half half = Half::Single)
		: tracer_(tracer), plan_(std::move(plan)), flags_(std::move(flags)),
		  scene_(Scene<F64>::from_defs(tracer.defs(), F64{1.0})), solver_(scene_, nullptr), half_(half) {
		flags_.erase(GHOST);
		scene_.emit_infinity = tracer.emit_infinity();
		res_.player = player;
		res_.domain = bounds.grown(50.0);
		res_.half = half;
	}

	// Stage 111: whether the plan splits into two halves (its first entry may
	// fix the realization per cursor, R1/R2)
	bool splits() const { return solver_.first_entry(plan_, flags_) >= 0; }
	// Workflow W2.0: the memo the two halves share (compute)
	void share(SolveMemo *memo) { memo_ = memo; }

	// One cast of the cursor c (after prepare(); for tests and debugging).
	std::optional<Cast> probe(const Pt &c) {
		if (!res_.supported && !prepare()) {
			return std::nullopt;
		}
		const std::optional<CN<F64>> X0 = backward(std::optional<CN<F64>>(cn_of(c)), K);
		const std::optional<Pt> d = X0 ? aim_dir(*X0) : std::nullopt;
		return cast(c, d ? *d : Pt{}, member_of(c));
	}
	// One cast whose aim direction is d.
	std::optional<Cast> probe_dir(const Pt &d) {
		if (!res_.supported && !prepare()) {
			return std::nullopt;
		}
		const std::optional<Pt> c = cursor_for(d);
		if (!c) {
			return std::nullopt;
		}
		return cast(*c, d, member_of(*c));
	}
	const Result &result() const { return res_; }

	Result build() {
		if (!prepare()) {
			return std::move(res_);
		}
		collect_members();
		cast_members();
		build_cells();
		merge_cells();
		for (Cell &c : res_.cells) {
			c.half = int(half_);
		}
		for (Loop &l : res_.loops) {
			l.half = int(half_);
		}
		return std::move(res_);
	}

private:
	Tracer &tracer_;
	SolveMemo *memo_ = nullptr;
	std::vector<PlanItem> plan_;
	Flags flags_;
	Scene<F64> scene_;
	mutable Solver<F64> solver_;
	Result res_;
	Half half_ = Half::Single;
	std::vector<EffectType> effs_;
	std::vector<bool> changes_; // entry j changes the frame
	std::vector<effects::Poles<F64>> frames_; // frames_[j]: after j entries
	std::optional<CN<F64>> q0_;
	bool via_ = false;
	size_t K = 0;
	std::string init_flags_;

	static constexpr double AIM_R = 128.0;

	bool unsupported(const std::string &why) {
		res_.supported = false;
		res_.reason = why;
		return false;
	}

	bool prepare() {
		if (!scene_supported(tracer_.defs())) {
			return unsupported("scene not supported by the solver");
		}
		K = plan_.size();
		effs_ = solver_.plan_effects(plan_, flags_);
		for (size_t j = 0; j < K; j++) {
			if (is_projective(effs_[j])) {
				return unsupported("projective plan entries are not supported yet");
			}
			changes_.push_back(!plan_[j].is_infinity() && is_transformative(effs_[j]) && scene_.find(plan_[j].surface_id) != nullptr);
		}
		res_.near_transforms = K > 0 && changes_[K - 1];
		for (const PlanItem &item : plan_) {
			const Surf<F64> *ps = item.is_infinity() ? nullptr : scene_.find(item.surface_id);
			for (const Surf<F64> *q : {ps, ps != nullptr && ps->portal ? scene_.find(ps->pair) : nullptr}) {
				if (q == nullptr) {
					continue;
				}
				Result::Carrier cr;
				cr.line = q->line;
				cr.p = Pt{q->sx, q->sy};
				cr.d = unit(Pt{q->ex - q->sx, q->ey - q->sy});
				cr.c = Pt{q->cx, q->cy};
				cr.r = q->r;
				res_.plan_carriers.push_back(cr);
			}
		}
		// Stage 111: a half runs its own realization; the whole runs the plan's
		via_ = half_ == Half::Single ? solver_.via_infinity(plan_, flags_) : half_ == Half::Reversed;
		q0_ = effects::scene_pre_q(scene_);
		const CN<F64> P = cn_of(res_.player);
		effects::Poles<F64> cur{P, q0_, q0_ ? std::optional<CN<F64>>(P.sub(*q0_)) : std::nullopt};
		frames_.push_back(cur);
		std::vector<std::pair<int, effects::Poles<F64>>> stack;
		for (size_t j = 0; j < K; j++) {
			if (changes_[j]) {
				const Surf<F64> &surf = *scene_.find(plan_[j].surface_id);
				if (!stack.empty() && pop_id_of(stack.back().first) == surf.id) {
					cur = stack.back().second;
					stack.pop_back();
				} else {
					if (!cur.P) {
						return unsupported("the player's image is at infinity");
					}
					stack.emplace_back(surf.id, cur);
					cur = effects::map_pole(cur, surf);
				}
			}
			frames_.push_back(cur);
		}
		if (!frames_.back().P) {
			return unsupported("the player's image is at infinity");
		}
		// reflections and inversions reverse orientation, portals keep it
		int reversing = 0;
		for (const auto &st : stack) {
			reversing += scene_.find(st.first)->portal ? 0 : 1;
		}
		// the realization through infinity runs every member the other way
		res_.orientation = (reversing % 2 == 0 ? 1 : -1) * (via_ ? -1 : 1);
		res_.pk = pt_of(*frames_.back().P);
		res_.qk_inf = !frames_.back().Q.has_value();
		if (!res_.qk_inf && pt_of(*frames_.back().Q) == res_.pk) {
			return unsupported("the player at the frame's infinity (every aim degenerates)");
		}
		if (frames_.back().Q) {
			res_.qk = pt_of(*frames_.back().Q);
		}
		Flags f = flags_;
		init_flags_ = f.canonical_key();
		res_.supported = true;
		return true;
	}

	int pop_id_of(int sid) const {
		const Surf<F64> *s = scene_.find(sid);
		return (s != nullptr && s->portal) ? s->pair : sid;
	}

	// a point of frame j mapped to frame `to` (j <= to), or to frame 0 (inverse)
	std::optional<CN<F64>> forward(std::optional<CN<F64>> z, size_t j, size_t to) const {
		for (size_t i = j; i < to && z; i++) {
			if (changes_[i]) {
				z = effects::map_point(z, *scene_.find(plan_[i].surface_id), false);
			}
		}
		return z;
	}
	std::optional<CN<F64>> backward(std::optional<CN<F64>> z, size_t j) const {
		for (size_t i = j; i-- > 0 && z;) {
			if (changes_[i]) {
				z = effects::map_point(z, *scene_.find(plan_[i].surface_id), true);
			}
		}
		return z;
	}

	// The aim direction at the player of the aim point X0 (frame 0).
	std::optional<Pt> aim_dir(const CN<F64> &X0) const {
		const CN<F64> P = cn_of(res_.player);
		if (X0.sub(P).is_zero()) {
			return std::nullopt;
		}
		Pt d;
		if (q0_) {
			const CN<F64> delta = P.sub(*q0_);
			d = pt_of(Beam<F64>::through(P, X0, &delta).tau);
		} else {
			d = unit(pt_of(X0) - res_.player);
		}
		if (via_) {
			d = d * -1.0;
		}
		return d.finite() ? std::optional<Pt>(unit(d)) : std::nullopt;
	}

	// A frame-K cursor whose aim direction is d (in this half's realization).
	std::optional<Pt> cursor_for(const Pt &d) const {
		const CN<F64> P = cn_of(res_.player);
		const Pt s = via_ ? d * -1.0 : d;
		Beam<F64> b = q0_ ? Beam<F64>::through_tangent(P, cn_of(s), *q0_) : Beam<F64>{P, cn_of(s), F64{0.0}};
		// the aim point lies between the player and Q0 on this circle (past Q0 the
		// arc belongs to the opposite aim)
		double t1 = AIM_R;
		if (q0_) {
			const double tq = b.param_of(*q0_).v;
			if (tq > 0.0) {
				t1 = std::min(AIM_R, tq / 2.0);
			}
		}
		const std::optional<CN<F64>> c = forward(std::optional<CN<F64>>(b.at(F64{t1})), 0, K);
		if (!c) {
			return std::nullopt;
		}
		const Pt p = pt_of(*c);
		return p.finite() ? std::optional<Pt>(p) : std::nullopt;
	}

	Beam<F64> member_of(const Pt &c) const {
		const effects::Poles<F64> &f = frames_.back();
		const CN<F64> *delta = f.delta ? &*f.delta : nullptr;
		Beam<F64> b = Beam<F64>::through(*f.P, cn_of(c), delta);
		if (via_) {
			b = Beam<F64>{b.p, b.tau.neg(), b.kappa.neg()};
		}
		return b;
	}

	// ---- points of interest

	// Whether the frames after j map surface sid's carrier onto itself (a
	// tangency found in frame j stays a tangency to it in the final frame: the
	// planned mirror itself, say).
	bool carrier_fixed(int sid, size_t j) const {
		const Surf<F64> *s = scene_.find(sid);
		if (s == nullptr) {
			return false;
		}
		const Pt S{s->sx, s->sy};
		const Pt E{s->ex, s->ey};
		std::vector<Pt> pts{S, E, (S + E) * 0.5};
		if (!s->line) {
			pts = {S, E, Pt{s->cx + s->r, s->cy}, Pt{s->cx, s->cy + s->r}};
		}
		for (const Pt &q : pts) {
			const std::optional<CN<F64>> z = forward(std::optional<CN<F64>>(cn_of(q)), j, K);
			if (!z) {
				return false;
			}
			const Pt w = pt_of(*z);
			const double on = s->line ? std::abs(cross(unit(E - S), w - S)) : std::abs(norm(w - Pt{s->cx, s->cy}) - s->r);
			if (!(on <= 1e-9 * std::max({1.0, std::abs(w.x), std::abs(w.y)}))) {
				return false;
			}
		}
		return true;
	}

	// Frame j is the final frame: no entry from j on changes it (K7: exact data
	// found in it is exact data of the final frame).
	bool same_frame_as_final(size_t j) const {
		for (size_t e = j; e < K; e++) {
			if (changes_[e]) {
				return false;
			}
		}
		return true;
	}

	void add_poi(std::vector<Member> &out, const CN<F64> &z, size_t j, const std::string &why, int tangent_sid = -1, bool exact = false,
			const std::optional<Pt> &other = std::nullopt) {
		const std::optional<CN<F64>> c = forward(std::optional<CN<F64>>(z), j, K);
		const std::optional<CN<F64>> X0 = backward(std::optional<CN<F64>>(z), j);
		if (!c || !X0) {
			return;
		}
		const Pt cp = pt_of(*c);
		if (!cp.finite() || cp == res_.pk || (!res_.qk_inf && cp == res_.qk) || cp == res_.player) {
			return;
		}
		const std::optional<Pt> d = aim_dir(*X0);
		if (!d) {
			return;
		}
		Member m;
		m.dir = *d;
		m.cursor = cp;
		m.poi = true;
		m.why = why + (j < K ? " (frame " + std::to_string(j) + ")" : "");
		m.tangent_sid = j == K || (tangent_sid >= 0 && carrier_fixed(tangent_sid, j)) ? tangent_sid : -1;
		m.exact = exact && same_frame_as_final(j);
		out.push_back(m);
		m.exact = false; // (only the first copy keeps the flag: see below)
		out.back().exact = exact && same_frame_as_final(j);
		// the walk the other way along the same member passes the point too (after
		// the wrap, or before an infinity entry): the opposite aim is critical as
		// well -- unless the walk's owned part comes first and nothing after it
		// matters (no plan entry, the direct realization)
		if (K == 0 && !via_) {
			return;
		}
		// its cursor: the point mirrored through the player when that is exact, a
		// frame-j point exactly on the member's other arc when the caller has one
		// (K8: a rounded aim crosses a carrier the member only touches), an aim
		// point's image otherwise
		Member o;
		o.dir = *d * -1.0;
		o.why = m.why + ", the other way";
		const std::optional<CN<F64>> exact_c = other ? forward(std::optional<CN<F64>>(cn_of(*other)), j, K) : std::nullopt;
		if (exact_c && pt_of(*exact_c).finite()) {
			o.cursor = pt_of(*exact_c);
			o.poi = true;
			o.why += " (exact)";
		} else if (K == 0 && !q0_) {
			o.cursor = res_.player * 2.0 - cp;
			o.poi = true;
		} else if (const std::optional<Pt> oc = cursor_for(o.dir)) {
			o.cursor = *oc;
		} else {
			return;
		}
		if (o.cursor.finite() && !(o.cursor == res_.player)) {
			out.push_back(std::move(o));
		}
	}

	// Members of frame j's family (the circles through A and B, lines through A
	// when B is infinity) tangent to a carrier: by the inversion about A (the
	// family becomes the lines through B').
	static std::vector<Pt> tangent_points(const Pt &A, const std::optional<Pt> &B, const Surf<F64> &s) {
		std::vector<Pt> out;
		auto inv = [&](const Pt &z) -> std::optional<Pt> {
			const Pt w = z - A;
			const double n2 = dot(w, w);
			if (n2 == 0.0) {
				return std::nullopt;
			}
			return A + w * (1.0 / n2);
		};
		const Pt Bp = B ? *inv(*B) : A; // infinity -> A
		// the carrier's image: a circle (c', r') or nothing (through A: a line)
		Pt cc;
		double rr = 0.0;
		if (!s.line) {
			const Pt c{s.cx, s.cy};
			const Pt d = c - A;
			const double D = dot(d, d) - s.r2;
			if (D == 0.0) {
				return out;
			}
			cc = A + d * (1.0 / D);
			rr = s.r / std::abs(D);
		} else {
			const Pt S{s.sx, s.sy};
			const Pt dir = unit(Pt{s.ex - s.sx, s.ey - s.sy});
			const Pt F = S + dir * dot(A - S, dir); // the foot of A on the line
			if (F == A) {
				return out;
			}
			const std::optional<Pt> Fi = inv(F);
			if (!Fi) {
				return out;
			}
			cc = A + (*Fi - A) * 0.5;
			rr = norm(*Fi - A) * 0.5;
		}
		const Pt u = Bp - cc;
		const double dd = norm(u);
		if (!(dd > rr * (1.0 + 1e-9)) || rr <= 0.0) {
			return out; // B' on the image (the carrier passes B): see tangent_at_pole
		}
		const Pt e = u * (1.0 / dd);
		const double ca = rr / dd;
		const double sa = std::sqrt(std::max(0.0, 1.0 - ca * ca));
		for (double sg : {1.0, -1.0}) {
			const Pt tp = cc + (e * ca + perp(e) * (sg * sa)) * rr;
			if (tp == A) {
				continue; // the tangent at A: a member through A twice
			}
			const std::optional<Pt> T = inv(tp);
			if (T && T->finite()) {
				out.push_back(*T);
			}
		}
		return out;
	}

	// The member of frame j's family (circles through A and B) tangent at B to a
	// carrier through B: the circle through A tangent to the carrier's direction there.
	static std::optional<Pt> tangent_at_pole(const Pt &A, const Pt &B, const Surf<F64> &s) {
		Pt t;
		if (s.line) {
			const Pt S{s.sx, s.sy};
			const Pt d = unit(Pt{s.ex - s.sx, s.ey - s.sy});
			if (std::abs(cross(d, B - S)) > 1e-9 * std::max({1.0, std::abs(B.x), std::abs(B.y)})) {
				return std::nullopt;
			}
			t = d;
		} else {
			const Pt c{s.cx, s.cy};
			if (std::abs(norm(B - c) - s.r) > 1e-9 * std::max(1.0, s.r)) {
				return std::nullopt;
			}
			t = perp(unit(B - c));
		}
		const Pt n = perp(t);
		const double den = 2.0 * dot(n, B - A);
		if (den == 0.0) {
			return std::nullopt; // A on the carrier's tangent at B: the line member
		}
		const double sd = -dot(B - A, B - A) / den;
		const Pt C = B + n * sd;
		const Pt o = C * 2.0 - B; // the circle's point opposite B
		if (norm(o - A) <= 1e-9 * std::max({1.0, std::abs(A.x), std::abs(A.y)})) {
			return C + perp(B - C); // A itself: a quarter turn from B instead
		}
		return o;
	}

	// A point exactly on that member's other arc (from A to B, missing `t`, the
	// point tangent_at_pole returned): A mirrored in the diameter across the
	// axis B - C, kept only when it lies on the arc and an exact test puts it on
	// the circle through A and B tangent to the carrier at B (K8: rational data,
	// an axis-parallel axis). nullopt otherwise.
	static std::optional<Pt> tangent_at_pole_other(const Pt &A, const Pt &B, const Surf<F64> &s, const Pt &t) {
		const Pt C = (B + t) * 0.5;
		const Pt u = unit(B - C);
		if (!u.finite()) {
			return std::nullopt;
		}
		const Pt X = A - u * (2.0 * dot(A - C, u));
		if (!X.finite() || X == A || X == B) {
			return std::nullopt;
		}
		const double sx = cross(B - A, X - A);
		const double st = cross(B - A, t - A);
		if (!(sx * st < 0.0)) {
			return std::nullopt; // on t's arc (A past the diameter): no simple exact point
		}
		using namespace exact;
		BigFloat nx, ny;
		if (s.line) {
			if (!orient_zero(s.sx, s.sy, s.ex, s.ey, B.x, B.y)) {
				return std::nullopt;
			}
			nx = sub(of(s.sy), of(s.ey));
			ny = sub(of(s.ex), of(s.sx));
		} else {
			if (!on_circle_zero(s.cx, s.cy, s.r, B.x, B.y)) {
				return std::nullopt;
			}
			nx = sub(of(B.x), of(s.cx));
			ny = sub(of(B.y), of(s.cy));
		}
		// with a = A - B and x = X - B: the circle through B, A and X is tangent
		// to the carrier at B iff |a|^2 (n . x) = |x|^2 (n . a)
		const BigFloat ax = sub(of(A.x), of(B.x)), ay = sub(of(A.y), of(B.y));
		const BigFloat xx = sub(of(X.x), of(B.x)), xy = sub(of(X.y), of(B.y));
		const BigFloat la = add(mul(ax, ax), mul(ay, ay)), lx = add(mul(xx, xx), mul(xy, xy));
		const BigFloat na = add(mul(nx, ax), mul(ny, ay)), nxx = add(mul(nx, xx), mul(ny, xy));
		if (na.is_zero() || !sub(mul(la, nxx), mul(lx, na)).is_zero()) {
			return std::nullopt;
		}
		return X;
	}

	// Proper crossings of two segments (interior to both, transversal).
	static std::vector<Pt> carrier_points(const Surf<F64> &a, const Surf<F64> &b) {
		std::vector<Pt> pts;
		auto line_circle = [&](const Surf<F64> &l, const Surf<F64> &c) {
			const Pt S{l.sx, l.sy};
			const Pt d = Pt{l.ex - l.sx, l.ey - l.sy};
			const Pt w = S - Pt{c.cx, c.cy};
			const double A2 = dot(d, d);
			const double B2 = 2.0 * dot(w, d);
			const double C2 = dot(w, w) - c.r2;
			const double disc = B2 * B2 - 4.0 * A2 * C2;
			if (disc <= 0.0 || A2 == 0.0) {
				return;
			}
			const double sq = std::sqrt(disc);
			for (double u : {(-B2 - sq) / (2.0 * A2), (-B2 + sq) / (2.0 * A2)}) {
				pts.push_back(S + d * u);
			}
		};
		if (a.line && b.line) {
			const Pt p{a.sx, a.sy};
			const Pt r{a.ex - a.sx, a.ey - a.sy};
			const Pt q{b.sx, b.sy};
			const Pt s{b.ex - b.sx, b.ey - b.sy};
			const double den = cross(r, s);
			if (den != 0.0) {
				pts.push_back(p + r * (cross(q - p, s) / den));
			}
		} else if (a.line) {
			line_circle(a, b);
		} else if (b.line) {
			line_circle(b, a);
		} else {
			const Pt c0{a.cx, a.cy};
			const Pt c1{b.cx, b.cy};
			const double d = norm(c1 - c0);
			if (d > 0.0 && d < a.r + b.r && d > std::abs(a.r - b.r)) {
				const double x = (d * d + a.r2 - b.r2) / (2.0 * d);
				const double h = std::sqrt(std::max(0.0, a.r2 - x * x));
				const Pt e = (c1 - c0) * (1.0 / d);
				pts.push_back(c0 + e * x + perp(e) * h);
				pts.push_back(c0 + e * x - perp(e) * h);
			}
		}
		std::vector<Pt> out;
		for (const Pt &p : pts) {
			const CN<F64> z = cn_of(p);
			if (!p.finite() || !geom::on_segment(a.seg, z) || !geom::on_segment(b.seg, z)) {
				continue;
			}
			if ((!a.full && ((p == Pt{a.sx, a.sy}) || (p == Pt{a.ex, a.ey}))) || (!b.full && ((p == Pt{b.sx, b.sy}) || (p == Pt{b.ex, b.ey})))) {
				continue; // an endpoint: a point of interest already
			}
			out.push_back(p);
		}
		return out;
	}

	void collect_members() {
		std::vector<Member> pois;
		for (size_t j = 0; j <= K; j++) {
			if (j > 0 && !changes_[j - 1]) {
				continue; // the same frame as before
			}
			const effects::Poles<F64> &fr = frames_[j];
			if (!fr.P) {
				continue;
			}
			const Pt A = pt_of(*fr.P);
			const std::optional<Pt> B = fr.Q ? std::optional<Pt>(pt_of(*fr.Q)) : std::nullopt;
			const auto &surfs = scene_.surfaces;
			for (size_t a = 0; a < surfs.size(); a++) {
				const Surf<F64> &s = *surfs[a];
				if (!s.full) {
					add_poi(pois, CN<F64>{F64{s.sx}, F64{s.sy}}, j, "endpoint of " + std::to_string(s.id), -1, true);
					add_poi(pois, CN<F64>{F64{s.ex}, F64{s.ey}}, j, "endpoint of " + std::to_string(s.id), -1, true);
				}
				if (!(s.line && !B)) {
					for (const Pt &t : tangent_points(A, B, s)) {
						add_poi(pois, cn_of(t), j, "tangency to " + std::to_string(s.id), s.id);
					}
				}
				if (B) {
					if (const std::optional<Pt> t = tangent_at_pole(A, *B, s)) {
						const size_t before = pois.size();
						add_poi(pois, cn_of(*t), j, "tangency at Q to " + std::to_string(s.id), -1, false, tangent_at_pole_other(A, *B, s, *t));
						for (size_t q = before; q < pois.size(); q++) {
							pois[q].tangent_pole = true; // (the other way round too: the same circle)
							if (j == K || carrier_fixed(s.id, j)) {
								pois[q].tangent_q_sid = s.id;
							}
						}
					}
				}
				for (size_t b = a + 1; b < surfs.size(); b++) {
					if (surfs[b]->carrier_rep == s.carrier_rep) {
						continue; // one carrier (K6)
					}
					for (const Pt &x : carrier_points(s, *surfs[b])) {
						add_poi(pois, cn_of(x), j, "crossing of " + std::to_string(s.id) + " and " + std::to_string(surfs[b]->id));
					}
				}
			}
			// a planned piece's ends (Stage 108), where its entry is hit
			for (size_t e = j; e < K; e++) {
				if (e > j && changes_[e - 1]) {
					break;
				}
				if (plan_[e].span) {
					const auto &sp = *plan_[e].span;
					add_poi(pois, CN<F64>{F64{sp[0]}, F64{sp[1]}}, j, "piece end", -1, true);
					add_poi(pois, CN<F64>{F64{sp[2]}, F64{sp[3]}}, j, "piece end", -1, true);
				}
			}
		}
		// a finite Q_K: the line through P_K and Q_K is a member (both ways along
		// it); the cells beside it reach the real infinity, which they clip
		if (!res_.qk_inf) {
			const Pt PK = res_.pk;
			const Pt QK = res_.qk;
			for (const Pt &c : {(PK + QK) * 0.5, PK * 2.0 - QK}) {
				const std::optional<CN<F64>> X0 = backward(std::optional<CN<F64>>(cn_of(c)), K);
				const std::optional<Pt> d = X0 ? aim_dir(*X0) : std::nullopt;
				if (d) {
					Member m;
					m.dir = *d;
					m.cursor = c;
					m.straight = true;
					m.why = "the line through P_K and Q_K";
					pois.push_back(std::move(m));
				}
			}
		}
		// Stage 111: the aims parallel to the first entry's line carrier: no
		// orientation fires the entry there (the cast is dead), and the members
		// bound the cells of both halves at the crack between them
		if (half_ != Half::Single) {
			const int j = solver_.first_entry(plan_, flags_);
			const Surf<F64> *e = j >= 0 ? scene_.find(plan_[size_t(j)].surface_id) : nullptr;
			if (e != nullptr && e->line) {
				const Pt u = unit(Pt{e->ex - e->sx, e->ey - e->sy});
				for (const Pt &d : {u, u * -1.0}) {
					if (const std::optional<Pt> c = cursor_for(d)) {
						Member m;
						m.dir = d;
						m.cursor = *c;
						m.why = "the aim parallel to the first entry's carrier";
						pois.push_back(std::move(m));
					}
				}
			}
		}
		// the axes: every gap between members stays below a quarter turn
		for (const Pt &d : {Pt{1, 0}, Pt{0, 1}, Pt{-1, 0}, Pt{0, -1}}) {
			Member m;
			m.dir = d;
			m.why = "axis";
			if (const std::optional<Pt> c = cursor_for(d)) {
				m.cursor = *c;
				pois.push_back(std::move(m));
			}
		}
		std::stable_sort(pois.begin(), pois.end(), [](const Member &a, const Member &b) { return dir_less(a.dir, b.dir); });
		// one member per direction: exact duplicates merge, and so do two images of
		// one point (a carrier's point fixed by its own map, computed per frame)
		auto same_point = [&](const Member &a, const Member &b) {
			const double tol = 1e-9 * std::max({1.0, std::abs(a.cursor.x), std::abs(a.cursor.y)});
			if (dot(a.dir, b.dir) <= 0.0) {
				return false;
			}
			if (norm(a.cursor - b.cursor) <= tol) {
				return true;
			}
			// two points of interest on one member (a circle tangent to two
			// carriers): one member, whose directions differ by rounding only
			if (ccw_angle(a.dir, b.dir) > 1e-7 && ccw_angle(b.dir, a.dir) > 1e-7) {
				return false;
			}
			const Beam<F64> mb = member_of(a.cursor);
			const Pt P = pt_of(mb.p);
			const Pt t = pt_of(mb.tau);
			const double k = mb.kappa.v;
			const double d = k == 0.0 ? std::abs(cross(t, b.cursor - P)) : std::abs(norm(b.cursor - (P + perp(t) * (1.0 / k))) - 1.0 / std::abs(k));
			return d <= 1e-9 * std::max({1.0, std::abs(b.cursor.x), std::abs(b.cursor.y)});
		};
		std::vector<Member> out;
		for (Member &m : pois) {
			if (!out.empty() && (dir_equal(out.back().dir, m.dir) || same_point(out.back(), m))) {
				if (m.straight || (!out.back().poi && !out.back().straight && m.poi)) {
					out.back() = std::move(m);
				}
				continue;
			}
			out.push_back(std::move(m));
		}
		if (out.size() > 1 && (dir_equal(out.front().dir, out.back().dir) || same_point(out.front(), out.back()))) {
			out.pop_back();
		}
		res_.members = std::move(out);
	}

	// ---- casts

	Cast cast(const Pt &c, const Pt &dir, const Beam<F64> &member, CastKind kind = CastKind::Other) {
		res_.casts++;
		res_.casts_by_kind[size_t(kind)]++;
		return cast_on(tracer_, c, dir, member);
	}

	// A batch of casts, in parallel over threads (Perf P3): each thread casts
	// with a helper tracer of its own (Tracer::helper; the solve caches are pure
	// memos), the results land by index, so they are the casts made in order.
	struct CastJob {
		Pt c;
		Pt dir;
		Beam<F64> member;
		CastKind kind = CastKind::Other;
	};
	std::vector<Cast> cast_batch(const std::vector<CastJob> &jobs) {
		std::vector<Cast> out(jobs.size());
		for (const CastJob &j : jobs) {
			res_.casts++;
			res_.casts_by_kind[size_t(j.kind)]++;
		}
		const size_t nt = std::min(jobs.size(), size_t(vis_threads()));
		if (nt <= 1) {
			for (size_t i = 0; i < jobs.size(); i++) {
				out[i] = cast_on(tracer_, jobs[i].c, jobs[i].dir, jobs[i].member);
			}
			return out;
		}
		std::vector<Tracer *> tracers{&tracer_};
		for (size_t k = 1; k < nt; k++) {
			tracers.push_back(&tracer_.helper(k - 1));
		}
		std::atomic<size_t> next{0};
		auto work = [&](Tracer *t) {
			for (size_t i = next++; i < jobs.size(); i = next++) {
				out[i] = cast_on(*t, jobs[i].c, jobs[i].dir, jobs[i].member);
			}
		};
		std::vector<std::thread> threads;
		for (size_t k = 1; k < nt; k++) {
			threads.emplace_back(work, tracers[k]);
		}
		work(tracers[0]);
		for (std::thread &th : threads) {
			th.join();
		}
		return out;
	}

	// Stage 111: a cast in this half's orientation. In a split plan the half's
	// orientation is forced on the solver; where it is not the member's
	// tie-choice (what a cursor both planned walks reach gets), the other
	// orientation's planned reach is the plan's realization there, and the
	// cast keeps only what lies outside it.
	// RC_VIS_TRACE=1: one line per cast (Workflow W5): the half, the cursor, the
	// aim, the tie-choice, whether the solver's own choice is this half (chosen),
	// valid / alive / met, the consuming event, the near and far ends.
	void trace_cast(const Cast &r, std::optional<bool> q) const {
		if (std::getenv("RC_VIS_TRACE") == nullptr) {
			return;
		}
		std::fprintf(stderr, "cast half %d cursor (%.9g, %.9g) dir (%.9f, %.9f) tie %s chosen %d valid %d alive %d met %d ek %d depart %d near %s end %s%s\n",
			int(half_), r.cursor.x, r.cursor.y, r.dir.x, r.dir.y, q ? (*q ? "reversed" : "direct") : "none", int(r.chosen), int(r.valid), int(r.alive),
			int(r.met), r.ek, int(r.has_depart), end_name(r.near_kind), end_name(r.end_kind), r.has_tie ? (r.tie_after_q ? " tie-cut after Q" : " tie-cut") : "");
	}

	Cast cast_on(Tracer &tracer, const Pt &c, const Pt &dir, const Beam<F64> &member) const {
		if (half_ == Half::Single) {
			Cast r = cast_core(tracer, c, dir, member, std::nullopt, nullptr);
			trace_cast(r, std::nullopt);
			return r;
		}
		const bool rev = half_ == Half::Reversed;
		// (a plan without an entry -- the prefix of a split plan, built in a cell's
		// orientation by a check -- has no tie-choice: the forced orientation alone)
		const std::optional<bool> q = solver_.first_entry(plan_, flags_) >= 0 ? memo_tie(tracer, c) : std::nullopt;
		if (std::getenv("RC_VIS_TIE") != nullptr) {
			std::fprintf(stderr, "tie half %d cursor (%.6g, %.6g) dir (%.9f, %.9f) angle %.9f -> %s\n", int(half_), c.x, c.y, dir.x, dir.y,
				std::atan2(dir.y, dir.x), q ? (*q ? "reversed" : "direct") : "none");
		}
		std::optional<Cast> tie;
		if (q && *q != rev) {
			tie = cast_core(tracer, c, dir, Beam<F64>{member.p, member.tau.neg(), member.kappa.neg()}, *q, nullptr);
			if (!tie->valid) {
				tie.reset();
			}
		}
		Cast r = cast_core(tracer, c, dir, member, rev, tie ? &*tie : nullptr);
		if (r.valid) {
			const std::optional<bool> own = memo_realization(tracer, c);
			r.chosen = own && *own == rev;
		}
		// a tie cut names the other orientation's departure: only in a split half,
		// only where the tie-choice is the other orientation
		RC_ASSERT(!r.has_tie || (q && *q != rev), "vis: a tie cut outside a tie");
		trace_cast(r, q);
		return r;
	}

	// the three solver calls of a cast, through the shared memo when there is one
	std::optional<LadderResult> memo_solve(Tracer &tracer, const Pt &c, std::optional<bool> force) const {
		const Pt P = res_.player;
		if (memo_ == nullptr) {
			return tracer.solve(P.x, P.y, c.x, c.y, plan_, flags_, force);
		}
		const SolveMemo::Key key = SolveMemo::key(c, force);
		{
			std::lock_guard<std::mutex> lock(memo_->mutex);
			const auto it = memo_->solves.find(key);
			if (it != memo_->solves.end()) {
				memo_->hits++;
				return it->second;
			}
		}
		const auto t0 = std::chrono::steady_clock::now();
		std::optional<LadderResult> lr = tracer.solve(P.x, P.y, c.x, c.y, plan_, flags_, force);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::lock_guard<std::mutex> lock(memo_->mutex);
		memo_->solves.emplace(key, lr);
		memo_->solve_ms += ms;
		memo_->solve_n++;
		return lr;
	}
	std::optional<bool> memo_tie(Tracer &tracer, const Pt &c) const {
		const Pt P = res_.player;
		if (memo_ == nullptr) {
			return tracer.tie_choice(P.x, P.y, c.x, c.y, plan_, flags_);
		}
		const SolveMemo::Key key = SolveMemo::key(c, std::nullopt);
		{
			std::lock_guard<std::mutex> lock(memo_->mutex);
			const auto it = memo_->ties.find(key);
			if (it != memo_->ties.end()) {
				memo_->hits++;
				return it->second;
			}
		}
		const auto t0 = std::chrono::steady_clock::now();
		const std::optional<bool> q = tracer.tie_choice(P.x, P.y, c.x, c.y, plan_, flags_);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::lock_guard<std::mutex> lock(memo_->mutex);
		memo_->ties.emplace(key, q);
		memo_->tie_ms += ms;
		memo_->tie_n++;
		return q;
	}
	std::optional<bool> memo_realization(Tracer &tracer, const Pt &c) const {
		const Pt P = res_.player;
		if (memo_ == nullptr) {
			return tracer.realization_reversed(P.x, P.y, c.x, c.y, plan_, flags_);
		}
		const SolveMemo::Key key = SolveMemo::key(c, std::nullopt);
		{
			std::lock_guard<std::mutex> lock(memo_->mutex);
			const auto it = memo_->realizations.find(key);
			if (it != memo_->realizations.end()) {
				memo_->hits++;
				return it->second;
			}
		}
		const auto t0 = std::chrono::steady_clock::now();
		const std::optional<bool> q = tracer.realization_reversed(P.x, P.y, c.x, c.y, plan_, flags_);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::lock_guard<std::mutex> lock(memo_->mutex);
		memo_->realizations.emplace(key, q);
		memo_->realization_ms += ms;
		memo_->realization_n++;
		return q;
	}

	Cast cast_core(Tracer &tracer, const Pt &c, const Pt &dir, const Beam<F64> &member, std::optional<bool> force, const Cast *tie) const {
		Cast r;
		r.cursor = c;
		r.dir = dir;
		const Pt P = res_.player;
		const std::optional<LadderResult> lr = memo_solve(tracer, c, force);
		if (!lr) {
			return r;
		}
		if (tie != nullptr) {
			r.has_tie = true;
			r.tie_after_q = tie->near_kind == End::PoleQ;
			r.tie_depart_pt = tie->depart_pt;
			r.tie_depart_sid = tie->depart_sid;
		}
		r.valid = true;
		const std::vector<CastEvent> pl = cast_events(lr->planned, init_flags_);
		const std::vector<CastEvent> ph = cast_events(lr->physical, init_flags_);

		// the event that consumes the last plan entry, on the planned walk
		int eK = -1;
		bool met = K == 0;
		if (!met) {
			size_t j = 0;
			for (size_t i = 0; i < pl.size() && !met; i++) {
				const CastEvent &e = pl[i];
				if (e.kind == EventKind::Cursor) {
					break;
				}
				const PlanItem &item = plan_[j];
				const bool consumed = item.is_infinity() ? (e.kind == EventKind::Infinity && e.applied)
														 : (e.kind == EventKind::Hit && e.sid == item.surface_id && e.left == item.left);
				if (consumed && !item.is_infinity()) {
					r.has_depart = true;
					r.depart_pt = e.pt;
					r.depart_sid = e.sid;
				}
				if (consumed && ++j == K) {
					eK = int(i);
					met = true;
				}
			}
		}
		// The walks as the merge reads them: the steps trace_pair draws, paired by
		// index, compared by frame id; es[i] counts the steps after event i.
		const auto paths = tracer.cast_paths(*lr, P.x, P.y, c.x, c.y, flags_);
		const std::vector<TraceStep> &spl = paths.first.steps;
		const std::vector<TraceStep> &sph = paths.second.steps;
		const std::vector<int> &espl = paths.first.event_steps;
		const std::vector<int> &esph = paths.second.event_steps;
		// aligned(n): both walks draw n steps, in one frame each
		auto aligned = [&](int n) {
			if (n > int(spl.size()) || n > int(sph.size())) {
				return false;
			}
			for (int k = 0; k < n; k++) {
				if (spl[size_t(k)].frame_id != sph[size_t(k)].frame_id) {
					return false;
				}
			}
			return true;
		};
		auto steps_after = [](const std::vector<int> &es, int i) { return i < int(es.size()) ? es[size_t(i)] : (es.empty() ? 0 : es.back()); };
		const int nK = eK >= 0 ? steps_after(espl, eK) : 0;
		r.ek = eK;
		r.met = met;
		// both walks reach the last entry's crossing in one frame, and leave it in one
		bool reached = met && aligned(nK);
		bool alive = reached && (eK < 0 || aligned(nK + 1));
		// the cast's own cursor: every step before it aligned
		r.cursor_ok = paths.first.cursor_index >= 0 && aligned(paths.first.cursor_index) && cursor_point_agrees(paths.first, paths.second);
		// the near end
		if (K == 0) {
			r.near_kind = End::PoleP;
		} else if (eK >= 0 && pl[size_t(eK)].kind == EventKind::Infinity) {
			r.near_kind = End::PoleQ;
		} else if (eK >= 0) {
			// the departure point of the last entry: the hit, moved by the entry's
			// map when it changes the frame (a portal departs its pair at T(h))
			r.near_kind = End::Near;
			r.near_sid = pl[size_t(eK)].sid;
			r.near_pt = pl[size_t(eK)].pt;
			const Surf<F64> *surf = scene_.find(r.near_sid);
			if (changes_[K - 1] && surf != nullptr && surf->portal) {
				if (const std::optional<CN<F64>> z = effects::map_point(std::optional<CN<F64>>(cn_of(r.near_pt)), *surf, false)) {
					r.near_pt = pt_of(*z);
					r.near_sid = surf->pair;
				}
			}
		}
		// the far end: the first physical event past the last entry's point (a
		// step beyond it) that changes the frame or ends the walk, or the budget
		int stop_i = -1;
		int budget_i = -1;
		int counted = 0;
		std::string frame_now = "0||" + init_flags_;
		for (size_t i = 0; i < ph.size(); i++) {
			const CastEvent &e = ph[i];
			if (e.kind == EventKind::Hit || e.kind == EventKind::Infinity || e.kind == EventKind::Max) {
				if (++counted == MAX_HITS && budget_i < 0) {
					budget_i = int(i);
				}
			}
			if (steps_after(esph, int(i)) <= nK) {
				frame_now = e.frame; // at or before the departure point
				continue;
			}
			if (e.kind == EventKind::Hit && stop_i < 0 && (e.applied || e.frame != frame_now)) {
				stop_i = int(i);
			}
			frame_now = e.frame;
		}
		if (budget_i >= 0 && eK >= 0 && steps_after(esph, budget_i) <= nK) {
			alive = false;
			reached = false;
		}
		// a walk whose loop closes ends where its beam began: the cast's own start
		// (a full loop), or an earlier point when the last entries kept the beam --
		// a lap's origin (an infinity entry in a pre-transform level), or, after a
		// surface entry that changes no frame, the last departure before it (P_K
		// when there is none)
		int lap_i = -1;
		End lap_kind = End::Loop;
		Pt lap_pt;
		auto same = [](const Pt &x, const Pt &y) { return norm(x - y) <= 1e-9 * std::max({1.0, std::abs(x.x), std::abs(x.y)}); };
		if (stop_i < 0 && budget_i < 0 && !ph.empty() && (ph.back().kind == EventKind::Origin || ph.back().kind == EventKind::Escape)
				&& steps_after(esph, int(ph.size()) - 1) > nK) {
			const CastEvent &last = ph.back();
			const Pt start = r.near_kind == End::Near ? r.near_pt : (r.near_kind == End::PoleP ? res_.pk : res_.qk);
			if (last.kind == EventKind::Origin && last.has_pt) {
				if (!same(last.pt, start) && !same(last.pt, res_.pk)) {
					lap_kind = End::Stop;
					lap_pt = last.pt;
				}
			} else if (K > 0 && !plan_.back().is_infinity() && !changes_[K - 1]) {
				Pt o = res_.player;
				for (size_t i = 0; i < ph.size() && steps_after(esph, int(i)) <= nK; i++) {
					const CastEvent &e = ph[i];
					if (e.kind == EventKind::Hit && e.applied && !e.terminal && e.has_pt) {
						o = e.pt;
						const Surf<F64> *surf = scene_.find(e.sid);
						if (surf != nullptr && surf->portal) {
							if (const std::optional<CN<F64>> z = effects::map_point(std::optional<CN<F64>>(cn_of(o)), *surf, false)) {
								o = pt_of(*z);
							}
						}
					}
				}
				if (!same(o, start)) {
					if (same(o, res_.pk)) {
						lap_kind = End::PoleP;
					} else if (!res_.qk_inf && same(o, res_.qk)) {
						lap_kind = End::PoleQ;
					} else {
						lap_kind = End::Stop;
						lap_pt = o;
					}
				}
			}
			if (lap_kind == End::Stop) {
				lap_i = int(ph.size()) - 1;
			}
		}
		r.alive = alive;
		// a whisker point is one a cursor there aims at along this member: on its owned arc
		r.whisker = !alive && reached && K > 0 && r.near_kind == End::Near && owned_at(member, c, r.near_pt);
		if (r.whisker && r.has_tie) {
			// Stage 111: the other orientation's reach owns the departure point
			if (!r.tie_after_q) {
				r.whisker = false;
			} else {
				const double tq = tq_of(member);
				const double tie_to = member.param_of(cn_of(r.tie_depart_pt)).v;
				const double td = member.param_of(cn_of(r.near_pt)).v;
				if (walk_key(td, tie_to) <= walk_key(tq, tie_to)) {
					r.whisker = false;
				}
			}
		}
		if (stop_i >= 0 && (budget_i < 0 || stop_i <= budget_i)) {
			r.end_kind = End::Stop;
			r.end_sid = ph[size_t(stop_i)].sid;
			r.end_pt = ph[size_t(stop_i)].pt;
			r.end_rehit = ph[size_t(stop_i)].rehit;
		} else if (budget_i >= 0) {
			const CastEvent &b = ph[size_t(budget_i)];
			if (b.kind == EventKind::Hit) {
				r.end_kind = End::Budget;
				r.end_sid = b.sid;
				r.end_pt = b.pt;
			} else if (b.kind == EventKind::Infinity || b.sid == INFINITY_ID || !b.has_pt) {
				r.end_kind = End::PoleQ;
			} else {
				r.end_kind = End::Budget;
				r.end_sid = carrier_through(b.pt);
				r.end_pt = b.pt;
			}
		} else if (lap_kind == End::PoleP || lap_kind == End::PoleQ) {
			r.end_kind = lap_kind;
		} else if (lap_i >= 0) {
			r.end_kind = End::Stop;
			r.end_pt = lap_pt;
			r.end_sid = -1;
			for (int i = lap_i - 1; i > eK && r.end_sid < 0; i--) {
				if (ph[size_t(i)].kind == EventKind::Hit && ph[size_t(i)].has_pt && ph[size_t(i)].pt == r.end_pt) {
					r.end_sid = ph[size_t(i)].sid;
				}
			}
			if (r.end_sid < 0) {
				r.end_sid = carrier_through(r.end_pt);
			}
		} else {
			r.end_kind = End::Loop;
		}
		if (alive) {
			intervals(r, member);
		}
		return r;
	}

	// the surface whose carrier passes nearest p (a max event names none)
	int carrier_through(const Pt &p) const {
		int best = -1;
		double bd = INF;
		for (const auto &s : scene_.surfaces) {
			double d;
			if (s->line) {
				const Pt S{s->sx, s->sy};
				const Pt dir = unit(Pt{s->ex - s->sx, s->ey - s->sy});
				d = std::abs(cross(dir, p - S));
			} else {
				d = std::abs(norm(p - Pt{s->cx, s->cy}) - s->r);
			}
			if (d < bd) {
				bd = d;
				best = s->id;
			}
		}
		return best;
	}

	// p lies on the arc of member b (with the cursor c on it) that c's aim owns
	bool owned_at(const Beam<F64> &b, const Pt &c, const Pt &p) const {
		const double tq = tq_of(b);
		const double tc = b.param_of(cn_of(c)).v;
		const double t = b.param_of(cn_of(p)).v;
		if (t == 0.0 || t == tq) {
			return false;
		}
		const bool own_a = walk_key(tc, 0.0) < walk_key(tq, 0.0);
		return (walk_key(t, 0.0) < walk_key(tq, 0.0)) == own_a;
	}

	double tq_of(const Beam<F64> &b) const {
		if (res_.qk_inf) {
			return INF;
		}
		return b.param_of(cn_of(res_.qk)).v;
	}

	// The member's visible intervals: the walk from the near end to the far end,
	// inside the owned arc.
	void intervals(Cast &r, const Beam<F64> &b) const {
		const double tq = tq_of(b);
		const double tc = b.param_of(cn_of(r.cursor)).v;
		const bool own_a = walk_key(tc, 0.0) < walk_key(tq, 0.0);
		auto owned = [&](double t) {
			if (t == 0.0 || t == tq) {
				return false;
			}
			return (walk_key(t, 0.0) < walk_key(tq, 0.0)) == own_a;
		};
		double t0 = 0.0;
		if (r.near_kind == End::PoleQ) {
			t0 = tq;
		} else if (r.near_kind == End::Near) {
			t0 = b.param_of(cn_of(r.near_pt)).v;
		}
		// a stop at the departure point comes after the full loop (it is re-hit last)
		const Pt start_pt = r.near_kind == End::Near ? r.near_pt : (r.near_kind == End::PoleP ? res_.pk : res_.qk);
		// (after a planned infinity last, the departure re-hit is a stop at the
		// departure point: the pieces passed before the wrap were not reached)
		const bool loop = r.end_kind == End::Loop || (r.end_kind == End::Stop && r.end_rehit && r.near_kind != End::PoleQ)
			|| ((r.end_kind == End::Stop || r.end_kind == End::Budget) && r.end_pt == start_pt && r.near_kind != End::PoleQ);
		double te = t0;
		if (r.end_kind == End::PoleQ) {
			te = tq;
		} else if (r.end_kind == End::PoleP) {
			te = 0.0;
		} else if ((r.end_kind == End::Stop || r.end_kind == End::Budget) && !loop) {
			te = b.param_of(cn_of(r.end_pt)).v;
		}
		const Key ke = loop ? Key{2, 0.0} : walk_key(te, t0);
		std::vector<Mark> marks{{r.near_kind, t0}};
		std::vector<Mark> bps;
		for (const Mark &m : {Mark{End::PoleP, 0.0}, Mark{End::PoleQ, tq}}) {
			if (m.t != t0 && walk_key(m.t, t0) < ke) {
				bps.push_back(m);
			}
		}
		// Stage 111: the other orientation's planned reach is the plan's
		// realization: its whole loop (nothing is left for this walk), or, after
		// a planned infinity last, from Q_K round to its departure -- which runs
		// the other way along the member: the arc from its departure forward to
		// Q_K in this walk's order
		double tie_to = 0.0; // its departure, on b
		if (r.has_tie) {
			if (!r.tie_after_q) {
				return;
			}
			tie_to = b.param_of(cn_of(r.tie_depart_pt)).v;
			const Mark m{End::TieNear, tie_to};
			if (m.t != t0 && walk_key(m.t, t0) < ke && !std::any_of(bps.begin(), bps.end(), [&](const Mark &x) { return x.t == m.t; })) {
				bps.push_back(m);
			}
		}
		auto in_tie = [&](double t) { return r.has_tie && walk_key(t, tie_to) <= walk_key(tq, tie_to); };
		std::sort(bps.begin(), bps.end(), [&](const Mark &a, const Mark &c) { return walk_key(a.t, t0) < walk_key(c.t, t0); });
		marks.insert(marks.end(), bps.begin(), bps.end());
		marks.push_back(Mark{r.end_kind, te});
		for (size_t i = 0; i + 1 < marks.size(); i++) {
			const bool full = loop && i == 0 && marks.size() == 2;
			const double mid = between(marks[i].t, marks[i + 1].t, full);
			if (owned(mid) && !in_tie(mid)) {
				r.iv.push_back(Interval{marks[i], marks[i + 1]});
			}
		}
	}

	void cast_members() {
		std::vector<Member> &ms = res_.members;
		const size_t n = ms.size();
		// every member's own cast and its two side casts, in one parallel batch
		std::vector<CastJob> jobs;
		std::vector<std::pair<size_t, int>> slot; // (member, 0 crit / 1 minus / 2 plus)
		for (size_t i = 0; i < n; i++) {
			Member &m = ms[i];
			m.beam = member_of(m.cursor);
			if (m.straight) {
				// exactly straight, run the way the circles near it run
				const Pt u = unit(res_.qk - res_.pk);
				const double sg = dot(pt_of(m.beam.tau), u) >= 0.0 ? 1.0 : -1.0;
				m.beam = Beam<F64>{cn_of(res_.pk), cn_of(u * sg), F64{0.0}};
			}
			m.tq = tq_of(m.beam);
			m.own_a = walk_key(m.beam.param_of(cn_of(m.cursor)).v, 0.0) < walk_key(m.tq, 0.0);
			jobs.push_back(CastJob{m.cursor, m.dir, m.beam, CastKind::Crit});
			slot.emplace_back(i, 0);
		}
		for (size_t i = 0; i < n; i++) {
			const Pt &prev = ms[(i + n - 1) % n].dir;
			const Pt &next = ms[(i + 1) % n].dir;
			const double gap = std::min(ccw_angle(prev, ms[i].dir), ccw_angle(ms[i].dir, next));
			const double eps = std::min(1e-6, gap / 8.0);
			for (int side : {1, 2}) {
				const Pt d = rotate(ms[i].dir, side == 1 ? -eps : eps);
				if (const std::optional<Pt> c = cursor_for(d)) {
					jobs.push_back(CastJob{*c, d, member_of(*c), CastKind::Side});
					slot.emplace_back(i, side);
				} else {
					(side == 1 ? ms[i].minus : ms[i].plus) = Cast{};
				}
			}
		}
		std::vector<Cast> casts = cast_batch(jobs);
		for (size_t k = 0; k < jobs.size(); k++) {
			Member &m = ms[slot[k].first];
			(slot[k].second == 0 ? m.crit : slot[k].second == 1 ? m.minus : m.plus) = std::move(casts[k]);
		}
	}

	Cast side_cast(const Pt &dir, double eps) {
		const Pt d = rotate(dir, eps);
		const std::optional<Pt> c = cursor_for(d);
		if (!c) {
			return Cast{};
		}
		return cast(*c, d, member_of(*c), CastKind::Side);
	}

	// ---- cells

	// Where an interval mark sits on member m, read from the cast `src` next to it:
	// a pole, or the crossing of m with the carrier src's walk names that lies
	// nearest the point src reached (the same computation for every cell that
	// shares the vertex, so shared vertices are equal).
	Vertex vertex_on(int mi, End kind, const Cast &src) const {
		const Member &m = res_.members[size_t(mi)];
		Vertex v;
		v.member = mi;
		if (kind == End::Loop) {
			kind = src.near_kind == End::Near ? End::Loop : src.near_kind; // a loop closes where it began: at a pole when it began there
		}
		if (kind == End::PoleP) {
			v.p = res_.pk;
			v.t = 0.0;
			v.pole = true;
			return v;
		}
		if (kind == End::PoleQ) {
			if (res_.qk_inf) {
				v.inf = true;
				v.t = INF;
				return v;
			}
			v.p = res_.qk;
			v.t = m.tq;
			v.pole = true;
			return v;
		}
		const bool near = kind == End::Near || kind == End::Loop;
		const Pt target = kind == End::TieNear ? src.tie_depart_pt : near ? src.near_pt : src.end_pt;
		// a walk that stops (or departs) at a pole -- a carrier through Q_K is
		// crossed exactly there (Stage 105) -- ends at the pole itself
		const double tol = 1e-9 * std::max({1.0, std::abs(target.x), std::abs(target.y)});
		const bool at_p = norm(target - res_.pk) <= tol;
		if (at_p || (!res_.qk_inf && norm(target - res_.qk) <= tol)) {
			v.p = at_p ? res_.pk : res_.qk;
			v.t = at_p ? 0.0 : m.tq;
			v.pole = true;
			return v;
		}
		return root_on(mi, kind == End::TieNear ? src.tie_depart_sid : near ? src.near_sid : src.end_sid, target);
	}

	// Stage 111: the carrier and the point a mark of this kind lies on
	static std::pair<int, Pt> mark_carrier(const Cast &c, End kind) {
		switch (kind) {
			case End::TieNear: return {c.tie_depart_sid, c.tie_depart_pt};
			case End::Near:
			case End::Loop: return {c.near_sid, c.near_pt};
			default: return {c.end_sid, c.end_pt};
		}
	}
	// the role a carrier edge of this mark kind plays (a tie cut is a departure or a budget cut)
	static End role_of(End kind) { return kind == End::TieNear ? End::Near : kind; }

	Vertex root_on(int mi, int sid, const Pt &target) const {
		const Member &m = res_.members[size_t(mi)];
		Vertex v;
		v.member = mi;
		if (sid >= 0 && sid == m.tangent_q_sid && !res_.qk_inf) {
			// the member touches this carrier at Q_K: its double root is the pole
			v.p = res_.qk;
			v.t = m.tq;
			v.pole = true;
			return v;
		}
		if (sid >= 0 && sid == m.tangent_sid) {
			// the member touches this carrier: its two crossings are one point,
			// the tangent point (the member's own point of interest)
			v.p = m.cursor;
			v.t = m.beam.param_of(cn_of(m.cursor)).v;
			return v;
		}
		const Surf<F64> *surf = scene_.find(sid);
		double best = INF;
		if (surf != nullptr) {
			for (const F64 &t : geom::hits(m.beam, surf->seg, false)) {
				const std::optional<CN<F64>> z = t.is_inf() ? m.beam.at_infinity() : std::optional<CN<F64>>(m.beam.at(t));
				if (!z) {
					continue;
				}
				const Pt p = pt_of(*z);
				const double d = norm(p - target);
				if (d < best) {
					best = d;
					v.p = p;
					v.t = t.v;
				}
			}
		}
		// the member's own point of interest, where a root lands on it (a corner, a
		// crossing): that exact point, so the cells on both sides share it
		if (best < INF && m.poi && norm(v.p - m.cursor) <= 1e-9 * std::max({1.0, std::abs(v.p.x), std::abs(v.p.y)})) {
			v.p = m.cursor;
			v.t = m.beam.param_of(cn_of(m.cursor)).v;
		}
		if (!(best < INF)) {
			// no crossing: the member touches the carrier (a tangency member, whose
			// rounding lost the double root) -- the point of the carrier nearest the
			// target, the tangent point
			Pt q = target;
			auto off = [&](const Pt &x) {
				return surf->line ? std::abs(cross(unit(Pt{surf->ex - surf->sx, surf->ey - surf->sy}), x - Pt{surf->sx, surf->sy}))
								  : std::abs(norm(x - Pt{surf->cx, surf->cy}) - surf->r);
			};
			if (surf != nullptr && m.poi && off(m.cursor) <= 1e-6 * std::max({1.0, std::abs(m.cursor.x), std::abs(m.cursor.y)})) {
				q = m.cursor; // the member's own tangent point
			} else if (surf != nullptr) {
				if (surf->line) {
					const Pt S{surf->sx, surf->sy};
					const Pt d = unit(Pt{surf->ex - surf->sx, surf->ey - surf->sy});
					q = S + d * dot(target - S, d);
				} else {
					const Pt c{surf->cx, surf->cy};
					q = c + unit(target - c) * surf->r;
				}
			}
			v.p = q;
			v.t = m.beam.param_of(cn_of(q)).v;
		}
		// a carrier through a pole (a finite Q_K, Stage 105; P_K when the player
		// stands on it) meets every member there: a root that close to the pole (a
		// double root's rounding) is the pole
		if (surf != nullptr) {
			for (int k = 0; k < 2; k++) {
				if (k == 1 && res_.qk_inf) {
					continue;
				}
				const Pt pole = k == 0 ? res_.pk : res_.qk;
				const double sc = std::max({1.0, std::abs(pole.x), std::abs(pole.y)});
				const double on = surf->line ? std::abs(cross(unit(Pt{surf->ex - surf->sx, surf->ey - surf->sy}), pole - Pt{surf->sx, surf->sy}))
											 : std::abs(norm(pole - Pt{surf->cx, surf->cy}) - surf->r);
				if (on <= 1e-9 * sc && norm(v.p - pole) <= 1e-5 * sc) {
					v.p = pole;
					v.t = k == 0 ? 0.0 : m.tq;
					v.pole = true;
					return v;
				}
			}
		}
		// a straight member (nearly) parallel to a line carrier meets it at
		// infinity, at the end the cast reached it from: no root, or one far off on
		// the wrong side (a refinement member at the switch through infinity)
		if (surf != nullptr && surf->line && m.beam.kappa.is_zero()) {
			const Rect &dm = res_.domain;
			const double far = 1e6 * (1.0 + (dm.x1 - dm.x0) + (dm.y1 - dm.y0));
			if (!(best < INF) || norm(v.p - res_.pk) > far || norm(target - res_.pk) > far) {
				const double tt = m.beam.param_of(cn_of(target)).v;
				v.inf = true;
				v.inf_sign = tt >= 0.0 ? 1 : -1;
				v.t = tt >= 0.0 ? INF : -INF;
				v.p = Pt{};
			}
		}
		return v;
	}

	// Whether parameter t of member m is in the member's own visible set
	// (closed at the near, stop, budget and loop ends, open at the poles).
	bool in_extent(int mi, double t) const {
		const Member &m = res_.members[size_t(mi)];
		if (!m.crit.alive) {
			return false;
		}
		const double t_start = m.own_a ? 0.0 : m.tq;
		const Key k = walk_key(t, t_start);
		for (const Interval &iv : m.crit.iv) {
			const Vertex va = vertex_on(mi, iv.a.kind, m.crit);
			const Vertex vb = vertex_on(mi, iv.b.kind, m.crit);
			const Key ka = walk_key(va.t, t_start);
			const Key kb = iv.b.kind == End::Loop && va.t == vb.t ? Key{2, 0.0} : walk_key(vb.t, t_start);
			const bool closed_a = !va.pole && !va.inf;
			const bool closed_b = !vb.pole && !vb.inf;
			if ((closed_a ? k >= ka : k > ka) && (closed_b ? k <= kb : k < kb)) {
				return true;
			}
		}
		return false;
	}

	// Whether a boundary vertex on member m is in the region: at the member's own
	// point of interest, what its cast says of a cursor exactly there (events in
	// that very point change no step before it); elsewhere the member's own
	// visible set.
	bool vertex_in(int mi, const Pt &p, double t) const {
		const Member &m = res_.members[size_t(mi)];
		if (m.poi && p == m.cursor) {
			return m.crit.cursor_ok;
		}
		return in_extent(mi, t);
	}

	Key arc_key(int mi, double t) const {
		const Member &m = res_.members[size_t(mi)];
		return walk_key(t, m.own_a ? 0.0 : m.tq);
	}

	Edge member_edge(int mi, const Vertex &a, const Vertex &b, bool forward) const {
		const Member &m = res_.members[size_t(mi)];
		Edge e;
		e.kind = EdgeKind::Member;
		e.member = mi;
		e.a = a.p;
		e.b = b.p;
		e.ta = a.t;
		e.tb = b.t;
		e.arc = !m.beam.kappa.is_zero();
		set_member_geom(e, m);
		const double tm = forward ? between(a.t, b.t) : between(b.t, a.t);
		if (std::isfinite(tm) || e.arc) {
			const std::optional<CN<F64>> z = std::isinf(tm) ? m.beam.at_infinity() : std::optional<CN<F64>>(m.beam.at(F64{tm}));
			e.via = z ? pt_of(*z) : (a.p + b.p) * 0.5;
		} else {
			e.via = (a.p + b.p) * 0.5;
		}
		return e;
	}

	Edge carrier_edge(int sid, End role, const Vertex &a, const Vertex &b, const Pt &via) const {
		Edge e;
		e.kind = EdgeKind::Carrier;
		e.surface = sid;
		e.role = role;
		e.a = a.p;
		e.b = b.p;
		e.via = via;
		const Surf<F64> *surf = scene_.find(sid);
		e.arc = surf != nullptr && !surf->line;
		if (surf != nullptr) {
			if (surf->line) {
				e.gline = true;
				e.gp = Pt{surf->sx, surf->sy};
				e.gd = unit(Pt{surf->ex - surf->sx, surf->ey - surf->sy});
			} else {
				e.gline = false;
				e.gc = Pt{surf->cx, surf->cy};
				e.gr = surf->r;
			}
		}
		e.inclusive = true; // a cursor on a stop is reached first (ties: the cursor); on a departure point at once
		return e;
	}

	static void set_member_geom(Edge &e, const Member &m) {
		const Pt P = pt_of(m.beam.p);
		const Pt t = pt_of(m.beam.tau);
		const double k = m.beam.kappa.v;
		if (k == 0.0) {
			e.gline = true;
			e.gp = P;
			e.gd = t;
		} else {
			e.gline = false;
			e.gc = P + perp(t) * (1.0 / k);
			e.gr = 1.0 / std::abs(k);
		}
	}

	struct Proto {
		Vertex as, ae, bs, be; // left start / end, right start / end
		End ka, kb;
		bool near_incl = true; // a cursor on the departure piece is reached (the solver's answer)
		int sid_a = -1, sid_b = -1;
		Pt via_a, via_b; // the mid cast's points on the near and far carriers
		Pt inside;
	};

	void make_protos(int li, int ri, const Cast &mid, std::vector<Proto> &out) const {
		const Member &L = res_.members[size_t(li)];
		const Member &R = res_.members[size_t(ri)];
		const Beam<F64> mb = member_of(mid.cursor);
		for (const Interval &iv : mid.iv) {
			Proto p;
			p.ka = iv.a.kind;
			p.kb = iv.b.kind;
			p.as = vertex_on(li, p.ka, L.plus);
			p.ae = vertex_on(li, p.kb, L.plus);
			p.bs = vertex_on(ri, p.ka, R.minus);
			p.be = vertex_on(ri, p.kb, R.minus);
			std::tie(p.sid_a, p.via_a) = mark_carrier(mid, p.ka);
			const bool loop = p.kb == End::Loop;
			std::tie(p.sid_b, p.via_b) = mark_carrier(mid, p.kb);
			const double tm = between(iv.a.t, iv.b.t, loop && iv.a.t == iv.b.t);
			const std::optional<CN<F64>> z = std::isinf(tm) ? mb.at_infinity() : std::optional<CN<F64>>(mb.at(F64{tm}));
			p.inside = z ? pt_of(*z) : mid.cursor;
			out.push_back(p);
		}
	}

	struct Gap {
		std::optional<Cast> mid;
		std::vector<Proto> protos;
	};

	// The two casts' shapes differ between directions lo and hi: bisect to the
	// switch and put a member there (its sides: the last casts of each shape).
	bool refine(size_t gi, std::vector<Gap> &gaps, const Cast &lo_cast, const Cast &hi_cast) {
		Cast lo = lo_cast;
		Cast hi = hi_cast;
		for (int it = 0; it < 48 && ccw_angle(lo.dir, hi.dir) > 1e-11; it++) {
			const Pt d = rotate(lo.dir, ccw_angle(lo.dir, hi.dir) / 2.0);
			const std::optional<Pt> c = cursor_for(d);
			if (!c) {
				break;
			}
			Cast m = cast(*c, d, member_of(*c), CastKind::Refine);
			if (m.same_shape(lo)) {
				lo = std::move(m);
			} else {
				hi = std::move(m);
			}
		}
		const Pt d = rotate(lo.dir, ccw_angle(lo.dir, hi.dir) / 2.0);
		// the switch lies strictly inside the gap (at a bounding member it is that
		// member's own: nothing to insert)
		const Pt &dl = res_.members[gi].dir;
		const Pt &dr = res_.members[(gi + 1) % res_.members.size()].dir;
		if (dir_equal(d, dl) || dir_equal(d, dr) || ccw_angle(dl, d) >= ccw_angle(dl, dr)) {
			return false;
		}
		const std::optional<Pt> c = cursor_for(d);
		if (!c) {
			return false;
		}
		Member nm;
		nm.dir = d;
		nm.cursor = *c;
		nm.why = "refinement (" + lo.shape() + " | " + hi.shape() + ")";
		nm.beam = member_of(*c);
		nm.tq = tq_of(nm.beam);
		nm.own_a = walk_key(nm.beam.param_of(cn_of(*c)).v, 0.0) < walk_key(nm.tq, 0.0);
		nm.crit = cast(*c, d, nm.beam, CastKind::Refine);
		nm.minus = std::move(lo);
		nm.plus = std::move(hi);
		auto &ms = res_.members;
		ms.insert(ms.begin() + long(gi) + 1, std::move(nm));
		gaps[gi].mid.reset();
		gaps.insert(gaps.begin() + long(gi) + 1, Gap{});
		res_.refinements++;
		return true;
	}

	void build_cells() {
		auto &ms = res_.members;
		if (ms.size() < 2) {
			if (half_ == Half::Single) {
				res_.notes.push_back("fewer than two members");
			}
			return;
		}
		std::vector<Gap> gaps(ms.size());
		const int MAX_REFINE = 64;
		// the gaps' middle casts, in one parallel batch up front, keyed by the
		// direction (the loop below takes them where it would cast; gaps a
		// refinement creates are cast there)
		auto skip_gap = [&](size_t i, size_t r) {
			return dir_equal(ms[i].dir, ms[r].dir) || (ms.size() > 4 && ccw_angle(ms[i].dir, ms[r].dir) > PI);
		};
		auto dir_key2 = [](const Pt &d) { return std::make_pair(std::bit_cast<uint64_t>(d.x), std::bit_cast<uint64_t>(d.y)); };
		std::map<std::pair<uint64_t, uint64_t>, Cast> pre_mid;
		{
			std::vector<CastJob> jobs;
			for (size_t i = 0; i < ms.size(); i++) {
				const size_t r = (i + 1) % ms.size();
				if (skip_gap(i, r)) {
					continue;
				}
				const Pt dm = rotate(ms[i].dir, ccw_angle(ms[i].dir, ms[r].dir) / 2.0);
				if (const std::optional<Pt> c = cursor_for(dm)) {
					jobs.push_back(CastJob{*c, dm, member_of(*c), CastKind::Mid});
				}
			}
			std::vector<Cast> casts = cast_batch(jobs);
			for (size_t k = 0; k < jobs.size(); k++) {
				pre_mid.emplace(dir_key2(jobs[k].dir), std::move(casts[k]));
			}
		}
		bool changed = true;
		while (changed) {
			changed = false;
			for (size_t i = 0; i < ms.size(); i++) {
				if (gaps[i].mid) {
					continue;
				}
				const size_t r = (i + 1) % ms.size();
				if (skip_gap(i, r)) {
					// no gap between one direction and itself (the axes keep every
					// real gap below a quarter turn: a wider one is a rounding reversal)
					gaps[i].mid = Cast{};
					continue;
				}
				const Pt dm = rotate(ms[i].dir, ccw_angle(ms[i].dir, ms[r].dir) / 2.0);
				if (auto pm = pre_mid.find(dir_key2(dm)); pm != pre_mid.end()) {
					gaps[i].mid = std::move(pm->second);
					pre_mid.erase(pm);
				} else {
					const std::optional<Pt> c = cursor_for(dm);
					gaps[i].mid = c ? cast(*c, dm, member_of(*c), CastKind::Mid) : Cast{};
				}
				const Cast &mid = *gaps[i].mid;
				const bool left_ok = ms[i].plus.same_shape(mid);
				const bool right_ok = ms[r].minus.same_shape(mid);
				if ((!left_ok || !right_ok) && res_.refinements < MAX_REFINE) {
					const bool ok = !left_ok ? refine(i, gaps, ms[i].plus, mid) : refine(i, gaps, mid, ms[r].minus);
					if (ok) {
						changed = true;
						break;
					}
				}
				if (!left_ok || !right_ok) {
					res_.notes.push_back("cell " + std::to_string(i) + ": shapes differ: " + ms[i].plus.shape() + " | " + mid.shape() + " | "
						+ ms[r].minus.shape());
				}
			}
		}
		// the cells' corners, then the clip rectangle around everything finite
		Rect clip = res_.domain;
		for (size_t i = 0; i < ms.size(); i++) {
			const Cast &mid = *gaps[i].mid;
			if (!mid.valid || !mid.alive || !ms[i].plus.same_shape(mid) || !ms[(i + 1) % ms.size()].minus.same_shape(mid)) {
				continue;
			}
			make_protos(int(i), int((i + 1) % ms.size()), mid, gaps[i].protos);
			// K7: a cursor exactly at a departure point is reached at once (the cursor
			// is a known point of the exact beam that crosses there), and the cell's
			// middle cast is alive -- both walks leave the point in one frame (V3):
			// the departure points belong to the region
			for (Proto &p : gaps[i].protos) {
				if (p.ka == End::Near) {
					p.near_incl = true;
				}
			}
			for (const Proto &p : gaps[i].protos) {
				for (const Vertex *v : {&p.as, &p.ae, &p.bs, &p.be}) {
					if (!v->inf && v->p.finite()) {
						clip.add(v->p);
					}
				}
			}
		}
		clip.add(res_.pk); // every member's line is clipped from P_K (one exit point per member and way)
		res_.clip = clip.grown(64.0);
		// whiskers: the departure points of gaps whose walks die at the last entry
		for (size_t i = 0; i < ms.size(); i++) {
			const Cast &mid = *gaps[i].mid;
			const Member &L = ms[i];
			const Member &R = ms[(i + 1) % ms.size()];
			if (!mid.whisker || !L.plus.whisker || !R.minus.whisker || !mid.same_shape(L.plus) || !mid.same_shape(R.minus)) {
				continue;
			}
			Vertex a = vertex_on(int(i), End::Near, L.plus);
			Vertex b = vertex_on(int((i + 1) % ms.size()), End::Near, R.minus);
			if (a.inf && b.inf) {
				continue;
			}
			// an end at infinity (a carrier parallel to its member): far out along the carrier
			if (a.inf) {
				a.p = carrier_far(mid.near_sid, b.p, mid.near_pt);
				a.inf = false;
			}
			if (b.inf) {
				b.p = carrier_far(mid.near_sid, a.p, mid.near_pt);
				b.inf = false;
			}
			// the solver's own answer at a departure point (reached at once, or only
			// after a loop past the divergence), at a point exactly on the carrier
			// as a cursor there is
			const Pt probe = exact_on_carrier(mid.near_sid, mid.near_pt);
			if (!cast(probe, mid.dir, member_of(probe), CastKind::Probe).cursor_ok) {
				continue;
			}
			Edge e = carrier_edge(mid.near_sid, End::Near, a, b, mid.near_pt);
			// K7: an end that is a member's exact point of interest (a segment's
			// endpoint, say) is in the region exactly when that member's own cast
			// reaches it there -- at a planned terminal's endpoint the walks diverge
			auto end_in = [](const Member &m, const Pt &p) { return !(m.exact && m.cursor == p) || m.crit.cursor_ok; };
			e.incl_a = end_in(L, e.a);
			e.incl_b = end_in(R, e.b);
			if (e.a == e.b) {
				continue;
			}
			if (e.gline && dot(e.a - e.via, e.b - e.via) > 0.0) {
				// a line piece through infinity: two pieces, out of the rectangle each way
				Vertex fa, fb;
				fa.p = carrier_far(mid.near_sid, a.p, a.p + (a.p - b.p));
				fb.p = carrier_far(mid.near_sid, b.p, b.p + (b.p - a.p));
				Edge e1 = carrier_edge(mid.near_sid, End::Near, a, fa, (a.p + fa.p) * 0.5);
				Edge e2 = carrier_edge(mid.near_sid, End::Near, fb, b, (fb.p + b.p) * 0.5);
				e1.incl_a = e.incl_a;
				e1.incl_b = true;
				e2.incl_a = true;
				e2.incl_b = e.incl_b;
				res_.whiskers.push_back(e1);
				res_.whiskers.push_back(e2);
			} else {
				res_.whiskers.push_back(e);
			}
		}
		// a member whose own cast sees it while the casts on either side do not
		// (the member tangent at Q to a carrier through Q): its visible arcs are
		// whiskers of their own
		for (size_t i = 0; i < ms.size(); i++) {
			const Member &m = ms[i];
			if (!m.crit.visible() || m.minus.visible() || m.plus.visible()) {
				continue;
			}
			for (const Interval &iv : m.crit.iv) {
				const Vertex a = vertex_on(int(i), iv.a.kind, m.crit);
				const Vertex b = vertex_on(int(i), iv.b.kind, m.crit);
				for (Edge e : member_walk(int(i), a, b)) {
					if (e.connector || e.a == e.b) {
						continue;
					}
					e.inclusive = true;
					// (the member's own cast sees its whole arc; an end at a pole is never in the region)
					e.incl_a = !(a.pole && e.a == a.p);
					e.incl_b = !(b.pole && e.b == b.p);
					res_.whiskers.push_back(e);
				}
			}
		}
		for (size_t i = 0; i < ms.size(); i++) {
			int layer = 0;
			for (const Proto &p : gaps[i].protos) {
				Cell cell;
				cell.left = int(i);
				cell.right = int((i + 1) % ms.size());
				cell.mid = *gaps[i].mid;
				cell.mid_beam = member_of(cell.mid.cursor);
				cell.inside = p.inside;
				cell.loops = cell_edges(cell.left, cell.right, p, layer++);
				res_.cells.push_back(std::move(cell));
			}
		}
	}

	// ---- clipping: every cell is cut by the clip rectangle (Weiler-Atherton
	// against a convex window); a cell through infinity runs outside it there

	// Where the ray from p along d leaves the clip rectangle (p inside it).
	Pt exit_point(const Pt &p, const Pt &d) const {
		const Rect &c = res_.clip;
		double lam = INF;
		if (d.x > 0) lam = std::min(lam, (c.x1 - p.x) / d.x);
		if (d.x < 0) lam = std::min(lam, (c.x0 - p.x) / d.x);
		if (d.y > 0) lam = std::min(lam, (c.y1 - p.y) / d.y);
		if (d.y < 0) lam = std::min(lam, (c.y0 - p.y) / d.y);
		return p + d * lam;
	}

	// A point far out along member m's line (straight members), the way s.
	Pt far_point(int mi, double s) const {
		const Member &m = res_.members[size_t(mi)];
		const Pt d = pt_of(m.beam.tau) * s;
		const Rect &c = res_.clip;
		return exit_point(pt_of(m.beam.p), d) + d * (2.0 * ((c.x1 - c.x0) + (c.y1 - c.y0)));
	}

	double perimeter_pos(const Pt &p) const {
		const Rect &c = res_.clip;
		const double W = c.x1 - c.x0;
		const double H = c.y1 - c.y0;
		if (p.y == c.y0) return p.x - c.x0;
		if (p.x == c.x1) return W + (p.y - c.y0);
		if (p.y == c.y1) return W + H + (c.x1 - p.x);
		return 2 * W + H + (c.y1 - p.y);
	}
	Pt corner(int k) const {
		const Rect &c = res_.clip;
		switch (k & 3) {
			case 0: return {c.x0, c.y0};
			case 1: return {c.x1, c.y0};
			case 2: return {c.x1, c.y1};
			default: return {c.x0, c.y1};
		}
	}

	Edge bounds_edge(const Pt &a, const Pt &b) const {
		Edge e;
		e.kind = EdgeKind::Bounds;
		e.a = a;
		e.b = b;
		e.via = (a + b) * 0.5;
		e.gline = true;
		e.gp = a;
		e.gd = unit(b - a);
		return e;
	}

	// The rectangle's boundary from x to y, counterclockwise (dir > 0) or clockwise.
	std::vector<Edge> boundary_path(const Pt &x, const Pt &y, int dir) const {
		const Rect &c = res_.clip;
		const double W = c.x1 - c.x0;
		const double H = c.y1 - c.y0;
		const double L = 2 * (W + H);
		const double sx = perimeter_pos(x);
		const double sy = perimeter_pos(y);
		const double len = std::fmod((dir > 0 ? sy - sx : sx - sy) + 2 * L, L);
		std::vector<std::pair<double, Pt>> along;
		const double cpos[4] = {0.0, W, W + H, 2 * W + H};
		for (int k = 0; k < 4; k++) {
			const double off = std::fmod((dir > 0 ? cpos[k] - sx : sx - cpos[k]) + 2 * L, L);
			if (off > 0 && off < len) along.emplace_back(off, corner(k));
		}
		std::sort(along.begin(), along.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		std::vector<Pt> pts{x};
		for (const auto &a : along) pts.push_back(a.second);
		pts.push_back(y);
		std::vector<Edge> out;
		for (size_t i = 0; i + 1 < pts.size(); i++) {
			if (!(pts[i] == pts[i + 1])) out.push_back(bounds_edge(pts[i], pts[i + 1]));
		}
		return out;
	}

	// Where the edge's carrier crosses the rectangle's sides strictly inside the
	// edge, ordered from a to b (each exactly on its side).
	std::vector<Pt> rect_hits(const Edge &e) const {
		const Rect &c = res_.clip;
		std::vector<Pt> pts;
		if (e.gline) {
			const Pt &p = e.gp;
			const Pt &d = e.gd;
			for (double x : {c.x0, c.x1}) {
				if (d.x != 0.0) {
					const double y = p.y + (x - p.x) / d.x * d.y;
					if (y >= c.y0 && y <= c.y1) pts.push_back({x, y});
				}
			}
			for (double y : {c.y0, c.y1}) {
				if (d.y != 0.0) {
					const double x = p.x + (y - p.y) / d.y * d.x;
					if (x > c.x0 && x < c.x1) pts.push_back({x, y});
				}
			}
		} else {
			for (double x : {c.x0, c.x1}) {
				const double dd = e.gr * e.gr - (x - e.gc.x) * (x - e.gc.x);
				if (dd > 0.0) {
					for (double sg : {-1.0, 1.0}) {
						const double y = e.gc.y + sg * std::sqrt(dd);
						if (y >= c.y0 && y <= c.y1) pts.push_back({x, y});
					}
				}
			}
			for (double y : {c.y0, c.y1}) {
				const double dd = e.gr * e.gr - (y - e.gc.y) * (y - e.gc.y);
				if (dd > 0.0) {
					for (double sg : {-1.0, 1.0}) {
						const double x = e.gc.x + sg * std::sqrt(dd);
						if (x > c.x0 && x < c.x1) pts.push_back({x, y});
					}
				}
			}
		}
		// strictly inside the edge, ordered from a
		std::vector<std::pair<double, Pt>> keyed;
		if (!e.arc) {
			const Pt ab = e.b - e.a;
			const double L2 = dot(ab, ab);
			for (const Pt &q : pts) {
				const double u = L2 > 0.0 ? dot(q - e.a, ab) / L2 : 0.0;
				if (u > 0.0 && u < 1.0 && !(q == e.a) && !(q == e.b)) keyed.emplace_back(u, q);
			}
		} else {
			const ArcGeom g = arc_geom(e);
			const double sw = std::abs(arc_sweep(e, g));
			const double a0 = std::atan2(e.a.y - g.c.y, e.a.x - g.c.x);
			for (const Pt &q : pts) {
				double ang = std::atan2(q.y - g.c.y, q.x - g.c.x) - a0;
				if (!g.ccw) ang = -ang;
				while (ang < 0.0) ang += 2 * PI;
				while (ang >= 2 * PI) ang -= 2 * PI;
				if (ang > 0.0 && ang < sw && !(q == e.a) && !(q == e.b)) keyed.emplace_back(ang, q);
			}
		}
		std::sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		std::vector<Pt> out;
		for (const auto &k : keyed) out.push_back(k.second);
		return out;
	}

	// The edge from a to q, then from q on: the same carrier, a new via.
	Edge piece_of(const Edge &e, const Pt &a, const Pt &b) const {
		Edge f = e;
		f.a = a;
		f.b = b;
		if (e.kind == EdgeKind::Member) {
			// an end the edge had keeps its parameter (a neighbour computed the same
			// one for the same vertex); a crossing with the rectangle gets its own
			const Member &m = res_.members[size_t(e.member)];
			f.ta = a == e.a ? e.ta : m.beam.param_of(cn_of(a)).v;
			f.tb = b == e.b ? e.tb : m.beam.param_of(cn_of(b)).v;
		}
		if (e.arc) {
			const ArcGeom g = arc_geom(e);
			const double a0 = std::atan2(a.y - g.c.y, a.x - g.c.x);
			double sw = std::atan2(b.y - g.c.y, b.x - g.c.x) - a0;
			if (g.ccw) {
				while (sw <= 0.0) sw += 2 * PI;
			} else {
				while (sw >= 0.0) sw -= 2 * PI;
			}
			f.via = Pt{g.c.x + g.r * std::cos(a0 + sw / 2), g.c.y + g.r * std::sin(a0 + sw / 2)};
		} else {
			f.via = (a + b) * 0.5;
		}
		return f;
	}

	bool piece_inside(const Edge &e) const {
		if (e.connector) return false;
		return res_.clip.has(e.via);
	}

	// A closed cell loop (connectors allowed) cut by the clip rectangle.
	std::vector<std::vector<Edge>> clip_loop(const std::vector<Edge> &loop, const Pt &inside) const {
		if (loop.empty()) {
			return {};
		}
		std::vector<Edge> pieces;
		for (const Edge &e : loop) {
			if (e.connector) {
				pieces.push_back(e);
				continue;
			}
			Pt from = e.a;
			for (const Pt &q : rect_hits(e)) {
				pieces.push_back(piece_of(e, from, q));
				from = q;
			}
			pieces.push_back(from == e.a ? e : piece_of(e, from, e.b));
		}
		std::vector<bool> in(pieces.size());
		bool any_in = false;
		bool any_out = false;
		for (size_t i = 0; i < pieces.size(); i++) {
			in[i] = piece_inside(pieces[i]);
			any_in = any_in || in[i];
			any_out = any_out || !in[i];
		}
		if (!any_out) {
			return {pieces};
		}
		const int dir = res_.orientation;
		if (!any_in) {
			if (!res_.clip.has(inside)) {
				return {};
			}
			// the rectangle lies inside the cell
			std::vector<Edge> rect;
			for (int k = 0; k < 4; k++) {
				const int a = dir > 0 ? k : 4 - k;
				const int b = dir > 0 ? k + 1 : 3 - k;
				rect.push_back(bounds_edge(corner(a), corner(b)));
			}
			return {rect};
		}
		// runs of inside pieces: each from an entry point to an exit point
		const size_t n = pieces.size();
		size_t s0 = 0;
		while (!(in[s0] && !in[(s0 + n - 1) % n])) s0++;
		struct Run {
			std::vector<Edge> edges;
			Pt entry;
			Pt exit;
		};
		std::vector<Run> runs;
		for (size_t k = 0; k < n; k++) {
			const size_t i = (s0 + k) % n;
			if (!in[i]) continue;
			if (!in[(i + n - 1) % n]) {
				runs.push_back(Run{{}, pieces[i].a, pieces[i].a});
			}
			runs.back().edges.push_back(pieces[i]);
			runs.back().exit = pieces[i].b;
		}
		const double L = 2 * ((res_.clip.x1 - res_.clip.x0) + (res_.clip.y1 - res_.clip.y0));
		std::vector<bool> used(runs.size(), false);
		std::vector<std::vector<Edge>> out;
		for (size_t r0 = 0; r0 < runs.size(); r0++) {
			if (used[r0]) continue;
			std::vector<Edge> loop_out;
			size_t r = r0;
			for (size_t guard = 0; guard <= runs.size(); guard++) {
				used[r] = true;
				loop_out.insert(loop_out.end(), runs[r].edges.begin(), runs[r].edges.end());
				const double px = perimeter_pos(runs[r].exit);
				size_t best = r0;
				double best_d = INF;
				for (size_t q = 0; q < runs.size(); q++) {
					const double pe = perimeter_pos(runs[q].entry);
					const double d = std::fmod((dir > 0 ? pe - px : px - pe) + 2 * L, L);
					if (d < best_d) {
						best_d = d;
						best = q;
					}
				}
				for (Edge &b : boundary_path(runs[r].exit, runs[best].entry, dir)) loop_out.push_back(b);
				r = best;
				if (r == r0) break;
			}
			out.push_back(std::move(loop_out));
		}
		return out;
	}

	// A point exactly on line carrier sid near p (its start plus a dyadic
	// fraction of its span, exact in float64 for float32 endpoints); p itself
	// on a circle.
	Pt exact_on_carrier(int sid, const Pt &p) const {
		const Surf<F64> *surf = scene_.find(sid);
		if (surf == nullptr || !surf->line) {
			return p;
		}
		const Pt S{surf->sx, surf->sy};
		const Pt D{surf->ex - surf->sx, surf->ey - surf->sy};
		const double dd = dot(D, D);
		if (!(dd > 0.0)) {
			return p;
		}
		const double t = std::round(dot(p - S, D) / dd * 1048576.0) / 1048576.0;
		return S + D * t;
	}

	// A point of line carrier sid far beyond the clip rectangle, from f the way of `toward`.
	Pt carrier_far(int sid, const Pt &f, const Pt &toward) const {
		const Surf<F64> *surf = scene_.find(sid);
		Pt d = surf != nullptr ? unit(Pt{surf->ex - surf->sx, surf->ey - surf->sy}) : unit(toward - f);
		if (dot(d, toward - f) < 0.0) {
			d = d * -1.0;
		}
		const Rect &c = res_.clip;
		return f + d * (norm(f - Pt{(c.x0 + c.x1) / 2, (c.y0 + c.y1) / 2}) + 2.0 * ((c.x1 - c.x0) + (c.y1 - c.y0)));
	}

	Edge connector_edge(const Pt &a, const Pt &b) const {
		Edge e;
		e.connector = true;
		e.kind = EdgeKind::Bounds;
		e.a = a;
		e.b = b;
		e.via = (a + b) * 0.5;
		return e;
	}

	Vertex vertex_at(int mi, const Pt &q) const {
		Vertex v;
		v.member = mi;
		v.p = q;
		v.t = res_.members[size_t(mi)].beam.param_of(cn_of(q)).v;
		return v;
	}

	// Member mi's edges from `from` to `to` in the member's direction. An end at
	// infinity, or a straight member's passage through the real infinity, leaves
	// the rectangle along the member's line (a connector beyond it).
	std::vector<Edge> member_walk(int mi, const Vertex &from, const Vertex &to, bool full_loop = false) const {
		const Member &m = res_.members[size_t(mi)];
		if (!full_loop && !from.inf && !to.inf && norm(from.p - to.p) <= 1e-9 * std::max({1.0, std::abs(from.p.x), std::abs(from.p.y)})) {
			return {}; // a piece of no length (both ends at one pole), not the whole circle
		}
		const bool through = m.straight && !from.inf && !to.inf && (from.t == to.t || walk_key(INF, from.t) < walk_key(to.t, from.t));
		std::vector<Edge> walk;
		if (from.inf && to.inf) {
			return walk; // a pinch at infinity of two straight members: the connector covers it
		}
		if (to.inf) {
			walk.push_back(member_edge(mi, from, vertex_at(mi, far_point(mi, to.inf_sign != 0 ? to.inf_sign : 1.0)), true));
		} else if (from.inf) {
			walk.push_back(member_edge(mi, vertex_at(mi, far_point(mi, from.inf_sign != 0 ? from.inf_sign : -1.0)), to, true));
		} else if (through) {
			const Vertex f1 = vertex_at(mi, far_point(mi, 1.0));
			const Vertex f2 = vertex_at(mi, far_point(mi, -1.0));
			walk.push_back(member_edge(mi, from, f1, true));
			walk.push_back(connector_edge(f1.p, f2.p));
			walk.push_back(member_edge(mi, f2, to, true));
		} else {
			walk.push_back(member_edge(mi, from, to, true));
		}
		return walk;
	}

	std::vector<std::vector<Edge>> cell_edges(int li, int ri, const Proto &p, int layer) const {
		const Member &L = res_.members[size_t(li)];
		const Member &R = res_.members[size_t(ri)];
		std::vector<Edge> loop;
		const Vertex &as = p.as, &ae = p.ae, &bs = p.bs, &be = p.be;
		auto incl = [&](const Vertex &v) { return !v.pole && !v.inf && vertex_in(v.member, v.p, v.t); };
		auto connector = [&](const Pt &a, const Pt &b) {
			Edge e;
			e.connector = true;
			e.kind = EdgeKind::Bounds;
			e.a = a;
			e.b = b;
			e.via = (a + b) * 0.5;
			return e;
		};
		// A member edge from u to w, walking forward (or backward) along it.
		auto member_piece = [&](int mi, const Member &, const Vertex &u, const Vertex &w, bool forward) {
			const std::vector<Edge> walk = member_walk(mi, forward ? u : w, forward ? w : u, p.kb == End::Loop);
			if (forward) {
				loop.insert(loop.end(), walk.begin(), walk.end());
			} else {
				for (auto it = walk.rbegin(); it != walk.rend(); ++it) {
					Edge e = *it;
					std::swap(e.a, e.b);
					std::swap(e.ta, e.tb);
					loop.push_back(e);
				}
			}
		};
		// where a vertex is drawn: a vertex at infinity, far out along its member
		auto place = [&](const Vertex &v, double dflt) { return v.inf ? far_point(v.member, v.inf_sign != 0 ? v.inf_sign : dflt) : v.p; };
		// bridge two consecutive pieces (a pinch at infinity, or a carrier edge)
		auto bridge = [&](const Vertex &v, double dflt) {
			if (loop.size() >= 1) {
				return loop.back().b;
			}
			return place(v, dflt);
		};
		// a carrier piece from u to w; an end at infinity (a carrier parallel to
		// its member) leaves the rectangle along the carrier, and a connector joins
		// the member's far point
		auto carrier_piece = [&](int sid, End role, const Vertex &u, const Vertex &w, const Pt &via, bool incl_u) {
			if (!u.inf && !w.inf) {
				Edge e = carrier_edge(sid, role, u, w, via);
				e.incl_a = incl_u;
				loop.push_back(e);
				return;
			}
			if (u.inf && w.inf) {
				loop.push_back(connector(bridge(u, 1.0), place(w, 1.0)));
				return;
			}
			const Vertex &f = u.inf ? w : u;
			const Vertex &g = u.inf ? u : w;
			const Pt mf = place(g, 1.0);
			const Pt far = carrier_far(sid, f.p, mf);
			Vertex fv;
			fv.p = far;
			if (u.inf) {
				loop.push_back(connector(bridge(u, 1.0), far));
				Edge e = carrier_edge(sid, role, fv, w, (far + w.p) * 0.5);
				loop.push_back(e);
			} else {
				Edge e = carrier_edge(sid, role, u, fv, (u.p + far) * 0.5);
				e.incl_a = incl_u;
				loop.push_back(e);
				loop.push_back(connector(far, mf));
			}
		};
		member_piece(li, L, as, ae, true);
		// the far side
		if (p.kb == End::PoleQ && res_.qk_inf) {
			// both ends at infinity: from the left member's far point to the right's
			const Pt from = bridge(ae, 1.0);
			const Pt to = place(be, 1.0);
			loop.push_back(connector(from, to));
		} else if (p.kb != End::PoleP && p.kb != End::PoleQ) {
			carrier_piece(p.sid_b, role_of(p.kb), ae, be, p.via_b, incl(ae));
		}
		member_piece(ri, R, be, bs, false);
		// the near side
		if (p.ka == End::PoleQ && res_.qk_inf) {
			const Pt from = bridge(bs, -1.0);
			const Pt to = loop.empty() ? place(as, -1.0) : loop.front().a;
			loop.push_back(connector(from, to));
		} else if (p.ka == End::Near || p.ka == End::TieNear) {
			const size_t before = loop.size();
			carrier_piece(p.sid_a, role_of(p.ka), bs, as, p.via_a, incl(bs));
			for (size_t k = before; k < loop.size(); k++) {
				if (!loop[k].connector) {
					// a tie cut belongs to the other orientation (Stage 111)
					loop[k].inclusive = p.ka == End::Near && p.near_incl;
				}
			}
		}
		std::vector<Edge> kept;
		for (Edge &e : loop) {
			e.layer = layer;
			if (!(e.a == e.b)) kept.push_back(e);
		}
		std::vector<std::vector<Edge>> out = clip_loop(kept, p.inside);
		for (auto &l : out) {
			for (Edge &e : l) {
				e.layer = layer;
				if (e.kind == EdgeKind::Bounds) {
					e.inclusive = false;
					e.incl_a = false;
				}
			}
		}
		return out;
	}

	// ---- regions: the cells of each run of adjacent live gaps, merged

	void merge_cells() {
		const size_t n = res_.members.size();
		if (res_.cells.empty()) {
			return;
		}
		std::vector<std::vector<size_t>> by_gap(n);
		for (size_t c = 0; c < res_.cells.size(); c++) {
			by_gap[size_t(res_.cells[c].left)].push_back(c);
		}
		// runs: maximal cyclic sequences of gaps with cells
		size_t start = 0;
		while (start < n && !by_gap[start].empty()) {
			start++;
		}
		std::vector<std::vector<size_t>> runs; // gap indices
		if (start == n) {
			std::vector<size_t> all(n);
			for (size_t i = 0; i < n; i++) all[i] = i;
			runs.push_back(all);
		} else {
			std::vector<size_t> cur;
			for (size_t k = 1; k <= n; k++) {
				const size_t g = (start + k) % n;
				if (!by_gap[g].empty()) {
					cur.push_back(g);
				} else if (!cur.empty()) {
					runs.push_back(cur);
					cur.clear();
				}
			}
			if (!cur.empty()) runs.push_back(cur);
		}
		for (size_t ri = 0; ri < runs.size(); ri++) {
			std::vector<Edge> edges;
			std::map<int, std::vector<Edge>> on_member;
			for (size_t g : runs[ri]) {
				for (size_t c : by_gap[g]) {
					res_.cells[c].run = int(ri);
					for (const Edge &e : res_.cells[c].edges()) {
						if (e.kind == EdgeKind::Member) {
							on_member[e.member].push_back(e);
						} else {
							edges.push_back(e);
						}
					}
				}
			}
			for (auto &kv : on_member) {
				cancel(kv.first, kv.second, edges);
			}
			// a carrier piece two layers share (one's departure side, the other's
			// stop on the same points, run the other way) is inside the region
			std::vector<bool> gone(edges.size(), false);
			for (size_t i = 0; i < edges.size(); i++) {
				for (size_t j = i + 1; j < edges.size() && !gone[i]; j++) {
					if (!gone[j] && edges[i].kind == EdgeKind::Carrier && edges[j].kind == EdgeKind::Carrier && edges[i].surface == edges[j].surface
							&& edges[i].a == edges[j].b && edges[i].b == edges[j].a) {
						gone[i] = gone[j] = true;
					}
				}
			}
			std::vector<Edge> kept;
			for (size_t i = 0; i < edges.size(); i++) {
				if (!gone[i]) kept.push_back(edges[i]);
			}
			chain(kept);
		}
	}

	// The member pieces of a run on one member: overlapping pieces of opposite
	// directions cancel (two adjacent cells share them); what remains is boundary.
	void cancel(int mi, const std::vector<Edge> &pieces, std::vector<Edge> &out) {
		const Member &m = res_.members[size_t(mi)];
		// one key per point: two pieces ending at one point (a pole reached by
		// two computations) share it, whatever their parameters
		std::vector<std::pair<Pt, Key>> known;
		auto key_of = [&](const Pt &p, double t) {
			const double tol = 1e-9 * std::max({1.0, std::abs(p.x), std::abs(p.y)});
			for (const auto &kp : known) {
				if (norm(kp.first - p) <= tol) {
					return kp.second;
				}
			}
			known.push_back({p, arc_key(mi, t)});
			return known.back().second;
		};
		std::map<Key, std::pair<Pt, double>> bp; // key -> point, parameter
		for (const Edge &e : pieces) {
			bp.emplace(key_of(e.a, e.ta), std::make_pair(e.a, e.ta));
			bp.emplace(key_of(e.b, e.tb), std::make_pair(e.b, e.tb));
		}
		// the member's own visible set splits pieces where it begins or ends
		if (m.crit.alive) {
			for (const Interval &iv : m.crit.iv) {
				for (End k : {iv.a.kind, iv.b.kind}) {
					const Vertex v = vertex_on(mi, k, m.crit);
					// (a split point off the member -- a fallback root of a cast
					// whose rounded member missed a tangency -- splits nothing)
					if (!v.inf && !v.pole && on_member(m, v.p)) {
						const Key key = key_of(v.p, v.t);
						if (!bp.count(key)) {
							// inside some piece's range only: a split point
							bp[key] = {v.p, v.t};
						}
					}
				}
			}
		}
		std::vector<std::pair<Key, std::pair<Pt, double>>> pts(bp.begin(), bp.end());
		for (size_t i = 0; i + 1 < pts.size(); i++) {
			const Key lo = pts[i].first;
			const Key hi = pts[i + 1].first;
			int net = 0;
			for (const Edge &e : pieces) {
				const Key ka = key_of(e.a, e.ta);
				const Key kb = key_of(e.b, e.tb);
				const Key emin = std::min(ka, kb);
				const Key emax = std::max(ka, kb);
				if (emin <= lo && hi <= emax) {
					net += ka < kb ? 1 : -1;
				}
			}
			if (net == 0) {
				continue;
			}
			if (std::abs(net) > 1) {
				res_.notes.push_back("member " + std::to_string(mi) + ": a piece covered " + std::to_string(std::abs(net)) + " times");
			}
			Vertex va;
			va.member = mi;
			va.p = pts[i].second.first;
			va.t = pts[i].second.second;
			Vertex vb = va;
			vb.p = pts[i + 1].second.first;
			vb.t = pts[i + 1].second.second;
			const double tmid = between(va.t, vb.t);
			Edge e = net > 0 ? member_edge(mi, va, vb, true) : member_edge(mi, vb, va, false);
			e.inclusive = in_extent(mi, tmid);
			const Vertex &start = net > 0 ? va : vb;
			e.incl_a = !(start.p == res_.pk) && !(!res_.qk_inf && start.p == res_.qk) && !on_clip(start.p) && vertex_in(mi, start.p, start.t);
			out.push_back(e);
		}
	}

	static bool on_member(const Member &m, const Pt &q) {
		const Pt P = pt_of(m.beam.p);
		const Pt t = pt_of(m.beam.tau);
		const double k = m.beam.kappa.v;
		const double d = k == 0.0 ? std::abs(cross(t, q - P)) : std::abs(norm(q - (P + perp(t) * (1.0 / k))) - 1.0 / std::abs(k));
		return d <= 1e-6 * std::max({1.0, std::abs(q.x), std::abs(q.y)});
	}

	bool on_clip(const Pt &p) const {
		const Rect &c = res_.clip;
		return p.x == c.x0 || p.x == c.x1 || p.y == c.y0 || p.y == c.y1;
	}

	// The edge's direction of travel at its start (at_start) or end.
	static Pt end_tangent(const Edge &e, bool at_start) {
		const ArcGeom g = arc_geom(e);
		if (g.line) {
			return unit(e.b - e.a);
		}
		const Pt p = at_start ? e.a : e.b;
		Pt t = perp(p - g.c); // counterclockwise travel
		if (!g.ccw) {
			t = t * -1.0;
		}
		return unit(t);
	}

	// A closed loop without its slivers: an edge shorter than 1e-9 of the
	// coordinates' scale (two members a hair apart) is dropped and its
	// neighbours meet at its start.
	static std::vector<Edge> drop_slivers(std::vector<Edge> es) {
		auto tiny = [](const Edge &e) {
			return norm(e.b - e.a) <= 1e-9 * std::max({1.0, std::abs(e.a.x), std::abs(e.a.y)}) && norm(e.via - e.a) <= 1e-6;
		};
		std::vector<Edge> out;
		for (size_t i = 0; i < es.size(); i++) {
			if (tiny(es[i]) && es.size() > 2) {
				// the next edge starts where this one started
				const size_t n = (i + 1) % es.size();
				es[n].a = es[i].a;
				continue;
			}
			out.push_back(es[i]);
		}
		if (!out.empty() && !(out.back().b == out.front().a)) {
			out.back().b = out.front().a; // a sliver at the loop's end
		}
		return out;
	}

	void chain(std::vector<Edge> &edges) {
		std::vector<bool> used(edges.size(), false);
		std::multimap<Pt, size_t> from;
		for (size_t i = 0; i < edges.size(); i++) {
			from.emplace(edges[i].a, i);
		}
		for (size_t s = 0; s < edges.size(); s++) {
			if (used[s]) {
				continue;
			}
			Loop loop;
			size_t cur = s;
			used[cur] = true;
			loop.edges.push_back(edges[cur]);
			bool closed = false;
			for (size_t guard = 0; guard < edges.size() + 1; guard++) {
				const Pt end = edges[cur].b;
				if (end == edges[s].a) {
					closed = true;
					break;
				}
				// at a pinch (several ways on), the way that keeps this lobe: the
				// first one clockwise from the way back (the region on the left;
				// counterclockwise when the cells wind the other way)
				size_t next = SIZE_MAX;
				double best = INF;
				const Pt back = end_tangent(edges[cur], false) * -1.0;
				auto range = from.equal_range(end);
				for (auto it = range.first; it != range.second; ++it) {
					if (used[it->second]) {
						continue;
					}
					const Pt out = end_tangent(edges[it->second], true);
					double ang = std::atan2(cross(out, back), dot(out, back)); // from out to back, counterclockwise
					if (res_.orientation < 0) {
						ang = -ang;
					}
					while (ang <= 0.0) ang += 2.0 * PI;
					if (ang < best) {
						best = ang;
						next = it->second;
					}
				}
				if (next == SIZE_MAX) {
					break;
				}
				used[next] = true;
				loop.edges.push_back(edges[next]);
				cur = next;
			}
			if (!closed) {
				res_.notes.push_back("an open boundary chain of " + std::to_string(loop.edges.size()) + " edges");
				res_.loops.push_back(std::move(loop));
				continue;
			}
			// a loop through one vertex twice (a region pinched there, e.g. a hole
			// touching the outer boundary) is cut there into two simple loops
			std::vector<std::vector<Edge>> pending{std::move(loop.edges)};
			while (!pending.empty()) {
				std::vector<Edge> es = std::move(pending.back());
				pending.pop_back();
				size_t cut_i = 0;
				size_t cut_j = 0;
				for (size_t i = 0; i < es.size() && cut_j == 0; i++) {
					for (size_t j = i + 1; j < es.size(); j++) {
						if (es[j].a == es[i].a) {
							cut_i = i;
							cut_j = j;
							break;
						}
					}
				}
				if (cut_j == 0) {
					Loop l;
					l.edges = drop_slivers(std::move(es));
					if (!l.edges.empty()) {
						res_.loops.push_back(std::move(l));
					}
					continue;
				}
				std::vector<Edge> inner(es.begin() + long(cut_i), es.begin() + long(cut_j));
				std::vector<Edge> outer(es.begin(), es.begin() + long(cut_i));
				outer.insert(outer.end(), es.begin() + long(cut_j), es.end());
				pending.push_back(std::move(inner));
				pending.push_back(std::move(outer));
			}
		}
	}
};


inline double seg_dist(const Pt &p, const Pt &a, const Pt &b) {
	const Pt ab = b - a;
	const double L2 = dot(ab, ab);
	double t = L2 > 0.0 ? dot(p - a, ab) / L2 : 0.0;
	t = std::clamp(t, 0.0, 1.0);
	return norm(p - (a + ab * t));
}

// scale: what a relative tolerance multiplies -- the query point's coordinates
// (and a huge circle's radius, whose distance carries that much rounding)
inline double edge_dist(const Edge &e, const Pt &p, double *scale = nullptr) {
	const ArcGeom g = arc_geom(e);
	const double ps = std::max({1.0, std::abs(p.x), std::abs(p.y)});
	if (g.line) {
		if (scale) *scale = ps;
		return seg_dist(p, e.a, e.b);
	}
	if (scale) *scale = std::max(ps, 1e-7 * g.r);
	if (on_arc_side(e, p)) {
		return std::abs(norm(p - g.c) - g.r);
	}
	return std::min(norm(p - e.a), norm(p - e.b));
}

// Crossings of e with the ray from q toward +x, the even-odd rule's count for
// one edge. Half-open at the ray's height (a point exactly at q.y counts as
// below it, the same for every edge sharing it): an arc is cut at its top and
// bottom into pieces monotone in y, each counted like a segment.
inline int ray_crossings(const Edge &e, const Pt &q) {
	const ArcGeom g = arc_geom(e);
	if (g.line) {
		if ((e.a.y > q.y) == (e.b.y > q.y)) {
			return 0;
		}
		const double x = e.a.x + (q.y - e.a.y) * (e.b.x - e.a.x) / (e.b.y - e.a.y);
		return x > q.x ? 1 : 0;
	}
	// the arc's angles from a, in its turning direction
	const double a0 = std::atan2(e.a.y - g.c.y, e.a.x - g.c.x);
	double sw = std::atan2(e.b.y - g.c.y, e.b.x - g.c.x) - a0;
	if (g.ccw) {
		while (sw <= 0.0) sw += 2.0 * PI;
	} else {
		while (sw >= 0.0) sw -= 2.0 * PI;
	}
	auto at = [&](double ang) { return Pt{g.c.x + g.r * std::cos(ang), g.c.y + g.r * std::sin(ang)}; };
	// the cuts: the circle's top and bottom (angles +-pi/2) strictly inside the arc
	std::vector<std::pair<double, Pt>> cuts; // offset along the sweep, point
	for (const double ext : {PI / 2.0, -PI / 2.0}) {
		double off = ext - a0;
		if (g.ccw) {
			while (off <= 0.0) off += 2.0 * PI;
			while (off > 2.0 * PI) off -= 2.0 * PI;
		} else {
			while (off >= 0.0) off -= 2.0 * PI;
			while (off < -2.0 * PI) off += 2.0 * PI;
		}
		if (std::abs(off) < std::abs(sw)) {
			cuts.emplace_back(off, Pt{g.c.x, g.c.y + (ext > 0 ? g.r : -g.r)});
		}
	}
	std::sort(cuts.begin(), cuts.end(), [](const auto &x, const auto &y) { return std::abs(x.first) < std::abs(y.first); });
	std::vector<std::pair<double, Pt>> pts{{0.0, e.a}};
	pts.insert(pts.end(), cuts.begin(), cuts.end());
	pts.emplace_back(sw, e.b);
	int n = 0;
	for (size_t i = 0; i + 1 < pts.size(); i++) {
		const Pt &p0 = pts[i].second;
		const Pt &p1 = pts[i + 1].second;
		if ((p0.y > q.y) == (p1.y > q.y)) {
			continue;
		}
		// the monotone piece lies in one half of the circle: its middle tells which
		const Pt mid = at(a0 + (pts[i].first + pts[i + 1].first) / 2.0);
		const double dy = q.y - g.c.y;
		const double h = std::sqrt(std::max(0.0, g.r * g.r - dy * dy));
		const double x = mid.x >= g.c.x ? g.c.x + h : g.c.x - h;
		if (x > q.x) {
			n++;
		}
	}
	return n;
}

inline Membership contains(const Result &r, const Pt &c) {
	if (!r.supported || !c.finite() || !r.domain.has(c)) {
		return Membership::Unclassified;
	}
	if (c == r.player || c == r.pk || (!r.qk_inf && c == r.qk)) {
		return Membership::Unclassified; // the aim's degenerate points (GAME_SPEC 11.4)
	}
	for (const Edge &e : r.whiskers) {
		double scale = 1.0;
		if (edge_dist(e, c, &scale) <= 1e-9 * scale) {
			// its ends as decided (K7; an excluded end is decided by the rest of
			// the region), its interior in
			const double tol = 1e-9 * std::max({1.0, std::abs(c.x), std::abs(c.y)});
			if ((norm(c - e.a) <= tol && !e.incl_a) || (norm(c - e.b) <= tol && !e.incl_b)) {
				continue;
			}
			return Membership::Inside;
		}
	}
	for (const Loop &l : r.loops) {
		for (const Edge &e : l.edges) {
			const double tol = 1e-9 * std::max({1.0, std::abs(c.x), std::abs(c.y)});
			if (norm(c - e.a) <= tol) {
				return e.incl_a ? Membership::Inside : Membership::Outside;
			}
		}
	}
	for (const Loop &l : r.loops) {
		for (const Edge &e : l.edges) {
			double scale = 1.0;
			const double d = edge_dist(e, c, &scale);
			if (d <= 1e-9 * scale) {
				return e.inclusive ? Membership::Inside : Membership::Outside;
			}
		}
	}
	int n = 0;
	for (const Loop &l : r.loops) {
		for (const Edge &e : l.edges) {
			n += ray_crossings(e, c);
		}
	}
	return (n % 2) == 1 ? Membership::Inside : Membership::Outside;
}

// The distance from c to the nearest boundary edge (for messages).
inline double margin(const Result &r, const Pt &c) {
	double m = INF;
	for (const Loop &l : r.loops) {
		for (const Edge &e : l.edges) {
			m = std::min(m, edge_dist(e, c));
		}
	}
	return m;
}

// The edge as a polyline from a to b, b excluded; arcs within `tol` px of the
// curve (huge circles need many points), at least 64 per full turn.
inline void tessellate(const Edge &e, std::vector<Pt> &out, double tol = 0.25) {
	const ArcGeom g = arc_geom(e);
	out.push_back(e.a);
	if (g.line) {
		return;
	}
	const double a0 = std::atan2(e.a.y - g.c.y, e.a.x - g.c.x);
	const double sw = arc_sweep(e, g);
	const double step = std::min(2.0 * PI / 64.0, 2.0 * std::acos(std::max(-1.0, 1.0 - tol / g.r)));
	const int n = std::max(2, int(std::min(1e6, std::ceil(std::abs(sw) / step))));
	for (int k = 1; k < n; k++) {
		const double a = a0 + sw * k / n;
		out.push_back(Pt{g.c.x + g.r * std::cos(a), g.c.y + g.r * std::sin(a)});
	}
}

inline std::vector<Pt> polyline(const std::vector<Edge> &edges) {
	std::vector<Pt> out;
	for (const Edge &e : edges) {
		tessellate(e, out);
	}
	return out;
}

// The exact signed area of a closed loop of edges (chords plus circular segments).
inline double loop_area(const std::vector<Edge> &edges) {
	double a = 0.0;
	for (const Edge &e : edges) {
		a += cross(e.a, e.b) / 2.0;
		const ArcGeom g = arc_geom(e);
		if (!g.line) {
			const double phi = std::abs(arc_sweep(e, g));
			const double seg = g.r * g.r / 2.0 * (phi - std::sin(phi));
			a += g.ccw ? seg : -seg;
		}
	}
	return a;
}

inline double signed_area(const std::vector<Pt> &p) {
	double a = 0.0;
	for (size_t i = 0; i < p.size(); i++) {
		const Pt &u = p[i];
		const Pt &v = p[(i + 1) % p.size()];
		a += cross(u, v);
	}
	return a / 2.0;
}

// A polygon clipped to a rectangle (Sutherland-Hodgman; for drawing).
inline std::vector<Pt> clip_polygon(const std::vector<Pt> &poly, const Rect &r) {
	std::vector<Pt> out = poly;
	auto pass = [&](auto inside, auto meet) {
		std::vector<Pt> in = std::move(out);
		out.clear();
		for (size_t i = 0; i < in.size(); i++) {
			const Pt &cur = in[i];
			const Pt &prev = in[(i + in.size() - 1) % in.size()];
			const bool ci = inside(cur);
			const bool pi = inside(prev);
			if (ci) {
				if (!pi) out.push_back(meet(prev, cur));
				out.push_back(cur);
			} else if (pi) {
				out.push_back(meet(prev, cur));
			}
		}
	};
	auto at_x = [](double x) { return [x](const Pt &a, const Pt &b) { const double t = (x - a.x) / (b.x - a.x); return Pt{x, a.y + t * (b.y - a.y)}; }; };
	auto at_y = [](double y) { return [y](const Pt &a, const Pt &b) { const double t = (y - a.y) / (b.y - a.y); return Pt{a.x + t * (b.x - a.x), y}; }; };
	pass([&](const Pt &p) { return p.x >= r.x0; }, at_x(r.x0));
	pass([&](const Pt &p) { return p.x <= r.x1; }, at_x(r.x1));
	pass([&](const Pt &p) { return p.y >= r.y0; }, at_y(r.y0));
	pass([&](const Pt &p) { return p.y <= r.y1; }, at_y(r.y1));
	return out;
}

// Whether p is inside a closed polyline (even-odd).
inline bool in_ring(const std::vector<Pt> &ring, const Pt &p) {
	bool in = false;
	for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
		const Pt &a = ring[i];
		const Pt &b = ring[j];
		if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
			in = !in;
		}
	}
	return in;
}

// A tessellated ring made simple: where two arcs nearly touch (a member nearly
// tangent to a carrier), chords within the tessellation's tolerance can cross,
// though the exact loop is simple (VIS-SIMPLE checks the arcs). The ring is cut
// at each crossing of two of its segments into two rings; a ring wound against
// the original is a chord artifact (a lobe of sub-pixel width) and is dropped.
inline void simple_rings(std::vector<Pt> ring, double sign, std::vector<std::vector<Pt>> &out, int depth = 0) {
	const size_t n = ring.size();
	if (n < 3) {
		return;
	}
	auto proper = [](const Pt &p1, const Pt &p2, const Pt &q1, const Pt &q2, Pt *x) {
		const double d1 = cross(p2 - p1, q1 - p1);
		const double d2 = cross(p2 - p1, q2 - p1);
		const double d3 = cross(q2 - q1, p1 - q1);
		const double d4 = cross(q2 - q1, p2 - q1);
		if (!((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) || !((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
			return false;
		}
		const double t = d3 / (d3 - d4);
		*x = p1 + (p2 - p1) * t;
		return true;
	};
	if (depth < 64) {
		for (size_t a = 0; a < n; a++) {
			const Pt &p1 = ring[a];
			const Pt &p2 = ring[(a + 1) % n];
			const double ax0 = std::min(p1.x, p2.x), ax1 = std::max(p1.x, p2.x), ay0 = std::min(p1.y, p2.y), ay1 = std::max(p1.y, p2.y);
			for (size_t b = a + 2; b < n; b++) {
				if ((b + 1) % n == a) {
					continue; // adjacent through the closure
				}
				const Pt &q1 = ring[b];
				const Pt &q2 = ring[(b + 1) % n];
				if (std::max(q1.x, q2.x) < ax0 || std::min(q1.x, q2.x) > ax1 || std::max(q1.y, q2.y) < ay0 || std::min(q1.y, q2.y) > ay1) {
					continue;
				}
				Pt x;
				if (!proper(p1, p2, q1, q2, &x)) {
					continue;
				}
				// ring A: 0..a, x, b+1..n-1; ring B: x, a+1..b
				std::vector<Pt> ra(ring.begin(), ring.begin() + long(a) + 1);
				ra.push_back(x);
				ra.insert(ra.end(), ring.begin() + long(b) + 1, ring.end());
				std::vector<Pt> rb{x};
				rb.insert(rb.end(), ring.begin() + long(a) + 1, ring.begin() + long(b) + 1);
				simple_rings(std::move(ra), sign, out, depth + 1);
				simple_rings(std::move(rb), sign, out, depth + 1);
				return;
			}
		}
	}
	const double a = signed_area(ring);
	if (a * sign > 0.0) {
		out.push_back(std::move(ring));
	}
}

// The region drawn as one generalized polygon (Stage 110, F8): every cell
// triangulated into one triangle array, with a conforming tessellation. Drawn
// cell by cell with independent tessellations, two cells sharing an arc chose
// different chord points (T-junctions): hairline cracks and double-tinted
// slivers. Here every edge on one circle or line (the same member or carrier:
// exactly the same canonical geometry) is cut at the same points -- a common
// angular grid on the circle plus every cell corner lying on it -- so cells that
// share a piece share its vertices and the mesh has no seams. (The merged loops
// themselves are not triangulated: their holes pinch at P_K and Q_K, where
// every member meets, and ear clipping is not reliable with touching holes.)
struct Fill {
	std::vector<Pt> points;
	std::vector<uint32_t> indices; // triangles, three per
};

namespace fill_detail {

// The geometry an edge lies on, as a key: a canonical circle or line, or none.
struct GeomKey {
	int kind = 0; // 0 none, 1 circle, 2 line
	double v[4] = {0, 0, 0, 0};
	bool operator<(const GeomKey &o) const {
		if (kind != o.kind) return kind < o.kind;
		for (int i = 0; i < 4; i++) {
			if (v[i] != o.v[i]) return v[i] < o.v[i];
		}
		return false;
	}
};

inline GeomKey key_of(const Edge &e) {
	GeomKey k;
	if (e.kind == EdgeKind::Bounds) {
		return k;
	}
	if (e.arc && !e.gline && e.gr > 0.0) {
		k.kind = 1;
		k.v[0] = e.gc.x;
		k.v[1] = e.gc.y;
		k.v[2] = e.gr;
		return k;
	}
	if (!e.arc && e.gline && (e.gd.x != 0.0 || e.gd.y != 0.0)) {
		Pt d = e.gd;
		if (d.x < 0.0 || (d.x == 0.0 && d.y < 0.0)) {
			d = d * -1.0;
		}
		k.kind = 2;
		k.v[0] = e.gp.x;
		k.v[1] = e.gp.y;
		k.v[2] = d.x;
		k.v[3] = d.y;
	}
	return k;
}

// The points of edge e, a first, b excluded: its conforming tessellation.
inline void conforming(const Edge &e, const std::map<GeomKey, std::vector<Pt>> &corners, std::vector<Pt> &out, double tol = 0.25) {
	const GeomKey k = key_of(e);
	if (k.kind == 0) {
		tessellate(e, out, tol); // no shared geometry: no neighbour to conform to
		return;
	}
	out.push_back(e.a);
	std::vector<std::pair<double, Pt>> inner; // (position along the edge, point)
	auto add_corners = [&](auto pos, double len) {
		const auto it = corners.find(k);
		if (it == corners.end()) {
			return;
		}
		for (const Pt &q : it->second) {
			const double s = pos(q);
			if (s > 1e-12 * std::max(1.0, len) && s < len * (1.0 - 1e-12)) {
				inner.push_back({s, q});
			}
		}
	};
	if (k.kind == 1) {
		const ArcGeom g = arc_geom(e);
		if (g.line) {
			return;
		}
		const double a0 = std::atan2(e.a.y - g.c.y, e.a.x - g.c.x);
		const double sw = arc_sweep(e, g);
		const double sg = sw > 0.0 ? 1.0 : -1.0;
		const double len = std::abs(sw);
		auto pos = [&](const Pt &q) {
			double d = (std::atan2(q.y - g.c.y, q.x - g.c.x) - a0) * sg;
			while (d < 0.0) d += 2.0 * PI;
			while (d >= 2.0 * PI) d -= 2.0 * PI;
			return d;
		};
		add_corners(pos, len);
		// the circle's common grid: angles k * 2 pi / N (a whole number of steps per
		// turn, so an edge across the angle +-pi meets the same points as one that is not)
		const double step0 = std::min(2.0 * PI / 64.0, 2.0 * std::acos(std::max(-1.0, 1.0 - tol / g.r)));
		const double turns = std::ceil(2.0 * PI / step0);
		const long nturn = long(std::min(turns, 4e6));
		const double step = 2.0 * PI / double(nturn);
		const double lo = sg > 0.0 ? a0 : a0 - len;
		const double hi = lo + len;
		const long k0 = long(std::ceil(lo / step));
		const long k1 = long(std::floor(hi / step));
		if (k1 - k0 < 2000000) {
			for (long j = k0; j <= k1; j++) {
				const double ang = double(((j % nturn) + nturn) % nturn) * step; // (bit-identical across turns)
				const Pt q{g.c.x + g.r * std::cos(ang), g.c.y + g.r * std::sin(ang)};
				const double s = pos(q);
				if (s > 1e-12 && s < len * (1.0 - 1e-12)) {
					inner.push_back({s, q});
				}
			}
		}
	} else if (k.kind == 2) {
		const Pt d = e.b - e.a;
		const double len = norm(d);
		if (!(len > 0.0)) {
			return;
		}
		const Pt u = d * (1.0 / len);
		add_corners([&](const Pt &q) { return std::abs(cross(u, q - e.a)) <= 1e-9 * std::max({1.0, std::abs(q.x), std::abs(q.y)}) ? dot(q - e.a, u) : -1.0; }, len);
	}
	std::sort(inner.begin(), inner.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
	for (const auto &[s, q] : inner) {
		if (!(out.back() == q)) {
			out.push_back(q);
		}
	}
}

} // namespace fill_detail

inline Fill fill_triangles(const Result &r) {
	using namespace fill_detail;
	Fill out;
	// every cell corner, by the geometry its edges lie on
	std::map<GeomKey, std::vector<Pt>> corners;
	for (const Cell &c : r.cells) {
		for (const auto &l : c.loops) {
			for (const Edge &e : l) {
				const GeomKey k = key_of(e);
				if (k.kind != 0) {
					corners[k].push_back(e.a);
					corners[k].push_back(e.b);
				}
			}
		}
	}
	earcut::Triangulator tri;
	for (const Cell &c : r.cells) {
		for (const auto &l : c.loops) {
			std::vector<Pt> poly;
			for (const Edge &e : l) {
				conforming(e, corners, poly);
			}
			std::vector<Pt> ring;
			for (const Pt &q : clip_polygon(poly, r.domain)) {
				if (ring.empty() || !(ring.back() == q)) {
					ring.push_back(q);
				}
			}
			while (ring.size() > 1 && ring.back() == ring.front()) {
				ring.pop_back();
			}
			const double a0 = ring.size() >= 3 ? signed_area(ring) : 0.0;
			if (std::abs(a0) <= 1e-9) {
				continue;
			}
			std::vector<std::vector<Pt>> simple;
			simple_rings(std::move(ring), a0 > 0.0 ? 1.0 : -1.0, simple);
			for (const std::vector<Pt> &sr : simple) {
				std::vector<earcut::XY> xy;
				xy.reserve(sr.size());
				for (const Pt &q : sr) {
					xy.push_back(earcut::XY{q.x, q.y});
				}
				const uint32_t base = uint32_t(out.points.size());
				out.points.insert(out.points.end(), sr.begin(), sr.end());
				for (uint32_t idx : tri.run({xy})) {
					out.indices.push_back(base + idx);
				}
			}
		}
	}
	return out;
}

// Stage 111: the two halves of a split plan as one result: the direct half's
// scalars, every member, cell, loop and whisker of both (the reversed half's
// member indices rebased; cells and loops tagged by their half for the checks
// that read one realization at a time: the winding, the disjointness -- the
// two share the crack between them). A half's whisker inside the other half's
// area is that area's (one member's cast landed on the other side of the
// switch): dropped.
inline Membership contains(const Result &r, const Pt &c);
inline Result join_halves(Result a, Result b) {
	auto rebase = [](Edge &e, int off) {
		if (e.member >= 0) {
			e.member += off;
		}
	};
	for (Result *h : {&a, &b}) {
		for (Loop &l : h->loops) {
			l.area = loop_area(l.edges);
		}
	}
	auto keep_whiskers = [](Result &mine, const Result &other) {
		std::vector<Edge> kept;
		for (const Edge &w : mine.whiskers) {
			if (w.kind == EdgeKind::Member) {
				// sampled along it (a straight whisker may run to the real infinity)
				std::vector<Pt> samples{w.via};
				if (!w.arc) {
					for (double f : {0.02, 0.2, 0.8, 0.98}) {
						samples.push_back(w.a + (w.b - w.a) * f);
					}
				}
				// kept only where it sticks out of the other half within the domain
				bool outside = false;
				for (const Pt &q : samples) {
					outside = outside || contains(other, q) == Membership::Outside;
				}
				if (!outside) {
					continue;
				}
			}
			kept.push_back(w);
		}
		mine.whiskers = std::move(kept);
	};
	keep_whiskers(a, b);
	keep_whiskers(b, a);
	Result r = a;
	r.half = Half::Single;
	r.split = true;
	if (!b.supported) {
		r.supported = false;
		r.reason = b.reason;
	}
	const int off = int(a.members.size());
	for (Edge &e : b.whiskers) {
		rebase(e, off);
	}
	for (Loop &l : b.loops) {
		for (Edge &e : l.edges) {
			rebase(e, off);
		}
	}
	for (Cell &c : b.cells) {
		if (c.left >= 0) {
			c.left += off;
		}
		if (c.right >= 0) {
			c.right += off;
		}
		for (auto &l : c.loops) {
			for (Edge &e : l) {
				rebase(e, off);
			}
		}
	}
	r.members.insert(r.members.end(), b.members.begin(), b.members.end());
	r.cells.insert(r.cells.end(), b.cells.begin(), b.cells.end());
	r.loops.insert(r.loops.end(), b.loops.begin(), b.loops.end());
	r.whiskers.insert(r.whiskers.end(), b.whiskers.begin(), b.whiskers.end());
	r.casts += b.casts;
	// every member reference of the joined region names one of its members (the
	// second half's rebased exactly once)
	{
		const int n = int(r.members.size());
		auto in_range = [n](int m) { return m >= -1 && m < n; };
		for (const Loop &l : r.loops) {
			for (const Edge &e : l.edges) {
				RC_ASSERT(in_range(e.member), "vis: a loop edge's member after the join");
			}
		}
		for (const Edge &e : r.whiskers) {
			RC_ASSERT(in_range(e.member), "vis: a whisker's member after the join");
		}
		for (const Cell &c : r.cells) {
			RC_ASSERT(in_range(c.left) && in_range(c.right), "vis: a cell's members after the join");
		}
	}
	r.refinements += b.refinements;
	for (size_t k = 0; k < r.casts_by_kind.size(); k++) {
		r.casts_by_kind[k] += b.casts_by_kind[k];
	}
	for (const std::string &n : b.notes) {
		r.notes.push_back("the other way: " + n);
	}
	r.clip.add(Pt{b.clip.x0, b.clip.y0});
	r.clip.add(Pt{b.clip.x1, b.clip.y1});
	return r;
}

// Stage 111: one half of a joined result, for the checks that read one
// realization: its cells and loops, winding its own way.
inline Result half_of(const Result &r, int half) {
	Result h = r;
	h.cells.clear();
	h.loops.clear();
	for (const Cell &c : r.cells) {
		if (c.half == half) {
			h.cells.push_back(c);
		}
	}
	for (const Loop &l : r.loops) {
		if (l.half == half) {
			h.loops.push_back(l);
		}
	}
	h.orientation = half == int(Half::Reversed) ? -r.orientation : r.orientation;
	return h;
}

// The visibility region of the plan for the player (bounds: the level's).
inline Result compute(Tracer &tracer, const Pt &player, const std::vector<PlanItem> &plan, const Flags &flags, const Rect &bounds) {
	const auto t0 = std::chrono::steady_clock::now();
	Builder b(tracer, player, plan, flags, bounds);
	Result r;
	if (!b.splits()) {
		r = b.build();
	} else {
		// Stage 111: the plan's first entry fixes the realization per cursor
		SolveMemo memo;
		Builder bd(tracer, player, plan, flags, bounds, Half::Direct);
		Builder br(tracer, player, plan, flags, bounds, Half::Reversed);
		bd.share(&memo);
		br.share(&memo);
		Result ra = bd.build();
		Result rb = br.build();
		if (std::getenv("RC_VIS_MEMO_STATS") != nullptr) {
			std::fprintf(stderr, "memo: solves %ld (%.1f ms), ties %ld (%.1f ms), realizations %ld (%.1f ms), hits %ld\n", memo.solve_n, memo.solve_ms, memo.tie_n,
				memo.tie_ms, memo.realization_n, memo.realization_ms, memo.hits);
		}
		r = join_halves(std::move(ra), std::move(rb));
	}
	for (Loop &l : r.loops) {
		l.area = loop_area(l.edges);
	}
	r.build_usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
	return r;
}

// ---- edge intersections (for the structure checks; testing-layer tolerances)

// Points where the carriers of e and f meet, lying on both edges (tol: distance).
inline std::vector<Pt> edge_crossings(const Edge &e, const Edge &f, double tol) {
	const ArcGeom ge = arc_geom(e);
	const ArcGeom gf = arc_geom(f);
	std::vector<Pt> cand;
	auto line_circle = [&](const Edge &l, const ArcGeom &c) {
		const Pt d = l.b - l.a;
		const Pt w = l.a - c.c;
		const double A = dot(d, d);
		const double B = 2.0 * dot(w, d);
		const double C = dot(w, w) - c.r * c.r;
		const double disc = B * B - 4 * A * C;
		if (A == 0.0 || disc < 0.0) return;
		const double sq = std::sqrt(disc);
		for (double u : {(-B - sq) / (2 * A), (-B + sq) / (2 * A)}) cand.push_back(l.a + d * u);
	};
	if (ge.line && gf.line) {
		const Pt r = e.b - e.a;
		const Pt q = f.b - f.a;
		const double den = cross(r, q);
		if (den != 0.0) cand.push_back(e.a + r * (cross(f.a - e.a, q) / den));
	} else if (ge.line) {
		line_circle(e, gf);
	} else if (gf.line) {
		line_circle(f, ge);
	} else {
		const double d = norm(gf.c - ge.c);
		if (d > 0.0 && d <= ge.r + gf.r && d >= std::abs(ge.r - gf.r)) {
			const double x = (d * d + ge.r * ge.r - gf.r * gf.r) / (2 * d);
			const double h = std::sqrt(std::max(0.0, ge.r * ge.r - x * x));
			const Pt u = (gf.c - ge.c) * (1.0 / d);
			cand.push_back(ge.c + u * x + perp(u) * h);
			cand.push_back(ge.c + u * x - perp(u) * h);
		}
	}
	std::vector<Pt> out;
	for (const Pt &p : cand) {
		if (p.finite() && edge_dist(e, p) <= tol && edge_dist(f, p) <= tol) out.push_back(p);
	}
	return out;
}

// e and f share a piece of positive length (one carrier, overlapping ranges).
inline bool edges_overlap(const Edge &e, const Edge &f, double tol) {
	const ArcGeom ge = arc_geom(e);
	const ArcGeom gf = arc_geom(f);
	if (ge.line != gf.line) return false;
	if (ge.line) {
		const Pt d = unit(e.b - e.a);
		if (std::abs(cross(d, f.a - e.a)) > tol || std::abs(cross(d, f.b - e.a)) > tol) return false;
		const double L = norm(e.b - e.a);
		const double fa = dot(f.a - e.a, d);
		const double fb = dot(f.b - e.a, d);
		return std::min(L, std::max(fa, fb)) - std::max(0.0, std::min(fa, fb)) > tol;
	}
	if (norm(ge.c - gf.c) > tol || std::abs(ge.r - gf.r) > tol) return false;
	// one arc's via, or an end strictly inside the other
	auto strictly_on = [&](const Edge &x, const Pt &p) { return on_arc_side(x, p) && norm(p - x.a) > tol && norm(p - x.b) > tol; };
	return strictly_on(e, f.via) || strictly_on(f, e.via) || strictly_on(e, f.a) || strictly_on(e, f.b) || strictly_on(f, e.a) || strictly_on(f, e.b);
}

// Whether p is strictly inside a closed loop of edges (even-odd).
inline bool inside_loop(const std::vector<Edge> &edges, const Pt &p) {
	int n = 0;
	for (const Edge &e : edges) n += ray_crossings(e, p);
	return (n % 2) == 1;
}
} // namespace vis
} // namespace rc
