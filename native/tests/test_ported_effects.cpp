// Stage 119.3: retired GUT tests (effects, beam geometry, BigFloat, the stage
// 81-87 oracle fixtures) ported to the native core they exercised. Each
// TEST_CASE is "ported: <gut file stem>: <test function>" and asserts what the
// GUT test asserted (the same inputs and expected values), on the native
// function the GDScript class bound; the stage 81-87 fixture rows share one
// oracle-diff case.
#include "goldens.hpp"
#include "oracle_fixtures.hpp"
#include "rc/beam_effects.hpp"
#include "rc/invariants.hpp"
#include "rc/tracer.hpp"
#include "rc/visibility.hpp"
#include "rc/visual.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace rc;
using namespace rc::test;

namespace {

constexpr double PI_D = 3.14159265358979323846;

// A Vector2 component (float32) as GDScript hands it to the solver.
double f32(double x) { return double(float(x)); }

template <Num N>
CN<N> cnl(const N &like, double x, double y) {
	return CN<N>{like.like(x), like.like(y)};
}

CN<F64> cn64(double x, double y) { return CN<F64>{F64{x}, F64{y}}; }

SideDef side(EffectType t, bool interactive) {
	SideDef s;
	s.type = t;
	s.interactive = interactive;
	return s;
}

// Segment.from_coords(s, e, (s + e) / 2) with one side config on both sides.
SurfaceDef line_def(int id, double sx, double sy, double ex, double ey, EffectType t, bool interactive) {
	SurfaceDef d;
	d.id = id;
	d.line = true;
	d.sx = sx; d.sy = sy; d.ex = ex; d.ey = ey;
	d.has_via = true; d.vx = (sx + ex) / 2.0; d.vy = (sy + ey) / 2.0;
	d.left = side(t, interactive);
	d.right = d.left;
	return d;
}

// A full circle (SceneDefs: the carrier's exact center and radius).
SurfaceDef circle_def(int id, double cx, double cy, double r, EffectType t) {
	SurfaceDef d;
	d.id = id;
	d.line = false;
	d.full = true;
	d.has_circle = true;
	d.cx = cx; d.cy = cy; d.radius = r;
	d.sx = cx + r; d.sy = cy;
	d.ex = cx - r / 2.0; d.ey = cy - 0.866 * r;
	d.has_via = true; d.vx = cx - r / 2.0; d.vy = cy + 0.866 * r;
	d.left = side(t, true);
	d.right = d.left;
	return d;
}

SurfaceDef arc_def(int id, double sx, double sy, double ex, double ey, double vx, double vy, EffectType t) {
	SurfaceDef d;
	d.id = id;
	d.line = false;
	d.full = false;
	d.sx = sx; d.sy = sy; d.ex = ex; d.ey = ey;
	d.has_via = true; d.vx = vx; d.vy = vy;
	d.left = side(t, true);
	d.right = d.left;
	return d;
}

// RoomBuilder.create_room_surfaces: terminal blocks, ids first..first+3.
std::vector<SurfaceDef> room(double w, double h) {
	return {line_def(1, 0, 0, w, 0, EffectType::Terminal, false), line_def(2, w, 0, w, h, EffectType::Terminal, false),
		line_def(3, w, h, 0, h, EffectType::Terminal, false), line_def(4, 0, h, 0, 0, EffectType::Terminal, false)};
}

std::vector<SurfaceDef> scene_dump(const std::string &name) {
	return defs_from_json(load_json("bench/scenes/" + name + ".surfaces.json")["surfaces"]);
}

bool near2(double ax, double ay, double bx, double by, double tol) {
	return std::fabs(ax - bx) <= tol && std::fabs(ay - by) <= tol;
}

// map_point as a point (nullopt: infinity) on the f64 surface.
std::optional<CN<F64>> map_of(const Surf<F64> &surf, std::optional<CN<F64>> z, bool inverse = false) {
	return effects::map_point(z, surf, inverse);
}

const Surf<F64> &only_surface(const Scene<F64> &sc) { return *sc.surfaces[0]; }

// BeamGeom.hit_records (the binding's composition of the native geometry):
// the roots with their points, the endpoint they are exactly at and the
// on-segment test; a known point at an endpoint replaces the nearest root.
template <Num N>
struct HitRec {
	N t;
	CN<N> point;
	int at_endpoint = 0;
	bool on_segment = false;
};

template <Num N>
std::vector<HitRec<N>> hit_records(const Beam<N> &b, const Seg<N> &sg, bool depart_on, const std::vector<std::pair<CN<N>, N>> &known = {}) {
	std::vector<HitRec<N>> recs;
	for (const N &t : geom::hits(b, sg, depart_on)) {
		const CN<N> pt = b.at(t);
		recs.push_back(HitRec<N>{t, pt, geom::endpoint_at(sg, pt), false});
	}
	for (const auto &k : known) {
		const int which = geom::endpoint_at(sg, k.first);
		if (which == 0 || recs.empty()) {
			continue;
		}
		size_t best = 0;
		double best_d = INF_D;
		for (size_t r = 0; r < recs.size(); r++) {
			const double d = recs[r].point.sub(k.first).abs2().to_float();
			if (d < best_d) {
				best_d = d;
				best = r;
			}
		}
		recs[best] = HitRec<N>{k.second, k.first, which, false};
	}
	for (HitRec<N> &r : recs) {
		r.on_segment = geom::on_segment(sg, r.point);
	}
	return recs;
}

template <Num N>
Seg<N> line_seg(const N &like, double ax, double ay, double bx, double by, int id = 1) {
	SegDef d;
	d.id = id;
	d.kind = SegKind::Line;
	d.sx = ax; d.sy = ay; d.ex = bx; d.ey = by;
	d.has_v = true; d.vx = (ax + bx) / 2.0; d.vy = (ay + by) / 2.0;
	return geom::seg_from_def(d, like);
}

template <class V>
std::vector<double> sorted_floats(const V &ts) {
	std::vector<double> out;
	for (const auto &t : ts) {
		out.push_back(t.to_float());
	}
	std::sort(out.begin(), out.end());
	return out;
}

// Vector2.distance_to (float32).
float dist32(float ax, float ay, float bx, float by) {
	const float dx = ax - bx;
	const float dy = ay - by;
	return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// ---------------------------------------------------------------- test_stage40_circle_inversion
// The map of a full inversion circle, center (200, 200), r 100.

namespace {
Scene<F64> inversion_scene() { return Scene<F64>::from_defs({circle_def(1, 200, 200, 100, EffectType::Inversion)}, F64{1}); }
} // namespace

TEST_CASE("ported: test_stage40_circle_inversion: test_stage40_inversion_apply_point") {
	const Scene<F64> sc = inversion_scene();
	const auto w = map_of(only_surface(sc), cn64(400, 200));
	REQUIRE(w);
	CHECK(near2(w->fx(), w->fy(), 250, 200, 0.01));
}

TEST_CASE("ported: test_stage40_circle_inversion: test_stage40_inversion_self_inverse") {
	const Scene<F64> sc = inversion_scene();
	const auto w = map_of(only_surface(sc), map_of(only_surface(sc), cn64(400, 200)));
	REQUIRE(w);
	CHECK(near2(w->fx(), w->fy(), 400, 200, 0.01));
}

TEST_CASE("ported: test_stage40_circle_inversion: test_stage40_inversion_point_on_circle_fixed") {
	const Scene<F64> sc = inversion_scene();
	const auto w = map_of(only_surface(sc), cn64(300, 200));
	REQUIRE(w);
	CHECK(near2(w->fx(), w->fy(), 300, 200, 0.01));
}

TEST_CASE("ported: test_stage40_circle_inversion: test_stage40_S2_transform_round_trip") {
	const Scene<F64> sc = inversion_scene();
	const auto back = map_of(only_surface(sc), map_of(only_surface(sc), cn64(400, 200)), true);
	REQUIRE(back);
	CHECK(near2(back->fx(), back->fy(), 400, 200, 0.01));
}

TEST_CASE("ported: test_stage40_circle_inversion: test_stage40_inversion_center_maps_to_infinity") {
	const Scene<F64> sc = inversion_scene();
	CHECK(!map_of(only_surface(sc), cn64(200, 200)));
	const auto c = map_of(only_surface(sc), std::nullopt);
	REQUIRE(c);
	CHECK(near2(c->fx(), c->fy(), 200, 200, 0.01));
}

TEST_CASE("ported: test_stage40_circle_inversion: test_stage40_S16_no_nan_inf") {
	const Scene<F64> sc = inversion_scene();
	const double pts[][2] = {{100, 100}, {300, 300}, {50, 200}, {400, 100}, {200, 100}, {200, 300}, {100, 200}, {300, 200}, {150, 150}, {350, 250}};
	for (const auto &p : pts) {
		const auto w = map_of(only_surface(sc), cn64(p[0], p[1]));
		REQUIRE(w);
		CHECK(std::isfinite(w->fx()));
		CHECK(std::isfinite(w->fy()));
		// closed form c + r^2 / conj(z - c)
		const double ux = p[0] - 200, uy = p[1] - 200;
		const double k = 10000.0 / (ux * ux + uy * uy);
		CHECK_MESSAGE(near2(w->fx(), w->fy(), 200 + ux * k, 200 + uy * k, 0.01), "closed form for (" << p[0] << ", " << p[1] << ")");
	}
}

// ---------------------------------------------------------------- test_stage44_reflection_formula
// Reflection maps of one reflective surface against the closed forms (TOL 0.5).

namespace {
constexpr double TOL44 = 0.5;
Scene<F64> mirror_line(double sx, double sy, double ex, double ey) {
	return Scene<F64>::from_defs({line_def(1, sx, sy, ex, ey, EffectType::Reflection, true)}, F64{1});
}
Scene<F64> mirror_circle(bool inversion) {
	return Scene<F64>::from_defs({circle_def(1, 960, 540, 200, inversion ? EffectType::Inversion : EffectType::Reflection)}, F64{1});
}
CN<F64> map1(const Scene<F64> &sc, double x, double y) { return *map_of(only_surface(sc), cn64(x, y)); }
CN<F64> map2(const Scene<F64> &sc, double x, double y) { return *map_of(only_surface(sc), map_of(only_surface(sc), cn64(x, y))); }
} // namespace

TEST_CASE("ported: test_stage44_reflection_formula: test_vertical_line_reflection") {
	const CN<F64> w = map1(mirror_line(500, 0, 500, 1000), 300, 400);
	CHECK(near2(w.fx(), w.fy(), 700, 400, TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_horizontal_line_reflection") {
	const CN<F64> w = map1(mirror_line(0, 500, 1000, 500), 300, 400);
	CHECK(near2(w.fx(), w.fy(), 300, 600, TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_diagonal_line_reflection") {
	const CN<F64> w = map1(mirror_line(0, 0, 1000, 1000), 300, 100);
	CHECK(near2(w.fx(), w.fy(), 100, 300, TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_horizontal_reflection_self_inverse") {
	const CN<F64> w = map2(mirror_line(0, 500, 1000, 500), 300, 400);
	CHECK(near2(w.fx(), w.fy(), 300, 400, TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_diagonal_reflection_self_inverse") {
	const CN<F64> w = map2(mirror_line(0, 0, 1000, 1000), 300, 100);
	CHECK(near2(w.fx(), w.fy(), 300, 100, TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_circle_reflection_point_on_circle_fixed") {
	const CN<F64> w = map1(mirror_circle(false), 960, 340);
	CHECK(near2(w.fx(), w.fy(), 960, 340, TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_circle_reflection_equals_inversion") {
	const CN<F64> r = map1(mirror_circle(false), 800, 400);
	const CN<F64> i = map1(mirror_circle(true), 800, 400);
	const double ux = 800 - 960, uy = 400 - 540;
	const double k = 40000.0 / (ux * ux + uy * uy);
	CHECK(near2(r.fx(), r.fy(), 960 + ux * k, 540 + uy * k, TOL44));
	CHECK(near2(r.fx(), r.fy(), i.fx(), i.fy(), TOL44));
}

TEST_CASE("ported: test_stage44_reflection_formula: test_circle_reflection_self_inverse") {
	const CN<F64> w = map2(mirror_circle(false), 800, 400);
	CHECK(near2(w.fx(), w.fy(), 800, 400, TOL44));
}

// ---------------------------------------------------------------- test_stage45_grazing
// A near-full reflective arc (center (960, 540), r 200, the gap at (1160, 540))
// in a 1920x1080 room of blocks; a beam grazing it at distance d from the
// center reflects into an arc of radius r^2 / (2 d).

namespace {
std::vector<SurfaceDef> grazing_scene() {
	std::vector<SurfaceDef> defs = room(1920, 1080);
	defs.push_back(arc_def(5, 1160, f32(540.01), 1160, f32(539.99), 760, 540, EffectType::Reflection));
	return defs;
}
std::optional<TracePair> grazing_trace(Tracer &tr, double y) {
	return tr.trace_pair(V2::of(200, y), V2::of(1700, y), {}, Flags());
}
} // namespace

TEST_CASE("ported: test_stage45_grazing: test_grazing_arc_radius_equals_half_surface_radius") {
	Tracer tr;
	tr.set_emit_infinity(true);
	tr.set_defs(grazing_scene());
	const auto pair = grazing_trace(tr, 341);
	REQUIRE(pair);
	const TraceStep *arc_step = nullptr;
	for (const TraceStep &s : pair->physical.steps) {
		if (s.frame_id != IDENTITY_FRAME && s.is_arc_step) {
			arc_step = &s;
			break;
		}
	}
	REQUIRE_MESSAGE(arc_step != nullptr, "a reflected arc step");
	REQUIRE_MESSAGE(arc::is_arc(arc_step->start, arc_step->via, arc_step->end), "the reflected arc step renders as an arc");
	const arc::ArcParams p = arc::arc_params(arc_step->start, arc_step->via, arc_step->end);
	CHECK(std::fabs(p.radius - 100.0) <= 50.0);
}

TEST_CASE("ported: test_stage45_grazing: test_grazing_arc_radius_multiple_angles") {
	Tracer tr;
	tr.set_emit_infinity(true);
	tr.set_defs(grazing_scene());
	const double r = 200.0;
	for (double y_offset : {1.0, 5.0, 10.0, 20.0, 50.0}) {
		const auto pair = grazing_trace(tr, 340 + y_offset);
		REQUIRE(pair);
		const double d = r - y_offset;
		const double expected = r * r / (2.0 * d);
		for (const TraceStep &s : pair->physical.steps) {
			if (s.frame_id != IDENTITY_FRAME && s.is_arc_step) {
				const bool is_arc = arc::is_arc(s.start, s.via, s.end);
				CHECK_MESSAGE(is_arc, "y_offset=" << y_offset << ": a valid arc");
				if (is_arc) {
					const arc::ArcParams p = arc::arc_params(s.start, s.via, s.end);
					CHECK_MESSAGE(std::fabs(p.radius - expected) <= 1.0, "y_offset=" << y_offset << ": radius " << p.radius << " vs " << expected);
				}
				break;
			}
		}
	}
}

// ---------------------------------------------------------------- test_stage80_beam_geometry

namespace {
template <Num N>
void line_hits_closed_form(const N &like, const char *tier) {
	INFO(tier);
	const Seg<N> vline = line_seg(like, 7, -50, 7, 50);
	const Seg<N> hline = line_seg(like, -50, 9, 50, 9);
	const Beam<N> b1 = Beam<N>::make(cnl(like, 0, 0), cnl(like, 1, 0), like.like(0));
	const Beam<N> b2 = Beam<N>::make(cnl(like, 0, 3), cnl(like, -1, 0), like.like(0));
	const Beam<N> b3 = Beam<N>::make(cnl(like, 4, 1), cnl(like, 0, 1), like.like(0));
	CHECK(sorted_floats(geom::hits(b1, vline, false)) == std::vector<double>{7.0});
	CHECK(sorted_floats(geom::hits(b2, vline, false)) == std::vector<double>{-7.0});
	CHECK(sorted_floats(geom::hits(b3, hline, false)) == std::vector<double>{8.0});
	CHECK(geom::hits(b1, hline, false).empty());
	const Seg<N> obl = line_seg(like, 10, -100, 10, 100);
	const Beam<N> b4 = Beam<N>::make(cnl(like, 1, 2), cnl(like, 0.6, 0.8), like.like(0));
	const std::vector<double> r = sorted_floats(geom::hits(b4, obl, false));
	REQUIRE(r.size() == 1);
	CHECK(std::fabs(r[0] - 15.0) <= 1e-13);
}

template <Num N>
Seg<N> fixture_seg(const N &like, const ojson &sj) {
	SegDef d;
	d.id = sj["id"].get<int>();
	d.sx = sj["start"][0].get<double>(); d.sy = sj["start"][1].get<double>();
	d.ex = sj["end"][0].get<double>(); d.ey = sj["end"][1].get<double>();
	if (sj.contains("via") && !sj["via"].is_null()) {
		d.has_v = true;
		d.vx = sj["via"][0].get<double>(); d.vy = sj["via"][1].get<double>();
	}
	const std::string kind = sj["kind"].get<std::string>();
	if (kind == "line") {
		d.kind = SegKind::Line;
	} else if (kind == "arc") {
		d.kind = SegKind::Circle;
	} else {
		d.kind = SegKind::Circle;
		d.full = true;
		d.cx = sj["center"][0].get<double>(); d.cy = sj["center"][1].get<double>();
		d.r = sj["radius"].get<double>();
	}
	return geom::seg_from_def(d, like);
}

template <Num N>
int arc_beam_vs_line(const N &like, const char *tier, double tol, const ojson &fx) {
	int checked = 0;
	for (const ojson &cs : fx["cases"]) {
		const ojson &bj = fx["beams"][cs["beam"].get<size_t>()];
		const Beam<N> b = Beam<N>::make(cnl(like, bj["p"][0].get<double>(), bj["p"][1].get<double>()),
			cnl(like, bj["tau"][0].get<double>(), bj["tau"][1].get<double>()), like.like(bj["kappa"].get<double>()));
		const ojson *sjp = nullptr;
		for (const ojson &sj : fx["segments"]) {
			if (sj["id"].get<int>() == cs["seg"].get<int>()) {
				sjp = &sj;
			}
		}
		REQUIRE(sjp != nullptr);
		const Seg<N> sg = fixture_seg(like, *sjp);
		const std::vector<HitRec<N>> recs = hit_records(b, sg, false);
		const ojson &exp = cs["hits"];
		const std::string label = std::string(tier) + " beam " + std::to_string(cs["beam"].get<int>()) + " seg " + std::to_string(cs["seg"].get<int>());
		CHECK_MESSAGE(recs.size() == exp.size(), label << ": root count");
		if (recs.size() != exp.size()) {
			continue;
		}
		// match by point (root order is not part of the contract)
		for (const ojson &ex : exp) {
			const double ex_x = std::stod(ex["x"].get<std::string>());
			const double ex_y = std::stod(ex["y"].get<std::string>());
			const HitRec<N> *best = nullptr;
			double best_d = INF_D;
			for (const HitRec<N> &rr : recs) {
				const double d = std::hypot(rr.point.fx() - ex_x, rr.point.fy() - ex_y);
				if (d < best_d) {
					best_d = d;
					best = &rr;
				}
			}
			CHECK_MESSAGE(best_d < tol, label << ": root point off by " << best_d);
			CHECK_MESSAGE(best->on_segment == ex["on_segment"].get<bool>(), label << ": on-segment");
			CHECK_MESSAGE(geom::side_left(sg, best->point, b.tangent_at(best->t)) == (ex["side"].get<std::string>() == "LEFT"), label << ": side");
			checked++;
		}
	}
	return checked;
}

template <Num N>
void tangent_double_root(const N &like, const char *tier) {
	INFO(tier);
	// circle beam of radius 8 centered (0, 8) through the origin, heading +x; x = 8 touches it at (8, 8)
	const Beam<N> b = Beam<N>::make(cnl(like, 0, 0), cnl(like, 1, 0), like.like(0.125));
	const std::vector<HitRec<N>> recs = hit_records(b, line_seg(like, 8, -50, 8, 50), false);
	CHECK(!recs.empty());
	for (const HitRec<N> &rr : recs) {
		CHECK(near2(rr.point.fx(), rr.point.fy(), 8, 8, 1e-12));
	}
}

template <Num N>
void on_segment_endpoints(const N &like, const char *tier) {
	INFO(tier);
	const Seg<N> sg = line_seg(like, 0, 0, 10, 0);
	const CN<N> E = cnl(like, 10, 0);
	CHECK(geom::on_segment(sg, E));
	CHECK(geom::endpoint_at(sg, E) == 2);
	const CN<N> outside = cnl(like, 10.000000000000002, 0); // 1 ulp beyond the end
	CHECK(!geom::on_segment(sg, outside));
	CHECK(geom::endpoint_at(sg, outside) == 0);
	// provenance: a beam constructed through the endpoint snaps to it
	const Beam<N> b = Beam<N>::make(cnl(like, -5, 0), cnl(like, 1, 0), like.like(0));
	const std::vector<HitRec<N>> recs = hit_records(b, line_seg(like, 10, -3, 10, 0), false, {{E, like.like(15)}});
	REQUIRE(recs.size() == 1);
	CHECK(recs[0].at_endpoint == 2);
	CHECK(recs[0].on_segment);
	CHECK(recs[0].point.fx() == 10.0);
	CHECK(recs[0].point.fy() == 0.0);
}
} // namespace

TEST_CASE("ported: test_stage80_beam_geometry: test_line_hits_closed_form") {
	line_hits_closed_form(F64{1}, "f64");
	line_hits_closed_form(BigFloat::from_int(1, 4), "big4");
}

TEST_CASE("ported: test_stage80_beam_geometry: test_arc_beam_vs_line") {
	const ojson fx = load_json("bench/fixtures/stage80_geometry.json");
	int checked = arc_beam_vs_line(F64{1}, "f64", 1e-6, fx);
	checked += arc_beam_vs_line(BigFloat::from_int(1, 4), "big4", 1e-9, fx);
	CHECK(checked > 300);
}

TEST_CASE("ported: test_stage80_beam_geometry: test_tangent_line_double_root") {
	tangent_double_root(F64{1}, "f64");
	tangent_double_root(BigFloat::from_int(1, 4), "big4");
}

TEST_CASE("ported: test_stage80_beam_geometry: test_on_segment_exact_at_endpoints") {
	on_segment_endpoints(F64{1}, "f64");
	on_segment_endpoints(BigFloat::from_int(1, 4), "big4");
	on_segment_endpoints(BigFloat::from_int(1, 8), "big8");
}

// ---------------------------------------------------------------- test_stage81_bigfloat_exact

TEST_CASE("ported: test_stage81_bigfloat_exact: test_exact_quotients") {
	const double cases[][3] = {{80000.0, 200.0, 400.0}, {1.0, 4.0, 0.25}, {-3.0, 8.0, -0.375}, {1e10, -1e5, -1e5},
		{7.0, 7.0, 1.0}, {0.75, 0.25, 3.0}, {400.0, 1.0, 400.0}};
	for (int n : {2, 4, 16}) {
		for (const auto &c : cases) {
			const BigFloat q = BigFloat::from_float(c[0], n).div(BigFloat::from_float(c[1], n));
			CHECK_MESSAGE(q.cmp(BigFloat::from_float(c[2], n)) == 0, n << " limbs: " << c[0] << " / " << c[1] << " = " << c[2] << " (got " << q.to_float() << ")");
		}
	}
}

TEST_CASE("ported: test_stage81_bigfloat_exact: test_exact_square_roots") {
	const double cases[][2] = {{1.0, 1.0}, {3600.0, 60.0}, {2.25, 1.5}, {0.0625, 0.25}, {4.0, 2.0}};
	for (int n : {2, 4, 16}) {
		for (const auto &c : cases) {
			const BigFloat r = BigFloat::from_float(c[0], n).sqrt_val();
			CHECK_MESSAGE(r.cmp(BigFloat::from_float(c[1], n)) == 0, n << " limbs: sqrt(" << c[0] << ") = " << c[1] << " (got " << r.to_float() << ")");
		}
	}
}

TEST_CASE("ported: test_stage81_bigfloat_exact: test_truncated_not_above") {
	// inexact results are truncated toward zero: q b <= a, s^2 <= a
	const int tiers[] = {2, 4, 16};
	std::mt19937_64 rng(81);
	std::uniform_real_distribution<double> u(0.001, 1000.0);
	for (int i = 0; i < 200; i++) {
		const int n = tiers[i % 3];
		const BigFloat a = BigFloat::from_float(u(rng), n);
		const BigFloat b = BigFloat::from_float(u(rng), n);
		const BigFloat q = a.div(b);
		CHECK_MESSAGE(q.with_limbs(n * 2).mul(b.with_limbs(n * 2)).cmp(a.with_limbs(n * 2)) <= 0, "case " << i << ": q b <= a");
		const BigFloat s = a.sqrt_val();
		CHECK_MESSAGE(s.with_limbs(n * 2).mul(s.with_limbs(n * 2)).cmp(a.with_limbs(n * 2)) <= 0, "case " << i << ": s^2 <= a");
		CHECK(std::fabs(q.to_float() - a.to_float() / b.to_float()) <= 1e-12 * std::fabs(q.to_float()));
		CHECK(std::fabs(s.to_float() - std::sqrt(a.to_float())) <= 1e-12 * s.to_float());
	}
}

// ---------------------------------------------------------------- test_stage81_effect_maps

TEST_CASE("ported: test_stage81_effect_maps: test_line_reflection_beam_map") {
	// closed-form reflection of a point across the line s -> e: S + u^2 conj(z - S)
	auto mirror = [](double x, double y, double sx, double sy, double ex, double ey) {
		const double dx = ex - sx, dy = ey - sy;
		const double n = dx * dx + dy * dy;
		const double u2r = (dx * dx - dy * dy) / n;
		const double u2i = (2.0 * dx * dy) / n;
		const double wx = x - sx, wy = -(y - sy);
		return std::array<double, 2>{sx + u2r * wx - u2i * wy, sy + u2r * wy + u2i * wx};
	};
	std::mt19937_64 rng(81);
	auto uni = [&rng](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
	int checked = 0;
	for (int i = 0; i < 200; i++) {
		const double ang = uni(-PI_D, PI_D);
		const double kappa = i % 4 == 0 ? 0.0 : uni(-0.02, 0.02);
		const double px = uni(-300, 300);
		const double py = uni(-300, 300);
		const Beam<F64> b = Beam<F64>::make(cn64(px, py), cn64(std::cos(ang), std::sin(ang)), F64{kappa});
		// a line through a point of the beam, in a random direction (Vector2 ends)
		const double th = uni(5.0, 120.0);
		const CN<F64> h = b.at(F64{th});
		const double la = uni(-PI_D, PI_D);
		const double sx = f32(h.fx() - 50.0 * std::cos(la)), sy = f32(h.fy() - 50.0 * std::sin(la));
		const double ex = f32(h.fx() + 70.0 * std::cos(la)), ey = f32(h.fy() + 70.0 * std::sin(la));
		const Seg<F64> seg = line_seg(F64{1}, sx, sy, ex, ey);
		const std::vector<F64> roots = geom::hits(b, seg, false);
		if (roots.empty()) {
			continue;
		}
		F64 t = roots[0];
		for (const F64 &r : roots) {
			if (std::fabs(r.to_float() - th) < std::fabs(t.to_float() - th)) {
				t = r;
			}
		}
		const Beam<F64> out = effects::reflect_line(b, t, seg);
		// the outgoing beam starts at the hit
		CHECK(std::hypot(out.p.fx() - b.at(t).fx(), out.p.fy() - b.at(t).fy()) < 1e-9);
		// it contains the mirror images of 3 points of the incoming beam, ahead of the departure
		for (double dt : {3.0, 17.0, 41.0}) {
			const CN<F64> z = b.at(t.add(F64{dt}));
			const auto m = mirror(z.fx(), z.fy(), sx, sy, ex, ey);
			const F64 uu = out.param_of(cn64(m[0], m[1]));
			const CN<F64> back = out.at(uu);
			CHECK_MESSAGE(std::hypot(back.fx() - m[0], back.fy() - m[1]) < 1e-6, "beam " << i << ": mirror image (dt=" << dt << ") on the outgoing beam");
			CHECK_MESSAGE(uu.to_float() > 0.0, "beam " << i << ": continuation maps ahead of the departure");
		}
		CHECK_MESSAGE(out.kappa.to_float() == -kappa, "beam " << i << ": kappa' = -kappa exactly");
		checked++;
	}
	CHECK(checked > 150);
}

TEST_CASE("ported: test_stage81_effect_maps: test_arc_reflection_equals_inversion") {
	const Scene<F64> sc = Scene<F64>::from_defs({arc_def(1, 700, 0, 300, 0, 500, -200, EffectType::Reflection)}, F64{1});
	const Surf<F64> &surf = *sc.find(1);
	const Circle<F64> circ = Circle<F64>::make(1, cn64(500, 0), F64{200});
	std::mt19937_64 rng(81);
	auto uni = [&rng](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
	int roots_checked = 0;
	for (int i = 0; i < 50; i++) {
		const double ang = uni(-PI_D, PI_D);
		const double px = uni(-100, 100);
		const double py = uni(-400, 400);
		const double k = i % 2 == 0 ? 0.0 : uni(-0.01, 0.01);
		const Beam<F64> b = Beam<F64>::make(cn64(px, py), cn64(std::cos(ang), std::sin(ang)), F64{k});
		for (const F64 &t : geom::hits(b, surf.seg, false)) {
			const Beam<F64> a = effects::apply(b, t, surf);
			const Beam<F64> x = b.invert_at(t, circ);
			CHECK_MESSAGE((std::vector<double>{a.p.fx(), a.p.fy(), a.tau.fx(), a.tau.fy(), a.kappa.to_float()}
					== std::vector<double>{x.p.fx(), x.p.fy(), x.tau.fx(), x.tau.fy(), x.kappa.to_float()}),
				"beam " << i << ": reflection on an arc = inversion");
			roots_checked++;
		}
	}
	CHECK(roots_checked > 0);
}

// ---------------------------------------------------------------- test_stage82_portals

TEST_CASE("ported: test_stage82_portals: test_portal_beam_map") {
	// the outgoing beam is the rigid image of the incoming one: it contains T(z)
	// for points z of the incoming continuation, ahead of the exit, same curvature
	std::mt19937_64 rng(82);
	auto uni = [&rng](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
	int checked = 0;
	for (int i = 0; i < 100; i++) {
		const double theta = i % 3 == 0 ? 0.0 : uni(-PI_D, PI_D);
		const double dxv = uni(-500, 500);
		const double dyv = uni(-500, 500);
		const double c = std::cos(theta), sn = std::sin(theta);
		auto portal_side = [](double th, double dx, double dy, int pair) {
			SideDef s;
			s.type = EffectType::Portal;
			s.theta = th;
			s.dx = dx;
			s.dy = dy;
			s.pair = pair;
			return s;
		};
		SurfaceDef src;
		src.id = 1;
		src.line = true;
		src.sx = 100; src.sy = -400; src.ex = 100; src.ey = 400;
		src.has_via = true; src.vx = 100; src.vy = 0;
		src.left = portal_side(theta, dxv, dyv, 2);
		src.right = src.left;
		SurfaceDef tgt;
		tgt.id = 2;
		tgt.line = true;
		tgt.sx = c * 100.0 + sn * 400.0 + dxv; tgt.sy = sn * 100.0 - c * 400.0 + dyv;
		tgt.ex = c * 100.0 - sn * 400.0 + dxv; tgt.ey = sn * 100.0 + c * 400.0 + dyv;
		tgt.has_via = true; tgt.vx = c * 100.0 + dxv; tgt.vy = sn * 100.0 + dyv;
		tgt.left = portal_side(-theta, -(c * dxv + sn * dyv), -(c * dyv - sn * dxv), 1);
		tgt.right = tgt.left;
		const Scene<F64> scene = Scene<F64>::from_defs({src, tgt}, F64{1});
		const double ang = uni(-0.5, 0.5);
		const double k = i % 2 == 0 ? 0.0 : uni(-0.004, 0.004);
		const Beam<F64> b = Beam<F64>::make(cn64(0, uni(-100, 100)), cn64(std::cos(ang), std::sin(ang)), F64{k});
		const Surf<F64> &s1 = *scene.find(1);
		const std::vector<F64> roots = geom::hits(b, s1.seg, false);
		if (roots.empty()) {
			continue;
		}
		const F64 t = roots[0];
		const Beam<F64> out = effects::apply(b, t, s1);
		CHECK_MESSAGE(out.kappa.to_float() == k, "beam " << i << ": curvature preserved");
		for (double dt : {5.0, 23.0}) {
			const CN<F64> z = b.at(t.add(F64{dt}));
			const double tzx = c * z.fx() - sn * z.fy() + dxv;
			const double tzy = sn * z.fx() + c * z.fy() + dyv;
			const F64 uu = out.param_of(cn64(tzx, tzy));
			const CN<F64> back = out.at(uu);
			CHECK_MESSAGE(std::hypot(back.fx() - tzx, back.fy() - tzy) < 1e-9, "beam " << i << ": T(z) on the outgoing beam");
			CHECK_MESSAGE(uu.to_float() > 0.0, "beam " << i << ": ahead of the exit point");
		}
		checked++;
	}
	CHECK(checked > 60);
}

// ---------------------------------------------------------------- the stage 81-87 oracle fixtures
// Every fixture of bench/fixtures/stage8{1,2,3,5,6,7}_*.json walked with the
// infinity switch on (the game's, under which the oracle wrote them) at f64,
// Dual, 4 and 64 limbs, event by event against the oracle; plus the extra
// assertions of the GUT tests on named fixtures.

namespace {
const char *const STAGE8X_FILES[] = {"stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags", "stage86_ghost",
	"stage87_projective_plans", "stage87_click_forks"};

// the degenerate inputs (an inversion center) are compared at 0.01 px
bool degenerate_fixture(const std::string &name) {
	return name == "player_at_inversion_center" || name == "player_at_inversion_center_planned" || name == "cursor_at_inversion_center"
		|| name == "cursor_at_inversion_center_no_plan";
}
} // namespace

TEST_CASE("ported: the stage 81-87 fixtures match the oracle") {
	int fixtures = 0;
	for (const char *f : STAGE8X_FILES) {
		const ojson fxs = load_json(std::string("bench/fixtures/") + f + ".json");
		for (const ojson &fx : fxs["fixtures"]) {
			const std::string name = std::string(f) + "/" + fx["name"].get<std::string>();
			const double tol = degenerate_fixture(fx["name"].get<std::string>()) ? 0.01 : 1e-9;
			const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
			const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
			CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"], tol) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"], tol)));
			const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
			CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"], tol) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"], tol)));
			const auto r4 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
			CHECK_MESSAGE(diff_against_oracle(r4, fx["expected"], tol) == "", (name + " big4: " + diff_against_oracle(r4, fx["expected"], tol)));
			const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64), true);
			CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"], tol) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"], tol)));
			fixtures++;
		}
	}
	CHECK(fixtures == 115);

	const ojson rules = load_json("bench/fixtures/stage81_rules.json");
	auto rule = [&rules](const std::string &n) -> const ojson & {
		for (const ojson &fx : rules["fixtures"]) {
			if (fx["name"].get<std::string>() == n) {
				return fx;
			}
		}
		FAIL("no fixture " << n);
		return rules;
	};
	auto walk64 = [](const ojson &fx) { return walk_oracle_fixture<F64>(fx, defs_from_json(fx["surfaces"]), F64{1}, true); };
	{
		INFO("test_terminal_ends_trace_at_hit: the oracle's block hit is the last event");
		CHECK(rule("terminal_ends_trace")["expected"].back()["terminal"].get<bool>());
	}
	{
		INFO("test_max_hits_32_counts_all_events: 32 hits, then the MAX_HITS stop");
		const auto ev = walk64(rule("max_hits_parallel_mirrors"));
		CHECK(std::count_if(ev.begin(), ev.end(), [](const Event<F64> &e) { return e.kind == EventKind::Hit; }) == 32);
		REQUIRE(!ev.empty());
		CHECK(ev.back().kind == EventKind::Max);
	}
	{
		INFO("test_departure_rehit_is_ordinary_hit: the second event (infinity aside) is the departure re-hit");
		std::vector<Event<F64>> ev;
		for (const Event<F64> &e : walk64(rule("departure_rehit_straight"))) {
			if (e.kind != EventKind::Infinity) {
				ev.push_back(e);
			}
		}
		if (ev.size() > 1) {
			CHECK(ev[1].rehit);
		}
	}
	{
		INFO("test_targets_recorded_on_segment: the on-segment target hits");
		const ojson &fx = rule("target_recorded_on_segment");
		Scene<F64> scene = Scene<F64>::from_defs(defs_from_json(fx["surfaces"]), F64{1});
		scene.emit_infinity = true;
		Solver<F64> solver(scene);
		const Walk<F64> w = solver.run_direction(cn64(fx["player"][0].get<double>(), fx["player"][1].get<double>()),
			cn64(fx["direction"][0].get<double>(), fx["direction"][1].get<double>()), {}, std::nullopt, Mode::Physical, MAX_HITS, Flags());
		CHECK(solver.targets_of(w.events) == fx["targets"].get<std::vector<int>>());
	}
	{
		INFO("test_planned_applies_projective: the planned projective entry is applied");
		const ojson proj = load_json("bench/fixtures/stage83_projective.json");
		bool found = false;
		for (const ojson &fx : proj["fixtures"]) {
			if (fx["name"].get<std::string>() != "planned_passes_projective") {
				continue;
			}
			found = true;
			const auto ev = walk64(fx);
			CHECK(std::any_of(ev.begin(), ev.end(),
				[](const Event<F64> &e) { return e.kind == EventKind::Hit && e.surface_id == 5 && e.applied && e.projective; }));
		}
		CHECK(found);
	}
}

// ---------------------------------------------------------------- test_stage83_projective

TEST_CASE("ported: test_stage83_projective: test_degenerate_inputs_native") {
	const ojson fxs = load_json("bench/fixtures/stage83_projective.json");
	int n = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		if (!degenerate_fixture(name)) {
			continue;
		}
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"], 0.01) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"], 0.01)));
		const auto r4 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(r4, fx["expected"], 0.01) == "", (name + " big4: " + diff_against_oracle(r4, fx["expected"], 0.01)));
		n++;
	}
	CHECK(n == 4);
	// the game path on the inversion grid: player at an inversion center, the beam solver, no fallback
	Tracer tr;
	tr.set_emit_infinity(true);
	tr.set_defs(scene_dump("playground__inversion_grid"));
	CHECK(tr.trace_pair(V2::of(760, 380), V2::of(1000, 700), {PlanItem{1, false, std::nullopt, std::nullopt}}, Flags()).has_value());
}

// ---------------------------------------------------------------- test_stage87_click_forks

TEST_CASE("ported: test_stage87_click_forks: test_hits_match_oracle") {
	const ojson fxs = load_json("bench/fixtures/stage87_click_forks.json");
	int n = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		Scene<F64> scene = Scene<F64>::from_defs(defs_from_json(fx["surfaces"]), F64{1});
		scene.emit_infinity = true;
		Solver<F64> solver(scene);
		const auto chain = solver.anchor_chain(cn64(fx["player"][0].get<double>(), fx["player"][1].get<double>()),
			cn64(fx["cursor"][0].get<double>(), fx["cursor"][1].get<double>()), plan_from_json(fx["plan"]), Flags());
		CHECK_MESSAGE(chain != nullptr, name);
		if (!chain) {
			continue;
		}
		CHECK_MESSAGE(chain->hits.size() == fx["hits"].size(), name << ": projective hit count");
		for (auto it = fx["hits"].begin(); it != fx["hits"].end(); ++it) {
			const double wx = std::stod(it.value()[0].get<std::string>());
			const double wy = std::stod(it.value()[1].get<std::string>());
			const auto h = chain->hits.find(std::stoi(it.key()));
			CHECK_MESSAGE(h != chain->hits.end(), name << ": hit " << it.key());
			if (h != chain->hits.end()) {
				CHECK_MESSAGE(near2(h->second.fx(), h->second.fy(), wx, wy, 1e-9), name << ": hit " << it.key());
			}
		}
		n++;
	}
	CHECK(n == int(fxs["fixtures"].size()));
}

TEST_CASE("ported: test_stage87_click_forks: test_property_nearest_candidate") {
	SurfaceDef d = circle_def(1, 500, 0, 200, EffectType::CircleNormalProjection);
	d.sx = 700; d.sy = 0; d.ex = 400; d.ey = -150; d.vx = 400; d.vy = 150;
	d.left->back = false;
	d.right->back = true;
	const Scene<F64> sc = Scene<F64>::from_defs({d}, F64{1});
	const Surf<F64> &surf = *sc.find(1);
	std::mt19937_64 rng(87);
	auto uni = [&rng](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
	for (int i = 0; i < 300; i++) {
		const float Xx = float(uni(100, 900)), Xy = float(uni(-400, 400));
		if (dist32(Xx, Xy, 500, 0) < 1.0f) {
			continue;
		}
		const effects::F32Point click{float(uni(250, 750)), float(uni(-250, 250))};
		const bool left = uni(0, 1) < 0.5;
		const CN<F64> X = cn64(Xx, Xy);
		const auto cands = effects::back_propagate_candidates(surf, left, X);
		CHECK_MESSAGE(cands.size() == 2, "two candidates on the line through the center and X");
		const auto H = effects::back_propagate(surf, left, X, false, &click);
		REQUIRE(H);
		const double hx = H->fx() - 500.0, hy = H->fy();
		CHECK(std::fabs(std::sqrt(hx * hx + hy * hy) - 200.0) <= 1e-9);
		CHECK(std::fabs(hx * Xy - hy * (Xx - 500.0)) <= 1e-6 * double(dist32(Xx, Xy, 500, 0)) * 200.0);
		const float hvx = float(H->fx()), hvy = float(H->fy());
		for (const auto &k : cands) {
			const float ox = float(k.h.fx()), oy = float(k.h.fy());
			CHECK_MESSAGE(double(dist32(hvx, hvy, click.x, click.y)) <= double(dist32(ox, oy, click.x, click.y)) + 1e-9, "nearest the click");
		}
		// no click: nearest the midpoint (via)
		const auto Hd = effects::back_propagate(surf, left, X);
		REQUIRE(Hd);
		const float hdx = float(Hd->fx()), hdy = float(Hd->fy());
		for (const auto &k : cands) {
			const float ox = float(k.h.fx()), oy = float(k.h.fy());
			CHECK_MESSAGE(double(dist32(hdx, hdy, 400, 150)) <= double(dist32(ox, oy, 400, 150)) + 1e-9, "default: nearest the midpoint");
		}
	}
	// provenance (Stage 114): X on the carrier is its own candidate, exactly; the
	// click picks it or the antipode (a full circle: both on the segment)
	for (int i = 0; i < 50; i++) {
		const double a = uni(0, 2.0 * PI_D);
		const float Xx = float(500.0f + float(std::cos(a)) * 200.0f), Xy = float(0.0f + float(std::sin(a)) * 200.0f);
		const effects::F32Point at_x{Xx, Xy};
		const auto H = effects::back_propagate(surf, true, cn64(Xx, Xy), true, &at_x);
		REQUIRE(H);
		CHECK_MESSAGE((float(H->fx()) == Xx && float(H->fy()) == Xy), "clicked at X: X itself");
		const effects::F32Point anti{500.0f - (Xx - 500.0f), 0.0f - (Xy - 0.0f)};
		const auto Ho = effects::back_propagate(surf, true, cn64(Xx, Xy), true, &anti);
		REQUIRE(Ho);
		CHECK_MESSAGE(dist32(float(Ho->fx()), float(Ho->fy()), Xx, Xy) > 300.0f, "clicked at the antipode: the antipode");
	}
}

// ---------------------------------------------------------------- test_stage87_mixed_planning

namespace {
SurfaceDef projective_line(EffectType t, bool has_normal, double nx, double ny) {
	SurfaceDef d = line_def(1, 300, -400, 300, 400, t, true);
	d.vx = 300; d.vy = 0;
	d.left->back = false;
	d.left->has_normal = has_normal;
	d.left->nx = nx;
	d.left->ny = ny;
	d.right = d.left;
	return d;
}
} // namespace

TEST_CASE("ported: test_stage87_mixed_planning: test_back_propagation_per_type") {
	// line normal: the foot of X on the line x = 300
	const Scene<F64> ln = Scene<F64>::from_defs({projective_line(EffectType::LineNormalProjection, false, 0, 0)}, F64{1});
	const auto H = effects::back_propagate(*ln.find(1), false, cn64(600, 200));
	REQUIRE(H);
	CHECK(H->fx() == 300.0);
	CHECK(H->fy() == 200.0);
	// circle normal: c +- R u, u = (0.6, 0.8): (620, 160), (380, -160)
	SurfaceDef cd = circle_def(1, 500, 0, 200, EffectType::CircleNormalProjection);
	cd.sx = 700; cd.sy = 0; cd.ex = 400; cd.ey = -173.20508075688772; cd.vx = 400; cd.vy = 173.20508075688772;
	const Scene<F64> cn = Scene<F64>::from_defs({cd}, F64{1});
	const Surf<F64> &s1 = *cn.find(1);
	const CN<F64> X = cn64(800, 400);
	const bool lft = geom::side_left(s1.seg, cn64(300, 0), cn64(1, 0));
	const effects::F32Point ca{375, -150};
	const auto Ha = effects::back_propagate(s1, lft, X, false, &ca);
	REQUIRE(Ha);
	CHECK(near2(Ha->fx(), Ha->fy(), 380, -160, 1e-9));
	const effects::F32Point cb{630, 150};
	const auto Hb = effects::back_propagate(s1, lft, X, false, &cb);
	REQUIRE(Hb);
	CHECK(near2(Hb->fx(), Hb->fy(), 620, 160, 1e-9));
	// no click: the segment's midpoint (via (400, 173.2)) stands in
	const auto Hm = effects::back_propagate(s1, lft, X);
	REQUIRE(Hm);
	CHECK(near2(Hm->fx(), Hm->fy(), 620, 160, 1e-9));
	// directional on a line: the line through X along the normal
	const Scene<F64> ld = Scene<F64>::from_defs({projective_line(EffectType::LineDirectionalProjection, true, 1, 1)}, F64{1});
	const auto Hd = effects::back_propagate(*ld.find(1), false, cn64(600, 250));
	REQUIRE(Hd);
	CHECK(Hd->fx() == 300.0);
	CHECK(Hd->fy() == -50.0);
	// a directional normal parallel to the line: no hit point
	const Scene<F64> lp = Scene<F64>::from_defs({projective_line(EffectType::LineDirectionalProjection, true, 0, 1)}, F64{1});
	CHECK(!effects::back_propagate(*lp.find(1), false, cn64(600, 250)));
}

TEST_CASE("ported: test_stage87_mixed_planning: test_planned_exit_beam_exactly_straight") {
	Tracer tr;
	tr.set_emit_infinity(true);
	tr.set_defs(scene_dump("playground__projective"));
	const std::vector<PlanItem> plan{PlanItem{6, true, std::nullopt, std::nullopt}, PlanItem{7, true, std::nullopt, std::nullopt}};
	const auto pair = tr.trace_pair(V2::of(1243.33337402344, 390.0), V2::of(1810.0, 90.0), plan, Flags());
	REQUIRE_MESSAGE(pair, "a trace pair (finite events)");
	CHECK_MESSAGE(pair->result.planned.limbs == 0, "decided at float64, no climb");
	const F64Events &ev = pair->result.planned.f64_events;
	for (size_t i = 1; i < ev.size(); i++) {
		if (ev[i - 1].kind == EventKind::Hit && ev[i - 1].applied && ev[i - 1].projective) {
			REQUIRE(ev[i].beam);
			CHECK_MESSAGE(ev[i].beam->kappa.is_zero(), "the exit beam is exactly straight");
		}
	}
}

TEST_CASE("ported: test_stage87_mixed_planning: test_planned_pass_shared_when_physical_reaches_cursor_anchored") {
	const ojson fxs = load_json("bench/fixtures/stage87_projective_plans.json");
	int shared = 0;
	int separate = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		if (!fx["direction"].is_null() || fx["cursor"].is_null()) {
			continue;
		}
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
		const Flags flags = fx.contains("flags") && !fx["flags"].is_null() ? flags_from_json(fx["flags"]) : Flags();
		const double px = fx["player"][0].get<double>(), py = fx["player"][1].get<double>();
		const double cx = fx["cursor"][0].get<double>(), cy = fx["cursor"][1].get<double>();
		LadderContext ctx(defs);
		ctx.set_emit_infinity(true);
		Ladder ladder(ctx);
		const LadderResult r = ladder.solve(px, py, cx, cy, plan, MAX_HITS, MAX_HITS, 0.01, false, flags);
		// the physical cursor event: reached anchored?
		bool cursor_found = false;
		bool cursor_anchored = false;
		auto find_cursor = [&](const auto &events) {
			for (const auto &e : events) {
				if (e.kind == EventKind::Cursor) {
					cursor_found = true;
					cursor_anchored = e.anchored;
					return;
				}
			}
		};
		if (r.physical.limbs == 0) {
			find_cursor(r.physical.f64_events);
		} else {
			find_cursor(r.physical.big_events);
		}
		if (r.planned_from_physical) {
			shared++;
			CHECK_MESSAGE((cursor_found && cursor_anchored), name << ": shared only when the cursor is reached anchored");
		} else {
			separate++;
		}
		if (r.planned.limbs != 0) {
			continue;
		}
		Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1});
		scene.emit_infinity = true;
		Solver<F64> solver(scene);
		const std::optional<Walk<F64>> w = solver.run(cn64(px, py), cn64(cx, cy), plan, Mode::Planned, MAX_HITS, flags);
		const std::vector<Event<F64>> own = w ? w->events : std::vector<Event<F64>>{};
		const F64Events &got = r.planned.f64_events;
		CHECK_MESSAGE(got.size() == own.size(), name << ": planned event count");
		for (size_t i = 0; i < std::min(got.size(), own.size()); i++) {
			const auto &a = got[i];
			const auto &b = own[i];
			CHECK_MESSAGE((a.kind == b.kind && a.surface_id == b.surface_id && a.left == b.left && a.applied == b.applied && a.state == b.state),
				name << " event " << i);
			if (a.point && b.point) {
				CHECK_MESSAGE((float(a.point->fx()) == float(b.point->fx()) && float(a.point->fy()) == float(b.point->fy())), name << " event " << i << " point");
			}
		}
	}
	CHECK_MESSAGE(shared > 0, "realizable plans share the pass");
	CHECK_MESSAGE(separate > 0, "diverging plans compute it");
}

// ---------------------------------------------------------------- test_stage106_pop_departure

TEST_CASE("ported: test_stage106_pop_departure: test_no_fixture_re_applies_in_place") {
	const ojson fxs = load_json("bench/fixtures/stage106_pop_departure.json");
	int n = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const auto events = walk_oracle_fixture<F64>(fx, defs_from_json(fx["surfaces"]), F64{1}, true);
		std::vector<inv::EvInfo> infos;
		for (const Event<F64> &e : events) {
			infos.push_back(inv::ev_info(e));
		}
		const inv::Violations v = inv::check_reapply_in_place(infos);
		CHECK_MESSAGE(v.empty(), fx["name"].get<std::string>());
		n++;
	}
	CHECK(n == 11);
}

// ---------------------------------------------------------------- test_stage111_orientation

TEST_CASE("ported: test_stage111_orientation: test_region_covers_both_sides_of_the_image_and_excludes_the_parallel_aims") {
	const ojson fxs = load_json("bench/fixtures/stage111_orientation.json");
	std::vector<SurfaceDef> defs;
	for (const ojson &fx : fxs["fixtures"]) {
		if (fx["name"].get<std::string>().rfind("mw_", 0) == 0) {
			defs = defs_from_json(fx["surfaces"]);
			break;
		}
	}
	REQUIRE(defs.size() == 9);
	const vis::Pt player{f32(1306.67), 540.0};
	const vis::Rect bounds{0, 0, 1920, 1080};
	auto at = [](double x, double y) { return vis::Pt{f32(x), f32(y)}; };
	const PlanItem mirror{5, true, std::nullopt, std::nullopt};
	const PlanItem inf{INFINITY_ID, true, std::nullopt, std::nullopt};
	{
		Tracer tr;
		tr.set_emit_infinity(true);
		tr.set_defs(defs);
		const vis::Result region = vis::compute(tr, player, {mirror}, Flags(), bounds);
		CHECK(region.supported);
		CHECK_MESSAGE(vis::contains(region, at(480.5005, 524.9722)) == vis::Membership::Inside, "in front of the image");
		CHECK_MESSAGE(vis::contains(region, at(166.1731, 535.9927)) == vis::Membership::Inside, "behind the image");
		CHECK_MESSAGE(vis::contains(region, at(1000, 600)) == vis::Membership::Inside, "the player's side");
		CHECK_MESSAGE(vis::contains(region, at(293.33, 900)) == vis::Membership::Outside, "the aim parallel to the mirror never fires it");
	}
	{
		Tracer tr;
		tr.set_emit_infinity(true);
		tr.set_defs(defs);
		const vis::Result with_inf = vis::compute(tr, player, {mirror, inf}, Flags(), bounds);
		CHECK_MESSAGE(vis::contains(with_inf, at(1000, 600)) == vis::Membership::Outside, "with the entry: the player's side is passed with the wrap pending");
		CHECK_MESSAGE(vis::contains(with_inf, at(166.1731, 535.9927)) == vis::Membership::Inside, "with the entry: behind the image");
	}
}
