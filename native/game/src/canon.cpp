#include "rcg/canon.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace rcg {
namespace {

using json = nlohmann::json;

bool scalar(const json &v) { return !v.is_array() && !v.is_object(); }

// A JSON string as Python's json.dumps(s, ensure_ascii=False) writes it.
std::string quote(const std::string &s) {
	std::string out = "\"";
	for (const char ch : s) {
		const auto c = static_cast<unsigned char>(ch);
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof buf, "\\u%04x", c);
					out += buf;
				} else {
					out += ch;
				}
		}
	}
	return out + "\"";
}

void write(const json &v, int depth, std::string &out) {
	const std::string pad(size_t(depth + 1), ' ');
	if (v.is_object()) {
		if (v.empty()) {
			out += "{}";
			return;
		}
		out += "{\n";
		bool first = true;
		for (auto it = v.begin(); it != v.end(); ++it) { // nlohmann::json keeps its keys sorted
			if (!first) {
				out += ",\n";
			}
			first = false;
			out += pad + quote(it.key()) + ": ";
			write(it.value(), depth + 1, out);
		}
		out += "\n" + std::string(size_t(depth), ' ') + "}";
	} else if (v.is_array()) {
		bool all_scalars = true;
		for (const json &x : v) {
			all_scalars = all_scalars && scalar(x);
		}
		if (all_scalars) {
			out += "[";
			for (size_t i = 0; i < v.size(); i++) {
				if (i) {
					out += ", ";
				}
				write(v[i], depth, out);
			}
			out += "]";
			return;
		}
		out += "[\n";
		for (size_t i = 0; i < v.size(); i++) {
			if (i) {
				out += ",\n";
			}
			out += pad;
			write(v[i], depth + 1, out);
		}
		out += "\n" + std::string(size_t(depth), ' ') + "]";
	} else if (v.is_number_float()) {
		out += float_repr(v.get<double>());
	} else if (v.is_number_integer()) {
		out += std::to_string(v.get<std::int64_t>());
	} else if (v.is_number_unsigned()) {
		out += std::to_string(v.get<std::uint64_t>());
	} else if (v.is_string()) {
		out += quote(v.get_ref<const std::string &>());
	} else if (v.is_boolean()) {
		out += v.get<bool>() ? "true" : "false";
	} else {
		out += "null";
	}
}

} // namespace

std::string float_repr(double x) {
	if (std::isnan(x)) {
		return "NaN";
	}
	if (std::isinf(x)) {
		return x > 0 ? "Infinity" : "-Infinity";
	}
	// the shortest round-trip digits and the decimal exponent: d.ddd e X
	char buf[64];
	const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
	const std::string_view sci(buf, size_t(r.ptr - buf));
	const size_t e_at = sci.find('e');
	std::string_view mant = sci.substr(0, e_at);
	const int exp = std::stoi(std::string(sci.substr(e_at + 1)));
	std::string sign;
	if (!mant.empty() && mant[0] == '-') {
		sign = "-";
		mant.remove_prefix(1);
	}
	std::string digits;
	for (char c : mant) {
		if (c != '.') {
			digits += c;
		}
	}
	// Python's repr: fixed notation for -4 <= exp < 16, else scientific
	if (exp >= -4 && exp < 16) {
		std::string s;
		if (exp >= 0) {
			const size_t int_len = size_t(exp) + 1;
			if (digits.size() <= int_len) {
				s = digits + std::string(int_len - digits.size(), '0') + ".0";
			} else {
				s = digits.substr(0, int_len) + "." + digits.substr(int_len);
			}
		} else {
			s = "0." + std::string(size_t(-exp - 1), '0') + digits;
		}
		return sign + s;
	}
	std::string s = digits.substr(0, 1);
	if (digits.size() > 1) {
		s += "." + digits.substr(1);
	}
	char eb[16];
	std::snprintf(eb, sizeof eb, "e%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
	return sign + s + eb;
}

std::string canonical(const nlohmann::json &doc) {
	std::string out;
	write(doc, 0, out);
	return out + "\n";
}

} // namespace rcg
