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
#include <tuple>

namespace {

namespace k = rcg::kinematics;
using rc::V2;

// (a platform is not counted: it is overlapped whenever something passes it;
// a shape at (x, y): from its nearest circle)
double clearance(const std::vector<k::Collider> &cs, double x, double y, const k::Shape &shape = k::Shape{}) {
	double best = std::numeric_limits<double>::infinity();
	for (const k::Collider &c : cs) {
		if (c.platform) {
			continue;
		}
		double nx, ny;
		best = std::min(best, k::distance(c, shape, x, y, nx, ny));
	}
	return best;
}

// a circle body's tick (the controller's own rules, a circle of the player's radius)
void step1(k::Body &b, const k::Input &in, const V2 &g, const std::vector<k::Collider> &cs) { k::step_body(b, in, g, cs, k::RADIUS); }

k::Collider wall(double ax, double ay, double bx, double by) {
	k::Collider c;
	c.ax = ax;
	c.ay = ay;
	c.bx = bx;
	c.by = by;
	return c;
}

k::Collider platform(double ax, double ay, double bx, double by) {
	k::Collider c = wall(ax, ay, bx, by);
	c.platform = true;
	return c;
}

const V2 G{0, 980};
const double REST = k::RADIUS + k::SKIN; // a body's centre above what it rests on

void run(k::Body &b, const k::Input &in, const V2 &g, const std::vector<k::Collider> &cs, int ticks) {
	for (int i = 0; i < ticks; i++) {
		step1(b, in, g, cs);
	}
}

} // namespace

TEST_CASE("kinematics: a wall stops the player a skin away; a floor holds it; a jump reaches v^2 / 2g") {
	const std::vector<k::Collider> box{wall(0, 0, 1000, 0), wall(1000, 0, 1000, 1000), wall(1000, 1000, 0, 1000), wall(0, 1000, 0, 0)};
	k::Body b{V2{500, 500}, V2{}, false};
	for (int i = 0; i < 300; i++) {
		step1(b, k::Input{1.0f, 0.0f, false}, V2{}, box);
	}
	CHECK(b.pos.x == doctest::Approx(1000 - k::RADIUS - k::SKIN).epsilon(1e-6));
	CHECK(b.pos.y == 500.0f);
	// gravity: falls to the floor and stays there
	const V2 g{0, 980};
	b = k::Body{V2{500, 500}, V2{}, false};
	for (int i = 0; i < 120; i++) {
		step1(b, k::Input{}, g, box);
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(1000 - k::RADIUS - k::SKIN).epsilon(1e-6));
	const float rest = b.pos.y;
	// a jump: up by about v^2 / 2g (400^2 / 1960 = 81.6 px), back down to the floor
	step1(b, k::Input{0, 0, true}, g, box);
	float top = b.pos.y;
	for (int i = 0; i < 120; i++) {
		step1(b, k::Input{}, g, box);
		top = std::min(top, b.pos.y);
	}
	CHECK(rest - top == doctest::Approx(81.6).epsilon(0.05));
	CHECK(b.pos.y == rest);
	CHECK(b.on_floor);
	// no jump in the air
	b.pos.y -= 200.0f;
	b.on_floor = false;
	b.vel = V2{};
	step1(b, k::Input{0, 0, true}, g, box);
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
			// (as a session spawns and moves it: tall under gravity)
			const k::Shape shape = g.y > 0 ? k::PLAYER : k::Shape{};
			k::Body b{g.y > 0 ? V2::of(lv.player_start->x, double(lv.player_start->y) - k::BODY) : *lv.player_start, V2{}, false};
			if (clearance(cs, b.pos.x, b.pos.y, shape) < k::RADIUS) {
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
				k::step_body(b, in, g, cs, shape);
				// no penetration (the float32 rounding of the position aside)
				const double cl = clearance(cs, b.pos.x, b.pos.y, shape);
				REQUIRE_MESSAGE(cl >= k::RADIUS - 1e-3, "tick " << t << " at " << b.pos.x << "," << b.pos.y << ": clearance " << cl);
				// no tunnelling: the straight path between the ticks keeps its distance -- but for the sagitta
				// of a slide around a convex corner within the tick (the true path keeps outside it; its chord
				// of length L cuts in by at most L^2 / 8r); a tunnel would leave no clearance at all
				const double chord = std::hypot(double(b.pos.x) - before.x, double(b.pos.y) - before.y);
				const double sagitta = chord * chord / (8.0 * k::RADIUS);
				for (int s = 1; s < 8; s++) {
					const double x = before.x + (b.pos.x - before.x) * s / 8.0, y = before.y + (b.pos.y - before.y) * s / 8.0;
					REQUIRE_MESSAGE(clearance(cs, x, y, shape) >= k::RADIUS - 1e-3 - sagitta, "tick " << t << " between the ticks");
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
			step1(b, k::Input{}, V2{0.0f, g}, floor_only);
			REQUIRE(b.pos.y <= 500.0f);
		}
		CHECK(b.pos.y == doctest::Approx(500 - k::RADIUS - k::SKIN).epsilon(1e-6));
		CHECK(b.on_floor);
	}
}

// Stage 172: platforms -- one-way, stood on from above, dropped through with down held

TEST_CASE("platforms: a body falling onto one rests on its top") {
	const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), wall(0, 1000, 1000, 1000)};
	k::Body b{V2{500, 300}, V2{}, false};
	run(b, k::Input{}, G, cs, 120);
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(500 - REST).epsilon(1e-6));
	// walking along it keeps it there
	run(b, k::Input{1.0f, 0.0f, false}, G, cs, 30);
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(500 - REST).epsilon(1e-6));
}

TEST_CASE("platforms: a jump from below passes up through one, keeps its whole height and lands on top") {
	const std::vector<k::Collider> floor{wall(0, 1000, 1000, 1000)};
	std::vector<k::Collider> cs = floor;
	cs.push_back(platform(0, 950, 1000, 950));
	auto apex = [](const std::vector<k::Collider> &with) {
		k::Body b{V2{500, float(1000 - REST)}, V2{}, true};
		step1(b, k::Input{0, 0, true}, G, with);
		float top = b.pos.y;
		for (int i = 0; i < 120; i++) {
			step1(b, k::Input{}, G, with);
			top = std::min(top, b.pos.y);
		}
		return std::pair{top, b};
	};
	const auto [free_top, free_end] = apex(floor);
	const auto [top, end] = apex(cs);
	CHECK(top == free_top); // the platform neither stopped nor lifted the jump
	CHECK(free_end.pos.y == doctest::Approx(1000 - REST).epsilon(1e-6));
	CHECK(end.on_floor);
	CHECK(end.pos.y == doctest::Approx(950 - REST).epsilon(1e-6)); // on the platform
}

TEST_CASE("platforms: down held drops through; released before the centre passes, back on top; after, on down") {
	const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), wall(0, 1000, 1000, 1000)};
	const k::Input down{0.0f, 1.0f, false};
	k::Body standing{V2{500, float(500 - REST)}, V2{}, true};
	run(standing, k::Input{}, G, cs, 5);
	REQUIRE(standing.on_floor);
	SUBCASE("held: through, onto the floor below") {
		k::Body b = standing;
		run(b, down, G, cs, 120);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(1000 - REST).epsilon(1e-6));
	}
	SUBCASE("released early: the centre still above, put back on top") {
		k::Body b = standing;
		run(b, down, G, cs, 4);
		CHECK(b.pos.y > standing.pos.y); // it did start to fall
		CHECK(b.pos.y < 500.0f);
		run(b, k::Input{}, G, cs, 30);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(500 - REST).epsilon(1e-6));
	}
	SUBCASE("released once the centre is below: it falls on") {
		k::Body b = standing;
		int ticks = 0;
		while (b.pos.y <= 500.0f && ticks < 100) {
			step1(b, down, G, cs);
			ticks++;
		}
		CHECK(ticks < 20); // about a sixth of a second
		run(b, k::Input{}, G, cs, 120);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(1000 - REST).epsilon(1e-6));
	}
	SUBCASE("down held in the air falls past it") {
		k::Body b{V2{500, 300}, V2{}, false};
		run(b, down, G, cs, 120);
		CHECK(b.pos.y == doctest::Approx(1000 - REST).epsilon(1e-6));
	}
}

TEST_CASE("platforms: half through one, the centre decides -- above is put on top, below falls through") {
	const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), wall(0, 1000, 1000, 1000)};
	k::Body above{V2{500, 495}, V2{}, false};
	step1(above, k::Input{}, G, cs);
	CHECK(above.on_floor);
	CHECK(above.pos.y == doctest::Approx(500 - REST).epsilon(1e-6));
	k::Body below{V2{500, 505}, V2{}, false};
	run(below, k::Input{}, G, cs, 120);
	CHECK(below.pos.y == doctest::Approx(1000 - REST).epsilon(1e-6));
	// settle (a journey's end) alike: on top or left where it is
	k::Body s1{V2{500, 495}, V2{}, false};
	k::settle(s1, cs, k::Shape{k::RADIUS});
	CHECK(s1.on_floor);
	CHECK(s1.pos.y == doctest::Approx(500 - REST).epsilon(1e-6));
	k::Body s2{V2{500, 505}, V2{}, false};
	k::settle(s2, cs, k::Shape{k::RADIUS});
	CHECK_FALSE(s2.on_floor);
	CHECK(s2.pos.y == 505.0f);
	// settle moves nothing onto a wall or a fence (only platforms)
	k::Body s3{V2{500, 995}, V2{}, false};
	k::settle(s3, cs, k::Shape{k::RADIUS});
	CHECK(s3.pos.y == 995.0f);
}

TEST_CASE("platforms: from below and the side never blocks; without gravity it passes both ways") {
	const std::vector<k::Collider> cs{platform(400, 500, 600, 500)};
	// from the side: walks through its end
	k::Body side{V2{300, 500}, V2{}, false};
	run(side, k::Input{1.0f, 0.0f, false}, V2{}, cs, 120);
	CHECK(side.pos.x > 650.0f);
	// top-down: down through it, and back up
	k::Body b{V2{500, 400}, V2{}, false};
	run(b, k::Input{0.0f, 1.0f, false}, V2{}, cs, 60);
	CHECK(b.pos.y > 550.0f);
	run(b, k::Input{0.0f, -1.0f, false}, V2{}, cs, 60);
	CHECK(b.pos.y < 450.0f);
}

TEST_CASE("platforms: a circle holds on its top and passes from below; a slope over 45 degrees never holds") {
	k::Collider ring = k::circle_collider(V2{500, 500}, 50);
	ring.platform = true;
	const std::vector<k::Collider> cs{ring, wall(0, 1000, 1000, 1000)};
	k::Body b{V2{500, 300}, V2{}, false};
	run(b, k::Input{}, G, cs, 120);
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(450 - REST).epsilon(1e-6));
	// rising through it from below (an upward push, no gravity: straight up and out of its top)
	k::Body up{V2{500, 600}, V2{}, false};
	run(up, k::Input{0.0f, -1.0f, false}, V2{}, cs, 90);
	CHECK(up.pos.y == doctest::Approx(300).epsilon(1e-6)); // 300 px unhindered
	// a steep platform (60 degrees) holds nothing
	const std::vector<k::Collider> steep{platform(500, 400, 500 + 100 * 0.5, 400 + 100 * 0.8660254037844386), wall(0, 1000, 1000, 1000)};
	k::Body s{V2{520, 300}, V2{}, false};
	run(s, k::Input{}, G, steep, 120);
	CHECK(s.pos.y == doctest::Approx(1000 - REST).epsilon(1e-6));
}

// Stage 185 (the user): moving while overlapping a platform engages it -- gravity-free movement until it overlaps none

namespace {
void run_ticks(k::Body &b, const k::Input &in, const std::vector<k::Collider> &cs, int ticks) {
	for (int i = 0; i < ticks; i++) {
		k::step(b, in, G, cs);
	}
}
} // namespace

TEST_CASE("engaging: not moving, a body in a platform falls as it would without it") {
	const std::vector<k::Collider> floor{wall(0, 1000, 1000, 1000)};
	for (const k::Collider &p : {platform(0, 500, 1000, 500), platform(500, 0, 500, 1000)}) { // level, and a rope it falls along
		std::vector<k::Collider> cs = floor;
		cs.push_back(p);
		k::Body with{V2{500, 490}, V2{}, false}, without = with;
		for (int i = 0; i < 60; i++) {
			k::step(with, k::Input{}, G, cs);
			k::step(without, k::Input{}, G, floor);
			CHECK_FALSE(with.engaged);
			CHECK(with.pos.y == without.pos.y);
			CHECK(with.vel.y == without.vel.y);
		}
	}
}

TEST_CASE("engaging: moving, any part overlapping engages at once, at the speed without gravity") {
	const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), wall(0, 1000, 1000, 1000)};
	const float diagonal = float(k::SPEED) * 0.70710678f;
	for (const auto &[in, vx, vy] : std::vector<std::tuple<k::Input, float, float>>{
				 {k::Input{1, 0, false}, float(k::SPEED), 0.0f},
				 {k::Input{-1, 0, false}, -float(k::SPEED), 0.0f},
				 {k::Input{0, -1, false}, 0.0f, -float(k::SPEED)},
				 {k::Input{0, 1, false}, 0.0f, float(k::SPEED)},
				 {k::Input{1, 1, false}, diagonal, diagonal}}) {
		for (const float y : {490.0f, 478.0f, 466.0f}) { // the line at the head, at the middle circle, at the feet
			k::Body b{V2{500, y}, V2{-50, 300}, false}; // falling, drifting: no momentum is kept
			k::step(b, in, G, cs);
			CHECK(b.engaged);
			CHECK(b.vel.x == doctest::Approx(vx));
			CHECK(b.vel.y == doctest::Approx(vy));
		}
	}
	k::Body clear{V2{500, 520}, V2{}, false}; // (the head's circle past the line)
	k::step(clear, k::Input{1, 0, false}, G, cs);
	CHECK_FALSE(clear.engaged);
}

TEST_CASE("engaging: released, the feet's line decides -- on a rope it hangs; never with the feet below a level platform") {
	SUBCASE("a rope: the feet's line crosses it all along: it hangs, at any height, until it moves off") {
		const std::vector<k::Collider> cs{platform(500, 100, 500, 1000), wall(0, 1000, 1000, 1000)};
		k::Body b{V2{500, 400}, V2{}, false};
		k::step(b, k::Input{0, -1, false}, G, cs);
		REQUIRE(b.engaged);
		const V2 at = b.pos;
		for (int i = 0; i < 60; i++) {
			k::step(b, k::Input{}, G, cs);
			CHECK(b.engaged);
			CHECK(b.pos.x == at.x);
			CHECK(b.pos.y == at.y);
		}
		// off to the side, the feet's line (as wide as the feet) past it: it falls
		run_ticks(b, k::Input{1, 0, false}, cs, 10);
		CHECK_FALSE(b.engaged);
		const float vy = b.vel.y;
		k::step(b, k::Input{}, G, cs);
		CHECK(b.vel.y > vy);
	}
	SUBCASE("a rope's end: the feet's line below it lets go") {
		const std::vector<k::Collider> cs{platform(500, 100, 500, 400), wall(0, 1000, 1000, 1000)};
		k::Body b{V2{500, 340}, V2{}, false}; // the feet's bottom at 376, the rope to 400
		k::step(b, k::Input{0, 1, false}, G, cs);
		REQUIRE(b.engaged);
		k::step(b, k::Input{}, G, cs);
		CHECK(b.engaged);
		k::Body under{V2{500, 370}, V2{}, false}; // the feet's bottom at 406, the body still at the rope
		k::step(under, k::Input{0, -1, false}, G, cs);
		REQUIRE(under.engaged);
		k::Body low = under;
		low.pos = V2{500, 370};
		k::step(low, k::Input{}, G, cs);
		CHECK_FALSE(low.engaged);
	}
	SUBCASE("a slope and an arc: the feet's line on them, it hangs") {
		const std::vector<k::Collider> slope{platform(400, 400, 600, 600)};
		k::Body b{V2{500, float(500 - 24 - 12)}, V2{}, false};
		k::step(b, k::Input{0, -1, false}, G, slope);
		REQUIRE(b.engaged);
		k::step(b, k::Input{}, G, slope);
		CHECK(b.engaged);
		k::Collider ring = k::circle_collider(V2{500, 500}, 50);
		ring.platform = true;
		k::Body a{V2{450, float(500 - 24 - 12)}, V2{}, false};
		k::step(a, k::Input{0, -1, false}, G, {ring});
		REQUIRE(a.engaged);
		k::step(a, k::Input{}, G, {ring});
		CHECK(a.engaged);
	}
	SUBCASE("a level platform: released with the feet below it, it falls; just under the top, it is put on top") {
		const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), wall(0, 1000, 1000, 1000)};
		const float on_top = float(500 - REST - k::BODY), on_floor = float(1000 - REST - k::BODY);
		for (const float y : {490.0f, 480.0f}) { // (the feet's centre below the line)
			k::Body b{V2{500, y}, V2{}, false};
			k::step(b, k::Input{1, 0, false}, G, cs);
			REQUIRE(b.engaged);
			k::step(b, k::Input{}, G, cs);
			CHECK_FALSE(b.engaged);
			run_ticks(b, k::Input{}, cs, 120);
			CHECK(b.pos.y == doctest::Approx(on_floor).epsilon(1e-6));
		}
		k::Body b{V2{500, 470}, V2{}, false}; // the feet's centre at 494, the line inside it
		k::step(b, k::Input{1, 0, false}, G, cs);
		REQUIRE(b.engaged);
		k::step(b, k::Input{}, G, cs);
		CHECK_FALSE(b.engaged);
		run_ticks(b, k::Input{}, cs, 30);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(on_top).epsilon(1e-6));
	}
}

TEST_CASE("engaging: held, it ends when no part overlaps a platform -- out of the bottom it falls, out of the top it lands, out of the side it falls") {
	const std::vector<k::Collider> cs{platform(400, 500, 600, 500), wall(0, 1000, 1000, 1000)};
	const float on_floor = float(1000 - REST - k::BODY), on_top = float(500 - REST - k::BODY);
	SUBCASE("down") {
		k::Body b{V2{500, 490}, V2{}, false};
		for (int i = 0; i < 6; i++) { // to the head's circle past the line (at 512)
			k::step(b, k::Input{0, 1, false}, G, cs);
			CHECK(b.vel.y == float(k::SPEED));
		}
		run_ticks(b, k::Input{0, 1, false}, cs, 2);
		CHECK_FALSE(b.engaged);
		CHECK(b.vel.y > float(k::SPEED)); // gravity again
		run_ticks(b, k::Input{}, cs, 120);
		CHECK(b.pos.y == doctest::Approx(on_floor).epsilon(1e-6));
	}
	SUBCASE("up: past the top, held or not, it lands there and stands") {
		for (const float held : {-1.0f, 0.0f}) {
			k::Body b{V2{500, 490}, V2{}, false};
			run_ticks(b, k::Input{0, -1, false}, cs, 16); // (the feet's circle past the line at 464)
			run_ticks(b, k::Input{0, held, false}, cs, 120);
			CHECK(b.on_floor);
			CHECK_FALSE(b.engaged);
			CHECK(b.pos.y == doctest::Approx(on_top).epsilon(1e-6));
		}
	}
	SUBCASE("sideways: past its end, it falls") {
		k::Body b{V2{500, 490}, V2{}, false};
		run_ticks(b, k::Input{1, 0, false}, cs, 30);
		CHECK(b.engaged);
		CHECK(b.pos.y == 490.0f);
		run_ticks(b, k::Input{1, 0, false}, cs, 20);
		CHECK_FALSE(b.engaged);
		CHECK(b.pos.x > 612.0f);
		CHECK(b.vel.y > 0.0f);
	}
}

TEST_CASE("engaging: standing on a platform, or landing on it moving, is no overlap") {
	const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), wall(0, 1000, 1000, 1000)};
	const float on_top = float(500 - REST - k::BODY);
	k::Body b{V2{100, 300}, V2{}, false};
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{1, 0, false}, G, cs); // falls onto it, walking
		CHECK_FALSE(b.engaged);
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(on_top).epsilon(1e-6));
	// a jump off it, and up held on it: never engaged
	k::step(b, k::Input{0, -1, true}, G, cs);
	CHECK(b.vel.y == -float(k::FEEL_JUMP_VELOCITY));
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{0, -1, false}, G, cs);
		CHECK_FALSE(b.engaged);
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(on_top).epsilon(1e-6));
}

TEST_CASE("dropping: down pressed standing on a platform falls under gravity, not engaging; a second press or another direction engages (Stage 187)") {
	const std::vector<k::Collider> cs{platform(0, 500, 1000, 500), platform(0, 600, 1000, 600), wall(0, 1000, 1000, 1000)};
	const float on_top = float(500 - REST - k::BODY), on_floor = float(1000 - REST - k::BODY);
	const k::Input down{0, 1, false};
	k::Body standing{V2{500, on_top}, V2{}, true};
	k::step(standing, k::Input{}, G, cs);
	REQUIRE(standing.on_floor);
	SUBCASE("held: gravity's fall through every platform, never engaged, down to the floor") {
		k::Body b = standing;
		float last = 0;
		for (int i = 0; i < 120; i++) {
			k::step(b, down, G, cs);
			CHECK_FALSE(b.engaged);
			if (!b.on_floor) {
				CHECK(b.vel.y >= last); // never the engaged speed's constant stop
			}
			last = b.vel.y;
		}
		CHECK(last == 0.0f); // landed
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(on_floor).epsilon(1e-6));
	}
	SUBCASE("an instant's tap: it falls through that platform, never engaged, and lands on the next") {
		k::Body b = standing;
		k::step(b, down, G, cs);
		for (int i = 0; i < 90; i++) {
			k::step(b, k::Input{}, G, cs);
			CHECK_FALSE(b.engaged);
		}
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(600 - REST - k::BODY).epsilon(1e-6));
	}
	SUBCASE("a second press engages at the speed without gravity; released with the feet below it, it falls on") {
		k::Body b = standing;
		run_ticks(b, down, cs, 12);
		REQUIRE_FALSE(b.engaged);
		k::step(b, k::Input{}, G, cs);
		CHECK_FALSE(b.engaged);
		k::step(b, down, G, cs);
		CHECK(b.engaged);
		CHECK(b.vel.y == float(k::SPEED));
		k::step(b, k::Input{}, G, cs);
		CHECK_FALSE(b.engaged);
	}
	SUBCASE("another direction engages, down still held or not; up too") {
		for (const k::Input other : {k::Input{1, 1, false}, k::Input{-1, 0, false}, k::Input{0, -1, false}}) {
			k::Body b = standing;
			run_ticks(b, down, cs, 12);
			k::step(b, other, G, cs);
			CHECK(b.engaged);
			CHECK_FALSE(b.dropping);
		}
	}
	SUBCASE("not standing: down with a part in a platform engages at the first press") {
		k::Body b{V2{500, 490}, V2{}, false};
		k::step(b, down, G, cs);
		CHECK(b.engaged);
	}
	SUBCASE("standing on a floor, down is no drop") {
		k::Body b{V2{500, on_floor}, V2{}, true};
		k::step(b, down, G, cs);
		CHECK_FALSE(b.dropping);
		CHECK(b.pos.y == doctest::Approx(on_floor).epsilon(1e-6));
	}
}

TEST_CASE("engaging: a jump held through a platform, a stack, a rope to the platform at its top, a bare rope") {
	const float on_floor = float(1000 - REST - k::BODY); // the head, the feet resting on y = 1000
	SUBCASE("a jump, up held: engaged when the head reaches it, the speed without gravity, on top") {
		const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(400, 900, 600, 900)};
		k::Body b{V2{500, on_floor}, V2{}, true};
		k::step(b, k::Input{0, -1, true}, G, cs);
		CHECK(b.vel.y == -float(k::FEEL_JUMP_VELOCITY));
		bool engaged = false;
		for (int i = 0; i < 120; i++) {
			k::step(b, k::Input{0, -1, false}, G, cs);
			if (b.engaged) {
				engaged = true;
				CHECK(b.vel.y == -float(k::SPEED));
			}
		}
		CHECK(engaged);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(900 - REST - k::BODY).epsilon(1e-6));
	}
	SUBCASE("a tapped jump into one, nothing held: no engagement, it falls back") {
		const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(400, 950, 600, 950)};
		k::Body b{V2{500, on_floor}, V2{}, true};
		k::step(b, k::Input{0, -1, true}, G, cs);
		for (int i = 0; i < 120; i++) {
			k::step(b, k::Input{}, G, cs);
			CHECK_FALSE(b.engaged);
		}
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(on_floor).epsilon(1e-6));
	}
	SUBCASE("a stack, 50 apart (the body is 48 tall): a jump held climbs to the top one and stands there") {
		std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000)};
		for (int y = 950; y >= 700; y -= 50) {
			cs.push_back(platform(400, y, 600, y));
		}
		k::Body b{V2{500, on_floor}, V2{}, true};
		k::step(b, k::Input{0, -1, true}, G, cs);
		run_ticks(b, k::Input{0, -1, false}, cs, 300);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(700 - REST - k::BODY).epsilon(1e-6));
	}
	SUBCASE("a rope with a platform at its top: up held climbs from the floor, steps onto the top and stays") {
		const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(500, 1000, 500, 500), platform(450, 500, 550, 500)};
		k::Body b{V2{500, on_floor}, V2{}, true};
		k::step(b, k::Input{0, -1, false}, G, cs);
		CHECK(b.vel.y == -float(k::SPEED));
		run_ticks(b, k::Input{0, -1, false}, cs, 300);
		CHECK(b.on_floor);
		CHECK(b.pos.y == doctest::Approx(500 - REST - k::BODY).epsilon(1e-6));
		run_ticks(b, k::Input{0, -1, false}, cs, 60);
		CHECK(b.pos.y == doctest::Approx(500 - REST - k::BODY).epsilon(1e-6)); // no bob at the top
	}
	SUBCASE("a bare rope: up held, the player keeps near its top; released on it, it hangs; off it, it falls") {
		const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(500, 1000, 500, 500)};
		k::Body b{V2{500, on_floor}, V2{}, true};
		run_ticks(b, k::Input{0, -1, false}, cs, 300);
		for (int i = 0; i < 60; i++) {
			k::step(b, k::Input{0, -1, false}, G, cs);
			CHECK(b.pos.y + k::BODY >= 500 - REST - 25); // the feet within a small hop of its top
			CHECK(b.pos.y + k::BODY <= 500 + REST);
		}
		k::Body hang{V2{500, 700}, V2{}, false};
		k::step(hang, k::Input{0, -1, false}, G, cs);
		const float y = hang.pos.y;
		run_ticks(hang, k::Input{}, cs, 60);
		CHECK(hang.pos.y == y);
		run_ticks(hang, k::Input{1, 0, false}, cs, 10); // off it sideways
		CHECK_FALSE(hang.engaged);
		run_ticks(hang, k::Input{}, cs, 120);
		CHECK(hang.on_floor);
		CHECK(hang.pos.y == doctest::Approx(on_floor).epsilon(1e-6));
	}
	SUBCASE("walking across a rope on the floor: engaged while through it, running on at its speed, still on the floor") {
		const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(500, 700, 500, 1000)};
		k::Body b{V2{300, on_floor}, V2{float(k::SPEED), 0}, true};
		bool engaged = false;
		for (int i = 0; i < 120; i++) {
			k::step(b, k::Input{1, 0, false}, G, cs);
			engaged = engaged || b.engaged;
			CHECK(b.vel.x == float(k::SPEED));
			CHECK(b.pos.y == doctest::Approx(on_floor).epsilon(1e-4));
		}
		CHECK(engaged);
		CHECK_FALSE(b.engaged);
		CHECK(b.on_floor);
	}
	SUBCASE("a jump pressed while engaged is no jump after it") {
		const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(400, 950, 600, 950)};
		k::Body b{V2{500, 940}, V2{}, false};
		k::step(b, k::Input{0, -1, true}, G, cs);
		CHECK(b.engaged);
		CHECK(b.buffer == 0);
		CHECK(b.vel.y == -float(k::SPEED));
	}
	SUBCASE("a wall still stops it") {
		const std::vector<k::Collider> cs{wall(600, 0, 600, 1000), platform(400, 500, 800, 500)};
		k::Body b{V2{500, 490}, V2{}, false};
		run_ticks(b, k::Input{1, 0, false}, cs, 60);
		CHECK(b.engaged);
		CHECK(b.pos.x == doctest::Approx(600 - REST).epsilon(1e-6));
		CHECK(b.pos.y == 490.0f);
	}
	SUBCASE("without gravity nothing engages; an object never does") {
		const std::vector<k::Collider> cs{platform(500, 1000, 500, 500)};
		k::Body b{V2{500, 800}, V2{}, false};
		k::step(b, k::Input{0, -1, false}, V2{}, cs);
		CHECK(b.vel.y == -float(k::SPEED));
		CHECK_FALSE(b.engaged);
		k::Body o{V2{500, 800}, V2{}, false};
		step1(o, k::Input{0, -1, false}, G, cs);
		CHECK_FALSE(o.engaged);
		CHECK(o.vel.y == doctest::Approx(980.0 / 60.0));
	}
}

TEST_CASE("platforms: a platform surface is a platform collider; a platform is never also player_solid") {
	const rcg::Level lv = rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [
		{"ids": [1], "start": [0, 500], "end": [1000, 500], "via": [500, 500], "platform": true,
		 "left": {"effect": {"type": "pass"}}, "right": {"effect": {"type": "pass"}}}]})");
	const std::vector<rcg::BuiltSurface> world = rcg::build_level(lv);
	REQUIRE(world.size() == 1);
	CHECK(world[0].platform);
	CHECK_FALSE(world[0].player_solid);
	CHECK(rcg::describe_surface(world[0])["platform"] == true);
	const std::vector<k::Collider> cs = k::colliders_of(world, rc::Flags{}, lv);
	REQUIRE(cs.size() == 1);
	CHECK(cs[0].platform);
	CHECK_THROWS_AS(rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [
		{"ids": [1], "start": [0, 500], "end": [1000, 500], "via": [500, 500], "platform": true, "player_solid": true,
		 "left": null, "right": null}]})"), rcg::LevelError);
	// a surface that is not a platform is described as before (no key)
	const rcg::Level wall_lv = rcg::parse_level(R"({"schema": "rc-level/1", "surfaces": [
		{"ids": [1], "start": [0, 500], "end": [1000, 500], "via": [500, 500], "player_solid": true,
		 "left": {"effect": {"type": "terminal"}}, "right": {"effect": {"type": "terminal"}}}]})");
	CHECK_FALSE(rcg::describe_surface(rcg::build_level(wall_lv)[0]).contains("platform"));
}

// Stage 173: the player is taller -- head (its position, the arrow origin) to feet, BODY apart

TEST_CASE("tall player: it stands on its feet, its head stops at a low ceiling and under a passage too low for it") {
	const double stand = 1000 - k::BODY - REST; // its position (the head) standing on the floor at 1000
	k::Body b{V2{500, 500}, V2{}, false};
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{}, G, {wall(0, 1000, 1000, 1000)});
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(stand).epsilon(1e-6));
	// a ceiling 70 above the floor cuts a jump (85 high, up held: Stage 182) short at the head
	const std::vector<k::Collider> low{wall(0, 1000, 1000, 1000), wall(0, 930, 1000, 930)};
	k::Body j{V2{500, float(stand)}, V2{}, true};
	k::step(j, k::Input{0, -1, true}, G, low);
	float top = j.pos.y;
	for (int i = 0; i < 60; i++) {
		k::step(j, k::Input{0, i < 30 ? -1.0f : 0.0f, false}, G, low);
		top = std::min(top, j.pos.y);
	}
	CHECK(top == doctest::Approx(930 + REST).epsilon(1e-6));
	CHECK(j.pos.y == doctest::Approx(stand).epsilon(1e-6));
	// a passage 40 high: a circle walks under it, the player does not
	const std::vector<k::Collider> passage{wall(0, 1000, 2000, 1000), wall(600, 960, 1000, 960)};
	k::Body circle{V2{400, float(1000 - REST)}, V2{}, true};
	run(circle, k::Input{1.0f, 0.0f, false}, G, passage, 120);
	CHECK(circle.pos.x > 700.0f);
	k::Body player{V2{400, float(stand)}, V2{}, true};
	for (int i = 0; i < 120; i++) {
		k::step(player, k::Input{1.0f, 0.0f, false}, G, passage);
	}
	CHECK(player.pos.x < 600.0f);
}

TEST_CASE("tall player: platforms judge by its feet -- a jump through lands on top, a drop needs the feet past") {
	const double on = 950 - k::BODY - REST; // standing on the platform at 950
	const std::vector<k::Collider> cs{wall(0, 1000, 1000, 1000), platform(0, 950, 1000, 950)};
	k::Body b{V2{500, float(1000 - k::BODY - REST)}, V2{}, true};
	k::step(b, k::Input{0, -1, true}, G, cs);
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{0, i < 30 ? -1.0f : 0.0f, false}, G, cs); // (up held: the whole jump, Stage 182)
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(on).epsilon(1e-6)); // its head passed first, never lifted
	// a drop held: through to the floor (a short one hangs: Stage 185)
	k::Body d = b;
	for (int i = 0; i < 64; i++) {
		k::step(d, k::Input{0.0f, 1.0f, false}, G, cs);
	}
	CHECK(d.pos.y == doctest::Approx(1000 - k::BODY - REST).epsilon(1e-6));
	// settled half through: its head above but its feet below stay through; its feet above, on top
	k::Body s1{V2{500, float(950 - k::BODY + 5)}, V2{}, false};
	k::settle(s1, cs, k::PLAYER);
	CHECK(s1.pos.y == float(950 - k::BODY + 5));
	k::Body s2{V2{500, float(950 - k::BODY - 5)}, V2{}, false};
	k::settle(s2, cs, k::PLAYER);
	CHECK(s2.on_floor);
	CHECK(s2.pos.y == doctest::Approx(on).epsilon(1e-6));
}

// Stage 182 (the user): the player's feel -- platformer practice, restrictive
TEST_CASE("feel: a full held jump keeps the old reach (85 up, 167 across in 50 ticks); a tap hops low") {
	const std::vector<k::Collider> floor{wall(-5000, 1000, 5000, 1000)};
	const float stand = float(1000 - k::BODY - REST);
	auto jump = [&](int held, float run) { // up held this many ticks (the press on the first); the height, the run, the ticks aloft
		k::Body b{V2{0, stand}, V2{run * float(k::SPEED), 0}, true};
		float top = b.pos.y;
		int ticks = 0;
		for (; ticks < 200; ticks++) {
			k::step(b, k::Input{run, ticks < held ? -1.0f : 0.0f, ticks == 0}, G, floor);
			top = std::min(top, b.pos.y);
			if (ticks > 0 && b.on_floor) {
				break;
			}
		}
		return std::tuple{double(stand - top), double(b.pos.x), ticks};
	};
	const auto [h, x, t] = jump(60, 1.0f);
	CHECK(h >= 85.0);
	CHECK(h < 86.0);
	CHECK(x >= 166.6);
	CHECK(x < 172.0);
	CHECK(t == doctest::Approx(50).epsilon(0.05));
	const auto [hop, hx, ht] = jump(1, 0.0f);
	CHECK(hop < 30.0); // released at once: the jump cut
	CHECK(hop > 10.0);
	CHECK(hx == 0.0);
	CHECK(ht < t);
}

TEST_CASE("feel: coyote time -- a jump just after walking off a ledge; not later, and not twice") {
	// a ledge ending at x = 0 (the floor far below)
	const std::vector<k::Collider> cs{wall(-1000, 1000, 0, 1000), wall(-1000, 2000, 1000, 2000)};
	auto off_then_jump = [&](int late) { // walk right off the ledge; press up `late` ticks after leaving it
		k::Body b{V2{-40, float(1000 - k::BODY - REST)}, V2{float(k::SPEED), 0}, true};
		int left = -1;
		for (int i = 0; i < 400 && left < 0; i++) {
			k::step(b, k::Input{1, 0, false}, G, cs);
			left = b.on_floor ? -1 : i;
		}
		REQUIRE(left >= 0);
		for (int i = 1; i < late; i++) {
			k::step(b, k::Input{1, 0, false}, G, cs);
		}
		k::step(b, k::Input{1, -1, true}, G, cs);
		return b;
	};
	CHECK(off_then_jump(1).vel.y < 0.0f);
	CHECK(off_then_jump(k::COYOTE_TICKS).vel.y < 0.0f);
	CHECK(off_then_jump(k::COYOTE_TICKS + 1).vel.y > 0.0f);
	// a jump spends it: a second press in the air does nothing
	k::Body b{V2{0, float(1000 - k::BODY - REST)}, V2{}, true};
	k::step(b, k::Input{0, -1, true}, G, cs);
	k::step(b, k::Input{0, 0, false}, G, cs);
	const float rising = b.vel.y;
	k::step(b, k::Input{0, -1, true}, G, cs);
	CHECK(b.vel.y > rising);
}

TEST_CASE("feel: a jump pressed just before landing is taken on landing; one pressed long before is not") {
	const std::vector<k::Collider> floor{wall(-1000, 1000, 1000, 1000)};
	auto land_after_press = [&](int early) { // falling; up pressed `early` ticks before the landing tick, then held
		k::Body probe{V2{0, 800}, V2{}, false};
		int lands = 0;
		for (; !probe.on_floor; lands++) {
			k::step(probe, k::Input{}, G, floor);
		}
		k::Body b{V2{0, 800}, V2{}, false};
		bool jumped = false;
		for (int i = 0; i < lands + 3; i++) {
			const bool press = i == lands - early;
			k::step(b, k::Input{0, i >= lands - early ? -1.0f : 0.0f, press}, G, floor);
			jumped = jumped || (i >= lands && b.vel.y < 0.0f);
		}
		return jumped;
	};
	CHECK(land_after_press(1));
	CHECK(land_after_press(k::JUMP_BUFFER_TICKS - 1));
	CHECK_FALSE(land_after_press(k::JUMP_BUFFER_TICKS + 3));
}

TEST_CASE("feel: it accelerates to its speed (slowly in the air) and slows to a stop; it falls no faster than MAX_FALL") {
	const std::vector<k::Collider> floor{wall(-5000, 1000, 5000, 1000)};
	k::Body b{V2{0, float(1000 - k::BODY - REST)}, V2{}, true};
	k::step(b, k::Input{1, 0, false}, G, floor);
	CHECK(b.vel.x == doctest::Approx(k::GROUND_ACCEL * k::STEP));
	for (int i = 0; i < 10; i++) {
		k::step(b, k::Input{1, 0, false}, G, floor);
	}
	CHECK(b.vel.x == float(k::SPEED));
	for (int i = 0; i < 4; i++) {
		k::step(b, k::Input{}, G, floor);
	}
	CHECK(b.vel.x > 0.0f);
	for (int i = 0; i < 4; i++) {
		k::step(b, k::Input{}, G, floor);
	}
	CHECK(b.vel.x == 0.0f);
	// in the air: slower to speed up
	k::Body a{V2{0, 0}, V2{}, false};
	k::step(a, k::Input{1, 0, false}, G, floor);
	CHECK(a.vel.x == doctest::Approx(k::AIR_ACCEL * k::STEP));
	// a long fall: capped
	k::Body f{V2{0, -20000}, V2{}, false};
	for (int i = 0; i < 300; i++) {
		k::step(f, k::Input{}, G, floor);
	}
	CHECK(f.vel.y == float(k::MAX_FALL));
	// an object keeps the plain motion (its speed at once, gravity as it is)
	k::Body o{V2{0, 0}, V2{}, false};
	step1(o, k::Input{1, 0, false}, G, floor);
	CHECK(o.vel.x == float(k::SPEED));
	CHECK(o.vel.y == doctest::Approx(980.0 / 60.0));
}

TEST_CASE("feel: the air's control -- held against the motion it stops in 5 ticks and reverses in 15; released it stops in 10 (Stage 184)") {
	const std::vector<k::Collider> floor{wall(-5000, 1000, 5000, 1000)};
	auto air = [&](float side, int n) { // high in the air at full speed right, `side` held for n ticks: the horizontal speed
		k::Body b{V2{0, -20000}, V2{float(k::SPEED), 0}, false};
		for (int i = 0; i < n; i++) {
			k::step(b, k::Input{side, 0, false}, G, floor);
		}
		return b.vel.x;
	};
	CHECK(air(-1, 1) == doctest::Approx(k::SPEED - k::AIR_TURN * k::STEP));
	CHECK(air(-1, 4) > 0.0f);
	CHECK(air(-1, 5) == 0.0f);
	CHECK(air(-1, 14) > -float(k::SPEED));
	CHECK(air(-1, 15) == -float(k::SPEED));
	CHECK(air(0, 9) > 0.0f);
	CHECK(air(0, 10) == 0.0f);
	CHECK(air(1, 10) == float(k::SPEED)); // (held with the motion: kept)
}

TEST_CASE("engaging kills the momentum at once: running into a rope, up held, it goes straight up (Stage 185)") {
	// a vertical rope at x = 500 (a steep platform), the floor at 1000; the player touching it, running right at full speed
	const std::vector<k::Collider> cs{wall(-5000, 1000, 5000, 1000), platform(500, 700, 500, 1000)};
	const float stand = float(1000 - k::BODY - REST);
	k::Body b{V2{490, stand}, V2{float(k::SPEED), 0}, true};
	k::step(b, k::Input{0, -1, false}, G, cs);
	CHECK(b.vel.x == 0.0f);
	CHECK(b.vel.y == -float(k::SPEED));
}
