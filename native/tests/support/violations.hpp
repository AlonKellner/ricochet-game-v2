// A violation's message with its arguments (Godot's % formatting, the subset
// the kernel uses), as the binding renders it for GUT. Test support.
#pragma once

#include "rc/invariants.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <variant>

namespace rc_test {

inline std::string render(const rc::inv::Violation &vi) {
	std::string out;
	size_t ai = 0;
	for (size_t i = 0; i < vi.fmt.size(); i++) {
		if (vi.fmt[i] != '%' || i + 1 >= vi.fmt.size()) {
			out += vi.fmt[i];
			continue;
		}
		size_t j = i + 1;
		while (j < vi.fmt.size() && (std::isdigit(static_cast<unsigned char>(vi.fmt[j])) || vi.fmt[j] == '.')) j++;
		const std::string spec = vi.fmt.substr(i, j - i + 1);
		const rc::inv::Arg &a = ai < vi.args.size() ? vi.args[ai++] : rc::inv::Arg(std::string("?"));
		char buf[256];
		if (const auto *x = std::get_if<int64_t>(&a)) std::snprintf(buf, sizeof buf, "%lld", (long long)*x);
		else if (const auto *d = std::get_if<double>(&a)) std::snprintf(buf, sizeof buf, spec.c_str(), *d);
		else if (const auto *str = std::get_if<std::string>(&a)) std::snprintf(buf, sizeof buf, "%s", str->c_str());
		else std::snprintf(buf, sizeof buf, "?");
		out += buf;
		i = j;
	}
	return out;
}

} // namespace rc_test
