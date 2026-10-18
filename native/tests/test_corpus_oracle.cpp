// The scene corpus against the oracle, natively (Workflow W2): every
// bench/scenes_corpus scenario solved by the ladder (as PrecisionLadder.solve
// does in tests/test_stage81_trace_rules.gd) and compared with the oracle's
// expectation event by event, points within 0.01 px, the infinity event on.
// The GUT copy stays as the binding's differential; this one needs no Godot.
#include "json_defs.hpp"
#include "oracle_fixtures.hpp"
#include "rc/ladder.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rc;

namespace {

constexpr double CORPUS_TOL = 0.01; // px (PHYSICAL-PREVIEW-MATCH)

std::string climb_diff(const Climb &c, const ojson &expected) {
	return c.limbs == 0 ? test::diff_against_oracle(c.f64_events, expected, CORPUS_TOL) : test::diff_against_oracle(c.big_events, expected, CORPUS_TOL);
}

} // namespace

TEST_CASE("corpus: every scene corpus scenario matches the oracle through the ladder") {
	const std::filesystem::path root = std::filesystem::path(RC_REPO_ROOT) / "bench" / "scenes_corpus";
	std::vector<std::string> names;
	for (const auto &e : std::filesystem::directory_iterator(root / "scenarios")) {
		if (e.path().extension() == ".json") {
			names.push_back(e.path().stem().string());
		}
	}
	std::sort(names.begin(), names.end());
	int bad = 0;
	for (const std::string &name : names) {
		ojson scn, ex;
		{
			std::ifstream in(root / "scenarios" / (name + ".json"));
			scn = ojson::parse(in);
			std::ifstream inx(root / "expected" / (name + ".json"));
			REQUIRE_MESSAGE(inx.good(), name << ": no expectation");
			ex = ojson::parse(inx);
		}
		const std::vector<SurfaceDef> defs = defs_from_json(scn["surfaces"]);
		const std::vector<PlanItem> plan = plan_from_json(scn["plan"]);
		LadderContext lctx(defs);
		lctx.set_emit_infinity(true);
		Ladder lad(lctx, []() { return 0LL; });
		const LadderResult lr = lad.solve(scn["player"][0].get<double>(), scn["player"][1].get<double>(), scn["cursor"][0].get<double>(),
			scn["cursor"][1].get<double>(), plan, MAX_HITS, MAX_HITS, CORPUS_TOL, false);
		const std::string dp = climb_diff(lr.planned, ex["planned"]);
		const std::string dh = climb_diff(lr.physical, ex["physical"]);
		CHECK_MESSAGE(dp.empty(), name << " planned: " << dp);
		CHECK_MESSAGE(dh.empty(), name << " physical: " << dh);
		if (!dp.empty() || !dh.empty()) {
			bad++;
		}
	}
	MESSAGE(names.size() << " scenarios, " << bad << " differing");
	CHECK(names.size() >= 420);
	CHECK(bad == 0);
}
