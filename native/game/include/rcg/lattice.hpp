// Stage 154 (GAME_SPEC 19.1, docs/TDD_59): exact carriers. A float32 point lies
// exactly on a declared line or circle only by luck, so the elements on one
// carrier are, to the solver's exact predicates, on slightly different ones. An
// exact carrier takes points that lie on it exactly instead: a line through a
// grid point in a small-integer direction (its points A + k h (q, p)); a circle
// about a grid point whose radius^2 / h^2 is an integer N with many ways of
// being a sum of two squares X^2 + Y^2 (its points C + h (X, Y)). Every such
// point is a float32 value, so the elements' points are exactly collinear or
// exactly concyclic, and a point two elements share is one value.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rcg::lattice {

// The finest grid a float32 holds exactly about these magnitudes (a power of two).
double grid_for(double magnitude);

struct Line {
	double ax = 0, ay = 0; // a grid point of it
	double h = 0;          // the grid
	int64_t q = 1, p = 0;  // its direction (primitive, |q|, |p| <= 1024)
};
Line line_of(double ax, double ay, double bx, double by);
// the lattice point nearest the projection of (x, y)
std::array<double, 2> nearest(const Line &l, double x, double y);

struct Circle {
	double cx = 0, cy = 0, h = 0;
	int64_t n = 0;                            // radius^2 / h^2
	std::vector<std::array<int64_t, 2>> pts;  // every (X, Y), X^2 + Y^2 = n, by angle
	std::vector<double> angles;               // ... their angles
	double radius() const;
	std::array<double, 2> point(size_t i) const;
};
// the circle near (center, radius): its center on the grid, its radius within
// half a pixel, the n with the most points (computed once per circle)
const Circle &circle_of(double cx, double cy, double r);
// the index of the lattice point nearest in direction to (x, y)
size_t nearest(const Circle &c, double x, double y);
// ... nearest to the angle a
size_t nearest_angle(const Circle &c, double a);

} // namespace rcg::lattice
