// Stage 122: the presentation layer (rcg/present.hpp, docs/TDD_27_PRESENTATION.md).
#include "rcg/present.hpp"

#include "rcg/session.hpp"

#include <algorithm>
#include <cmath>

namespace rcg {

namespace region_field {

namespace {

using rc::vis::Edge;
using rc::vis::EdgeKind;

constexpr double TAU = 6.283185307179586;
constexpr int GRID = 720;        // the rays every region is sampled along (half a degree apart)
constexpr double INSET = 1e-6;   // a cell's edges are sampled this far inside it

double wrap(double a) {
	a = std::fmod(a, TAU);
	return a < 0.0 ? a + TAU : a;
}
double angle_of(const Pt &d) { return wrap(std::atan2(d.y, d.x)); }
Pt lerp(const Pt &a, const Pt &b, double e) { return Pt{a.x + (b.x - a.x) * e, a.y + (b.y - a.y) * e}; }
double dist(const Pt &a, const Pt &b) { return std::hypot(a.x - b.x, a.y - b.y); }

struct Seg {
	Pt a, b;
	const Edge *edge;
};

struct CellGeom {
	std::vector<Seg> segs; // every loop, closed
	double lo = 0.0, span = 0.0; // the rays through it: [lo, lo + span] (wrapped)
	int index = 0;
	bool has(double phi) const { return wrap(phi - lo) <= span; }
};

bool inside(const CellGeom &c, const Pt &q) { // even-odd
	bool in = false;
	for (const Seg &s : c.segs) {
		if ((s.a.y > q.y) != (s.b.y > q.y)) {
			const double x = s.a.x + (q.y - s.a.y) / (s.b.y - s.a.y) * (s.b.x - s.a.x);
			if (x > q.x) {
				in = !in;
			}
		}
	}
	return in;
}

End end_on(const Edge *e, const Pt &p) {
	End out;
	out.p = p;
	if (e->kind == EdgeKind::Carrier) {
		out.kind = End::Carrier;
		out.surface = e->surface;
		out.circle = !e->gline;
		out.center = e->gc;
		out.radius = e->gr;
	} else {
		out.kind = End::Bounds;
	}
	return out;
}

// the ray apex + s u, s in [enter, exit], inside the rectangle
bool ray_in(const rc::vis::Rect &r, const Pt &apex, const Pt &u, double &enter, double &exit) {
	enter = 0.0;
	exit = INFINITY;
	const double o[2] = {apex.x, apex.y}, d[2] = {u.x, u.y}, lo[2] = {r.x0, r.y0}, hi[2] = {r.x1, r.y1};
	for (int k = 0; k < 2; k++) {
		if (std::abs(d[k]) < 1e-15) {
			if (o[k] < lo[k] || o[k] > hi[k]) {
				return false;
			}
			continue;
		}
		double t0 = (lo[k] - o[k]) / d[k], t1 = (hi[k] - o[k]) / d[k];
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		enter = std::max(enter, t0);
		exit = std::min(exit, t1);
	}
	return exit > enter;
}

// the spans of one cell along the ray at phi
void cast(const CellGeom &c, const Pt &apex, double phi, const rc::vis::Rect &domain, std::vector<Span> &out) {
	const Pt u{std::cos(phi), std::sin(phi)};
	struct Hit {
		double s;
		const Edge *edge;
	};
	std::vector<Hit> hits;
	for (const Seg &sg : c.segs) {
		if (sg.edge->kind == EdgeKind::Member) {
			continue; // (a ray inside the cell meets its members only at the apex)
		}
		const Pt d{sg.b.x - sg.a.x, sg.b.y - sg.a.y};
		const double den = rc::vis::cross(u, d);
		if (std::abs(den) < 1e-15) {
			continue;
		}
		const Pt ao{sg.a.x - apex.x, sg.a.y - apex.y};
		const double s = rc::vis::cross(ao, d) / den, w = rc::vis::cross(ao, u) / den;
		if (s > 1e-9 && w >= 0.0 && w <= 1.0) {
			hits.push_back(Hit{s, sg.edge});
		}
	}
	std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.s < b.s; });
	// a vertex between two segments is one crossing
	std::vector<Hit> uniq;
	for (const Hit &h : hits) {
		if (uniq.empty() || h.s - uniq.back().s > 1e-7 * std::max(1.0, h.s)) {
			uniq.push_back(h);
		}
	}
	double enter = 0.0, exit = 0.0;
	if (!ray_in(domain, apex, u, enter, exit)) {
		return;
	}
	const double first = uniq.empty() ? std::min(exit, 1e6) : uniq.front().s;
	bool in = inside(c, Pt{apex.x + u.x * first * 0.5, apex.y + u.y * first * 0.5});
	double from = 0.0;
	const Edge *from_edge = nullptr;
	int ordinal = 0;
	auto emit = [&](double s0, const Edge *e0, double s1, const Edge *e1) {
		const double a = std::max(s0, enter), b = std::min(s1, exit);
		if (b <= a) {
			return;
		}
		Span sp;
		const Pt pa{apex.x + u.x * a, apex.y + u.y * a}, pb{apex.x + u.x * b, apex.y + u.y * b};
		if (a > s0 || (e0 == nullptr && s0 > 0.0)) {
			sp.near.kind = End::Bounds;
			sp.near.p = pa;
		} else if (e0 == nullptr) {
			sp.near.kind = End::Apex;
			sp.near.p = pa;
		} else {
			sp.near = end_on(e0, pa);
		}
		if (b < s1 || e1 == nullptr) {
			sp.far.kind = End::Bounds;
			sp.far.p = pb;
		} else {
			sp.far = end_on(e1, pb);
		}
		sp.id = int64_t(c.index) * 64 + ordinal++;
		out.push_back(sp);
	};
	for (const Hit &h : uniq) {
		if (in) {
			emit(from, from_edge, h.s, h.edge);
		}
		in = !in;
		from = h.s;
		from_edge = h.edge;
	}
	if (in) {
		emit(from, from_edge, INFINITY, nullptr);
	}
}

void sort_spans(std::vector<Span> &v, const Pt &apex) {
	std::stable_sort(v.begin(), v.end(), [&](const Span &a, const Span &b) { return dist(a.near.p, apex) < dist(b.near.p, apex); });
}

bool same_place(const End &a, const End &b) {
	return a.kind == b.kind && (a.kind != End::Carrier || a.surface == b.surface);
}

// an end the way from a to b: along the surface they share (a circle's ends
// round the circle), else straight
End move(const End &a, const End &b, double e) {
	End out = e < 0.5 ? a : b;
	if (a.kind == End::Carrier && b.kind == End::Carrier && a.surface == b.surface && a.circle && b.circle) {
		const Pt c = lerp(a.center, b.center, e);
		const double r0 = dist(a.p, a.center), r1 = dist(b.p, b.center);
		const double t0 = std::atan2(a.p.y - a.center.y, a.p.x - a.center.x);
		double dt = std::atan2(b.p.y - b.center.y, b.p.x - b.center.x) - t0;
		dt = std::remainder(dt, TAU);
		const double t = t0 + dt * e, r = r0 + (r1 - r0) * e;
		out.p = Pt{c.x + r * std::cos(t), c.y + r * std::sin(t)};
		return out;
	}
	out.p = lerp(a.p, b.p, e);
	return out;
}

int64_t pair_id(int64_t a, int64_t b) {
	uint64_t h = uint64_t(a + 1) * 0x9E3779B97F4A7C15ull;
	h ^= uint64_t(b + 1) + 0x7F4A7C159E3779B9ull + (h << 6) + (h >> 2);
	return int64_t(h >> 1);
}

// the pairs of spans along one ray, in order, most surfaces in common (a span
// left unpaired grows or shrinks)
std::vector<std::pair<int, int>> align(const std::vector<Span> &A, const std::vector<Span> &B) {
	const size_t n = A.size(), m = B.size();
	std::vector<std::vector<int>> best(n + 1, std::vector<int>(m + 1, 0));
	auto score = [&](size_t i, size_t j) {
		return 1 + (same_place(A[i].near, B[j].near) ? 2 : 0) + (same_place(A[i].far, B[j].far) ? 2 : 0);
	};
	for (size_t i = n; i-- > 0;) {
		for (size_t j = m; j-- > 0;) {
			best[i][j] = std::max({best[i + 1][j], best[i][j + 1], best[i + 1][j + 1] + score(i, j)});
		}
	}
	std::vector<std::pair<int, int>> out;
	size_t i = 0, j = 0;
	while (i < n || j < m) {
		if (i < n && j < m && best[i][j] == best[i + 1][j + 1] + score(i, j)) {
			out.push_back({int(i++), int(j++)});
		} else if (i < n && (j == m || best[i][j] == best[i + 1][j])) {
			out.push_back({int(i++), -1});
		} else {
			out.push_back({-1, int(j++)});
		}
	}
	return out;
}

} // namespace

Field field_of(const rc::vis::Result &r) {
	Field f;
	if (!r.supported || r.cells.empty()) {
		f.ok = true;
		return f;
	}
	if (r.pk_none) {
		return f;
	}
	f.apex = r.pk;
	std::vector<CellGeom> cells;
	for (size_t ci = 0; ci < r.cells.size(); ci++) {
		const rc::vis::Cell &cell = r.cells[ci];
		CellGeom g;
		g.index = int(ci);
		for (const auto &loop : cell.loops) {
			std::vector<std::pair<Pt, const Edge *>> ring;
			for (const Edge &e : loop) {
				if (e.kind == EdgeKind::Member && e.arc) {
					return Field{}; // a pencil of circles: not sampled along rays
				}
				std::vector<Pt> pts;
				rc::vis::tessellate(e, pts);
				for (const Pt &p : pts) {
					ring.push_back({p, &e});
				}
			}
			for (size_t k = 0; k < ring.size(); k++) {
				g.segs.push_back(Seg{ring[k].first, ring[(k + 1) % ring.size()].first, ring[k].second});
			}
		}
		// the rays through it: round the ray to its inside point, every corner's offset
		const Pt in{cell.inside.x - f.apex.x, cell.inside.y - f.apex.y};
		if (std::hypot(in.x, in.y) < 1e-9) {
			return Field{};
		}
		const double mid = angle_of(in);
		double lo = 0.0, hi = 0.0;
		for (const Seg &s : g.segs) {
			const Pt d{s.a.x - f.apex.x, s.a.y - f.apex.y};
			if (std::hypot(d.x, d.y) < 1e-6) {
				continue;
			}
			const double off = std::remainder(angle_of(d) - mid, TAU);
			lo = std::min(lo, off);
			hi = std::max(hi, off);
		}
		if (hi - lo >= TAU - 1e-6) {
			return Field{}; // round the apex: not a wedge
		}
		g.lo = wrap(mid + lo);
		g.span = hi - lo;
		cells.push_back(std::move(g));
	}
	std::vector<double> phis;
	for (int k = 0; k < GRID; k++) {
		phis.push_back(TAU * k / GRID);
	}
	for (const CellGeom &g : cells) {
		if (g.span > 2 * INSET) {
			phis.push_back(wrap(g.lo + INSET));
			phis.push_back(wrap(g.lo + g.span - INSET));
		}
	}
	std::sort(phis.begin(), phis.end());
	phis.erase(std::unique(phis.begin(), phis.end()), phis.end());
	for (double phi : phis) {
		Sample s;
		s.phi = phi;
		for (const CellGeom &g : cells) {
			if (g.has(phi)) {
				cast(g, f.apex, phi, r.domain, s.spans);
			}
		}
		sort_spans(s.spans, f.apex);
		f.empty = f.empty && s.spans.empty();
		f.samples.push_back(std::move(s));
	}
	f.ok = true;
	return f;
}

std::vector<Span> spans_at(const Field &f, double phi) {
	const size_t n = f.samples.size();
	if (n == 0) {
		return {};
	}
	phi = wrap(phi);
	auto it = std::upper_bound(f.samples.begin(), f.samples.end(), phi, [](double v, const Sample &s) { return v < s.phi; });
	const size_t k1 = size_t(it - f.samples.begin()) % n;
	const size_t k = (k1 + n - 1) % n;
	const Sample &a = f.samples[k], &b = f.samples[k1];
	if (std::abs(a.phi - phi) < 1e-12) {
		return a.spans;
	}
	const double gap = wrap(b.phi - a.phi), w = gap > 0.0 ? wrap(phi - a.phi) / gap : 0.0;
	std::vector<Span> out;
	for (const Span &sa : a.spans) {
		for (const Span &sb : b.spans) {
			if (sb.id == sa.id) {
				Span sp = sa;
				sp.near.p = lerp(sa.near.p, sb.near.p, w);
				sp.far.p = lerp(sa.far.p, sb.far.p, w);
				out.push_back(sp);
				break;
			}
		}
	}
	return out;
}

Field morph(const Field &a, const Field &b, double e) {
	Field out;
	out.ok = true;
	out.apex = a.empty ? b.apex : b.empty ? a.apex : lerp(a.apex, b.apex, e);
	std::vector<double> phis;
	for (const Sample &s : a.samples) {
		phis.push_back(s.phi);
	}
	for (const Sample &s : b.samples) {
		phis.push_back(s.phi);
	}
	std::sort(phis.begin(), phis.end());
	phis.erase(std::unique(phis.begin(), phis.end()), phis.end());
	for (double phi : phis) {
		const std::vector<Span> A = spans_at(a, phi), B = spans_at(b, phi);
		Sample s;
		s.phi = phi;
		for (const auto &[pi, pj] : align(A, B)) {
			const size_t i = size_t(std::max(pi, 0)), j = size_t(std::max(pj, 0));
			Span sp;
			if (pi >= 0 && pj >= 0) {
				sp.near = move(A[i].near, B[j].near, e);
				sp.far = move(A[i].far, B[j].far, e);
				sp.id = pair_id(A[i].id, B[j].id);
			} else if (pj >= 0) { // born: out of its near end
				sp.near = B[j].near;
				sp.far = B[j].far;
				sp.far.p = lerp(B[j].near.p, B[j].far.p, e);
				sp.id = pair_id(-2, B[j].id);
			} else { // dying: into its near end
				sp.near = A[i].near;
				sp.far = A[i].far;
				sp.far.p = lerp(A[i].far.p, A[i].near.p, e);
				sp.id = pair_id(A[i].id, -2);
			}
			s.spans.push_back(sp);
		}
		out.empty = out.empty && s.spans.empty();
		out.samples.push_back(std::move(s));
	}
	return out;
}

void fill_of(const Field &f, std::vector<rc::V2> &pts, std::vector<int> &indices) {
	const size_t n = f.samples.size();
	if (n < 2) {
		return;
	}
	for (size_t k = 0; k < n; k++) {
		const Sample &a = f.samples[k], &b = f.samples[(k + 1) % n];
		if (wrap(b.phi - a.phi) > 0.05) {
			continue; // (never between samples this far apart)
		}
		for (const Span &sa : a.spans) {
			for (const Span &sb : b.spans) {
				if (sb.id != sa.id) {
					continue;
				}
				const int base = int(pts.size());
				pts.push_back(rc::V2::of(sa.near.p.x, sa.near.p.y));
				pts.push_back(rc::V2::of(sa.far.p.x, sa.far.p.y));
				pts.push_back(rc::V2::of(sb.far.p.x, sb.far.p.y));
				pts.push_back(rc::V2::of(sb.near.p.x, sb.near.p.y));
				indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
				break;
			}
		}
	}
}

void clip_disk(const std::vector<rc::V2> &pts, const std::vector<int> &indices, const Pt &c, double r, std::vector<rc::V2> &out_pts,
	std::vector<int> &out_indices, int sides) {
	if (r <= 0.0) {
		return;
	}
	std::vector<Pt> disk;
	for (int k = 0; k < sides; k++) {
		const double a = TAU * k / sides;
		disk.push_back(Pt{c.x + r * std::cos(a), c.y + r * std::sin(a)});
	}
	std::vector<Pt> poly, next;
	for (size_t t = 0; t + 2 < indices.size(); t += 3) {
		poly.clear();
		bool all_in = true;
		for (int k = 0; k < 3; k++) {
			const rc::V2 &v = pts[size_t(indices[t + size_t(k)])];
			poly.push_back(Pt{double(v.x), double(v.y)});
			all_in = all_in && std::hypot(double(v.x) - c.x, double(v.y) - c.y) <= r * std::cos(TAU / sides / 2);
		}
		if (!all_in) {
			for (int k = 0; k < sides && !poly.empty(); k++) {
				const Pt &p0 = disk[size_t(k)], &p1 = disk[size_t((k + 1) % sides)];
				const Pt d{p1.x - p0.x, p1.y - p0.y};
				auto side = [&](const Pt &q) { return rc::vis::cross(d, Pt{q.x - p0.x, q.y - p0.y}); };
				next.clear();
				for (size_t i = 0; i < poly.size(); i++) {
					const Pt &q0 = poly[i], &q1 = poly[(i + 1) % poly.size()];
					const double s0 = side(q0), s1 = side(q1);
					if (s0 >= 0.0) {
						next.push_back(q0);
					}
					if ((s0 >= 0.0) != (s1 >= 0.0)) {
						next.push_back(lerp(q0, q1, s0 / (s0 - s1)));
					}
				}
				poly.swap(next);
			}
		}
		if (poly.size() < 3) {
			continue;
		}
		const int base = int(out_pts.size());
		for (const Pt &q : poly) {
			out_pts.push_back(rc::V2::of(q.x, q.y));
		}
		for (int i = 1; i + 1 < int(poly.size()); i++) {
			out_indices.insert(out_indices.end(), {base, base + i, base + i + 1});
		}
	}
}

} // namespace region_field

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
	using region_field::Field;
	for (render::FrameNode &n : nodes) {
		if (n.key.kind == render::NodeKind::Player) {
			n.position = player(s);
		}
		if (n.key.kind != render::NodeKind::Visibility) {
			continue;
		}
		const render::Call *mesh = mesh_of(n);
		static const std::vector<rc::V2> none_pts;
		static const std::vector<int> none_idx;
		const std::vector<rc::V2> &target_pts = mesh ? mesh->pts : none_pts;
		const std::vector<int> &target_idx = mesh ? mesh->indices : none_idx;
		const int version = s.visibility_version();
		if (version != region_version_) {
			Field next = s.region() ? region_field::field_of(*s.region()) : Field{true, true, {}, {}};
			if (config_.region_seconds > 0.0 && region_version_ >= 0) {
				Morph m;
				const Field &from = morph_ ? current_ : shown_;
				const bool from_ok = morph_ ? current_ok_ : shown_.ok;
				const bool match = from_ok && next.ok &&
								   (from.empty || next.empty || std::hypot(from.apex.x - next.apex.x, from.apex.y - next.apex.y) <= config_.match_distance);
				if (match) {
					m.from = from;
					m.to = next;
				} else {
					m.disk = true;
					m.from_pts = shown_pts_;
					m.from_idx = shown_idx_;
				}
				m.start = now_;
				morph_ = std::move(m);
			} else {
				shown_pts_ = target_pts;
				shown_idx_ = target_idx;
			}
			region_version_ = version;
			shown_ = std::move(next);
		}
		if (morph_) {
			const double x = (now_ - morph_->start) / config_.region_seconds;
			if (x >= 1.0) {
				morph_.reset(); // the exact region
				shown_pts_ = target_pts;
				shown_idx_ = target_idx;
			} else {
				std::vector<rc::V2> pts;
				std::vector<int> idx;
				if (!morph_->disk) {
					current_ = region_field::morph(morph_->from, morph_->to, ease_out(x));
					current_ok_ = true;
					region_field::fill_of(current_, pts, idx);
				} else {
					current_ = Field{};
					current_ok_ = false;
					const rc::V2 c = player(s);
					const region_field::Pt pc{double(c.x), double(c.y)};
					// a mesh's reach from the player: its nearest and farthest points (the disk sweeps between)
					auto reach = [&](const std::vector<rc::V2> &v, double &lo, double &hi) {
						lo = INFINITY;
						hi = 0.0;
						for (const rc::V2 &q : v) {
							const double d = std::hypot(double(q.x) - pc.x, double(q.y) - pc.y);
							lo = std::min(lo, d);
							hi = std::max(hi, d);
						}
						lo = std::isfinite(lo) ? lo : 0.0;
					};
					const double share = std::clamp(config_.collapse_share, 0.0, 1.0);
					double lo = 0.0, hi = 0.0;
					if (x < share) {
						reach(morph_->from_pts, lo, hi);
						region_field::clip_disk(morph_->from_pts, morph_->from_idx, pc, hi - (hi - lo) * ease_out(x / share), pts, idx);
					} else {
						reach(target_pts, lo, hi);
						region_field::clip_disk(target_pts, target_idx, pc, lo + (hi - lo) * ease_out((x - share) / std::max(1e-9, 1.0 - share)), pts, idx);
					}
				}
				shown_pts_ = pts;
				shown_idx_ = idx;
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
		}
	}
	return nodes;
}

} // namespace rcg
