// Stage 88c: the `infinity` event and the infinity plan entry. With
// Scene::emit_infinity on, the native solver must reproduce the oracle's Stage
// 88 fixtures (trace2.py with EMIT_INFINITY, bench/fixtures/stage88_infinity.json)
// at every tier; with it on, every older fixture may differ from its golden only
// by inserted infinity events and the earlier MAX_HITS cut they cause; with it
// off (the default) the goldens test replays the older fixtures unchanged.
#include "goldens.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace rc;
using namespace rc::test;

namespace {

template <Num N>
std::vector<Event<N>> walk_infinity_fixture(const ojson &fx, const std::vector<SurfaceDef> &defs, const N &like, bool emit) {
	Scene<N> scene = Scene<N>::from_defs(defs, like);
	scene.emit_infinity = emit;
	SolveCache<N> cache;
	Solver<N> solver(scene, &cache);
	const CN<N> player{like.like(fx["player"][0].get<double>()), like.like(fx["player"][1].get<double>())};
	std::optional<CN<N>> cursor;
	if (!fx["cursor"].is_null()) {
		cursor = CN<N>{like.like(fx["cursor"][0].get<double>()), like.like(fx["cursor"][1].get<double>())};
	}
	const Mode mode = fx["mode"].get<std::string>() == "physical" ? Mode::Physical : Mode::Planned;
	const std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
	const Flags flags = fx.contains("flags") && !fx["flags"].is_null() ? flags_from_json(fx["flags"]) : Flags();
	if (!fx["direction"].is_null()) {
		Walk<N> w = solver.run_direction(player, CN<N>{like.like(fx["direction"][0].get<double>()), like.like(fx["direction"][1].get<double>())},
			plan, cursor, mode, MAX_HITS, flags);
		return w.events;
	}
	std::optional<Walk<N>> w = solver.run(player, *cursor, plan, mode, MAX_HITS, flags);
	return w ? w->events : std::vector<Event<N>>{};
}

// The oracle's event record compared field by field (points within 1e-9 px,
// as the GUT fixture tests compare).
template <Num N>
std::string diff_against_oracle(const std::vector<Event<N>> &events, const ojson &expected) {
	if (events.size() != expected.size()) {
		std::string got;
		for (const Event<N> &e : events) {
			got += std::string(event_kind_name(e.kind)) + " ";
		}
		return "count " + std::to_string(events.size()) + " vs oracle " + std::to_string(expected.size()) + " (" + got + ")";
	}
	for (size_t i = 0; i < events.size(); i++) {
		const Event<N> &e = events[i];
		const ojson &x = expected[i];
		const std::string side = e.kind == EventKind::Hit ? (e.left ? "LEFT" : "RIGHT") : "";
		if (std::string(event_kind_name(e.kind)) != x["kind"].get<std::string>() || e.surface_id != x["surface_id"].get<int>()
				|| side != x["side"].get<std::string>() || e.applied != x["applied"].get<bool>() || e.terminal != x["terminal"].get<bool>()
				|| e.on_segment != x["on_segment"].get<bool>() || e.rehit != x["rehit"].get<bool>()) {
			return "event " + std::to_string(i) + ": " + event_kind_name(e.kind) + " s=" + std::to_string(e.surface_id) + " " + side
				+ (e.applied ? " applied" : "") + " vs oracle " + x["kind"].get<std::string>() + " s=" + std::to_string(x["surface_id"].get<int>())
				+ " " + x["side"].get<std::string>() + (x["applied"].get<bool>() ? " applied" : "");
		}
		if (x.contains("state") && e.state != x["state"].get<bool>()) {
			return "event " + std::to_string(i) + ": state";
		}
		if (x.contains("flags") && !x["flags"].is_null() && e.flags.canonical_key() != flags_from_json(x["flags"]).canonical_key()) {
			return "event " + std::to_string(i) + ": flags " + e.flags.canonical_key();
		}
		if (x.contains("x")) {
			if (!e.point) {
				return "event " + std::to_string(i) + ": no point, oracle has one";
			}
			const double dx = e.point->fx() - std::stod(x["x"].get<std::string>());
			const double dy = e.point->fy() - std::stod(x["y"].get<std::string>());
			if (std::sqrt(dx * dx + dy * dy) > 1e-9) {
				return "event " + std::to_string(i) + ": point off by " + std::to_string(std::sqrt(dx * dx + dy * dy));
			}
		} else if (e.point && e.kind != EventKind::Hit) {
			return "event " + std::to_string(i) + ": a point where the oracle has none";
		}
	}
	return "";
}

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
		const auto r64 = walk_infinity_fixture<F64>(fx, defs, F64{1}, true);
		CHECK_MESSAGE(diff_against_oracle(r64, fx["expected"]) == "", (name + " f64: " + diff_against_oracle(r64, fx["expected"])));
		const auto rd = walk_infinity_fixture<Dual>(fx, defs, Dual{1, 0}, true);
		CHECK_MESSAGE(diff_against_oracle(rd, fx["expected"]) == "", (name + " dual: " + diff_against_oracle(rd, fx["expected"])));
		const auto rb = walk_infinity_fixture<BigFloat>(fx, defs, BigFloat::from_int(1, 4), true);
		CHECK_MESSAGE(diff_against_oracle(rb, fx["expected"]) == "", (name + " big4: " + diff_against_oracle(rb, fx["expected"])));
		for (const Event<F64> &e : r64) {
			if (e.kind == EventKind::Infinity) {
				infinity_events++;
				CHECK(e.surface_id == INFINITY_ID);
				CHECK(!e.point);
				CHECK(!e.t_to);
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

TEST_CASE("infinity: with the switch off the Stage 88 scenes never emit the event") {
	const ojson fxs = load_json("bench/fixtures/stage88_infinity.json");
	for (const ojson &fx : fxs["fixtures"]) {
		const std::vector<SurfaceDef> defs = defs_from_json(fx["surfaces"]);
		for (const Event<F64> &e : walk_infinity_fixture<F64>(fx, defs, F64{1}, false)) {
			CHECK(e.kind != EventKind::Infinity);
			CHECK(e.surface_id != INFINITY_ID);
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
	// the Stage 88b evidence (docs/research/stage88b_fixture_diff.md): 26 of the
	// 115 fixtures change, 57 infinity events, 7 traces cut earlier
	CHECK(changed == 26);
	CHECK(inserted == 57);
	CHECK(cut == 7);
}
