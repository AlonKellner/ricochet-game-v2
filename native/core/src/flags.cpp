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

bool flag_key_is(const FlagValue &v, const std::string &s) {
	if (const bool *b = std::get_if<bool>(&v)) {
		return *b ? s == "true" : s == "false";
	}
	if (const double *d = std::get_if<double>(&v)) {
		if (std::isfinite(*d) && *d == std::floor(*d)) {
			char buf[32];
			std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(*d));
			return s == buf;
		}
		return s == gd_float_str(*d);
	}
	return std::get<std::string>(v) == s;
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

namespace {
const std::string GHOST_KEY = "ghost";
bool truth_bool(const FlagValue &v) {
	const bool *b = std::get_if<bool>(&v);
	return b != nullptr && *b;
}
} // namespace

void Flags::set(const std::string &key, FlagValue value) {
	key_ok_ = false;
	const bool ghost = key == GHOST_KEY;
	for (Entry &e : entries_) {
		if (e.first == key) {
			e.second = std::move(value);
			if (ghost) {
				ghost_ = truth_bool(e.second);
			}
			return;
		}
	}
	entries_.emplace_back(key, std::move(value));
	if (ghost) {
		ghost_ = truth_bool(entries_.back().second);
	}
}

void Flags::erase(const std::string &key) {
	key_ok_ = false;
	entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.first == key; }), entries_.end());
	if (key == GHOST_KEY) {
		ghost_ = false;
	}
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
	if (key.size() == 5 && key == GHOST_KEY) {
		return ghost_;
	}
	const FlagValue *v = get(key);
	if (v == nullptr) {
		return false;
	}
	const bool *b = std::get_if<bool>(v);
	return b != nullptr && *b;
}

const std::string &Flags::canonical_key() const {
	if (key_ok_) {
		return key_;
	}
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
	key_ = std::move(out);
	key_ok_ = true;
	return key_;
}

} // namespace rc
