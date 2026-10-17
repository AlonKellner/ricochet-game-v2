// Stage 110, F8: rc/earcut.hpp, the triangulation the visibility fill is drawn
// with. The triangles must cover each polygon exactly: their areas add up to
// the polygon's (outer minus holes), and none is inverted.
#include "rc/earcut.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using rc::earcut::Triangulator;
using rc::earcut::XY;

namespace {

double ring_area(const std::vector<XY> &r) {
	double a = 0.0;
	for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
		a += r[j].x * r[i].y - r[i].x * r[j].y;
	}
	return std::abs(a) / 2.0;
}

double triangles_area(const std::vector<std::vector<XY>> &rings, const std::vector<uint32_t> &idx, bool *same_winding) {
	std::vector<XY> pts;
	for (const auto &r : rings) pts.insert(pts.end(), r.begin(), r.end());
	double a = 0.0;
	int pos = 0, neg = 0;
	for (size_t t = 0; t + 2 < idx.size(); t += 3) {
		const XY &p = pts[idx[t]], &q = pts[idx[t + 1]], &s = pts[idx[t + 2]];
		const double cr = (q.x - p.x) * (s.y - p.y) - (q.y - p.y) * (s.x - p.x);
		pos += cr > 0 ? 1 : 0;
		neg += cr < 0 ? 1 : 0;
		a += std::abs(cr) / 2.0;
	}
	*same_winding = pos == 0 || neg == 0;
	return a;
}

void check_exact(const std::vector<std::vector<XY>> &rings, const char *label) {
	Triangulator t;
	const std::vector<uint32_t> idx = t.run(rings);
	double expected = ring_area(rings[0]);
	for (size_t h = 1; h < rings.size(); h++) expected -= ring_area(rings[h]);
	bool same = false;
	const double got = triangles_area(rings, idx, &same);
	CHECK_MESSAGE(idx.size() % 3 == 0, label);
	CHECK_MESSAGE(same, label);
	CHECK_MESSAGE(std::abs(got - expected) <= 1e-9 * std::max(1.0, expected), label);
}

} // namespace

TEST_CASE("earcut: a polygon, a polygon with holes, concave and degenerate rings") {
	check_exact({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, "square");
	check_exact({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{3, 3}, {3, 7}, {7, 7}, {7, 3}}}, "square with a hole");
	check_exact({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{1, 1}, {1, 3}, {3, 3}, {3, 1}}, {{6, 6}, {8, 6}, {8, 8}, {6, 8}}}, "two holes");
	// a comb: deep concavities
	std::vector<XY> comb{{0, 0}, {20, 0}, {20, 10}};
	for (int k = 9; k >= 0; k--) {
		comb.push_back({2.0 * k + 1.5, 10});
		comb.push_back({2.0 * k + 1.0, 2});
		comb.push_back({2.0 * k + 0.5, 10});
	}
	comb.push_back({0, 10});
	check_exact({comb}, "comb");
	// collinear and repeated points (tessellated arcs meet the clip rectangle so)
	check_exact({{{0, 0}, {5, 0}, {5, 0}, {10, 0}, {10, 5}, {10, 10}, {0, 10}, {0, 5}}}, "collinear and repeated points");
	// a hole touching the outer ring at a point
	check_exact({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{0, 5}, {4, 3}, {4, 7}}}, "a hole touching the outer ring");
	// a circle tessellated finely, with a circular hole
	std::vector<XY> outer, hole;
	for (int k = 0; k < 720; k++) {
		const double a = 2 * M_PI * k / 720;
		outer.push_back({400 * std::cos(a), 400 * std::sin(a)});
		hole.push_back({150 + 100 * std::cos(-a), 100 * std::sin(-a)});
	}
	Triangulator t;
	const std::vector<std::vector<XY>> rings{outer, hole};
	const auto idx = t.run(rings);
	bool same = false;
	const double got = triangles_area(rings, idx, &same);
	CHECK(same);
	CHECK(std::abs(got - (ring_area(outer) - ring_area(hole))) <= 1e-6 * ring_area(outer));
}
