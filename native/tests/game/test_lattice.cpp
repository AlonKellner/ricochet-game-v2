// Stage 154 (docs/TDD_59): exact carriers -- lattice points exactly on one line or one circle.
#include "rc/exact.hpp"
#include "rcg/lattice.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>

TEST_CASE("lattice: a circle's points are float32 values exactly on it, dense enough") {
	for (const double r : {40.0, 150.0, 400.0, 900.0}) {
		CAPTURE(r);
		const auto t0 = std::chrono::steady_clock::now();
		const rcg::lattice::Circle &c = rcg::lattice::circle_of(1000.3, 540.7, r);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		MESSAGE("r=" << r << " n=" << c.n << " points=" << c.pts.size() << " radius=" << c.radius() << " h=" << c.h << " " << ms << " ms");
		CHECK(std::fabs(c.radius() - r) <= 0.5);
		double gap = 0;
		for (size_t i = 0; i < c.pts.size(); i++) {
			const auto p = c.point(i);
			CHECK(double(float(p[0])) == p[0]);
			CHECK(double(float(p[1])) == p[1]);
			CHECK(c.pts[i][0] * c.pts[i][0] + c.pts[i][1] * c.pts[i][1] == c.n);
			const double a = c.angles[(i + 1) % c.pts.size()] - c.angles[i];
			gap = std::max(gap, (a < 0 ? a + 2 * 3.141592653589793 : a) * r);
		}
		MESSAGE("  the largest gap: " << gap << " px");
		// any four of them are exactly concyclic
		const auto a = c.point(0), b = c.point(c.pts.size() / 3), d = c.point(2 * c.pts.size() / 3), e = c.point(c.pts.size() / 2);
		CHECK(rc::exact::incircle_zero(a[0], a[1], b[0], b[1], d[0], d[1], e[0], e[1]));
	}
}

TEST_CASE("lattice: a line's points are float32 values exactly on it") {
	const rcg::lattice::Line l = rcg::lattice::line_of(600.2, 300.7, 1311.9, 777.3);
	const auto a = rcg::lattice::nearest(l, 600, 300), b = rcg::lattice::nearest(l, 900, 500), c = rcg::lattice::nearest(l, 1300, 770);
	for (const auto &p : {a, b, c}) {
		CHECK(double(float(p[0])) == p[0]);
		CHECK(double(float(p[1])) == p[1]);
	}
	CHECK(rc::exact::orient_zero(a[0], a[1], c[0], c[1], b[0], b[1]));
	CHECK(std::hypot(b[0] - 900, b[1] - 500) < 2.0); // (near its projection: the line barely turned)
}

#include "rcg/build.hpp"
#include "rcg/level.hpp"

#include <nlohmann/json.hpp>

namespace {

rcg::Level exact_level(const std::string &elements, bool exact = true) {
	nlohmann::json lv = nlohmann::json::parse(R"({"schema": "rc-level/2", "player": {"start": [660, 540]}, "room": {"rect": [560, 240, 800, 600], "built": true},
		"carriers": [{"name": "ring", "circle": {"center": [1000.3, 520.1], "radius": 150}},
		             {"name": "rail", "line": {"a": [600.2, 300.7], "b": [1311.9, 777.3]}}]})");
	for (auto &c : lv["carriers"]) {
		c["exact"] = exact;
	}
	lv["elements"] = nlohmann::json::parse(elements);
	return rcg::parse_level_json(lv);
}

const char *RING = R"([
	{"kind": "wall", "name": "a", "carrier": "ring", "from": [860, 570], "to": [950, 380]},
	{"kind": "mirror", "name": "b", "carrier": "ring", "from": [950, 380], "to": [1140, 570]},
	{"kind": "inversion", "name": "c", "carrier": "ring", "from": [1140, 570], "to": [950, 660]},
	{"kind": "door", "name": "d", "carrier": "ring", "from": [950, 660], "to": [860, 570]},
	{"kind": "wall", "name": "w1", "carrier": "rail", "from": [600, 300], "to": [900, 500]},
	{"kind": "wall", "name": "w2", "carrier": "rail", "from": [900, 500], "to": [1300, 770]}])";

} // namespace

TEST_CASE("lattice: elements on an exact carrier lie exactly on one circle or line, their shared points one value") {
	const rcg::Level lv = exact_level(RING);
	const std::vector<rcg::BuiltSurface> built = rcg::build_level(lv);
	auto by_label_order = [&](size_t k) { return built[4 + k].seg; }; // (the room 1-4)
	std::vector<rc::arc::SegGeom> arcs;
	for (size_t k = 0; k < 4; k++) {
		arcs.push_back(by_label_order(k));
	}
	// every arc's points on the first arc's circle, exactly
	const rc::arc::SegGeom &f = arcs[0];
	for (const rc::arc::SegGeom &g : arcs) {
		for (const rc::V2 p : {g.s, g.v, g.e}) {
			CHECK(rc::exact::incircle_zero(f.s.x, f.s.y, f.v.x, f.v.y, f.e.x, f.e.y, p.x, p.y));
		}
	}
	for (size_t k = 0; k < 4; k++) {
		CHECK(arcs[k].e == arcs[(k + 1) % 4].s); // the ring closes on shared points
	}
	const rc::arc::SegGeom w1 = by_label_order(4), w2 = by_label_order(5);
	CHECK(w1.e == w2.s);
	CHECK(rc::exact::orient_zero(w1.s.x, w1.s.y, w1.e.x, w1.e.y, w2.e.x, w2.e.y));
	// without exact: the plain projection (the ends rounded off the carrier: not one circle)
	const std::vector<rcg::BuiltSurface> plain = rcg::build_level(exact_level(RING, false));
	bool all = true;
	const rc::arc::SegGeom &pf = plain[4].seg;
	for (size_t k = 1; k < 4; k++) {
		const rc::arc::SegGeom &g = plain[4 + k].seg;
		for (const rc::V2 p : {g.s, g.v, g.e}) {
			all = all && rc::exact::incircle_zero(pf.s.x, pf.s.y, pf.v.x, pf.v.y, pf.e.x, pf.e.y, p.x, p.y);
		}
	}
	CHECK_FALSE(all);
}

TEST_CASE("lattice: an exact carrier's errors") {
	auto err = [](const std::string &elements) {
		try {
			exact_level(elements);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"([{"kind": "wall", "carrier": "ring"}])").find("holds arcs") != std::string::npos);
	CHECK(err(R"([{"kind": "wall", "carrier": "ring", "from": [1150, 520], "to": [1150, 520.5]}])").find("no error") == std::string::npos);
	nlohmann::json bad = nlohmann::json::parse(R"({"schema": "rc-level/2", "elements": [], "carriers": [{"name": "c", "line": {"a": [0, 0], "b": [1, 1]}, "exact": 1}]})");
	CHECK_THROWS_WITH_AS(rcg::parse_level_json(bad), doctest::Contains("true or false"), rcg::LevelError);
}

#include "rcg/session.hpp"

#include <filesystem>

TEST_CASE("lattice: the demo level -- a ring on an exact circle; its hatch opened, the button inside") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data/levels/objectives/ring.json").string()));
	auto shoot = [&](double x, double y) {
		s.set_cursor(rc::V2::of(x, y));
		s.settle();
		s.fire(-1, 1.0 / 60.0);
	};
	s.hold({}, 30);
	shoot(1000, 520);
	CHECK_FALSE(s.flags().has("door:gate")); // the hatch is shut
	shoot(810, 300);
	CHECK(s.flags().is_true("door:hatch"));
	shoot(1000, 520);
	CHECK(s.flags().is_true("door:gate"));
	s.hold({"move_right"}, 180);
	CHECK(s.won());
}
