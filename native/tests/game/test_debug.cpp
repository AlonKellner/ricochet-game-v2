// Stage 128: the debug dump (F12): the solver's inputs exactly, the scene, the
// preview's walks and the region, as JSON.
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>

namespace {

const std::filesystem::path ROOT = RC_REPO_ROOT;

std::string hex(double v) {
	char buf[40];
	std::snprintf(buf, sizeof buf, "%a", v);
	return buf;
}

} // namespace

TEST_CASE("debug dump: the inputs exactly, the scene, the preview's walks and the region") {
	rcg::Session s(rcg::load_level((ROOT / "data" / "levels" / "test_levels" / "crossings.json").string()));
	const rc::V2 cursor = s.player() + rc::V2::of(300.1, -120.7);
	s.set_cursor(cursor);
	s.settle();
	const nlohmann::json d = nlohmann::json::parse(s.debug_dump().dump()); // (it round-trips as text)
	CHECK(d["cursor"]["hex"][0] == hex(double(cursor.x)));
	CHECK(d["cursor"]["hex"][1] == hex(double(cursor.y)));
	CHECK(d["player"]["hex"][0] == hex(double(s.player().x)));
	CHECK(d["plan"].empty());
	CHECK(d["defs"].size() == s.world().defs().size());
	CHECK(d["preview"]["exact"] == true);
	CHECK(!d["preview"]["ladder"]["physical"]["events"].empty());
	CHECK(!d["preview"]["merged"].empty());
	CHECK(d["region"]["supported"] == true);
	CHECK(d["region"]["cells"].get<int>() > 0);
}
