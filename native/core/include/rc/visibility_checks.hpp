// The visibility region's invariants (Stage 110, GAME_SPEC 15 and 29.3).
//
// Per cursor -- VISIBILITY-EQUALS-NON-DIVERGENCE: the region contains the
// cursor iff the preview's merged steps before it are ALIGNED and it is
// reached. Per region -- the polygons are valid and correctly built:
//   validity:     VIS-CLOSED, VIS-SIMPLE, VIS-ORIENTED, VIS-DISJOINT, VIS-FINITE
//   provenance:   VIS-EDGE-ON-GEOMETRY, VIS-VERTEX-PROVENANCE, VIS-SHADOW-ANCHORED,
//                 VIS-NO-BLOCKER-INSIDE, VIS-SOURCE-ON-BOUNDARY
//   consistency:  VIS-STAR, VIS-CELL-CONSTANT, VIS-ITERATION-CHAIN,
//                 VIS-BOUNDARY-DECIDED, VIS-CONTAINS-MATCHES-POLYGON, VIS-DETERMINISTIC
// Tolerances here are the testing layer's (positions), never a solver decision.
#pragma once

#include "rc/visibility.hpp"

#include <map>
#include <memory>
#include <set>

namespace rc {
namespace inv {

using detail::F;
using detail::I;
using detail::S;
using detail::v;

// What the preview shows: the planned display path reaches the cursor and the
// merged steps before it are ALIGNED (GAME_SPEC 15.1).
inline bool preview_visible(const TracePath &planned, const std::vector<TraceStep> &typed, bool cursor_point_agrees = true) {
	if (planned.cursor_index < 0 || !cursor_point_agrees) {
		return false; // (V1; V3: both walks leave the cursor's own point in one frame, B4)
	}
	const int ci = planned.cursor_index;
	if (int(typed.size()) < ci) {
		return false;
	}
	for (int i = 0; i < ci; i++) {
		if (typed[size_t(i)].type != StepType::Aligned) {
			return false;
		}
	}
	return true;
}

// Where a disagreement is a known finding: Stage 113's K13 -- a cursor exactly
// on a member tangent to a carrier beyond the tangency (the member's own walk
// fires at the touch, the cursor's own beam a rounding off it passes), on a
// projective plan's region (docs/TDD_18_PROJECTIVE_VISIBILITY.md). (K8, a
// cursor on a critical member, was fixed by exact member cursors; K7, a cursor
// exactly on a planned carrier, by the cursor as a known point of exact beams
// and by exact endpoints deciding whisker ends.)
inline std::string pin_tag(const vis::Result &r, const vis::Pt &c) {
	if (r.pencil.empty() || r.pencil == "aim") {
		return "";
	}
	const double tol = 1e-9 * std::max({1.0, std::abs(c.x), std::abs(c.y)});
	for (const vis::Loop &l : r.loops) {
		for (const vis::Edge &e : l.edges) {
			if (e.kind == vis::EdgeKind::Member && e.member >= 0 && e.member < int(r.members.size()) && r.members[size_t(e.member)].tangent_sid >= 0
					&& vis::edge_dist(e, c) <= tol) {
				return "on a member tangent to a carrier (K13)";
			}
		}
	}
	return "";
}

inline Violations check_VISIBILITY_EQUALS_NON_DIVERGENCE(const vis::Result &r, const V2 &cursor, const TracePath *planned,
		const std::vector<TraceStep> *typed, bool cursor_point_agrees) {
	Violations out;
	if (planned == nullptr || typed == nullptr) {
		return out; // no trace: the cursor is not classified
	}
	const vis::Pt c{cursor.x, cursor.y};
	const vis::Membership m = vis::contains(r, c);
	if (m == vis::Membership::Unclassified) {
		return out;
	}
	const bool in = m == vis::Membership::Inside;
	const bool shown = preview_visible(*planned, *typed, cursor_point_agrees);
	if (in != shown) {
		const std::string tag = pin_tag(r, c);
		out.push_back(v("VISIBILITY-EQUALS-NON-DIVERGENCE%s: %s at the cursor (%.3f, %.3f): the region %s it, the preview %s (margin %.6f px)",
			S(tag.empty() ? std::string() : " (" + tag + ")"), S(in ? "false visible" : "false shadow"), F(c.x), F(c.y), S(in ? "contains" : "excludes"),
			S(shown ? "is aligned to it" : "is not"), F(vis::margin(r, c))));
	}
	return out;
}

// Everything a region check needs: the region and how to recompute it.
struct VisInput {
	const vis::Result *region = nullptr;
	Tracer *tracer = nullptr; // the region's tracer (null: the recomputing checks are skipped)
	vis::Pt player;
	std::vector<PlanItem> plan;
	Flags flags;
	vis::Rect bounds;
};

namespace visdetail {

constexpr double TOL = 1e-6; // px, relative to the coordinates' scale

inline double scale_of(const vis::Pt &p) { return std::max({1.0, std::abs(p.x), std::abs(p.y)}); }

inline double carrier_dist(const Surf<F64> &s, const vis::Pt &p) {
	if (s.line) {
		const vis::Pt a{s.sx, s.sy};
		const vis::Pt d = vis::unit(vis::Pt{s.ex - s.sx, s.ey - s.sy});
		return std::abs(vis::cross(d, p - a));
	}
	return std::abs(vis::norm(p - vis::Pt{s.cx, s.cy}) - s.r);
}

inline double member_dist(const vis::Member &m, const vis::Pt &p) {
	const vis::Pt P = vis::pt_of(m.beam.p);
	const vis::Pt t = vis::pt_of(m.beam.tau);
	const double k = m.beam.kappa.v;
	if (k == 0.0) {
		return std::abs(vis::cross(t, p - P));
	}
	const vis::Pt c = P + vis::perp(t) * (1.0 / k);
	return std::abs(vis::norm(p - c) - 1.0 / std::abs(k));
}

inline bool on_some_carrier(const Scene<F64> &scene, const vis::Pt &p) {
	for (const auto &s : scene.surfaces) {
		if (carrier_dist(*s, p) <= TOL * scale_of(p)) {
			return true;
		}
	}
	return false;
}

inline bool on_clip(const vis::Rect &c, const vis::Pt &p) {
	const double t = TOL * scale_of(p);
	return (std::abs(p.x - c.x0) <= t || std::abs(p.x - c.x1) <= t || std::abs(p.y - c.y0) <= t || std::abs(p.y - c.y1) <= t) && c.grown(t).has(p);
}

inline bool is_pole(const vis::Result &r, const vis::Pt &p) {
	const double t = TOL * scale_of(p);
	return vis::norm(p - r.pk) <= t || (!r.qk_inf && vis::norm(p - r.qk) <= t);
}

inline bool on_boundary(const vis::Result &r, const vis::Pt &p, double tol) {
	for (const vis::Loop &l : r.loops) {
		for (const vis::Edge &e : l.edges) {
			if (vis::edge_dist(e, p) <= tol) {
				return true;
			}
		}
	}
	return false;
}

inline bool strictly_inside(const vis::Result &r, const vis::Pt &p) {
	int n = 0;
	for (const vis::Loop &l : r.loops) {
		for (const vis::Edge &e : l.edges) {
			n += vis::ray_crossings(e, p);
		}
	}
	return (n % 2) == 1 && !on_boundary(r, p, 1e-6 * scale_of(p));
}

inline std::string pt_str(const vis::Pt &p) {
	char b[64];
	std::snprintf(b, sizeof b, "(%.3f, %.3f)", p.x, p.y);
	return b;
}

// Whether the drawn cell (its tessellated loops) covers p (even-odd).
inline bool cell_covers(const vis::Cell &c, const vis::Pt &p);

inline bool in_polygon(const std::vector<vis::Pt> &poly, const vis::Pt &p) {
	bool in = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		const vis::Pt &a = poly[i];
		const vis::Pt &b = poly[j];
		if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
			in = !in;
		}
	}
	return in;
}

} // namespace visdetail

// ---------------------------------------------------------------- validity

inline Violations check_VIS_CLOSED(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	for (const std::string &n : r.notes) {
		if (n.find("open boundary") != std::string::npos) {
			out.push_back(v("VIS-CLOSED: %s", S(n)));
		}
	}
	for (size_t li = 0; li < r.loops.size(); li++) {
		const auto &es = r.loops[li].edges;
		for (size_t i = 0; i < es.size(); i++) {
			const vis::Edge &e = es[i];
			const vis::Edge &n = es[(i + 1) % es.size()];
			if (!(e.b == n.a)) {
				out.push_back(v("VIS-CLOSED: loop %d breaks after edge %d: %s vs %s", I(int64_t(li)), I(int64_t(i)), S(pt_str(e.b)), S(pt_str(n.a))));
			}
			if (e.a == e.b) {
				out.push_back(v("VIS-CLOSED: loop %d edge %d has zero length", I(int64_t(li)), I(int64_t(i))));
			}
		}
	}
	return out;
}

inline Violations check_VIS_SIMPLE(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	for (size_t li = 0; li < r.loops.size(); li++) {
		const auto &es = r.loops[li].edges;
		const size_t n = es.size();
		// edges joined through edges shorter than the tolerance meet like neighbours
		auto short_between = [&](size_t from, size_t to, double tol) {
			for (size_t k = (from + 1) % n; k != to; k = (k + 1) % n) {
				if (!es[k].crack && vis::norm(es[k].b - es[k].a) > tol) { // (a crack has no width: its neighbours meet)
					return false;
				}
			}
			return true;
		};
		for (size_t i = 0; i < n; i++) {
			for (size_t j = i + 1; j < n; j++) {
				const double tol = TOL * scale_of(es[i].a);
				if (es[i].crack && es[j].crack && es[i].member == es[j].member) {
					continue; // Stage 113: a crack's two edges lie on each other
				}
				if (vis::edges_overlap(es[i], es[j], tol)) {
					out.push_back(v("VIS-SIMPLE: loop %d edges %d and %d overlap", I(int64_t(li)), I(int64_t(i)), I(int64_t(j))));
					continue;
				}
				const bool next = j == i + 1 || short_between(i, j, tol);
				const bool wrap = (i == 0 && j == n - 1) || short_between(j, i, tol);
				// adjacent edges meet at their shared vertex; any other meeting is a
				// crossing. A tangent contact there (a member tangent to the carrier
				// at its corner) is found as two meetings a double root's rounding
				// apart: within 1e-5 of the scale, a meeting is the vertex.
				const double vtol = 10.0 * tol;
				for (const vis::Pt &p : vis::edge_crossings(es[i], es[j], tol)) {
					const bool shared = (next && (vis::norm(p - es[i].b) <= vtol || vis::norm(p - es[j].a) <= vtol))
						|| (wrap && (vis::norm(p - es[i].a) <= vtol || vis::norm(p - es[j].b) <= vtol));
					if (!shared) {
						out.push_back(v("VIS-SIMPLE: loop %d edges %d and %d meet at %s", I(int64_t(li)), I(int64_t(i)), I(int64_t(j)), S(pt_str(p))));
						break;
					}
				}
			}
		}
	}
	return out;
}

inline Violations check_VIS_ORIENTED(const vis::Result &r) {
	Violations out;
	int pos = 0;
	int neg = 0;
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		if (r.cells[ci].loops.empty()) {
			continue; // cut away whole by the clip rectangle
		}
		double a = 0.0;
		for (const auto &l : r.cells[ci].loops) a += vis::loop_area(l);
		if (a == 0.0) {
			out.push_back(v("VIS-ORIENTED: cell %d has no area", I(int64_t(ci))));
		}
		(a > 0 ? pos : neg)++;
	}
	if (pos > 0 && neg > 0) {
		out.push_back(v("VIS-ORIENTED: cells wind both ways (%d one way, %d the other)", I(int64_t(pos)), I(int64_t(neg))));
	}
	const int sign = pos >= neg ? 1 : -1;
	bool outer = r.cells.empty();
	for (size_t li = 0; li < r.loops.size(); li++) {
		if (r.loops[li].area == 0.0) {
			out.push_back(v("VIS-ORIENTED: loop %d has no area", I(int64_t(li))));
		}
		outer = outer || (r.loops[li].area > 0) == (sign > 0);
	}
	if (!outer) {
		out.push_back(v("VIS-ORIENTED: no loop winds the cells' way (no outer boundary)"));
	}
	return out;
}

inline Violations check_VIS_DISJOINT(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	for (const std::string &n : r.notes) {
		if (n.find("covered") != std::string::npos) {
			out.push_back(v("VIS-DISJOINT: %s", S(n)));
		}
	}
	for (size_t a = 0; a < r.loops.size(); a++) {
		for (size_t b = a + 1; b < r.loops.size(); b++) {
			for (const vis::Edge &e : r.loops[a].edges) {
				for (const vis::Edge &f : r.loops[b].edges) {
					const double tol = TOL * scale_of(e.a);
					if (e.crack && f.crack && e.member == f.member) {
						continue; // Stage 113: a crack's two edges, in two loops
					}
					if (vis::edges_overlap(e, f, tol)) {
						out.push_back(v("VIS-DISJOINT: loops %d and %d share an edge piece", I(int64_t(a)), I(int64_t(b))));
						goto next_pair;
					}
					for (const vis::Pt &p : vis::edge_crossings(e, f, tol)) {
						const bool at_vertex = vis::norm(p - e.a) <= tol || vis::norm(p - e.b) <= tol || vis::norm(p - f.a) <= tol || vis::norm(p - f.b) <= tol;
						if (!at_vertex) {
							out.push_back(v("VIS-DISJOINT: loops %d and %d cross at %s", I(int64_t(a)), I(int64_t(b)), S(pt_str(p))));
							goto next_pair;
						}
					}
				}
			}
		next_pair:;
		}
	}
	// the cells partition the region: no cell's inner point inside another cell
	for (size_t a = 0; a < r.cells.size(); a++) {
		for (size_t b = 0; b < r.cells.size(); b++) {
			if (a == b) {
				continue;
			}
			double near_edge = vis::INF;
			const std::vector<vis::Edge> be = r.cells[b].edges();
			for (const vis::Edge &e : be) {
				near_edge = std::min(near_edge, vis::edge_dist(e, r.cells[a].inside));
			}
			if (near_edge > 1e-6 * scale_of(r.cells[a].inside) && vis::inside_loop(be, r.cells[a].inside)) {
				out.push_back(v("VIS-DISJOINT: cells %d and %d overlap at %s", I(int64_t(a)), I(int64_t(b)), S(pt_str(r.cells[a].inside))));
			}
		}
	}
	return out;
}

inline Violations check_VIS_FINITE(const vis::Result &r) {
	Violations out;
	auto bad = [](const vis::Edge &e) { return !e.a.finite() || !e.b.finite() || !e.via.finite(); };
	for (size_t li = 0; li < r.loops.size(); li++) {
		for (const vis::Edge &e : r.loops[li].edges) {
			if (bad(e)) {
				out.push_back(v("VIS-FINITE: loop %d has a non-finite point", I(int64_t(li))));
				break;
			}
		}
	}
	for (const vis::Edge &e : r.whiskers) {
		if (bad(e)) {
			out.push_back(v("VIS-FINITE: a whisker has a non-finite point"));
		}
	}
	return out;
}

// ---------------------------------------------------------------- provenance

// Every edge lies on what it claims: a member (a beam through the player's
// image P_K and the frame's infinity Q_K), a surface's carrier (on its
// segment where the walk fires: stops and departure points), or the clip
// rectangle.
inline Violations check_VIS_EDGE_ON_GEOMETRY(const vis::Result &r, const Scene<F64> &scene) {
	using namespace visdetail;
	Violations out;
	auto check = [&](const vis::Edge &e, const std::string &where) {
		const double tol = TOL * std::max(scale_of(e.a), scale_of(e.b));
		switch (e.kind) {
			case vis::EdgeKind::Member: {
				if (e.member < 0 || e.member >= int(r.members.size())) {
					out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: no member %d", S(where), I(int64_t(e.member))));
					return;
				}
				const vis::Member &m = r.members[size_t(e.member)];
				for (const vis::Pt &p : {e.a, e.via, e.b}) {
					if (member_dist(m, p) > tol) {
						out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: %s is %.3g px off member %d", S(where), S(pt_str(p)), F(member_dist(m, p)), I(int64_t(e.member))));
						return;
					}
				}
				if ((!r.pk_none && member_dist(m, r.pk) > tol) || (!r.qk_inf && member_dist(m, r.qk) > tol)) {
					out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: member %d misses P_K or Q_K", S(where), I(int64_t(e.member))));
				}
				return;
			}
			case vis::EdgeKind::Carrier: {
				const Surf<F64> *s = scene.find(e.surface);
				if (s == nullptr) {
					out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: no surface %d", S(where), I(int64_t(e.surface))));
					return;
				}
				for (const vis::Pt &p : {e.a, e.via, e.b}) {
					if (carrier_dist(*s, p) > tol) {
						out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: %s is %.3g px off the carrier of %d", S(where), S(pt_str(p)), F(carrier_dist(*s, p)),
							I(int64_t(e.surface))));
						return;
					}
				}
				// a stop fires there; a departure lies on the segment when its entry
				// transforms (a pass or state entry is consumed on its carrier, B1)
				const bool on_seg = e.role == vis::End::Stop || (e.role == vis::End::Near && r.near_transforms);
				if (on_seg && !geom::on_segment(s->seg, vis::cn_of(e.via))) {
					out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: a %s edge off the segment of %d at %s", S(where), S(vis::end_name(e.role)),
						I(int64_t(e.surface)), S(pt_str(e.via))));
				}
				return;
			}
			case vis::EdgeKind::Bounds:
				if (!on_clip(r.clip, e.a) || !on_clip(r.clip, e.b)) {
					out.push_back(v("VIS-EDGE-ON-GEOMETRY: %s: a bounds edge off the clip rectangle", S(where)));
				}
				return;
		}
	};
	for (size_t li = 0; li < r.loops.size(); li++) {
		for (size_t i = 0; i < r.loops[li].edges.size(); i++) {
			check(r.loops[li].edges[i], "loop " + std::to_string(li) + " edge " + std::to_string(i));
		}
	}
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		const std::vector<vis::Edge> es = r.cells[ci].edges();
		for (size_t i = 0; i < es.size(); i++) {
			check(es[i], "cell " + std::to_string(ci) + " edge " + std::to_string(i));
		}
	}
	return out;
}

// Every vertex is a pole (P_K, Q_K), on the clip rectangle, or on a surface's
// carrier: where a member meets what stops it or departs from it.
inline Violations check_VIS_VERTEX_PROVENANCE(const vis::Result &r, const Scene<F64> &scene) {
	using namespace visdetail;
	Violations out;
	for (size_t li = 0; li < r.loops.size(); li++) {
		for (const vis::Edge &e : r.loops[li].edges) {
			if (!is_pole(r, e.a) && !on_clip(r.clip, e.a) && !on_some_carrier(scene, e.a)) {
				out.push_back(v("VIS-VERTEX-PROVENANCE: loop %d vertex %s is on no carrier", I(int64_t(li)), S(pt_str(e.a))));
			}
		}
	}
	return out;
}

// A boundary piece of a member (a shadow edge) runs between two anchors: a
// pole, the clip rectangle, or a surface's carrier at each end.
inline Violations check_VIS_SHADOW_ANCHORED(const vis::Result &r, const Scene<F64> &scene) {
	using namespace visdetail;
	Violations out;
	for (size_t li = 0; li < r.loops.size(); li++) {
		for (const vis::Edge &e : r.loops[li].edges) {
			if (e.kind != vis::EdgeKind::Member) {
				continue;
			}
			for (const vis::Pt &p : {e.a, e.b}) {
				if (!is_pole(r, p) && !on_clip(r.clip, p) && !on_some_carrier(scene, p)) {
					out.push_back(v("VIS-SHADOW-ANCHORED: loop %d: the shadow edge on member %d ends at %s, on no carrier", I(int64_t(li)),
						I(int64_t(e.member)), S(pt_str(p))));
				}
			}
		}
	}
	return out;
}

// A surface that fires from both sides on its segment (always present, no
// table, not planned) never has region inside it: a cursor there is a stop, at most.
inline Violations check_VIS_NO_BLOCKER_INSIDE(const vis::Result &r, const Scene<F64> &scene, const Flags &flags, const std::vector<PlanItem> &plan) {
	using namespace visdetail;
	Violations out;
	for (const auto &sp : scene.surfaces) {
		const Surf<F64> &s = *sp;
		if (std::any_of(plan.begin(), plan.end(), [&](const PlanItem &p) { return p.surface_id == s.id; })) {
			continue; // a planned surface: its points are departure points
		}
		if (!s.presence.empty() || !s.state_key.empty() || !s.left_states.empty() || !s.right_states.empty() || s.effect(true, flags) == EffectType::Pass
				|| s.effect(false, flags) == EffectType::Pass) {
			continue;
		}
		if (carrier_dist(s, r.pk) <= TOL * scale_of(r.pk) || (!r.qk_inf && carrier_dist(s, r.qk) <= TOL * scale_of(r.qk))) {
			continue; // members run along this carrier (the player, or the frame's infinity, on it)
		}
		std::vector<vis::Pt> samples;
		if (s.line) {
			for (int k = 1; k < 8; k++) {
				samples.push_back(vis::Pt{s.sx + (s.ex - s.sx) * k / 8.0, s.sy + (s.ey - s.sy) * k / 8.0});
			}
		} else if (s.has_v) {
			samples.push_back(vis::Pt{s.vx, s.vy});
		}
		for (vis::Pt p : samples) {
			if (!s.line) {
				// on the circle itself (a stored via is rounded off it)
				const vis::Pt c{s.cx, s.cy};
				p = c + vis::unit(p - c) * s.r;
			}
			if (strictly_inside(r, p)) {
				out.push_back(v("VIS-NO-BLOCKER-INSIDE: the region covers %s on surface %d", S(pt_str(p)), I(int64_t(s.id))));
				break;
			}
		}
	}
	return out;
}

// The last entry's departure points (the region's near side) belong to the
// region: on its boundary, or inside where another layer of the walk (the
// part after the wrap) meets them.
inline Violations check_VIS_SOURCE_ON_BOUNDARY(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		for (const vis::Edge &e : r.cells[ci].edges()) {
			if (e.kind == vis::EdgeKind::Carrier && e.role == vis::End::Near && r.domain.has(e.via)
					&& vis::contains(r, e.via) != vis::Membership::Inside && !on_boundary(r, e.via, TOL * scale_of(e.via))) {
				out.push_back(v("VIS-SOURCE-ON-BOUNDARY: cell %d's departure point %s is outside the region", I(int64_t(ci)), S(pt_str(e.via))));
			}
		}
	}
	return out;
}

// ---------------------------------------------------------------- consistency

// The region holds each cell's middle member over its visible intervals (the
// cells fan out from their near side: star-shaped along the members).
inline Violations check_VIS_STAR(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		const vis::Cell &c = r.cells[ci];
		for (const vis::Interval &iv : c.mid.iv) {
			const bool loop = iv.b.kind == vis::End::Loop && iv.a.t == iv.b.t;
			for (double f : {0.25, 0.5, 0.75}) {
				const double t = vis::along(iv.a.t, iv.b.t, f, loop);
				const std::optional<CN<F64>> z = std::isinf(t) ? c.mid_beam.at_infinity() : std::optional<CN<F64>>(c.mid_beam.at(F64{t}));
				if (!z) {
					continue;
				}
				const vis::Pt p = vis::pt_of(*z);
				if (!r.domain.has(p)) {
					continue;
				}
				if (vis::contains(r, p) != vis::Membership::Inside && !on_boundary(r, p, 1e-6 * scale_of(p))) {
					out.push_back(v("VIS-STAR: cell %d's middle member leaves the region at %s", I(int64_t(ci)), S(pt_str(p))));
					goto next_cell;
				}
			}
		}
	next_cell:;
	}
	return out;
}

// The members bound cells of one walk shape: a cast inside a cell agrees.
inline Violations check_VIS_CELL_CONSTANT(const VisInput &in) {
	Violations out;
	const vis::Result &r = *in.region;
	for (const std::string &n : r.notes) {
		if (n.find("shapes differ") != std::string::npos) {
			out.push_back(v("VIS-CELL-CONSTANT: %s", S(n)));
		}
	}
	if (in.tracer == nullptr) {
		return out;
	}
	std::map<int, std::unique_ptr<vis::Builder>> builders; // per half (Stage 111)
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		const vis::Cell &c = r.cells[ci];
		std::unique_ptr<vis::Builder> &b = builders[c.half];
		if (!b) {
			b = std::make_unique<vis::Builder>(*in.tracer, in.player, in.plan, in.flags, in.bounds, vis::Half(c.half));
		}
		// away from the middle
		const std::optional<vis::Cast> k = b->probe_between(r.members[size_t(c.left)], r.members[size_t(c.right)], 0.381966);
		if (k && !k->same_shape(c.mid)) {
			char at[96];
			std::snprintf(at, sizeof at, " (at (%.6g, %.6g) key %.9g; the middle at (%.6g, %.6g) key %.9g)", k->cursor.x, k->cursor.y, k->key, c.mid.cursor.x,
				c.mid.cursor.y, c.mid.key);
			out.push_back(v("VIS-CELL-CONSTANT: cell %d: %s inside, %s in the middle%s", I(int64_t(ci)), S(k->shape()), S(c.mid.shape()), S(std::string(at))));
		}
	}
	return out;
}

// The last entry departs where the region of the plan without it reaches its surface.
inline Violations check_VIS_ITERATION_CHAIN(const VisInput &in) {
	Violations out;
	if (in.tracer == nullptr || in.plan.empty() || in.plan.back().is_infinity() || in.plan.back().is_origin()) {
		return out;
	}
	const vis::Result &r = *in.region;
	const std::vector<PlanItem> prefix(in.plan.begin(), in.plan.end() - 1);
	const vis::Result prev = vis::compute(*in.tracer, in.player, prefix, in.flags, in.bounds);
	if (!prev.supported) {
		return out;
	}
	// Stage 111: a split plan's cells depart where their own orientation's walk
	// arrives: in the region of the plan without its last entry built in that
	// same forced orientation
	std::map<int, vis::Result> prev_half;
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		const vis::Result *check = &prev;
		if (r.split) {
			const int half = r.cells[ci].half;
			if (!prev_half.count(half)) {
				vis::Builder b(*in.tracer, in.player, prefix, in.flags, in.bounds, vis::Half(half));
				vis::Result ph = b.build();
				for (vis::Loop &l : ph.loops) {
					l.area = vis::loop_area(l.edges);
				}
				prev_half.emplace(half, std::move(ph));
			}
			if (!prev_half.at(half).supported) {
				continue;
			}
			check = &prev_half.at(half);
		}
		for (const vis::Edge &e : r.cells[ci].edges()) {
			if (e.kind != vis::EdgeKind::Carrier || e.role != vis::End::Near || e.surface != in.plan.back().surface_id) {
				continue; // a portal departs its pair: the plan without it reaches the source
			}
			// (beyond the domain: unclassified; Stage 113: a departure at a tangency of
			// the previous region's member with the carrier lies on its boundary a
			// rounding away -- within 1e-6 px it is on it)
			if (vis::contains(*check, e.via) == vis::Membership::Outside && vis::margin(*check, e.via) > 1e-6 * visdetail::scale_of(e.via)) {
				out.push_back(v("VIS-ITERATION-CHAIN: cell %d departs from %s, outside the previous region", I(int64_t(ci)), S(visdetail::pt_str(e.via))));
			}
		}
	}
	return out;
}

// At every point of interest that is exact data (a segment's endpoint, a
// piece's end, in the final frame) the region decides as the solver does --
// those are the boundary points a cursor really lands on; a computed point (a
// tangency, a crossing) is decided by rounding on both sides.
inline Violations check_VIS_BOUNDARY_DECIDED(const vis::Result &r) {
	Violations out;
	// Stage 111: a split plan casts every point in both orientations; the
	// solver's own answer is the orientation it picks, so the point is reached
	// when either half's cast reaches it
	// (every copy of the point, in either half, from any frame: a frame-j
	// member's image of the point lands a rounding away from the exact one)
	auto reached_at = [&](const vis::Pt &c) {
		const double tol = 1e-9 * std::max({1.0, std::abs(c.x), std::abs(c.y)});
		for (const vis::Member &o : r.members) {
			if (o.crit.valid && o.poi_ok && o.poi_chosen && vis::norm(o.cursor - c) <= tol) {
				return true;
			}
		}
		return false;
	};
	std::set<std::pair<double, double>> done;
	for (size_t mi = 0; mi < r.members.size(); mi++) {
		const vis::Member &m = r.members[mi];
		if (!m.exact || !m.crit.valid || !done.insert({m.cursor.x, m.cursor.y}).second) {
			continue;
		}
		const vis::Membership c = vis::contains(r, m.cursor);
		if (c == vis::Membership::Unclassified) {
			continue;
		}
		const bool ok = reached_at(m.cursor);
		if ((c == vis::Membership::Inside) != ok) {
			out.push_back(v("VIS-BOUNDARY-DECIDED: at %s (%s) the region says %s, the solver %s", S(visdetail::pt_str(m.cursor)), S(m.why),
				S(c == vis::Membership::Inside ? "in" : "out"), S(ok ? "reached" : "not reached")));
		}
	}
	return out;
}

// The merged loops and the drawn cells agree away from the boundary.
inline Violations check_VIS_CONTAINS_MATCHES_POLYGON(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	const vis::Rect &d = r.domain;
	for (int i = 0; i <= 32; i++) {
		for (int j = 0; j <= 18; j++) {
			const vis::Pt p{d.x0 + (d.x1 - d.x0) * (i + 0.37) / 33.0, d.y0 + (d.y1 - d.y0) * (j + 0.61) / 19.0};
			if (vis::margin(r, p) < 1.0) {
				continue;
			}
			bool in_cells = false;
			for (const vis::Cell &c : r.cells) {
				in_cells = in_cells || cell_covers(c, p);
			}
			const vis::Membership m = vis::contains(r, p);
			if (m != vis::Membership::Unclassified && (m == vis::Membership::Inside) != in_cells) {
				out.push_back(v("VIS-CONTAINS-MATCHES-POLYGON: at %s the region says %s, the cells %s", S(pt_str(p)), S(m == vis::Membership::Inside ? "in" : "out"),
					S(in_cells ? "in" : "out")));
			}
		}
	}
	return out;
}

// What is drawn is the region (F8): the fill's triangles (vis::fill_triangles,
// one conforming mesh of the cells) cover a sampled point more than 1 px from the
// boundary exactly once where contains() says Inside and never elsewhere, and
// their area is the cells' exact area within the tessellation's bound.
inline Violations check_VIS_DRAWN_MATCHES(const vis::Result &r) {
	using namespace visdetail;
	Violations out;
	const vis::Fill f = vis::fill_triangles(r);
	auto tri_area = [&](size_t t) {
		const vis::Pt &a = f.points[f.indices[t]];
		const vis::Pt &b = f.points[f.indices[t + 1]];
		const vis::Pt &c = f.points[f.indices[t + 2]];
		return vis::cross(b - a, c - a) / 2.0;
	};
	auto covers = [&](size_t t, const vis::Pt &p) {
		const vis::Pt &a = f.points[f.indices[t]];
		const vis::Pt &b = f.points[f.indices[t + 1]];
		const vis::Pt &c = f.points[f.indices[t + 2]];
		const double d1 = vis::cross(b - a, p - a);
		const double d2 = vis::cross(c - b, p - b);
		const double d3 = vis::cross(a - c, p - c);
		return (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
	};
	const vis::Rect &d = r.domain;
	int reported = 0;
	for (int i = 0; i <= 32 && reported < 4; i++) {
		for (int j = 0; j <= 18 && reported < 4; j++) {
			const vis::Pt p{d.x0 + (d.x1 - d.x0) * (i + 0.37) / 33.0, d.y0 + (d.y1 - d.y0) * (j + 0.61) / 19.0};
			const vis::Membership m = vis::contains(r, p);
			if (m == vis::Membership::Unclassified || vis::margin(r, p) < 1.0) {
				continue;
			}
			int n = 0;
			for (size_t t = 0; t + 2 < f.indices.size(); t += 3) {
				n += std::abs(tri_area(t)) > 0.0 && covers(t, p) ? 1 : 0;
			}
			if (n != (m == vis::Membership::Inside ? 1 : 0)) {
				out.push_back(v("VIS-DRAWN-MATCHES: at %s the region says %s, the drawn fill covers it %d times", S(pt_str(p)),
					S(m == vis::Membership::Inside ? "in" : "out"), I(int64_t(n))));
				reported++;
			}
		}
	}
	double drawn = 0.0;
	for (size_t t = 0; t + 2 < f.indices.size(); t += 3) {
		drawn += std::abs(tri_area(t));
	}
	double exact = 0.0;
	double perimeter = 0.0;
	for (const vis::Cell &c : r.cells) {
		for (const auto &l : c.loops) {
			const std::vector<vis::Pt> poly = vis::clip_polygon(vis::polyline(l), d);
			exact += std::abs(vis::signed_area(poly));
			for (size_t k = 0; k < poly.size(); k++) {
				perimeter += vis::norm(poly[(k + 1) % poly.size()] - poly[k]);
			}
		}
	}
	if (std::abs(drawn - exact) > 0.5 * perimeter + 1e-6 * exact) {
		out.push_back(v("VIS-DRAWN-MATCHES: the fill's area %.3f differs from the cells' %.3f (bound %.3f)", F(drawn), F(exact), F(0.5 * perimeter)));
	}
	return out;
}

inline Violations check_VIS_DETERMINISTIC(const VisInput &in) {
	Violations out;
	if (in.tracer == nullptr) {
		return out;
	}
	const vis::Result again = vis::compute(*in.tracer, in.player, in.plan, in.flags, in.bounds);
	const vis::Result &r = *in.region;
	bool same = again.loops.size() == r.loops.size() && again.cells.size() == r.cells.size();
	for (size_t li = 0; same && li < r.loops.size(); li++) {
		same = again.loops[li].edges.size() == r.loops[li].edges.size();
		for (size_t i = 0; same && i < r.loops[li].edges.size(); i++) {
			const vis::Edge &a = again.loops[li].edges[i];
			const vis::Edge &b = r.loops[li].edges[i];
			same = a.a == b.a && a.b == b.b && a.via == b.via && a.inclusive == b.inclusive;
		}
	}
	if (!same) {
		out.push_back(v("VIS-DETERMINISTIC: the same inputs gave a different region"));
	}
	return out;
}

// The region has area wherever it is (B4, Stage 110). Its only 1D parts are
// whiskers of two kinds: an arc of a member tangent at a frame's finite infinity
// point to a carrier through it (every other member crosses that carrier there),
// and a piece of the last planned entry's own segment when that side is a
// terminal (the arrow ends exactly at the cursor). A whisker anywhere else -- a departure point off
// the planned segment, the thin lines of the B4 report -- is a violation, as is
// a loop that doubles back on itself (a spike of no area).
// Stage 129 (GAME_SPEC 15.1): the planned walk to cursor c consumes an entry that
// names a piece exactly at an end of its span -- a crossing that cuts the planned
// design surface: the walks beside it meet the other piece (or the cutting
// surface) and leave the plan, so the cursors along that walk are a whisker
inline bool consumes_at_piece_end(Tracer &tracer, const vis::Pt &player, const vis::Pt &c, const std::vector<PlanItem> &plan, const Flags &flags) {
	bool spans = false;
	for (const PlanItem &it : plan) {
		spans = spans || it.span.has_value();
	}
	if (!spans) {
		return false;
	}
	const std::optional<LadderResult> lr = tracer.solve(player.x, player.y, c.x, c.y, plan, flags);
	if (!lr) {
		return false;
	}
	size_t j = 0;
	for (const EvInfo &e : ev_infos(lr->planned)) {
		if (j < plan.size() && ((plan[j].is_infinity() && e.kind == EventKind::Infinity) || (plan[j].is_origin() && e.kind == EventKind::Origin)) && e.applied) {
			j++;
			continue;
		}
		if (j >= plan.size() || e.kind != EventKind::Hit || plan[j].is_infinity() || plan[j].is_origin() || e.surface_id != plan[j].surface_id || e.left != plan[j].left) {
			continue;
		}
		if (const auto &sp = plan[j].span; sp && e.has_point) {
			const double tol = 1e-6 * std::max({1.0, std::abs(e.fx), std::abs(e.fy)});
			for (int k : {0, 2}) {
				if (std::abs(e.fx - (*sp)[size_t(k)]) <= tol && std::abs(e.fy - (*sp)[size_t(k) + 1]) <= tol) {
					return true;
				}
			}
		}
		j++;
	}
	return false;
}

inline Violations check_VIS_REGULAR(const vis::Result &r, const Scene<F64> *scene, const Flags &flags, const std::vector<PlanItem> &plan, Tracer *tracer = nullptr) {
	using namespace visdetail;
	Violations out;
	for (size_t wi = 0; wi < r.whiskers.size(); wi++) {
		const vis::Edge &w = r.whiskers[wi];
		if (w.kind == vis::EdgeKind::Member) {
			// (Stage 111: and the straight member through P_K and a finite Q_K, a
			// line where its neighbours are circles: its own realization's reach)
			const bool tangent_pole = w.member >= 0 && w.member < int(r.members.size())
				&& (r.members[size_t(w.member)].tangent_pole || r.members[size_t(w.member)].straight);
			if (!tangent_pole && !(tracer != nullptr && consumes_at_piece_end(*tracer, r.player, w.via, plan, flags))) {
				out.push_back(v("VIS-REGULAR: whisker %d lies along member %d, which is not tangent at a frame's infinity point", I(int64_t(wi)),
					I(int64_t(w.member))));
			}
			continue;
		}
		if (scene == nullptr) {
			continue;
		}
		const Surf<F64> *s = scene->find(w.surface);
		const bool last = !plan.empty() && !plan.back().is_infinity() && !plan.back().is_origin() && plan.back().surface_id == w.surface;
		const bool terminal = last && s != nullptr && s->effect(plan.back().left, flags) == EffectType::Terminal;
		if (!terminal || !geom::on_segment(s->seg, vis::cn_of(w.via))) {
			out.push_back(v("VIS-REGULAR: whisker %d on surface %d at %s has no area and is not on the planned terminal's segment", I(int64_t(wi)),
				I(int64_t(w.surface)), S(pt_str(w.via))));
		}
	}
	for (size_t li = 0; li < r.loops.size(); li++) {
		const auto &es = r.loops[li].edges;
		const size_t n = es.size();
		for (size_t i = 0; n > 1 && i < n; i++) {
			// an edge and the next on the same geometry, the next back to where the first began
			const vis::Edge &a = es[i];
			const vis::Edge &b = es[(i + 1) % n];
			const bool same = a.kind == b.kind && a.member == b.member && a.surface == b.surface && a.kind != vis::EdgeKind::Bounds;
			const double tol = TOL * scale_of(a.a);
			// the next edge retraces this one (its middle on this edge): a full circle
			// split into two arcs is not a spike
			if (same && !(a.crack && b.crack) && vis::norm(a.b - a.a) > tol && vis::norm(a.a - b.b) <= tol && vis::edge_dist(a, b.via) <= tol) {
				out.push_back(v("VIS-REGULAR: loop %d doubles back at %s (edges %d and %d, a spike of no area)", I(int64_t(li)), S(pt_str(a.b)),
					I(int64_t(i)), I(int64_t((i + 1) % n))));
			}
		}
	}
	return out;
}

// Every region check, in order.
inline Violations check_visibility(const VisInput &in) {
	Violations out;
	if (in.region == nullptr || !in.region->supported) {
		return out;
	}
	const vis::Result &r = *in.region;
	auto add = [&](Violations vs) { out.insert(out.end(), vs.begin(), vs.end()); };
	add(check_VIS_CLOSED(r));
	add(check_VIS_SIMPLE(r));
	if (r.cells.empty() && r.loops.empty()) {
		add(check_VIS_ORIENTED(r));
		add(check_VIS_DISJOINT(r));
	}
	// Stage 111: each half winds its own way, and the two share the crack;
	// Stage 113: so do a pencil's two sheets, which meet at a pole
	for (const auto &[half, wind] : vis::parts_of(r)) {
		const vis::Result h = vis::part_of(r, half, wind);
		add(check_VIS_ORIENTED(h));
		add(check_VIS_DISJOINT(h));
	}
	add(check_VIS_FINITE(r));
	if (in.tracer != nullptr) {
		Flags flags = in.flags;
		flags.erase(GHOST);
		const Scene<F64> scene = Scene<F64>::from_defs(in.tracer->defs(), F64{1.0});
		add(check_VIS_EDGE_ON_GEOMETRY(r, scene));
		add(check_VIS_VERTEX_PROVENANCE(r, scene));
		add(check_VIS_SHADOW_ANCHORED(r, scene));
		add(check_VIS_NO_BLOCKER_INSIDE(r, scene, flags, in.plan));
		add(check_VIS_REGULAR(r, &scene, flags, in.plan, in.tracer));
	} else {
		add(check_VIS_REGULAR(r, nullptr, in.flags, in.plan));
	}
	add(check_VIS_SOURCE_ON_BOUNDARY(r));
	add(check_VIS_STAR(r));
	add(check_VIS_CELL_CONSTANT(in));
	add(check_VIS_ITERATION_CHAIN(in));
	add(check_VIS_BOUNDARY_DECIDED(r));
	add(check_VIS_CONTAINS_MATCHES_POLYGON(r));
	add(check_VIS_DRAWN_MATCHES(r));
	add(check_VIS_DETERMINISTIC(in));
	return out;
}

namespace visdetail {
inline bool cell_covers(const vis::Cell &c, const vis::Pt &p) {
	bool in = false;
	for (const auto &l : c.loops) {
		if (in_polygon(vis::polyline(l), p)) in = !in;
	}
	return in;
}
} // namespace visdetail

} // namespace inv
} // namespace rc
