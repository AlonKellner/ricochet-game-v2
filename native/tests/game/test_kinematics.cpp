// Stage 117: the native player controller (rcg/kinematics.hpp). Its own
// rules (resting on a floor, stopping at a wall, the jump's apex, absent
// surfaces) and two properties on random inputs over every level with solids:
// no penetration (the circle never comes within the radius of a collider) and
// no tunnelling (between two ticks the path never passes through one).
#include "rcg/build.hpp"
#include "rcg/kinematics.hpp"
#include "rcg/rng.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

namespace {

namespace k = rcg::kinematics;
using rc::V2;

double clearance(const std::vector<k::Collider> &cs, double x, double y) {
	double best = std::numeric_limits<double>::infinity();
	for (const k::Collider &c : cs) {
		double nx, ny;
		best = std::min(best, k::distance(c, x, y, nx, ny));
	}
	return best;
}

k::Collider wall(double ax, double ay, double bx, double by) {
	k::Collider c;
	c.ax = ax;
	c.ay = ay;
	c.bx = bx;
	c.by = by;
	return c;
}

} // namespace

TEST_CASE("kinematics: a wall stops the player a skin away; a floor holds it; a jump reaches v^2 / 2g") {
	const std::vector<k::Collider> box{wall(0, 0, 1000, 0), wall(1000, 0, 1000, 1000), wall(1000, 1000, 0, 1000), wall(0, 1000, 0, 0)};
	k::Body b{V2{500, 500}, V2{}, false};
	for (int i = 0; i < 300; i++) {
		k::step(b, k::Input{1.0f, 0.0f, false}, V2{}, box);
	}
	CHECK(b.pos.x == doctest::Approx(1000 - k::RADIUS - k::SKIN).epsilon(1e-6));
	CHECK(b.pos.y == 500.0f);
	// gravity: falls to the floor and stays there
	const V2 g{0, 980};
	b = k::Body{V2{500, 500}, V2{}, false};
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{}, g, box);
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(1000 - k::RADIUS - k::SKIN).epsilon(1e-6));
	const float rest = b.pos.y;
	// a jump: up by about v^2 / 2g (400^2 / 1960 = 81.6 px), back down to the floor
	k::step(b, k::Input{0, 0, true}, g, box);
	float top = b.pos.y;
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{}, g, box);
		top = std::min(top, b.pos.y);
	}
	CHECK(rest - top == doctest::Approx(81.6).epsilon(0.05));
	CHECK(b.pos.y == rest);
	CHECK(b.on_floor);
	// no jump in the air
	b.pos.y -= 200.0f;
	b.on_floor = false;
	b.vel = V2{};
	k::step(b, k::Input{0, 0, true}, g, box);
	CHECK(b.vel.y > 0.0f);
}

TEST_CASE("kinematics: an absent player-solid surface does not block") {
	const rcg::Level lv = rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [
		{"ids": [1], "start": [600, 0], "end": [600, 1000], "via": [600, 500], "player_solid": true,
		 "left": {"effect": {"type": "terminal"}}, "right": {"effect": {"type": "terminal"}},
		 "presence": [{"key": "open", "values": ["DEFAULT"]}]}]})");
	const std::vector<rcg::BuiltSurface> world = rcg::build_level(lv);
	rc::Flags closed;
	rc::Flags open;
	open.set("open", true);
	CHECK(k::colliders_of(world, closed, lv).size() == 1);
	CHECK(k::colliders_of(world, open, lv).empty());
}

TEST_CASE("kinematics: no penetration, no tunnelling, on random input over every level with solids") {
	const std::filesystem::path root = RC_REPO_ROOT;
	int levels = 0;
	long ticks = 0;
	for (const auto &e : std::filesystem::recursive_directory_iterator(root / "data" / "levels")) {
		if (e.path().extension() != ".json") {
			continue;
		}
		const rcg::Level lv = rcg::load_level(e.path().string());
		const std::vector<k::Collider> cs = k::colliders_of(rcg::build_level(lv), rc::Flags{}, lv);
		if (cs.empty() || !lv.player_start) {
			continue;
		}
		CAPTURE(e.path().filename().string());
		levels++;
		for (const V2 &g : {V2{}, V2{0, 980}}) {
			// seeded by the level's own path (FNV-1a), so a level added elsewhere changes no other level's walk
			std::uint64_t seed = 0xcbf29ce484222325ull;
			for (const char c : std::filesystem::relative(e.path(), root).generic_string()) {
				seed = (seed ^ std::uint64_t(static_cast<unsigned char>(c))) * 0x100000001b3ull;
			}
			rcg::GodotRng rng(seed + (g.y > 0 ? 1u : 0u));
			k::Body b{*lv.player_start, V2{}, false};
			if (clearance(cs, b.pos.x, b.pos.y) < k::RADIUS) {
				continue; // a start inside a solid (none today)
			}
			k::Input in;
			for (int t = 0; t < 1500; t++) {
				if (t % 20 == 0) {
					in.axis_x = float(int(rng.randi() % 3u) - 1);
					in.axis_y = float(int(rng.randi() % 3u) - 1);
				}
				in.jump = rng.randi() % 30u == 0u;
				const V2 before = b.pos;
				k::step(b, in, g, cs);
				// no penetration (the float32 rounding of the position aside)
				const double cl = clearance(cs, b.pos.x, b.pos.y);
				REQUIRE_MESSAGE(cl >= k::RADIUS - 1e-3, "tick " << t << " at " << b.pos.x << "," << b.pos.y << ": clearance " << cl);
				// no tunnelling: the straight path between the ticks keeps its distance
				for (int s = 1; s < 8; s++) {
					const double x = before.x + (b.pos.x - before.x) * s / 8.0, y = before.y + (b.pos.y - before.y) * s / 8.0;
					REQUIRE_MESSAGE(clearance(cs, x, y) >= k::RADIUS - 1e-3, "tick " << t << " between the ticks");
				}
				ticks++;
			}
		}
	}
	CHECK(levels >= 3);
	CHECK(ticks > 5000);
}

TEST_CASE("kinematics: a fall at any speed does not tunnel through a thin floor") {
	const std::vector<k::Collider> floor_only{wall(-1000, 500, 2000, 500)};
	for (float g : {1e3f, 1e5f, 1e7f}) {
		k::Body b{V2{100, 100}, V2{}, false};
		// with g = 1e7 px/s^2 the fall passes the floor's whole approach in one tick
		for (int i = 0; i < 60; i++) {
			k::step(b, k::Input{}, V2{0.0f, g}, floor_only);
			REQUIRE(b.pos.y <= 500.0f);
		}
		CHECK(b.pos.y == doctest::Approx(500 - k::RADIUS - k::SKIN).epsilon(1e-6));
		CHECK(b.on_floor);
	}
}
