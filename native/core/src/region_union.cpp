// Stage 123: the visibility region as the union of its cells (rc/region_union.hpp).
#include "rc/region_union.hpp"

#include "rc/earcut.hpp"

#include <clipper2/clipper.engine.h>

#include <functional>
#include <map>

namespace rc::vis {

namespace {

double signed_area_of(const std::vector<Pt> &ring) {
	double a = 0.0;
	for (size_t i = 0; i < ring.size(); i++) {
		const Pt &p = ring[i], &q = ring[(i + 1) % ring.size()];
		a += p.x * q.y - p.y * q.x;
	}
	return a / 2.0;
}

} // namespace

Outline union_outline(const Result &r) {
	using namespace fill_detail;
	Outline out;
	if (!r.supported || r.cells.empty()) {
		return out;
	}
	// every cell's ring as the fill makes it (conforming: a corner of one cell on
	// another's edge is a vertex of both), each turned to positive area
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
	Clipper2Lib::PathsD subjects;
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
			if (ring.size() < 3 || signed_area_of(ring) == 0.0) {
				continue;
			}
			if (signed_area_of(ring) < 0.0) {
				std::reverse(ring.begin(), ring.end());
			}
			Clipper2Lib::PathD path;
			path.reserve(ring.size());
			for (const Pt &q : ring) {
				path.push_back(Clipper2Lib::PointD(q.x, q.y));
			}
			subjects.push_back(std::move(path));
		}
	}
	if (subjects.empty()) {
		return out;
	}
	// the union: every point some cell covers (a positive winding)
	Clipper2Lib::ClipperD clipper(4);
	clipper.AddSubject(subjects);
	Clipper2Lib::PolyTreeD tree;
	clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::Positive, tree);
	std::function<void(const Clipper2Lib::PolyPathD &, int)> walk = [&](const Clipper2Lib::PolyPathD &node, int outer) {
		for (const auto &child : node) {
			std::vector<Pt> ring;
			for (const Clipper2Lib::PointD &q : child->Polygon()) {
				ring.push_back(Pt{q.x, q.y});
			}
			const bool hole = child->IsHole();
			if (ring.size() >= 3) {
				const double a = signed_area_of(ring);
				if ((hole && a > 0.0) || (!hole && a < 0.0)) {
					std::reverse(ring.begin(), ring.end());
				}
				out.rings.push_back(std::move(ring));
				out.parent.push_back(hole ? outer : -1);
			}
			walk(*child, hole ? outer : int(out.rings.size()) - 1);
		}
	};
	walk(tree, -1);
	return out;
}

Fill fill_of(const Outline &o) {
	Fill out;
	earcut::Triangulator tri;
	for (size_t k = 0; k < o.rings.size(); k++) {
		if (o.parent[k] != -1) {
			continue;
		}
		std::vector<const std::vector<Pt> *> order{&o.rings[k]};
		for (size_t h = 0; h < o.rings.size(); h++) {
			if (o.parent[h] == int(k)) {
				order.push_back(&o.rings[h]);
			}
		}
		std::vector<std::vector<earcut::XY>> rings;
		const uint32_t base = uint32_t(out.points.size());
		for (const std::vector<Pt> *ring : order) {
			std::vector<earcut::XY> xy;
			xy.reserve(ring->size());
			for (const Pt &q : *ring) {
				xy.push_back(earcut::XY{q.x, q.y});
				out.points.push_back(q);
			}
			rings.push_back(std::move(xy));
		}
		for (uint32_t i : tri.run(rings)) {
			out.indices.push_back(base + i);
		}
	}
	return out;
}

} // namespace rc::vis
