#include "rc/flags.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rc {

namespace {

// GDScript str(float) for the non-integral case: shortest round-trip form.
std::string gd_float_str(double v) {
	char buf[32];
	for (int prec = 1; prec <= 17; prec++) {
		std::snprintf(buf, sizeof buf, "%.*g", prec, v);
		if (std::strtod(buf, nullptr) == v) {
			break;
		}
	}
	return buf;
}

} // namespace

std::string flag_key(const FlagValue &v) {
	if (const bool *b = std::get_if<bool>(&v)) {
		return *b ? "true" : "false";
	}
	if (const double *d = std::get_if<double>(&v)) {
		if (std::isfinite(*d) && *d == std::floor(*d)) {
			return std::to_string(static_cast<long long>(*d));
		}
		return gd_float_str(*d);
	}
	return std::get<std::string>(v);
}

bool flag_truth(const FlagValue &v) {
	if (const bool *b = std::get_if<bool>(&v)) {
		return *b;
	}
	if (const double *d = std::get_if<double>(&v)) {
		return *d != 0.0;
	}
	return !std::get<std::string>(v).empty();
}

bool Flags::has(const std::string &key) const {
	return get(key) != nullptr;
}

const FlagValue *Flags::get(const std::string &key) const {
	for (const Entry &e : entries_) {
		if (e.first == key) {
			return &e.second;
		}
	}
	return nullptr;
}

void Flags::set(const std::string &key, FlagValue value) {
	for (Entry &e : entries_) {
		if (e.first == key) {
			e.second = std::move(value);
			return;
		}
	}
	entries_.emplace_back(key, std::move(value));
}

void Flags::erase(const std::string &key) {
	entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.first == key; }), entries_.end());
}

Flags Flags::applied(const StateChangeDef &change) const {
	Flags out = *this;
	if (change.toggle) {
		const FlagValue *cur = get(change.key);
		out.set(change.key, !(cur != nullptr && flag_truth(*cur)));
	} else {
		out.set(change.key, change.value);
	}
	return out;
}

bool Flags::is_true(const std::string &key) const {
	const FlagValue *v = get(key);
	if (v == nullptr) {
		return false;
	}
	const bool *b = std::get_if<bool>(v);
	return b != nullptr && *b;
}

std::string Flags::canonical_key() const {
	std::vector<const Entry *> sorted;
	for (const Entry &e : entries_) {
		sorted.push_back(&e);
	}
	std::sort(sorted.begin(), sorted.end(), [](const Entry *a, const Entry *b) { return a->first < b->first; });
	std::string out;
	for (size_t i = 0; i < sorted.size(); i++) {
		if (i) {
			out += ",";
		}
		out += sorted[i]->first + "=" + flag_key(sorted[i]->second);
	}
	return out;
}

} // namespace rc
