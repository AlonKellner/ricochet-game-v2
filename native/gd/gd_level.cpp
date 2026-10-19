#include "gd_level.hpp"

#include "rcg/build.hpp"
#include "rcg/level.hpp"
#include "rcg/model.hpp"
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
