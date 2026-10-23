// Stage 119.3: retired GUT tests of the game layer, ported to rcg:: (one
// TEST_CASE per GUT test function, "ported: <gut file stem>: <test function>";
// docs/research/gut_ledger_game.tsv maps every row). Surfaces the GUT tests
// made with SurfaceSpec / Surface.new are written as rc-level/1 JSON and read
// by the level's own reader; the GDScript classes are their ports
// (LandingFold -> rcg::fold, SpawnFrame -> rcg::spawn_frame, Pieces ->
// rcg::build_pieces, SpawnTemplate.place -> rcg::mapped through the frame).
#include "rcg/build.hpp"
#include "rcg/geom.hpp"
#include "rcg/landing.hpp"
#include "rcg/level.hpp"
#include "rcg/model.hpp"
#include "rcg/session.hpp"

#include "rc/flags.hpp"
#include "rc/sweep.hpp"
#include "rc/tracer.hpp"
#include "rc/visual.hpp"
#include "rc/xf2.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;
using rc::V2;
namespace fs = std::filesystem;

const fs::path ROOT = RC_REPO_ROOT;
constexpr double PI = 3.141592653589793;

// SideSpec.make({"type": type} + params, interactive, hits)
json side(const std::string &type = "pass", bool interactive = false, json params = json::object(), json hits = json::array()) {
	json effect = {{"type", type}};
	for (auto it = params.begin(); it != params.end(); ++it) {
		effect[it.key()] = it.value();
	}
	json s = {{"effect", effect}, {"interactive", interactive}};
	if (!hits.empty()) {
		s["hits"] = hits;
	}
	return s;
}

// SurfaceSpec.arc / line (via: the midpoint) / circle
json arc(V2 s, V2 e, V2 v, const json &left, const json &right) {
	return {{"start", {s.x, s.y}}, {"end", {e.x, e.y}}, {"via", {v.x, v.y}}, {"left", left}, {"right", right}};
}
json line(V2 a, V2 b, const json &left, const json &right) {
	return arc(a, b, V2::of((double(a.x) + double(b.x)) / 2.0, (double(a.y) + double(b.y)) / 2.0), left, right);
}
json circle(V2 c, double r, const json &left, const json &right) {
	return {{"circle", {{"center", {c.x, c.y}}, {"radius", r}}}, {"left", left}, {"right", right}};
}

bool is_portal_json(const json &s) {
	return s.contains("left") && s["left"].is_object() && s["left"]["effect"]["type"] == "portal";
}

// a level of the surfaces (ids from 1 in order; a portal takes two), read by parse_level
rcg::Level level_of(const std::vector<json> &surfaces) {
	json level = {{"schema", "rc-level/1"}, {"surfaces", json::array()}};
	int id = 1;
	for (json s : surfaces) {
		const bool portal = is_portal_json(s);
		s["ids"] = portal ? json::array({id, id + 1}) : json::array({id});
		id += portal ? 2 : 1;
		level["surfaces"].push_back(s);
	}
	return rcg::parse_level(level.dump());
}

// one surface's spec (SurfaceSpec)
rcg::SurfaceSpec spec_of(const json &surface) { return level_of({surface}).surfaces.front().spec; }

// SurfaceSpec.build()[0] with the given id
rcg::BuiltSurface built(const json &surface, int id) { return rcg::build_spec(spec_of(surface), id, std::string("Surface")).front(); }

// a spawn template (SpawnTemplate.make) as authored in a hit effect
json template_of(const json &surface, const std::string &orientation, bool trim = true) {
	return {{"surface", surface}, {"orientation", orientation}, {"trim", trim}};
}

// SpawnTemplate.place(frame): the surface mapped by frame * Transform2D(0, offset)
rcg::SurfaceSpec place_in(const rcg::SurfaceSpec &t, const rc::Xf2 &frame, V2 offset = V2{}) {
	return rcg::mapped(t, frame * rc::Xf2::rotation(0.0f, offset));
}

rc::Flags flags(std::initializer_list<std::pair<std::string, rc::FlagValue>> entries) {
	rc::Flags f;
	for (const auto &[k, v] : entries) {
		f.set(k, v);
	}
	return f;
}

// a synthetic hit record on a host line at x (LEFT, on the segment, beam +x)
rc::HitRecord hit_at(const rcg::BuiltSurface &s, double y, const rc::Flags &before = {}, bool fired = true) {
	rc::HitRecord h;
	h.surface_id = s.id;
	h.side = rc::Side::Left;
	h.on_segment = true;
	h.fired = fired;
	h.state = fired;
	h.point = V2::of(s.seg.s.x, y);
	h.tangent = V2{1.0f, 0.0f};
	h.flags_before = before;
	h.step_index = 0;
	return h;
}

bool near(const V2 &a, const V2 &b, double tol) { return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol; }

// ---- test_stage107_landing_fold.gd

json fold_template() {
	return template_of(line(V2{0, -20}, V2{0, 20}, side("terminal"), side("terminal")), "CARRIER_TANGENT");
}
json spawn(const std::string &channel, bool deferred = false) {
	return {{"kind", "spawn"}, {"channel", channel}, {"template", fold_template()}, {"deferred", deferred}};
}
json reset(const std::vector<std::string> &channels, bool deferred = false) {
	return {{"kind", "reset"}, {"channels", channels}, {"deferred", deferred}};
}
// a host line at x (y -100..100), both sides the same plannable side with the hits
rcg::BuiltSurface fold_surface(double x, const json &hits, int id, const std::string &type = "pass") {
	const json s = side(type, true, json::object(), hits);
	return built(line(V2::of(x, -100), V2::of(x, 100), s, s), id);
}

// ---- test_stage108_trim.gd

rcg::BuiltSurface trim_host(double x, const std::string &channel, const json &tmpl, int id) {
	const json s = side("pass", true, json::object(), json::array({{{"kind", "spawn"}, {"channel", channel}, {"template", tmpl}}}));
	return built(line(V2::of(x, -200), V2::of(x, 200), s, s), id);
}
rcg::BuiltSurface trim_wall(V2 a, V2 b, int id, const json &presence = json::array()) {
	json w = line(a, b, side("terminal"), side("terminal"));
	if (!presence.empty()) {
		w["presence"] = presence;
	}
	return built(w, id);
}
// a mirror across the beam, centred on the hit: from (x, y - 60) to (x, y + 60)
json across(bool trim = true) {
	return template_of(line(V2{-60, 0}, V2{60, 0}, side("reflection", true), side("reflection", true)), "BEAM_NORMAL", trim);
}

// ---- test_stage108_pieces.gd

json term_line(V2 a, V2 b) { return line(a, b, side("terminal"), side("terminal")); }
json term_circle(V2 c, double r) { return circle(c, r, side("terminal"), side("terminal")); }

size_t count_pieces(const rcg::Pieces &p, const std::vector<rcg::BuiltSurface> &surfaces) {
	size_t n = 0;
	for (const rcg::BuiltSurface &s : surfaces) {
		n += p.of(s.id).size();
	}
	return n;
}

// ---- test_stage55_chevron_overlays.gd / test_stage85_resolvers.gd: Segment.from_coords((400, 0), (400, 600), (400, 300))
json chevron_surface(const json &left, const json &right) { return arc(V2{400, 0}, V2{400, 600}, V2{400, 300}, left, right); }

} // namespace

// ================================================================ test_invariant_combos.gd

TEST_CASE("ported: test_invariant_combos: test_no_piece_crossings") {
	// the test levels and the playground (combo_base aside): pieces of different
	// design surfaces never cross (NO-PIECE-CROSSINGS, GAME_SPEC 9.7)
	int scenes = 0;
	std::vector<std::string> violations;
	for (const char *dir : {"test_levels", "playground"}) {
		for (const auto &e : fs::directory_iterator(ROOT / "data" / "levels" / dir)) {
			if (e.path().extension() != ".json" || e.path().stem() == "combo_base") {
				continue;
			}
			scenes++;
			const std::vector<rcg::BuiltSurface> surfaces = rcg::build_level(rcg::load_level(e.path().string()));
			const rcg::Pieces pieces = rcg::build_pieces(surfaces);
			std::vector<std::pair<int, const rcg::SegGeom *>> segs;
			for (const rcg::BuiltSurface &s : surfaces) {
				if (s.pre_transform) {
					continue;
				}
				for (const rcg::Piece &p : pieces.of(s.id)) {
					segs.push_back({s.id, &p.segment});
				}
			}
			for (size_t i = 0; i < segs.size(); i++) {
				for (size_t j = i + 1; j < segs.size(); j++) {
					if (segs[i].first != segs[j].first && !rcg::crossings_of(*segs[i].second, *segs[j].second).empty()) {
						violations.push_back("[" + e.path().filename().string() + "] pieces of " + std::to_string(segs[i].first) + " and " +
							std::to_string(segs[j].first) + " cross");
					}
				}
			}
		}
	}
	CHECK(scenes > 0);
	for (const std::string &v : violations) {
		INFO(v);
		CHECK(false);
	}
	CHECK(violations.empty());
}

// ================================================================ test_stage107_landing_fold.gd

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_fired_spawn_lands_as_the_next_generation") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c")}), 1);
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host}, flags({{"chan:c", std::string("pending")}}), {{"c", 3}});
	CHECK(r.flags == flags({{"chan:c", std::string("g4")}}));
	CHECK(r.generations == std::map<std::string, int>{{"c", 4}});
	REQUIRE(r.placed.size() == 1);
	const rcg::SurfaceSpec &spec = r.placed[0].spec;
	// placed at the hit, across the carrier (local y is v)
	CHECK(spec.start == V2{120, 10});
	CHECK(spec.end == V2{80, 10});
	// the group exists at its generation
	REQUIRE(spec.presence.size() == 1);
	CHECK(spec.presence[0].key == "chan:c");
	CHECK(spec.presence[0].values == std::vector<std::string>{"g4"});
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_hit_that_did_not_fire_does_nothing") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c")}), 1);
	const rcg::Landing r = rcg::fold({hit_at(host, 10, {}, false)}, {host}, {}, {});
	CHECK(r.placed.empty());
	CHECK(r.flags == rc::Flags{});
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_reset_in_the_shot_cancels_the_spawn") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c")}), 1);
	const rcg::BuiltSurface rs = fold_surface(200, json::array({reset({"c"})}), 2);
	const std::vector<rc::HitRecord> hits{hit_at(host, 10), hit_at(rs, 10, flags({{"chan:c", std::string("pending")}}))};
	const rcg::Landing r = rcg::fold(hits, {host, rs}, flags({{"chan:c", std::string("armed")}}), {});
	CHECK(r.placed.empty()); // the solver's final flags say armed: nothing lands
	CHECK(r.flags == flags({{"chan:c", std::string("armed")}}));
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_spawn_reset_spawn_lands_the_last_firing") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c")}), 1);
	const rcg::BuiltSurface rs = fold_surface(200, json::array({reset({"c"})}), 2);
	const std::vector<rc::HitRecord> hits{hit_at(host, 10), hit_at(rs, 10, flags({{"chan:c", std::string("pending")}})),
		hit_at(host, 60, flags({{"chan:c", std::string("armed")}}))};
	const rcg::Landing r = rcg::fold(hits, {host, rs}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 1);
	CHECK(r.placed[0].spec.start == V2{120, 60}); // at the second firing (y = 60)
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_deferred_reset_applies_at_landing") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c")}), 1);
	const rcg::BuiltSurface rs = fold_surface(200, json::array({reset({"c"}, true)}), 2);
	const std::vector<rc::HitRecord> hits{hit_at(host, 10), hit_at(rs, 10, flags({{"chan:c", std::string("pending")}}))};
	// deferred: the solver did not apply it -- the shot ended pending
	const rcg::Landing r = rcg::fold(hits, {host, rs}, flags({{"chan:c", std::string("pending")}}), {});
	CHECK(r.placed.empty()); // applied at landing, it cancels the pending spawn
	CHECK(r.flags == flags({{"chan:c", std::string("armed")}}));
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_deferred_spawn_lands_from_its_landing_write") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c", true)}), 1);
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host}, {}, {});
	CHECK(r.placed.size() == 1); // pending is written at landing, then the spawn lands
	CHECK(r.flags == flags({{"chan:c", std::string("g1")}}));
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_shared_channel_lands_the_host_that_fired") {
	const rcg::BuiltSurface a = fold_surface(100, json::array({spawn("c")}), 1);
	const rcg::BuiltSurface b = fold_surface(300, json::array({spawn("c")}), 2);
	const rcg::Landing r = rcg::fold({hit_at(b, 10)}, {a, b}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(!r.placed.empty());
	CHECK(r.placed[0].spec.start == V2{320, 10}); // at the host that fired
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_list_order_on_one_side") {
	// target, spawn, reset of the same channel: in the shot the reset is the
	// last writer (armed), so the spawn does not land; the target registered
	const rcg::BuiltSurface host = fold_surface(100, json::array({{{"kind", "target"}}, spawn("c"), reset({"c"})}), 1);
	const rc::Flags final_flags = flags({{"target:" + std::to_string(host.id), true}, {"chan:c", std::string("armed")}});
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host}, final_flags, {});
	CHECK(r.placed.empty());
	CHECK(r.flags == final_flags);
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_categorical_side_is_resolved_under_the_flags_before") {
	json spec = line(V2{100, -100}, V2{100, 100}, nullptr, nullptr);
	const json c1 = side("pass", true, json::object(), json::array({spawn("c1")}));
	const json c2 = side("pass", true, json::object(), json::array({spawn("c2")}));
	spec["resolver"] = {{"key", "mode"}, {"table", json::array({json::array({"x", {{"left", c1}, {"right", c1}}}),
															 json::array({"DEFAULT", {{"left", c2}, {"right", c2}}})})}};
	const rcg::BuiltSurface host = built(spec, 1);
	const rcg::Landing r = rcg::fold({hit_at(host, 10, flags({{"mode", std::string("x")}}))}, {host},
		flags({{"mode", std::string("x")}, {"chan:c1", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 1);
	CHECK(r.placed[0].channel == "c1");
	const rcg::Landing r2 = rcg::fold({hit_at(host, 10, {})}, {host}, flags({{"chan:c2", std::string("pending")}}), {});
	REQUIRE(r2.placed.size() == 1);
	CHECK(r2.placed[0].channel == "c2");
}

TEST_CASE("ported: test_stage107_landing_fold: test_stage107_a_host_gets_its_channel_presence") {
	const rcg::BuiltSurface host = fold_surface(100, json::array({spawn("c"), spawn("d")}), 1);
	CHECK(rcg::present(host, {}));
	CHECK_FALSE(rcg::present(host, flags({{"chan:c", std::string("pending")}})));
	// a host needs every channel it spawns into armed
	CHECK_FALSE(rcg::present(host, flags({{"chan:d", std::string("g2")}})));
	CHECK(rcg::present(host, flags({{"chan:c", std::string("armed")}, {"chan:d", std::string("armed")}})));
}

// ================================================================ test_stage107_level.gd

TEST_CASE("ported: test_stage107_level: test_stage107_k5_pinned_portal_departure_on_a_carrier") {
	// K5 (fixed) without any Stage 107 feature: the legacy portal_lines and a pass
	// line whose carrier runs through the portal image's departure point
	// (build_room off: the portal, the four edge walls, the pass line)
	const json pl = side("portal", true, {{"theta", 0.0}, {"d", {300.0, 0.0}}});
	const json off = side();
	std::vector<json> surfaces{line(V2{1000, 150}, V2{1000, 300}, pl, pl)};
	surfaces.back()["label"] = "Portal";
	for (const auto &[a, b] : std::vector<std::pair<V2, V2>>{
				 {V2{0, 0}, V2{1920, 0}}, {V2{1920, 0}, V2{1920, 1080}}, {V2{1920, 1080}, V2{0, 1080}}, {V2{0, 1080}, V2{0, 0}}}) {
		surfaces.push_back(line(a, b, off, off));
		surfaces.back()["label"] = nullptr;
	}
	surfaces.push_back(line(V2{1300, 400}, V2{1300, 600}, off, off)); // the image's carrier, below it
	surfaces.back()["label"] = "PassThrough";
	rc::Tracer t;
	t.set_defs(rcg::defs_of(rcg::build_level(level_of(surfaces))));
	const std::optional<rc::TracePair> pair = t.trace_pair(V2{700, 200}, V2{800, 210}, {}, {});
	REQUIRE(pair);
	const std::vector<rc::TraceStep> &steps = pair->physical.steps;
	int after = -1;
	for (size_t i = 0; i < steps.size(); i++) {
		if (steps[i].start.x == 1300.0f) {
			after = int(i);
			break;
		}
	}
	CHECK(after > 0); // the beam teleported to the image
	if (after > 0) {
		CHECK(steps[size_t(after)].after_portal); // K5 (fixed): the step after the teleport keeps its after_portal mark
	}
}

// ================================================================ test_stage107_spawn_frame.gd

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_carrier_tangent_follows_the_segment") {
	CHECK(rcg::carrier_tangent(V2{0, 0}, V2{5, 0}, V2{10, 0}, true, V2{}, V2{3, 0}) == V2{1, 0}); // a line: start -> end
	CHECK(rcg::carrier_tangent(V2{10, 0}, V2{5, 0}, V2{0, 0}, true, V2{}, V2{3, 0}) == V2{-1, 0}); // the reversed line
	// the arc (10, 0) -> (0, 10) -> (-10, 0) about the origin: at (0, 10) it moves toward -x
	CHECK(rcg::carrier_tangent(V2{10, 0}, V2{0, 10}, V2{-10, 0}, false, V2{}, V2{0, 10}) == V2{-1, 0});
	CHECK(rcg::carrier_tangent(V2{-10, 0}, V2{0, 10}, V2{10, 0}, false, V2{}, V2{0, 10}) == V2{1, 0}); // the reversed arc
	CHECK(rcg::carrier_tangent(V2{10, 0}, V2{0, 10}, V2{-10, 0}, false, V2{}, V2{10, 0}) == V2{0, 1});  // at the start
}

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_frame_axes_and_the_mirror") {
	using M = rcg::SpawnMode;
	const rc::Xf2 f = rcg::spawn_frame(M::BeamTangent, V2{100, 50}, V2{0, 1}, V2{1, 0}, false, true);
	// v = u rotated by +90
	CHECK(f.x == V2{0, 1});
	CHECK(f.y == V2{-1, 0});
	CHECK(f.origin == V2{100, 50});
	const rc::Xf2 g = rcg::spawn_frame(M::BeamTangent, V2{100, 50}, V2{0, 1}, V2{1, 0}, true, true);
	CHECK(g.y == V2{1, 0}); // mirrored on a RIGHT-side hit
	const rc::Xf2 h = rcg::spawn_frame(M::BeamTangent, V2{100, 50}, V2{0, 1}, V2{1, 0}, true, false);
	CHECK(h.y == V2{-1, 0}); // not mirrored without mirror_with_side
	CHECK(g.determinant() < 0.0f); // a mirrored frame reflects
}

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_tangent_and_perpendicular_placement") {
	// a line along the local x axis lies along u; along y, perpendicular to it
	const rcg::SurfaceSpec along = spec_of(line(V2{-20, 0}, V2{20, 0}, side("reflection", true), side("reflection", true)));
	const rcg::SurfaceSpec acr = spec_of(line(V2{0, -20}, V2{0, 20}, side("terminal"), side("terminal")));
	const rc::Xf2 f = rcg::spawn_frame(rcg::SpawnMode::CarrierTangent, V2{100, 50}, V2{0, 1}, V2{1, 0}, false, false);
	const rcg::SurfaceSpec a = place_in(along, f);
	CHECK(a.start == V2{80, 50}); // tangent to the carrier
	CHECK(a.end == V2{120, 50});
	const rcg::SurfaceSpec b = place_in(acr, f);
	CHECK(b.start == V2{100, 30}); // perpendicular to the carrier
	CHECK(b.end == V2{100, 70});
}

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_circle_template_is_placed_by_its_center") {
	const rcg::SurfaceSpec t = spec_of(circle(V2{30, 0}, 10.0, side("inversion", true), side("inversion", true)));
	const rc::Xf2 f = rcg::spawn_frame(rcg::SpawnMode::BeamTangent, V2{100, 50}, V2{0, -1}, V2{1, 0}, false, false);
	const rcg::SurfaceSpec c = place_in(t, f);
	CHECK(c.full_circle);
	CHECK(c.center == V2{100, 20}); // 30 along the beam
	CHECK(c.radius == 10.0);
}

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_landing_places_at_the_hit_on_an_arc_host") {
	// the host: the arc (10, 0) -> (0, 10) -> (-10, 0) around (0, 0); a hit at
	// (0, 10) from outside (the beam going -y): the carrier tangent there is -x
	const rcg::BuiltSurface host = built(arc(V2{10, 0}, V2{-10, 0}, V2{0, 10}, side(), side()), 1);
	rcg::SpawnTemplateSpec t;
	t.surface = std::make_shared<rcg::SurfaceSpec>(spec_of(line(V2{5, 0}, V2{15, 0}, side("terminal"), side("terminal"))));
	t.orientation = "CARRIER_TANGENT";
	rc::HitRecord h;
	h.point = V2{0, 10};
	h.tangent = V2{0, -1};
	h.side = rc::Side::Left;
	const rcg::SurfaceSpec spec = rcg::place(t, host, h);
	CHECK(near(spec.start, V2{-5, 10}, 1e-4));
	CHECK(near(spec.end, V2{-15, 10}, 1e-4));
}

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_rotated_line_template_stays_a_line") {
	// a line placed at an arbitrary angle stays an exact line (rotated float32
	// points are almost never exactly collinear; a three-point segment would be a
	// huge arc). Regression for the user's report on hit_effects.tscn.
	const rcg::SurfaceSpec t = spec_of(line(V2{100, -40}, V2{100, 40}, side("reflection", true), side("reflection", true)));
	for (const double angle : {0.3, 1.234567, 2.9, -0.71}) {
		CAPTURE(angle);
		const V2 beam = V2::of(std::cos(angle), std::sin(angle)); // Vector2.from_angle
		const rc::Xf2 f = rcg::spawn_frame(rcg::SpawnMode::BeamTangent, V2::of(503.37, 271.91), beam, V2{0, 1}, false, false);
		const rcg::SurfaceSpec placed = place_in(t, f);
		CHECK(placed.straight); // marked straight
		CHECK(rcg::segment_of(placed).is_line()); // an exact line
		const json d = rcg::describe_surface(rcg::build_spec(placed, 1, std::string("Surface")).front());
		CHECK(d["line"].get<bool>()); // the solver sees a line
	}
	const json p = side("portal", true, {{"theta", 0.7}, {"d", {300.5, -20.25}}});
	const std::vector<rcg::BuiltSurface> pair = rcg::build_spec(spec_of(line(V2{10, 3}, V2{91, 57}, p, p)), 1, std::nullopt);
	REQUIRE(pair.size() == 2);
	CHECK(pair[1].seg.is_line()); // a rotated portal's image of a line is a line
}

TEST_CASE("ported: test_stage107_spawn_frame: test_stage107_template_offset_is_in_the_frame_axes") {
	// a template is centred on the hit point by default; `offset` (in the frame's axes) moves it
	const rcg::SurfaceSpec l = spec_of(line(V2{-20, 0}, V2{20, 0}, side("terminal"), side("terminal")));
	const rc::Xf2 f = rcg::spawn_frame(rcg::SpawnMode::BeamTangent, V2{100, 50}, V2{0, 1}, V2{1, 0}, false, false);
	const rcg::SurfaceSpec centred = place_in(l, f);
	CHECK(centred.start == V2{100, 30}); // centred on the hit point
	CHECK(centred.end == V2{100, 70});
	const rcg::SurfaceSpec ahead = place_in(l, f, V2{50, 10});
	CHECK(ahead.start == V2{90, 80}); // 50 along u (the beam), 10 along v
	CHECK(ahead.end == V2{90, 120});
}

// ================================================================ test_stage107_surface_spec.gd

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_every_path_effect_round_trips") {
	// (the GDScript SurfaceSpec.of round trip is gone: the built surface carries
	// the effect, its parameters and the hit effects as authored)
	const V2 a0{0, -100}, a1{0, 100}, a2{100, 0};
	const json hits = json::array({{{"kind", "state"}, {"key", "k"}, {"op", "set"}, {"value", 3}, {"deferred", false}},
		{{"kind", "target"}, {"deferred", true}}, {{"kind", "reset"}, {"channels", {"a"}}, {"deferred", false}}});
	const std::vector<std::pair<json, bool>> cases{
		{side("terminal"), false},
		{side("reflection", true), false},
		{side("reflection", true), true},
		{side("inversion", true), true},
		{side("line_normal_projection", true, {{"back", true}}), false},
		{side("circle_normal_projection", true, {{"back", false}}), true},
		{side("line_directional_projection", true, {{"back", false}, {"normal", {1.0, 0.0}}}), false}, // (across the line: B9)
		{side("circle_directional_projection", true, {{"back", true}, {"normal", {1.0, 0.0}}}), true},
		{side("pass", true, json::object(), hits), false},
	};
	for (const auto &[sd, on_arc] : cases) {
		const json &effect = sd["effect"];
		CAPTURE(effect.dump());
		const rcg::SurfaceSpec spec = spec_of(on_arc ? arc(a0, a1, a2, sd, side()) : line(V2{100, -50}, V2{100, 50}, sd, side()));
		CHECK(rcg::validate_spec(spec).empty());
		const rcg::BuiltSurface surf = rcg::build_spec(spec, 1, std::string("Surface")).front();
		// the effect round-trips
		const json d = rcg::describe_surface(surf);
		for (auto it = effect.begin(); it != effect.end(); ++it) {
			CAPTURE(it.key());
			CHECK(d["left"][it.key()] == it.value());
		}
		// the hit effects round-trip, in order
		const json want = sd.value("hits", json::array());
		REQUIRE(surf.left);
		REQUIRE(surf.left->hits.size() == want.size());
		for (size_t i = 0; i < want.size(); i++) {
			for (auto it = want[i].begin(); it != want[i].end(); ++it) {
				CAPTURE(i);
				CAPTURE(it.key());
				CHECK(surf.left->hits[i].raw[it.key()] == it.value());
			}
		}
	}
}

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_validation_knows_the_carriers") {
	CHECK_FALSE(rcg::validate_spec(spec_of(line(V2{100, -50}, V2{100, 50}, side("inversion", true), side()))).empty()); // inversion on a line
	CHECK_FALSE(rcg::validate_spec(spec_of(circle(V2{}, 50.0, side("line_normal_projection", true), side()))).empty()); // a line projection on a circle
	CHECK_FALSE(rcg::validate_spec(spec_of(line(V2{1, 1}, V2{1, 1}, side(), side()))).empty());                       // zero length
	CHECK_FALSE(rcg::validate_spec(spec_of(circle(V2{}, 0.0, side(), side()))).empty());                               // zero radius
	CHECK_FALSE(rcg::validate_spec(spec_of(line(V2{100, -50}, V2{100, 50}, side("teleport"), side()))).empty());      // an unknown effect
	CHECK_FALSE(rcg::validate_spec(spec_of(line(V2{100, -50}, V2{100, 50}, side("pass", true), side()))).empty()); // a plannable pass without hit effects
}

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_identity_frame_is_bit_exact") {
	const rcg::SurfaceSpec spec = spec_of(arc(V2::of(0.1, -100.3), V2::of(0.7, 100.9), V2::of(100.5, 0.25), side("reflection", true),
		side("line_normal_projection", true)));
	const rcg::SurfaceSpec m = rcg::mapped(spec, rc::Xf2::identity());
	CHECK(m.start == spec.start);
	CHECK(m.end == spec.end);
	CHECK(m.via == spec.via);
	REQUIRE(m.left);
	REQUIRE(m.right);
	CHECK(m.left->effect.raw == spec.left->effect.raw);
	CHECK(m.left->effect.type == spec.left->effect.type);
	CHECK(m.right->effect.raw == spec.right->effect.raw);
	CHECK(m.right->effect.type == spec.right->effect.type);
	CHECK(m.right->effect.back == spec.right->effect.back);
}

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_rotation_maps_points_and_normals") {
	const rcg::SurfaceSpec spec =
		spec_of(line(V2{10, 0}, V2{20, 0}, side("line_directional_projection", true, {{"normal", {1.0, 0.0}}}), side()));
	const rc::Xf2 xf = rc::Xf2::rotation(float(PI / 2.0), V2{5, 5});
	const rcg::SurfaceSpec m = rcg::mapped(spec, xf);
	CHECK(near(m.start, V2{5, 15}, 1e-4));
	CHECK(near(m.end, V2{5, 25}, 1e-4));
	REQUIRE(m.left);
	REQUIRE(m.left->effect.normal);
	const V2 n = V2::of((*m.left->effect.normal)[0], (*m.left->effect.normal)[1]);
	CHECK(near(n, V2{0, 1}, 1e-6)); // the normal turns with the frame
	CHECK(m.left->effect.type == "line_directional_projection"); // no swap under a rotation
}

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_reflection_swaps_sides") {
	const rcg::SurfaceSpec spec = spec_of(line(V2{0, -10}, V2{0, 10}, side("reflection", true), side("terminal")));
	const rc::Xf2 mirror = rc::Xf2::of(V2{-1, 0}, V2{0, 1}, V2{}); // x -> -x
	const rcg::SurfaceSpec m = rcg::mapped(spec, mirror);
	REQUIRE(m.left);
	REQUIRE(m.right);
	CHECK(m.left->effect.type == "terminal"); // the side facing +x faces -x after the mirror: sides swap
	CHECK(m.right->effect.type == "reflection");
	// a beam from a region meets the same behaviour as the mirrored beam from
	// the mirrored region (the side it hits carries the same effect)
	std::vector<std::string> met;
	const std::vector<std::tuple<const rcg::SurfaceSpec *, V2, V2>> ws{{&spec, V2{-50, 0}, V2{-40, 0}}, {&m, V2{50, 0}, V2{40, 0}}};
	for (const auto &[w, from, toward] : ws) {
		rc::Tracer t;
		t.set_defs(rcg::defs_of(rcg::build_spec(*w, 1, std::string("Surface"))));
		const std::optional<rc::TracePair> pair = t.trace_pair(from, toward, {}, {});
		REQUIRE(pair);
		REQUIRE(!pair->physical.hits.empty());
		const rc::Side s = pair->physical.hits[0].side;
		const std::optional<rcg::SideSpec> &cfg = s == rc::Side::Left ? w->left : w->right;
		REQUIRE(cfg);
		met.push_back(cfg->effect.type);
	}
	CHECK(met[0] == met[1]); // the mirrored beam meets the same effect
}

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_portal_motion_is_conjugated_by_the_frame") {
	for (const rc::Xf2 &xf : {rc::Xf2::rotation(0.7f, V2{30, -20}), rc::Xf2::of(V2{-1, 0}, V2{0, 1}, V2{4, 9})}) {
		const json p = side("portal", true, {{"theta", 0.4}, {"d", {100.0, 20.0}}});
		const rcg::SurfaceSpec m = rcg::mapped(spec_of(line(V2{0, -50}, V2{0, 50}, p, p)), xf);
		REQUIRE(m.left);
		REQUIRE(m.left->effect.theta);
		REQUIRE(m.left->effect.d);
		const double th = *m.left->effect.theta;
		const V2 d = V2::of((*m.left->effect.d)[0], (*m.left->effect.d)[1]);
		for (const V2 &z : {V2{3, 4}, V2{-60, 10}, V2{0, 0}}) {
			const V2 want = xf.xform(rcg::apply_motion(z, 0.4, V2{100, 20}));
			const V2 got = rcg::apply_motion(xf.xform(z), th, d);
			CHECK(near(got, want, 1e-3)); // T'(F z) = F(T z)
		}
	}
}

TEST_CASE("ported: test_stage107_surface_spec: test_stage107_presence_survives_build") {
	json l = line(V2{100, -50}, V2{100, 50}, side("reflection", true), side());
	l["presence"] = json::array({{{"key", "chan:c"}, {"values", {"g1"}}}});
	const rcg::BuiltSurface s = built(l, 1);
	CHECK_FALSE(rcg::present(s, {}));
	CHECK(rcg::present(s, flags({{"chan:c", std::string("g1")}})));
	CHECK(rcg::describe_surface(s)["presence"] == json::array({{{"key", "chan:c"}, {"values", {"g1"}}}}));
}

// ================================================================ test_stage108_pieces.gd

TEST_CASE("ported: test_stage108_pieces: test_stage108_the_spec_counts") {
	const auto two = rcg::build_level(level_of({term_line(V2{0, 50}, V2{100, 50}), term_line(V2{50, 0}, V2{50, 100})}));
	CHECK(count_pieces(rcg::build_pieces(two), two) == 4); // two crossing lines: 4 pieces
	const auto seven = rcg::build_level(
		level_of({term_line(V2{0, 50}, V2{100, 50}), term_line(V2{30, 0}, V2{30, 100}), term_line(V2{70, 0}, V2{70, 100})}));
	CHECK(count_pieces(rcg::build_pieces(seven), seven) == 7); // one line crossing two others: 7
	// a triangle of lines extended past each other's corners
	const auto nine = rcg::build_level(
		level_of({term_line(V2{-10, 0}, V2{110, 0}), term_line(V2{-5, -10}, V2{55, 110}), term_line(V2{105, -10}, V2{45, 110})}));
	CHECK(count_pieces(rcg::build_pieces(nine), nine) == 9); // three inter-crossing lines: 9
}

TEST_CASE("ported: test_stage108_pieces: test_stage108_pieces_share_ends_and_keep_the_carrier") {
	const auto surfaces = rcg::build_level(level_of({term_line(V2{500, -300}, V2{500, 300}), term_line(V2{400, 200}, V2{600, 200})}));
	const rcg::BuiltSurface &a = surfaces[0];
	const rcg::Pieces p = rcg::build_pieces(surfaces);
	const std::vector<rcg::Piece> &pa = p.of(a.id);
	const std::vector<rcg::Piece> &pb = p.of(surfaces[1].id);
	REQUIRE(pa.size() == 2);
	REQUIRE(!pb.empty());
	CHECK(pa[0].segment.s == V2{500, -300}); // the first piece starts at the start
	CHECK(pa[0].segment.e == pa[1].segment.s); // consecutive pieces share their ends
	CHECK(pa[1].segment.e == V2{500, 300});   // the last ends at the end
	CHECK(pa[0].segment.e == pb[0].segment.e); // both surfaces' pieces share the crossing
	CHECK(rc::arc::same_circle(pa[0].segment.get_carrier(), a.seg.get_carrier())); // the design surface's carrier
	CHECK(pa[0].contains(V2{500, 0}));
	CHECK_FALSE(pa[0].contains(V2{500, 250}));
	CHECK(p.crossings == std::vector<V2>{V2{500, 200}});
}

TEST_CASE("ported: test_stage108_pieces: test_stage108_crossing_free_surfaces_are_their_own_piece") {
	const auto surfaces = rcg::build_level(level_of({term_line(V2{0, 0}, V2{100, 0}), term_line(V2{0, 10}, V2{100, 10})}));
	const rcg::BuiltSurface &a = surfaces[0];
	const rcg::Pieces p = rcg::build_pieces(surfaces);
	REQUIRE(p.of(a.id).size() == 1);
	CHECK(p.of(a.id)[0].index == -1); // the whole surface
	const rcg::SegGeom &seg = p.of(a.id)[0].segment; // its own segment
	CHECK(seg.s == a.seg.s);
	CHECK(seg.e == a.seg.e);
	CHECK(seg.v == a.seg.v);
	CHECK(seg.full == a.seg.full);
	CHECK_FALSE(p.of(a.id)[0].span.has_value()); // no span: a plan entry on it matches the whole surface
}

TEST_CASE("ported: test_stage108_pieces: test_stage108_circle_pieces") {
	const auto surfaces = rcg::build_level(level_of({term_circle(V2{0, 0}, 5.0), term_line(V2{3, -10}, V2{3, 10})}));
	const rcg::BuiltSurface &c = surfaces[0];
	const rcg::Pieces p = rcg::build_pieces(surfaces);
	CHECK(p.of(c.id).size() == 2); // a full circle crossed twice: two arcs
	CHECK(p.of(surfaces[1].id).size() == 3);
	for (const rcg::Piece &piece : p.of(c.id)) {
		CHECK_FALSE(piece.segment.full);
		CHECK(rc::arc::same_circle(piece.segment.get_carrier(), c.seg.get_carrier()));
		CHECK(std::fabs(double(piece.segment.v.length()) - 5.0) <= 1e-4); // the via lies on the circle
	}
	// ends inside: one crossing
	const auto two = rcg::build_level(level_of({term_circle(V2{0, 0}, 5.0), term_line(V2{3, -10}, V2{3, 0})}));
	CHECK(rcg::build_pieces(two).of(two[0].id).size() == 1); // one cut leaves a full circle whole
}

TEST_CASE("ported: test_stage108_pieces: test_stage108_portals_and_pre_transforms_are_not_split") {
	const json pl = side("portal", true, {{"theta", 0.0}, {"d", {300.0, 0.0}}});
	std::vector<rcg::BuiltSurface> surfaces =
		rcg::build_level(level_of({arc(V2{0, -50}, V2{0, 50}, V2{0, 0}, pl, pl), term_line(V2{-20, 0}, V2{20, 0})}));
	REQUIRE(surfaces.size() == 3);
	surfaces.erase(surfaces.begin() + 1); // the portal source alone (the GUT test built no image)
	const rcg::Pieces p = rcg::build_pieces(surfaces);
	CHECK(p.of(surfaces[0].id).size() == 1);
	CHECK(p.of(surfaces[1].id).size() == 1);
	CHECK(p.portal_crossings.size() == 1); // reported for level validation
}

// ================================================================ test_stage108_trim.gd

TEST_CASE("ported: test_stage108_trim: test_stage108_a_spawn_is_trimmed_by_a_present_wall") {
	const rcg::BuiltSurface host = trim_host(100, "c", across(), 1);
	const rcg::BuiltSurface wall = trim_wall(V2{0, 40}, V2{200, 40}, 2); // crosses the mirror at (100, 40)
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host, wall}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 1);
	const rcg::SurfaceSpec &spec = r.placed[0].spec;
	CHECK(spec.start == V2{100, -50}); // the midpoint's piece (BEAM_NORMAL: u = +y)
	CHECK(spec.end == V2{100, 40});
	CHECK(spec.straight);
	CHECK(rcg::crossings_of(rcg::segment_of(spec), wall.seg).empty()); // it no longer crosses the wall
}

TEST_CASE("ported: test_stage108_trim: test_stage108_absent_surfaces_do_not_cut_and_trim_can_be_off") {
	const rcg::BuiltSurface host = trim_host(100, "c", across(), 1);
	const rcg::BuiltSurface absent =
		trim_wall(V2{0, 40}, V2{200, 40}, 2, json::array({{{"key", "on"}, {"values", {"true"}}}}));
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host, absent}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 1);
	CHECK(r.placed[0].spec.end == V2{100, 70}); // an absent surface does not cut
	const rcg::BuiltSurface host2 = trim_host(100, "c", across(false), 1);
	const rcg::BuiltSurface wall = trim_wall(V2{0, 40}, V2{200, 40}, 2);
	const rcg::Landing r2 = rcg::fold({hit_at(host2, 10)}, {host2, wall}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(r2.placed.size() == 1);
	CHECK(r2.placed[0].spec.end == V2{100, 70}); // trim off: whole
}

TEST_CASE("ported: test_stage108_trim: test_stage108_a_spawned_circle_keeps_its_longest_arc") {
	const json t = template_of(circle(V2{0, 0}, 50.0, side("terminal"), side("terminal")), "BEAM_TANGENT");
	const rcg::BuiltSurface host = trim_host(100, "c", t, 1);
	const rcg::BuiltSurface wall = trim_wall(V2{130, -200}, V2{130, 200}, 2); // cuts the circle about (100, 10) at x = 130
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host, wall}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 1);
	const rcg::SurfaceSpec &spec = r.placed[0].spec;
	CHECK_FALSE(spec.full_circle); // an arc now
	CHECK(std::fabs(spec.start.x - 130.0) <= 1e-3);
	CHECK(std::fabs(spec.end.x - 130.0) <= 1e-3);
	CHECK(spec.via.x < 100.0f); // the longer arc (through x = 50, the far left)
}

TEST_CASE("ported: test_stage108_trim: test_stage108_two_spawns_of_one_landing_do_not_cross") {
	const rcg::BuiltSurface a = trim_host(100, "a", across(), 1);
	// b's spawn: a horizontal mirror along the beam through (100, 30)
	const json along = template_of(line(V2{-150, 0}, V2{150, 0}, side("reflection", true), side("reflection", true)), "BEAM_TANGENT");
	const rcg::BuiltSurface b = trim_host(300, "b", along, 2);
	rc::HitRecord hb = hit_at(b, 30);
	hb.point = V2{100, 30}; // placed through x = 100 (crossing a's spawn)
	const rcg::Landing r = rcg::fold({hit_at(a, 10), hb}, {a, b},
		flags({{"chan:a", std::string("pending")}, {"chan:b", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 2);
	const rcg::SurfaceSpec &sa = r.placed[0].spec;
	const rcg::SurfaceSpec &sb = r.placed[1].spec;
	CHECK(rcg::crossings_of(rcg::segment_of(sa), rcg::segment_of(sb)).empty()); // the later one is trimmed by the earlier
	CHECK(sa.end == V2{100, 70}); // the first placed is whole
}

TEST_CASE("ported: test_stage108_trim: test_stage108_untrimmed_when_nothing_crosses") {
	const rcg::BuiltSurface host = trim_host(100, "c", across(), 1);
	const rcg::Landing r = rcg::fold({hit_at(host, 10)}, {host}, flags({{"chan:c", std::string("pending")}}), {});
	REQUIRE(r.placed.size() == 1);
	CHECK(r.placed[0].spec.start == V2{100, -50});
	CHECK(r.placed[0].spec.end == V2{100, 70});
}

// ================================================================ test_stage22_plan.gd

TEST_CASE("ported: test_stage22_plan: test_stage22_line_sample_endpoints") {
	const V2 s{0, 0}, e{100, 0};
	const rc::arc::LineSample r0 = rc::arc::line_sample(s, e, 0.0);
	const rc::arc::LineSample r1 = rc::arc::line_sample(s, e, 1.0);
	CHECK(near(r0.position, s, 0.01)); // t=0 should give start
	CHECK(near(r1.position, e, 0.01)); // t=1 should give end
}

TEST_CASE("ported: test_stage22_plan: test_stage22_arc_sample_start") {
	const V2 center{300, 300};
	const double radius = 100.0, sa = 0.0, span = PI;
	const rc::arc::ArcSample r = rc::arc::arc_sample(center, radius, sa, span, false, 0.0);
	const V2 expected = center + V2::of(std::cos(sa), std::sin(sa)) * float(radius);
	CHECK(near(r.position, expected, 0.1)); // t=0 should give start of arc
}

// ================================================================ test_stage32_checkpoint.gd

TEST_CASE("ported: test_stage32_checkpoint: test_stage32_undo_indistinguishable_from_pre_shot") {
	// the main level (gravity off, as the GUT scene): fire at player + (100, 0),
	// land, teleport to (999, 999), undo. The velocity is not in the record: it
	// shows in the movement after, against a session that never shot. Without
	// gravity the controller sets it from the input alone, so the run is
	// repeated with gravity on and the player falling (a velocity of its own).
	const rcg::Level level = rcg::load_level((ROOT / "data" / "levels" / "main.json").string());
	for (const bool falling : {false, true}) {
		CAPTURE(falling);
		auto prepare = [&](rcg::Session &s) {
			if (falling) {
				s.key(rcg::Action::ToggleGravity);
				s.hold({}, 5);
			}
			s.set_cursor(s.player() + V2{100, 0});
			s.settle();
		};
		rcg::Session ref(level), game(level);
		prepare(ref);
		prepare(game);
		const json pre = game.record(json::object());
		game.fire(-1, 1.0 / 60.0);
		game.settle();
		CHECK(game.record(json::object())["checkpoints"] == 1);
		game.teleport(V2{999, 999});
		game.key(rcg::Action::Undo);
		game.settle();
		const json post = game.record(json::object());
		CHECK(post["player"] == pre["player"]);   // position restored
		CHECK(post["plan"] == pre["plan"]);       // plan size restored
		CHECK(post["targets"] == pre["targets"]); // targets restored
		CHECK(post["flags"] == pre["flags"]);
		CHECK(post["checkpoints"] == pre["checkpoints"]);
		// velocity restored: the same movement from here as the session that never shot
		const std::vector<V2> want = ref.hold({}, 10);
		const std::vector<V2> got = game.hold({}, 10);
		if (falling) {
			// (the check has teeth: a zeroed velocity moves otherwise)
			rcg::Session still(level);
			prepare(still);
			still.teleport(still.player());
			CHECK(still.hold({}, 10) != want);
		}
		CHECK(got == want);
	}
}

// ================================================================ test_stage55_chevron_overlays.gd

TEST_CASE("ported: test_stage55_chevron_overlays: test_non_interactive_side_no_link") {
	const rcg::BuiltSurface surf = built(chevron_surface(side("reflection", true), side("pass", false)), 1);
	CHECK_FALSE(rcg::overlay_link(surf, rcg::Side::Right, {}).has_value()); // non-interactive side has no link
}

TEST_CASE("ported: test_stage55_chevron_overlays: test_terminal_side_no_link") {
	const rcg::BuiltSurface surf = built(chevron_surface(side("terminal"), side("terminal")), 1);
	CHECK_FALSE(rcg::overlay_link(surf, rcg::Side::Left, {}).has_value());  // terminal LEFT has no link
	CHECK_FALSE(rcg::overlay_link(surf, rcg::Side::Right, {}).has_value()); // terminal RIGHT has no link
}

TEST_CASE("ported: test_stage55_chevron_overlays: test_non_interactive_side_not_highlighted") {
	// the hover overlays are built from the side's link: none, nothing highlighted
	const rcg::BuiltSurface surf = built(chevron_surface(side("pass", false), side("pass", false)), 1);
	CHECK_FALSE(rcg::overlay_link(surf, rcg::Side::Left, {}).has_value());
}

// ================================================================ test_stage85_resolvers.gd

TEST_CASE("ported: test_stage85_resolvers: test_categorical_resolver_lookup_and_DEFAULT_fallback") {
	const json mirror = side("reflection", true), block = side("terminal", false), open = side("pass", false);
	auto row = [](const char *value, const json &l, const json &r) { return json::array({value, {{"left", l}, {"right", r}}}); };
	json spec = arc(V2{0, 0}, V2{0, 100}, V2{0, 50}, nullptr, nullptr);
	spec["resolver"] = {{"key", "door"}, {"table", json::array({row("true", mirror, mirror), row("false", open, open), row("DEFAULT", block, nullptr)})}};
	const rcg::BuiltSurface res = built(spec, 1);
	REQUIRE(res.resolver.size() == 3);
	const rcg::BuiltSide *m = &*res.resolver[0].left, *o = &*res.resolver[1].right, *b = &*res.resolver[2].left;
	CHECK(rcg::active_side(res, rcg::Side::Left, flags({{"door", true}})) == m);
	CHECK(rcg::active_side(res, rcg::Side::Right, flags({{"door", false}})) == o);
	// missing key -> DEFAULT
	CHECK(rcg::active_side(res, rcg::Side::Left, {}) == b);
	CHECK(rcg::active_side(res, rcg::Side::Right, {}) == nullptr);
	// a value without an entry -> DEFAULT
	CHECK(rcg::active_side(res, rcg::Side::Left, flags({{"door", rcg::flag_value(rcg::FlagValue{std::int64_t(7)})}})) == b);
	// no DEFAULT: the first entry
	json spec2 = arc(V2{0, 0}, V2{0, 100}, V2{0, 50}, nullptr, nullptr);
	spec2["resolver"] = {{"key", "k"}, {"table", json::array({row("1", mirror, mirror), row("2", block, block)})}};
	const rcg::BuiltSurface res2 = built(spec2, 2);
	REQUIRE(res2.resolver.size() == 2);
	CHECK(rcg::active_side(res2, rcg::Side::Left, {}) == &*res2.resolver[0].left);
	// values are keyed by their JSON spelling: 2 and 2.0 are the same key
	CHECK(rcg::active_side(res2, rcg::Side::Left, flags({{"k", 2.0}})) == &*res2.resolver[1].left);
	CHECK(rcg::active_side(res2, rcg::Side::Left, flags({{"k", rcg::flag_value(rcg::FlagValue{std::int64_t(2)})}})) == &*res2.resolver[1].left);
}

// ================================================================ test_stage85_state_changes.gd

TEST_CASE("ported: test_stage85_state_changes: test_set_and_toggle_ops") {
	rc::Flags f;
	f = f.applied(rc::StateChangeDef{"open", true, true});
	CHECK(f == flags({{"open", true}})); // toggle of a missing key: true
	f = f.applied(rc::StateChangeDef{"open", true, true});
	CHECK(f == flags({{"open", false}}));
	const rc::Flags before = f;
	f = f.applied(rc::StateChangeDef{"k", false, rcg::flag_value(rcg::FlagValue{std::int64_t(2)})});
	CHECK(f == flags({{"open", false}, {"k", 2.0}}));
	CHECK(before == flags({{"open", false}})); // apply returns a new dictionary
	CHECK(rc::flag_key(true) == "true");
	CHECK(rc::flag_key(false) == "false");
	CHECK(rc::flag_key(rcg::flag_value(rcg::FlagValue{std::int64_t(2)})) == "2");
	CHECK(rc::flag_key(2.0) == "2");
	CHECK(rc::flag_key(std::string("red")) == "red");
	// scene-def form (oracle format): a side's in-shot writes
	const rcg::BuiltSurface s = built(line(V2{0, 0}, V2{0, 100},
		side("pass", true, json::object(), json::array({{{"kind", "state"}, {"key", "k"}, {"op", "set"}, {"value", 2}},
			{{"kind", "state"}, {"key", "open"}, {"op", "toggle"}, {"value", true}}})), side()), 1);
	REQUIRE(s.left);
	const json defs = rcg::in_shot_writes(*s.left, s.id);
	CHECK(defs == json::array({{{"key", "k"}, {"op", "set"}, {"value", 2}}, {{"key", "open"}, {"op", "toggle"}, {"value", true}}}));
}

TEST_CASE("ported: test_stage85_state_changes: test_state_change_starts_new_frame") {
	const std::vector<rcg::BuiltSurface> surfaces =
		rcg::build_level(rcg::load_level((ROOT / "data" / "levels" / "test_levels" / "flag_switches.json").string()));
	const rcg::BuiltSurface *tog = nullptr;
	for (const rcg::BuiltSurface &s : surfaces) {
		const rcg::BuiltSide *c = rcg::active_side(s, rcg::Side::Left, {});
		if (c != nullptr && !c->hits.empty()) {
			tog = &s;
		}
	}
	REQUIRE(tog != nullptr);
	// TestTrace: a fresh tracer with the game's switches (the infinity event on)
	rc::Tracer t;
	t.set_defs(rcg::defs_of(surfaces));
	const V2 player{700, 520}, cursor{1000, 530};
	const std::optional<rc::TracePair> pair = t.trace_pair(player, cursor, {}, {});
	REQUIRE(pair);
	REQUIRE(pair->physical.steps.size() > 1);
	REQUIRE(pair->planned.steps.size() > 1);
	CHECK(pair->physical.steps[0].frame_id == rc::IDENTITY_FRAME);
	CHECK(pair->physical.steps[1].frame_id != rc::IDENTITY_FRAME); // the toggle starts a new physical frame
	CHECK(pair->planned.steps[1].frame_id == rc::IDENTITY_FRAME);  // the planned trace passes the unplanned toggler
	const int ci = pair->planned.cursor_index >= 0 ? pair->planned.cursor_index : int(pair->planned.steps.size());
	const std::vector<rc::TraceStep> merged = rc::sweep::merge(pair->planned.steps, pair->physical.steps, ci);
	REQUIRE(merged.size() > 1);
	CHECK(merged[1].type != rc::StepType::Aligned); // diverged at the toggler
	// planned toggler: both toggle, aligned through the cursor
	const bool left = pair->physical.steps[0].hit_side == int(rc::Side::Left);
	rc::PlanItem item;
	item.surface_id = tog->id;
	item.left = left;
	const std::optional<rc::TracePair> pair2 = t.trace_pair(player, cursor, {item}, {});
	REQUIRE(pair2);
	CHECK(pair2->planned.cursor_index != -1); // the plan reaches the cursor
	const int ci2 = pair2->planned.cursor_index;
	const std::vector<rc::TraceStep> merged2 = rc::sweep::merge(pair2->planned.steps, pair2->physical.steps, ci2);
	for (int i = 0; i < ci2; i++) {
		CAPTURE(i);
		REQUIRE(size_t(i) < merged2.size());
		CHECK(merged2[size_t(i)].type == rc::StepType::Aligned);
	}
}
