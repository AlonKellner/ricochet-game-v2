// The regions of the user's reports (F12 dumps), replayed exactly.
#include "rcg/level.hpp"
#include "rcg/session.hpp"
#include "rc/sweep.hpp"
#include "rc/visibility.hpp"
#include "rc/visibility_checks.hpp"

#include <doctest/doctest.h>

#include <filesystem>

namespace {

const std::filesystem::path ROOT = RC_REPO_ROOT;

} // namespace

// Stage 128 (the F12 dump of 2026-10-22 11:13): crossings, the mirror circle 17
// planned from outside on its western half; a cell of the reversed half closed
// on 17 by an arc 1.5e-6 px long, drawn the long way round: the whole disc was
// in the region, every point of it divergent (VISIBILITY_EQUALS_NON_DIVERGENCE)
TEST_CASE("region reports: crossings [17 L] -- the mirror circle's disc is not in the region") {
	const rcg::Level lv = rcg::load_level((ROOT / "data" / "levels" / "test_levels" / "crossings.json").string());
	rcg::Session s(lv);
	rc::Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(s.world().defs());
	const rc::V2 player = rc::V2::of(0x1.204d26p+10, 0x1.cdac8cp+7);
	rc::PlanItem it;
	it.surface_id = 17;
	it.left = true;
	it.hint = rc::effects::F32Point{0x1.4516bp+10f, 0x1.3d969p+8f};
	it.span = std::array<double, 6>{1400.0, 390.0, 1400.0, 210.0, 1310.0, 300.0};
	const std::vector<rc::PlanItem> plan{it};
	const rc::vis::Result r = rc::vis::compute(t, rc::vis::Pt{double(player.x), double(player.y)}, plan, rc::Flags(), rc::vis::Rect{0, 0, 1920, 1080});
	REQUIRE(r.supported);
	int inside = 0;
	for (double y = 220; y <= 380; y += 8) {
		for (double x = 1320; x <= 1480; x += 8) {
			if ((x - 1400) * (x - 1400) + (y - 300) * (y - 300) < 85.0 * 85.0) {
				inside += rc::vis::contains(r, rc::vis::Pt{x, y}) == rc::vis::Membership::Inside ? 1 : 0;
			}
		}
	}
	CHECK(inside == 0);
	// the dump's own cursor
	const rc::V2 cursor = rc::V2::of(0x1.541ab2p+10, 0x1.298d1p+8);
	CHECK(rc::vis::contains(r, rc::vis::Pt{double(cursor.x), double(cursor.y)}) == rc::vis::Membership::Outside);
}

// Stage 129 (the F12 dump of 2026-10-22 11:40): crossings, the mirror circle 17
// planned on its western piece from below; the aim line meets only the eastern
// piece, where the physical walk reflects too -- out of plan (GAME_SPEC 9.7,
// 14.2): the paths diverge there, and the cursor beyond is not visible
TEST_CASE("region reports: crossings [17 L, western piece] -- a hit on the eastern piece diverges") {
	rcg::Session s(rcg::load_level((ROOT / "data" / "levels" / "test_levels" / "crossings.json").string()));
	s.teleport(rc::V2::of(0x1.593f58p+10, 0x1.f0316ap+8));
	rc::PlanItem it;
	it.surface_id = 17;
	it.left = true;
	it.hint = rc::effects::F32Point{1300.3544921875f, 345.6401062011719f};
	it.span = std::array<double, 6>{1400.0, 390.0, 1400.0, 210.0, 1310.0, 300.0};
	s.set_plan({it});
	REQUIRE(s.plan().size() == 1);
	CHECK(s.plan()[0].piece >= 0); // (the span is one of the world's pieces)
	s.set_cursor(rc::V2::of(0x1.932b82p+10, 0x1.62a824p+8));
	s.settle();
	const std::string t = s.debug_summary();
	CHECK(t.find("cursor: preview not visible, region outside") != std::string::npos);
	// the merged steps: aligned to the hit on 17, diverged from there on
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	REQUIRE(m.size() >= 3);
	CHECK(m[0].type == rc::StepType::Aligned);
	CHECK(m[1].type == rc::StepType::Aligned);
	CHECK(m[2].type != rc::StepType::Aligned);
	// control: the whole surface planned, the same hit is the planned one
	it.span.reset();
	s.set_plan({it});
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview visible, region inside") != std::string::npos);
}

// The user's report (F12, 2026-10-22, Stage 131): on open_room_playable, the
// mirror 5 planned; the path runs off the level through infinity and returns
// to the cursor -- an unplanned infinity, so the paths diverge there (GAME_SPEC
// 12.5) and the cursor beyond is not visible. Planned, the wrap is aligned.
TEST_CASE("region reports: open_room_playable [5 L] -- an unplanned infinity before the cursor diverges") {
	rcg::Session s(rcg::load_level((ROOT / "data" / "levels" / "open_room_playable.json").string()));
	s.teleport(rc::V2::of(0x1.41aad8p+10, 0x1.9df5c2p+9));
	rc::PlanItem it;
	it.surface_id = 5;
	it.left = true;
	it.hint = rc::effects::F32Point{0x1.2ed0cp+10f, 0x1.3b1562p+9f};
	s.set_plan({it});
	REQUIRE(s.plan().size() == 1);
	s.set_cursor(rc::V2::of(0x1.d97e42p+9, 0x1.f66e34p+9));
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview not visible, region outside") != std::string::npos);
	// the merged steps: aligned to the level's edge, diverged past the wrap
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	REQUIRE(m.size() >= 6);
	for (size_t i = 0; i < 5; i++) {
		CHECK(m[i].type == rc::StepType::Aligned);
	}
	CHECK(m[5].type != rc::StepType::Aligned);
	// control: infinity planned after the mirror, the wrap is the plan's
	rc::PlanItem inf;
	inf.surface_id = rc::INFINITY_ID;
	inf.left = true;
	s.set_plan({it, inf});
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview visible, region inside") != std::string::npos);
}

// The user's report (F12, 2026-10-22, Stage 131): [5 L, inf] -- the wrap planned,
// the beam comes back through the ray's origin, the player's image in the mirror
// x = 1200, before the cursor: unplanned, a divergence there (GAME_SPEC 12.6)
TEST_CASE("region reports: open_room_playable [5 L, inf] -- the player's image passed before the cursor diverges") {
	rcg::Session s(rcg::load_level((ROOT / "data" / "levels" / "open_room_playable.json").string()));
	s.teleport(rc::V2::of(0x1.7df8aep+10, 0x1.cef2e4p+9));
	rc::PlanItem it;
	it.surface_id = 5;
	it.left = true;
	it.hint = rc::effects::F32Point{0x1.2d906ap+10f, 0x1.36933ep+9f};
	rc::PlanItem inf;
	inf.surface_id = rc::INFINITY_ID;
	inf.left = true;
	inf.hint = rc::effects::F32Point{0x1.dc7f1p+10f, 0x1.90bdfep+4f};
	s.set_plan({it, inf});
	REQUIRE(s.plan().size() == 2);
	s.set_cursor(rc::V2::of(0x1.d7fddcp+9, 0x1.b24de8p+9));
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview not visible, region outside") != std::string::npos);
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	REQUIRE(m.size() >= 12);
	for (size_t i = 0; i < 11; i++) {
		CHECK(m[i].type == rc::StepType::Aligned);
	}
	CHECK(m[11].type != rc::StepType::Aligned);
}
