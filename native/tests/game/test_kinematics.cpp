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
	// a ceiling 70 above the floor cuts a jump (81.6 high) short at the head
	const std::vector<k::Collider> low{wall(0, 1000, 1000, 1000), wall(0, 930, 1000, 930)};
	k::Body j{V2{500, float(stand)}, V2{}, true};
	k::step(j, k::Input{0, 0, true}, G, low);
	float top = j.pos.y;
	for (int i = 0; i < 60; i++) {
		k::step(j, k::Input{}, G, low);
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
	k::step(b, k::Input{0, 0, true}, G, cs);
	for (int i = 0; i < 120; i++) {
		k::step(b, k::Input{}, G, cs);
	}
	CHECK(b.on_floor);
	CHECK(b.pos.y == doctest::Approx(on).epsilon(1e-6)); // its head passed first, never lifted
	// a short drop (the feet still above): back on top; a long one: through to the floor
	k::Body d = b;
	for (int i = 0; i < 4; i++) {
		k::step(d, k::Input{0.0f, 1.0f, false}, G, cs);
	}
	for (int i = 0; i < 60; i++) {
		k::step(d, k::Input{}, G, cs);
	}
	CHECK(d.pos.y == doctest::Approx(on).epsilon(1e-6));
	for (int i = 0; i < 60; i++) {
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
