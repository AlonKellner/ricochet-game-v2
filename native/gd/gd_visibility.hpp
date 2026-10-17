// Stage 110: the visibility region (GAME_SPEC 15) for GDScript. A
// VisibilityRegion is computed by BeamTracer.visibility and holds the region's
// cells (drawn), boundary loops and whiskers, its membership test and its
// structure checks. It keeps the inputs it was computed from and a reference to
// the tracer (the tracer holds none back: no reference cycle).
#pragma once

#include "gd_tracer.hpp"
#include "rc/invariants.hpp"
#include "rc/visibility.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2.hpp>

class VisibilityRegion : public godot::RefCounted {
	GDCLASS(VisibilityRegion, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rc::vis::Result region;
	godot::Ref<BeamTracer> tracer;
	std::vector<rc::PlanItem> plan;
	rc::Flags flags;
	rc::vis::Rect bounds;

	bool is_supported() const { return region.supported; }
	godot::String get_reason() const;
	// the cells as polygons for drawing (tessellated, cut to the classified domain)
	godot::Array cell_polygons() const;
	// The region's fill (Stage 110, F8): its merged loops triangulated --
	// {points: PackedVector2Array, indices: PackedInt32Array}
	godot::Dictionary fill_triangles() const;
	// the boundary loops and the whiskers as polylines (closed loops repeat no point)
	godot::Array outlines() const;
	godot::Array whisker_polylines() const;
	// 1: inside, 0: outside, -1: not classified (outside the domain, or a degenerate aim)
	int contains(const godot::Vector2 &p) const;
	double margin(const godot::Vector2 &p) const;
	int get_cast_count() const { return region.casts; }
	int get_cell_count() const { return int(region.cells.size()); }
	int get_member_count() const { return int(region.members.size()); }
	godot::Rect2 get_domain() const;
	// the region's structure invariants (VIS-*), rendered
	godot::Array check() const;
	godot::PackedStringArray notes() const;
};
