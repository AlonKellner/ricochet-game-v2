#include "gd_visibility.hpp"

#include "gd_invariants.hpp"
#include "gd_tracer.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

using namespace godot;

namespace {

PackedVector2Array to_packed(const std::vector<rc::vis::Pt> &pts) {
	PackedVector2Array out;
	out.resize(int64_t(pts.size()));
	for (size_t i = 0; i < pts.size(); i++) {
		out.set(int64_t(i), Vector2(real_t(pts[i].x), real_t(pts[i].y)));
	}
	return out;
}

} // namespace

void VisibilityRegion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_supported"), &VisibilityRegion::is_supported);
	ClassDB::bind_method(D_METHOD("get_reason"), &VisibilityRegion::get_reason);
	ClassDB::bind_method(D_METHOD("cell_polygons"), &VisibilityRegion::cell_polygons);
	ClassDB::bind_method(D_METHOD("outlines"), &VisibilityRegion::outlines);
	ClassDB::bind_method(D_METHOD("whisker_polylines"), &VisibilityRegion::whisker_polylines);
	ClassDB::bind_method(D_METHOD("contains", "point"), &VisibilityRegion::contains);
	ClassDB::bind_method(D_METHOD("margin", "point"), &VisibilityRegion::margin);
	ClassDB::bind_method(D_METHOD("get_cast_count"), &VisibilityRegion::get_cast_count);
	ClassDB::bind_method(D_METHOD("get_cell_count"), &VisibilityRegion::get_cell_count);
	ClassDB::bind_method(D_METHOD("get_member_count"), &VisibilityRegion::get_member_count);
	ClassDB::bind_method(D_METHOD("get_domain"), &VisibilityRegion::get_domain);
	ClassDB::bind_method(D_METHOD("check"), &VisibilityRegion::check);
	ClassDB::bind_method(D_METHOD("notes"), &VisibilityRegion::notes);
}

String VisibilityRegion::get_reason() const { return String(region.reason.c_str()); }

Array VisibilityRegion::cell_polygons() const {
	Array out;
	for (const rc::vis::Cell &c : region.cells) {
		for (const auto &l : c.loops) {
			const std::vector<rc::vis::Pt> poly = rc::vis::clip_polygon(rc::vis::polyline(l), region.domain);
			if (poly.size() >= 3) {
				out.push_back(to_packed(poly));
			}
		}
	}
	return out;
}

Array VisibilityRegion::outlines() const {
	Array out;
	for (const rc::vis::Loop &l : region.loops) {
		out.push_back(to_packed(rc::vis::polyline(l.edges)));
	}
	return out;
}

Array VisibilityRegion::whisker_polylines() const {
	Array out;
	for (const rc::vis::Edge &e : region.whiskers) {
		std::vector<rc::vis::Pt> pts;
		rc::vis::tessellate(e, pts);
		pts.push_back(e.b);
		out.push_back(to_packed(pts));
	}
	return out;
}

int VisibilityRegion::contains(const Vector2 &p) const {
	return int(rc::vis::contains(region, rc::vis::Pt{p.x, p.y}));
}

double VisibilityRegion::margin(const Vector2 &p) const { return rc::vis::margin(region, rc::vis::Pt{p.x, p.y}); }

Rect2 VisibilityRegion::get_domain() const {
	const rc::vis::Rect &d = region.domain;
	return Rect2(real_t(d.x0), real_t(d.y0), real_t(d.x1 - d.x0), real_t(d.y1 - d.y0));
}

Array VisibilityRegion::check() const {
	rc::inv::VisInput in{&region, tracer.is_valid() ? &tracer->tracer : nullptr, region.player, plan, flags, bounds};
	return render_violations(rc::inv::check_visibility(in));
}

PackedStringArray VisibilityRegion::notes() const {
	PackedStringArray out;
	for (const std::string &n : region.notes) {
		out.push_back(String(n.c_str()));
	}
	return out;
}
