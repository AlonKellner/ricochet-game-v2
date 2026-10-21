// Stage 122: the presentation layer (rcg/present.hpp, docs/TDD_27_PRESENTATION.md).
#include "rcg/present.hpp"

#include "rc/earcut.hpp"
#include "rcg/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

namespace rcg {

namespace shape_morph {

namespace {

constexpr double TAG_TOL = 0.02; // a point on a carrier (float positions)

Pt sub(const Pt &a, const Pt &b) { return Pt{a.x - b.x, a.y - b.y}; }
Pt lerp(const Pt &a, const Pt &b, double e) { return Pt{a.x + (b.x - a.x) * e, a.y + (b.y - a.y) * e}; }
double dist2(const Pt &a, const Pt &b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
double dist(const Pt &a, const Pt &b) { return std::sqrt(dist2(a, b)); } // (presentation: no hypot's care)

// the closest point of segment ab to q, and its parameter
Pt closest_on(const Pt &a, const Pt &b, const Pt &q, double *w = nullptr) {
	const Pt d = sub(b, a);
	const double l2 = dot(d, d);
	const double t = l2 > 0.0 ? std::clamp(dot(sub(q, a), d) / l2, 0.0, 1.0) : 0.0;
	if (w) {
		*w = t;
	}
	return lerp(a, b, t);
}

double area(const std::vector<Pt> &ring) {
	double a = 0.0;
	for (size_t i = 0; i < ring.size(); i++) {
		a += cross(ring[i], ring[(i + 1) % ring.size()]);
	}
	return a / 2.0;
}

std::vector<Pt> points(const Ring &r) {
	std::vector<Pt> out;
	out.reserve(r.size());
	for (const Vertex &v : r) {
		out.push_back(v.p);
	}
	return out;
}

Pt centroid(const std::vector<Pt> &ring) {
	Pt c{0, 0};
	for (const Pt &p : ring) {
		c.x += p.x / double(ring.size());
		c.y += p.y / double(ring.size());
	}
	return c;
}

// the tag of a point: the surfaces it lies on
void tag(Vertex &v, const std::vector<Carrier> &carriers) {
	std::vector<int> ids;
	int line = -1, lines = 0;
	for (size_t k = 0; k < carriers.size(); k++) {
		if (carriers[k].dist(v.p) < TAG_TOL) {
			ids.push_back(carriers[k].surface);
			if (carriers[k].line) {
				line = int(k);
				lines++;
			}
		}
	}
	std::sort(ids.begin(), ids.end());
	uint64_t h = 0;
	for (int id : ids) {
		h = h * 1000003ull + uint64_t(id + 2);
	}
	v.tag = int64_t(h);
	v.carrier = ids.size() == 1 && lines == 1 ? line : -1;
}

// the closest point of a ring's chain from vertex i0 forward to i1 (cyclic) to q, and its arc parameter
Pt closest_on_chain(const std::vector<Pt> &ring, size_t i0, size_t i1, const Pt &q, double *param = nullptr) {
	const size_t n = ring.size();
	Pt best = ring[i0 % n];
	double bd = INFINITY, bp = 0.0, run = 0.0;
	for (size_t k = i0; k <= i0 + n; k++) {
		const Pt &a = ring[k % n];
		if (k % n == i1 % n && k > i0) {
			if (dist(a, q) < bd) {
				best = a;
				bp = run;
			}
			break;
		}
		const Pt &b = ring[(k + 1) % n];
		double w = 0.0;
		const Pt c = closest_on(a, b, q, &w);
		const double d = dist(c, q);
		if (d < bd) {
			bd = d;
			best = c;
			bp = run + w * dist(a, b);
		}
		run += dist(a, b);
	}
	if (param) {
		*param = bp;
	}
	return best;
}

// the point of a ring's chain from i0 forward to i1 at arc parameter `at`
Pt point_on_chain(const std::vector<Pt> &ring, size_t i0, size_t i1, double at) {
	const size_t n = ring.size();
	double run = 0.0;
	for (size_t k = i0; k < i0 + n; k++) {
		if (k % n == i1 % n && k > i0) {
			break;
		}
		const Pt &a = ring[k % n], &b = ring[(k + 1) % n];
		const double l = dist(a, b);
		if (run + l >= at) {
			return lerp(a, b, l > 0.0 ? (at - run) / l : 0.0);
		}
		run += l;
	}
	return ring[i1 % n];
}

// the closest edge of a shape to q (where a piece grows out of, or shrinks into)
bool closest_edge(const Shape &s, const Pt &q, Pt &a, Pt &b) {
	double bd = INFINITY;
	for (const Ring &r : s.rings) {
		for (size_t i = 0; i < r.size(); i++) {
			const Pt &u = r[i].p, &v = r[(i + 1) % r.size()].p;
			const double d = dist(closest_on(u, v, q), q);
			if (d < bd) {
				bd = d;
				a = u;
				b = v;
			}
		}
	}
	return std::isfinite(bd);
}

double dist_to_ring(const std::vector<Pt> &ring, const Pt &q) {
	double bd = INFINITY;
	for (size_t i = 0; i < ring.size(); i++) {
		bd = std::min(bd, dist2(closest_on(ring[i], ring[(i + 1) % ring.size()], q), q));
	}
	return std::sqrt(bd);
}

Vertex moved_to(const Vertex &v, const Pt &p) {
	Vertex out = v;
	out.p = p;
	out.shadow = false;
	out.apex = false;
	return out;
}

// a cyclic diff of two rings (both with the interior on the left): anchored at
// the closest pair of equal tags, then a sequence alignment (Needleman-Wunsch):
// a pair costs its distance (half when they lie on the same surfaces), an
// unpaired vertex its distance to the other ring and a gap; an unpaired vertex
// then moves to (from) the closest point of the other ring between its paired
// neighbours' partners.
// the alignment from one pair of anchors, and its cost
std::vector<Track> align_from(const Ring &A, const Ring &B, size_t a0, size_t b0, double *total) {
	const size_t n = A.size(), m = B.size();
	const std::vector<Pt> pa = points(A), pb = points(B);
	auto ai = [&](size_t i) { return (a0 + i) % n; };
	auto bj = [&](size_t j) { return (b0 + j) % m; };
	constexpr double GAP = 8.0, OTHER = 24.0;
	std::vector<double> del(n), ins(m);
	for (size_t i = 0; i < n; i++) {
		del[i] = dist_to_ring(pb, pa[ai(i)]) + GAP;
	}
	for (size_t j = 0; j < m; j++) {
		ins[j] = dist_to_ring(pa, pb[bj(j)]) + GAP;
	}
	auto pair_cost = [&](size_t i, size_t j) {
		const Vertex &x = A[ai(i)], &y = B[bj(j)];
		const double d = dist(x.p, y.p);
		return x.tag != y.tag ? d + OTHER : x.tag != 0 ? 0.5 * d : d; // (on the same surfaces: cheaper)
	};
	// the alignment of A[1..n) with B[1..m) (the anchors paired)
	const size_t N = n - 1, M = m - 1, W = M + 1;
	std::vector<double> cost((N + 1) * W, 0.0);
	std::vector<uint8_t> step((N + 1) * W, 0); // 1 pair, 2 delete, 3 insert
	for (size_t i = 1; i <= N; i++) {
		cost[i * W] = cost[(i - 1) * W] + del[i];
		step[i * W] = 2;
	}
	for (size_t j = 1; j <= M; j++) {
		cost[j] = cost[j - 1] + ins[j];
		step[j] = 3;
	}
	for (size_t i = 1; i <= N; i++) {
		for (size_t j = 1; j <= M; j++) {
			const double p = cost[(i - 1) * W + j - 1] + pair_cost(i, j), d = cost[(i - 1) * W + j] + del[i], s = cost[i * W + j - 1] + ins[j];
			if (p <= d && p <= s) {
				cost[i * W + j] = p;
				step[i * W + j] = 1;
			} else if (d <= s) {
				cost[i * W + j] = d;
				step[i * W + j] = 2;
			} else {
				cost[i * W + j] = s;
				step[i * W + j] = 3;
			}
		}
	}
	if (total) {
		*total = cost[N * W + M] + dist(pa[a0], pb[b0]);
	}
	struct Op {
		long i = -1, j = -1; // offsets from the anchors (-1: none)
	};
	std::vector<Op> ops;
	for (size_t i = N, j = M; i > 0 || j > 0;) {
		const uint8_t s = step[i * W + j];
		if (s == 1) {
			ops.push_back(Op{long(i--), long(j--)});
		} else if (s == 2) {
			ops.push_back(Op{long(i--), -1});
		} else {
			ops.push_back(Op{-1, long(j--)});
		}
	}
	ops.push_back(Op{0, 0});
	std::reverse(ops.begin(), ops.end());
	std::vector<Track> out;
	out.reserve(ops.size());
	size_t k = 0;
	while (k < ops.size()) {
		if (ops[k].i >= 0 && ops[k].j >= 0) {
			out.push_back(Track{A[ai(size_t(ops[k].i))], B[bj(size_t(ops[k].j))]});
			k++;
			continue;
		}
		// a gap run between two pairs (the last one: the anchors again)
		size_t e = k;
		while (e < ops.size() && !(ops[e].i >= 0 && ops[e].j >= 0)) {
			e++;
		}
		const Op prev = ops[k - 1];
		const Op next = e < ops.size() ? ops[e] : Op{long(n), long(m)};
		// deleted vertices keep A's order, inserted ones B's; each moves to (from) the
		// other chain between the paired neighbours' partners, in order along it
		struct Gap {
			Track t;
			double own = 0.0, other = 0.0; // its arc parameter on its own chain, its projection's on the other
		};
		std::vector<Gap> dels, inss;
		const size_t A0 = ai(size_t(prev.i)), A1 = ai(size_t(next.i)), B0 = bj(size_t(prev.j)), B1 = bj(size_t(next.j));
		for (size_t g = k; g < e; g++) {
			Gap gp;
			if (ops[g].i >= 0) { // deleted: along A, to B's chain
				const Vertex &v = A[ai(size_t(ops[g].i))];
				closest_on_chain(pa, A0, A1, v.p, &gp.own);
				closest_on_chain(pb, B0, B1, v.p, &gp.other);
				gp.t = Track{v, v};
				dels.push_back(gp);
			} else { // inserted: along B, from A's chain
				const Vertex &v = B[bj(size_t(ops[g].j))];
				closest_on_chain(pb, B0, B1, v.p, &gp.own);
				closest_on_chain(pa, A0, A1, v.p, &gp.other);
				gp.t = Track{v, v};
				inss.push_back(gp);
			}
		}
		// merged along A (a deleted vertex by its own place, an inserted one by its
		// projection's), then every projection kept in order on its chain and between
		// the fixed vertices around it there, so both ends trace their rings in order
		struct Item {
			bool del;
			double a, b; // the arc parameters on A's and B's chains (one fixed, one projected)
			Track t;
		};
		std::vector<Item> items;
		for (size_t x = 0, y = 0; x < dels.size() || y < inss.size();) {
			if (y == inss.size() || (x < dels.size() && dels[x].own <= inss[y].other)) {
				items.push_back(Item{true, dels[x].own, dels[x].other, dels[x].t});
				x++;
			} else {
				items.push_back(Item{false, inss[y].other, inss[y].own, inss[y].t});
				y++;
			}
		}
		auto keep_in_order = [&](bool on_a) {
			// the fixed values on this chain: A's for deleted vertices, B's for inserted ones
			const size_t c = items.size();
			std::vector<double> lo(c, 0.0), hi(c, INFINITY);
			double floor = 0.0;
			for (size_t q = 0; q < c; q++) {
				const bool fixed = items[q].del == on_a;
				double &v = on_a ? items[q].a : items[q].b;
				if (fixed) {
					floor = std::max(floor, v);
				}
				lo[q] = floor;
			}
			double ceil = INFINITY;
			for (size_t q = c; q-- > 0;) {
				const bool fixed = items[q].del == on_a;
				const double v = on_a ? items[q].a : items[q].b;
				if (fixed) {
					ceil = std::min(ceil, v);
				}
				hi[q] = ceil;
			}
			double run = 0.0;
			for (size_t q = 0; q < c; q++) {
				double &v = on_a ? items[q].a : items[q].b;
				if (items[q].del != on_a) { // projected: between its fixed neighbours, after the previous
					v = std::max({v, lo[q], run});
					v = std::min(v, std::max(hi[q], lo[q]));
				}
				run = std::max(run, v);
			}
		};
		keep_in_order(true);
		keep_in_order(false);
		for (Item &it : items) {
			if (it.del) {
				it.t.to = moved_to(it.t.from, point_on_chain(pb, B0, B1, it.b));
			} else {
				it.t.from = moved_to(it.t.to, point_on_chain(pa, A0, A1, it.a));
			}
			out.push_back(it.t);
		}
		k = e;
	}
	return out;
}

// a cyclic diff of two rings (both counterclockwise): the alignment of the
// cheapest anchors tried -- every rotation of a small ring, the two closest
// pairs on one surface and the closest point for a large one
std::vector<Track> align(const Ring &A, const Ring &B) {
	const size_t n = A.size(), m = B.size();
	std::vector<std::pair<size_t, size_t>> anchors;
	if (n * m <= 400) {
		for (size_t j = 0; j < m; j++) {
			anchors.push_back({0, j});
		}
	} else {
		std::vector<std::tuple<double, size_t, size_t>> pairs;
		for (size_t i = 0; i < n; i++) {
			for (size_t j = 0; j < m; j++) {
				if (A[i].tag != 0 && A[i].tag == B[j].tag) {
					pairs.push_back({dist(A[i].p, B[j].p), i, j});
				}
			}
		}
		std::sort(pairs.begin(), pairs.end());
		for (size_t k = 0; k < pairs.size() && k < 2; k++) {
			anchors.push_back({std::get<1>(pairs[k]), std::get<2>(pairs[k])});
		}
		size_t near = 0;
		for (size_t j = 1; j < m; j++) {
			near = dist(A[0].p, B[j].p) < dist(A[0].p, B[near].p) ? j : near;
		}
		anchors.push_back({0, near});
	}
	std::vector<Track> best;
	double best_cost = INFINITY;
	for (const auto &[a0, b0] : anchors) {
		double c = 0.0;
		std::vector<Track> t = align_from(A, B, a0, b0, &c);
		if (c < best_cost) {
			best_cost = c;
			best = std::move(t);
		}
	}
	return best;
}

} // namespace

double Carrier::dist(const Pt &q) const { return line ? std::abs(cross(d, sub(q, p))) : std::abs(std::hypot(q.x - c.x, q.y - c.y) - r); }

std::vector<Carrier> carriers_of(const rc::vis::Result &r, const World *world) {
	std::map<int, Carrier> by_id;
	for (const rc::vis::Cell &cell : r.cells) {
		for (const auto &loop : cell.loops) {
			for (const rc::vis::Edge &e : loop) {
				if (e.kind != rc::vis::EdgeKind::Carrier || by_id.count(e.surface)) {
					continue;
				}
				Carrier c;
				c.surface = e.surface;
				c.line = e.gline;
				c.p = e.gp;
				const double l = std::hypot(e.gd.x, e.gd.y);
				c.d = l > 0.0 ? Pt{e.gd.x / l, e.gd.y / l} : Pt{1, 0};
				c.c = e.gc;
				c.r = e.gr;
				by_id[e.surface] = c;
			}
		}
	}
	std::vector<Carrier> out;
	for (auto &[id, c] : by_id) {
		if (const BuiltSurface *b = world ? world->by_id(id) : nullptr; b && c.line && b->seg.is_line()) {
			c.has_segment = true;
			c.s = Pt{double(b->seg.s.x), double(b->seg.s.y)};
			c.e = Pt{double(b->seg.e.x), double(b->seg.e.y)};
		}
		out.push_back(c);
	}
	return out;
}

// a ring's vertices kept where it turns: straight runs merged, arcs to chords within tol
std::vector<Pt> simplified(const std::vector<Pt> &ring, double tol) {
	const size_t n = ring.size();
	size_t s0 = 0;
	double sharpest = -1.0;
	for (size_t i = 0; i < n; i++) {
		const Pt u = sub(ring[i], ring[(i + n - 1) % n]), w = sub(ring[(i + 1) % n], ring[i]);
		const double turn = std::abs(std::atan2(rc::vis::cross(u, w), rc::vis::dot(u, w)));
		if (turn > sharpest) {
			sharpest = turn;
			s0 = i;
		}
	}
	std::vector<Pt> kept{ring[s0]};
	size_t anchor = 0; // the last kept (an offset from s0)
	for (size_t off = 1; off < n; off++) {
		const Pt &next = ring[(s0 + off + 1) % n];
		bool fits = true;
		for (size_t k = anchor + 1; k <= off && fits; k++) {
			const Pt &q = ring[(s0 + k) % n];
			fits = dist(closest_on(ring[(s0 + anchor) % n], next, q), q) <= tol;
		}
		if (!fits) {
			kept.push_back(ring[(s0 + off) % n]);
			anchor = off;
		}
	}
	return kept;
}

// the union of rings that do not overlap (counterclockwise, conforming): an edge two
// rings share runs both ways and cancels; the rest chains into the outline's
// rings (outer counterclockwise, holes clockwise; where rings touch at a vertex,
// the rightmost turn, each stays simple). Points are one on a 1e-4 px grid (two
// cells tessellate a shared arc from its two ends).
std::vector<std::vector<Pt>> union_of(const std::vector<std::vector<Pt>> &rings) {
	using Q = std::pair<long long, long long>;
	auto q_of = [](const Pt &p) { return Q{std::llround(p.x * 1e4), std::llround(p.y * 1e4)}; };
	std::map<Q, Pt> at_q;
	std::map<std::pair<Q, Q>, int> edges;
	for (const std::vector<Pt> &r : rings) {
		for (size_t k = 0; k < r.size(); k++) {
			const Q u = q_of(r[k]), v = q_of(r[(k + 1) % r.size()]);
			at_q.emplace(u, r[k]);
			if (u == v) {
				continue;
			}
			if (auto it = edges.find({v, u}); it != edges.end()) {
				if (--it->second == 0) {
					edges.erase(it);
				}
			} else {
				edges[{u, v}]++;
			}
		}
	}
	std::vector<std::pair<Q, Q>> list;
	for (const auto &[e, n] : edges) {
		for (int k = 0; k < n; k++) {
			list.push_back(e);
		}
	}
	std::map<Q, std::vector<size_t>> outgoing;
	for (size_t k = 0; k < list.size(); k++) {
		outgoing[list[k].first].push_back(k);
	}
	std::vector<std::vector<Pt>> out;
	std::vector<bool> used(list.size(), false);
	for (size_t e0 = 0; e0 < list.size(); e0++) {
		if (used[e0]) {
			continue;
		}
		used[e0] = true;
		std::vector<Pt> ring{at_q[list[e0].first]};
		Q prev = list[e0].first, cur = list[e0].second;
		const Q start = prev;
		for (size_t guard = 0; cur != start && guard <= list.size(); guard++) {
			ring.push_back(at_q[cur]);
			size_t pick = list.size();
			double best = INFINITY;
			const Pt din = sub(at_q[cur], at_q[prev]);
			for (size_t ei : outgoing[cur]) {
				if (used[ei]) {
					continue;
				}
				const Pt dout = sub(at_q[list[ei].second], at_q[cur]);
				const double turn = std::atan2(rc::vis::cross(din, dout), rc::vis::dot(din, dout));
				if (turn < best) {
					best = turn;
					pick = ei;
				}
			}
			if (pick == list.size()) {
				break;
			}
			used[pick] = true;
			prev = cur;
			cur = list[pick].second;
		}
		if (ring.size() >= 3) {
			out.push_back(std::move(ring));
		}
	}
	return out;
}

// whether rings' edges cross one another properly (touching at a vertex is not
// crossing): a sweep over x
bool crossing(const std::vector<std::vector<Pt>> &rings) {
	struct Seg {
		Pt a, b;
		double x0, x1;
	};
	std::vector<Seg> segs;
	for (const std::vector<Pt> &r : rings) {
		for (size_t k = 0; k < r.size(); k++) {
			const Pt &a = r[k], &b = r[(k + 1) % r.size()];
			segs.push_back(Seg{a, b, std::min(a.x, b.x), std::max(a.x, b.x)});
		}
	}
	std::sort(segs.begin(), segs.end(), [](const Seg &x, const Seg &y) { return x.x0 < y.x0; });
	auto orient = [](const Pt &p, const Pt &q, const Pt &t) { return rc::vis::cross(sub(q, p), sub(t, p)); };
	for (size_t i = 0; i < segs.size(); i++) {
		for (size_t j = i + 1; j < segs.size() && segs[j].x0 <= segs[i].x1; j++) {
			const Seg &u = segs[i], &v = segs[j];
			if (std::max(u.a.y, u.b.y) < std::min(v.a.y, v.b.y) || std::max(v.a.y, v.b.y) < std::min(u.a.y, u.b.y)) {
				continue;
			}
			const double eps = 1e-9 * (1.0 + std::abs(u.x1) + std::abs(v.x1));
			const double o1 = orient(u.a, u.b, v.a), o2 = orient(u.a, u.b, v.b), o3 = orient(v.a, v.b, u.a), o4 = orient(v.a, v.b, u.b);
			if (((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) && ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps))) {
				return true;
			}
		}
	}
	return false;
}

Shape shape_of(const rc::vis::Result &r, std::vector<Carrier> carriers, double tol) {
	using namespace rc::vis::fill_detail;
	Shape out;
	out.carriers = std::move(carriers);
	if (!r.supported) {
		return out;
	}
	// the cells' rings as the fill makes them (conforming: a corner of one cell on
	// another's edge is a vertex of both), each counterclockwise
	std::map<GeomKey, std::vector<Pt>> corners;
	for (const rc::vis::Cell &c : r.cells) {
		for (const auto &l : c.loops) {
			for (const rc::vis::Edge &e : l) {
				const GeomKey k = key_of(e);
				if (k.kind != 0) {
					corners[k].push_back(e.a);
					corners[k].push_back(e.b);
				}
			}
		}
	}
	std::vector<std::vector<Pt>> exact; // the cells' rings, counterclockwise
	for (const rc::vis::Cell &c : r.cells) {
		for (const auto &l : c.loops) {
			std::vector<Pt> poly;
			for (const rc::vis::Edge &e : l) {
				conforming(e, corners, poly);
			}
			std::vector<Pt> ring;
			for (const Pt &q : rc::vis::clip_polygon(poly, r.domain)) {
				if (ring.empty() || !(ring.back() == q)) {
					ring.push_back(q);
				}
			}
			while (ring.size() > 1 && ring.back() == ring.front()) {
				ring.pop_back();
			}
			const double a0 = ring.size() >= 3 ? area(ring) : 0.0;
			if (std::abs(a0) <= 1e-9) {
				continue;
			}
			std::vector<std::vector<Pt>> simple;
			rc::vis::simple_rings(std::move(ring), a0 > 0.0 ? 1.0 : -1.0, simple);
			for (std::vector<Pt> &sr : simple) {
				if (area(sr) < 0.0) {
					std::reverse(sr.begin(), sr.end());
				}
				exact.push_back(std::move(sr));
			}
		}
	}
	// cells that do not overlap morph as their union's outline: no edge between two
	// cells to part mid-morph; overlapping ones (a pencil of circles can) as pieces.
	// (Overlapping cells leave an outline that crosses itself.)
	auto rings_of = [&](const std::vector<std::vector<Pt>> &from) {
		std::vector<Ring> rings;
		for (const std::vector<Pt> &e : from) {
			const std::vector<Pt> kept = e.size() > 3 ? simplified(e, tol) : e;
			if (kept.size() < 3 || std::abs(area(kept)) < 0.01) {
				continue;
			}
			Ring rr;
			for (const Pt &q : kept) {
				Vertex v;
				v.p = q;
				tag(v, out.carriers);
				rr.push_back(v);
			}
			rings.push_back(std::move(rr));
		}
		return rings;
	};
	const std::vector<std::vector<Pt>> outline = union_of(exact);
	out.pieces = rings_of(exact);
	out.outline = !crossing(outline);
	out.rings = out.outline ? rings_of(outline) : out.pieces;
	return out;
}

// one ring's shadows (mark_shadows)
void mark_ring_shadows(Ring &r, const std::vector<Carrier> &carriers, const std::vector<std::pair<Pt, int>> &corners, const Pt &apex) {
	{
		const size_t n = r.size();
		for (size_t i = 0; i < n; i++) {
			r[i].apex = r[i].apex || dist(r[i].p, apex) < 1e-3;
		}
		for (size_t i = 0; i < n; i++) {
			for (const size_t j : {(i + 1) % n, (i + n - 1) % n}) {
				// an edge on a ray from the apex; its far end on a straight surface
				const Vertex &u = r[j];
				Vertex &v = r[i];
				const Pt du = sub(u.p, apex), dv = sub(v.p, apex);
				const double lu = std::hypot(du.x, du.y), lv = std::hypot(dv.x, dv.y);
				if (lv <= lu || lv < 1e-3 || v.carrier < 0 || !carriers[size_t(v.carrier)].has_segment) {
					continue;
				}
				if (lu >= 1e-3 && (rc::vis::dot(du, dv) <= 0.0 || std::abs(rc::vis::cross(du, dv)) > 1e-5 * lu * lv)) {
					continue;
				}
				// the occluding corner: another surface's end on the ray, the farthest before v
				double best = 0.0;
				for (const auto &[c, k] : corners) {
					if (k == v.carrier) {
						continue;
					}
					double w = 0.0;
					if (dist(closest_on(apex, v.p, c, &w), c) < 1e-3 && w > 1e-3 && w < 1.0 - 1e-6 && w > best) {
						best = w;
						v.shadow = true;
						v.pivot = c;
					}
				}
			}
		}
	}
}

void mark_shadows(Shape &s, const Pt &apex) {
	s.has_apex = true;
	s.apex = apex;
	// the corners that can occlude: the surfaces' ends
	std::vector<std::pair<Pt, int>> corners;
	for (size_t k = 0; k < s.carriers.size(); k++) {
		if (s.carriers[k].has_segment) {
			corners.push_back({s.carriers[k].s, int(k)});
			corners.push_back({s.carriers[k].e, int(k)});
		}
	}
	auto mark_ring = [&](Ring &r) { mark_ring_shadows(r, s.carriers, corners, apex); };
	for (Ring &r : s.rings) {
		mark_ring(r);
	}
	for (Ring &r : s.pieces) {
		mark_ring(r);
	}
}

Vertex recast(const Vertex &v, const std::vector<Carrier> &carriers, const Pt &apex) {
	Vertex out = v;
	if (v.apex) {
		out.p = apex;
		return out;
	}
	if (!v.shadow || v.carrier < 0 || size_t(v.carrier) >= carriers.size()) {
		return out;
	}
	const Carrier &c = carriers[size_t(v.carrier)];
	const Pt dir = sub(v.pivot, apex), d = sub(c.e, c.s);
	const double den = cross(dir, d);
	if (std::abs(den) < 1e-12) {
		return out;
	}
	const Pt so = sub(c.s, apex);
	const double t = cross(so, d) / den, w = cross(so, dir) / den;
	if (t > 1.0 && w >= 0.0 && w <= 1.0) { // beyond the corner, on the surface
		out.p = Pt{apex.x + dir.x * t, apex.y + dir.y * t};
	}
	return out;
}

Shape follow(const Shape &s, const Pt &apex) {
	Shape out = s;
	if (!s.has_apex) {
		return out;
	}
	for (auto *rings : {&out.rings, &out.pieces}) {
		for (Ring &r : *rings) {
			for (Vertex &v : r) {
				v = recast(v, s.carriers, apex);
			}
		}
	}
	return out;
}

std::vector<int> parents_of(const Shape &s) {
	// a clockwise ring is a hole of the smallest counterclockwise ring around it
	std::vector<std::vector<Pt>> rings;
	std::vector<double> areas;
	for (const Ring &r : s.rings) {
		rings.push_back(points(r));
		areas.push_back(rings.back().size() >= 3 ? area(rings.back()) : 0.0);
	}
	std::vector<int> parent(rings.size(), -1);
	for (size_t k = 0; k < rings.size(); k++) {
		if (areas[k] >= 0.0 || rings[k].empty()) {
			continue;
		}
		double best = INFINITY;
		for (size_t o = 0; o < rings.size(); o++) {
			if (areas[o] > 0.0 && areas[o] < best && rc::vis::in_ring(rings[o], rings[k][0])) {
				best = areas[o];
				parent[k] = int(o);
			}
		}
		if (parent[k] < 0) {
			parent[k] = -2; // (a hole of nothing: not drawn)
		}
	}
	return parent;
}

// a piece with no partner: its vertices on the other shape's edge closest to it
// (a degenerate polygon there) at one end of the way
std::vector<Track> grow(const Ring &piece, const Shape &other, const Pt &anchor, bool born) {
	const std::vector<Pt> pts = points(piece);
	Pt ea = anchor, eb = anchor;
	closest_edge(other, centroid(pts), ea, eb);
	std::vector<Track> out;
	for (const Vertex &v : piece) {
		const Vertex there = moved_to(v, closest_on(ea, eb, v.p));
		out.push_back(born ? Track{there, v} : Track{v, there});
	}
	return out;
}

Plan plan(const Shape &a_in, const Shape &b_in, const Pt &anchor) {
	// outlines when both have one (no edge between cells to part), else both as pieces
	// (a shape with no pieces -- one on screen mid-morph -- as its rings)
	const bool outlines = a_in.outline && b_in.outline;
	Shape a, b;
	a.rings = outlines || a_in.pieces.empty() ? a_in.rings : a_in.pieces;
	b.rings = outlines || b_in.pieces.empty() ? b_in.rings : b_in.pieces;
	Plan out;
	out.outline = outlines;
	// the pieces paired by place (how far each one's vertices lie from the other's
	// outline, for their size), greedily
	struct Cand {
		double cost;
		size_t i, j;
	};
	std::vector<Cand> cands;
	std::vector<std::vector<Pt>> PA, PB;
	std::vector<Pt> CA, CB;
	std::vector<double> SA, SB;
	for (const Ring &r : a.rings) {
		PA.push_back(points(r));
		CA.push_back(centroid(PA.back()));
		SA.push_back(std::sqrt(std::max(std::abs(area(PA.back())), 1.0)));
	}
	for (const Ring &r : b.rings) {
		PB.push_back(points(r));
		CB.push_back(centroid(PB.back()));
		SB.push_back(std::sqrt(std::max(std::abs(area(PB.back())), 1.0)));
	}
	// a cheap signature first (centroids for the size, the areas' ratio): each
	// piece's three likeliest partners, both ways; then how far their outlines lie
	std::vector<std::pair<size_t, size_t>> likely;
	auto rough = [&](size_t i, size_t j) {
		const double size = std::max(SA[i], SB[j]);
		return dist(CA[i], CB[j]) / size + std::abs(std::log(SA[i] / SB[j]));
	};
	for (int way = 0; way < 2; way++) {
		const size_t outer = way == 0 ? PA.size() : PB.size(), inner = way == 0 ? PB.size() : PA.size();
		for (size_t x = 0; x < outer; x++) {
			std::vector<std::pair<double, size_t>> by;
			for (size_t y = 0; y < inner; y++) {
				by.push_back({way == 0 ? rough(x, y) : rough(y, x), y});
			}
			const size_t keep = std::min<size_t>(3, by.size());
			std::partial_sort(by.begin(), by.begin() + long(keep), by.end());
			for (size_t q = 0; q < keep; q++) {
				likely.push_back(way == 0 ? std::pair{x, by[q].second} : std::pair{by[q].second, x});
			}
		}
	}
	std::sort(likely.begin(), likely.end());
	likely.erase(std::unique(likely.begin(), likely.end()), likely.end());
	for (const auto &[i, j] : likely) {
		{
			const double size = std::max(SA[i], SB[j]);
			if (dist(CA[i], CB[j]) > 2.0 * size) {
				continue;
			}
			// (at most 24 vertices of each, evenly spread)
			double d = 0.0;
			const size_t sa = std::max<size_t>(1, PA[i].size() / 24), sb = std::max<size_t>(1, PB[j].size() / 24);
			for (size_t q = 0; q < PA[i].size(); q += sa) {
				d += dist_to_ring(PB[j], PA[i][q]) / double((PA[i].size() + sa - 1) / sa);
			}
			for (size_t q = 0; q < PB[j].size(); q += sb) {
				d += dist_to_ring(PA[i], PB[j][q]) / double((PB[j].size() + sb - 1) / sb);
			}
			const double c = d / 2.0 / size;
			if (c < 0.5) {
				cands.push_back(Cand{c, i, j});
			}
		}
	}
	std::sort(cands.begin(), cands.end(), [](const Cand &x, const Cand &y) { return x.cost < y.cost; });
	std::vector<int> pair_of_a(a.rings.size(), -1), pair_of_b(b.rings.size(), -1);
	for (const Cand &c : cands) {
		if (pair_of_a[c.i] < 0 && pair_of_b[c.j] < 0) {
			pair_of_a[c.i] = int(c.j);
			pair_of_b[c.j] = int(c.i);
		}
	}
	for (size_t j = 0; j < b.rings.size(); j++) {
		if (pair_of_b[j] >= 0) {
			out.rings.push_back(align(a.rings[size_t(pair_of_b[j])], b.rings[j]));
		} else { // born: out of the old shape's closest edge (or the anchor)
			out.rings.push_back(grow(b.rings[j], a, anchor, true));
		}
	}
	for (size_t i = 0; i < a.rings.size(); i++) {
		if (pair_of_a[i] < 0) { // dying: into the new shape's closest edge (or the anchor)
			out.rings.push_back(grow(a.rings[i], b, anchor, false));
		}
	}
	return out;
}

Shape at(const Plan &p, double e) {
	Shape out;
	out.outline = p.outline;
	for (const std::vector<Track> &ring : p.rings) {
		Ring r;
		r.reserve(ring.size());
		for (const Track &t : ring) {
			Vertex v = e < 0.5 ? t.from : t.to;
			v.p = lerp(t.from.p, t.to.p, e);
			r.push_back(v);
		}
		out.rings.push_back(std::move(r));
	}
	return out;
}

void fill_of(const Shape &s, std::vector<rc::V2> &pts, std::vector<int> &indices) {
	const std::vector<int> parent = parents_of(s);
	rc::earcut::Triangulator tri;
	for (size_t k = 0; k < s.rings.size(); k++) {
		if (parent[k] != -1 || s.rings[k].size() < 3) { // (outer rings; their holes go with them)
			continue;
		}
		std::vector<const Ring *> order{&s.rings[k]};
		for (size_t h = 0; h < s.rings.size(); h++) {
			if (parent[h] == int(k) && s.rings[h].size() >= 3) {
				order.push_back(&s.rings[h]);
			}
		}
		std::vector<std::vector<rc::earcut::XY>> rings;
		const int base = int(pts.size());
		for (const Ring *r : order) {
			std::vector<rc::earcut::XY> xy;
			for (const Vertex &v : *r) {
				xy.push_back(rc::earcut::XY{v.p.x, v.p.y});
				pts.push_back(rc::V2::of(v.p.x, v.p.y));
			}
			rings.push_back(std::move(xy));
		}
		for (uint32_t i : tri.run(rings)) {
			indices.push_back(base + int(i));
		}
	}
}

} // namespace shape_morph

namespace {

double ease_out(double x) {
	x = std::clamp(x, 0.0, 1.0);
	const double y = 1.0 - x;
	return 1.0 - y * y * y;
}

// the region's mesh in a visibility node's calls (its Triangles call)
const render::Call *mesh_of(const render::FrameNode &n) {
	for (const render::Call &c : *n.calls) {
		if (c.kind == render::Kind::Triangles) {
			return &c;
		}
	}
	return nullptr;
}

} // namespace

Presenter::Presenter(PresentConfig config) : config_(config) {}

void Presenter::advance(double dt) { now_ += std::max(0.0, dt); }

rc::V2 Presenter::player(const Session &s) const {
	if (!config_.interpolate || s.flying()) {
		return s.player();
	}
	const rc::V2 from = s.tick_from(), to = s.player();
	if ((to - from).length() > 64.0f) {
		return to; // (a teleport, a reset: no motion to show)
	}
	const double a = std::clamp(s.tick_alpha(), 0.0, 1.0);
	return rc::V2::of(double(from.x) + (double(to.x) - double(from.x)) * a, double(from.y) + (double(to.y) - double(from.y)) * a);
}

std::vector<render::FrameNode> Presenter::frame(std::vector<render::FrameNode> nodes, const Session &s) {
	namespace sm = shape_morph;
	const rc::V2 drawn = player(s);
	const sm::Pt here{double(drawn.x), double(drawn.y)};
	for (render::FrameNode &n : nodes) {
		if (n.key.kind == render::NodeKind::Player) {
			n.position = drawn;
		}
		if (n.key.kind != render::NodeKind::Visibility) {
			continue;
		}
		const render::Call *mesh = mesh_of(n);
		const int version = s.visibility_version();
		const bool late = s.region_pending() || config_.smooth_on_time;
		if (version != region_version_) {
			std::vector<sm::Carrier> carriers;
			std::optional<sm::Pt> apex;
			if (const auto &r = s.region(); r && r->supported) {
				carriers = sm::carriers_of(*r, &s.world());
				if (!r->pk_none && std::hypot(r->pk.x - r->player.x, r->pk.y - r->player.y) < 1e-6) {
					apex = r->player; // the region's rays leave the player: its shadows can follow it
				}
			}
			sm::Shape next = s.region() ? sm::shape_of(*s.region(), std::move(carriers)) : sm::Shape{};
			if (apex) {
				sm::mark_shadows(next, *apex);
			}
			if (config_.region_seconds > 0.0 && region_version_ >= 0 && (was_late_ || config_.smooth_on_time)) {
				// it was late: morph in from what was shown
				morph_ = Morph{sm::plan(shown_, next, here), now_, config_.region_seconds};
			} else if (morph_) {
				// on time during a morph: retargeted from what is shown over the time the
				// morph has left, never extended (on-time regions keep no morph alive)
				const double left = morph_->start + morph_->seconds - now_;
				if (left > 0.0) {
					morph_ = Morph{sm::plan(shown_, next, here), now_, left};
				} else {
					morph_.reset();
				}
			}
			region_version_ = version;
			target_ = std::move(next);
			if (!morph_) {
				shown_ = target_;
			}
		}
		was_late_ = late;
		const bool moved = config_.follow && late && target_.has_apex && std::hypot(here.x - target_.apex.x, here.y - target_.apex.y) > 1e-6;
		sm::Shape shape;
		if (morph_) {
			const double x = (now_ - morph_->start) / morph_->seconds;
			if (x >= 1.0) {
				morph_.reset();
			} else {
				const double e = ease_out(x);
				shape = sm::at(morph_->plan, e);
				if (moved) { // the target's shadows follow the player: each way ends where its end is recast
					for (size_t k = 0; k < shape.rings.size(); k++) {
						const std::vector<sm::Track> &tracks = morph_->plan.rings[k];
						for (size_t i = 0; i < tracks.size(); i++) {
							const sm::Pt to = sm::recast(tracks[i].to, target_.carriers, here).p, &from = tracks[i].from.p;
							shape.rings[k][i].p = sm::Pt{from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e};
						}
					}
				}
			}
		}
		if (!morph_) {
			if (!moved) {
				shown_ = target_; // the exact region: the node as the session made it
				continue;
			}
			shape = sm::follow(target_, here);
		}
		std::vector<rc::V2> pts;
		std::vector<int> idx;
		sm::fill_of(shape, pts, idx);
		shown_ = std::move(shape);
		auto calls = std::make_shared<std::vector<render::Call>>();
		if (!idx.empty()) { // (as the session: no mesh call without triangles)
			render::Call k;
			k.kind = render::Kind::Triangles;
			k.pts = std::move(pts);
			k.indices = std::move(idx);
			k.colors = mesh ? mesh->colors : std::vector<render::Color>{render::Color{1.0f, 1.0f, 1.0f, 0.1f}};
			calls->push_back(std::move(k));
		}
		for (const render::Call &c : *n.calls) {
			if (c.kind != render::Kind::Triangles) {
				calls->push_back(c); // the whiskers
			}
		}
		n.calls = std::move(calls);
		n.version = 0x5052455345000000ull + ++frame_serial_;
	}
	return nodes;
}

} // namespace rcg
