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
const rc::CN<N> *cn_of(const Ref<NCN> &c) {
	return c.is_null() ? nullptr : std::get_if<rc::CN<N>>(&c->value);
}
template <class N>
const N *num_ref(const Ref<NNum> &n) {
	return n.is_null() ? nullptr : std::get_if<N>(&n->value);
}
template <class N>
const rc::Seg<N> *seg_of(const Ref<NBeamGeomSeg> &s) {
	return s.is_null() ? nullptr : std::get_if<rc::Seg<N>>(&s->seg);
}
template <class N>
const rc::Circle<N> *circle_of(const Ref<NHPBeamCircle> &c) {
	return c.is_null() ? nullptr : std::get_if<rc::Circle<N>>(&c->circle);
}
template <class N>
const rc::Beam<N> *beam_of(const Ref<NHPBeam> &b) {
	if (b.is_null()) {
		return nullptr;
	}
	const auto *p = std::get_if<rc::BeamPtr<N>>(&b->beam);
	return p == nullptr ? nullptr : p->get();
}
template <class N>
const rc::Surf<N> *surf_of(const Ref<NBeamSceneSurf> &s) {
	if (s.is_null()) {
		return nullptr;
	}
	const auto *sc = std::get_if<std::shared_ptr<rc::Scene<N>>>(&s->scene);
	return (sc == nullptr || *sc == nullptr) ? nullptr : (*sc)->find(s->surface_id);
}
template <class N>
Ref<NHPBeam> wrap_beam(rc::Beam<N> b) {
	return NHPBeam::wrap(BeamV(std::make_shared<const rc::Beam<N>>(std::move(b))));
}
template <class N>
Array nums_array(const std::vector<N> &v) {
	Array a;
	for (const N &x : v) {
		a.push_back(NNum::wrap(NumV(x)));
	}
	return a;
}

// the tier's `like` value from a ctx
template <class F>
auto with_like(const Ref<NNumCtx> &ctx, F &&f) {
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
		const Ref<NNum> n = a[i];
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
		a.push_back(NNum::wrap(NumV(b)));
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

// ---------------------------------------------------------------- NHPBeamCircle

void NHPBeamCircle::_bind_methods() {
	ClassDB::bind_static_method("NHPBeamCircle", D_METHOD("make", "id", "c", "r"), &NHPBeamCircle::make);
	ClassDB::bind_method(D_METHOD("get_id"), &NHPBeamCircle::get_id);
	ClassDB::bind_method(D_METHOD("get_c"), &NHPBeamCircle::get_c);
	ClassDB::bind_method(D_METHOD("get_r"), &NHPBeamCircle::get_r);
	ClassDB::bind_method(D_METHOD("get_r2"), &NHPBeamCircle::get_r2);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "id"), "", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "c", godot::PROPERTY_HINT_RESOURCE_TYPE, "NCN"), "", "get_c");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "r", godot::PROPERTY_HINT_RESOURCE_TYPE, "NNum"), "", "get_r");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "r2", godot::PROPERTY_HINT_RESOURCE_TYPE, "NNum"), "", "get_r2");
}

Ref<NHPBeamCircle> NHPBeamCircle::wrap(CircleV c) {
	Ref<NHPBeamCircle> o;
	o.instantiate();
	o->circle = std::move(c);
	return o;
}
Ref<NHPBeamCircle> NHPBeamCircle::make(int id, const Ref<NCN> &c, const Ref<NNum> &r) {
	if (c.is_null() || r.is_null()) {
		rc::report_error("NHPBeamCircle.make: null argument");
		return Ref<NHPBeamCircle>();
	}
	return std::visit([&](const auto &cc) -> Ref<NHPBeamCircle> {
		using N = std::decay_t<decltype(cc.re)>;
		const N *rr = num_ref<N>(r);
		if (rr == nullptr) {
			tier_error("NHPBeamCircle.make");
			return Ref<NHPBeamCircle>();
		}
		return wrap(rc::Circle<N>::make(id, cc, *rr));
	}, c->value);
}
int NHPBeamCircle::get_id() const { return std::visit([](const auto &c) { return c.id; }, circle); }
Ref<NCN> NHPBeamCircle::get_c() const { return std::visit([](const auto &c) { return NCN::wrap(c.c); }, circle); }
Ref<NNum> NHPBeamCircle::get_r() const { return std::visit([](const auto &c) { return NNum::wrap(c.r); }, circle); }
Ref<NNum> NHPBeamCircle::get_r2() const { return std::visit([](const auto &c) { return NNum::wrap(c.r2); }, circle); }

// ---------------------------------------------------------------- NBeamGeomSeg

void NBeamGeomSeg::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_id"), &NBeamGeomSeg::get_id);
	ClassDB::bind_method(D_METHOD("get_kind"), &NBeamGeomSeg::get_kind);
	ClassDB::bind_method(D_METHOD("get_full"), &NBeamGeomSeg::get_full);
	ClassDB::bind_method(D_METHOD("get_s"), &NBeamGeomSeg::get_s);
	ClassDB::bind_method(D_METHOD("get_e"), &NBeamGeomSeg::get_e);
	ClassDB::bind_method(D_METHOD("get_v"), &NBeamGeomSeg::get_v);
	ClassDB::bind_method(D_METHOD("get_circ"), &NBeamGeomSeg::get_circ);
	ClassDB::bind_method(D_METHOD("c"), &NBeamGeomSeg::c);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "id"), "", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind"), "", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "full"), "", "get_full");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "s", godot::PROPERTY_HINT_RESOURCE_TYPE, "NCN"), "", "get_s");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "e", godot::PROPERTY_HINT_RESOURCE_TYPE, "NCN"), "", "get_e");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "v", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_v");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "circ", godot::PROPERTY_HINT_NONE, "", godot::PROPERTY_USAGE_DEFAULT | godot::PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_circ");
}

Ref<NBeamGeomSeg> NBeamGeomSeg::wrap(SegV s) {
	Ref<NBeamGeomSeg> o;
	o.instantiate();
	o->seg = std::move(s);
	return o;
}
int NBeamGeomSeg::get_id() const { return std::visit([](const auto &s) { return s.id; }, seg); }
int NBeamGeomSeg::get_kind() const { return std::visit([](const auto &s) { return int(s.kind); }, seg); }
bool NBeamGeomSeg::get_full() const { return std::visit([](const auto &s) { return s.full; }, seg); }
Ref<NCN> NBeamGeomSeg::get_s() const { return std::visit([](const auto &s) { return NCN::wrap(s.s); }, seg); }
Ref<NCN> NBeamGeomSeg::get_e() const { return std::visit([](const auto &s) { return NCN::wrap(s.e); }, seg); }
Variant NBeamGeomSeg::get_v() const {
	return std::visit([](const auto &s) -> Variant { return s.v ? Variant(NCN::wrap(*s.v)) : Variant(); }, seg);
}
Variant NBeamGeomSeg::get_circ() const {
	return std::visit([](const auto &s) -> Variant { return s.circ ? Variant(NHPBeamCircle::wrap(CircleV(*s.circ))) : Variant(); }, seg);
}
Ref<NCN> NBeamGeomSeg::c() const {
	return std::visit([](const auto &s) -> Ref<NCN> { return s.circ ? NCN::wrap(s.circ->c) : Ref<NCN>(); }, seg);
}

// NBeamSceneSurf.seg (declared in gd_scene.hpp)
Ref<NBeamGeomSeg> NBeamSceneSurf::get_seg() const {
	return visit([](const auto &s) { return NBeamGeomSeg::wrap(SegV(s.seg)); });
}

// ---------------------------------------------------------------- NBeamGeom

void NBeamGeom::_bind_methods() {
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("seg_from_dict", "ctx", "d"), &NBeamGeom::seg_from_dict);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("seg_from_segment", "ctx", "segment", "sid"), &NBeamGeom::seg_from_segment);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("circumcenter", "A", "B", "C"), &NBeamGeom::circumcenter);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("hits", "beam", "seg", "depart_on"), &NBeamGeom::hits);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("hit_records", "beam", "seg", "depart_on", "known"), &NBeamGeom::hit_records, DEFVAL(Array()));
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("on_segment_value", "seg", "p"), &NBeamGeom::on_segment_value);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("on_segment", "seg", "p"), &NBeamGeom::on_segment);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("eq", "a", "b"), &NBeamGeom::eq);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("endpoint_at", "seg", "p"), &NBeamGeom::endpoint_at);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("grad", "seg", "p"), &NBeamGeom::grad);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("winding", "seg"), &NBeamGeom::winding);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("side_left", "seg", "p", "dir"), &NBeamGeom::side_left);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("tangent_into", "seg", "which"), &NBeamGeom::tangent_into);
	ClassDB::bind_static_method("NBeamGeom", D_METHOD("blocked_sides", "seg", "which", "dir"), &NBeamGeom::blocked_sides);
	BIND_ENUM_CONSTANT(LINE);
	BIND_ENUM_CONSTANT(CIRCLE);
}

namespace {

double num_at(const Variant &arr, int64_t i) {
	const Array a = arr;
	return double(a[i]);
}

Ref<NBeamGeomSeg> seg_from_def(const Ref<NNumCtx> &ctx, const rc::SegDef &def) {
	return with_like(ctx, [&](const auto &like) { return NBeamGeomSeg::wrap(SegV(rc::geom::seg_from_def(def, like))); });
}

} // namespace

Ref<NBeamGeomSeg> NBeamGeom::seg_from_dict(const Ref<NNumCtx> &ctx, const Dictionary &d) {
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
		rc::report_error("NBeamGeom.seg_from_dict: kind is line | arc | full_circle");
	}
	return seg_from_def(ctx, def);
}

Ref<NBeamGeomSeg> NBeamGeom::seg_from_segment(const Ref<NNumCtx> &ctx, const Variant &segment, int sid) {
	// BeamGeom.seg_from_segment: a game Segment (start / end / via Points, full, get_carrier())
	godot::Object *seg = segment;
	if (seg == nullptr) {
		rc::report_error("NBeamGeom.seg_from_segment: null segment");
		return Ref<NBeamGeomSeg>();
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

Ref<NCN> NBeamGeom::circumcenter(const Ref<NCN> &A, const Ref<NCN> &B, const Ref<NCN> &C) {
	if (A.is_null()) {
		return Ref<NCN>();
	}
	return std::visit([&](const auto &a) -> Ref<NCN> {
		using N = std::decay_t<decltype(a.re)>;
		const rc::CN<N> *b = cn_of<N>(B);
		const rc::CN<N> *c = cn_of<N>(C);
		if (b == nullptr || c == nullptr) {
			tier_error("NBeamGeom.circumcenter");
			return Ref<NCN>();
		}
		return NCN::wrap(rc::geom::circumcenter(a, *b, *c));
	}, A->value);
}

// Runs f(beam, seg) at the beam's tier.
template <class F>
static auto with_beam_seg(const char *where, const Ref<NHPBeam> &beam, const Ref<NBeamGeomSeg> &seg, F &&f) {
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

Array NBeamGeom::hits(const Ref<NHPBeam> &beam, const Ref<NBeamGeomSeg> &seg, bool depart_on) {
	return with_beam_seg("NBeamGeom.hits", beam, seg, [&](const auto &b, const auto &sg) { return nums_array(rc::geom::hits(b, sg, depart_on)); });
}

Array NBeamGeom::hit_records(const Ref<NHPBeam> &beam, const Ref<NBeamGeomSeg> &seg, bool depart_on, const Array &known) {
	// BeamGeom.hit_records: {t, point, at_endpoint, on_segment} per root; known
	// points at an endpoint replace the nearest root (provenance snap).
	return with_beam_seg("NBeamGeom.hit_records", beam, seg, [&](const auto &b, const auto &sg) -> Array {
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
			const rc::CN<N> *k0 = cn_of<N>(Ref<NCN>(kp[0]));
			const N *k1 = num_ref<N>(Ref<NNum>(kp[1]));
			if (k0 == nullptr || k1 == nullptr) {
				tier_error("NBeamGeom.hit_records");
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
			d["t"] = NNum::wrap(NumV(r.t));
			d["point"] = NCN::wrap(CNV(r.point));
			d["at_endpoint"] = r.at_endpoint;
			d["on_segment"] = rc::geom::on_segment(sg, r.point);
			out.push_back(d);
		}
		return out;
	});
}

// Runs f(seg, p) at the seg's tier.
template <class F>
static auto with_seg_cn(const char *where, const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &p, F &&f) {
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

Ref<NNum> NBeamGeom::on_segment_value(const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &p) {
	return with_seg_cn("NBeamGeom.on_segment_value", seg, p, [](const auto &sg, const auto &pp) { return NNum::wrap(NumV(rc::geom::on_segment_value(sg, pp))); });
}
bool NBeamGeom::on_segment(const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &p) {
	return with_seg_cn("NBeamGeom.on_segment", seg, p, [](const auto &sg, const auto &pp) { return rc::geom::on_segment(sg, pp); });
}
bool NBeamGeom::eq(const Ref<NCN> &a, const Ref<NCN> &b) {
	if (a.is_null() || b.is_null()) {
		return false;
	}
	return std::visit([&](const auto &x) -> bool {
		using N = std::decay_t<decltype(x.re)>;
		const rc::CN<N> *y = cn_of<N>(b);
		return y != nullptr && rc::geom::eq(x, *y);
	}, a->value);
}
int NBeamGeom::endpoint_at(const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &p) {
	return with_seg_cn("NBeamGeom.endpoint_at", seg, p, [](const auto &sg, const auto &pp) { return rc::geom::endpoint_at(sg, pp); });
}
Ref<NCN> NBeamGeom::grad(const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &p) {
	return with_seg_cn("NBeamGeom.grad", seg, p, [](const auto &sg, const auto &pp) { return NCN::wrap(CNV(rc::geom::grad(sg, pp))); });
}
Ref<NNum> NBeamGeom::winding(const Ref<NBeamGeomSeg> &seg) {
	if (seg.is_null()) {
		return Ref<NNum>();
	}
	return std::visit([](const auto &sg) { return NNum::wrap(NumV(rc::geom::winding(sg))); }, seg->seg);
}
bool NBeamGeom::side_left(const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &p, const Ref<NCN> &dir) {
	return with_seg_cn("NBeamGeom.side_left", seg, p, [&](const auto &sg, const auto &pp) -> bool {
		using N = std::decay_t<decltype(pp.re)>;
		const rc::CN<N> *dd = cn_of<N>(dir);
		if (dd == nullptr) {
			tier_error("NBeamGeom.side_left");
			return false;
		}
		return rc::geom::side_left(sg, pp, *dd);
	});
}
Ref<NCN> NBeamGeom::tangent_into(const Ref<NBeamGeomSeg> &seg, int which) {
	if (seg.is_null()) {
		return Ref<NCN>();
	}
	return std::visit([&](const auto &sg) { return NCN::wrap(CNV(rc::geom::tangent_into(sg, which))); }, seg->seg);
}
Array NBeamGeom::blocked_sides(const Ref<NBeamGeomSeg> &seg, int which, const Ref<NCN> &dir) {
	return with_seg_cn("NBeamGeom.blocked_sides", seg, dir, [&](const auto &sg, const auto &dd) -> Array {
		const rc::geom::Blocked bl = rc::geom::blocked_sides(sg, which, dd);
		Array a;
		a.push_back(bl.left);
		a.push_back(bl.right);
		return a;
	});
}

// ---------------------------------------------------------------- NBeamEffects

void NBeamEffects::_bind_methods() {
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("apply", "beam", "t", "surf", "etype", "h"), &NBeamEffects::apply, DEFVAL(Variant()));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("portal", "beam", "t", "surf", "h_exact"), &NBeamEffects::portal, DEFVAL(Variant()));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("project", "beam", "t", "surf", "left", "h_exact"), &NBeamEffects::project, DEFVAL(Variant()));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("projective_exit", "surf", "left", "h"), &NBeamEffects::projective_exit);
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("back_propagate", "surf", "left", "X", "x_on_carrier", "click"), &NBeamEffects::back_propagate,
			DEFVAL(false), DEFVAL(Variant()));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("back_propagate_candidates", "surf", "left", "X", "x_on_carrier"),
			&NBeamEffects::back_propagate_candidates, DEFVAL(false));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("portal_point", "z", "surf", "inverse"), &NBeamEffects::portal_point, DEFVAL(false));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("u2", "seg"), &NBeamEffects::u2);
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("reflect_line", "beam", "t", "seg", "h_exact"), &NBeamEffects::reflect_line, DEFVAL(Variant()));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("map_point", "z", "surf", "inverse"), &NBeamEffects::map_point, DEFVAL(false));
	ClassDB::bind_static_method("NBeamEffects", D_METHOD("map_pole", "parent", "surf"), &NBeamEffects::map_pole);
}

// Runs f(beam, t, surf, h*) at the beam's tier.
template <class F>
static Ref<NHPBeam> with_beam_surf(const char *where, const Ref<NHPBeam> &beam, const Ref<NNum> &t, const Ref<NBeamSceneSurf> &surf,
		const Ref<NCN> &h, F &&f) {
	if (beam.is_null() || t.is_null() || surf.is_null()) {
		rc::report_error((std::string(where) + ": null argument").c_str());
		return Ref<NHPBeam>();
	}
	return std::visit([&](const auto &bp) -> Ref<NHPBeam> {
		using N = std::decay_t<decltype(bp->kappa)>;
		const N *tt = num_ref<N>(t);
		const rc::Surf<N> *s = surf_of<N>(surf);
		const rc::CN<N> *hh = h.is_null() ? nullptr : cn_of<N>(h);
		if (bp == nullptr || tt == nullptr || s == nullptr || (h.is_valid() && hh == nullptr)) {
			tier_error(where);
			return Ref<NHPBeam>();
		}
		return wrap_beam<N>(f(*bp, *tt, *s, hh));
	}, beam->beam);
}

Ref<NHPBeam> NBeamEffects::apply(const Ref<NHPBeam> &beam, const Ref<NNum> &t, const Ref<NBeamSceneSurf> &surf, const String &etype, const Ref<NCN> &h) {
	(void)etype; // BeamEffects.apply ignores it too: the map is the surface's
	return with_beam_surf("NBeamEffects.apply", beam, t, surf, h, [](const auto &b, const auto &tt, const auto &s, const auto *hh) {
		return rc::effects::apply(b, tt, s, hh);
	});
}
Ref<NHPBeam> NBeamEffects::portal(const Ref<NHPBeam> &beam, const Ref<NNum> &t, const Ref<NBeamSceneSurf> &surf, const Ref<NCN> &h_exact) {
	return with_beam_surf("NBeamEffects.portal", beam, t, surf, h_exact, [](const auto &b, const auto &tt, const auto &s, const auto *hh) {
		return rc::effects::portal(b, tt, s, hh);
	});
}
Ref<NHPBeam> NBeamEffects::project(const Ref<NHPBeam> &beam, const Ref<NNum> &t, const Ref<NBeamSceneSurf> &surf, bool left, const Ref<NCN> &h_exact) {
	return with_beam_surf("NBeamEffects.project", beam, t, surf, h_exact, [&](const auto &b, const auto &tt, const auto &s, const auto *hh) {
		return rc::effects::project(b, tt, s, left, hh);
	});
}

// Runs f(surf, X) at X's tier.
template <class F>
static auto with_surf_cn(const char *where, const Ref<NBeamSceneSurf> &surf, const Ref<NCN> &X, F &&f) {
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

Ref<NCN> NBeamEffects::projective_exit(const Ref<NBeamSceneSurf> &surf, bool left, const Ref<NCN> &h) {
	return with_surf_cn("NBeamEffects.projective_exit", surf, h, [&](const auto &s, const auto &hh) { return NCN::wrap(CNV(rc::effects::projective_exit(s, left, hh))); });
}
Variant NBeamEffects::back_propagate(const Ref<NBeamSceneSurf> &surf, bool left, const Ref<NCN> &X, bool x_on_carrier, const Variant &click) {
	return with_surf_cn("NBeamEffects.back_propagate", surf, X, [&](const auto &s, const auto &x) -> Variant {
		using N = std::decay_t<decltype(x.re)>;
		std::optional<rc::effects::F32Point> c;
		if (click.get_type() == Variant::VECTOR2) {
			const Vector2 v = click;
			c = rc::effects::F32Point{float(v.x), float(v.y)};
		}
		const std::optional<rc::CN<N>> h = rc::effects::back_propagate(s, left, x, x_on_carrier, c ? &*c : nullptr);
		return h ? Variant(NCN::wrap(CNV(*h))) : Variant();
	});
}
Array NBeamEffects::back_propagate_candidates(const Ref<NBeamSceneSurf> &surf, bool left, const Ref<NCN> &X, bool x_on_carrier) {
	return with_surf_cn("NBeamEffects.back_propagate_candidates", surf, X, [&](const auto &s, const auto &x) -> Array {
		Array out;
		for (const auto &c : rc::effects::back_propagate_candidates(s, left, x, x_on_carrier)) {
			Array pair;
			pair.push_back(NCN::wrap(CNV(c.h)));
			pair.push_back(c.direct);
			out.push_back(pair);
		}
		return out;
	});
}
Ref<NCN> NBeamEffects::portal_point(const Ref<NCN> &z, const Ref<NBeamSceneSurf> &surf, bool inverse) {
	return with_surf_cn("NBeamEffects.portal_point", surf, z, [&](const auto &s, const auto &zz) { return NCN::wrap(CNV(rc::effects::portal_point(zz, s, inverse))); });
}
Ref<NCN> NBeamEffects::u2(const Ref<NBeamGeomSeg> &seg) {
	if (seg.is_null()) {
		return Ref<NCN>();
	}
	return std::visit([](const auto &sg) { return NCN::wrap(CNV(rc::effects::u2(sg))); }, seg->seg);
}
Ref<NHPBeam> NBeamEffects::reflect_line(const Ref<NHPBeam> &beam, const Ref<NNum> &t, const Ref<NBeamGeomSeg> &seg, const Ref<NCN> &h_exact) {
	if (t.is_null()) {
		rc::report_error("NBeamEffects.reflect_line: null t");
		return Ref<NHPBeam>();
	}
	return with_beam_seg("NBeamEffects.reflect_line", beam, seg, [&](const auto &b, const auto &sg) -> Ref<NHPBeam> {
		using N = std::decay_t<decltype(b.kappa)>;
		const N *tt = num_ref<N>(t);
		const rc::CN<N> *hh = h_exact.is_null() ? nullptr : cn_of<N>(h_exact);
		if (tt == nullptr || (h_exact.is_valid() && hh == nullptr)) {
			tier_error("NBeamEffects.reflect_line");
			return Ref<NHPBeam>();
		}
		return wrap_beam<N>(rc::effects::reflect_line(b, *tt, sg, hh));
	});
}
Variant NBeamEffects::map_point(const Variant &z, const Ref<NBeamSceneSurf> &surf, bool inverse) {
	if (surf.is_null()) {
		return Variant();
	}
	// z: NCN or null (the point at infinity) at the surface's tier
	return surf->visit([&](const auto &s) -> Variant {
		using N = std::decay_t<decltype(s.sx)>;
		(void)sizeof(N);
		using SN = std::decay_t<decltype(s.seg.s.re)>;
		std::optional<rc::CN<SN>> zz;
		if (z.get_type() == Variant::OBJECT) {
			const Ref<NCN> zr = z;
			const rc::CN<SN> *p = cn_of<SN>(zr);
			if (p == nullptr) {
				tier_error("NBeamEffects.map_point");
				return Variant();
			}
			zz = *p;
		}
		const std::optional<rc::CN<SN>> r = rc::effects::map_point(zz, s, inverse);
		return r ? Variant(NCN::wrap(CNV(*r))) : Variant();
	});
}
Array NBeamEffects::map_pole(const Array &parent, const Ref<NBeamSceneSurf> &surf) {
	if (surf.is_null() || parent.size() < 3) {
		rc::report_error("NBeamEffects.map_pole: parent is [P, Q or null, delta or null]");
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
			const rc::CN<SN> *p = cn_of<SN>(Ref<NCN>(v));
			if (p == nullptr) {
				tier_error("NBeamEffects.map_pole");
				return Array();
			}
			(i == 0 ? poles.P : (i == 1 ? poles.Q : poles.delta)) = *p;
		}
		const rc::effects::Poles<SN> r = rc::effects::map_pole(poles, s);
		Array out;
		for (const std::optional<rc::CN<SN>> *q : {&r.P, &r.Q, &r.delta}) {
			out.push_back(*q ? Variant(NCN::wrap(CNV(**q))) : Variant());
		}
		return out;
	});
}

// ---------------------------------------------------------------- NExact

void NExact::_bind_methods() {
	ClassDB::bind_static_method("NExact", D_METHOD("U"), &NExact::U);
	ClassDB::bind_static_method("NExact", D_METHOD("of", "x"), &NExact::of);
	ClassDB::bind_static_method("NExact", D_METHOD("mul", "a", "b"), &NExact::mul);
	ClassDB::bind_static_method("NExact", D_METHOD("add", "a", "b"), &NExact::add);
	ClassDB::bind_static_method("NExact", D_METHOD("sub", "a", "b"), &NExact::sub);
	ClassDB::bind_static_method("NExact", D_METHOD("emul", "x", "y"), &NExact::emul);
	ClassDB::bind_static_method("NExact", D_METHOD("eadd", "x", "y"), &NExact::eadd);
	ClassDB::bind_static_method("NExact", D_METHOD("esub", "x", "y"), &NExact::esub);
	ClassDB::bind_static_method("NExact", D_METHOD("orient_zero", "ax", "ay", "bx", "by", "cx", "cy"), &NExact::orient_zero);
	ClassDB::bind_static_method("NExact", D_METHOD("on_circle_zero", "cx", "cy", "r", "kx", "ky"), &NExact::on_circle_zero);
	ClassDB::bind_static_method("NExact", D_METHOD("incircle_zero", "ax", "ay", "bx", "by", "cx", "cy", "dx", "dy"), &NExact::incircle_zero);
	ClassDB::bind_static_method("NExact", D_METHOD("line_from_dir", "px", "py", "dx", "dy"), &NExact::line_from_dir);
	ClassDB::bind_static_method("NExact", D_METHOD("line_through", "px", "py", "qx", "qy"), &NExact::line_through);
	ClassDB::bind_static_method("NExact", D_METHOD("reflect_point_h", "X", "sx", "sy", "ex", "ey"), &NExact::reflect_point_h);
	ClassDB::bind_static_method("NExact", D_METHOD("invert_point_h", "X", "C", "rw2"), &NExact::invert_point_h);
	ClassDB::bind_static_method("NExact", D_METHOD("line_through_h", "px", "py", "X"), &NExact::line_through_h);
	ClassDB::bind_static_method("NExact", D_METHOD("circumcenter_h", "ax", "ay", "bx", "by", "cx", "cy"), &NExact::circumcenter_h);
	ClassDB::bind_static_method("NExact", D_METHOD("on_line_h", "L", "X"), &NExact::on_line_h);
	ClassDB::bind_static_method("NExact", D_METHOD("tangent_zero", "L", "C", "rw2"), &NExact::tangent_zero);
	ClassDB::bind_static_method("NExact", D_METHOD("reflect_line", "L", "sx", "sy", "ex", "ey"), &NExact::reflect_line);
}

Ref<NNum> NExact::of(double x) { return NNum::wrap(NumV(rc::exact::of(x))); }

namespace {
Ref<NNum> big_op(const Ref<NNum> &a, const Ref<NNum> &b, const char *where, rc::BigFloat (*op)(const rc::BigFloat &, const rc::BigFloat &)) {
	const rc::BigFloat *x = num_ref<rc::BigFloat>(a);
	const rc::BigFloat *y = num_ref<rc::BigFloat>(b);
	if (x == nullptr || y == nullptr) {
		rc::report_error((std::string(where) + ": BigFloat arguments").c_str());
		return Ref<NNum>();
	}
	return NNum::wrap(NumV(op(*x, *y)));
}
} // namespace

Ref<NNum> NExact::mul(const Ref<NNum> &a, const Ref<NNum> &b) { return big_op(a, b, "NExact.mul", &rc::exact::mul); }
Ref<NNum> NExact::add(const Ref<NNum> &a, const Ref<NNum> &b) { return big_op(a, b, "NExact.add", &rc::exact::add); }
Ref<NNum> NExact::sub(const Ref<NNum> &a, const Ref<NNum> &b) { return big_op(a, b, "NExact.sub", &rc::exact::sub); }
Array NExact::emul(const Array &x, const Array &y) { return eb_array(rc::exact::emul(eb_of(x), eb_of(y))); }
Array NExact::eadd(const Array &x, const Array &y) { return eb_array(rc::exact::eadd(eb_of(x), eb_of(y))); }
Array NExact::esub(const Array &x, const Array &y) { return eb_array(rc::exact::esub(eb_of(x), eb_of(y))); }
bool NExact::orient_zero(double ax, double ay, double bx, double by, double cx, double cy) { return rc::exact::orient_zero(ax, ay, bx, by, cx, cy); }
bool NExact::on_circle_zero(double cx, double cy, double r, double kx, double ky) { return rc::exact::on_circle_zero(cx, cy, r, kx, ky); }
bool NExact::incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
	return rc::exact::incircle_zero(ax, ay, bx, by, cx, cy, dx, dy);
}
Array NExact::line_from_dir(double px, double py, double dx, double dy) { return triple_array(rc::exact::line_from_dir(px, py, dx, dy)); }
Array NExact::line_through(double px, double py, double qx, double qy) { return triple_array(rc::exact::line_through(px, py, qx, qy)); }
Array NExact::reflect_point_h(const Array &X, double sx, double sy, double ex, double ey) {
	rc::exact::Point3 x;
	if (!triple_of(X, x, "NExact.reflect_point_h")) {
		return Array();
	}
	return triple_array(rc::exact::reflect_point_h(x, sx, sy, ex, ey));
}
Variant NExact::invert_point_h(const Array &X, const Array &C, const Ref<NNum> &rw2) {
	rc::exact::Point3 x, c;
	const rc::BigFloat *r = num_ref<rc::BigFloat>(rw2);
	if (!triple_of(X, x, "NExact.invert_point_h") || !triple_of(C, c, "NExact.invert_point_h") || r == nullptr) {
		return Variant();
	}
	const std::optional<rc::exact::Point3> p = rc::exact::invert_point_h(x, c, *r);
	return p ? Variant(triple_array(*p)) : Variant();
}
Array NExact::line_through_h(double px, double py, const Array &X) {
	rc::exact::Point3 x;
	if (!triple_of(X, x, "NExact.line_through_h")) {
		return Array();
	}
	return triple_array(rc::exact::line_through_h(px, py, x));
}
Array NExact::circumcenter_h(double ax, double ay, double bx, double by, double cx, double cy) {
	return triple_array(rc::exact::circumcenter_h(ax, ay, bx, by, cx, cy));
}
bool NExact::on_line_h(const Array &L, const Array &X) {
	rc::exact::Line3 l;
	rc::exact::Point3 x;
	if (!triple_of(L, l, "NExact.on_line_h") || !triple_of(X, x, "NExact.on_line_h")) {
		return false;
	}
	return rc::exact::on_line_h(l, x);
}
bool NExact::tangent_zero(const Array &L, const Array &C, const Ref<NNum> &rw2) {
	rc::exact::Line3 l;
	rc::exact::Point3 c;
	const rc::BigFloat *r = num_ref<rc::BigFloat>(rw2);
	if (!triple_of(L, l, "NExact.tangent_zero") || !triple_of(C, c, "NExact.tangent_zero") || r == nullptr) {
		return false;
	}
	return rc::exact::tangent_zero(l, c, *r);
}
Array NExact::reflect_line(const Array &L, double sx, double sy, double ex, double ey) {
	rc::exact::Line3 l;
	if (!triple_of(L, l, "NExact.reflect_line")) {
		return Array();
	}
	return triple_array(rc::exact::reflect_line(l, sx, sy, ex, ey));
}

// ---------------------------------------------------------------- NHPBeam: inf_like / through / invert_point / hits / invert_at

Ref<NNum> NHPBeam::inf_like(const Ref<NNum> &x) {
	if (x.is_null()) {
		return Ref<NNum>();
	}
	return std::visit([](const auto &v) { return NNum::wrap(NumV(v.inf_like())); }, x->value);
}

Ref<NHPBeam> NHPBeam::through(const Ref<NCN> &P, const Ref<NCN> &X, const Variant &delta) {
	if (P.is_null() || X.is_null()) {
		rc::report_error("NHPBeam.through: null argument");
		return Ref<NHPBeam>();
	}
	return std::visit([&](const auto &p) -> Ref<NHPBeam> {
		using N = std::decay_t<decltype(p.re)>;
		const rc::CN<N> *x = cn_of<N>(X);
		const rc::CN<N> *d = nullptr;
		if (delta.get_type() == Variant::OBJECT) {
			d = cn_of<N>(Ref<NCN>(delta));
			if (d == nullptr) {
				tier_error("NHPBeam.through");
				return Ref<NHPBeam>();
			}
		}
		if (x == nullptr) {
			tier_error("NHPBeam.through");
			return Ref<NHPBeam>();
		}
		return wrap_beam<N>(rc::Beam<N>::through(p, *x, d));
	}, P->value);
}

Ref<NCN> NHPBeam::invert_point(const Ref<NCN> &z, const Variant &circ) {
	const Ref<NHPBeamCircle> c = circ;
	if (z.is_null() || c.is_null()) {
		rc::report_error("NHPBeam.invert_point: null argument");
		return Ref<NCN>();
	}
	return std::visit([&](const auto &zz) -> Ref<NCN> {
		using N = std::decay_t<decltype(zz.re)>;
		const rc::Circle<N> *cc = circle_of<N>(c);
		if (cc == nullptr) {
			tier_error("NHPBeam.invert_point");
			return Ref<NCN>();
		}
		return NCN::wrap(CNV(rc::Beam<N>::invert_point(zz, *cc)));
	}, z->value);
}

Array NHPBeam::hits(const Variant &circ, bool depart_on) const {
	const Ref<NHPBeamCircle> c = circ;
	if (c.is_null() || null_beam_v(beam)) {
		rc::report_error("NHPBeam.hits: null argument");
		return Array();
	}
	return std::visit([&](const auto &bp) -> Array {
		using N = std::decay_t<decltype(bp->kappa)>;
		const rc::Circle<N> *cc = circle_of<N>(c);
		if (cc == nullptr) {
			tier_error("NHPBeam.hits");
			return Array();
		}
		return nums_array(bp->hits(*cc, depart_on));
	}, beam);
}

Ref<NHPBeam> NHPBeam::invert_at(const Ref<NNum> &t, const Variant &circ, const Ref<NCN> &h_exact) const {
	const Ref<NHPBeamCircle> c = circ;
	if (t.is_null() || c.is_null() || null_beam_v(beam)) {
		rc::report_error("NHPBeam.invert_at: null argument");
		return Ref<NHPBeam>();
	}
	return std::visit([&](const auto &bp) -> Ref<NHPBeam> {
		using N = std::decay_t<decltype(bp->kappa)>;
		const N *tt = num_ref<N>(t);
		const rc::Circle<N> *cc = circle_of<N>(c);
		const rc::CN<N> *hh = h_exact.is_null() ? nullptr : cn_of<N>(h_exact);
		if (tt == nullptr || cc == nullptr || (h_exact.is_valid() && hh == nullptr)) {
			tier_error("NHPBeam.invert_at");
			return Ref<NHPBeam>();
		}
		return wrap_beam<N>(bp->invert_at(*tt, *cc, hh));
	}, beam);
}
