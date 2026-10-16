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
				CHECK(e.surface_id != INFINITY_ID);
			}
		}
	}
}

TEST_CASE("infinity: with the switch on every older fixture differs from its golden only by insertions and an earlier MAX_HITS cut") {
	const ojson gold = load_json("bench/goldens/fixtures.json");
	const char *files[] = {"stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags", "stage86_ghost", "stage87_projective_plans", "stage87_click_forks"};
	int fixtures = 0;
	int changed = 0;
	int inserted = 0;
	int cut = 0;
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
				} else {
					stripped.push_back(e);
				}
			}
			const ojson mine = ojson::parse(encode_events(stripped, nullptr));
			const ojson &golden = gold[key]["f64"];
			const auto &ge = golden["events"];
			const auto &me = mine["events"];
			// the beams in order of first use: a trace cut earlier uses a prefix of them
			REQUIRE_MESSAGE(mine["beams"].size() <= golden["beams"].size(), (key + ": more beams than the golden"));
			for (size_t bi = 0; bi < mine["beams"].size(); bi++) {
				CHECK_MESSAGE(mine["beams"][bi] == golden["beams"][bi], (key + ": beam " + std::to_string(bi)));
			}
			REQUIRE_MESSAGE(me.size() <= ge.size(), (key + ": more non-infinity events than the golden"));
			bool same = me.size() == ge.size();
			for (size_t i = 0; i + 1 < me.size(); i++) {
				CHECK_MESSAGE(blank_t_from(me[i].get<std::string>()) == blank_t_from(ge[i].get<std::string>()), (key + ": event " + std::to_string(i)));
			}
			if (!me.empty()) {
				const std::string last = me.back().get<std::string>();
				if (me.size() == ge.size() && blank_t_from(last) == blank_t_from(ge[me.size() - 1].get<std::string>())) {
					// identical (apart from inserted infinity events)
				} else {
					// the trace was cut earlier by MAX_HITS: the last event is the max
					CHECK_MESSAGE(kind_of(last) == "max", (key + ": the last non-infinity event of a shortened trace is the max cut"));
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
	// of the 115 fixtures change, 83 infinity events, 8 traces cut earlier
	CHECK(changed == 32);
	CHECK(inserted == 83);
	CHECK(cut == 8);
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
