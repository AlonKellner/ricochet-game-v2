#include "rcg/landing.hpp"

#include "rcg/model.hpp"

#include <algorithm>

namespace rcg {
namespace {

V2 rot90(const V2 &v) { return V2{-v.y, v.x}; }

// SideSpec.mapped: a directional normal through the frame's basis.
SideSpec mapped_side(const SideSpec &s, const rc::Xf2 &xf) {
	SideSpec o = s;
	if (o.effect.normal) {
		const V2 n = xf.basis_xform(V2::of((*o.effect.normal)[0], (*o.effect.normal)[1]));
		o.effect.normal = std::array<double, 2>{n.x, n.y};
		o.effect.raw["normal"] = nlohmann::json::array({double(n.x), double(n.y)});
	}
	return o;
}

std::optional<SideSpec> mapped_opt(const std::optional<SideSpec> &s, const rc::Xf2 &xf) {
	return s ? std::optional<SideSpec>(mapped_side(*s, xf)) : std::nullopt;
}

bool is_pending(const rc::Flags &flags, const std::string &key) {
	const rc::FlagValue *v = flags.get(key);
	const std::string *str = v ? std::get_if<std::string>(v) : nullptr;
	return str != nullptr && *str == "pending";
}

} // namespace

SurfaceSpec mapped(const SurfaceSpec &s, const rc::Xf2 &xf) {
	SurfaceSpec o;
	o.full_circle = s.full_circle;
	o.straight = !s.full_circle && (s.straight || segment_of(s).is_line());
	o.start = xf.xform(s.start);
	o.end = xf.xform(s.end);
	o.via = xf.xform(s.via);
	o.center = xf.xform(s.center);
	o.radius = s.radius;
	const bool mirrored = xf.determinant() < 0.0f;
	std::optional<SideSpec> l = mapped_opt(s.left, xf);
	std::optional<SideSpec> r = mapped_opt(s.right, xf);
	o.left = mirrored ? r : l;
	o.right = mirrored ? l : r;
	if (s.is_portal()) {
		const double theta = *s.left->effect.theta;
		const V2 d = V2::of((*s.left->effect.d)[0], (*s.left->effect.d)[1]);
		const V2 at0 = xf.xform(apply_motion(xf.affine_inverse().xform(V2{}), theta, d));
		for (std::optional<SideSpec> *side : {&o.left, &o.right}) {
			if (*side) {
				(*side)->effect.theta = mirrored ? -theta : theta;
				(*side)->effect.d = std::array<double, 2>{at0.x, at0.y};
				(*side)->effect.raw["theta"] = mirrored ? -theta : theta;
				(*side)->effect.raw["d"] = nlohmann::json::array({double(at0.x), double(at0.y)});
			}
		}
	}
	o.resolver_key = s.resolver_key;
	for (const ResolverRow &row : s.resolver) {
		std::optional<SideSpec> tl = mapped_opt(row.left, xf);
		std::optional<SideSpec> tr = mapped_opt(row.right, xf);
		o.resolver.push_back({row.value, mirrored ? tr : tl, mirrored ? tl : tr});
	}
	o.presence = s.presence;
	o.player_solid = s.player_solid;
	o.platform = s.platform;
	return o;
}

SpawnMode spawn_mode(const std::string &name) {
	if (name == "BEAM_TANGENT") return SpawnMode::BeamTangent;
	if (name == "BEAM_NORMAL") return SpawnMode::BeamNormal;
	if (name == "CARRIER_NORMAL") return SpawnMode::CarrierNormal;
	if (name == "BEAM_REFLECTED") return SpawnMode::BeamReflected;
	if (name == "WORLD") return SpawnMode::World;
	return SpawnMode::CarrierTangent;
}

V2 carrier_tangent(const V2 &start, const V2 &via, const V2 &end, bool is_line, const V2 &center, const V2 &point) {
	if (is_line) {
		return (end - start).normalized();
	}
	const float turn = (via - start).cross(end - via);
	const V2 t = rot90(point - center).normalized();
	return turn > 0.0f ? t : -t;
}

V2 spawn_axis(SpawnMode mode, const V2 &beam, const V2 &tangent) {
	switch (mode) {
		case SpawnMode::BeamTangent:
			return beam;
		case SpawnMode::BeamNormal:
			return rot90(beam);
		case SpawnMode::CarrierTangent:
			return tangent;
		case SpawnMode::CarrierNormal: {
			const V2 n = rot90(tangent);
			return n.dot(beam) > 0.0f ? -n : n;
		}
		case SpawnMode::BeamReflected: {
			// 2.0 * beam.dot(tangent) * tangent - beam: the float64 scalar rounded to float32
			const float k = float(2.0 * double(beam.dot(tangent)));
			return tangent * k - beam;
		}
		case SpawnMode::World:
			break;
	}
	return V2{1.0f, 0.0f};
}

rc::Xf2 spawn_frame(SpawnMode mode, const V2 &point, const V2 &beam, const V2 &tangent, bool from_right, bool mirror_with_side) {
	const V2 u = spawn_axis(mode, beam, tangent);
	V2 v = rot90(u);
	if (mirror_with_side && from_right) {
		v = -v;
	}
	return rc::Xf2::of(u, v, point);
}

SurfaceSpec place(const SpawnTemplateSpec &t, const BuiltSurface &host, const rc::HitRecord &hit) {
	const SegGeom &seg = host.seg;
	const rc::arc::GeneralizedCircle &carrier = seg.get_carrier();
	const bool is_line = carrier.is_line();
	const V2 tangent = carrier_tangent(seg.s, seg.v, seg.e, is_line, is_line ? V2{} : rc::arc::center_of(carrier), hit.point);
	const rc::Xf2 frame = spawn_frame(spawn_mode(t.orientation), hit.point, hit.tangent, tangent, hit.side == rc::Side::Right,
		t.mirror_with_side);
	return mapped(*t.surface, frame * rc::Xf2::rotation(0.0f, t.offset));
}

SurfaceSpec trim(const SurfaceSpec &spec, const std::vector<SegGeom> &cutters) {
	if (spec.is_portal()) {
		return spec; // a portal pair is not split (GAME_SPEC 9.7)
	}
	const SegGeom seg = segment_of(spec);
	std::vector<std::pair<double, V2>> cuts;
	for (const SegGeom &c : cutters) {
		for (const Crossing &x : crossings_of(seg, c)) {
			cuts.push_back({x.ta, x.point});
		}
	}
	if (cuts.empty() || (seg.full && cuts.size() < 2)) {
		return spec;
	}
	std::sort(cuts.begin(), cuts.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
	const rc::arc::GeneralizedCircle carrier = seg.get_carrier();
	V2 a = seg.s;
	V2 b = seg.e;
	if (seg.full) {
		double best = -1.0;
		for (size_t i = 0; i < cuts.size(); i++) {
			const double t0 = cuts[i].first;
			const double t1 = cuts[(i + 1) % cuts.size()].first + (i + 1 == cuts.size() ? TAU : 0.0);
			if (t1 - t0 > best) {
				best = t1 - t0;
				a = cuts[i].second;
				b = cuts[(i + 1) % cuts.size()].second;
			}
		}
	} else {
		const double keep_t = param(seg, seg.v);
		for (const auto &x : cuts) {
			if (x.first <= keep_t) {
				a = x.second;
			} else if (b == seg.e) {
				b = x.second;
			}
		}
	}
	SurfaceSpec out = mapped(spec, rc::Xf2::identity());
	out.full_circle = false;
	out.start = a;
	out.end = b;
	out.via = between(seg, a, b);
	out.straight = carrier.is_line();
	return out;
}

Landing fold(const std::vector<rc::HitRecord> &hits, const std::vector<BuiltSurface> &world, const rc::Flags &final_flags,
		const std::map<std::string, int> &generations) {
	struct Spawn {
		const BuiltSurface *host;
		const rc::HitRecord *hit;
		const HitSpec *eff;
	};
	rc::Flags flags = final_flags;
	std::map<std::string, Spawn> spawns; // channel -> the hit that last fired its spawn
	for (const rc::HitRecord &h : hits) {
		if (!h.fired) {
			continue;
		}
		auto host = std::find_if(world.begin(), world.end(), [&](const BuiltSurface &s) { return s.id == h.surface_id; });
		if (host == world.end()) {
			continue;
		}
		const BuiltSide *cfg = active_side(*host, h.side == rc::Side::Left ? Side::Left : Side::Right, h.flags_before);
		if (cfg == nullptr) {
			continue;
		}
		for (const HitSpec &eff : cfg->hits) {
			if (eff.deferred) {
				for (const rc::StateChangeDef &w : hit_writes(eff, host->id)) {
					flags = flags.applied(w);
				}
			}
			if (eff.kind == "spawn") {
				spawns[eff.channel] = Spawn{&*host, &h, &eff};
			}
		}
	}
	Landing out;
	out.generations = generations;
	std::vector<std::string> landing;
	for (const auto &[ch, rec] : spawns) { // the channels in sorted order
		if (!is_pending(flags, "chan:" + ch)) {
			continue; // cancelled in the shot (a reset after the spawn)
		}
		const int n = out.generations[ch] + 1;
		out.generations[ch] = n;
		flags.set("chan:" + ch, std::string("g") + std::to_string(n));
		landing.push_back(ch);
	}
	// trimming cuts against what is present once the shot has landed, then the groups placed before
	std::vector<SegGeom> cutters;
	for (const BuiltSurface &s : world) {
		if (!s.pre_transform && present(s, flags)) {
			cutters.push_back(s.seg);
		}
	}
	for (const std::string &ch : landing) {
		const Spawn &rec = spawns.at(ch);
		SurfaceSpec spec = place(*rec.eff->spawn_template, *rec.host, *rec.hit);
		if (rec.eff->spawn_template->trim) {
			spec = trim(spec, cutters);
		}
		spec.presence.push_back({"chan:" + ch, {"g" + std::to_string(out.generations[ch])}});
		cutters.push_back(segment_of(spec));
		out.placed.push_back({ch, out.generations[ch], std::move(spec)});
	}
	out.flags = std::move(flags);
	return out;
}

} // namespace rcg
