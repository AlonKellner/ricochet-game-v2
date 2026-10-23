// Stage 88c: the `infinity` event and the infinity plan entry. With
// Scene::emit_infinity on, the native solver must reproduce the oracle's Stage
// 88 fixtures (trace2.py with EMIT_INFINITY, bench/fixtures/stage88_infinity.json)
// at every tier; with it on, every older fixture may differ from its golden only
// by inserted infinity events and the earlier MAX_HITS cut they cause; with it
// off (the default) the goldens test replays the older fixtures unchanged.
#include "goldens.hpp"
#include "oracle_fixtures.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace rc;
using namespace rc::test;

namespace {

// An encoded event with its t_from blanked (the arc after an infinity event
// starts at infinity instead of at the previous point) and its kind kept.
std::string blank_t_from(const std::string &enc) {
	std::string out;
	int field = 0;
	for (char c : enc) {
		if (c == ';') {
			field++;
			out += c;
		} else if (field != 17) {
			out += c;
		}
	}
	return out;
}

std::string kind_of(const std::string &enc) { return enc.substr(0, enc.find(';')); }

} // namespace

TEST_CASE("infinity: the Stage 88 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage88_infinity.json");
	int fixtures = 0;
	int infinity_events = 0;
	int consumed = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if (e.kind == EventKind::Infinity) {
				infinity_events++;
				CHECK(e.surface_id == INFINITY_ID);
				// the real infinity has no point and no t_to; a finite image of it has both
				CHECK(e.point.has_value() == e.t_to.has_value());
				if (e.applied) {
					consumed++;
				}
			}
			if (e.kind == EventKind::Max && e.surface_id == INFINITY_ID) {
				CHECK(!e.point);
			}
		}
		fixtures++;
	}
	CHECK(fixtures == 26);
	CHECK(infinity_events > 0);
	CHECK(consumed > 0);
}

TEST_CASE("infinity: the Stage 101 fixtures (infinity as a point, the pre-transform) match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage101_infinity_point.json");
	int fixtures = 0;
	int with_point = 0;
	int pre_scenes = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		for (const SurfaceDef &d : defs) {
			pre_scenes += d.pre_transform ? 1 : 0;
		}
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if ((e.kind == EventKind::Infinity || e.kind == EventKind::Max) && e.surface_id == INFINITY_ID && e.point) {
				with_point++;
				CHECK(e.t_to.has_value());
			}
		}
		fixtures++;
	}
	CHECK(fixtures == int(fxs["fixtures"].size()));
	CHECK(with_point > 0);
	CHECK(pre_scenes > 0);
}

TEST_CASE("infinity: with the switch off the Stage 88 and 101 scenes never emit the event") {
	for (const char *f : {"bench/fixtures/stage88_infinity.json", "bench/fixtures/stage101_infinity_point.json"}) {
		const ojson fxs = load_json(f);
		for (const ojson &fx : fxs["fixtures"]) {
			const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
			for (const Event<F64> &e : walk_oracle_fixture<F64>(fx, defs, F64{1}, false)) {
				CHECK(e.kind != EventKind::Infinity);
				// (Stage 131: a circle beam's passages of Q, unseen, still count: a max there names it)
				CHECK((e.surface_id != INFINITY_ID || e.kind == EventKind::Max));
			}
		}
	}
}

// Stage 131 (no loop detection): with the switch on, a fixture's walk is its
// golden (the switch off) with infinity events inserted, up to the golden's end:
// where the golden escapes (a line leaves through infinity) the walk wraps and
// goes on; where they part sooner it is cut by a limit (the passages count)
TEST_CASE("infinity: with the switch on every older fixture differs from its golden only by insertions, a wrap past its escape, or an earlier cut") {
	const ojson gold = load_json("bench/goldens/fixtures.json");
	const char *files[] = {"stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags", "stage86_ghost", "stage87_projective_plans", "stage87_click_forks"};
	int fixtures = 0;
	int changed = 0;
	int inserted = 0;
	int cut = 0;
	int wrapped = 0;
	for (const char *f : files) {
		const ojson fxs = load_json(std::string("bench/fixtures/") + f + ".json");
		for (const ojson &fx : fxs["fixtures"]) {
			const std::string key = std::string(f) + "/" + fx["name"].get<std::string>();
			const ojson &inp = gold[key]["inputs"];
			const std::vector<SurfaceDef> defs = defs_from_json_hex(fx["surfaces"], inp);
			// the same inputs as the goldens test (hex bit patterns), the switch on
			Scene<F64> scene = Scene<F64>::from_defs(defs, F64{1});
			scene.emit_infinity = true;
			SolveCache<F64> cache;
			Solver<F64> solver(scene, &cache);
			const F64 like{1};
			const CN<F64> player{like.like(from_hex_str(inp["player"][0])), like.like(from_hex_str(inp["player"][1]))};
			std::optional<CN<F64>> cursor;
			if (!fx["cursor"].is_null()) {
				cursor = CN<F64>{like.like(from_hex_str(inp["cursor"][0])), like.like(from_hex_str(inp["cursor"][1]))};
			}
			const Mode mode = fx["mode"].get<std::string>() == "physical" ? Mode::Physical : Mode::Planned;
			std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
			for (size_t k = 0; k < plan.size() && k < inp["clicks"].size(); k++) {
				if (!inp["clicks"][k].is_null()) {
					plan[k].hint = effects::F32Point{float(from_hex_str(inp["clicks"][k][0])), float(from_hex_str(inp["clicks"][k][1]))};
				}
			}
			const Flags flags = fx.contains("flags") ? flags_from_json(fx["flags"]) : Flags();
			std::vector<Event<F64>> events;
			if (!fx["direction"].is_null()) {
				events = solver.run_direction(player, CN<F64>{like.like(from_hex_str(inp["direction"][0])), like.like(from_hex_str(inp["direction"][1]))},
					plan, cursor, mode, MAX_HITS, flags).events;
			} else {
				std::optional<Walk<F64>> w = solver.run(player, *cursor, plan, mode, MAX_HITS, flags);
				if (w) {
					events = w->events;
				}
			}
			std::vector<Event<F64>> stripped;
			for (const Event<F64> &e : events) {
				if (e.kind == EventKind::Infinity) {
					inserted++;
				} else if (e.kind != EventKind::Origin) { // (Stage 131: the passages of the ray's origin come with the wraps)
					stripped.push_back(e);
				}
			}
			const ojson mine = ojson::parse(encode_events(stripped, nullptr));
			const ojson &golden = gold[key]["f64"];
			ojson ge = ojson::array();
			for (const auto &g : golden["events"]) {
				if (kind_of(g.get<std::string>()) != "origin") {
					ge.push_back(g);
				}
			}
			const auto &me = mine["events"];
			// the beams in order of first use: a trace cut earlier uses a prefix of them
			for (size_t bi = 0; bi < std::min(mine["beams"].size(), golden["beams"].size()); bi++) {
				CHECK_MESSAGE(mine["beams"][bi] == golden["beams"][bi], (key + ": beam " + std::to_string(bi)));
			}
			const bool escapes = !ge.empty() && kind_of(ge.back().get<std::string>()) == "escape";
			bool same = me.size() == ge.size();
			const size_t n = std::min(me.size(), ge.size());
			for (size_t i = 0; i + 1 < n; i++) {
				CHECK_MESSAGE(blank_t_from(me[i].get<std::string>()) == blank_t_from(ge[i].get<std::string>()), (key + ": event " + std::to_string(i)));
			}
			if (!me.empty()) {
				const std::string last = me.back().get<std::string>();
				if (me.size() == ge.size() && blank_t_from(last) == blank_t_from(ge[me.size() - 1].get<std::string>())) {
					// identical (apart from inserted infinity events)
				} else if (escapes && me.size() >= ge.size()) {
					// the golden escapes; the walk wraps through infinity and goes on
					CHECK_MESSAGE(kind_of(last) == "max", (key + ": a walk that wraps on is cut by a limit"));
					wrapped++;
					same = false;
				} else {
					// cut earlier by a limit (the passages of infinity are significant): the last event is the max
					CHECK_MESSAGE(kind_of(last) == "max", (key + ": the last non-infinity event of a shortened trace is the max cut"));
					CHECK_MESSAGE(me.size() <= ge.size(), (key + ": cut earlier"));
					cut++;
					same = false;
				}
			}
			if (!same || stripped.size() != events.size()) {
				changed++;
			}
			fixtures++;
		}
	}
	CHECK(fixtures == 115);
	// the oracle's own diff of the regenerated fixtures against the pre-Stage-88
	// ones (docs/research/stage88b_fixture_diff.md, updated for Stage 101): 32
	// of the 115 fixtures change, 83 infinity events, 8 traces cut earlier;
	// since Stage 111's re-bless (11 fixtures realized by the plan's first
	// entry, GAME_SPEC 13.1) 87 infinity events and 9 cuts; since Stage 114's
	// (the fork rule: fork_other_click_planned and the two cn_twice cases take
	// the on-segment or the clicked candidate) 31 fixtures, 84 events
	MESSAGE("changed " << changed << ", infinity events " << inserted << ", cut earlier " << cut << ", wrapped past an escape " << wrapped);
	// Stage 131 (no loop detection, the event limits, the passages of the ray's
	// origin): 47 fixtures, 86 events, 12 cut earlier (9 wrap past their escape)
	CHECK(changed == 47);
	CHECK(inserted == 86);
	CHECK(cut == 12);
	CHECK(wrapped == 9);
}

TEST_CASE("Stage 105: the tangency-at-infinity fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage105_tangent_at_infinity.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto r4 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(r4, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(r4, fx["expected"])));
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64), true);
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == int(fxs["fixtures"].size()));
	CHECK(fixtures >= 14);
}

TEST_CASE("Stage 106: the pop-departure fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage106_pop_departure.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto r4 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(r4, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(r4, fx["expected"])));
		const auto r64l = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64), true);
		CHECK_MESSAGE(diff_against_oracle(r64l, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(r64l, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == int(fxs["fixtures"].size()));
	CHECK(fixtures >= 11);
}

TEST_CASE("Stage 111: the realization-by-entry fixtures match the oracle at f64, Dual, 4 and 64 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage111_orientation.json");
	int fixtures = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		const auto rb64 = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 64), true);
		CHECK_MESSAGE(diff_against_oracle(rb64, fx["expected"]) == "", (name + " big64: " + diff_against_oracle(rb64, fx["expected"])));
		fixtures++;
	}
	CHECK(fixtures == 32);
}

// Stage 132 (GAME_SPEC 4.2, 12.6): the ray's origin as a plan entry -- consumed by
// its passage in both walks; the realization by the parity of the infinity and
// origin entries (bench/fixtures/stage132_origin_entry.json, the oracle's)
TEST_CASE("origin entry: the Stage 132 fixtures match the oracle at f64, Dual and 4 limbs") {
	const ojson fxs = load_json("bench/fixtures/stage132_origin_entry.json");
	int fixtures = 0;
	int consumed = 0;
	for (const ojson &fx : fxs["fixtures"]) {
		const std::string name = fx["name"].get<std::string>();
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		const auto r64 = walk_oracle_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_oracle_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_oracle_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if (e.kind == EventKind::Origin && e.applied) {
				consumed++;
			}
		}
		fixtures++;
	}
	CHECK(fixtures == 14);
	CHECK(consumed == 8);
}
