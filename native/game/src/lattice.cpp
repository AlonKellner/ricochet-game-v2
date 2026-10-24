// Stage 154: exact carriers (rcg/lattice.hpp).
#include "rcg/lattice.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <tuple>

namespace rcg::lattice {

namespace {

constexpr double PI = 3.14159265358979323846;

int64_t isqrt(int64_t n) {
	int64_t r = int64_t(std::sqrt(double(n)));
	while (r * r > n) {
		r--;
	}
	while ((r + 1) * (r + 1) <= n) {
		r++;
	}
	return r;
}

} // namespace

double grid_for(double magnitude) {
	int e = 0;
	std::frexp(std::max(magnitude, 1.0), &e); // magnitude < 2^e
	return std::ldexp(1.0, e - 24);
}

Line line_of(double ax, double ay, double bx, double by) {
	Line l;
	l.h = grid_for(std::max({std::fabs(ax), std::fabs(ay), std::fabs(bx), std::fabs(by)}) + 4096); // (room to run along it)
	l.ax = std::round(ax / l.h) * l.h;
	l.ay = std::round(ay / l.h) * l.h;
	const double dx = bx - ax, dy = by - ay;
	// the primitive (q, p) nearest the direction in angle, |q|, |p| <= 1024 (the smaller first on a tie)
	const double want = std::atan2(dy, dx);
	double best = INFINITY;
	const bool steep = std::fabs(dy) > std::fabs(dx);
	for (int64_t a = 1; a <= 1024; a++) {
		const double other = steep ? double(a) * dx / std::fabs(dy) : double(a) * dy / std::fabs(dx);
		const int64_t b = int64_t(std::llround(other));
		if (std::llabs(b) > 1024) {
			continue;
		}
		int64_t q = steep ? b : (dx < 0 ? -a : a), p = steep ? (dy < 0 ? -a : a) : b;
		const int64_t g = std::gcd(std::llabs(q), std::llabs(p));
		q /= g;
		p /= g;
		double d = std::fabs(std::remainder(std::atan2(double(p), double(q)) - want, 2 * PI));
		if (d < best - 1e-15) {
			best = d;
			l.q = q;
			l.p = p;
		}
	}
	return l;
}

std::array<double, 2> nearest(const Line &l, double x, double y) {
	const double vx = double(l.q) * l.h, vy = double(l.p) * l.h;
	const double k = std::round(((x - l.ax) * vx + (y - l.ay) * vy) / (vx * vx + vy * vy));
	return {l.ax + k * vx, l.ay + k * vy};
}

double Circle::radius() const { return std::sqrt(double(n)) * h; }

std::array<double, 2> Circle::point(size_t i) const { return {cx + double(pts[i][0]) * h, cy + double(pts[i][1]) * h}; }

const Circle &circle_of(double cx, double cy, double r) {
	static std::mutex mu;
	static std::map<std::tuple<double, double, double>, Circle> cache;
	const std::lock_guard<std::mutex> lock(mu);
	auto key = std::make_tuple(cx, cy, r);
	if (auto it = cache.find(key); it != cache.end()) {
		return it->second;
	}
	Circle c;
	c.h = grid_for(std::max(std::fabs(cx), std::fabs(cy)) + r + 1);
	c.cx = std::round(cx / c.h) * c.h;
	c.cy = std::round(cy / c.h) * c.h;
	const double delta = std::min(0.5, r / 20);
	const int64_t lo = int64_t(std::ceil(std::pow((r - delta) / c.h, 2))), hi = int64_t(std::floor(std::pow((r + delta) / c.h, 2)));
	// n = P k: P a product of distinct primes = 1 mod 4 (their count sets the points), k in the window (at
	// most 256 of them per P), k = 1 mod 4 primes of the list times 2s and squares (else skipped)
	static const int64_t PRIMES[] = {5, 13, 17, 29, 37, 41, 53, 61, 73, 89, 97, 101, 109, 113, 137, 149};
	constexpr int NP = int(std::size(PRIMES));
	int64_t best_n = 0, best_count = -1;
	double best_off = INFINITY;
	for (int mask = 1; mask < (1 << NP); mask++) {
		int64_t P = 1;
		bool over = false;
		for (int i = 0; i < NP && !over; i++) {
			if (mask & (1 << i)) {
				over = P > hi / PRIMES[i];
				P *= PRIMES[i];
			}
		}
		if (over) {
			continue;
		}
		const int64_t k0 = (lo + P - 1) / P, k1 = hi / P;
		if (k1 < k0 || k1 - k0 > 256) {
			continue;
		}
		for (int64_t k = k0; k <= k1; k++) {
			int e[NP] = {};
			int64_t rest = k;
			for (int i = 0; i < NP; i++) {
				e[i] = (mask >> i) & 1;
				while (rest % PRIMES[i] == 0) {
					e[i]++;
					rest /= PRIMES[i];
				}
			}
			while (rest % 2 == 0) {
				rest /= 2;
			}
			for (const int64_t q : {3, 7, 11, 19, 23, 31, 43, 47}) {
				while (rest % (q * q) == 0) {
					rest /= q * q;
				}
			}
			if (rest != 1) {
				continue;
			}
			int64_t count = 4;
			for (int i = 0; i < NP; i++) {
				count *= e[i] + 1;
			}
			const int64_t n = P * k;
			const double off = std::fabs(std::sqrt(double(n)) * c.h - r);
			if (count > best_count || (count == best_count && off < best_off)) {
				best_n = n;
				best_count = count;
				best_off = off;
			}
		}
	}
	if (best_count < 0) {
		best_n = int64_t(std::llround(std::pow(r / c.h, 2))); // (no rich n: a perfect square's few points at least)
		const int64_t root = isqrt(best_n);
		best_n = root * root;
	}
	c.n = best_n;
	const int64_t top = isqrt(c.n);
	for (int64_t x = 0; x <= top; x++) {
		const int64_t yy = c.n - x * x;
		const int64_t y = isqrt(yy);
		if (y * y != yy) {
			continue;
		}
		for (const int64_t sx : {1, -1}) {
			for (const int64_t sy : {1, -1}) {
				if ((x == 0 && sx < 0) || (y == 0 && sy < 0)) {
					continue;
				}
				c.pts.push_back({sx * x, sy * y});
			}
		}
	}
	std::sort(c.pts.begin(), c.pts.end(), [](const auto &a, const auto &b) { return std::atan2(double(a[1]), double(a[0])) < std::atan2(double(b[1]), double(b[0])); });
	for (const auto &p : c.pts) {
		c.angles.push_back(std::atan2(double(p[1]), double(p[0])));
	}
	return cache.emplace(key, std::move(c)).first->second;
}

size_t nearest_angle(const Circle &c, double a) {
	const auto it = std::lower_bound(c.angles.begin(), c.angles.end(), a);
	const size_t n = c.angles.size();
	const size_t hi = size_t(it - c.angles.begin()) % n, lo = (hi + n - 1) % n;
	auto gap = [&](size_t i) { return std::fabs(std::remainder(c.angles[i] - a, 2 * PI)); };
	return gap(lo) <= gap(hi) ? lo : hi;
}

size_t nearest(const Circle &c, double x, double y) { return nearest_angle(c, std::atan2(y - c.cy, x - c.cx)); }

} // namespace rcg::lattice
