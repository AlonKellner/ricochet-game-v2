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
	// flags.get(key) == true: the value is the bool true.
	bool is_true(const std::string &key) const;
	// Canonical form: sorted keys, "k=v" joined by ",", JSON spelling (BeamTracer._flags_key).
	std::string canonical_key() const;
	bool operator==(const Flags &o) const { return entries_ == o.entries_; }

private:
	std::vector<Entry> entries_;
};

} // namespace rc
