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
