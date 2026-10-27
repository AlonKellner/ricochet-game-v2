#include "rcg/digest.hpp"

#include "rcg/sha256.hpp"

#include <cstdio>
#include <cstring>

namespace rcg::digest {
namespace {

using json = nlohmann::json;

std::string hex_f64(double x) {
	unsigned char b[8];
	std::memcpy(b, &x, 8);
	static const char *digits = "0123456789abcdef";
	std::string out = "\"f";
	for (unsigned char c : b) {
		out += digits[c >> 4];
		out += digits[c & 15];
	}
	return out + "\"";
}

std::string quote(const std::string &s) {
	std::string out = "\"";
	for (const char ch : s) {
		const auto c = static_cast<unsigned char>(ch);
		if (c == '"') {
			out += "\\\"";
		} else if (c == '\\') {
			out += "\\\\";
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\t') {
			out += "\\t";
		} else if (c == '\r') {
			out += "\\r";
		} else if (c < 0x20) {
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		} else {
			out += ch;
		}
	}
	return out + "\"";
}

void write(const json &v, std::string &out) {
	switch (v.type()) {
		case json::value_t::null:
			out += "null";
			return;
		case json::value_t::boolean:
			out += v.get<bool>() ? "true" : "false";
			return;
		case json::value_t::number_integer:
			out += std::to_string(v.get<std::int64_t>());
			return;
		case json::value_t::number_unsigned:
			out += std::to_string(v.get<std::uint64_t>());
			return;
		case json::value_t::number_float:
			out += hex_f64(v.get<double>());
			return;
		case json::value_t::string:
			out += quote(v.get_ref<const std::string &>());
			return;
		case json::value_t::array: {
			out += "[";
			bool first = true;
			for (const json &x : v) {
				if (!first) {
					out += ",";
				}
				first = false;
				write(x, out);
			}
			out += "]";
			return;
		}
		case json::value_t::object: {
			out += "{";
			bool first = true;
			for (auto it = v.begin(); it != v.end(); ++it) { // sorted keys
				if (!first) {
					out += ",";
				}
				first = false;
				out += quote(it.key()) + ":";
				write(it.value(), out);
			}
			out += "}";
			return;
		}
		default:
			out += "null";
	}
}

json side_data(const std::optional<SideSpec> &s) {
	if (!s) {
		return nullptr;
	}
	json hits = json::array();
	for (const HitSpec &h : s->hits) {
		hits.push_back(h.raw);
	}
	return {{"effect", s->effect.raw}, {"hits", hits}, {"interactive", s->interactive}};
}

} // namespace

std::string encode(const json &v) {
	std::string out;
	write(v, out);
	return out;
}

std::string sha256_text(const std::string &text) { return Sha256::hex(text); }

std::string sha(const json &v) { return sha256_text(encode(v)); }

json spec_data(const SurfaceSpec &s) {
	json table = json::object();
	for (const ResolverRow &r : s.resolver) {
		table[r.value] = json::array({side_data(r.left), side_data(r.right)});
	}
	json presence = json::array();
	for (const PresenceSpec &c : s.presence) {
		presence.push_back({{"key", c.key}, {"values", c.values}});
	}
	json out = {
		{"start", vec(s.start)},
		{"end", vec(s.end)},
		{"via", vec(s.via)},
		{"full_circle", s.full_circle},
		{"straight", s.straight},
		{"center", vec(s.center)},
		{"radius", s.radius},
		{"left", side_data(s.left)},
		{"right", side_data(s.right)},
		{"resolver_key", s.resolver_key},
		{"resolver_table", table},
		{"presence", presence},
		{"player_solid", s.player_solid},
	};
	if (s.platform) {
		out["platform"] = true; // only when set: every other digest unchanged
	}
	return out;
}

} // namespace rcg::digest
