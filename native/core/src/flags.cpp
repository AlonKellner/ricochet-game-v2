#include "rc/flags.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

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
	const bool ghost = key == GHOST_KEY, frost = key == FROST;
	for (Entry &e : entries_) {
		if (e.first == key) {
			e.second = std::move(value);
			if (ghost) {
				ghost_ = truth_bool(e.second);
			}
			if (frost) {
				frost_ = truth_bool(e.second);
			}
			return;
		}
	}
	entries_.emplace_back(key, std::move(value));
	if (ghost) {
		ghost_ = truth_bool(entries_.back().second);
	}
	if (frost) {
		frost_ = truth_bool(entries_.back().second);
	}
}

bool Flags::holds(const std::vector<FlagCond> &conds) const {
	for (const FlagCond &c : conds) {
		if (!c.holds(*this)) {
			return false;
		}
	}
	return true;
}

void Flags::erase(const std::string &key) {
	key_ok_ = false;
	entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.first == key; }), entries_.end());
	if (key == GHOST_KEY) {
		ghost_ = false;
	}
	if (key == FROST) {
		frost_ = false;
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
	if (key.size() == 10 && key == FROST) {
		return frost_;
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

// ---------------------------------------------------------------- Stage 171: flag expressions

namespace {
bool operator_char(char c) { return c == '&' || c == '|' || c == '^' || c == '!' || c == '(' || c == ')' || c == ' ' || c == '\t'; }
} // namespace

bool FlagExpr::is_expression(const std::string &key) {
	return std::any_of(key.begin(), key.end(), operator_char);
}

std::shared_ptr<const FlagExpr> FlagExpr::compile(const std::string &key) {
	if (!is_expression(key)) {
		return nullptr;
	}
	auto e = std::make_shared<FlagExpr>();
	e->source_ = key;
	// the tokens: && || ^ ! ( ) and names (runs of other characters)
	std::vector<std::string> toks;
	for (size_t i = 0; i < key.size();) {
		const char c = key[i];
		if (c == ' ' || c == '\t') {
			i++;
		} else if ((c == '&' || c == '|') && i + 1 < key.size() && key[i + 1] == c) {
			toks.push_back(key.substr(i, 2));
			i += 2;
		} else if (c == '&' || c == '|') {
			throw std::invalid_argument("flag expression '" + key + "': '" + std::string(1, c) + "' alone (&& and || are the operators)");
		} else if (operator_char(c)) {
			toks.emplace_back(1, c);
			i++;
		} else {
			size_t j = i;
			while (j < key.size() && !operator_char(key[j])) {
				j++;
			}
			toks.push_back(key.substr(i, j - i));
			i = j;
		}
	}
	// recursive descent: or := and ('||' and)*; and := xor ('&&' xor)*; xor := unary ('^' unary)*;
	// unary := '!' unary | '(' or ')' | name
	size_t at = 0;
	int depth = 0, max_depth = 0;
	auto fail = [&](const std::string &why) -> void { throw std::invalid_argument("flag expression '" + key + "': " + why); };
	auto peek = [&]() -> const std::string * { return at < toks.size() ? &toks[at] : nullptr; };
	auto push = [&](Op op, std::uint32_t arg) {
		e->code_.push_back({op, arg});
		depth += op == Push ? 1 : op == Not ? 0 : -1;
		max_depth = std::max(max_depth, depth);
	};
	std::function<void()> parse_or;
	std::function<void()> parse_unary = [&] {
		const std::string *t = peek();
		if (t == nullptr) {
			fail("ends where a flag is expected");
		} else if (*t == "!") {
			at++;
			parse_unary();
			push(Not, 0);
		} else if (*t == "(") {
			at++;
			parse_or();
			if (peek() == nullptr || *peek() != ")") {
				fail("a '(' is not closed");
			}
			at++;
		} else if (*t == ")" || *t == "&&" || *t == "||" || *t == "^") {
			fail("'" + *t + "' where a flag is expected");
		} else {
			const auto it = std::find(e->names_.begin(), e->names_.end(), *t);
			push(Push, std::uint32_t(it - e->names_.begin()));
			if (it == e->names_.end()) {
				e->names_.push_back(*t);
			}
			at++;
		}
	};
	auto binary = [&](const char *tok, Op op, const std::function<void()> &operand) {
		operand();
		while (peek() != nullptr && *peek() == tok) {
			at++;
			operand();
			push(op, 0);
		}
	};
	const std::function<void()> parse_xor = [&] { binary("^", Xor, parse_unary); };
	const std::function<void()> parse_and = [&] { binary("&&", And, parse_xor); };
	parse_or = [&] { binary("||", Or, parse_and); };
	parse_or();
	if (at != toks.size()) {
		fail("'" + toks[at] + "' after a whole expression (an operator missing?)");
	}
	if (max_depth > 64) {
		fail("nested too deeply");
	}
	return e;
}

std::shared_ptr<const FlagExpr> FlagExpr::cached(const std::string &key) {
	static std::mutex mu;
	static std::unordered_map<std::string, std::shared_ptr<const FlagExpr>> memo;
	const std::lock_guard<std::mutex> lock(mu);
	auto it = memo.find(key);
	if (it == memo.end()) {
		it = memo.emplace(key, compile(key)).first;
	}
	return it->second;
}

bool FlagExpr::eval(const Flags &flags) const {
	std::uint64_t stack = 0; // (bit i: the i-th value from the bottom; at most 64 deep, checked when compiled)
	int n = 0;
	for (const auto &[op, arg] : code_) {
		switch (op) {
			case Push: {
				const FlagValue *v = flags.get(names_[arg]);
				stack = (stack & ~(std::uint64_t(1) << n)) | (std::uint64_t(v != nullptr && flag_truth(*v)) << n);
				n++;
				break;
			}
			case Not: stack ^= std::uint64_t(1) << (n - 1); break;
			default: {
				const bool b = (stack >> (n - 1)) & 1, a = (stack >> (n - 2)) & 1;
				const bool r = op == And ? (a && b) : op == Or ? (a || b) : (a != b);
				n--;
				stack = (stack & ~(std::uint64_t(1) << (n - 1))) | (std::uint64_t(r) << (n - 1));
				break;
			}
		}
	}
	return stack & 1;
}

bool key_true(const std::string &key, const Flags &flags) {
	if (!FlagExpr::is_expression(key)) {
		return flags.is_true(key);
	}
	return FlagExpr::cached(key)->eval(flags);
}

bool key_holds(const std::string &key, const std::vector<std::string> &values, const Flags &flags) {
	if (!FlagExpr::is_expression(key)) {
		const FlagValue *v = flags.get(key);
		for (const std::string &val : values) {
			if (v ? flag_key_is(*v, val) : val == "DEFAULT") {
				return true;
			}
		}
		return false;
	}
	FlagCond c;
	c.values = values;
	c.expr = FlagExpr::cached(key);
	return c.holds(flags);
}

} // namespace rc
