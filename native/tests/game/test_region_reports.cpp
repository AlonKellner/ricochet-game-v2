// The regions of the user's reports (F12 dumps), replayed exactly.
#include "rcg/level.hpp"
#include "rcg/session.hpp"
#include "rc/sweep.hpp"
#include "rc/visibility.hpp"
#include "rc/visibility_checks.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {

const std::filesystem::path ROOT = RC_REPO_ROOT;

// the merged steps as "type: start -> end" lines (a failing check's message)
std::string steps_text(const std::vector<rc::TraceStep> &m) {
	std::string out;
	for (size_t i = 0; i < m.size(); i++) {
		char b[160];
		std::snprintf(b, sizeof b, "\n  %zu type %d: (%.2f, %.2f) -> (%.2f, %.2f)%s", i, int(m[i].type), m[i].start.x, m[i].start.y, m[i].end.x, m[i].end.y,
			m[i].significant ? " sig" : "");
		out += b;
	}
	return out;
}

} // namespace

// Stage 128 (the F12 dump of 2026-10-22 11:13): crossings, the mirror circle 17
// planned from outside on its western half; a cell of the reversed half closed
// on 17 by an arc 1.5e-6 px long, drawn the long way round: the whole disc was
// in the region, every point of it divergent (VISIBILITY_EQUALS_NON_DIVERGENCE)
TEST_CASE("region reports: crossings [17 L] -- the mirror circle's disc is not in the region") {
	const rcg::Level lv = rcg::load_level((ROOT / "data" / "levels" / "test_levels" / "crossings.json").string());
	rcg::Session s(lv);
	rc::Tracer t;
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
	// the merged steps: aligned to the hit on 17, diverged from there on (Stage
	// 134: by segments -- the first is the run to that hit)
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	INFO(steps_text(m));
	REQUIRE(m.size() >= 2);
	CHECK(m[0].type == rc::StepType::Aligned);
	CHECK(m[0].significant);
	CHECK(m[1].type != rc::StepType::Aligned);
	CHECK(m[1].start == m[0].end);
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
	// (Stage 134: the mirror, then the step toward infinity)
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	INFO(steps_text(m));
	REQUIRE(m.size() >= 3);
	CHECK(m[0].type == rc::StepType::Aligned);
	CHECK(m[1].type == rc::StepType::Aligned);
	CHECK(m[1].to_infinity);
	CHECK(m[2].type != rc::StepType::Aligned);
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
	// the merged steps: aligned to the player's image, diverged past it (Stage
	// 134: the mirror, the wrap, the run back to the image)
	const std::vector<rc::TraceStep> &m = s.preview_steps();
	INFO(steps_text(m));
	REQUIRE(m.size() >= 4);
	for (size_t i = 0; i < 3; i++) {
		CHECK(m[i].type == rc::StepType::Aligned);
	}
	CHECK(m[1].to_infinity);
	CHECK(std::abs(m[2].end.x - 872.11f) < 0.01f); // (the player's image in the mirror x = 1200)
	CHECK(m[3].type != rc::StepType::Aligned);
}

// Stage 132 (GAME_SPEC 4.2): after [5 L, inf] the walk is behind the ray's origin,
// the player's image in the mirror x = 1200: its marker is there, plannable; a
// click plans it, and the user's cursor past it is visible on the plan
TEST_CASE("region reports: open_room_playable [5 L, inf] -- the ray's origin is plannable, and planned the cursor is visible") {
	rcg::Session s(rcg::load_level((ROOT / "data" / "levels" / "open_room_playable.json").string()));
	const rc::V2 player = rc::V2::of(0x1.7df8aep+10, 0x1.cef2e4p+9);
	s.teleport(player);
	rc::PlanItem it;
	it.surface_id = 5;
	it.left = true;
	it.hint = rc::effects::F32Point{0x1.2d906ap+10f, 0x1.36933ep+9f};
	rc::PlanItem inf;
	inf.surface_id = rc::INFINITY_ID;
	inf.left = true;
	inf.hint = rc::effects::F32Point{0x1.dc7f1p+10f, 0x1.90bdfep+4f};
	// [5 L] alone leaves the walk in front of it: no marker
	s.set_plan({it});
	s.set_cursor(rc::V2::of(0x1.d7fddcp+9, 0x1.b24de8p+9));
	s.settle();
	CHECK(!s.record(nlohmann::json::object()).contains("origin"));
	s.set_plan({it, inf});
	s.settle();
	const nlohmann::json r = s.record(nlohmann::json::object());
	REQUIRE(r.contains("origin"));
	REQUIRE(!r["origin"]["point"].is_null());
	// the image of the player in the mirror (the line x = 1200)
	const rc::V2 image = rc::V2::of(2400.0 - double(player.x), double(player.y));
	s.set_cursor(image + rc::V2::of(3.0, 2.0));
	s.settle();
	CHECK(s.record(nlohmann::json::object())["origin"]["hovered"] == true);
	s.click(false);
	REQUIRE(s.plan().size() == 3);
	CHECK(s.plan()[2].surface_id == rc::ORIGIN_ID);
	s.set_cursor(rc::V2::of(0x1.d7fddcp+9, 0x1.b24de8p+9));
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview visible, region inside") != std::string::npos);
	const nlohmann::json r2 = s.record(nlohmann::json::object());
	REQUIRE(r2.contains("origin"));
	REQUIRE(r2["origin"]["marks"].size() == 1);
	CHECK(r2["origin"]["marks"][0]["valid"] == true);
	CHECK(r2["origin"]["point"].is_null()); // (in front of it again: not plannable)
	// right-click on its mark removes it
	s.set_cursor(image);
	s.settle();
	s.set_plan({it, inf});
	s.settle();
	s.click(false);
	s.click(true);
	CHECK(s.plan().size() == 2);
}

// The user's report (F12, 2026-10-23, Stage 134): on ghost_room, [6 L, 11 R, inf,
// 12 R, the ray's origin] -- the beam departs mirror 6, passes the togglers, the
// wrap and the origin (the player's image, (660, 660)) on one beam and comes back
// to mirror 6, its departure. The region read that re-hit as the end of a full
// loop from the cast's start; the cast starts at the origin, and the re-hit lies
// ahead of it: visible from the origin to the mirror, not past it.
TEST_CASE("region reports: ghost_room [6 L, 11 R, inf, 12 R, origin] -- the departure re-hit ahead of the ray's origin") {
	rcg::Session s(rcg::load_level((ROOT / "data" / "levels" / "test_levels" / "ghost_room.json").string()));
	s.teleport(rc::V2::of(900.0, 540.0));
	auto entry = [](int sid, bool left, float cx, float cy) {
		rc::PlanItem it;
		it.surface_id = sid;
		it.left = left;
		it.hint = rc::effects::F32Point{cx, cy};
		return it;
	};
	s.set_plan({entry(6, true, 771.803955078125f, 558.033447265625f), entry(11, false, 991.0322875976562f, 443.8219299316406f),
		entry(rc::INFINITY_ID, true, 1757.8310546875f, 2.0037107467651367f), entry(12, false, 637.6641845703125f, 673.246826171875f),
		entry(rc::ORIGIN_ID, true, 668.696533203125f, 658.218994140625f)});
	REQUIRE(s.plan().size() == 5);
	// before the mirror: both walks reach the cursor alike
	s.set_cursor(rc::V2::of(0x1.7663d6p+9, 0x1.2f8fecp+9));
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview visible, region inside") != std::string::npos);
	// past it: the physical walk reflects there, the planned one passes (no entry plans it)
	s.set_cursor(rc::V2::of(0x1.ab71fcp+9, 0x1.0cff88p+9));
	s.settle();
	CHECK(s.debug_summary().find("cursor: preview not visible, region outside") != std::string::npos);
}
