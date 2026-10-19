#include "gd_level.hpp"

#include "rcg/build.hpp"
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/geom.hpp"
#include "rcg/digest.hpp"
#include "rcg/landing.hpp"
#include "rcg/world.hpp"
#include "gd_common.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <nlohmann/json.hpp>

using namespace godot;

namespace {

// A JSON value as a Variant: objects as Dictionaries, arrays as Arrays, ints
// as int, floats as float (float64, correctly rounded by the native reader).
Variant to_variant(const nlohmann::json &j) {
	switch (j.type()) {
		case nlohmann::json::value_t::object: {
			Dictionary d;
			for (auto it = j.begin(); it != j.end(); ++it) {
				d[String::utf8(it.key().c_str())] = to_variant(it.value());
			}
			return d;
		}
		case nlohmann::json::value_t::array: {
			Array a;
			for (const nlohmann::json &x : j) {
				a.push_back(to_variant(x));
			}
			return a;
		}
		case nlohmann::json::value_t::string:
			return String::utf8(j.get_ref<const std::string &>().c_str());
		case nlohmann::json::value_t::boolean:
			return j.get<bool>();
		case nlohmann::json::value_t::number_integer:
			return j.get<int64_t>();
		case nlohmann::json::value_t::number_unsigned:
			return int64_t(j.get<uint64_t>());
		case nlohmann::json::value_t::number_float:
			return j.get<double>();
		default:
			return Variant();
	}
}

} // namespace

void RcgLevel::_bind_methods() {
	ClassDB::bind_static_method("RcgLevel", D_METHOD("parse", "text"), &RcgLevel::parse);
	ClassDB::bind_static_method("RcgLevel", D_METHOD("probe_model", "text", "flags"), &RcgLevel::probe_model);
	ClassDB::bind_static_method("RcgLevel", D_METHOD("probe_pieces", "text"), &RcgLevel::probe_pieces);
	ClassDB::bind_static_method("RcgLevel", D_METHOD("probe_crossings", "a", "b", "points"), &RcgLevel::probe_crossings);
	ClassDB::bind_static_method("RcgLevel", D_METHOD("probe_fold", "text", "hits", "final_flags", "generations"), &RcgLevel::probe_fold);
}


namespace {
Vector2 gv(const rc::V2 &v) { return Vector2(v.x, v.y); }
rc::V2 rv(const Vector2 &v) { return rc::V2{v.x, v.y}; }

rcg::SegGeom seg_of(const Dictionary &d) {
	if (d.has("center")) {
		return rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rv(d["center"]), double(d["radius"])));
	}
	return rc::arc::SegGeom::make(rv(d["start"]), rv(d["end"]), rv(d["via"]));
}
} // namespace

Dictionary RcgLevel::probe_pieces(const String &text) {
	const CharString utf8 = text.utf8();
	const rcg::Pieces p = rcg::build_pieces(rcg::build_level(rcg::parse_level(std::string_view(utf8.get_data(), size_t(utf8.length())))));
	Array pieces;
	for (const auto &[id, list] : p.by_id) {
		for (const rcg::Piece &pc : list) {
			Array row;
			row.push_back(id);
			row.push_back(pc.index);
			row.push_back(gv(pc.segment.s));
			row.push_back(gv(pc.segment.e));
			row.push_back(gv(pc.segment.v));
			if (pc.span) {
				PackedFloat64Array sp;
				for (double x : *pc.span) {
					sp.push_back(x);
				}
				row.push_back(sp);
			} else {
				row.push_back(Variant());
			}
			pieces.push_back(row);
		}
	}
	Array xs;
	for (const rc::V2 &x : p.crossings) {
		xs.push_back(gv(x));
	}
	Dictionary out;
	out["pieces"] = pieces;
	out["crossings"] = xs;
	out["portal_crossings"] = int64_t(p.portal_crossings.size());
	return out;
}

Dictionary RcgLevel::probe_crossings(const Dictionary &a, const Dictionary &b, const PackedVector2Array &points) {
	const rcg::SegGeom sa = seg_of(a), sb = seg_of(b);
	Array xs;
	for (const rcg::Crossing &x : rcg::crossings_of(sa, sb)) {
		Array row;
		row.push_back(gv(x.point));
		row.push_back(x.ta);
		row.push_back(x.tb);
		row.push_back(x.enters);
		xs.push_back(row);
	}
	Array params;
	Array incident;
	for (int64_t i = 0; i < points.size(); i++) {
		params.push_back(rcg::param(sa, rv(points[i])));
		incident.push_back(rcg::incident(sa, rv(points[i])));
	}
	Dictionary out;
	out["crossings"] = xs;
	out["params"] = params;
	out["incident"] = incident;
	out["span_a"] = rcg::span(sa);
	out["span_b"] = rcg::span(sb);
	return out;
}

namespace {
Variant side_ref(const rcg::SideRef &r) {
	Array a;
	a.push_back(r.surface);
	a.push_back(int(r.side));
	return a;
}
} // namespace

Array RcgLevel::probe_model(const String &text, const Dictionary &flags) {
	Array out;
	const CharString utf8 = text.utf8();
	const rc::Flags f = rcgd::flags_from_dict(flags);
	for (const rcg::BuiltSurface &s : rcg::build_level(rcg::parse_level(std::string_view(utf8.get_data(), size_t(utf8.length()))))) {
		Dictionary d;
		d["id"] = s.id;
		d["present"] = rcg::present(s, f);
		Array sides;
		for (const rcg::Side side : {rcg::Side::Left, rcg::Side::Right}) {
			Dictionary sd;
			const rcg::BuiltSide *cfg = rcg::active_side(s, side, f);
			sd["active"] = to_variant(rcg::describe_side(cfg, s.id));
			Array landing;
			if (cfg) {
				for (const rc::StateChangeDef &w : rcg::writes(*cfg, s.id, true)) {
					landing.push_back(to_variant(nlohmann::json{{"key", w.key}, {"op", w.toggle ? "toggle" : "set"},
							{"value", w.toggle ? nlohmann::json(true) : std::visit([](const auto &x) -> nlohmann::json { return x; }, w.value)}}));
				}
			}
			sd["landing"] = landing;
			const std::optional<rcg::OverlayLink> link = rcg::overlay_link(s, side, f);
			if (link) {
				Array l;
				l.push_back(side_ref(link->from));
				l.push_back(link->outgoing ? side_ref(*link->outgoing) : Variant());
				sd["link"] = l;
			} else {
				sd["link"] = Variant();
			}
			sides.push_back(sd);
		}
		d["sides"] = sides;
		out.push_back(d);
	}
	return out;
}

Dictionary RcgLevel::parse(const String &text) {
	Dictionary out;
	const CharString utf8 = text.utf8();
	const std::string_view src(utf8.get_data(), size_t(utf8.length()));
	try {
		rcg::build_level(rcg::parse_level(src));
		out["level"] = to_variant(nlohmann::json::parse(src));
	} catch (const std::exception &e) {
		out["error"] = String::utf8(e.what());
	}
	return out;
}

Dictionary RcgLevel::probe_fold(const String &text, const Array &hits, const Dictionary &final_flags, const Dictionary &generations) {
	const CharString utf8 = text.utf8();
	const rcg::World world(rcg::build_level(rcg::parse_level(std::string_view(utf8.get_data(), size_t(utf8.length())))));
	std::vector<rc::HitRecord> recs;
	for (int64_t i = 0; i < hits.size(); i++) {
		const Dictionary h = hits[i];
		rc::HitRecord r;
		r.surface_id = int(int64_t(h["surface_id"]));
		r.side = int(int64_t(h["side"])) == 0 ? rc::Side::Left : rc::Side::Right;
		r.fired = bool(h["fired"]);
		r.point = rv(h["point"]);
		r.tangent = rv(h["tangent"]);
		r.flags_before = rcgd::flags_from_dict(h["flags_before"]);
		recs.push_back(r);
	}
	std::map<std::string, int> gens;
	const Array keys = generations.keys();
	for (int64_t i = 0; i < keys.size(); i++) {
		gens[rcgd::to_std(String(keys[i]))] = int(int64_t(generations[keys[i]]));
	}
	const rcg::Landing l = rcg::fold(recs, world.surfaces(), rcgd::flags_from_dict(final_flags), gens);
	nlohmann::json placed = nlohmann::json::array();
	for (const rcg::Placed &p : l.placed) {
		placed.push_back({{"channel", p.channel}, {"generation", p.generation}, {"spec", rcg::digest::spec_data(p.spec)}});
	}
	Dictionary g;
	for (const auto &[ch, n] : l.generations) {
		g[String::utf8(ch.c_str())] = n;
	}
	Dictionary out;
	out["flags"] = rcgd::flags_to_dict(l.flags);
	out["generations"] = g;
	out["placed"] = String::utf8(rcg::digest::encode(placed).c_str());
	return out;
}
