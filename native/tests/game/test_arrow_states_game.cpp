// Stage 156 (GAME_SPEC 10.6, docs/TDD_61): the arrow's states in the game. Tags
// give elements and objects their part: flammable (fire passes between the
// arrow and it), conductive (they swap charge), frosted (the arrow takes
// frost); burning and charged start them so. Water puts the arrow's fire out;
// a frosted arrow that would be stopped is reflected or inverted, once.
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace {

using json = nlohmann::json;

rcg::Level level_of(const std::string &extra) {
	json lv = json::parse(R"({"schema": "rc-level/2", "player": {"start": [660, 540]}, "room": {"rect": [560, 240, 800, 600], "built": true},
		"elements": []})");
	const json e = json::parse(extra);
	for (auto it = e.begin(); it != e.end(); ++it) {
		lv[it.key()] = it.value();
	}
	return rcg::parse_level_json(lv);
}

void shoot(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.fire(-1, 1.0 / 60.0);
}

// the x of every fired hit of the current preview's physical walk
std::vector<float> hits_x(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	std::vector<float> out;
	for (const rc::HitRecord &h : s.physical_trace()->hits) {
		if (h.fired) {
			out.push_back(h.point.x);
		}
	}
	return out;
}

} // namespace

TEST_CASE("arrow states: fire -- a burning surface lights the arrow, a burning arrow lights what is flammable; water puts it out") {
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "torch", "name": "t", "start": [800, 300], "end": [800, 780]},
		{"kind": "wall", "name": "w", "tags": ["flammable"], "start": [1000, 300], "end": [1000, 780]}]})"));
	CHECK(s.flags().is_true("el:t:fire")); // burning from the start
	shoot(s, 1000, 540);
	CHECK(s.flags().is_true("el:w:fire"));
	CHECK_FALSE(s.flags().has("shot:fire")); // the arrow's fire ends with the shot
	// an unlit arrow touching a burning surface catches fire, and lights the next flammable one
	rcg::Session u(level_of(R"({"flags": {"el:a:fire": true}, "elements": [
		{"kind": "passthrough", "name": "a", "tags": ["flammable"], "start": [800, 300], "end": [800, 780]},
		{"kind": "passthrough", "name": "b", "tags": ["flammable"], "start": [900, 300], "end": [900, 780]}]})"));
	shoot(u, 1000, 540);
	CHECK(u.flags().is_true("el:b:fire"));
	// water between them puts the arrow out
	rcg::Session v(level_of(R"({"elements": [
		{"kind": "torch", "start": [800, 300], "end": [800, 780]},
		{"kind": "water", "start": [900, 300], "end": [900, 780]},
		{"kind": "wall", "name": "w", "tags": ["flammable"], "start": [1000, 300], "end": [1000, 780]}]})"));
	shoot(v, 1000, 540);
	CHECK_FALSE(v.flags().has("el:w:fire"));
}

TEST_CASE("arrow states: charge -- the arrow and a conductor swap it; a battery starts charged") {
	rcg::Session s(level_of(R"({"elements": [
		{"kind": "charger", "start": [750, 300], "end": [750, 780]},
		{"kind": "conductor", "name": "c", "start": [900, 300], "end": [900, 780]},
		{"kind": "battery", "name": "cell", "start": [1100, 300], "end": [1100, 780]}]})"));
	CHECK(s.flags().is_true("el:cell:charge"));
	shoot(s, 1100, 540); // charged by the charger, it gives the charge to c; uncharged, it takes the battery's
	CHECK(s.flags().is_true("el:c:charge"));
	CHECK(s.flags().get("el:cell:charge") != nullptr);
	CHECK_FALSE(s.flags().is_true("el:cell:charge"));
	// a conductive object that stops the arrow takes its charge (one passed through is touched twice: given, then taken back)
	rcg::Session o(level_of(R"({"objects": [{"name": "box", "kind": "static", "face": "wall", "circle": {"center": [1000, 540], "radius": 20},
		"tags": ["conductive"]}], "elements": [{"kind": "charger", "start": [750, 300], "end": [750, 780]}]})"));
	shoot(o, 1200, 540);
	CHECK(o.flags().is_true("el:box:charge"));
}

TEST_CASE("arrow states: frost -- a frosted arrow reflects off what would stop it, once; mirrors keep it; a shadow's edge still stops it") {
	{
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "frost_barrier", "start": [800, 300], "end": [800, 780]},
			{"kind": "wall", "start": [1000, 300], "end": [1000, 780]}]})"));
		const std::vector<float> xs = hits_x(s, 1000, 540);
		REQUIRE(xs.size() >= 3);
		CHECK(xs[1] == 1000); // reflected off the wall
		CHECK(xs[2] == 800);  // back through the barrier: frosted again
	}
	{
		// a frosted circle object: the arrow is frosted by it and keeps going (a pass), then the wall reflects it
		rcg::Session s(level_of(R"({"objects": [{"name": "ice", "kind": "static", "face": "pass", "circle": {"center": [900, 540], "radius": 20},
			"tags": ["frosted"]}], "elements": [{"kind": "wall", "start": [1100, 300], "end": [1100, 780]}]})"));
		const std::vector<float> xs = hits_x(s, 1100, 540);
		REQUIRE(xs.size() >= 3);
		CHECK(xs[2] == 1100);
	}
	{
		// a shadow's edge is no wall frost can turn: the frosted arrow stops at it
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "frost_barrier", "start": [800, 300], "end": [800, 780]},
			{"kind": "dark", "name": "night", "circle": {"center": [1150, 540], "radius": 120}}]})"));
		const std::vector<float> xs = hits_x(s, 1150, 540);
		REQUIRE(xs.size() == 2);
		CHECK(xs[1] == doctest::Approx(1030).epsilon(1e-4));
	}
}

TEST_CASE("arrow states: the tags' errors") {
	auto err = [](const char *extra) {
		try {
			level_of(extra);
		} catch (const rcg::LevelError &e) {
			return std::string(e.what());
		}
		return std::string("no error");
	};
	CHECK(err(R"({"elements": [{"kind": "wall", "tags": ["frosted"], "start": [800, 300], "end": [800, 700]}]})").find("cannot bear frost") != std::string::npos);
	CHECK(err(R"({"objects": [{"name": "o", "circle": {"center": [800, 500], "radius": 20}, "tags": ["frosted"]}]})").find("cannot bear frost")
			!= std::string::npos);
	CHECK(err(R"({"elements": [{"kind": "raw", "surface": {"start": [800, 300], "end": [800, 700], "via": [800, 500], "left": {"effect": {"type": "pass"},
		"hits": [{"kind": "target", "when": [{"key": "a", "values": ["true"]}]}]}}}]})").find("only a state write") != std::string::npos);
}

namespace {

// the preview's marks on the arrow's steps (aligned or the physical walk's own), and its consequences
std::pair<std::vector<uint8_t>, std::vector<std::pair<std::string, int>>> preview_marks(rcg::Session &s, double x, double y) {
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	return {s.preview_marks(), s.preview_becomes()};
}

} // namespace

TEST_CASE("arrow states: the preview marks each walk's fire over its own steps (Stage 160); what catches fire only before the walks diverge (Stage 157)") {
	{
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "torch", "start": [800, 300], "end": [800, 780]},
			{"kind": "wall", "name": "w", "tags": ["flammable"], "start": [1000, 300], "end": [1000, 780]}]})"));
		// unplanned, the torch's change diverges the walks (GAME_SPEC 14.2): the wall's fire is after it
		CHECK(preview_marks(s, 1000, 540).second.empty());
		s.set_cursor(rc::V2::of(800, 540)); // planned: the walks stay aligned to the wall
		s.settle();
		s.click(false);
		const auto [marks, becomes] = preview_marks(s, 1000, 540);
		CHECK(std::count_if(marks.begin(), marks.end(), [](uint8_t m) { return (m & 1) != 0; }) >= 1);
		REQUIRE(becomes.size() == 1);
		CHECK(becomes[0] == std::pair<std::string, int>{"w", 1});
		bool glow = false;
		for (const auto &n : s.draw_frame()) {
			glow = glow || n.key.kind == rcg::render::NodeKind::Glow;
		}
		CHECK(glow);
	}
	{
		// an unplanned mirror turns the arrow down through a torch onto a flammable floor: the walks diverge at the
		// mirror, so the fire begins after the divergence -- no marks, no consequence
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "two_sided_mirror", "start": [760, 500], "end": [840, 580]},
			{"kind": "torch", "start": [700, 700], "end": [900, 700]},
			{"kind": "wall", "name": "floor", "tags": ["flammable"], "start": [700, 800], "end": [900, 800]}]})"));
		const auto [marks, becomes] = preview_marks(s, 1000, 540);
		CHECK(becomes.empty()); // a world effect after the divergence: not shown
		// a path effect: each walk's own, over its own steps -- the physical walk burns after the torch, the planned one never
		const auto &steps = s.preview_steps();
		bool physical_fire = false, planned_fire = false;
		for (size_t i = 0; i < steps.size(); i++) {
			physical_fire = physical_fire || (steps[i].type == rc::StepType::DivergedPhysical && (marks[i] & 1));
			planned_fire = planned_fire || ((steps[i].type == rc::StepType::DivergedPlanned || steps[i].type == rc::StepType::DivergedPostPlanned) && (marks[i] & 1));
		}
		CHECK(physical_fire);
		CHECK_FALSE(planned_fire);
		shoot(s, 1000, 540); // (yet the shot does burn it)
		CHECK(s.flags().is_true("el:floor:fire"));
	}
	{
		// charge and frost marks
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "charger", "start": [750, 300], "end": [750, 780]},
			{"kind": "frost_barrier", "start": [850, 300], "end": [850, 780]}]})"));
		for (const double x : {750.0, 850.0}) { // (both planned: the walks stay aligned through them)
			s.set_cursor(rc::V2::of(x, 540));
			s.settle();
			s.click(false);
		}
		const auto [marks, becomes] = preview_marks(s, 1000, 540);
		CHECK(std::count_if(marks.begin(), marks.end(), [](uint8_t m) { return m == (2 | 4); }) >= 1);
	}
}

namespace {

// a click on the side at (x, y): whether it planned something
bool plan_click(rcg::Session &s, double x, double y) {
	const size_t before = s.plan().size();
	s.set_cursor(rc::V2::of(x, y));
	s.settle();
	s.click(false);
	return s.plan().size() == before + 1;
}

} // namespace

TEST_CASE("arrow states: a stopping side is plannable while the plan's frost is active; a shadow's edge never (Stage 158)") {
	const char *LEVEL = R"({"elements": [
		{"kind": "frost_barrier", "name": "f", "start": [800, 300], "end": [800, 780]},
		{"kind": "wall", "name": "w", "start": [1000, 300], "end": [1000, 780]},
		{"kind": "wall", "name": "w2", "start": [900, 250], "end": [1100, 250]}]})";
	{
		rcg::Session s(level_of(LEVEL));
		CHECK_FALSE(plan_click(s, 999, 540)); // no frost planned: a wall is not plannable
		CHECK(plan_click(s, 799, 540));       // the frost barrier (the side the arrow comes from)
		CHECK(plan_click(s, 999, 540));       // now the wall plans as a reflection
		CHECK_FALSE(plan_click(s, 1000, 251)); // the frost is used up: the next wall is not
		// the preview: both walks reflect off the wall (aligned through it)
		s.set_cursor(rc::V2::of(700, 400));
		s.settle();
		const auto &steps = s.preview_steps();
		REQUIRE(steps.size() >= 3);
		CHECK(steps[1].type == rc::StepType::Aligned);
		CHECK(steps[1].end.x == doctest::Approx(1000));
		CHECK(steps[2].type != rc::StepType::DivergedPlanned);
	}
	{
		// a shadow's edge is rigid: never plannable, frost or not
		rcg::Session s(level_of(R"({"elements": [
			{"kind": "frost_barrier", "name": "f", "start": [800, 300], "end": [800, 780]},
			{"kind": "dark", "name": "night", "circle": {"center": [1150, 540], "radius": 120}}]})"));
		CHECK(plan_click(s, 799, 540));
		CHECK_FALSE(plan_click(s, 1029, 540));
	}
	{
		// a corner of two walls hit exactly stops a frosted arrow (a shared point), frost or not
		rcg::Session s(level_of(R"({"flags": {"shot:frost": true}, "elements": [
			{"kind": "wall", "start": [1000, 540], "end": [1100, 440]},
			{"kind": "wall", "start": [1000, 540], "end": [1100, 640]}]})"));
		s.set_cursor(rc::V2::of(1050, 540));
		s.settle();
		const rc::TracePath *t = s.physical_trace();
		REQUIRE(t != nullptr);
		CHECK(t->steps.back().end == rc::V2::of(1000, 540)); // stopped at the corner
		CHECK(std::none_of(t->hits.begin(), t->hits.end(), [](const rc::HitRecord &h) { return h.fired; })); // nothing fired: the frost kept
	}
}
