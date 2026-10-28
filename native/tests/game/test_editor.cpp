// The level editor (rcg/editor.hpp, docs/EDITOR.md): its commands, its
// pointer, key and text input, its panel and its draw list -- the editor a host
// paints, driven here as a host drives it.
#include "rcg/canon.hpp"
#include "rcg/editor.hpp"
#include "rcg/level2.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <set>
#include <sstream>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const fs::path ROOT = RC_REPO_ROOT;

std::string read_text(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

json ok(rcg::Editor &ed, const json &cmd) {
	const json r = ed.command(cmd);
	INFO(cmd.dump() << " -> " << r.dump());
	REQUIRE(r.value("ok", false));
	return r;
}

const json &element(const rcg::Editor &ed, const std::string &name) {
	for (const json &e : ed.document()["elements"]) {
		if (e.value("name", "") == name) {
			return e;
		}
	}
	FAIL("no element " << name);
	static const json none;
	return none;
}

// A click (down, up) at a world point.
void click(rcg::Editor &ed, double x, double y, unsigned button = rcg::Editor::BtnLeft, unsigned mods = 0) {
	const rc::V2 s = ed.to_screen(rc::V2::of(x, y));
	ed.pointer(rcg::Editor::PtrMove, s.x, s.y, 0, mods);
	ed.pointer(rcg::Editor::PtrDown, s.x, s.y, button, mods);
	ed.pointer(rcg::Editor::PtrUp, s.x, s.y, button, mods);
}

// A drag from one world point to another.
void drag(rcg::Editor &ed, double x0, double y0, double x1, double y1, unsigned mods = 0) {
	const rc::V2 a = ed.to_screen(rc::V2::of(x0, y0)), b = ed.to_screen(rc::V2::of(x1, y1));
	ed.pointer(rcg::Editor::PtrMove, a.x, a.y, 0, mods);
	ed.pointer(rcg::Editor::PtrDown, a.x, a.y, rcg::Editor::BtnLeft, mods);
	ed.pointer(rcg::Editor::PtrMove, (a.x + b.x) / 2, (a.y + b.y) / 2, rcg::Editor::BtnLeft, mods);
	ed.pointer(rcg::Editor::PtrMove, b.x, b.y, rcg::Editor::BtnLeft, mods);
	ed.pointer(rcg::Editor::PtrUp, b.x, b.y, rcg::Editor::BtnLeft, mods);
}

// A click on the panel row whose label is `label`.
void click_row(rcg::Editor &ed, const std::string &label, unsigned mods = 0) {
	const json panel = ok(ed, {{"op", "panel"}}); // (kept: a range over a temporary's member dangles)
	for (const json &r : panel["rows"]) {
		if (r["label"] == label) {
			const float y = r["y"].get<float>() + 10, x = r["x"].get<float>() + 150;
			ed.pointer(rcg::Editor::PtrMove, x, y, 0, mods);
			ed.pointer(rcg::Editor::PtrDown, x, y, rcg::Editor::BtnLeft, mods);
			ed.pointer(rcg::Editor::PtrUp, x, y, rcg::Editor::BtnLeft, mods);
			return;
		}
	}
	FAIL("no panel row " << label);
}

} // namespace

TEST_CASE("editor: every level loads and saves byte for byte") {
	int n = 0;
	for (const auto &e : fs::recursive_directory_iterator(ROOT / "data" / "levels")) {
		if (e.path().extension() != ".json") {
			continue;
		}
		CAPTURE(e.path().string());
		const std::string text = read_text(e.path());
		const json doc = json::parse(text);
		rcg::Editor ed(&doc);
		CHECK(ed.document_text() == text);
		CHECK(ed.errors().empty());
		CHECK_FALSE(ed.status().dirty);
		CHECK_FALSE(ed.draw().empty());
		n++;
	}
	CHECK(n == std::distance(fs::directory_iterator(ROOT / "bench" / "lowered"), fs::directory_iterator())); // a level per committed lowering (make lowered)
	CHECK(n > 40);
	// an rc-level/1 document is migrated
	const json v1 = json::parse(read_text(ROOT / "bench" / "lowered" / "test_levels__flag_switches.json"));
	rcg::Editor ed(&v1);
	CHECK(ed.document_text() == read_text(ROOT / "data" / "levels" / "test_levels" / "flag_switches.json"));
}

TEST_CASE("editor: commands edit the document, undo and redo restore it") {
	rcg::Editor ed;
	CHECK(ed.document()["schema"] == "rc-level/2");
	CHECK(ed.errors().empty());
	const std::string empty = ed.document_text();
	CHECK(ok(ed, {{"op", "add"}, {"element", {{"kind", "door"}, {"start", {900.0, 300.0}}, {"end", {900.0, 500.0}}}}})["name"] == "door_1");
	CHECK(ok(ed, {{"op", "add"}, {"element", {{"kind", "button"}, {"opens", "door_1"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}}}})["name"]
			== "button_1");
	CHECK(ed.status().dirty);
	CHECK(ed.errors().empty());
	// a rename carries the buttons that open the door
	ok(ed, {{"op", "rename"}, {"name", "door_1"}, {"to", "gate"}});
	CHECK(element(ed, "button_1")["opens"] == "gate");
	CHECK(ed.command({{"op", "rename"}, {"name", "gate"}, {"to", "button_1"}})["ok"] == false);
	ok(ed, {{"op", "set"}, {"name", "gate"}, {"key", "closed"}, {"value", "mirror"}});
	CHECK(element(ed, "gate")["closed"] == "mirror");
	ok(ed, {{"op", "set"}, {"name", "gate"}, {"key", "closed"}, {"value", nullptr}});
	CHECK_FALSE(element(ed, "gate").contains("closed"));
	ok(ed, {{"op", "set"}, {"path", "/flags"}, {"value", {{"door:gate", true}}}});
	CHECK(ed.document()["flags"]["door:gate"] == true);
	ok(ed, {{"op", "move"}, {"name", "gate"}, {"handle", "body"}, {"by", {10.0, 0.0}}});
	CHECK(element(ed, "gate")["start"] == json::array({910.0, 300.0}));
	CHECK(ok(ed, {{"op", "duplicate"}, {"name", "gate"}})["name"] == "door_1");
	CHECK(element(ed, "door_1")["start"] == json::array({930.0, 320.0}));
	ok(ed, {{"op", "remove"}, {"name", "door_1"}});
	const std::string full = ed.document_text();
	// the level the editor holds is a level: it lowers and builds
	CHECK_NOTHROW(rcg::build_level(rcg::parse_level(full)));
	for (int k = 0; k < 9; k++) {
		ok(ed, {{"op", "undo"}});
	}
	CHECK(ed.document_text() == empty);
	CHECK(ed.command({{"op", "undo"}})["ok"] == false);
	for (int k = 0; k < 9; k++) {
		ok(ed, {{"op", "redo"}});
	}
	CHECK(ed.document_text() == full);
	// errors are named
	CHECK(ed.command({{"op", "remove"}, {"name", "nothing"}})["error"] == "no element named 'nothing'");
	CHECK(ed.command({{"op", "fly"}})["ok"] == false);
	// the save handshake: the host writes the document, then says so
	ok(ed, {{"op", "save"}});
	CHECK(ed.status().save_requested);
	ok(ed, {{"op", "saved"}});
	CHECK_FALSE(ed.status().dirty);
	CHECK_FALSE(ed.status().save_requested);
}

TEST_CASE("editor: the pointer places elements on the grid and on other points exactly") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "kind"}, {"kind", "mirror"}});
	ed.key('L', 0, true);
	click(ed, 700, 300);
	click(ed, 702, 698); // the grid: (700, 700)
	REQUIRE(ed.document()["elements"].size() == 1);
	const json m = ed.document()["elements"][0];
	CHECK(m["kind"] == "mirror");
	CHECK(m["start"] == json::array({700.0, 300.0}));
	CHECK(m["end"] == json::array({700.0, 700.0}));
	CHECK_FALSE(m.contains("via")); // a line: its middle
	// an arc: start, end, via
	ok(ed, {{"op", "kind"}, {"kind", "two_sided_mirror"}});
	ed.key('A', 0, true);
	click(ed, 900, 300);
	click(ed, 900, 500);
	click(ed, 950, 400);
	REQUIRE(ed.document()["elements"].size() == 2);
	CHECK(ed.document()["elements"][1]["via"] == json::array({950.0, 400.0}));
	// a circle, and an inversion refused on a line
	ok(ed, {{"op", "kind"}, {"kind", "inversion"}});
	ed.key('L', 0, true);
	click(ed, 1000, 300);
	click(ed, 1100, 300);
	CHECK(ed.document()["elements"].size() == 2);
	CHECK(ok(ed, {{"op", "state"}})["hint"] == "inversion needs an arc or a circle (A, C)");
	ed.key('C', 0, true);
	click(ed, 1000, 600);
	click(ed, 1050, 600);
	REQUIRE(ed.document()["elements"].size() == 3);
	CHECK(ed.document()["elements"][2]["circle"] == json::parse(R"({"center": [1000.0, 600.0], "radius": 50.0})"));
	// a point off the grid, then a line from near it: the same point, exactly (provenance, not the grid)
	ok(ed, {{"op", "add"}, {"element", {{"kind", "wall"}, {"start", {600.25, 450.5}}, {"end", {600.25, 560.0}}}}});
	ok(ed, {{"op", "kind"}, {"kind", "wall"}});
	ed.key('L', 0, true);
	click(ed, 602, 452);
	click(ed, 640, 450);
	const json w = ed.document()["elements"].back();
	CHECK(w["start"] == json::array({600.25, 450.5}));
	CHECK(w["end"] == json::array({640.0, 450.0}));
	// Alt: no snapping at all; right click cancels a placement
	click(ed, 300, 300, rcg::Editor::BtnLeft);
	CHECK(ok(ed, {{"op", "state"}})["placing"] == 1);
	click(ed, 300, 300, rcg::Editor::BtnRight);
	CHECK(ok(ed, {{"op", "state"}})["placing"] == 0);
	// the player's start
	ed.key('P', 0, true);
	click(ed, 640, 640);
	CHECK(ed.document()["player"]["start"] == json::array({640.0, 640.0}));
	CHECK(ed.errors().empty());
}

TEST_CASE("editor: dragging handles and bodies") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "add"}, {"element", {{"kind", "two_sided_mirror"}, {"name", "m"}, {"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}}}});
	ok(ed, {{"op", "select"}, {"name", "m"}});
	const size_t undo0 = ok(ed, {{"op", "state"}})["undo"];
	// the middle handle of a line: dragging it bends the line into an arc
	drag(ed, 700, 500, 760, 500);
	CHECK(element(ed, "m")["via"] == json::array({760.0, 500.0}));
	// an endpoint moves; the via stays where it is (GAME_SPEC 19.2)
	drag(ed, 700, 700, 700, 800);
	CHECK(element(ed, "m")["end"] == json::array({700.0, 800.0}));
	CHECK(element(ed, "m")["via"] == json::array({760.0, 500.0}));
	// the body moves by whole grid steps (a point on the arc: the circle through the three points has its
	// center at (230, 550), radius^2 283400)
	drag(ed, 230 + std::sqrt(283400.0 - 150 * 150), 400, 253 + std::sqrt(283400.0 - 150 * 150), 412);
	CHECK(element(ed, "m")["start"] == json::array({720.0, 310.0}));
	CHECK(element(ed, "m")["via"] == json::array({780.0, 510.0}));
	CHECK(ok(ed, {{"op", "state"}})["undo"] == undo0 + 3);
	// a click without a move is no edit
	click(ed, 720, 310);
	CHECK(ok(ed, {{"op", "state"}})["undo"] == undo0 + 3);
	ok(ed, {{"op", "undo"}});
	CHECK(element(ed, "m")["start"] == json::array({700.0, 300.0}));
	// a click on nothing deselects; on the element selects it
	click(ed, 1200, 800);
	CHECK(ok(ed, {{"op", "state"}})["selected"].is_null());
	click(ed, 230 + std::sqrt(283400.0 - 200 * 200), 350);
	CHECK(ok(ed, {{"op", "state"}})["selected"] == "m");
	// the player's start is a handle too
	drag(ed, 960, 540, 1000, 540);
	CHECK(ed.document()["player"]["start"] == json::array({1000.0, 540.0}));
}

TEST_CASE("editor: keys") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "add"}, {"element", {{"kind", "wall"}, {"name", "w"}, {"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}}}});
	ok(ed, {{"op", "select"}, {"name", "w"}});
	ed.key(rcg::Editor::KeyRight, 0, true);
	CHECK(element(ed, "w")["start"] == json::array({710.0, 300.0}));
	ed.key(rcg::Editor::KeyUp, rcg::Editor::ModShift, true);
	CHECK(element(ed, "w")["start"] == json::array({710.0, 200.0}));
	ed.key('D', rcg::Editor::ModCtrl, true);
	CHECK(ed.document()["elements"].size() == 2);
	ed.key(rcg::Editor::KeyDelete, 0, true);
	CHECK(ed.document()["elements"].size() == 1);
	ed.key('Z', rcg::Editor::ModCtrl, true);
	CHECK(ed.document()["elements"].size() == 2);
	ed.key('Z', rcg::Editor::ModCtrl | rcg::Editor::ModShift, true);
	CHECK(ed.document()["elements"].size() == 1);
	const std::string kind0 = ok(ed, {{"op", "state"}})["kind"];
	ed.key(rcg::Editor::KeyTab, 0, true);
	CHECK(ok(ed, {{"op", "state"}})["kind"] != kind0);
	ed.key(rcg::Editor::KeyTab, rcg::Editor::ModShift, true);
	CHECK(ok(ed, {{"op", "state"}})["kind"] == kind0);
	ed.key('2', 0, true);
	CHECK(ok(ed, {{"op", "state"}})["kind"] == "arrow_block");
	ed.key('G', 0, true);
	CHECK(ok(ed, {{"op", "state"}})["grid"] == false);
	ed.key('S', rcg::Editor::ModCtrl, true);
	CHECK(ed.status().save_requested);
	// F5 plays (the document is valid), Esc stops; while playing nothing edits and nothing is drawn
	ed.key(rcg::Editor::KeyF1 + 4, 0, true);
	CHECK(ed.status().mode == rcg::Editor::Mode::Play);
	CHECK(ed.draw().empty());
	CHECK(ed.command({{"op", "remove"}, {"name", "w"}})["error"] == "playing: stop first");
	ed.key(rcg::Editor::KeyEscape, 0, true);
	CHECK(ed.status().mode == rcg::Editor::Mode::Edit);
}

TEST_CASE("editor: problems are named, drawn and block a play-test") {
	rcg::Editor ed;
	ok(ed, {{"op", "add"}, {"element", {{"kind", "inversion"}, {"name", "lens"}, {"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}}}});
	REQUIRE_FALSE(ed.errors().empty()); // one per side
	for (const auto &e : ed.errors()) {
		CHECK(e.element == 0);
		CHECK(e.name == "lens");
		CHECK(e.message.find("inversion needs a circle carrier") != std::string::npos);
	}
	CHECK(ed.command({{"op", "play"}})["ok"] == false);
	const size_t before = ed.errors().size();
	ok(ed, {{"op", "add"}, {"element", {{"kind", "crystal"}, {"start", {800.0, 300.0}}, {"end", {800.0, 700.0}}}}});
	REQUIRE(ed.errors().size() == before + 1);
	CHECK(ed.errors().back().message.find("unknown element kind 'crystal'") != std::string::npos);
	ok(ed, {{"op", "set"}, {"name", "lens"}, {"key", "via"}, {"value", {760.0, 500.0}}});
	ok(ed, {{"op", "remove"}, {"name", "crystal_1"}});
	CHECK(ed.errors().empty());
	const json played = ok(ed, {{"op", "play"}});
	CHECK(played["from"] == json::array({960.0, 540.0}));
}

TEST_CASE("editor: the panel edits the selected element and the level") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "add"}, {"element", {{"kind", "mirror"}, {"name", "m"}, {"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}}}});
	ok(ed, {{"op", "select"}, {"name", "m"}});
	click_row(ed, "back"); // an enum cycles
	CHECK(element(ed, "m")["back"] == "block");
	click_row(ed, "flip"); // a bool toggles
	CHECK(element(ed, "m")["flip"] == true);
	click_row(ed, "label"); // a typed field: focus, text, Enter
	CHECK(ok(ed, {{"op", "state"}})["focus"] == "label");
	ed.text("Lens");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(element(ed, "m")["label"] == "Lens");
	click_row(ed, "name");
	for (int k = 0; k < 4; k++) {
		ed.key(rcg::Editor::KeyBackspace, 0, true); // "m" and more: empty
	}
	ed.text("front");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(element(ed, "front")["kind"] == "mirror");
	click_row(ed, "end");
	ed.key(rcg::Editor::KeyEscape, 0, true); // Esc cancels the field
	CHECK(element(ed, "front")["end"] == json::array({700.0, 700.0}));
	// the level's fields when nothing is selected
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	click_row(ed, "room built");
	CHECK(ed.document()["room"]["built"] == false);
	click_row(ed, "gravity");
	ed.text("[0, 980]");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(ed.document()["gravity"] == json::array({0, 980}));
	// the place row cycles what the tools place
	const std::string kind0 = ok(ed, {{"op", "state"}})["kind"];
	click_row(ed, "place");
	CHECK(ok(ed, {{"op", "state"}})["kind"] != kind0);
	// a problem's row selects its element
	ok(ed, {{"op", "set"}, {"name", "front"}, {"key", "back"}, {"value", "maybe"}});
	REQUIRE(ed.errors().size() == 1);
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	const json panel = ok(ed, {{"op", "panel"}});
	for (const json &r : panel["rows"]) {
		if (r["kind"] == "error") {
			const float y = r["y"].get<float>() + 10, x = r["x"].get<float>() + 50;
			ed.pointer(rcg::Editor::PtrDown, x, y, rcg::Editor::BtnLeft, 0);
			ed.pointer(rcg::Editor::PtrUp, x, y, rcg::Editor::BtnLeft, 0);
			break;
		}
	}
	CHECK(ok(ed, {{"op", "state"}})["selected"] == "front");
}

TEST_CASE("editor: the draw list") {
	const json doc = json::parse(read_text(ROOT / "data" / "levels" / "test_levels" / "flag_switches.json"));
	rcg::Editor ed(&doc);
	ed.resize(1600, 900);
	const std::vector<rcg::render::FrameNode> a = ed.draw();
	std::set<std::string> keys;
	bool title = false;
	for (const auto &n : a) {
		CHECK(n.key.kind == rcg::render::NodeKind::Editor);
		keys.insert(n.key.json().dump());
		for (const auto &c : *n.calls) {
			title = title || c.text == "Ricochet level editor";
		}
	}
	CHECK(title);
	CHECK(keys.size() == a.size()); // one node per key
	CHECK(keys.count("[\"editor\",3,2]") == 1); // the third element
	// nothing changed: the same versions (a host repaints nothing)
	const std::vector<rcg::render::FrameNode> b = ed.draw();
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		CHECK(a[i].version == b[i].version);
	}
	// zoom about the pointer: the world point under it stays
	const rc::V2 at{500, 400};
	ed.pointer(rcg::Editor::PtrMove, at.x, at.y, 0, 0);
	const rc::V2 w0 = ed.to_world(at);
	ed.scroll(0, -3, 0);
	const rc::V2 w1 = ed.to_world(at);
	CHECK(w1.x == doctest::Approx(w0.x).epsilon(1e-4));
	CHECK(w1.y == doctest::Approx(w0.y).epsilon(1e-4));
}

TEST_CASE("editor: the goal and the detector tools (Stage 145)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ed.key('O', 0, true);
	click(ed, 900, 400);
	click(ed, 800, 300); // any two corners
	CHECK(ed.document()["objective"]["goal"] == json::parse(R"({"rect": [800.0, 300.0, 100.0, 100.0]})"));
	ed.key('D', 0, true);
	click(ed, 600, 600);
	click(ed, 650, 660);
	REQUIRE(ed.document()["detectors"].size() == 1);
	CHECK(ed.document()["detectors"][0] == json::parse(R"({"mode": "enter", "name": "detector_1", "region": {"rect": [600.0, 600.0, 50.0, 60.0]}})"));
	// a door the detector opens, a goal that needs it
	ok(ed, {{"op", "add"}, {"element", {{"kind", "door"}, {"flag", "det:detector_1"}, {"start", {750.0, 240.0}}, {"end", {750.0, 840.0}}}}});
	ok(ed, {{"op", "set"}, {"path", "/objective/requires"}, {"value", json::parse(R"([{"key": "det:detector_1", "values": ["true"]}])")}});
	CHECK(ed.errors().empty());
	std::vector<std::string> texts;
	for (const auto &n : ed.draw()) {
		for (const auto &c : *n.calls) {
			texts.push_back(c.text);
		}
	}
	CHECK(std::find(texts.begin(), texts.end(), "goal") != texts.end());
	CHECK(std::find(texts.begin(), texts.end(), "detector_1") != texts.end());
	// a goal level's mistakes are named
	ok(ed, {{"op", "set"}, {"path", "/objective/goal"}, {"value", json::parse(R"({"rect": [0, 0, 0, 5]})")}});
	REQUIRE(ed.errors().size() == 1);
	CHECK(ed.errors()[0].message.find("level.objective.goal.rect: a positive width and height expected") != std::string::npos);
}

TEST_CASE("editor: the object tool (Stage 146)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ed.key('B', 0, true);
	click(ed, 1100, 540);
	REQUIRE(ed.document()["objects"].size() == 1);
	CHECK(ed.document()["objects"][0] == json::parse(R"({"circle": {"center": [1100.0, 540.0], "radius": 16.0}, "name": "object_1"})"));
	CHECK(ed.errors().empty());
	bool drawn = false;
	for (const auto &n : ed.draw()) {
		for (const auto &c : *n.calls) {
			drawn = drawn || c.text == "object_1";
		}
	}
	CHECK(drawn);
}

TEST_CASE("editor: the goal, detectors and objects are selected, dragged and edited like elements (Stage 148)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "set"}, {"path", "/objective"}, {"value", json::parse(R"({"goal": {"rect": [800, 300, 100, 100]},
		"requires": [{"key": "det:detector_1", "values": ["true"]}]})")}});
	ok(ed, {{"op", "set"}, {"path", "/detectors"}, {"value", json::parse(R"([{"name": "detector_1", "region": {"rect": [600, 600, 60, 60]}, "mode": "enter"}])")}});
	ok(ed, {{"op", "set"}, {"path", "/objects"}, {"value", json::parse(R"([{"name": "object_1", "circle": {"center": [1100, 540], "radius": 16}}])")}});
	ok(ed, {{"op", "add"}, {"element", {{"kind", "door"}, {"name", "gate"}, {"flag", "det:detector_1"}, {"start", {1000.0, 240.0}}, {"end", {1000.0, 840.0}}}}});
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	// the goal: a click inside selects it; its corner and its body drag
	click(ed, 850, 350);
	CHECK(ok(ed, {{"op", "state"}})["item"] == "goal");
	drag(ed, 900, 400, 950, 420); // the bottom-right corner
	CHECK(ed.document()["objective"]["goal"]["rect"] == json::parse("[800.0, 300.0, 150.0, 120.0]"));
	drag(ed, 850, 350, 883, 372); // the body, by whole grid steps
	CHECK(ed.document()["objective"]["goal"]["rect"] == json::parse("[830.0, 320.0, 150.0, 120.0]"));
	click_row(ed, "shape"); // a rect -> the circle in it
	CHECK(ed.document()["objective"]["goal"] == json::parse(R"({"circle": {"center": [905.0, 380.0], "radius": 60.0}})"));
	drag(ed, 965, 380, 985, 380); // its radius handle
	CHECK(ed.document()["objective"]["goal"]["circle"]["radius"] == 80.0);
	// a detector: options cycle; a rename carries det:<name> to the door and the goal
	click(ed, 630, 630);
	CHECK(ok(ed, {{"op", "state"}})["item"] == "detector:detector_1");
	click_row(ed, "mode");
	CHECK(ed.document()["detectors"][0]["mode"] == "toggle");
	click_row(ed, "name");
	for (int k = 0; k < 12; k++) {
		ed.key(rcg::Editor::KeyBackspace, 0, true);
	}
	ed.text("plate");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(ed.document()["detectors"][0]["name"] == "plate");
	CHECK(element(ed, "gate")["flag"] == "det:plate");
	CHECK(ed.document()["objective"]["requires"][0]["key"] == "det:plate");
	CHECK(ok(ed, {{"op", "state"}})["item"] == "detector:plate");
	CHECK(ed.errors().empty());
	// an object: its kind cycles, ctrl+D duplicates it, Delete removes the copy, arrows move it
	click(ed, 1100, 540);
	CHECK(ok(ed, {{"op", "state"}})["item"] == "object:object_1");
	click_row(ed, "kind");
	CHECK(ed.document()["objects"][0]["kind"] == "static");
	ed.key('D', rcg::Editor::ModCtrl, true);
	REQUIRE(ed.document()["objects"].size() == 2);
	CHECK(ed.document()["objects"][1]["circle"]["center"] == json::parse("[1120.0, 560.0]"));
	CHECK(ok(ed, {{"op", "state"}})["item"] == "object:object_2");
	ed.key(rcg::Editor::KeyDelete, 0, true);
	CHECK(ed.document()["objects"].size() == 1);
	click(ed, 1100, 540);
	ed.key(rcg::Editor::KeyRight, 0, true);
	CHECK(ed.document()["objects"][0]["circle"]["center"] == json::parse("[1110.0, 540.0]"));
	// an element above an item wins the click
	click(ed, 1000, 400);
	CHECK(ok(ed, {{"op", "state"}})["selected"] == "gate");
	CHECK(ok(ed, {{"op", "state"}})["item"].is_null());
	// the goal removed, and back by undo
	ok(ed, {{"op", "select"}, {"item", "goal"}});
	ed.key(rcg::Editor::KeyDelete, 0, true);
	CHECK_FALSE(ed.document().contains("objective"));
	ok(ed, {{"op", "undo"}});
	CHECK(ed.document()["objective"]["goal"]["circle"]["radius"] == 80.0);
	CHECK(ed.command({{"op", "duplicate"}, {"item", "goal"}})["ok"] == false);
	CHECK(ed.command({{"op", "select"}, {"item", "detector:nobody"}})["error"] == "no item 'detector:nobody'");
}

TEST_CASE("editor: carriers -- placed, built on, dragged, renamed, deleted (Stage 149)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ed.key('K', 0, true); // a line carrier: two clicks
	click(ed, 600, 700);
	click(ed, 1200, 700);
	REQUIRE(ed.document()["carriers"].size() == 1);
	CHECK(ed.document()["carriers"][0] == json::parse(R"({"line": {"a": [600.0, 700.0], "b": [1200.0, 700.0]}, "name": "carrier_1"})"));
	// a wall on it: both line clicks land on the carrier
	ok(ed, {{"op", "kind"}, {"kind", "wall"}});
	ed.key('L', 0, true);
	click(ed, 700, 704);
	click(ed, 900, 696);
	REQUIRE(ed.document()["elements"].size() == 1);
	const json w = ed.document()["elements"][0];
	CHECK(w["carrier"] == "carrier_1");
	CHECK(w["from"] == json::parse("[700.0, 700.0]"));
	CHECK(w["to"] == json::parse("[900.0, 700.0]"));
	CHECK(ed.errors().empty());
	// the carrier's point dragged: the wall follows (it lowers onto the moved line)
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	ed.key('V', 0, true);
	click(ed, 1000, 700); // the carrier, past the wall
	CHECK(ok(ed, {{"op", "state"}})["item"] == "carrier:carrier_1");
	drag(ed, 1200, 700, 1200, 900); // its b
	const json low = ok(ed, {{"op", "lower"}})["level"];
	CHECK(low["surfaces"][4]["end"][1].get<double>() > 700.0); // (y rises along the tilted line)
	// renamed: the wall follows; deleted: the wall keeps its place on its own
	ok(ed, {{"op", "rename"}, {"item", "carrier:carrier_1"}, {"to", "floor"}});
	CHECK(ed.document()["elements"][0]["carrier"] == "floor");
	const json before = ok(ed, {{"op", "lower"}})["level"]["surfaces"][4];
	ok(ed, {{"op", "remove"}, {"item", "carrier:floor"}});
	CHECK_FALSE(ed.document()["elements"][0].contains("carrier"));
	CHECK(ok(ed, {{"op", "lower"}})["level"]["surfaces"][4] == before);
	CHECK(ed.errors().empty());
	// a circle carrier, and an arc on it by three clicks
	ed.key('K', rcg::Editor::ModShift, true);
	click(ed, 900, 400);
	click(ed, 1000, 400);
	REQUIRE(ed.document()["carriers"].size() == 1);
	CHECK(ed.document()["carriers"][0]["circle"] == json::parse(R"({"center": [900.0, 400.0], "radius": 100.0})"));
	ok(ed, {{"op", "kind"}, {"kind", "mirror"}});
	ed.key('A', 0, true);
	click(ed, 1000, 402);
	click(ed, 900, 302);
	click(ed, 971, 330);
	const json arc = ed.document()["elements"].back();
	CHECK(arc["carrier"] == "carrier_1");
	CHECK(arc.contains("via"));
	CHECK(ed.errors().empty());
}

TEST_CASE("editor: an element's kind picked from the palette") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "add"}, {"element", {{"kind", "wall"}, {"name", "w"}, {"start", {700.0, 300.0}}, {"end", {700.0, 700.0}}}}});
	click_row(ed, "kind");
	// the palette's first row holds wall, arrow_block, fence: the third column is fence; it comes after the place row
	const json panel = ok(ed, {{"op", "panel"}});
	float y = -1;
	bool next = false;
	for (const json &r : panel["rows"]) {
		if (next) {
			y = r["y"].get<float>() + 10;
			break;
		}
		next = r["label"] == "place";
	}
	REQUIRE(y > 0);
	const float x = 1920 - rcg::Editor::PANEL_WIDTH + 10 + 2.5f * (rcg::Editor::PANEL_WIDTH - 20) / 3;
	ed.pointer(rcg::Editor::PtrDown, x, y, rcg::Editor::BtnLeft, 0);
	ed.pointer(rcg::Editor::PtrUp, x, y, rcg::Editor::BtnLeft, 0);
	CHECK(element(ed, "w")["kind"] == "fence");
	CHECK(ok(ed, {{"op", "state"}})["kind"] == "wall"); // what the tools place did not change
}

TEST_CASE("editor: a moving element shows its keys faint; its motion is a panel field (Stage 151)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "add"}, {"element", json::parse(R"({"kind": "wall", "name": "w", "start": [700, 400], "end": [700, 600]})")}});
	size_t before = 0;
	for (const auto &n : ed.draw()) {
		if (n.key.a == 3) {
			before = n.calls->size();
		}
	}
	click_row(ed, "motion");
	ed.text(R"({"keys": [[0, 0, 0, 0], [0.5, 100, 0, 0]]})");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(element(ed, "w")["motion"]["keys"][1][1] == 100);
	CHECK(ed.errors().empty());
	size_t after = 0;
	for (const auto &n : ed.draw()) {
		if (n.key.a == 3) {
			after = n.calls->size();
		}
	}
	CHECK(after > before); // the key's faint copy
}

TEST_CASE("editor: an element's tags and the level's spread rules are panel fields (Stage 152)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "add"}, {"element", json::parse(R"({"kind": "wall", "name": "w", "start": [700, 400], "end": [700, 600]})")}});
	click_row(ed, "tags");
	ed.text(R"(["conductive", "powered"])");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(element(ed, "w")["tags"] == json::array({"conductive", "powered"}));
	CHECK(ed.errors().empty());
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	click_row(ed, "spread");
	ed.text(R"([{"flag": "glow", "tag": "conductive"}])");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK(ok(ed, {{"op", "document"}})["document"]["spread"][0]["flag"] == "glow");
	CHECK(ed.errors().empty());
	click_row(ed, "spread");
	ed.text(R"([{"flag": "glow"}])"); // no tag: the level's problem, shown
	ed.key(rcg::Editor::KeyEnter, 0, true);
	CHECK_FALSE(ed.errors().empty());
}

TEST_CASE("editor: shades are drawn in their colours over a faint shadow (Stage 153)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	auto shadow = [&] {
		for (const auto &n : ed.draw()) {
			if (n.key.a == 11 && !n.calls->empty() && n.calls->front().kind == rcg::render::Kind::Triangles) {
				return true;
			}
		}
		return false;
	};
	CHECK_FALSE(shadow());
	ok(ed, {{"op", "add"}, {"element", json::parse(R"({"kind": "dark", "name": "night", "start": [1000, 240], "end": [1000, 840]})")}});
	CHECK(ed.errors().empty());
	CHECK(shadow());
	ok(ed, {{"op", "add"}, {"element", json::parse(R"({"kind": "light", "name": "sun", "start": [1000, 240], "end": [1000, 840]})")}});
	CHECK_FALSE(shadow()); // the same line and side: the light covers the dark
	ok(ed, {{"op", "undo"}});
	CHECK(shadow());
}

TEST_CASE("editor: the level menu -- listed by the host, opened by name, previous and next, unsaved changes asked again (Stage 155)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	CHECK_FALSE(ed.status().open_requested);
	ok(ed, {{"op", "levels"}, {"list", {"a/one", "a/two", "b/three"}}, {"current", "a/two"}});
	CHECK(ok(ed, {{"op", "state"}})["level"] == "a/two");
	// the list: the nav row's middle opens it, a level row asks for that level
	click_row(ed, "a/two"); // (the nav row shows the open level)
	CHECK(ok(ed, {{"op", "state"}})["level_menu"] == true);
	click_row(ed, "b/three");
	CHECK(ed.status().open_requested);
	CHECK(ok(ed, {{"op", "open_request"}})["name"] == "b/three");
	CHECK_FALSE(ed.status().open_requested);
	ok(ed, {{"op", "load"}, {"text", R"({"schema": "rc-level/2", "elements": []})"}, {"name", "b/three"}});
	CHECK(ok(ed, {{"op", "state"}})["level"] == "b/three");
	CHECK(ok(ed, {{"op", "state"}})["level_menu"] == false);
	// next wraps around; ctrl+left goes back
	CHECK(ok(ed, {{"op", "next_level"}, {"by", 1}})["name"] == "a/one");
	ok(ed, {{"op", "open_request"}});
	ed.key(rcg::Editor::KeyLeft, rcg::Editor::ModCtrl, true);
	CHECK(ok(ed, {{"op", "open_request"}})["name"] == "a/two");
	// unsaved changes: the first ask is refused, the second opens
	ok(ed, {{"op", "add"}, {"element", json::parse(R"({"kind": "wall", "start": [700, 400], "end": [700, 600]})")}});
	CHECK(ed.status().dirty);
	const json first = ed.command({{"op", "open"}, {"name", "a/one"}});
	CHECK(first["ok"] == false);
	CHECK(first["error"].get<std::string>().find("unsaved") != std::string::npos);
	CHECK_FALSE(ed.status().open_requested);
	CHECK(ok(ed, {{"op", "open"}, {"name", "a/one"}})["name"] == "a/one");
	CHECK(ed.status().open_requested);
}

TEST_CASE("editor: every tool is a chip in the panel; a click uses it (Stage 161)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	const json panel = ok(ed, {{"op", "panel"}});
	std::vector<json> rows;
	for (const json &r : panel["rows"]) {
		if (r["kind"] == "tools") {
			rows.push_back(r);
		}
	}
	REQUIRE(rows.size() == 4); // ten tools, three to a row
	const float x = rows[0]["x"].get<float>(), pw = 1920.0f + 20.0f - x, x0 = 1920.0f - pw + 10.0f, cw = (pw - 20.0f) / 3.0f;
	auto click = [&](const json &row, int column) {
		const float px = x0 + cw * (float(column) + 0.5f), py = row["y"].get<float>() + 10.0f;
		ed.pointer(rcg::Editor::PtrMove, px, py, 0, 0);
		ed.pointer(rcg::Editor::PtrDown, px, py, rcg::Editor::BtnLeft, 0);
		ed.pointer(rcg::Editor::PtrUp, px, py, rcg::Editor::BtnLeft, 0);
	};
	click(rows[0], 2);
	CHECK(ok(ed, {{"op", "state"}})["tool"] == "arc");
	click(rows[2], 0);
	CHECK(ok(ed, {{"op", "state"}})["tool"] == "detector");
	click(rows[3], 0);
	CHECK(ok(ed, {{"op", "state"}})["tool"] == "carrier_circle");
}

TEST_CASE("editor: colliders and the camera are items -- placed, selected, dragged, duplicated, removed (Stage 164)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	auto doc = [&] { return ok(ed, {{"op", "document"}})["document"]; };
	// X, two clicks: a collider
	ok(ed, {{"op", "tool"}, {"tool", "select"}});
	ed.key('X', 0, true);
	CHECK(ok(ed, {{"op", "state"}})["tool"] == "collider");
	ok(ed, {{"op", "set"}, {"path", "/colliders"}, {"value", json::parse(R"([{"a": [600, 700], "b": [900, 700]}])")}});
	REQUIRE(doc()["colliders"].size() == 1);
	// the item: selected, its end moved, duplicated, removed
	ok(ed, {{"op", "select"}, {"item", "collider:0"}});
	CHECK(ok(ed, {{"op", "state"}})["item"] == "collider:0");
	ok(ed, {{"op", "move"}, {"item", "collider:0"}, {"handle", "b"}, {"to", {1000, 720}}});
	CHECK(doc()["colliders"][0]["b"] == json::array({1000, 720}));
	ok(ed, {{"op", "move"}, {"item", "collider:0"}, {"handle", "body"}, {"by", {0, -100}}});
	CHECK(doc()["colliders"][0]["a"] == json::array({600.0, 600.0}));
	CHECK(ok(ed, {{"op", "duplicate"}, {"item", "collider:0"}})["item"] == "collider:1");
	CHECK(doc()["colliders"].size() == 2);
	ok(ed, {{"op", "remove"}, {"item", "collider:1"}});
	CHECK(doc()["colliders"].size() == 1);
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	// the camera: its start drags
	ok(ed, {{"op", "set"}, {"path", "/camera"}, {"value", json::parse(R"({"start": [960, 540]})")}});
	ok(ed, {{"op", "move"}, {"item", "camera"}, {"handle", "start"}, {"to", {1000, 500}}});
	CHECK(doc()["camera"]["start"] == json::array({1000, 500}));
	CHECK(ed.command({{"op", "duplicate"}, {"item", "camera"}})["ok"] == false);
	ok(ed, {{"op", "remove"}, {"item", "camera"}});
	CHECK_FALSE(doc().contains("camera"));
	CHECK(ed.errors().empty());
}

TEST_CASE("editor: the collider tool places a collider with two clicks; the level's every key is a field (Stage 164)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ed.key('X', 0, true);
	// two clicks on the canvas
	for (const auto &[x, y] : {std::pair{300.0f, 500.0f}, std::pair{600.0f, 500.0f}}) {
		ed.pointer(rcg::Editor::PtrMove, x, y, 0, 0);
		ed.pointer(rcg::Editor::PtrDown, x, y, rcg::Editor::BtnLeft, 0);
		ed.pointer(rcg::Editor::PtrUp, x, y, rcg::Editor::BtnLeft, 0);
	}
	const json d = ok(ed, {{"op", "document"}})["document"];
	REQUIRE(d.contains("colliders"));
	CHECK(d["colliders"].size() == 1);
	// with nothing selected, the panel lists every level key
	const json panel = ok(ed, {{"op", "panel"}});
	std::set<std::string> labels;
	for (const json &r : panel["rows"]) {
		labels.insert(r["label"].get<std::string>());
	}
	for (const char *k : {"colliders", "camera", "background", "features", "templates", "carriers", "spread", "cycle", "objects", "detectors"}) {
		CHECK_MESSAGE(labels.count(k) == 1, k);
	}
}

TEST_CASE("editor: a new level -- empty or a copy, named in a prompt, unsaved until saved (Stage 165)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "levels"}, {"list", {"a/one", "a/two"}}, {"current", "a/one"}});
	// the menu's "+ new empty level": a prompt with a free name; Enter creates it
	ok(ed, {{"op", "level_menu"}, {"show", true}});
	click_row(ed, "+ new empty level");
	CHECK(ok(ed, {{"op", "state"}})["new_prompt"] == true);
	CHECK(ok(ed, {{"op", "state"}})["focus"] == "#new_level");
	ed.text("_x"); // (the default "untitled" -> "untitled_x")
	ed.key(rcg::Editor::KeyEnter, 0, true);
	json st = ok(ed, {{"op", "state"}});
	CHECK(st["level"] == "untitled_x");
	CHECK(st["dirty"] == true);
	CHECK(ok(ed, {{"op", "document"}})["document"]["name"] == "untitled_x");
	CHECK(ok(ed, {{"op", "document"}})["document"]["elements"].empty());
	// a name in use is refused; unsaved changes are asked about twice
	CHECK(ed.command({{"op", "new_level"}, {"name", "a/two"}})["ok"] == false);
	CHECK(ed.command({{"op", "new_level"}, {"name", "b/three"}})["ok"] == false); // (untitled_x is unsaved)
	CHECK(ok(ed, {{"op", "new_level"}, {"name", "b/three"}})["name"] == "b/three");
	ok(ed, {{"op", "saved"}});
	// "+ copy of a level": pick it, a prompt "<it>_copy"; the host loads it under the new name, unsaved
	ok(ed, {{"op", "level_menu"}, {"show", true}});
	click_row(ed, "+ copy of a level");
	CHECK(ok(ed, {{"op", "state"}})["copy_pick"] == true);
	click_row(ed, "a/two");
	CHECK(ok(ed, {{"op", "state"}})["new_prompt"] == true);
	ed.key(rcg::Editor::KeyEnter, 0, true); // (the default name: a/two_copy)
	CHECK(ed.status().open_requested);
	const json req = ok(ed, {{"op", "open_request"}});
	CHECK(req["name"] == "a/two");
	CHECK(req["as"] == "a/two_copy");
	ok(ed, {{"op", "load"}, {"text", R"({"schema": "rc-level/2", "name": "a/two", "elements": [{"kind": "wall", "start": [700, 300], "end": [700, 500]}]})"},
			   {"name", "a/two_copy"}, {"unsaved", true}});
	st = ok(ed, {{"op", "state"}});
	CHECK(st["level"] == "a/two_copy");
	CHECK(st["dirty"] == true);
	CHECK(ok(ed, {{"op", "document"}})["document"]["name"] == "a/two_copy");
	CHECK(ok(ed, {{"op", "document"}})["document"]["elements"].size() == 1);
	// ctrl+N opens the prompt; Esc cancels it
	ok(ed, {{"op", "saved"}});
	ed.key('N', rcg::Editor::ModCtrl, true);
	CHECK(ok(ed, {{"op", "state"}})["new_prompt"] == true);
	ed.key(rcg::Editor::KeyEscape, 0, true);
	CHECK(ok(ed, {{"op", "state"}})["new_prompt"] == false);
	CHECK(ok(ed, {{"op", "state"}})["level"] == "a/two_copy");
}

TEST_CASE("editor: a level renamed -- saved under the new name at once, the old name for the host to remove (Stage 170)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "levels"}, {"list", {"a/one", "a/two"}}, {"current", "a/one"}});
	ok(ed, {{"op", "load"}, {"text", R"({"schema": "rc-level/2", "name": "a/one", "elements": []})"}, {"name", "a/one"}});
	ok(ed, {{"op", "add"}, {"element", {{"kind", "wall"}, {"start", {700, 300}}, {"end", {700, 500}}}}});
	// the menu's "~ rename this level": a prompt with the level's name; Enter renames it and asks for a save
	ok(ed, {{"op", "level_menu"}, {"show", true}});
	click_row(ed, "~ rename this level");
	json st = ok(ed, {{"op", "state"}});
	CHECK(st["rename_prompt"] == true);
	CHECK(st["focus"] == "#rename_level");
	for (int i = 0; i < 3; i++) {
		ed.key(rcg::Editor::KeyBackspace, 0, true); // (a/one -> a/)
	}
	ed.text("uno");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	st = ok(ed, {{"op", "state"}});
	CHECK(st["level"] == "a/uno");
	CHECK(st["renamed_from"] == "a/one");
	CHECK(st["save_requested"] == true);
	CHECK(st["rename_prompt"] == false);
	CHECK(st["hint"] == "renamed a/one to a/uno (saved)");
	CHECK(ok(ed, {{"op", "document"}})["document"]["name"] == "a/uno");
	json names = json::array();
	const json listed = ok(ed, {{"op", "level_search"}, {"query", ""}}); // (kept: a range over a temporary's member dangles)
	for (const json &l : listed["levels"]) {
		names.push_back(l["name"]);
	}
	CHECK(names == json::array({"a/two", "a/uno"})); // (the list has the new name, not the old)
	// an undo keeps the name (the file has it); the unsaved wall goes with the save
	ok(ed, {{"op", "undo"}});
	CHECK(ok(ed, {{"op", "document"}})["document"]["name"] == "a/uno");
	CHECK(ok(ed, {{"op", "document"}})["document"]["elements"].empty());
	ok(ed, {{"op", "redo"}});
	CHECK(ok(ed, {{"op", "document"}})["document"]["elements"].size() == 1);
	ok(ed, {{"op", "saved"}});
	st = ok(ed, {{"op", "state"}});
	CHECK(st["renamed_from"].is_null());
	CHECK(st["dirty"] == false);
	// a name in use or not a level's name is refused; the same name does nothing
	CHECK(ed.command({{"op", "rename_level"}, {"to", "a/two"}})["ok"] == false);
	CHECK(ed.command({{"op", "rename_level"}, {"to", "../x"}})["ok"] == false);
	CHECK_FALSE(ok(ed, {{"op", "rename_level"}, {"to", "a/uno"}}).contains("from"));
	CHECK(ok(ed, {{"op", "state"}})["save_requested"] == false);
	// renamed twice before a save (the first save failed): the file on disk keeps the first name; back to it, nothing to remove
	ok(ed, {{"op", "rename_level"}, {"to", "b/x"}});
	ok(ed, {{"op", "save_cancelled"}});
	ok(ed, {{"op", "rename_level"}, {"to", "b/y"}});
	CHECK(ok(ed, {{"op", "state"}})["renamed_from"] == "a/uno");
	ok(ed, {{"op", "rename_level"}, {"to", "a/uno"}});
	CHECK(ok(ed, {{"op", "state"}})["renamed_from"].is_null());
	ok(ed, {{"op", "saved"}});
	// F2 opens the prompt, Esc cancels it
	ed.key(rcg::Editor::KeyF1 + 1, 0, true);
	CHECK(ok(ed, {{"op", "state"}})["rename_prompt"] == true);
	ed.key(rcg::Editor::KeyEscape, 0, true);
	st = ok(ed, {{"op", "state"}});
	CHECK(st["rename_prompt"] == false);
	CHECK(st["level"] == "a/uno");
	// the level's name field renames too
	ok(ed, {{"op", "level_menu"}, {"show", false}});
	click_row(ed, "name");
	REQUIRE(ok(ed, {{"op", "state"}})["focus"] == "/name");
	ed.text("_2");
	ed.key(rcg::Editor::KeyEnter, 0, true);
	st = ok(ed, {{"op", "state"}});
	CHECK(st["level"] == "a/uno_2");
	CHECK(st["renamed_from"] == "a/uno");
	// playing: refused
	ok(ed, {{"op", "saved"}});
	ok(ed, {{"op", "play"}});
	CHECK(ed.command({{"op", "rename_level"}, {"to", "c"}})["ok"] == false);
	ok(ed, {{"op", "stop"}});
	// an unnamed level is named: nothing to remove; a host saving an unnamed level names it
	rcg::Editor fresh;
	ok(fresh, {{"op", "rename_level"}, {"to", "fresh"}});
	st = ok(fresh, {{"op", "state"}});
	CHECK(st["level"] == "fresh");
	CHECK(st["renamed_from"].is_null());
	CHECK(st["save_requested"] == true);
	rcg::Editor unnamed;
	ok(unnamed, {{"op", "saved"}, {"name", "untitled"}});
	CHECK(ok(unnamed, {{"op", "state"}})["level"] == "untitled");
}

TEST_CASE("editor: the level list is searchable -- soft matches first, their characters marked; up/down and Enter (Stage 167)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	ok(ed, {{"op", "levels"}, {"list", {"main", "objectives/grapple_gap", "objectives/lantern", "playground/portals", "test_levels/ghost_room"}}});
	// the ranking: "gg" -> grapple_gap first (a word start, then a word start); the rest after it, in their order
	const json r = ok(ed, {{"op", "level_search"}, {"query", "gg"}})["levels"];
	REQUIRE(r.size() == 5);
	CHECK(r[0]["name"] == "objectives/grapple_gap");
	CHECK(r[0]["marks"] == json::array({11, 19}));
	CHECK(r[1]["name"] == "main");
	CHECK(r[1]["marks"].empty());
	const json g = ok(ed, {{"op", "level_search"}, {"query", "gr"}})["levels"]; // three matches (grapple, ghost_room, playground) above the rest
	CHECK(g[0]["name"] == "objectives/grapple_gap"); // (a run at a word's start first)
	CHECK(g[2]["marks"].size() == 2);
	CHECK(g[3]["marks"].empty());
	const json p = ok(ed, {{"op", "level_search"}, {"query", "PORT"}})["levels"]; // (case-insensitive, a run)
	CHECK(p[0]["name"] == "playground/portals");
	CHECK(p[0]["marks"] == json::array({11, 12, 13, 14}));
	// typed into the open list: the search has the focus; Down picks the next, Enter opens it
	ok(ed, {{"op", "level_menu"}, {"show", true}});
	CHECK(ok(ed, {{"op", "state"}})["focus"] == "#level_search");
	ed.text("o");
	ed.key(rcg::Editor::KeyDown, 0, true);
	ed.key(rcg::Editor::KeyEnter, 0, true);
	const json first = ok(ed, {{"op", "level_search"}, {"query", "o"}})["levels"];
	CHECK(ok(ed, {{"op", "open_request"}})["name"] == first[1]["name"]);
	// Esc clears the search, a second Esc closes the list
	ed.text("zz");
	ed.key(rcg::Editor::KeyEscape, 0, true);
	CHECK(ok(ed, {{"op", "state"}})["level_menu"] == true);
	ed.key(rcg::Editor::KeyEscape, 0, true);
	CHECK(ok(ed, {{"op", "state"}})["level_menu"] == false);
}

TEST_CASE("editor: the level's edges -- a panel field, drawn as their kind, their mistakes named (Stage 169)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	auto faces = [&] { // the configured edges' strokes (an element's are 3 px too; this level has none)
		int n = 0;
		for (const auto &node : ed.draw()) {
			for (const auto &c : *node.calls) {
				n += c.kind == rcg::render::Kind::Line && c.width == 3.0 ? 1 : 0;
			}
		}
		return n;
	};
	CHECK(faces() == 0); // (passes: the dashed bounds alone)
	ok(ed, {{"op", "set"}, {"path", "/edges"}, {"value", "mirror"}});
	CHECK(ed.errors().empty());
	CHECK(faces() == 4);
	ok(ed, {{"op", "set"}, {"path", "/edges"}, {"value", {{"right", "wall"}}}});
	CHECK(faces() == 1);
	ok(ed, {{"op", "set"}, {"path", "/edges"}, {"value", "wrap"}});
	CHECK(faces() == 4); // (two portals and their images)
	ok(ed, {{"op", "set"}, {"path", "/edges"}, {"value", "lava"}});
	REQUIRE(ed.errors().size() == 1);
	CHECK(ed.errors()[0].name == "edges");
	CHECK(ed.errors()[0].message.find("unknown element kind 'lava'") != std::string::npos);
	ok(ed, {{"op", "set"}, {"path", "/edges"}, {"value", nullptr}});
	CHECK(faces() == 0);
	CHECK_FALSE(ed.document().contains("edges"));
}

TEST_CASE("editor: several selected -- a marquee or ctrl+click; moved, scaled, stretched, copied and pasted together (Stage 175)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	auto state = [&] { return ok(ed, {{"op", "state"}}); };
	auto object = [&] { return ed.document()["objects"][0]["circle"]; };
	ok(ed, {{"op", "add"}, {"element", {{"kind", "door"}, {"name", "gate"}, {"start", {600.0, 300.0}}, {"end", {600.0, 400.0}}}}});
	ok(ed, {{"op", "add"}, {"element", {{"kind", "button"}, {"name", "b"}, {"opens", "gate"}, {"start", {700.0, 300.0}}, {"end", {800.0, 300.0}}}}});
	ok(ed, {{"op", "add"}, {"element", {{"kind", "wall"}, {"name", "far"}, {"start", {1300.0, 300.0}}, {"end", {1300.0, 400.0}}}}});
	ok(ed, {{"op", "set"}, {"path", "/objects"}, {"value", json::parse(R"([{"name": "box_1", "circle": {"center": [700, 380], "radius": 16}}])")}});
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	// a press on nothing, held and dragged: what lies wholly inside the marquee is selected
	drag(ed, 550, 250, 850, 450);
	CHECK(state()["selection"] == json::array({"element:gate", "element:b", "object:box_1"}));
	CHECK(state()["selected"].is_null());
	// ctrl (command) + click adds one or takes it out
	click(ed, 1300, 350, rcg::Editor::BtnLeft, rcg::Editor::ModCtrl);
	CHECK(state()["selection"].size() == 4);
	click(ed, 1300, 350, rcg::Editor::BtnLeft, rcg::Editor::ModCtrl);
	click(ed, 700, 380, rcg::Editor::BtnLeft, rcg::Editor::ModCtrl);
	CHECK(state()["selection"] == json::array({"element:gate", "element:b"}));
	click(ed, 700, 380, rcg::Editor::BtnLeft, rcg::Editor::ModCtrl);
	CHECK(state()["selection"].size() == 3);
	// a member dragged: they all move, by whole grid steps, in one undo step
	const size_t undo0 = state()["undo"];
	drag(ed, 600, 325, 643, 343);
	CHECK(element(ed, "gate")["start"] == json::array({640.0, 320.0}));
	CHECK(element(ed, "b")["end"] == json::array({840.0, 320.0}));
	CHECK(object()["center"] == json::array({740.0, 400.0}));
	CHECK(element(ed, "far")["start"] == json::array({1300.0, 300.0}));
	CHECK(state()["undo"] == undo0 + 1);
	CHECK(state()["selection"].size() == 3);
	// the box (640..840, 320..420): a corner scales about the opposite one, uniformly, the radius with it
	drag(ed, 840, 420, 1040, 520);
	CHECK(element(ed, "gate")["start"] == json::array({640.0, 320.0}));
	CHECK(element(ed, "gate")["end"] == json::array({640.0, 520.0}));
	CHECK(element(ed, "b")["start"] == json::array({840.0, 320.0}));
	CHECK(element(ed, "b")["end"] == json::array({1040.0, 320.0}));
	CHECK(object()["center"] == json::array({840.0, 480.0}));
	CHECK(object()["radius"] == 32.0);
	// a side's middle stretches from the opposite side; the points and the radius onto the grid
	drag(ed, 1040, 420, 840, 420);
	CHECK(element(ed, "b")["start"] == json::array({740.0, 320.0}));
	CHECK(element(ed, "b")["end"] == json::array({840.0, 320.0}));
	CHECK(element(ed, "gate")["end"] == json::array({640.0, 520.0}));
	CHECK(object()["center"] == json::array({740.0, 480.0}));
	CHECK(object()["radius"] == 23.0); // (32 x sqrt(0.5), to whole units)
	CHECK(state()["undo"] == undo0 + 3);
	// by command: each axis its own factor, about a point
	ok(ed, {{"op", "scale"}, {"selection", true}, {"by", {1.0, 0.5}}, {"about", {640.0, 320.0}}});
	CHECK(element(ed, "gate")["end"] == json::array({640.0, 420.0}));
	ok(ed, {{"op", "undo"}});
	// copy, then paste at the pointer: the box's center there (whole grid steps), fresh names, the button opening
	// the pasted door
	ed.key('C', rcg::Editor::ModCtrl, true);
	{
		const rc::V2 s = ed.to_screen(rc::V2::of(1500, 800));
		ed.pointer(rcg::Editor::PtrMove, s.x, s.y, 0, 0);
	}
	ed.key('V', rcg::Editor::ModCtrl, true);
	CHECK(state()["selection"] == json::array({"object:box_2", "element:door_1", "element:button_1"}));
	CHECK(element(ed, "door_1")["start"] == json::array({1400.0, 700.0})); // (the box 640..840, 320..520: moved 760, 380)
	CHECK(element(ed, "button_1")["opens"] == "door_1");
	CHECK(element(ed, "b")["opens"] == "gate");
	CHECK(ed.document()["objects"].size() == 2);
	// Delete removes them all; ctrl+D duplicates; Esc deselects
	ed.key(rcg::Editor::KeyDelete, 0, true);
	CHECK(ed.document()["elements"].size() == 3);
	CHECK(ed.document()["objects"].size() == 1);
	CHECK(state()["selection"].empty());
	ok(ed, {{"op", "select"}, {"group", {"element:gate", "element:b"}}});
	ed.key('D', rcg::Editor::ModCtrl, true);
	CHECK(state()["selection"] == json::array({"element:door_1", "element:button_1"}));
	CHECK(element(ed, "door_1")["start"] == json::array({660.0, 340.0}));
	CHECK(element(ed, "button_1")["opens"] == "door_1");
	ed.key(rcg::Editor::KeyRight, 0, true);
	CHECK(element(ed, "door_1")["start"] == json::array({670.0, 340.0}));
	ed.key(rcg::Editor::KeyEscape, 0, true);
	CHECK(state()["selection"].empty());
	// cut and paste (no pointer over the canvas: beside where it was); the clipboard outlives the level
	ok(ed, {{"op", "select"}, {"group", {"element:door_1", "element:button_1"}}});
	CHECK(ok(ed, {{"op", "cut"}})["copied"] == 2);
	CHECK(ed.document()["elements"].size() == 3);
	ok(ed, {{"op", "new"}});
	CHECK(ok(ed, {{"op", "paste"}})["selection"].size() == 2);
	CHECK(element(ed, "door_1")["start"] == json::array({690.0, 360.0}));
	// a ctrl+marquee adds to what is selected; one selected is the ordinary selection
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	drag(ed, 650, 330, 720, 600, rcg::Editor::ModCtrl);
	CHECK(state()["selected"] == "door_1");
	drag(ed, 750, 330, 900, 380, rcg::Editor::ModCtrl);
	CHECK(state()["selection"] == json::array({"element:door_1", "element:button_1"}));
	CHECK(ed.errors().empty());
}

TEST_CASE("editor: the player's start is selected like an item -- alone, by the marquee, by ctrl+click; moved and scaled with the rest (Stage 175)") {
	rcg::Editor ed;
	ed.resize(1920, 1080);
	auto state = [&] { return ok(ed, {{"op", "state"}}); };
	auto player = [&] { return ed.document()["player"]["start"]; };
	ok(ed, {{"op", "add"}, {"element", {{"kind", "wall"}, {"name", "w"}, {"start", {800.0, 600.0}}, {"end", {900.0, 600.0}}}}});
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	// a click on it selects it; its panel shows its start; it is moved, never removed or duplicated
	click(ed, 960, 540);
	CHECK(state()["item"] == "player");
	const json panel = ok(ed, {{"op", "panel"}});
	CHECK(std::any_of(panel["rows"].begin(), panel["rows"].end(), [](const json &r) { return r["label"] == "start"; }));
	ed.key(rcg::Editor::KeyDelete, 0, true);
	CHECK(player() == json::array({960.0, 540.0}));
	CHECK(ed.command({{"op", "duplicate"}, {"item", "player"}})["ok"] == false);
	ed.key(rcg::Editor::KeyRight, 0, true);
	CHECK(player() == json::array({970.0, 540.0}));
	// the marquee takes it with the wall; dragging it moves them both
	ok(ed, {{"op", "select"}, {"name", nullptr}});
	drag(ed, 750, 450, 1050, 650);
	CHECK(state()["selection"] == json::array({"element:w", "player"}));
	drag(ed, 970, 540, 990, 520);
	CHECK(player() == json::array({990.0, 520.0}));
	CHECK(element(ed, "w")["start"] == json::array({820.0, 580.0}));
	// the box (820..990, 520..580): its corner scales the player's start too
	drag(ed, 820, 520, 650, 460);
	CHECK(element(ed, "w")["start"] == json::array({650.0, 580.0}));
	CHECK(player() == json::array({990.0, 460.0})); // (about the opposite corner, 990, 580: x2)
	// ctrl+click takes it out and puts it back
	click(ed, 990, 460, rcg::Editor::BtnLeft, rcg::Editor::ModCtrl);
	CHECK(state()["selected"] == "w");
	click(ed, 990, 460, rcg::Editor::BtnLeft, rcg::Editor::ModCtrl);
	CHECK(state()["selection"] == json::array({"element:w", "player"}));
	// copied with the rest, it is not pasted where the level has one; Delete leaves it
	ed.key('C', rcg::Editor::ModCtrl, true);
	CHECK(ok(ed, {{"op", "paste"}})["selection"] == json::array({"element:wall_1"}));
	ok(ed, {{"op", "select"}, {"group", {"element:w", "player"}}});
	ed.key(rcg::Editor::KeyDelete, 0, true);
	CHECK(ed.document()["elements"].size() == 1);
	CHECK(player() == json::array({990.0, 460.0}));
	CHECK(ed.errors().empty());
}

// Stage 179 (the user): a marker on an element and on a detector, from the panel
TEST_CASE("editor: a marker -- the panel's rows cycle its shape (none: off) and colour, on an element and a detector; drawn") {
	rcg::Editor ed;
	ok(ed, {{"op", "add"}, {"element", {{"kind", "door"}, {"name", "gate"}, {"start", {750.0, 240.0}}, {"end", {750.0, 840.0}}}}});
	ok(ed, {{"op", "select"}, {"name", "gate"}});
	click_row(ed, "marker");
	CHECK(element(ed, "gate")["marker"] == json::parse(R"({"shape": "circle", "color": "red"})"));
	click_row(ed, "marker"); // the next shape, the colour kept
	click_row(ed, "marker color");
	CHECK(element(ed, "gate")["marker"] == json::parse(R"({"shape": "square", "color": "orange"})"));
	click_row(ed, "marker color", rcg::Editor::ModShift); // back
	CHECK(element(ed, "gate")["marker"]["color"] == "red");
	CHECK(ed.errors().empty());
	auto drawn = [&](float r, float g) {
		for (const auto &n : ed.draw()) {
			for (const auto &c : *n.calls) {
				if (c.kind == rcg::render::Kind::ColoredPolygon && !c.colors.empty() && c.colors[0][0] == r && c.colors[0][1] == g) {
					return true;
				}
			}
		}
		return false;
	};
	CHECK(drawn(0.95f, 0.25f)); // the red square
	click_row(ed, "marker", rcg::Editor::ModShift); // back to the circle
	click_row(ed, "marker", rcg::Editor::ModShift); // none: no marker
	CHECK_FALSE(element(ed, "gate").contains("marker"));
	// a detector's
	ok(ed, {{"op", "set"}, {"path", "/detectors"}, {"value", json::parse(R"([{"name": "pad", "region": {"rect": [600, 600, 40, 40]}}])")}});
	ok(ed, {{"op", "select"}, {"item", "detector:pad"}});
	click_row(ed, "marker");
	click_row(ed, "marker");
	click_row(ed, "marker");
	click_row(ed, "marker color");
	CHECK(ed.document()["detectors"][0]["marker"] == json::parse(R"({"shape": "triangle", "color": "orange"})"));
	CHECK(ed.errors().empty());
	CHECK(drawn(1.0f, 0.6f)); // the orange triangle
}

// Stage 179 (the user): a synced element's marker rows edit its flag's -- every element on the flag follows
TEST_CASE("editor: a flag's marker -- a button and its door alike; editing one edits both; sync marker off keeps its own") {
	rcg::Editor ed;
	ok(ed, {{"op", "add"}, {"element", {{"kind", "button"}, {"name", "b"}, {"opens", "gate"}, {"start", {600.0, 300.0}}, {"end", {600.0, 400.0}}}}});
	ok(ed, {{"op", "add"}, {"element", {{"kind", "door"}, {"name", "gate"}, {"start", {750.0, 240.0}}, {"end", {750.0, 840.0}}}}});
	auto marker_of_level = [&](size_t i) {
		const rcg::FlagMarkers fm = rcg::flag_markers(ed.document(), rcg::lower_level(ed.document()));
		return fm.element.at(i);
	};
	REQUIRE(marker_of_level(0));
	CHECK(marker_of_level(0) == marker_of_level(1)); // a default, shared
	ok(ed, {{"op", "select"}, {"name", "gate"}});
	click_row(ed, "marker"); // the flag's next shape
	REQUIRE(ed.document().contains("flag_markers"));
	CHECK(ed.document()["flag_markers"].contains("door:gate"));
	CHECK_FALSE(element(ed, "gate").contains("marker"));
	CHECK(marker_of_level(0) == marker_of_level(1)); // the button followed
	click_row(ed, "marker color");
	CHECK(marker_of_level(0) == marker_of_level(1));
	const std::optional<rcg::Marker> shared = marker_of_level(0);
	// sync off: the door keeps what it showed, as its own; then its own changes alone
	click_row(ed, "sync marker");
	CHECK(element(ed, "gate")["sync_marker"] == false);
	CHECK(marker_of_level(1) == shared);
	click_row(ed, "marker");
	CHECK(element(ed, "gate").contains("marker"));
	CHECK(marker_of_level(0) == shared);
	CHECK_FALSE(marker_of_level(1) == shared);
	// sync on again: the flag's
	click_row(ed, "sync marker");
	CHECK_FALSE(element(ed, "gate").contains("sync_marker"));
	CHECK(marker_of_level(1) == shared);
	// none on the flag: neither shows one
	for (int k = 0; k < 7 && marker_of_level(0); k++) {
		click_row(ed, "marker");
	}
	CHECK_FALSE(marker_of_level(0));
	CHECK_FALSE(marker_of_level(1));
	CHECK(ed.document()["flag_markers"]["door:gate"] == "none");
	CHECK(ed.errors().empty());
}
