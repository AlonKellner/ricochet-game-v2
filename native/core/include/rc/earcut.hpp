// Polygon triangulation with holes by ear clipping (the earcut algorithm:
// https://github.com/mapbox/earcut, ISC License, Copyright (c) 2016 Mapbox).
// A C++ rendering of the algorithm for the visibility region's fill (Stage 110):
// holes are bridged into the outer ring, ears are clipped, and the two fallback
// passes (curing local self-intersections, splitting the polygon) handle the
// degenerate rings tessellated arcs can produce. No z-order hashing: the rings
// here hold a few thousand points at most.
//
// ISC License: Permission to use, copy, modify, and/or distribute this software
// for any purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies. THE SOFTWARE
// IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace rc {
namespace earcut {

struct XY {
	double x = 0.0;
	double y = 0.0;
};

class Triangulator {
public:
	// rings[0] is the outer ring, the others its holes (either orientation);
	// returns triangles as index triples into the rings' points, numbered
	// consecutively ring after ring.
	std::vector<uint32_t> run(const std::vector<std::vector<XY>> &rings) {
		indices_.clear();
		nodes_.clear();
		if (rings.empty() || rings[0].size() < 3) {
			return indices_;
		}
		uint32_t start = 0;
		Node *outer = linked_list(rings[0], start, true);
		start += uint32_t(rings[0].size());
		if (outer == nullptr || outer->next == outer->prev) {
			return indices_;
		}
		if (rings.size() > 1) {
			std::vector<Node *> queue;
			for (size_t h = 1; h < rings.size(); h++) {
				Node *list = linked_list(rings[h], start, false);
				start += uint32_t(rings[h].size());
				if (list == nullptr) {
					continue;
				}
				if (list == list->next) {
					list->steiner = true;
				}
				queue.push_back(leftmost(list));
			}
			// by x; holes whose leftmost points meet are ordered counterclockwise
			// so each bridges at the point where they meet (compareXYSlope)
			std::sort(queue.begin(), queue.end(), [](const Node *a, const Node *b) {
				if (a->x != b->x) {
					return a->x < b->x;
				}
				if (a->y != b->y) {
					return a->y < b->y;
				}
				return (a->next->y - a->y) / (a->next->x - a->x) < (b->next->y - b->y) / (b->next->x - b->x);
			});
			for (Node *h : queue) {
				outer = eliminate_hole(h, outer);
			}
		}
		earcut_linked(outer, 0);
		return indices_;
	}

private:
	struct Node {
		uint32_t i = 0;
		double x = 0.0;
		double y = 0.0;
		Node *prev = nullptr;
		Node *next = nullptr;
		bool steiner = false;
	};
	std::deque<Node> nodes_; // stable addresses
	std::vector<uint32_t> indices_;

	Node *create(uint32_t i, double x, double y) {
		nodes_.push_back(Node{i, x, y});
		return &nodes_.back();
	}

	Node *insert(uint32_t i, double x, double y, Node *last) {
		Node *p = create(i, x, y);
		if (last == nullptr) {
			p->prev = p;
			p->next = p;
		} else {
			p->next = last->next;
			p->prev = last;
			last->next->prev = p;
			last->next = p;
		}
		return p;
	}

	static void remove(Node *p) {
		p->next->prev = p->prev;
		p->prev->next = p->next;
	}

	static double signed_area(const std::vector<XY> &ring) {
		double sum = 0.0;
		for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
			sum += (ring[j].x - ring[i].x) * (ring[i].y + ring[j].y);
		}
		return sum;
	}

	Node *linked_list(const std::vector<XY> &ring, uint32_t start, bool clockwise) {
		Node *last = nullptr;
		if (ring.size() < 3) {
			return nullptr;
		}
		if (clockwise == (signed_area(ring) > 0)) {
			for (size_t k = 0; k < ring.size(); k++) {
				last = insert(start + uint32_t(k), ring[k].x, ring[k].y, last);
			}
		} else {
			for (size_t k = ring.size(); k-- > 0;) {
				last = insert(start + uint32_t(k), ring[k].x, ring[k].y, last);
			}
		}
		if (last != nullptr && equals(last, last->next)) {
			remove(last);
			last = last->next;
		}
		return last;
	}

	static bool equals(const Node *a, const Node *b) { return a->x == b->x && a->y == b->y; }

	static double area(const Node *p, const Node *q, const Node *r) { return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y); }

	static bool point_in_triangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
		return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py)
			&& (bx - px) * (cy - py) >= (cx - px) * (by - py);
	}

	// Removes duplicate and collinear points.
	static Node *filter_points(Node *start, Node *end = nullptr) {
		if (start == nullptr) {
			return start;
		}
		if (end == nullptr) {
			end = start;
		}
		Node *p = start;
		bool again;
		do {
			again = false;
			if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0.0)) {
				remove(p);
				p = end = p->prev;
				if (p == p->next) {
					break;
				}
				again = true;
			} else {
				p = p->next;
			}
		} while (again || p != end);
		return end;
	}

	static bool is_ear(const Node *ear) {
		const Node *a = ear->prev;
		const Node *b = ear;
		const Node *c = ear->next;
		if (area(a, b, c) >= 0.0) {
			return false; // reflex
		}
		const double x0 = std::min({a->x, b->x, c->x}), y0 = std::min({a->y, b->y, c->y});
		const double x1 = std::max({a->x, b->x, c->x}), y1 = std::max({a->y, b->y, c->y});
		for (const Node *p = c->next; p != a; p = p->next) {
			// (a point at the ear's first corner does not block it: earcut's
			// pointInTriangleExceptFirst)
			if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && !equals(p, a)
				&& point_in_triangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) && area(p->prev, p, p->next) >= 0.0) {
				return false;
			}
		}
		return true;
	}

	void earcut_linked(Node *ear, int pass) {
		if (ear == nullptr) {
			return;
		}
		Node *stop = ear;
		while (ear->prev != ear->next) {
			Node *prev = ear->prev;
			Node *next = ear->next;
			if (is_ear(ear)) {
				indices_.push_back(prev->i);
				indices_.push_back(ear->i);
				indices_.push_back(next->i);
				remove(ear);
				ear = next->next;
				stop = next->next;
				continue;
			}
			ear = next;
			if (ear == stop) {
				if (pass == 0) {
					earcut_linked(filter_points(ear), 1);
				} else if (pass == 1) {
					ear = cure_local_intersections(filter_points(ear));
					earcut_linked(ear, 2);
				} else {
					split_earcut(ear);
				}
				break;
			}
		}
	}

	Node *cure_local_intersections(Node *start) {
		Node *p = start;
		do {
			Node *a = p->prev;
			Node *b = p->next->next;
			if (!equals(a, b) && intersects(a, p, p->next, b) && locally_inside(a, b) && locally_inside(b, a)) {
				indices_.push_back(a->i);
				indices_.push_back(p->i);
				indices_.push_back(b->i);
				remove(p);
				remove(p->next);
				p = start = b;
			}
			p = p->next;
		} while (p != start);
		return filter_points(p);
	}

	void split_earcut(Node *start) {
		Node *a = start;
		do {
			Node *b = a->next->next;
			while (b != a->prev) {
				if (a->i != b->i && is_valid_diagonal(a, b)) {
					Node *c = split_polygon(a, b);
					a = filter_points(a, a->next);
					c = filter_points(c, c->next);
					earcut_linked(a, 0);
					earcut_linked(c, 0);
					return;
				}
				b = b->next;
			}
			a = a->next;
		} while (a != start);
	}

	Node *eliminate_hole(Node *hole, Node *outer) {
		Node *bridge = find_hole_bridge(hole, outer);
		if (bridge == nullptr) {
			return outer;
		}
		Node *bridge_reverse = split_polygon(bridge, hole);
		filter_points(bridge_reverse, bridge_reverse->next);
		return filter_points(bridge, bridge->next);
	}

	static Node *find_hole_bridge(Node *hole, Node *outer) {
		Node *p = outer;
		const double hx = hole->x;
		const double hy = hole->y;
		double qx = -std::numeric_limits<double>::infinity();
		Node *m = nullptr;
		// the hole touching the outer ring at a vertex bridges there
		if (equals(hole, p)) {
			return p;
		}
		// the segment left of the hole's leftmost point that its ray crosses nearest
		do {
			if (equals(hole, p->next)) {
				return p->next;
			}
			if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
				const double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
				if (x <= hx && x > qx) {
					qx = x;
					m = p->x < p->next->x ? p : p->next;
					if (x == hx) {
						return m; // the hole touches the outer segment
					}
				}
			}
			p = p->next;
		} while (p != outer);
		if (m == nullptr) {
			return nullptr;
		}
		// the vertex inside the triangle (hole point, crossing, m) with the
		// smallest angle to the ray, if any
		const Node *stop = m;
		const double mx = m->x;
		const double my = m->y;
		double tan_min = std::numeric_limits<double>::infinity();
		p = m;
		do {
			if (hx >= p->x && p->x >= mx && hx != p->x
				&& point_in_triangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
				const double tan = std::abs(hy - p->y) / (hx - p->x);
				if (locally_inside(p, hole)
					&& (tan < tan_min || (tan == tan_min && (p->x > m->x || (p->x == m->x && sector_contains_sector(m, p)))))) {
					m = p;
					tan_min = tan;
				}
			}
			p = p->next;
		} while (p != stop);
		return m;
	}

	static bool sector_contains_sector(const Node *m, const Node *p) { return area(m->prev, m, p->prev) < 0.0 && area(p->next, m, m->next) < 0.0; }

	static Node *leftmost(Node *start) {
		Node *p = start;
		Node *l = start;
		do {
			if (p->x < l->x || (p->x == l->x && p->y < l->y)) {
				l = p;
			}
			p = p->next;
		} while (p != start);
		return l;
	}

	static bool is_valid_diagonal(const Node *a, const Node *b) {
		return a->next->i != b->i && a->prev->i != b->i && !intersects_polygon(a, b)
			&& ((locally_inside(a, b) && locally_inside(b, a) && middle_inside(a, b) && (area(a->prev, a, b->prev) != 0.0 || area(a, b->prev, b) != 0.0))
				|| (equals(a, b) && area(a->prev, a, a->next) > 0.0 && area(b->prev, b, b->next) > 0.0));
	}

	static int sign(double v) { return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0); }

	static bool on_segment(const Node *p, const Node *q, const Node *r) {
		return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) && q->y <= std::max(p->y, r->y) && q->y >= std::min(p->y, r->y);
	}

	static bool intersects(const Node *p1, const Node *q1, const Node *p2, const Node *q2) {
		const int o1 = sign(area(p1, q1, p2));
		const int o2 = sign(area(p1, q1, q2));
		const int o3 = sign(area(p2, q2, p1));
		const int o4 = sign(area(p2, q2, q1));
		if (o1 != o2 && o3 != o4) {
			return true;
		}
		if (o1 == 0 && on_segment(p1, p2, q1)) return true;
		if (o2 == 0 && on_segment(p1, q2, q1)) return true;
		if (o3 == 0 && on_segment(p2, p1, q2)) return true;
		if (o4 == 0 && on_segment(p2, q1, q2)) return true;
		return false;
	}

	static bool intersects_polygon(const Node *a, const Node *b) {
		const Node *p = a;
		do {
			if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) {
				return true;
			}
			p = p->next;
		} while (p != a);
		return false;
	}

	static bool locally_inside(const Node *a, const Node *b) {
		return area(a->prev, a, a->next) < 0.0 ? area(a, b, a->next) >= 0.0 && area(a, a->prev, b) >= 0.0
											   : area(a, b, a->prev) < 0.0 || area(a, a->next, b) < 0.0;
	}

	static bool middle_inside(const Node *a, const Node *b) {
		const Node *p = a;
		bool inside = false;
		const double px = (a->x + b->x) / 2.0;
		const double py = (a->y + b->y) / 2.0;
		do {
			if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y && (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x)) {
				inside = !inside;
			}
			p = p->next;
		} while (p != a);
		return inside;
	}

	// Links a and b with a bridge: the polygon splits in two (or a hole joins).
	Node *split_polygon(Node *a, Node *b) {
		Node *a2 = create(a->i, a->x, a->y);
		Node *b2 = create(b->i, b->x, b->y);
		Node *an = a->next;
		Node *bp = b->prev;
		a->next = b;
		b->prev = a;
		a2->next = an;
		an->prev = a2;
		b2->next = a2;
		a2->prev = b2;
		bp->next = b2;
		b2->prev = bp;
		return b2;
	}
};

} // namespace earcut
} // namespace rc
