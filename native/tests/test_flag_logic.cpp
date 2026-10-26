// Stage 171 (GAME_SPEC 9.11, the user): flag expressions. Wherever a flag is
// read -- presence, a resolver's key, a state change's conditions -- its key may
// combine flags with ! ^ && || and parentheses (C's precedence), each flag read
// as its truth; a false expression reads as a missing flag
// (bench/fixtures/stage171_flag_logic.json, the oracle's).
#include "goldens.hpp"
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace rc;
using namespace rc::test;

namespace {

bool eval(const std::string &e, const Flags &f) { return FlagExpr::compile(e)->eval(f); }

} // namespace

TEST_CASE("flag logic: the Stage 171 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage171_flag_logic.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1});
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0});
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4));
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 10);
}

TEST_CASE("flag logic: expressions -- the operators, C's precedence, truths, a missing flag false") {
	Flags f;
	f.set("x", true);
	f.set("y", false);
	f.set("z", true);
	f.set("w", 1.0);
	f.set("s", std::string("lit"));
	f.set("door:gate", true);
	CHECK_FALSE(eval("x && y", f));
	CHECK(eval("(!x) || (!y) || (!z)", f));
	CHECK_FALSE(eval("x ^ y ^ z", f));
	CHECK(eval("(x && y) || (w && z)", f));
	CHECK(eval("x && y ^ z", f)); // x && (y ^ z)
	CHECK(eval("y ^ z && x", f)); // (y ^ z) && x
	CHECK(eval("y || z && x", f)); // y || (z && x)
	CHECK_FALSE(eval("!(x || y)", f));
	CHECK(eval("!!x", f));
	CHECK(eval("s && w", f)); // (a non-empty string, a non-zero number: true)
	CHECK(eval("missing || !missing", f));
	CHECK_FALSE(eval("(missing)", f));
	CHECK(eval("!door:gate || det:plate:x", Flags())); // (names with colons)
	CHECK_FALSE(FlagExpr::is_expression("door:gate"));
	CHECK(FlagExpr::compile("door:gate") == nullptr);
	CHECK(FlagExpr::cached("x && z")->eval(f));
	for (const char *bad : {"x &", "x &&", "(x", "x y", "x)", "&& x", "x | y", "()", "!"}) {
		CHECK_THROWS_AS(FlagExpr::compile(bad), std::invalid_argument);
	}
}

TEST_CASE("flag logic: a condition on an expression -- true matches \"true\", false \"false\" and \"DEFAULT\"") {
	Flags f;
	f.set("a", true);
	const FlagCond door{"a ^ b", {"DEFAULT", "false"}}, lit{"a ^ b", {"true"}};
	CHECK_FALSE(door.holds(f));
	CHECK(lit.holds(f));
	f.set("b", true);
	CHECK(door.holds(f));
	CHECK_FALSE(lit.holds(f));
	CHECK(f.holds({FlagCond{"a && b", {"true"}}, FlagCond{"a", {"true"}}}));
	CHECK(key_holds("!a", {"false"}, f));
	CHECK(key_holds("a", {"true"}, f));
	CHECK(key_true("a && b", f));
	CHECK_FALSE(key_true("a && !b", f));
	// a state change's conditions read an expression
	const StateChangeDef ch{"k", false, 1.0, {FlagCond{"!a || b", {"true"}}}};
	CHECK(f.applied(std::vector<StateChangeDef>{ch}).has("k"));
	Flags g;
	g.set("a", true);
	CHECK_FALSE(g.applied(std::vector<StateChangeDef>{ch}).has("k"));
}
