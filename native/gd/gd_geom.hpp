// Bindings of the geometry, effect and exact kernels (Stages 91-92): HPBeamCircle,
// BeamGeomSeg, BeamGeom, BeamEffects, Exact. Flat registration for the
// GDScript inner classes (HPBeam.Circle, BeamGeom.Seg). Exact homogeneous
// triples are Arrays of three BigFloat NNums; [value, bound] pairs are Arrays
// of two floats, as in GDScript.
#pragma once

#include "gd_scene.hpp"
#include "gd_solver.hpp"
#include "rc/beam_effects.hpp"
#include "rc/beam_geom.hpp"
#include "rc/exact.hpp"

namespace rcgd {
using CircleV = std::variant<rc::Circle<rc::F64>, rc::Circle<rc::Dual>, rc::Circle<rc::BigFloat>>;
using SegV = std::variant<rc::Seg<rc::F64>, rc::Seg<rc::Dual>, rc::Seg<rc::BigFloat>>;
} // namespace rcgd

class HPBeamCircle : public godot::RefCounted {
	GDCLASS(HPBeamCircle, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::CircleV circle = rc::Circle<rc::F64>{};

	static godot::Ref<HPBeamCircle> wrap(rcgd::CircleV c);
	static godot::Ref<HPBeamCircle> make(int id, const godot::Ref<CN> &c, const godot::Ref<Num> &r);
	int get_id() const;
	godot::Ref<CN> get_c() const;
	godot::Ref<Num> get_r() const;
	godot::Ref<Num> get_r2() const;
};

class BeamGeomSeg : public godot::RefCounted {
	GDCLASS(BeamGeomSeg, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::SegV seg = rc::Seg<rc::F64>{};

	static godot::Ref<BeamGeomSeg> wrap(rcgd::SegV s);
	int get_id() const;
	int get_kind() const;
	bool get_full() const;
	godot::Ref<CN> get_s() const;
	godot::Ref<CN> get_e() const;
	godot::Variant get_v() const;
	godot::Variant get_circ() const;
	godot::Ref<CN> c() const;
};

class BeamGeom : public godot::RefCounted {
	GDCLASS(BeamGeom, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	enum Kind { LINE = 0, CIRCLE = 1 };

	static godot::Ref<BeamGeomSeg> seg_from_dict(const godot::Ref<NumCtx> &ctx, const godot::Dictionary &d);
	static godot::Ref<BeamGeomSeg> seg_from_segment(const godot::Ref<NumCtx> &ctx, const godot::Variant &segment, int sid);
	static godot::Ref<CN> circumcenter(const godot::Ref<CN> &A, const godot::Ref<CN> &B, const godot::Ref<CN> &C);
	static godot::Array hits(const godot::Ref<HPBeam> &beam, const godot::Ref<BeamGeomSeg> &seg, bool depart_on);
	static godot::Array hit_records(const godot::Ref<HPBeam> &beam, const godot::Ref<BeamGeomSeg> &seg, bool depart_on,
			const godot::Array &known = godot::Array());
	static godot::Ref<Num> on_segment_value(const godot::Ref<BeamGeomSeg> &seg, const godot::Ref<CN> &p);
	static bool on_segment(const godot::Ref<BeamGeomSeg> &seg, const godot::Ref<CN> &p);
	static bool eq(const godot::Ref<CN> &a, const godot::Ref<CN> &b);
	static int endpoint_at(const godot::Ref<BeamGeomSeg> &seg, const godot::Ref<CN> &p);
	static godot::Ref<CN> grad(const godot::Ref<BeamGeomSeg> &seg, const godot::Ref<CN> &p);
	static godot::Ref<Num> winding(const godot::Ref<BeamGeomSeg> &seg);
	static bool side_left(const godot::Ref<BeamGeomSeg> &seg, const godot::Ref<CN> &p, const godot::Ref<CN> &dir);
	static godot::Ref<CN> tangent_into(const godot::Ref<BeamGeomSeg> &seg, int which);
	static godot::Array blocked_sides(const godot::Ref<BeamGeomSeg> &seg, int which, const godot::Ref<CN> &dir);
};

VARIANT_ENUM_CAST(BeamGeom::Kind);

class BeamEffects : public godot::RefCounted {
	GDCLASS(BeamEffects, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<HPBeam> apply(const godot::Ref<HPBeam> &beam, const godot::Ref<Num> &t, const godot::Ref<BeamSceneSurf> &surf,
			const godot::String &etype, const godot::Ref<CN> &h = godot::Ref<CN>());
	static godot::Ref<HPBeam> portal(const godot::Ref<HPBeam> &beam, const godot::Ref<Num> &t, const godot::Ref<BeamSceneSurf> &surf,
			const godot::Ref<CN> &h_exact = godot::Ref<CN>());
	static godot::Ref<HPBeam> project(const godot::Ref<HPBeam> &beam, const godot::Ref<Num> &t, const godot::Ref<BeamSceneSurf> &surf, bool left,
			const godot::Ref<CN> &h_exact = godot::Ref<CN>());
	static godot::Ref<CN> projective_exit(const godot::Ref<BeamSceneSurf> &surf, bool left, const godot::Ref<CN> &h);
	static godot::Variant back_propagate(const godot::Ref<BeamSceneSurf> &surf, bool left, const godot::Ref<CN> &X, bool x_on_carrier = false,
			const godot::Variant &click = godot::Variant());
	static godot::Array back_propagate_candidates(const godot::Ref<BeamSceneSurf> &surf, bool left, const godot::Ref<CN> &X, bool x_on_carrier = false);
	static godot::Ref<CN> portal_point(const godot::Ref<CN> &z, const godot::Ref<BeamSceneSurf> &surf, bool inverse = false);
	static godot::Ref<CN> u2(const godot::Ref<BeamGeomSeg> &seg);
	static godot::Ref<HPBeam> reflect_line(const godot::Ref<HPBeam> &beam, const godot::Ref<Num> &t, const godot::Ref<BeamGeomSeg> &seg,
			const godot::Ref<CN> &h_exact = godot::Ref<CN>());
	static godot::Variant map_point(const godot::Variant &z, const godot::Ref<BeamSceneSurf> &surf, bool inverse = false);
	static godot::Array map_pole(const godot::Array &parent, const godot::Ref<BeamSceneSurf> &surf);
};

class Exact : public godot::RefCounted {
	GDCLASS(Exact, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static double U() { return rc::exact::U; }
	static godot::Ref<Num> of(double x);
	static godot::Ref<Num> mul(const godot::Ref<Num> &a, const godot::Ref<Num> &b);
	static godot::Ref<Num> add(const godot::Ref<Num> &a, const godot::Ref<Num> &b);
	static godot::Ref<Num> sub(const godot::Ref<Num> &a, const godot::Ref<Num> &b);
	static godot::Array emul(const godot::Array &x, const godot::Array &y);
	static godot::Array eadd(const godot::Array &x, const godot::Array &y);
	static godot::Array esub(const godot::Array &x, const godot::Array &y);
	static bool orient_zero(double ax, double ay, double bx, double by, double cx, double cy);
	static bool on_circle_zero(double cx, double cy, double r, double kx, double ky);
	static bool incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy);
	static godot::Array line_from_dir(double px, double py, double dx, double dy);
	static godot::Array line_through(double px, double py, double qx, double qy);
	static godot::Array reflect_point_h(const godot::Array &X, double sx, double sy, double ex, double ey);
	static godot::Variant invert_point_h(const godot::Array &X, const godot::Array &C, const godot::Ref<Num> &rw2);
	static godot::Array line_through_h(double px, double py, const godot::Array &X);
	static godot::Array circumcenter_h(double ax, double ay, double bx, double by, double cx, double cy);
	static bool on_line_h(const godot::Array &L, const godot::Array &X);
	static bool tangent_zero(const godot::Array &L, const godot::Array &C, const godot::Ref<Num> &rw2);
	static godot::Array reflect_line(const godot::Array &L, double sx, double sy, double ex, double ey);
};
