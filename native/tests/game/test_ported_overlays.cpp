// Stage 119: GUT tests ported (docs/research/gut_ledger.tsv) -- the plan
// overlays' colours (rcg::plan_overlays: ChevronOverlayBuilder.build_plan_overlays)
// and the visibility region recomputed only when its inputs change.
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>

#include <filesystem>

namespace {

using Color = std::array<float, 4>;
const Color VALID{1.0f, 1.0f, 1.0f, 0.15f};
const Color INVALID{1.0f, 0.0f, 0.0f, 0.15f};

// test_helpers.gd's mirror(400): a two-sided plannable mirror x = 400, y 0..600
rcg::World mirror_world() {
	const rcg::Level level = rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [
		{"ids": [1], "start": [400, 0], "end": [400, 600], "via": [400, 300],
		 "left": {"effect": {"type": "reflection"}, "interactive": true},
		 "right": {"effect": {"type": "reflection"}, "interactive": true}}]})");
	return rcg::World(rcg::build_level(level));
}

rc::PhysicalHit hit(rcg::Side side, bool on_segment, bool continues) {
	rc::PhysicalHit h;
	h.side = int(side);
	h.on_segment = on_segment;
	h.has_continuation = continues;
	h.point = rc::V2{400.0f, 300.0f};
	return h;
}

std::vector<std::pair<int, rcg::Overlay>> overlays(const std::vector<rcg::PlanEntry> &plan, const std::vector<rc::PhysicalHit> &hits) {
	const rcg::World w = mirror_world();
	std::vector<std::pair<int, std::vector<rc::PhysicalHit>>> by_id;
	if (!hits.empty()) {
		by_id.push_back({1, hits});
	}
	return rcg::plan_overlays(plan, by_id, w, rc::Flags{});
}

const rcg::PlanEntry LEFT{1, rcg::Side::Left, rc::V2::inf(), -1, std::nullopt};
const rcg::PlanEntry RIGHT{1, rcg::Side::Right, rc::V2::inf(), -1, std::nullopt};

} // namespace

TEST_CASE("ported: test_stage53_plan_highlight: test_plan_valid_when_hit_correct_side_on_segment") {
	CHECK(overlays({LEFT}, {hit(rcg::Side::Left, true, true)}).at(0).second.incoming_color == VALID);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_plan_invalid_when_surface_not_hit") {
	CHECK(overlays({LEFT}, {}).at(0).second.incoming_color == INVALID);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_plan_invalid_when_wrong_side") {
	CHECK(overlays({LEFT}, {hit(rcg::Side::Right, true, true)}).at(0).second.incoming_color == INVALID);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_plan_invalid_when_off_segment") {
	CHECK(overlays({LEFT}, {hit(rcg::Side::Left, false, true)}).at(0).second.incoming_color == INVALID);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_plan_valid_if_any_hit_matches") {
	CHECK(overlays({LEFT}, {hit(rcg::Side::Right, true, true), hit(rcg::Side::Left, true, true)}).at(0).second.incoming_color == VALID);
}

TEST_CASE("ported: test_stage53_plan_highlight: test_plan_highlight_color_selection") {
	const auto ovs = overlays({LEFT, RIGHT}, {hit(rcg::Side::Left, true, true)});
	REQUIRE(ovs.size() == 2);
	CHECK(ovs[0].second.incoming_color == VALID);   // the valid entry
	CHECK(ovs[1].second.incoming_color == INVALID); // the invalid one
}

TEST_CASE("ported: test_stage55_chevron_overlays: test_plan_reflection_valid_incoming_invalid_outgoing") {
	const auto ovs = overlays({LEFT}, {hit(rcg::Side::Left, true, false)});
	REQUIRE(ovs.size() == 1);
	CHECK(ovs[0].second.incoming_color == VALID);  // hit on the segment
	CHECK(ovs[0].second.outgoing_color == INVALID); // no continuation
}

TEST_CASE("ported: test_stage110_visibility: test_the_renderer_recomputes_only_on_change") {
	const std::filesystem::path root = RC_REPO_ROOT;
	rcg::Session s(rcg::load_level((root / "data" / "levels" / "test_levels" / "room_with_mirror.json").string()));
	s.set_cursor(rc::V2{1200.0f, 400.0f});
	s.settle();
	const int v0 = s.visibility_version();
	CHECK(s.has_region());
	s.settle();
	CHECK(s.visibility_version() == v0); // a still frame recomputes nothing
	s.set_cursor(rc::V2{1237.0f, 411.0f});
	s.settle();
	CHECK(s.visibility_version() == v0); // the cursor does not move the region
	s.teleport(s.player() + rc::V2{20.0f, 0.0f});
	s.settle();
	CHECK(s.visibility_version() == v0 + 1); // the player does
	s.key(rcg::Action::ToggleVisibility);
	s.settle();
	CHECK(s.visibility_version() == v0 + 2); // the switch does
	CHECK_FALSE(s.has_region());             // switched off: nothing drawn
}
