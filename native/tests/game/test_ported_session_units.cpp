// Stage 119.3: retired GUT tests (session units), natively. Each TEST_CASE is
// one GUT test function (docs/research/gut_ledger_session_units.tsv): the same
// inputs and the same expectations, on rc::Tracer, the rcg:: model and
// rcg::Session. A GUT test that built its own surfaces builds them here as an
// inline rc-level/1 level; a ClickDetector call is a click on a session
// without the ∞ switch (the detector alone: no pole, no edge band).
#include "rc/scene.hpp"
#include "rc/sweep.hpp"
#include "rc/tracer.hpp"
#include "rcg/build.hpp"
#include "rcg/geom.hpp"
#include "rcg/landing.hpp"
#include "rcg/level.hpp"
#include "rcg/level2.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"
#include "rcg/sweeps.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;
using rc::V2;
namespace fs = std::filesystem;

const fs::path LEVELS = fs::path(RC_REPO_ROOT) / "data" / "levels";
constexpr double PI = 3.141592653589793;
constexpr double FRAME = 1.0 / 60.0;

V2 v(double x, double y) { return V2::of(x, y); }

json read_json(const fs::path &p) {
	std::ifstream in(p);
	REQUIRE_MESSAGE(in.good(), p.string());
	return json::parse(in);
}

rcg::Level level_file(const std::string &name) { return rcg::load_level((LEVELS / (name + ".json")).string()); }

// ---------------------------------------------------------------- inline levels

json side(const std::string &type, bool interactive, json hits = json::array(), json effect_extra = json::object()) {
	json e = {{"type", type}};
	e.update(effect_extra);
	return {{"effect", e}, {"interactive", interactive}, {"hits", hits}};
}

json arc_json(V2 s, V2 e, V2 via, const json &left, const json &right) {
	return {{"start", {s.x, s.y}}, {"end", {e.x, e.y}}, {"via", {via.x, via.y}}, {"left", left}, {"right", right}};
}

// SurfaceSpec.line / Segment.from_coords(a, b, (a + b) / 2)
json line_json(V2 a, V2 b, const json &left, const json &right) { return arc_json(a, b, (a + b) / 2.0f, left, right); }

json circle_json(V2 c, double r, const json &left, const json &right) {
	return {{"circle", {{"center", {c.x, c.y}}, {"radius", r}}}, {"left", left}, {"right", right}};
}

// a level of these surfaces alone (ids in order; no room, no edge walls)
rcg::Level level_of(const std::vector<json> &surfaces) {
	json ss = json::array();
	int id = 1;
	for (json s : surfaces) {
		s["ids"] = json::array({id++});
		s["label"] = "Surface";
		ss.push_back(s);
	}
	const json lv = {{"schema", "rc-level/1"}, {"room", {{"rect", {560, 240, 800, 600}}, {"built", false}}}, {"surfaces", ss}};
	return rcg::parse_level(lv.dump());
}

// test_levels/combo_base with surface specs added (scene.surface_specs.append:
// built after the level's own, before the edge walls; ids renumbered in order)
rcg::Level combo_with(const std::vector<json> &extra) {
	json lv = rcg::lower_level(read_json(LEVELS / "test_levels" / "combo_base.json")).level; // its rc-level/1 surfaces
	json out = json::array();
	int next = 1;
	bool added = false;
	for (json s : lv["surfaces"]) {
		if (s["label"].is_null() && !added) {
			for (json e : extra) {
				e["ids"] = json::array({next++});
				e["label"] = "Surface";
				out.push_back(e);
			}
			added = true;
		}
		s["ids"] = json::array({next++});
		out.push_back(s);
	}
	lv["surfaces"] = out;
	return rcg::parse_level(lv.dump());
}

// a spawn template (SpawnTemplate.make: no mirror, no offset)
json spawn_template(const json &surface, const std::string &orientation) {
	return {{"surface", surface}, {"orientation", orientation}, {"mirror_with_side", false}, {"offset", {0, 0}}};
}

// ClickDetector.detect_click(p, surfaces): a left click on a session of these surfaces alone
std::vector<rcg::PlanEntry> click_on(const std::vector<json> &surfaces, V2 p) {
	rcg::Session s(level_of(surfaces), rcg::SessionConfig{});
	s.set_cursor(p);
	s.click(false);
	return s.plan();
}

std::vector<int> hit_ids(const rc::TracePath &path) {
	std::vector<int> out;
	for (const rc::HitRecord &h : path.hits) {
		out.push_back(h.surface_id);
	}
	return out;
}

bool has(const std::vector<int> &ids, int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// the shot's physical trace as the game manager fires it (a fresh tracer on the world), then the shot
rc::TracePath shoot(rcg::Session &s, V2 player, V2 cursor) {
	rc::Tracer t;
	t.set_defs(s.world().defs());
	const std::optional<rc::TracePair> pair = t.trace_pair(player, cursor, {}, s.flags());
	REQUIRE(pair.has_value());
	s.teleport(player);
	s.set_cursor(cursor);
	s.fire(-1, FRAME);
	return pair->physical;
}

// the world's surface standing at x (a vertical line), else nullptr
const rcg::BuiltSurface *at_x(const rcg::Session &s, float x) {
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		if (!b.seg.full && b.seg.s.x == x && b.seg.e.x == x) {
			return &b;
		}
	}
	return nullptr;
}

const rc::FlagValue *flag(const rcg::Session &s, const std::string &key) { return s.flags().get(key); }

std::string flag_str(const rcg::Session &s, const std::string &key) {
	const rc::FlagValue *f = flag(s, key);
	const std::string *str = f ? std::get_if<std::string>(f) : nullptr;
	return str ? *str : "<none>";
}

std::vector<rc::SurfaceDef> defs_of_level(const rcg::Level &lv) { return rcg::defs_of(rcg::build_level(lv)); }

rc::PlanItem item(int id, bool left) { return rc::PlanItem{id, left, std::nullopt, std::nullopt}; }

rcg::SegGeom seg_line(V2 a, V2 b) { return rc::arc::SegGeom::make(a, b, (a + b) / 2.0f); }
rcg::SegGeom seg_circle(V2 c, double r) { return rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(c, r)); }

std::vector<V2> points(const std::vector<rcg::Crossing> &xs) {
	std::vector<V2> out;
	for (const rcg::Crossing &x : xs) {
		out.push_back(x.point);
	}
	return out;
}

// a drawn outline's geometry: a line's ends, or a circle's centre, radius and first angle
std::string hexf(double x) {
	char buf[64];
	std::snprintf(buf, sizeof buf, "%a", x);
	return buf;
}
std::string line_key(V2 a, V2 b) { return "line " + hexf(a.x) + " " + hexf(a.y) + " " + hexf(b.x) + " " + hexf(b.y); }
std::string arc_key(V2 c, double r, double a0) { return "arc " + hexf(c.x) + " " + hexf(c.y) + " " + hexf(r) + " " + hexf(a0); }

// how the spawn preview outlines a segment (dashed: a line, else arcs from the start angle)
std::string outline_key(const rcg::SegGeom &seg) {
	if (seg.is_line()) {
		return line_key(seg.s, seg.e);
	}
	const rc::arc::GeneralizedCircle &g = seg.get_carrier();
	const V2 c = rc::arc::center_of(g);
	if (g.radius() > rc::arc::MAX_ARC_RADIUS && !seg.full) {
		return line_key(seg.s, seg.e);
	}
	return arc_key(c, g.radius(), seg.full ? 0.0 : double((seg.s - c).angle()));
}

// the spawn node's outlines in draw order (consecutive dashes of one circle and colour: one outline)
std::vector<std::string> spawn_outlines(const std::vector<rcg::render::Node> &nodes) {
	std::vector<std::string> out;
	for (const rcg::render::Node &n : nodes) {
		if (n.key != json::array({"spawn"})) {
			continue;
		}
		std::string last;
		std::array<float, 4> last_color{};
		for (const rcg::render::Call &k : n.calls) {
			std::string key;
			if (k.kind == rcg::render::Kind::DashedLine) {
				key = line_key(k.pts[0], k.pts[1]);
				out.push_back(key);
				last.clear();
				continue;
			}
			REQUIRE(k.kind == rcg::render::Kind::Arc);
			const std::string circle = hexf(k.pts[0].x) + " " + hexf(k.pts[0].y) + " " + hexf(k.radius);
			if (!last.empty() && circle == last && k.colors[0] == last_color) {
				continue;
			}
			out.push_back(arc_key(k.pts[0], k.radius, k.start));
			last = circle;
			last_color = k.colors[0];
		}
	}
	return out;
}

std::vector<int> world_ids(const rcg::Session &s) {
	std::vector<int> out;
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		out.push_back(b.id);
	}
	return out;
}

} // namespace

// ================================================================ test_stage101_infinity_point.gd

TEST_CASE("ported: test_stage101_infinity_point: test_pre_transform_surface_is_not_a_surface_to_the_solver") {
	const rcg::Level lv = level_file("playground/pre_inverted");
	const std::vector<rcg::BuiltSurface> built = rcg::build_level(lv);
	const rcg::BuiltSurface *pre = nullptr;
	for (const rcg::BuiltSurface &s : built) {
		if (s.pre_transform) {
			pre = &s;
		}
	}
	REQUIRE_MESSAGE(pre != nullptr, "the scene has a pre-transform circle");
	const rcg::BuiltSide *left = rcg::active_side(*pre, rcg::Side::Left, rc::Flags{});
	CHECK_FALSE((left != nullptr && left->interactive));
	const std::vector<rc::SurfaceDef> defs = rcg::defs_of(built);
	CHECK(std::count_if(defs.begin(), defs.end(), [](const rc::SurfaceDef &d) { return d.pre_transform; }) == 1);
	const rc::Scene<rc::F64> scene = rc::Scene<rc::F64>::from_defs(defs, rc::F64::of(1.0));
	CHECK_MESSAGE(scene.find(pre->id) == nullptr, "not among the solver's surfaces");
	CHECK(rc::scene_supported(defs));
	// InvariantChecker.surface_infos: the sweep manifest's infos of the level
	const json recipes = {{"groups", json::array({{{"name", "pre"}, {"levels", {"playground/pre_inverted"}}, {"plans", "empty"},
		{"positions", {{"rule", "bounded"}, {"grid", 1}, {"fuzz", 0}, {"seed", 1}}}}})}};
	const auto ms = rcg::sweeps::manifests(recipes, LEVELS.string());
	REQUIRE(ms.size() == 1);
	REQUIRE(!ms[0].second["infos"].empty());
	for (const json &info : ms[0].second["infos"]) {
		CHECK_MESSAGE(info["id"].get<int>() != pre->id, "not a surface the invariant kernel checks");
	}
	// not clickable as a surface (the detector alone)
	rcg::Session s(lv, rcg::SessionConfig{});
	s.set_cursor(v(960 + 260, 540));
	s.click(false);
	CHECK(s.plan().empty());
}

TEST_CASE("ported: test_stage101_infinity_point: test_infinity_point_follows_the_frame") {
	const V2 Q0 = v(960, 540);
	rc::Tracer t;
	const std::vector<rcg::BuiltSurface> built = rcg::build_level(level_file("playground/pre_inverted"));
	t.set_defs(rcg::defs_of(built));
	CHECK_MESSAGE(t.infinity_point({}, {}) == std::optional<V2>(Q0), "the pre-transform's image of infinity");
	int mirror = 0;
	for (const rcg::BuiltSurface &s : built) {
		if (!s.pre_transform && s.seg.is_line() && s.seg.s.x == 500.0f && s.seg.e.x == 500.0f) {
			mirror = s.id;
		}
	}
	REQUIRE(mirror != 0);
	CHECK_MESSAGE(t.infinity_point({item(mirror, true)}, {}) == std::optional<V2>(v(40, 540)), "reflected in x = 500");
	CHECK_MESSAGE(t.infinity_point({item(mirror, true), item(mirror, true)}, {}) == std::optional<V2>(Q0), "the same mirror twice pops");
	CHECK_MESSAGE(t.infinity_point({item(rc::INFINITY_ID, true)}, {}) == std::optional<V2>(Q0), "an ∞ entry does not move it");
	// the identity frame: infinity (none); after an inversion: the circle's centre
	const std::vector<rcg::BuiltSurface> main = rcg::build_level(level_file("main"));
	t.set_defs(rcg::defs_of(main));
	CHECK_MESSAGE(!t.infinity_point({}, {}).has_value(), "no pre-transform: infinity");
	const rcg::BuiltSurface *inv = nullptr;
	for (const rcg::BuiltSurface &s : main) {
		const rcg::BuiltSide *c = rcg::active_side(s, rcg::Side::Left, rc::Flags{});
		if (!s.seg.is_line() && c && c->effect.type == "inversion") {
			inv = &s;
		}
	}
	REQUIRE(inv != nullptr);
	CHECK_MESSAGE(t.infinity_point({item(inv->id, true)}, {}) == std::optional<V2>(rc::arc::center_of(inv->seg.get_carrier())),
		"after an inversion: its center");
}

// ================================================================ test_stage107_spawn_frame.gd

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_the_four_required_modes_and_the_extras") {
	using M = rcg::SpawnMode;
	const V2 beam = v(1, 0), tangent = v(0, 1);
	CHECK(rcg::spawn_axis(M::BeamTangent, beam, tangent) == v(1, 0));
	CHECK(rcg::spawn_axis(M::BeamNormal, beam, tangent) == v(0, 1));
	CHECK(rcg::spawn_axis(M::CarrierTangent, beam, tangent) == v(0, 1));
	CHECK(rcg::spawn_axis(M::CarrierNormal, beam, tangent) == v(-1, 0));
	CHECK(rcg::spawn_axis(M::CarrierNormal, -beam, tangent) == v(1, 0));
	CHECK(rcg::spawn_axis(M::BeamReflected, beam, tangent) == v(-1, 0));
	CHECK(rcg::spawn_axis(M::BeamReflected, v(0.75, 0.5), v(1, 0)) == v(0.75, -0.5));
	CHECK(rcg::spawn_axis(M::World, beam, tangent) == v(1, 0));
}

// ================================================================ test_stage107_spawn_preview.gd

// The spawn preview is the landing: over a grid of aims, the outlines the
// preview draws are the surfaces the shot creates (draw order: by channel,
// as the GUT's a, b), the vanishing ones those that leave; settling builds
// nothing, writes no flag.
TEST_CASE("ported: test_stage107_spawn_preview: test_stage107_preview_equals_landing") {
	const json mirror = spawn_template(line_json(v(-40, 120), v(40, 120), side("reflection", true), side("reflection", true)), "CARRIER_NORMAL");
	const json block = spawn_template(circle_json(v(150, 0), 30.0, side("terminal", false), side("terminal", false)), "BEAM_REFLECTED");
	const json h1 = side("pass", true, json::array({{{"kind", "spawn"}, {"channel", "a"}, {"template", mirror}}}));
	const json h2 = side("reflection", true, json::array({{{"kind", "spawn"}, {"channel", "b"}, {"template", block}}}));
	const json r = side("pass", true, json::array({{{"kind", "reset"}, {"channels", {"a", "b"}}}}));
	const rcg::Level lv = combo_with({line_json(v(900, 250), v(900, 750), h1, h1), arc_json(v(1300, 300), v(1300, 700), v(1450, 500), h2, h2),
		line_json(v(600, 150), v(1100, 150), r, r)});
	rcg::Session s(lv);
	s.settle();
	int compared = 0, spawned = 0;
	for (double px : {400.0, 650.0}) {
		for (double py : {300.0, 500.0, 700.0}) {
			for (V2 dir : {v(1, 0), v(1, 0.25), v(1, -0.375), v(-0.5, -1), v(1, 0.75)}) {
				for (int shot = 0; shot < 2; shot++) {
					const V2 player = v(px, py);
					const V2 cursor = player + dir * 64.0f;
					CAPTURE(px);
					CAPTURE(py);
					CAPTURE(dir.x);
					CAPTURE(dir.y);
					CAPTURE(shot);
					s.teleport(player);
					s.set_cursor(cursor);
					const int ids_before = s.world().next_id();
					const std::vector<int> world_before = world_ids(s);
					const rc::Flags flags_before = s.flags();
					s.settle();
					CHECK_MESSAGE(s.world().next_id() == ids_before, "the preview builds no surface");
					CHECK_MESSAGE(world_ids(s) == world_before, "the preview leaves the world alone");
					CHECK_MESSAGE(s.flags() == flags_before, "and the flags");
					const json n = s.record(json::object())["spawn_n"];
					const std::vector<std::string> drawn = spawn_outlines(s.draw());
					const size_t placed = n[0].get<size_t>();
					REQUIRE(drawn.size() == placed + n[1].get<size_t>());
					const std::vector<std::string> want(drawn.begin(), drawn.begin() + long(placed));
					const std::vector<std::string> vanish(drawn.begin() + long(placed), drawn.end());
					std::vector<const rcg::BuiltSurface *> present_before;
					for (const rcg::BuiltSurface &b : s.world().surfaces()) {
						if (rcg::present(b, s.flags())) {
							present_before.push_back(&b);
						}
					}
					std::vector<rcg::BuiltSurface> keep_before;
					for (const rcg::BuiltSurface *b : present_before) {
						keep_before.push_back(*b);
					}
					s.fire(-1, FRAME);
					std::vector<std::string> got;
					for (const char *ch : {"a", "b"}) {
						if (const rcg::World::Group *g = s.world().group_of(ch)) {
							for (const rcg::BuiltSurface &b : g->surfaces) {
								if (!has(world_before, b.id)) {
									got.push_back(outline_key(b.seg));
								}
							}
						}
					}
					CHECK_MESSAGE(got == want, "the preview is the landing");
					std::vector<std::string> left;
					for (const rcg::BuiltSurface &b : keep_before) {
						const rcg::BuiltSurface *now = s.world().by_id(b.id);
						if (!b.presence.empty() && !(now != nullptr && rcg::present(*now, s.flags()))) {
							left.push_back(outline_key(b.seg));
						}
					}
					CHECK_MESSAGE(left == vanish, "the marked surfaces are the ones that leave");
					compared++;
					spawned += int(want.size());
				}
				s.key(rcg::Action::Undo);
				s.key(rcg::Action::Undo);
			}
		}
	}
	CHECK_MESSAGE(spawned > 5, "the grid spawns (" << spawned << " of " << compared << " shots)");
}

// ================================================================ test_stage107_world.gd

namespace {

// the host H at x = 900 (y 300..780) spawning a block 400 along the beam (channel c)
json host_spec() {
	const json block = spawn_template(line_json(v(400, -60), v(400, 60), side("terminal", false), side("terminal", false)), "BEAM_TANGENT");
	const json s = side("pass", true, json::array({{{"kind", "spawn"}, {"channel", "c"}, {"template", block}}}));
	return line_json(v(900, 300), v(900, 780), s, s);
}

json reset_spec() {
	const json s = side("pass", true, json::array({{{"kind", "reset"}, {"channels", {"c"}}}}));
	return line_json(v(1100, 150), v(1100, 420), s, s);
}

} // namespace

TEST_CASE("ported: test_stage107_world: test_stage107_the_spawning_shot_ignores_its_spawn_the_next_does_not") {
	rcg::Session s(combo_with({host_spec()}));
	const rcg::BuiltSurface *host = at_x(s, 900);
	REQUIRE(host != nullptr);
	const int host_id = host->id;
	const rc::TracePath first = shoot(s, v(700, 540), v(800, 540));
	REQUIRE(!first.hits.empty());
	CHECK_MESSAGE(first.hits[0].surface_id == host_id, "the host fires");
	CHECK_MESSAGE(flag_str(s, "chan:c") == "g1", "generation 1 landed");
	const rcg::BuiltSurface *block = at_x(s, 1300);
	REQUIRE_MESSAGE(block != nullptr, "a block was placed 400 along the beam, across its line");
	const int block_id = block->id;
	CHECK_MESSAGE(!has(hit_ids(first), block_id), "the shot that spawned it did not meet it");
	CHECK_MESSAGE(!rcg::present(*s.world().by_id(host_id), s.flags()), "the host is spent");
	const rc::TracePath second = shoot(s, v(700, 540), v(800, 540));
	CHECK_MESSAGE(hit_ids(second) == std::vector<int>{block_id}, "the next shot passes the spent host and stops at the block");
}

TEST_CASE("ported: test_stage107_world: test_stage107_a_reset_acts_in_the_shot") {
	rcg::Session s(combo_with({host_spec(), reset_spec()}));
	shoot(s, v(700, 540), v(800, 540));
	const rcg::BuiltSurface *block = at_x(s, 1300);
	const rcg::BuiltSurface *reset = at_x(s, 1100);
	REQUIRE(block != nullptr);
	REQUIRE(reset != nullptr);
	const int block_id = block->id, reset_id = reset->id;
	// through the reset at x = 1100 (y ~ 347), then where the block stands (1300, 540)
	const rc::TracePath shot = shoot(s, v(1000, 250), v(1150, 395));
	REQUIRE(!shot.hits.empty());
	CHECK_MESSAGE(shot.hits[0].surface_id == reset_id, "the reset fires");
	CHECK_MESSAGE(!has(hit_ids(shot), block_id), "the block vanished in the same shot");
	CHECK(flag_str(s, "chan:c") == "armed");
	const rcg::World::Group *g = s.world().group_of("c");
	CHECK_MESSAGE((g == nullptr || g->surfaces.empty()), "the group left the world");
	CHECK(at_x(s, 1300) == nullptr);
	const rcg::BuiltSurface *host = at_x(s, 900);
	REQUIRE(host != nullptr);
	CHECK_MESSAGE(rcg::present(*host, s.flags()), "the host is back");
}

TEST_CASE("ported: test_stage107_world: test_stage107_targets_register_by_flag") {
	const json t = side("pass", false, json::array({{{"kind", "target"}}}));
	rcg::Session s(combo_with({line_json(v(900, 300), v(900, 780), t, t)}));
	const rcg::BuiltSurface *target = at_x(s, 900);
	REQUIRE(target != nullptr);
	const int target_id = target->id;
	const rc::TracePath shot = shoot(s, v(700, 540), v(800, 540));
	s.settle();
	CHECK(s.record(json::object())["targets"] == json::array({target_id}));
	CHECK_MESSAGE(has(shot.targets_hit, target_id), "the same target the solver's on-segment rule counts");
	s.key(rcg::Action::Undo);
	s.settle();
	CHECK_MESSAGE(s.record(json::object())["targets"] == json::array(), "undone");
}

// ================================================================ test_stage108_crossings.gd

TEST_CASE("ported: test_stage108_crossings: test_stage108_two_lines_cross_once") {
	const rcg::SegGeom a = seg_line(v(500, -300), v(500, 300));
	const rcg::SegGeom b = seg_line(v(400, 200), v(600, 200));
	const std::vector<rcg::Crossing> ab = rcg::crossings_of(a, b);
	CHECK(points(ab) == std::vector<V2>{v(500, 200)});
	REQUIRE(ab.size() == 1);
	CHECK_MESSAGE(std::abs(ab[0].ta - 500.0 / 600.0) <= 1e-12, "5/6 along a");
	CHECK(std::abs(ab[0].tb - 0.5) <= 1e-12);
	const std::vector<rcg::Crossing> ba = rcg::crossings_of(b, a);
	CHECK_MESSAGE(points(ba) == std::vector<V2>{v(500, 200)}, "symmetric");
	REQUIRE(ba.size() == 1);
	CHECK(ba[0].ta == ab[0].tb);
}

TEST_CASE("ported: test_stage108_crossings: test_stage108_touching_is_not_crossing") {
	const rcg::SegGeom a = seg_line(v(0, 0), v(100, 0));
	CHECK_MESSAGE(rcg::crossings_of(a, seg_line(v(50, 0), v(50, 80))).empty(), "a T-junction (an endpoint on the other)");
	CHECK_MESSAGE(rcg::crossings_of(a, seg_line(v(100, 0), v(100, 80))).empty(), "a corner");
	CHECK_MESSAGE(rcg::crossings_of(a, seg_line(v(0, 10), v(100, 10))).empty(), "parallel");
	CHECK_MESSAGE(rcg::crossings_of(a, seg_line(v(50, 0), v(150, 0))).empty(), "collinear overlap");
	CHECK_MESSAGE(rcg::crossings_of(a, seg_line(v(200, -10), v(200, 10))).empty(), "carriers cross outside a");
	CHECK_MESSAGE(rcg::crossings_of(a, seg_circle(v(50, 30), 30.0)).empty(), "a tangent circle (a touch)");
}

TEST_CASE("ported: test_stage108_crossings: test_stage108_line_and_circle") {
	const rcg::SegGeom c = seg_circle(v(0, 0), 5.0);
	const rcg::SegGeom l = seg_line(v(3, -10), v(3, 10));
	const std::vector<rcg::Crossing> x = rcg::crossings_of(l, c);
	CHECK_MESSAGE((points(x) == std::vector<V2>{v(3, -4), v(3, 4)}), "ordered along the line");
	REQUIRE(x.size() == 2);
	CHECK(x[0].ta < x[1].ta);
	CHECK_MESSAGE(x[0].enters != x[1].enters, "A enters B at one crossing and exits at the other");
	const std::vector<rcg::Crossing> y = rcg::crossings_of(c, l);
	REQUIRE(y.size() == 2);
	CHECK(y[0].enters != y[1].enters);
	// a line ending exactly on the circle: only its other crossing
	CHECK(points(rcg::crossings_of(seg_line(v(3, -10), v(3, 4)), c)) == std::vector<V2>{v(3, -4)});
}

TEST_CASE("ported: test_stage108_crossings: test_stage108_arc_limits_its_crossings") {
	// the upper half (y < 0 on screen) of the circle r = 5 about the origin
	const rcg::SegGeom arc = rc::arc::SegGeom::make(v(5, 0), v(-5, 0), v(0, -5));
	const rcg::SegGeom l = seg_line(v(3, -10), v(3, 10));
	CHECK_MESSAGE(points(rcg::crossings_of(arc, l)) == std::vector<V2>{v(3, -4)}, "the lower crossing is off the arc");
}

TEST_CASE("ported: test_stage108_crossings: test_stage108_two_circles") {
	const rcg::SegGeom a = seg_circle(v(0, 0), 5.0);
	const rcg::SegGeom b = seg_circle(v(6, 0), 5.0);
	const std::vector<rcg::Crossing> x = rcg::crossings_of(a, b);
	REQUIRE(x.size() == 2);
	const std::vector<V2> pts = points(x);
	CHECK((std::count(pts.begin(), pts.end(), v(3, 4)) == 1 && std::count(pts.begin(), pts.end(), v(3, -4)) == 1));
	CHECK(x[0].enters != x[1].enters);
	CHECK_MESSAGE(rcg::crossings_of(a, seg_circle(v(10, 0), 5.0)).empty(), "externally tangent: a touch");
	CHECK_MESSAGE(rcg::crossings_of(a, seg_circle(v(0, 0), 3.0)).empty(), "concentric");
}

TEST_CASE("ported: test_stage108_crossings: test_stage108_param_along_segments") {
	const rcg::SegGeom l = seg_line(v(0, 0), v(10, 0));
	CHECK(rcg::param(l, v(2.5, 0)) == 0.25);
	const rcg::SegGeom arc = rc::arc::SegGeom::make(v(5, 0), v(-5, 0), v(0, -5));
	CHECK(std::abs(rcg::span(arc) - PI) <= 1e-12);
	CHECK(std::abs(rcg::param(arc, v(0, -5)) - PI / 2.0) <= 1e-12);
	CHECK(rcg::span(seg_circle(v(0, 0), 3.0)) == rcg::TAU);
}

// ================================================================ test_stage108_level.gd

TEST_CASE("ported: test_stage108_level: test_stage108_level_spawn_is_trimmed_by_the_floor") {
	rcg::Session s(level_file("test_levels/crossings"));
	s.teleport(v(300, 800));
	s.set_cursor(v(400, 800));
	s.fire(-1, FRAME);
	const rcg::World::Group *g = s.world().group_of("t");
	REQUIRE(g != nullptr);
	REQUIRE_MESSAGE(g->surfaces.size() == 1, "spawned");
	const rcg::SegGeom &seg = g->surfaces[0].seg;
	std::vector<float> ys{seg.s.y, seg.e.y};
	std::sort(ys.begin(), ys.end());
	CHECK_MESSAGE((ys == std::vector<float>{650.0f, 900.0f}),"300 px centred on (450, 800), trimmed at the floor (y = 900)");
	CHECK(seg.is_line());
}

// ================================================================ test_stage110_truncation.gd

namespace {

// two facing mirrors x = 0 and x = 100 (H.mirror: y 0..600, both sides reflecting, plannable)
std::vector<json> trapped_mirrors() {
	const json m = side("reflection", true);
	return {line_json(v(0, 0), v(0, 600), m, m), line_json(v(100, 0), v(100, 600), m, m)};
}

} // namespace

TEST_CASE("ported: test_stage110_truncation: test_a_path_cut_by_the_limit_is_truncated") {
	rc::Tracer t;
	t.set_defs(defs_of_level(level_of(trapped_mirrors())));
	const std::optional<rc::TracePair> pair = t.trace_pair(v(50, 300), v(90, 300), {}, {});
	REQUIRE(pair.has_value());
	CHECK_MESSAGE(pair->physical.truncated, "the physical path is cut by the limit");
	const rc::TracePath display = rc::sweep::prepare_for_display(pair->physical, rc::Rect2f::of(0, 0, 1920, 1080), {});
	CHECK_MESSAGE(display.truncated, "the display conversion keeps the flag");
}

// The preview's stop marker (PathRenderer.truncation_markers), as the session draws it:
// an octagon of radius 7 and a bar through its centre, in the colour of the step ending there.
TEST_CASE("ported: test_stage110_truncation: test_the_preview_marks_the_cut") {
	const rcg::Level lv = level_of(trapped_mirrors());
	rcg::Session s(lv);
	s.teleport(v(50, 300));
	s.set_cursor(v(90, 300));
	s.settle();
	// the path's last point (the display path of the same trace)
	rc::Tracer t;
	t.set_defs(defs_of_level(lv));
	const std::optional<rc::TracePair> pair = t.trace_pair(v(50, 300), v(90, 300), {}, {});
	REQUIRE(pair.has_value());
	const rc::TracePath traced = rc::sweep::prepare_for_display(pair->physical, rc::Rect2f::of(0, 0, 1920, 1080), {});
	REQUIRE(!traced.steps.empty());
	const V2 last = traced.steps.back().end;
	struct Marker {
		V2 point;
		std::array<float, 4> color;
	};
	std::vector<Marker> markers;
	for (const rcg::render::Node &n : s.draw()) {
		if (n.key != json::array({"path"})) {
			continue;
		}
		for (size_t i = 0; i + 1 < n.calls.size(); i++) {
			const rcg::render::Call &o = n.calls[i], &bar = n.calls[i + 1];
			if (o.kind == rcg::render::Kind::Polyline && o.pts.size() == 9 && bar.kind == rcg::render::Kind::Line && bar.pts.size() == 2) {
				const V2 centre = (bar.pts[0] + bar.pts[1]) / 2.0f;
				if (std::abs(double(o.pts[0].distance_to(centre)) - 7.0) < 1e-3) {
					markers.push_back({centre, o.colors[0]});
				}
			}
		}
	}
	REQUIRE_MESSAGE(markers.size() == 1, "one marker for the one (shared) trajectory");
	CHECK_MESSAGE(markers[0].point == last, "at the path's last point");
	const std::array<float, 4> aligned_post_planned{0, 1, 0, 1}; // StepTypes.color(ALIGNED_POST_PLANNED)
	CHECK_MESSAGE(markers[0].color == aligned_post_planned, "in the step's colour");
}

// ================================================================ test_stage113_projective_visibility.gd

// PlanManager.snap_click through the click: the entry's click point. The GUT's
// (600, 350) on arc2 and (600, 300) on the circle normal are 50 and 100 px off
// their arcs, beyond the click tolerance: no click reaches them (not ported).
TEST_CASE("ported: test_stage113_projective_visibility: test_b7_a_click_on_the_diameter_across_the_normal_is_moved_off_it") {
	auto clicked = [](const json &surface, V2 p) -> std::optional<V2> {
		const std::vector<rcg::PlanEntry> plan = click_on({surface}, p);
		if (plan.empty()) {
			return std::nullopt;
		}
		return plan.back().click;
	};
	// the right half of the circle centred (600, 300) r 100, n = (0, 1): the diameter across n is y = 300
	const json cfg = side("circle_directional_projection", true, json::array(), {{"normal", {0, 1}}});
	const json arc = arc_json(v(600, 200), v(600, 400), v(700, 300), cfg, cfg);
	CHECK_MESSAGE(clicked(arc, v(690, 330)) == std::optional<V2>(v(690, 330)), "off the diameter: unchanged");
	CHECK_MESSAGE(clicked(arc, v(690, 300)) == std::optional<V2>(v(690, 308)), "on it: moved to +n (the via is on it)");
	CHECK_MESSAGE(clicked(arc, v(690, 297)) == std::optional<V2>(v(690, 308)), "within the tolerance: the same");
	// a line normal keeps its click
	const json ln = side("line_normal_projection", true, json::array(), {{"back", false}});
	CHECK(clicked(line_json(v(600, 150), v(600, 450), ln, ln), v(600, 300)) == std::optional<V2>(v(600, 300)));
}

// ================================================================ test_stage22_plan.gd

namespace {

// _make_double_mirror(x): (x, 0)-(x, 600), reflecting and plannable on both sides
json double_mirror(double x) {
	const json m = side("reflection", true);
	return arc_json(v(x, 0), v(x, 600), v(x, 300), m, m);
}

// _make_arc_mirror((300, 300), 100, 0, PI): the lower semicircle (400, 300) -> (200, 300) via (300, 400)
json semicircle_mirror() {
	const V2 c = v(300, 300);
	const double r = 100.0, a0 = 0.0, a1 = PI, am = (a0 + a1) / 2.0;
	const V2 s = c + V2::of(std::cos(a0), std::sin(a0)) * float(r);
	const V2 e = c + V2::of(std::cos(a1), std::sin(a1)) * float(r);
	const V2 via = c + V2::of(std::cos(am), std::sin(am)) * float(r);
	const json m = side("reflection", true);
	return arc_json(s, e, via, m, m);
}

} // namespace

TEST_CASE("ported: test_stage22_plan: test_stage22_non_interactive_rejected") {
	const json p = side("pass", false);
	CHECK_MESSAGE(click_on({arc_json(v(400, 0), v(400, 600), v(400, 300), p, p)}, v(400, 300)).empty(), "Non-interactive surface should not be clickable");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_interactive_accepted") {
	CHECK_MESSAGE(click_on({double_mirror(400)}, v(398, 300)).size() == 1, "Interactive surface should be clickable within tolerance");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_click_outside_tolerance") {
	CHECK_MESSAGE(click_on({double_mirror(400)}, v(430, 300)).empty(), "Click 30px away should be outside tolerance");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_nearest_surface_wins") {
	const std::vector<rcg::PlanEntry> plan = click_on({double_mirror(400), double_mirror(405)}, v(403, 300));
	REQUIRE_MESSAGE(plan.size() == 1, "Should find a surface");
	CHECK_MESSAGE(plan[0].surface_id == 2, "Nearest surface should win"); // ids in order: x = 405 is the second
}

// Stage 177 (the user): a left click over nothing plannable fires, as the fire action does
TEST_CASE("plan: a left click over nothing plannable fires; over a plannable side it plans, a right click never fires") {
	const json w = side("terminal", false);
	const rcg::Level lv = level_of({double_mirror(400), arc_json(v(700, 0), v(700, 600), v(700, 300), w, w)});
	rcg::Session s(lv, rcg::SessionConfig{});
	s.teleport(v(200, 300));
	s.set_cursor(v(398, 300)); // the mirror: planned, nothing fired
	CHECK_FALSE(s.click(false));
	CHECK_FALSE(s.flying());
	REQUIRE(s.plan().size() == 1);
	s.set_cursor(v(300, 100)); // nothing plannable: fired with the plan
	CHECK(s.click(false));
	CHECK(s.flying());
	const rcg::Flight f = s.fly(-1, FRAME);
	CHECK(f.frames > 0);
	CHECK_FALSE(s.flying());
	CHECK(s.plan().size() == 1); // (the plan is kept after a shot)
	s.set_cursor(v(700, 300)); // a wall: no plannable side -- fires
	CHECK(s.click(false));
	s.fly(-1, FRAME);
	s.set_cursor(v(300, 100)); // a right click over nothing clears the plan, fires nothing
	CHECK_FALSE(s.click(true));
	CHECK_FALSE(s.flying());
	CHECK(s.plan().empty());
	s.set_cursor(s.player()); // over nothing but at the player: no shot (as the fire action)
	CHECK_FALSE(s.click(false));
	CHECK_FALSE(s.flying());
}

TEST_CASE("ported: test_stage22_plan: test_stage22_wall_not_interactive") {
	// RoomBuilder.create_block_surface: terminal on both sides, not plannable
	const json w = side("terminal", false);
	CHECK_MESSAGE(click_on({arc_json(v(400, 0), v(400, 600), v(400, 300), w, w)}, v(400, 300)).empty(), "Terminal surfaces should not be interactive");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_arc_click_on_arc_accepted") {
	CHECK_MESSAGE(click_on({semicircle_mirror()}, v(300, 400)).size() == 1, "Click on arc curve should be detected");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_arc_click_near_arc_within_tolerance") {
	CHECK_MESSAGE(click_on({semicircle_mirror()}, v(300, 405)).size() == 1, "Click 5px from arc should be within tolerance");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_arc_click_on_chord_far_from_arc_rejected") {
	CHECK_MESSAGE(click_on({semicircle_mirror()}, v(300, 300)).empty(), "Click at chord midpoint (100px from arc) should be rejected");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_arc_click_outside_span_rejected") {
	CHECK_MESSAGE(click_on({semicircle_mirror()}, v(300, 200)).empty(), "Click at correct radius but outside arc span should be rejected");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_arc_click_near_endpoint_accepted") {
	CHECK_MESSAGE(click_on({semicircle_mirror()}, v(205, 300)).size() == 1, "Click 5px from arc endpoint should be detected");
}

TEST_CASE("ported: test_stage22_plan: test_stage22_full_circle_click_accepted") {
	const json m = side("reflection", true);
	CHECK_MESSAGE(click_on({circle_json(v(300, 300), 100.0, m, m)}, v(300, 200)).size() == 1, "Click on full circle should be detected");
}

// ================================================================ test_stage23_plan_removal.gd

TEST_CASE("ported: test_stage23_plan_removal: test_stage23_remove_only_instance") {
	rcg::Session s(level_of({double_mirror(400)}), rcg::SessionConfig{});
	s.set_cursor(v(398, 300));
	s.click(false);
	REQUIRE(s.plan().size() == 1);
	CHECK(s.plan()[0].surface_id == 1);
	s.click(true);
	CHECK_MESSAGE(s.plan().empty(), "Plan should be empty after removing only instance");
}

// ================================================================ test_stage55_chevron_overlays.gd

TEST_CASE("ported: test_stage55_chevron_overlays: test_hover_flag_mirror_follows_state") {
	const json refl = side("reflection", true);
	const json off = side("pass", false);
	json surf = {{"start", {400, 0}}, {"end", {400, 600}}, {"via", {400, 300}},
		{"resolver", {{"key", "open"}, {"table", json::array({json::array({"true", {{"left", refl}, {"right", refl}}}),
												 json::array({"DEFAULT", {{"left", off}, {"right", off}}})})}}}};
	const std::vector<rcg::BuiltSurface> built = rcg::build_level(level_of({surf}));
	REQUIRE(built.size() == 1);
	const rcg::BuiltSurface &s = built[0];
	CHECK_MESSAGE(!rcg::overlay_link(s, rcg::Side::Left, rc::Flags{}).has_value(), "closed: not interactive, no highlight");
	rc::Flags open;
	open.set("open", true);
	const std::optional<rcg::OverlayLink> link = rcg::overlay_link(s, rcg::Side::Left, open);
	REQUIRE(link.has_value());
	REQUIRE(link->outgoing.has_value());
	CHECK_MESSAGE(*link->outgoing == (rcg::SideRef{s.id, rcg::Side::Left}), "open: a mirror, same side");
}

// ================================================================ test_stage82_portals.gd

TEST_CASE("ported: test_stage82_portals: test_after_portal_gap_flag") {
	const rcg::Level lv = level_file("test_levels/portals");
	const std::vector<rcg::BuiltSurface> built = rcg::build_level(lv);
	rc::Tracer t;
	t.set_defs(rcg::defs_of(built));
	const std::optional<rc::TracePair> pair = t.trace_pair(v(760, 500), v(1250, 560), {}, {});
	REQUIRE(pair.has_value());
	// PathRenderer.get_traced_path: the display path (the level's bounds and surfaces)
	std::vector<rc::inv::SurfaceInfo> infos;
	for (const rcg::BuiltSurface &s : built) {
		rc::inv::SurfaceInfo i;
		i.id = s.id;
		i.s = s.seg.s;
		i.e = s.seg.e;
		i.v = s.seg.v;
		i.full = s.seg.full;
		i.carrier = s.seg.get_carrier();
		infos.push_back(i);
	}
	const rc::TracePath path = rc::sweep::prepare_for_display(pair->physical, rc::Rect2f::of(lv.bounds.x, lv.bounds.y, lv.bounds.w, lv.bounds.h), infos);
	bool found = false;
	for (size_t i = 1; i < path.steps.size(); i++) {
		const rc::TraceStep &s = path.steps[i];
		if (s.after_portal) {
			found = true;
			// the step starts on the paired surface (x = 1130), the previous ended on the source (x = 830)
			CHECK_MESSAGE(std::abs(double(s.start.x) - 1130.0) <= 1e-3, "exit on the pair");
			CHECK_MESSAGE(std::abs(double(path.steps[i - 1].end.x) - 830.0) <= 1e-3, "entry on the source");
			break;
		}
	}
	CHECK_MESSAGE(found, "a step after the portal carries after_portal");
}

// ================================================================ test_stage86_ghost.gd

TEST_CASE("ported: test_stage86_ghost: test_click_near_refused_side_takes_toggler") {
	const json tog = side("pass", true, json::array({{{"kind", "state"}, {"key", "ghost"}, {"op", "toggle"}}}));
	const json mir = side("reflection", true);
	rcg::Session s(level_of({line_json(v(0, -100), v(0, 100), tog, tog), line_json(v(10, -100), v(10, 100), mir, mir),
					   line_json(v(-200, -100), v(-200, 100), tog, tog)}),
		rcg::SessionConfig{});
	// the plan [s3 RIGHT]: a click on the x = -200 toggler's right side
	for (double x : {-198.0, -202.0}) {
		s.set_cursor(v(x, 0));
		s.click(false);
		if (!s.plan().empty() && s.plan().back().side == rcg::Side::Right) {
			break;
		}
		s.key(rcg::Action::ClearPlan);
	}
	REQUIRE(s.plan().size() == 1);
	REQUIRE(s.plan()[0].surface_id == 3);
	REQUIRE(s.plan()[0].side == rcg::Side::Right);
	s.set_cursor(v(8, 0));
	s.click(false);
	REQUIRE(s.plan().size() == 2);
	CHECK_MESSAGE(s.plan()[1].surface_id == 1, "the mirror is nearer but refused");
}

TEST_CASE("ported: test_stage86_ghost: test_ghost_is_per_shot") {
	const rcg::Level lv = level_file("test_levels/ghost_room");
	rc::Tracer t;
	t.set_defs(defs_of_level(lv));
	rc::Flags ghost;
	ghost.set("ghost", true);
	const std::optional<rc::TracePair> p = t.trace_pair(v(900, 540), v(1300, 540), {}, ghost);
	const std::optional<rc::TracePair> q = t.trace_pair(v(900, 540), v(1300, 540), {}, {});
	REQUIRE(p.has_value());
	REQUIRE(q.has_value());
	CHECK_MESSAGE(p->result.physical.size() == q->result.physical.size(), "a trace never starts ghosted");
	rcg::Session s(lv);
	s.teleport(v(900, 540));
	s.set_cursor(v(1300, 540));
	s.fire(-1, FRAME);
	CHECK_MESSAGE(!s.flags().has("ghost"), "ghost is not kept after the shot");
}
