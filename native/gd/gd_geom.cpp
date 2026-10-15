#include "gd_geom.hpp"

#include "rc/error.hpp"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>

using godot::Array;
using godot::ClassDB;
using godot::D_METHOD;
using godot::Dictionary;
using godot::PropertyInfo;
using godot::Ref;
using godot::String;
using godot::Variant;
using godot::Vector2;
using namespace rcgd;

namespace {

bool null_beam_v(const BeamV &bv) {
	return std::visit([](const auto &b) { return b == nullptr; }, bv);
}

void tier_error(const char *where) {
	rc::report_error((std::string(where) + ": arguments of different precision tiers").c_str());
}

template <class N>
const rc::CN<N> *cn_of(const Ref<CN> &c) {
	return c.is_null() ? nullptr : std::get_if<rc::CN<N>>(&c->value);
}
template <class N>
const N *num_ref(const Ref<Num> &n) {
	return n.is_null() ? nullptr : std::get_if<N>(&n->value);
}
template <class N>
const rc::Seg<N> *seg_of(const Ref<BeamGeomSeg> &s) {
	return s.is_null() ? nullptr : std::get_if<rc::Seg<N>>(&s->seg);
}
template <class N>
const rc::Circle<N> *circle_of(const Ref<HPBeamCircle> &c) {
	return c.is_null() ? nullptr : std::get_if<rc::Circle<N>>(&c->circle);
}
template <class N>
const rc::Beam<N> *beam_of(const Ref<HPBeam> &b) {
	if (b.is_null()) {
		return nullptr;
	}
	const auto *p = std::get_if<rc::BeamPtr<N>>(&b->beam);
	return p == nullptr ? nullptr : p->get();
}
template <class N>
const rc::Surf<N> *surf_of(const Ref<BeamSceneSurf> &s) {
	if (s.is_null()) {
		return nullptr;
	}
	const auto *sc = std::get_if<std::shared_ptr<rc::Scene<N>>>(&s->scene);
	return (sc == nullptr || *sc == nullptr) ? nullptr : (*sc)->find(s->surface_id);
}
template <class N>
Ref<HPBeam> wrap_beam(rc::Beam<N> b) {
	return HPBeam::wrap(BeamV(std::make_shared<const rc::Beam<N>>(std::move(b))));
}
template <class N>
Array nums_array(const std::vector<N> &v) {
	Array a;
	for (const N &x : v) {
		a.push_back(Num::wrap(NumV(x)));
	}
	return a;
}

// the tier's `like` value from a ctx
template <class F>
auto with_like(const Ref<NumCtx> &ctx, F &&f) {
	if (ctx.is_valid() && ctx->limbs == DUAL_LIMBS) {
		return f(rc::Dual{1.0, 0.0});
	}
	if (ctx.is_valid() && ctx->limbs > 0) {
		return f(rc::BigFloat::from_int(1, ctx->limbs));
	}
	return f(rc::F64{1.0});
}

// ---- exact triples and [value, bound] pairs

bool triple_of(const Array &a, std::array<rc::BigFloat, 3> &out, const char *where) {
	if (a.size() != 3) {
		rc::report_error((std::string(where) + ": a homogeneous triple has 3 BigFloats").c_str());
		return false;
	}
	for (int i = 0; i < 3; i++) {
		const Ref<Num> n = a[i];
		const rc::BigFloat *b = num_ref<rc::BigFloat>(n);
		if (b == nullptr) {
			rc::report_error((std::string(where) + ": a homogeneous triple has 3 BigFloats").c_str());
			return false;
		}
		out[size_t(i)] = *b;
	}
	return true;
}
Array triple_array(const std::array<rc::BigFloat, 3> &t) {
	Array a;
	for (const rc::BigFloat &b : t) {
		a.push_back(Num::wrap(NumV(b)));
	}
	return a;
}
rc::exact::EB eb_of(const Array &a) {
	return rc::exact::EB{a.size() > 0 ? double(a[0]) : 0.0, a.size() > 1 ? double(a[1]) : 0.0};
}
Array eb_array(rc::exact::EB e) {
	Array a;
	a.push_back(e.v);
	a.push_back(e.e);
	return a;
}

} // namespace

// ---------------------------------------------------------------- HPBeamCircle

void HPBeamCircle::_bind_methods() {
	ClassDB::bind_static_method("HPBeamCircle", D_METHOD("make", "id", "c", "r"), &HPBeamCircle::make);
	ClassDB::bind_method(D_METHOD("get_id"), &HPBeamCircle::get_id);
	ClassDB::bind_method(D_METHOD("get_c"), &HPBeamCircle::get_c);
	ClassDB::bind_method(D_METHOD("get_r"), &HPBeamCircle::get_r);
	ClassDB::bind_method(D_METHOD("get_r2"), &HPBeamCircle::get_r2);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "id"), "", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "c", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN"), "", "get_c");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "r", godot::PROPERTY_HINT_RESOURCE_TYPE, "Num"), "", "get_r");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "r2", godot::PROPERTY_HINT_RESOURCE_TYPE, "Num"), "", "get_r2");
}

Ref<HPBeamCircle> HPBeamCircle::wrap(CircleV c) {
	Ref<HPBeamCircle> o;
	o.instantiate();
	o->circle = std::move(c);
	return o;
}
Ref<HPBeamCircle> HPBeamCircle::make(int id, const Ref<CN> &c, const Ref<Num> &r) {
	if (c.is_null() || r.is_null()) {
		rc::report_error("HPBeamCircle.make: null argument");
		return Ref<HPBeamCircle>();
	}
	return std::visit([&](const auto &cc) -> Ref<HPBeamCircle> {
		using N = std::decay_t<decltype(cc.re)>;
		const N *rr = num_ref<N>(r);
		if (rr == nullptr) {
			tier_error("HPBeamCircle.make");
			return Ref<HPBeamCircle>();
		}
		return wrap(rc::Circle<N>::make(id, cc, *rr));
	}, c->value);
}
int HPBeamCircle::get_id() const { return std::visit([](const auto &c) { return c.id; }, circle); }
Ref<CN> HPBeamCircle::get_c() const { return std::visit([](const auto &c) { return CN::wrap(c.c); }, circle); }
Ref<Num> HPBeamCircle::get_r() const { return std::visit([](const auto &c) { return Num::wrap(c.r); }, circle); }
Ref<Num> HPBeamCircle::get_r2() const { return std::visit([](const auto &c) { return Num::wrap(c.r2); }, circle); }

// ---------------------------------------------------------------- BeamGeomSeg

void BeamGeomSeg::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_id"), &BeamGeomSeg::get_id);
	ClassDB::bind_method(D_METHOD("get_kind"), &BeamGeomSeg::get_kind);
	ClassDB::bind_method(D_METHOD("get_full"), &BeamGeomSeg::get_full);
	ClassDB::bind_method(D_METHOD("get_s"), &BeamGeomSeg::get_s);
	ClassDB::bind_method(D_METHOD("get_e"), &BeamGeomSeg::get_e);
	ClassDB::bind_method(D_METHOD("get_v"), &BeamGeomSeg::get_v);
	ClassDB::bind_method(D_METHOD("get_circ"), &BeamGeomSeg::get_circ);
	ClassDB::bind_method(D_METHOD("c"), &BeamGeomSeg::c);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "id"), "", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind"), "", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "full"), "", "get_full");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "s", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN"), "", "get_s");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "e", godot::PROPERTY_HINT_RESOURCE_TYPE, "CN"), "", "get_e");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "v", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_v");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "circ", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_circ");
}

Ref<BeamGeomSeg> BeamGeomSeg::wrap(SegV s) {
	Ref<BeamGeomSeg> o;
	o.instantiate();
	o->seg = std::move(s);
	return o;
}
int BeamGeomSeg::get_id() const { return std::visit([](const auto &s) { return s.id; }, seg); }
int BeamGeomSeg::get_kind() const { return std::visit([](const auto &s) { return int(s.kind); }, seg); }
bool BeamGeomSeg::get_full() const { return std::visit([](const auto &s) { return s.full; }, seg); }
Ref<CN> BeamGeomSeg::get_s() const { return std::visit([](const auto &s) { return CN::wrap(s.s); }, seg); }
Ref<CN> BeamGeomSeg::get_e() const { return std::visit([](const auto &s) { return CN::wrap(s.e); }, seg); }
Variant BeamGeomSeg::get_v() const {
	return std::visit([](const auto &s) -> Variant { return s.v ? Variant(CN::wrap(*s.v)) : Variant(); }, seg);
}
Variant BeamGeomSeg::get_circ() const {
	return std::visit([](const auto &s) -> Variant { return s.circ ? Variant(HPBeamCircle::wrap(CircleV(*s.circ))) : Variant(); }, seg);
}
Ref<CN> BeamGeomSeg::c() const {
	return std::visit([](const auto &s) -> Ref<CN> { return s.circ ? CN::wrap(s.circ->c) : Ref<CN>(); }, seg);
}

// BeamSceneSurf.seg (declared in gd_scene.hpp)
Ref<BeamGeomSeg> BeamSceneSurf::get_seg() const {
	return visit([](const auto &s) { return BeamGeomSeg::wrap(SegV(s.seg)); });
}

// ---------------------------------------------------------------- BeamGeom

void BeamGeom::_bind_methods() {
	ClassDB::bind_static_method("BeamGeom", D_METHOD("seg_from_dict", "ctx", "d"), &BeamGeom::seg_from_dict);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("seg_from_segment", "ctx", "segment", "sid"), &BeamGeom::seg_from_segment);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("circumcenter", "A", "B", "C"), &BeamGeom::circumcenter);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("hits", "beam", "seg", "depart_on"), &BeamGeom::hits);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("hit_records", "beam", "seg", "depart_on", "known"), &BeamGeom::hit_records, DEFVAL(Array()));
	ClassDB::bind_static_method("BeamGeom", D_METHOD("on_segment_value", "seg", "p"), &BeamGeom::on_segment_value);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("on_segment", "seg", "p"), &BeamGeom::on_segment);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("eq", "a", "b"), &BeamGeom::eq);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("endpoint_at", "seg", "p"), &BeamGeom::endpoint_at);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("grad", "seg", "p"), &BeamGeom::grad);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("winding", "seg"), &BeamGeom::winding);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("side_left", "seg", "p", "dir"), &BeamGeom::side_left);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("tangent_into", "seg", "which"), &BeamGeom::tangent_into);
	ClassDB::bind_static_method("BeamGeom", D_METHOD("blocked_sides", "seg", "which", "dir"), &BeamGeom::blocked_sides);
	BIND_ENUM_CONSTANT(LINE);
	BIND_ENUM_CONSTANT(CIRCLE);
}

namespace {

double num_at(const Variant &arr, int64_t i) {
	const Array a = arr;
	return double(a[i]);
}

Ref<BeamGeomSeg> seg_from_def(const Ref<NumCtx> &ctx, const rc::SegDef &def) {
	return with_like(ctx, [&](const auto &like) { return BeamGeomSeg::wrap(SegV(rc::geom::seg_from_def(def, like))); });
}

} // namespace

Ref<BeamGeomSeg> BeamGeom::seg_from_dict(const Ref<NumCtx> &ctx, const Dictionary &d) {
	rc::SegDef def;
	def.id = int(int64_t(d.get("id", 0)));
	def.sx = num_at(d["start"], 0);
	def.sy = num_at(d["start"], 1);
	def.ex = num_at(d["end"], 0);
	def.ey = num_at(d["end"], 1);
	if (d.has("via") && d["via"].get_type() != Variant::NIL) {
		def.has_v = true;
		def.vx = num_at(d["via"], 0);
		def.vy = num_at(d["via"], 1);
	}
	def.full = bool(d.get("full", false));
	const String kind = d["kind"];
	if (kind == String("line")) {
		def.kind = rc::SegKind::Line;
	} else if (kind == String("arc")) {
		def.kind = rc::SegKind::Circle;
	} else if (kind == String("full_circle")) {
		def.kind = rc::SegKind::Circle;
		def.full = true;
		def.cx = num_at(d["center"], 0);
		def.cy = num_at(d["center"], 1);
		def.r = double(d["radius"]);
	} else {
		rc::report_error("BeamGeom.seg_from_dict: kind is line | arc | full_circle");
	}
	return seg_from_def(ctx, def);
}

Ref<BeamGeomSeg> BeamGeom::seg_from_segment(const Ref<NumCtx> &ctx, const Variant &segment, int sid) {
	// BeamGeom.seg_from_segment: a game Segment (start / end / via Points, full, get_carrier())
	godot::Object *seg = segment;
	if (seg == nullptr) {
		rc::report_error("BeamGeom.seg_from_segment: null segment");
		return Ref<BeamGeomSeg>();
	}
	auto coords = [](const Variant &point) -> Vector2 {
		godot::Object *p = point;
		return p == nullptr ? Vector2() : Vector2(p->get("coords"));
	};
	const Vector2 s = coords(seg->get("start"));
	const Vector2 e = coords(seg->get("end"));
	const Vector2 via = coords(seg->get("via"));
	const Variant carrier = seg->call("get_carrier");
	godot::Object *car = carrier;
	rc::SegDef def;
	def.id = sid;
	def.sx = s.x;
	def.sy = s.y;
	def.ex = e.x;
	def.ey = e.y;
	if (!(std::isinf(via.x) || std::isinf(via.y))) {
		def.has_v = true;
		def.vx = via.x;
		def.vy = via.y;
	}
	def.full = bool(seg->get("full"));
	if (car != nullptr && bool(car->call("is_line"))) {
		def.kind = rc::SegKind::Line;
	} else if (def.full && car != nullptr) {
		def.kind = rc::SegKind::Circle;
		const double a = double(car->get("a"));
		def.cx = -double(car->get("b")) / (2.0 * a);
		def.cy = -double(car->get("c")) / (2.0 * a);
		def.r = double(car->call("radius"));
	} else {
		def.kind = rc::SegKind::Circle;
	}
	return seg_from_def(ctx, def);
}

Ref<CN> BeamGeom::circumcenter(const Ref<CN> &A, const Ref<CN> &B, const Ref<CN> &C) {
	if (A.is_null()) {
		return Ref<CN>();
	}
	return std::visit([&](const auto &a) -> Ref<CN> {
		using N = std::decay_t<decltype(a.re)>;
		const rc::CN<N> *b = cn_of<N>(B);
		const rc::CN<N> *c = cn_of<N>(C);
		if (b == nullptr || c == nullptr) {
			tier_error("BeamGeom.circumcenter");
			return Ref<CN>();
		}
		return CN::wrap(rc::geom::circumcenter(a, *b, *c));
	}, A->value);
}

// Runs f(beam, seg) at the beam's tier.
template <class F>
static auto with_beam_seg(const char *where, const Ref<HPBeam> &beam, const Ref<BeamGeomSeg> &seg, F &&f) {
	using R = decltype(f(std::declval<const rc::Beam<rc::F64> &>(), std::declval<const rc::Seg<rc::F64> &>()));
	if (beam.is_null() || seg.is_null()) {
		rc::report_error((std::string(where) + ": null argument").c_str());
		return R();
	}
	return std::visit([&](const auto &bp) -> R {
		using N = std::decay_t<decltype(bp->kappa)>;
		const rc::Seg<N> *sg = seg_of<N>(seg);
		if (bp == nullptr || sg == nullptr) {
			tier_error(where);
			return R();
		}
		return f(*bp, *sg);
	}, beam->beam);
}

Array BeamGeom::hits(const Ref<HPBeam> &beam, const Ref<BeamGeomSeg> &seg, bool depart_on) {
	return with_beam_seg("BeamGeom.hits", beam, seg, [&](const auto &b, const auto &sg) { return nums_array(rc::geom::hits(b, sg, depart_on)); });
}

Array BeamGeom::hit_records(const Ref<HPBeam> &beam, const Ref<BeamGeomSeg> &seg, bool depart_on, const Array &known) {
	// BeamGeom.hit_records: {t, point, at_endpoint, on_segment} per root; known
	// points at an endpoint replace the nearest root (provenance snap).
	return with_beam_seg("BeamGeom.hit_records", beam, seg, [&](const auto &b, const auto &sg) -> Array {
		using N = std::decay_t<decltype(b.kappa)>;
		struct Rec {
			N t;
			rc::CN<N> point;
			int at_endpoint;
		};
		std::vector<Rec> recs;
		for (const N &t : rc::geom::hits(b, sg, depart_on)) {
			const rc::CN<N> pt = b.at(t);
			recs.push_back(Rec{t, pt, rc::geom::endpoint_at(sg, pt)});
		}
		for (int64_t i = 0; i < known.size(); i++) {
			const Array kp = known[i];
			if (kp.size() < 2) {
				continue;
			}
			const rc::CN<N> *k0 = cn_of<N>(Ref<CN>(kp[0]));
			const N *k1 = num_ref<N>(Ref<Num>(kp[1]));
			if (k0 == nullptr || k1 == nullptr) {
				tier_error("BeamGeom.hit_records");
				continue;
			}
			const int which = rc::geom::endpoint_at(sg, *k0);
			if (which == 0 || recs.empty()) {
				continue;
			}
			size_t best = 0;
			double best_d = rc::INF_D;
			for (size_t r = 0; r < recs.size(); r++) {
				const double d = recs[r].point.sub(*k0).abs2().to_float();
				if (d < best_d) {
					best_d = d;
					best = r;
				}
			}
			recs[best] = Rec{*k1, *k0, which};
		}
		Array out;
		for (const Rec &r : recs) {
			Dictionary d;
			d["t"] = Num::wrap(NumV(r.t));
			d["point"] = CN::wrap(CNV(r.point));
			d["at_endpoint"] = r.at_endpoint;
			d["on_segment"] = rc::geom::on_segment(sg, r.point);
			out.push_back(d);
		}
		return out;
	});
}

// Runs f(seg, p) at the seg's tier.
template <class F>
static auto with_seg_cn(const char *where, const Ref<BeamGeomSeg> &seg, const Ref<CN> &p, F &&f) {
	using R = decltype(f(std::declval<const rc::Seg<rc::F64> &>(), std::declval<const rc::CN<rc::F64> &>()));
	if (seg.is_null() || p.is_null()) {
		rc::report_error((std::string(where) + ": null argument").c_str());
		return R();
	}
	return std::visit([&](const auto &sg) -> R {
		using N = std::decay_t<decltype(sg.s.re)>;
		const rc::CN<N> *pp = cn_of<N>(p);
		if (pp == nullptr) {
			tier_error(where);
			return R();
		}
		return f(sg, *pp);
	}, seg->seg);
}

Ref<Num> BeamGeom::on_segment_value(const Ref<BeamGeomSeg> &seg, const Ref<CN> &p) {
	return with_seg_cn("BeamGeom.on_segment_value", seg, p, [](const auto &sg, const auto &pp) { return Num::wrap(NumV(rc::geom::on_segment_value(sg, pp))); });
}
bool BeamGeom::on_segment(const Ref<BeamGeomSeg> &seg, const Ref<CN> &p) {
	return with_seg_cn("BeamGeom.on_segment", seg, p, [](const auto &sg, const auto &pp) { return rc::geom::on_segment(sg, pp); });
}
bool BeamGeom::eq(const Ref<CN> &a, const Ref<CN> &b) {
	if (a.is_null() || b.is_null()) {
		return false;
	}
	return std::visit([&](const auto &x) -> bool {
		using N = std::decay_t<decltype(x.re)>;
		const rc::CN<N> *y = cn_of<N>(b);
		return y != nullptr && rc::geom::eq(x, *y);
	}, a->value);
}
int BeamGeom::endpoint_at(const Ref<BeamGeomSeg> &seg, const Ref<CN> &p) {
	return with_seg_cn("BeamGeom.endpoint_at", seg, p, [](const auto &sg, const auto &pp) { return rc::geom::endpoint_at(sg, pp); });
}
Ref<CN> BeamGeom::grad(const Ref<BeamGeomSeg> &seg, const Ref<CN> &p) {
	return with_seg_cn("BeamGeom.grad", seg, p, [](const auto &sg, const auto &pp) { return CN::wrap(CNV(rc::geom::grad(sg, pp))); });
}
Ref<Num> BeamGeom::winding(const Ref<BeamGeomSeg> &seg) {
	if (seg.is_null()) {
		return Ref<Num>();
	}
	return std::visit([](const auto &sg) { return Num::wrap(NumV(rc::geom::winding(sg))); }, seg->seg);
}
bool BeamGeom::side_left(const Ref<BeamGeomSeg> &seg, const Ref<CN> &p, const Ref<CN> &dir) {
	return with_seg_cn("BeamGeom.side_left", seg, p, [&](const auto &sg, const auto &pp) -> bool {
		using N = std::decay_t<decltype(pp.re)>;
		const rc::CN<N> *dd = cn_of<N>(dir);
		if (dd == nullptr) {
			tier_error("BeamGeom.side_left");
			return false;
		}
		return rc::geom::side_left(sg, pp, *dd);
	});
}
Ref<CN> BeamGeom::tangent_into(const Ref<BeamGeomSeg> &seg, int which) {
	if (seg.is_null()) {
		return Ref<CN>();
	}
	return std::visit([&](const auto &sg) { return CN::wrap(CNV(rc::geom::tangent_into(sg, which))); }, seg->seg);
}
Array BeamGeom::blocked_sides(const Ref<BeamGeomSeg> &seg, int which, const Ref<CN> &dir) {
	return with_seg_cn("BeamGeom.blocked_sides", seg, dir, [&](const auto &sg, const auto &dd) -> Array {
		const rc::geom::Blocked bl = rc::geom::blocked_sides(sg, which, dd);
		Array a;
		a.push_back(bl.left);
		a.push_back(bl.right);
		return a;
	});
}

// ---------------------------------------------------------------- BeamEffects

void BeamEffects::_bind_methods() {
	ClassDB::bind_static_method("BeamEffects", D_METHOD("apply", "beam", "t", "surf", "etype", "h"), &BeamEffects::apply, DEFVAL(Variant()));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("portal", "beam", "t", "surf", "h_exact"), &BeamEffects::portal, DEFVAL(Variant()));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("project", "beam", "t", "surf", "left", "h_exact"), &BeamEffects::project, DEFVAL(Variant()));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("projective_exit", "surf", "left", "h"), &BeamEffects::projective_exit);
	ClassDB::bind_static_method("BeamEffects", D_METHOD("back_propagate", "surf", "left", "X", "x_on_carrier", "click"), &BeamEffects::back_propagate,
			DEFVAL(false), DEFVAL(Variant()));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("back_propagate_candidates", "surf", "left", "X", "x_on_carrier"),
			&BeamEffects::back_propagate_candidates, DEFVAL(false));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("portal_point", "z", "surf", "inverse"), &BeamEffects::portal_point, DEFVAL(false));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("u2", "seg"), &BeamEffects::u2);
	ClassDB::bind_static_method("BeamEffects", D_METHOD("reflect_line", "beam", "t", "seg", "h_exact"), &BeamEffects::reflect_line, DEFVAL(Variant()));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("map_point", "z", "surf", "inverse"), &BeamEffects::map_point, DEFVAL(false));
	ClassDB::bind_static_method("BeamEffects", D_METHOD("map_pole", "parent", "surf"), &BeamEffects::map_pole);
}

// Runs f(beam, t, surf, h*) at the beam's tier.
template <class F>
static Ref<HPBeam> with_beam_surf(const char *where, const Ref<HPBeam> &beam, const Ref<Num> &t, const Ref<BeamSceneSurf> &surf,
		const Ref<CN> &h, F &&f) {
	if (beam.is_null() || t.is_null() || surf.is_null()) {
		rc::report_error((std::string(where) + ": null argument").c_str());
		return Ref<HPBeam>();
	}
	return std::visit([&](const auto &bp) -> Ref<HPBeam> {
		using N = std::decay_t<decltype(bp->kappa)>;
		const N *tt = num_ref<N>(t);
		const rc::Surf<N> *s = surf_of<N>(surf);
		const rc::CN<N> *hh = h.is_null() ? nullptr : cn_of<N>(h);
		if (bp == nullptr || tt == nullptr || s == nullptr || (h.is_valid() && hh == nullptr)) {
			tier_error(where);
			return Ref<HPBeam>();
		}
		return wrap_beam<N>(f(*bp, *tt, *s, hh));
	}, beam->beam);
}

Ref<HPBeam> BeamEffects::apply(const Ref<HPBeam> &beam, const Ref<Num> &t, const Ref<BeamSceneSurf> &surf, const String &etype, const Ref<CN> &h) {
	(void)etype; // BeamEffects.apply ignores it too: the map is the surface's
	return with_beam_surf("BeamEffects.apply", beam, t, surf, h, [](const auto &b, const auto &tt, const auto &s, const auto *hh) {
		return rc::effects::apply(b, tt, s, hh);
	});
}
Ref<HPBeam> BeamEffects::portal(const Ref<HPBeam> &beam, const Ref<Num> &t, const Ref<BeamSceneSurf> &surf, const Ref<CN> &h_exact) {
	return with_beam_surf("BeamEffects.portal", beam, t, surf, h_exact, [](const auto &b, const auto &tt, const auto &s, const auto *hh) {
		return rc::effects::portal(b, tt, s, hh);
	});
}
Ref<HPBeam> BeamEffects::project(const Ref<HPBeam> &beam, const Ref<Num> &t, const Ref<BeamSceneSurf> &surf, bool left, const Ref<CN> &h_exact) {
	return with_beam_surf("BeamEffects.project", beam, t, surf, h_exact, [&](const auto &b, const auto &tt, const auto &s, const auto *hh) {
		return rc::effects::project(b, tt, s, left, hh);
	});
}

// Runs f(surf, X) at X's tier.
template <class F>
static auto with_surf_cn(const char *where, const Ref<BeamSceneSurf> &surf, const Ref<CN> &X, F &&f) {
	using R = decltype(f(std::declval<const rc::Surf<rc::F64> &>(), std::declval<const rc::CN<rc::F64> &>()));
	if (surf.is_null() || X.is_null()) {
		rc::report_error((std::string(where) + ": null argument").c_str());
		return R();
	}
	return std::visit([&](const auto &x) -> R {
		using N = std::decay_t<decltype(x.re)>;
		const rc::Surf<N> *s = surf_of<N>(surf);
		if (s == nullptr) {
			tier_error(where);
			return R();
		}
		return f(*s, x);
	}, X->value);
}

Ref<CN> BeamEffects::projective_exit(const Ref<BeamSceneSurf> &surf, bool left, const Ref<CN> &h) {
	return with_surf_cn("BeamEffects.projective_exit", surf, h, [&](const auto &s, const auto &hh) { return CN::wrap(CNV(rc::effects::projective_exit(s, left, hh))); });
}
Variant BeamEffects::back_propagate(const Ref<BeamSceneSurf> &surf, bool left, const Ref<CN> &X, bool x_on_carrier, const Variant &click) {
	return with_surf_cn("BeamEffects.back_propagate", surf, X, [&](const auto &s, const auto &x) -> Variant {
		using N = std::decay_t<decltype(x.re)>;
		std::optional<rc::effects::F32Point> c;
		if (click.get_type() == Variant::VECTOR2) {
			const Vector2 v = click;
			c = rc::effects::F32Point{float(v.x), float(v.y)};
		}
		const std::optional<rc::CN<N>> h = rc::effects::back_propagate(s, left, x, x_on_carrier, c ? &*c : nullptr);
		return h ? Variant(CN::wrap(CNV(*h))) : Variant();
	});
}
Array BeamEffects::back_propagate_candidates(const Ref<BeamSceneSurf> &surf, bool left, const Ref<CN> &X, bool x_on_carrier) {
	return with_surf_cn("BeamEffects.back_propagate_candidates", surf, X, [&](const auto &s, const auto &x) -> Array {
		Array out;
		for (const auto &c : rc::effects::back_propagate_candidates(s, left, x, x_on_carrier)) {
			Array pair;
			pair.push_back(CN::wrap(CNV(c.h)));
			pair.push_back(c.direct);
			out.push_back(pair);
		}
		return out;
	});
}
Ref<CN> BeamEffects::portal_point(const Ref<CN> &z, const Ref<BeamSceneSurf> &surf, bool inverse) {
	return with_surf_cn("BeamEffects.portal_point", surf, z, [&](const auto &s, const auto &zz) { return CN::wrap(CNV(rc::effects::portal_point(zz, s, inverse))); });
}
Ref<CN> BeamEffects::u2(const Ref<BeamGeomSeg> &seg) {
	if (seg.is_null()) {
		return Ref<CN>();
	}
	return std::visit([](const auto &sg) { return CN::wrap(CNV(rc::effects::u2(sg))); }, seg->seg);
}
Ref<HPBeam> BeamEffects::reflect_line(const Ref<HPBeam> &beam, const Ref<Num> &t, const Ref<BeamGeomSeg> &seg, const Ref<CN> &h_exact) {
	if (t.is_null()) {
		rc::report_error("BeamEffects.reflect_line: null t");
		return Ref<HPBeam>();
	}
	return with_beam_seg("BeamEffects.reflect_line", beam, seg, [&](const auto &b, const auto &sg) -> Ref<HPBeam> {
		using N = std::decay_t<decltype(b.kappa)>;
		const N *tt = num_ref<N>(t);
		const rc::CN<N> *hh = h_exact.is_null() ? nullptr : cn_of<N>(h_exact);
		if (tt == nullptr || (h_exact.is_valid() && hh == nullptr)) {
			tier_error("BeamEffects.reflect_line");
			return Ref<HPBeam>();
		}
		return wrap_beam<N>(rc::effects::reflect_line(b, *tt, sg, hh));
	});
}
Variant BeamEffects::map_point(const Variant &z, const Ref<BeamSceneSurf> &surf, bool inverse) {
	if (surf.is_null()) {
		return Variant();
	}
	// z: CN or null (the point at infinity) at the surface's tier
	return surf->visit([&](const auto &s) -> Variant {
		using N = std::decay_t<decltype(s.sx)>;
		(void)sizeof(N);
		using SN = std::decay_t<decltype(s.seg.s.re)>;
		std::optional<rc::CN<SN>> zz;
		if (z.get_type() == Variant::OBJECT) {
			const Ref<CN> zr = z;
			const rc::CN<SN> *p = cn_of<SN>(zr);
			if (p == nullptr) {
				tier_error("BeamEffects.map_point");
				return Variant();
			}
			zz = *p;
		}
		const std::optional<rc::CN<SN>> r = rc::effects::map_point(zz, s, inverse);
		return r ? Variant(CN::wrap(CNV(*r))) : Variant();
	});
}
Array BeamEffects::map_pole(const Array &parent, const Ref<BeamSceneSurf> &surf) {
	if (surf.is_null() || parent.size() < 3) {
		rc::report_error("BeamEffects.map_pole: parent is [P, Q or null, delta or null]");
		return Array();
	}
	return surf->visit([&](const auto &s) -> Array {
		using SN = std::decay_t<decltype(s.seg.s.re)>;
		rc::effects::Poles<SN> poles;
		for (int i = 0; i < 3; i++) {
			const Variant v = parent[i];
			if (v.get_type() == Variant::NIL) {
				continue;
			}
			const rc::CN<SN> *p = cn_of<SN>(Ref<CN>(v));
			if (p == nullptr) {
				tier_error("BeamEffects.map_pole");
				return Array();
			}
			(i == 0 ? poles.P : (i == 1 ? poles.Q : poles.delta)) = *p;
		}
		const rc::effects::Poles<SN> r = rc::effects::map_pole(poles, s);
		Array out;
		for (const std::optional<rc::CN<SN>> *q : {&r.P, &r.Q, &r.delta}) {
			out.push_back(*q ? Variant(CN::wrap(CNV(**q))) : Variant());
		}
		return out;
	});
}

// ---------------------------------------------------------------- Exact

void Exact::_bind_methods() {
	ClassDB::bind_static_method("Exact", D_METHOD("U"), &Exact::U);
	ClassDB::bind_static_method("Exact", D_METHOD("of", "x"), &Exact::of);
	ClassDB::bind_static_method("Exact", D_METHOD("mul", "a", "b"), &Exact::mul);
	ClassDB::bind_static_method("Exact", D_METHOD("add", "a", "b"), &Exact::add);
	ClassDB::bind_static_method("Exact", D_METHOD("sub", "a", "b"), &Exact::sub);
	ClassDB::bind_static_method("Exact", D_METHOD("emul", "x", "y"), &Exact::emul);
	ClassDB::bind_static_method("Exact", D_METHOD("eadd", "x", "y"), &Exact::eadd);
	ClassDB::bind_static_method("Exact", D_METHOD("esub", "x", "y"), &Exact::esub);
	ClassDB::bind_static_method("Exact", D_METHOD("orient_zero", "ax", "ay", "bx", "by", "cx", "cy"), &Exact::orient_zero);
	ClassDB::bind_static_method("Exact", D_METHOD("on_circle_zero", "cx", "cy", "r", "kx", "ky"), &Exact::on_circle_zero);
	ClassDB::bind_static_method("Exact", D_METHOD("incircle_zero", "ax", "ay", "bx", "by", "cx", "cy", "dx", "dy"), &Exact::incircle_zero);
	ClassDB::bind_static_method("Exact", D_METHOD("line_from_dir", "px", "py", "dx", "dy"), &Exact::line_from_dir);
	ClassDB::bind_static_method("Exact", D_METHOD("line_through", "px", "py", "qx", "qy"), &Exact::line_through);
	ClassDB::bind_static_method("Exact", D_METHOD("reflect_point_h", "X", "sx", "sy", "ex", "ey"), &Exact::reflect_point_h);
	ClassDB::bind_static_method("Exact", D_METHOD("invert_point_h", "X", "C", "rw2"), &Exact::invert_point_h);
	ClassDB::bind_static_method("Exact", D_METHOD("line_through_h", "px", "py", "X"), &Exact::line_through_h);
	ClassDB::bind_static_method("Exact", D_METHOD("circumcenter_h", "ax", "ay", "bx", "by", "cx", "cy"), &Exact::circumcenter_h);
	ClassDB::bind_static_method("Exact", D_METHOD("on_line_h", "L", "X"), &Exact::on_line_h);
	ClassDB::bind_static_method("Exact", D_METHOD("tangent_zero", "L", "C", "rw2"), &Exact::tangent_zero);
	ClassDB::bind_static_method("Exact", D_METHOD("reflect_line", "L", "sx", "sy", "ex", "ey"), &Exact::reflect_line);
}

Ref<Num> Exact::of(double x) { return Num::wrap(NumV(rc::exact::of(x))); }

namespace {
Ref<Num> big_op(const Ref<Num> &a, const Ref<Num> &b, const char *where, rc::BigFloat (*op)(const rc::BigFloat &, const rc::BigFloat &)) {
	const rc::BigFloat *x = num_ref<rc::BigFloat>(a);
	const rc::BigFloat *y = num_ref<rc::BigFloat>(b);
	if (x == nullptr || y == nullptr) {
		rc::report_error((std::string(where) + ": BigFloat arguments").c_str());
		return Ref<Num>();
	}
	return Num::wrap(NumV(op(*x, *y)));
}
} // namespace

Ref<Num> Exact::mul(const Ref<Num> &a, const Ref<Num> &b) { return big_op(a, b, "Exact.mul", &rc::exact::mul); }
Ref<Num> Exact::add(const Ref<Num> &a, const Ref<Num> &b) { return big_op(a, b, "Exact.add", &rc::exact::add); }
Ref<Num> Exact::sub(const Ref<Num> &a, const Ref<Num> &b) { return big_op(a, b, "Exact.sub", &rc::exact::sub); }
Array Exact::emul(const Array &x, const Array &y) { return eb_array(rc::exact::emul(eb_of(x), eb_of(y))); }
Array Exact::eadd(const Array &x, const Array &y) { return eb_array(rc::exact::eadd(eb_of(x), eb_of(y))); }
Array Exact::esub(const Array &x, const Array &y) { return eb_array(rc::exact::esub(eb_of(x), eb_of(y))); }
bool Exact::orient_zero(double ax, double ay, double bx, double by, double cx, double cy) { return rc::exact::orient_zero(ax, ay, bx, by, cx, cy); }
bool Exact::on_circle_zero(double cx, double cy, double r, double kx, double ky) { return rc::exact::on_circle_zero(cx, cy, r, kx, ky); }
bool Exact::incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
	return rc::exact::incircle_zero(ax, ay, bx, by, cx, cy, dx, dy);
}
Array Exact::line_from_dir(double px, double py, double dx, double dy) { return triple_array(rc::exact::line_from_dir(px, py, dx, dy)); }
Array Exact::line_through(double px, double py, double qx, double qy) { return triple_array(rc::exact::line_through(px, py, qx, qy)); }
Array Exact::reflect_point_h(const Array &X, double sx, double sy, double ex, double ey) {
	rc::exact::Point3 x;
	if (!triple_of(X, x, "Exact.reflect_point_h")) {
		return Array();
	}
	return triple_array(rc::exact::reflect_point_h(x, sx, sy, ex, ey));
}
Variant Exact::invert_point_h(const Array &X, const Array &C, const Ref<Num> &rw2) {
	rc::exact::Point3 x, c;
	const rc::BigFloat *r = num_ref<rc::BigFloat>(rw2);
	if (!triple_of(X, x, "Exact.invert_point_h") || !triple_of(C, c, "Exact.invert_point_h") || r == nullptr) {
		return Variant();
	}
	const std::optional<rc::exact::Point3> p = rc::exact::invert_point_h(x, c, *r);
	return p ? Variant(triple_array(*p)) : Variant();
}
Array Exact::line_through_h(double px, double py, const Array &X) {
	rc::exact::Point3 x;
	if (!triple_of(X, x, "Exact.line_through_h")) {
		return Array();
	}
	return triple_array(rc::exact::line_through_h(px, py, x));
}
Array Exact::circumcenter_h(double ax, double ay, double bx, double by, double cx, double cy) {
	return triple_array(rc::exact::circumcenter_h(ax, ay, bx, by, cx, cy));
}
bool Exact::on_line_h(const Array &L, const Array &X) {
	rc::exact::Line3 l;
	rc::exact::Point3 x;
	if (!triple_of(L, l, "Exact.on_line_h") || !triple_of(X, x, "Exact.on_line_h")) {
		return false;
	}
	return rc::exact::on_line_h(l, x);
}
bool Exact::tangent_zero(const Array &L, const Array &C, const Ref<Num> &rw2) {
	rc::exact::Line3 l;
	rc::exact::Point3 c;
	const rc::BigFloat *r = num_ref<rc::BigFloat>(rw2);
	if (!triple_of(L, l, "Exact.tangent_zero") || !triple_of(C, c, "Exact.tangent_zero") || r == nullptr) {
		return false;
	}
	return rc::exact::tangent_zero(l, c, *r);
}
Array Exact::reflect_line(const Array &L, double sx, double sy, double ex, double ey) {
	rc::exact::Line3 l;
	if (!triple_of(L, l, "Exact.reflect_line")) {
		return Array();
	}
	return triple_array(rc::exact::reflect_line(l, sx, sy, ex, ey));
}

// ---------------------------------------------------------------- HPBeam: inf_like / through / invert_point / hits / invert_at

Ref<Num> HPBeam::inf_like(const Ref<Num> &x) {
	if (x.is_null()) {
		return Ref<Num>();
	}
	return std::visit([](const auto &v) { return Num::wrap(NumV(v.inf_like())); }, x->value);
}

Ref<HPBeam> HPBeam::through(const Ref<CN> &P, const Ref<CN> &X, const Variant &delta) {
	if (P.is_null() || X.is_null()) {
		rc::report_error("HPBeam.through: null argument");
		return Ref<HPBeam>();
	}
	return std::visit([&](const auto &p) -> Ref<HPBeam> {
		using N = std::decay_t<decltype(p.re)>;
		const rc::CN<N> *x = cn_of<N>(X);
		const rc::CN<N> *d = nullptr;
		if (delta.get_type() == Variant::OBJECT) {
			d = cn_of<N>(Ref<CN>(delta));
			if (d == nullptr) {
				tier_error("HPBeam.through");
				return Ref<HPBeam>();
			}
		}
		if (x == nullptr) {
			tier_error("HPBeam.through");
			return Ref<HPBeam>();
		}
		return wrap_beam<N>(rc::Beam<N>::through(p, *x, d));
	}, P->value);
}

Ref<CN> HPBeam::invert_point(const Ref<CN> &z, const Variant &circ) {
	const Ref<HPBeamCircle> c = circ;
	if (z.is_null() || c.is_null()) {
		rc::report_error("HPBeam.invert_point: null argument");
		return Ref<CN>();
	}
	return std::visit([&](const auto &zz) -> Ref<CN> {
		using N = std::decay_t<decltype(zz.re)>;
		const rc::Circle<N> *cc = circle_of<N>(c);
		if (cc == nullptr) {
			tier_error("HPBeam.invert_point");
			return Ref<CN>();
		}
		return CN::wrap(CNV(rc::Beam<N>::invert_point(zz, *cc)));
	}, z->value);
}

Array HPBeam::hits(const Variant &circ, bool depart_on) const {
	const Ref<HPBeamCircle> c = circ;
	if (c.is_null() || null_beam_v(beam)) {
		rc::report_error("HPBeam.hits: null argument");
		return Array();
	}
	return std::visit([&](const auto &bp) -> Array {
		using N = std::decay_t<decltype(bp->kappa)>;
		const rc::Circle<N> *cc = circle_of<N>(c);
		if (cc == nullptr) {
			tier_error("HPBeam.hits");
			return Array();
		}
		return nums_array(bp->hits(*cc, depart_on));
	}, beam);
}

Ref<HPBeam> HPBeam::invert_at(const Ref<Num> &t, const Variant &circ, const Ref<CN> &h_exact) const {
	const Ref<HPBeamCircle> c = circ;
	if (t.is_null() || c.is_null() || null_beam_v(beam)) {
		rc::report_error("HPBeam.invert_at: null argument");
		return Ref<HPBeam>();
	}
	return std::visit([&](const auto &bp) -> Ref<HPBeam> {
		using N = std::decay_t<decltype(bp->kappa)>;
		const N *tt = num_ref<N>(t);
		const rc::Circle<N> *cc = circle_of<N>(c);
		const rc::CN<N> *hh = h_exact.is_null() ? nullptr : cn_of<N>(h_exact);
		if (tt == nullptr || cc == nullptr || (h_exact.is_valid() && hh == nullptr)) {
			tier_error("HPBeam.invert_at");
			return Ref<HPBeam>();
		}
		return wrap_beam<N>(bp->invert_at(*tt, *cc, hh));
	}, beam);
}
