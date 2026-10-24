// Trace state flags (GAME_SPEC 9.4 / 10.6), Stage 93: the port of the
// Dictionary of flags and of StateChange.flag_key / apply_state. Insertion
// order is kept (a categorical table's first-entry fallback and the frame
// identity depend on it); a value is a bool, a float64 or a string, as JSON
// and GDScript Variants carry them.
#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rc {

using FlagValue = std::variant<bool, double, std::string>;

// Canonical table key of a flag value: its JSON spelling (true / false, an
// integral number without a fraction, a string as is).
std::string flag_key(const FlagValue &v);
// GDScript bool(v): a bool itself, a non-zero number, a non-empty string.
bool flag_truth(const FlagValue &v);
// Stage 141: flag_key(v) == s, compared in place (no string built): the
// presence conditions and the categorical tables ask this per effect lookup.
bool flag_key_is(const FlagValue &v, const std::string &s);

struct StateChangeDef {
	std::string key;
	bool toggle = false; // op: "toggle" | "set"
	FlagValue value = true;
	bool operator==(const StateChangeDef &) const = default;
};

class Flags {
public:
	using Entry = std::pair<std::string, FlagValue>;

	Flags() = default;

	bool has(const std::string &key) const;
	const FlagValue *get(const std::string &key) const;
	// Set or append (insertion order of a new key: last).
	void set(const std::string &key, FlagValue value);
	void erase(const std::string &key);
	const std::vector<Entry> &entries() const { return entries_; }
	bool empty() const { return entries_.empty(); }
	size_t size() const { return entries_.size(); }

	// The flags after a state change (a new object; toggle stores the negation
	// of the current truth value, a missing key being false).
	Flags applied(const StateChangeDef &change) const;
	// Stage 107: a side's state changes, in order (last writer wins)
	Flags applied(const std::vector<StateChangeDef> &changes) const {
		Flags f = *this;
		for (const StateChangeDef &c : changes) {
			f = f.applied(c);
		}
		return f;
	}
	// flags.get(key) == true: the value is the bool true.
	bool is_true(const std::string &key) const;
	// Canonical form: sorted keys, "k=v" joined by ",", JSON spelling (BeamTracer._flags_key).
	// (Stage 141: computed once per value and kept with it -- every event's copy of
	// the walk's flags carries it; set and erase drop it.)
	const std::string &canonical_key() const;
	bool operator==(const Flags &o) const { return entries_ == o.entries_; }

private:
	std::vector<Entry> entries_;
	bool ghost_ = false; // (A.7: is_true("ghost"), asked per effect lookup, kept current by set and erase)
	mutable bool key_ok_ = false;
	mutable std::string key_;
};

} // namespace rc
