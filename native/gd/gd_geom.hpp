// Bindings of the geometry, effect and exact kernels (Stages 91-92): NHPBeamCircle,
// NBeamGeomSeg, NBeamGeom, NBeamEffects, NExact. Flat registration for the
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

class NHPBeamCircle : public godot::RefCounted {
	GDCLASS(NHPBeamCircle, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::CircleV circle = rc::Circle<rc::F64>{};

	static godot::Ref<NHPBeamCircle> wrap(rcgd::CircleV c);
	static godot::Ref<NHPBeamCircle> make(int id, const godot::Ref<NCN> &c, const godot::Ref<NNum> &r);
	int get_id() const;
	godot::Ref<NCN> get_c() const;
	godot::Ref<NNum> get_r() const;
	godot::Ref<NNum> get_r2() const;
};

class NBeamGeomSeg : public godot::RefCounted {
	GDCLASS(NBeamGeomSeg, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	rcgd::SegV seg = rc::Seg<rc::F64>{};

	static godot::Ref<NBeamGeomSeg> wrap(rcgd::SegV s);
	int get_id() const;
	int get_kind() const;
	bool get_full() const;
	godot::Ref<NCN> get_s() const;
	godot::Ref<NCN> get_e() const;
	godot::Variant get_v() const;
	godot::Variant get_circ() const;
	godot::Ref<NCN> c() const;
};

class NBeamGeom : public godot::RefCounted {
	GDCLASS(NBeamGeom, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	enum Kind { LINE = 0, CIRCLE = 1 };

	static godot::Ref<NBeamGeomSeg> seg_from_dict(const godot::Ref<NNumCtx> &ctx, const godot::Dictionary &d);
	static godot::Ref<NBeamGeomSeg> seg_from_segment(const godot::Ref<NNumCtx> &ctx, const godot::Variant &segment, int sid);
	static godot::Ref<NCN> circumcenter(const godot::Ref<NCN> &A, const godot::Ref<NCN> &B, const godot::Ref<NCN> &C);
	static godot::Array hits(const godot::Ref<NHPBeam> &beam, const godot::Ref<NBeamGeomSeg> &seg, bool depart_on);
	static godot::Array hit_records(const godot::Ref<NHPBeam> &beam, const godot::Ref<NBeamGeomSeg> &seg, bool depart_on,
			const godot::Array &known = godot::Array());
	static godot::Ref<NNum> on_segment_value(const godot::Ref<NBeamGeomSeg> &seg, const godot::Ref<NCN> &p);
	static bool on_segment(const godot::Ref<NBeamGeomSeg> &seg, const godot::Ref<NCN> &p);
	static bool eq(const godot::Ref<NCN> &a, const godot::Ref<NCN> &b);
	static int endpoint_at(const godot::Ref<NBeamGeomSeg> &seg, const godot::Ref<NCN> &p);
	static godot::Ref<NCN> grad(const godot::Ref<NBeamGeomSeg> &seg, const godot::Ref<NCN> &p);
	static godot::Ref<NNum> winding(const godot::Ref<NBeamGeomSeg> &seg);
	static bool side_left(const godot::Ref<NBeamGeomSeg> &seg, const godot::Ref<NCN> &p, const godot::Ref<NCN> &dir);
	static godot::Ref<NCN> tangent_into(const godot::Ref<NBeamGeomSeg> &seg, int which);
	static godot::Array blocked_sides(const godot::Ref<NBeamGeomSeg> &seg, int which, const godot::Ref<NCN> &dir);
};

VARIANT_ENUM_CAST(NBeamGeom::Kind);

class NBeamEffects : public godot::RefCounted {
	GDCLASS(NBeamEffects, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static godot::Ref<NHPBeam> apply(const godot::Ref<NHPBeam> &beam, const godot::Ref<NNum> &t, const godot::Ref<NBeamSceneSurf> &surf,
			const godot::String &etype, const godot::Ref<NCN> &h = godot::Ref<NCN>());
	static godot::Ref<NHPBeam> portal(const godot::Ref<NHPBeam> &beam, const godot::Ref<NNum> &t, const godot::Ref<NBeamSceneSurf> &surf,
			const godot::Ref<NCN> &h_exact = godot::Ref<NCN>());
	static godot::Ref<NHPBeam> project(const godot::Ref<NHPBeam> &beam, const godot::Ref<NNum> &t, const godot::Ref<NBeamSceneSurf> &surf, bool left,
			const godot::Ref<NCN> &h_exact = godot::Ref<NCN>());
	static godot::Ref<NCN> projective_exit(const godot::Ref<NBeamSceneSurf> &surf, bool left, const godot::Ref<NCN> &h);
	static godot::Variant back_propagate(const godot::Ref<NBeamSceneSurf> &surf, bool left, const godot::Ref<NCN> &X, bool x_on_carrier = false,
			const godot::Variant &click = godot::Variant());
	static godot::Array back_propagate_candidates(const godot::Ref<NBeamSceneSurf> &surf, bool left, const godot::Ref<NCN> &X, bool x_on_carrier = false);
	static godot::Ref<NCN> portal_point(const godot::Ref<NCN> &z, const godot::Ref<NBeamSceneSurf> &surf, bool inverse = false);
	static godot::Ref<NCN> u2(const godot::Ref<NBeamGeomSeg> &seg);
	static godot::Ref<NHPBeam> reflect_line(const godot::Ref<NHPBeam> &beam, const godot::Ref<NNum> &t, const godot::Ref<NBeamGeomSeg> &seg,
			const godot::Ref<NCN> &h_exact = godot::Ref<NCN>());
	static godot::Variant map_point(const godot::Variant &z, const godot::Ref<NBeamSceneSurf> &surf, bool inverse = false);
	static godot::Array map_pole(const godot::Array &parent, const godot::Ref<NBeamSceneSurf> &surf);
};

class NExact : public godot::RefCounted {
	GDCLASS(NExact, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static double U() { return rc::exact::U; }
	static godot::Ref<NNum> of(double x);
	static godot::Ref<NNum> mul(const godot::Ref<NNum> &a, const godot::Ref<NNum> &b);
	static godot::Ref<NNum> add(const godot::Ref<NNum> &a, const godot::Ref<NNum> &b);
	static godot::Ref<NNum> sub(const godot::Ref<NNum> &a, const godot::Ref<NNum> &b);
	static godot::Array emul(const godot::Array &x, const godot::Array &y);
	static godot::Array eadd(const godot::Array &x, const godot::Array &y);
	static godot::Array esub(const godot::Array &x, const godot::Array &y);
	static bool orient_zero(double ax, double ay, double bx, double by, double cx, double cy);
	static bool on_circle_zero(double cx, double cy, double r, double kx, double ky);
	static bool incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy);
	static godot::Array line_from_dir(double px, double py, double dx, double dy);
	static godot::Array line_through(double px, double py, double qx, double qy);
	static godot::Array reflect_point_h(const godot::Array &X, double sx, double sy, double ex, double ey);
	static godot::Variant invert_point_h(const godot::Array &X, const godot::Array &C, const godot::Ref<NNum> &rw2);
	static godot::Array line_through_h(double px, double py, const godot::Array &X);
	static godot::Array circumcenter_h(double ax, double ay, double bx, double by, double cx, double cy);
	static bool on_line_h(const godot::Array &L, const godot::Array &X);
	static bool tangent_zero(const godot::Array &L, const godot::Array &C, const godot::Ref<NNum> &rw2);
	static godot::Array reflect_line(const godot::Array &L, double sx, double sy, double ex, double ey);
};
