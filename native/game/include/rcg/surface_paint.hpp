// Stage 208: one surface's look (the port of SurfaceNode._draw: its sides'
// colours, bands, door posts, hit marks, designer marker, hover and plan
// overlays) and the draw primitives, shared by the session's draw list and the
// editor's canvas, so an element looks the same in both. In world units.
#pragma once

#include "rcg/build.hpp"
#include "rcg/geom.hpp"
#include "rcg/render.hpp"
#include "rcg/session.hpp"

#include <array>
#include <string>
#include <vector>

namespace rcg::paint {

using render::Call;
using render::Color;
using render::Kind;

V2 vs(const V2 &v, double s); // Vector2 * float (the scalar rounded first)
V2 rot90(const V2 &v);

Call line(const V2 &a, const V2 &b, const Color &c, double width);
Call arc(const V2 &center, double radius, double start, double end, int count, const Color &c, double width);
Call polygon(const std::array<V2, 3> &pts, const Color &c);
Call polygon_n(const std::vector<V2> &pts, const Color &c);
Call circle(const V2 &center, double radius, const Color &c);
Call text(const V2 &pos, const std::string &s, int alignment, int size, const Color &c);
Call stroke(Kind kind, std::vector<V2> pts, const Color &c, double width);
double wrapf(double value, double lo, double hi);

// A segment's outline: a line or an arc, dashed or solid.
void dashed(std::vector<Call> &out, const SegGeom &seg, const Color &color, double width);
void outline_seg(std::vector<Call> &out, const SegGeom &seg, const Color &color, double width, bool dash);

// A side's colour by what it does (SurfaceNode.config_color).
Color config_color(const BuiltSide *cfg);

// One piece of a surface (piece -1: the whole, its marks drawn) under the flags: its look, then the hover and
// plan overlays and the plan's labels (none given: none drawn).
void surface(std::vector<Call> &out, const BuiltSurface &s, const SegGeom &seg, int piece, const rc::Flags &flags,
		const std::vector<Overlay> &hover = {}, const std::vector<Overlay> &plan = {}, const std::vector<int> &plan_indices = {});

} // namespace rcg::paint
