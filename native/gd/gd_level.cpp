#include "gd_level.hpp"

#include "rcg/build.hpp"
#include "rcg/level.hpp"

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
