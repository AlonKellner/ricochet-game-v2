// The level editor (Stage 148): the level's items -- the goal, the detectors,
// the objects -- selected, dragged, reshaped and edited like elements
// (rcg/editor.hpp). An item is named "goal", "detector:<name>" or
// "object:<name>"; its handles move the JSON they point at.
#include "rcg/canon.hpp"
#include "rcg/editor.hpp"
#include "rcg/kinematics.hpp"
#include "rcg/level2.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rcg {
namespace {

using json = nlohmann::json;

[[noreturn]] void refuse(const std::string &why) { throw std::runtime_error(why); }

bool is_point(const json &p) { return p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(); }
V2 pt(const json &p) { return V2::of(p[0].get<double>(), p[1].get<double>()); }
json jpoint(double x, double y) { return json::array({x, y}); }
bool is_rect(const json &r) { return r.is_array() && r.size() == 4 && std::all_of(r.begin(), r.end(), [](const json &v) { return v.is_number(); }); }
bool is_circle(const json &c) { return c.is_object() && is_point(c.value("center", json())) && c.value("radius", json()).is_number(); }

// "detector:plate" -> ("detector", "plate")
std::pair<std::string, std::string> split(const std::string &item) {
	const size_t at = item.find(':');
	return at == std::string::npos ? std::pair{item, std::string()} : std::pair{item.substr(0, at), item.substr(at + 1)};
}

int index_in(const json &doc, const char *list, const std::string &name) {
	if (!doc.contains(list) || !doc[list].is_array()) {
		return -1;
	}
	for (size_t i = 0; i < doc[list].size(); i++) {
		if (doc[list][i].is_object() && doc[list][i].value("name", "") == name) {
			return int(i);
		}
	}
	return -1;
}

// Moves a region, a segment holder or a circle holder by (dx, dy).
void translate_item(json &j, double dx, double dy) {
	if (j.contains("rect") && is_rect(j["rect"])) {
		j["rect"][0] = j["rect"][0].get<double>() + dx;
		j["rect"][1] = j["rect"][1].get<double>() + dy;
	}
	if (j.contains("circle") && is_circle(j["circle"])) {
		const json c = j["circle"]["center"];
		j["circle"]["center"] = jpoint(c[0].get<double>() + dx, c[1].get<double>() + dy);
	}
	if (j.contains("region") && j["region"].is_object()) {
		translate_item(j["region"], dx, dy);
	}
	if (j.contains("line") && j["line"].is_object()) { // a carrier's defining points
		for (const char *k : {"a", "b"}) {
			if (is_point(j["line"].value(k, json()))) {
				const json p = j["line"][k];
				j["line"][k] = jpoint(p[0].get<double>() + dx, p[1].get<double>() + dy);
			}
		}
	}
	if (j.contains("segment") && j["segment"].is_object()) {
		for (const char *k : {"a", "b"}) {
			if (is_point(j["segment"].value(k, json()))) {
				const json p = j["segment"][k];
				j["segment"][k] = jpoint(p[0].get<double>() + dx, p[1].get<double>() + dy);
			}
		}
	}
	for (const char *k : {"a", "b", "start"}) { // Stage 164: a collider's ends, the camera's start
		if (is_point(j.value(k, json()))) {
			const json p = j[k];
			j[k] = jpoint(p[0].get<double>() + dx, p[1].get<double>() + dy);
		}
	}
}

std::string fresh(const json &doc, const char *list, const std::string &base) {
	for (int n = 1;; n++) {
		const std::string name = base + "_" + std::to_string(n);
		if (index_in(doc, list, name) < 0) {
			return name;
		}
	}
}

} // namespace

std::optional<std::string> Editor::item_path(const std::string &item) const {
	const auto [kind, name] = split(item);
	if (kind == "goal") {
		if (doc_.contains("objective") && doc_["objective"].is_object() && doc_["objective"].contains("goal")) {
			return std::string("/objective/goal");
		}
		return std::nullopt;
	}
	if (kind == "player") { // Stage 175: the player's start
		if (doc_.contains("player") && doc_["player"].is_object() && is_point(doc_["player"].value("start", json()))) {
			return std::string("/player");
		}
		return std::nullopt;
	}
	if (kind == "camera") { // Stage 164: the camera's start
		if (doc_.contains("camera") && doc_["camera"].is_object()) {
			return std::string("/camera");
		}
		return std::nullopt;
	}
	if (kind == "collider") { // Stage 164: a player-only collider, by its index
		if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos || !doc_.contains("colliders") || !doc_["colliders"].is_array()
				|| std::stoul(name) >= doc_["colliders"].size()) {
			return std::nullopt;
		}
		return "/colliders/" + name;
	}
	const char *list = kind == "detector" ? "detectors" : kind == "object" ? "objects" : kind == "carrier" ? "carriers" : nullptr;
	if (list == nullptr) {
		return std::nullopt;
	}
	const int i = index_in(doc_, list, name);
	if (i < 0) {
		return std::nullopt;
	}
	return "/" + std::string(list) + "/" + std::to_string(i);
}

std::vector<Editor::Handle> Editor::item_handles() const {
	std::vector<Handle> out;
	const std::optional<std::string> path = item_.empty() ? std::nullopt : item_path(item_);
	if (!path || item_ == "player") {
		return out; // (the player's start is always a handle)
	}
	const json &j = doc_[json::json_pointer(*path)];
	// a region (the goal; a detector's region), a segment, a circle holder (an object)
	auto region = [&](const json &reg, const std::string &at) {
		if (reg.contains("rect") && is_rect(reg["rect"])) {
			const double x = reg["rect"][0].get<double>(), y = reg["rect"][1].get<double>(), w = reg["rect"][2].get<double>(), h = reg["rect"][3].get<double>();
			const V2 c[4] = {V2::of(x, y), V2::of(x + w, y), V2::of(x + w, y + h), V2::of(x, y + h)};
			for (int k = 0; k < 4; k++) {
				out.push_back({at, "corner" + std::to_string(k), c[k], item_});
			}
		}
		if (reg.contains("circle") && is_circle(reg["circle"])) {
			const V2 c = pt(reg["circle"]["center"]);
			out.push_back({at, "center", c, item_});
			out.push_back({at, "radius", V2::of(double(c.x) + reg["circle"]["radius"].get<double>(), c.y), item_});
		}
	};
	if (split(item_).first == "goal") {
		region(j, *path);
	} else if (split(item_).first == "collider" || split(item_).first == "camera") { // Stage 164: its own points
		for (const char *k : {"a", "b", "start"}) {
			if (is_point(j.value(k, json()))) {
				out.push_back({*path, k, pt(j[k]), item_});
			}
		}
	} else if (j.contains("line") && j["line"].is_object()) { // a line carrier: its two defining points
		for (const char *k : {"a", "b"}) {
			if (is_point(j["line"].value(k, json()))) {
				out.push_back({*path, k, pt(j["line"][k]), item_});
			}
		}
	} else if (j.contains("region")) {
		region(j["region"], *path + "/region");
	} else if (j.contains("segment") && j["segment"].is_object()) {
		for (const char *k : {"a", "b"}) {
			if (is_point(j["segment"].value(k, json()))) {
				out.push_back({*path, k, pt(j["segment"][k]), item_});
			}
		}
	} else {
		region(j, *path); // an object: its "circle"
	}
	return out;
}

// The item under the pointer: an object (in its circle), a detector (in its
// region, near its segment), the goal (in its region) -- the smallest first.
std::string Editor::item_at(const V2 &screen) const {
	const V2 w = to_world(screen);
	auto in_region = [&](const json &reg) {
		if (reg.contains("rect") && is_rect(reg["rect"])) {
			const double x = reg["rect"][0].get<double>(), y = reg["rect"][1].get<double>(), ww = reg["rect"][2].get<double>(), h = reg["rect"][3].get<double>();
			return w.x >= x && w.y >= y && w.x <= x + ww && w.y <= y + h;
		}
		if (reg.contains("circle") && is_circle(reg["circle"])) {
			const V2 c = pt(reg["circle"]["center"]);
			return std::hypot(double(w.x) - c.x, double(w.y) - c.y) <= reg["circle"]["radius"].get<double>();
		}
		return false;
	};
	auto near_segment = [&](const V2 &wa, const V2 &wb) {
		const V2 a = to_screen(wa), b = to_screen(wb);
		const double ex = double(b.x) - a.x, ey = double(b.y) - a.y, l2 = ex * ex + ey * ey;
		const double u = l2 > 0 ? std::clamp(((double(screen.x) - a.x) * ex + (double(screen.y) - a.y) * ey) / l2, 0.0, 1.0) : 0.0;
		return std::hypot(double(a.x) + ex * u - screen.x, double(a.y) + ey * u - screen.y) <= 8.0;
	};
	// Stage 175: the player (its start; under gravity up to its head)
	if (item_path("player")) {
		const V2 p = to_screen(pt(doc_["player"]["start"]));
		const json g = doc_.value("gravity", json::array({0.0, 0.0}));
		const bool tall = g.is_array() && g.size() == 2 && g[0].is_number() && g[1].is_number() && (g[0].get<double>() != 0.0 || g[1].get<double>() != 0.0);
		const double r = kinematics::RADIUS * zoom_ + 4.0, top = double(p.y) - (tall ? kinematics::BODY * zoom_ : 0.0);
		const double dy = screen.y > p.y ? double(screen.y) - p.y : screen.y < top ? top - screen.y : 0.0;
		if (std::hypot(double(screen.x) - p.x, dy) <= r) {
			return "player";
		}
	}
	// Stage 164: the camera's start (its marker), a collider (thin, like an element)
	if (doc_.contains("camera") && doc_["camera"].is_object() && is_point(doc_["camera"].value("start", json()))) {
		const V2 c = to_screen(pt(doc_["camera"]["start"]));
		if (std::hypot(double(c.x) - screen.x, double(c.y) - screen.y) <= 12.0) {
			return "camera";
		}
	}
	{
		const std::string c = carrier_near(screen); // a carrier's line or circle (thin, like an element)
		if (!c.empty()) {
			return "carrier:" + c;
		}
	}
	if (doc_.contains("colliders") && doc_["colliders"].is_array()) {
		for (size_t i = doc_["colliders"].size(); i-- > 0;) {
			const json &k = doc_["colliders"][i];
			if (k.is_object() && is_point(k.value("a", json())) && is_point(k.value("b", json())) && near_segment(pt(k["a"]), pt(k["b"]))) {
				return "collider:" + std::to_string(i);
			}
		}
	}
	if (doc_.contains("objects") && doc_["objects"].is_array()) {
		for (auto it = doc_["objects"].rbegin(); it != doc_["objects"].rend(); ++it) {
			if (it->is_object() && in_region(*it)) {
				return "object:" + it->value("name", "");
			}
		}
	}
	if (doc_.contains("detectors") && doc_["detectors"].is_array()) {
		for (auto it = doc_["detectors"].rbegin(); it != doc_["detectors"].rend(); ++it) {
			if (!it->is_object()) {
				continue;
			}
			bool hit = it->contains("region") && in_region((*it)["region"]);
			if (it->contains("segment") && (*it)["segment"].is_object() && is_point((*it)["segment"].value("a", json()))
					&& is_point((*it)["segment"].value("b", json()))) {
				const V2 a = to_screen(pt((*it)["segment"]["a"])), b = to_screen(pt((*it)["segment"]["b"]));
				const double ex = double(b.x) - a.x, ey = double(b.y) - a.y, l2 = ex * ex + ey * ey;
				const double u = l2 > 0 ? std::clamp(((double(screen.x) - a.x) * ex + (double(screen.y) - a.y) * ey) / l2, 0.0, 1.0) : 0.0;
				hit = hit || std::hypot(double(a.x) + ex * u - screen.x, double(a.y) + ey * u - screen.y) <= 8.0;
			}
			if (hit) {
				return "detector:" + it->value("name", "");
			}
		}
	}
	if (item_path("goal") && in_region(doc_["objective"]["goal"])) {
		return "goal";
	}
	return "";
}

void Editor::drag_item(const V2 &screen, unsigned mods) {
	const json::json_pointer ptr(drag_->element);
	if (!doc_.contains(ptr)) {
		return;
	}
	json &j = doc_[ptr];
	const json before = j;
	const std::string &k = drag_->kind;
	if (k == "body") {
		const V2 w = to_world(screen);
		const double step = (mods & ModAlt) ? 0.0 : grid_ ? 10.0 : 1.0;
		double dx = double(w.x) - drag_last_.x, dy = double(w.y) - drag_last_.y;
		if (step > 0) {
			dx = std::trunc(dx / step) * step;
			dy = std::trunc(dy / step) * step;
		}
		if (dx == 0 && dy == 0) {
			return;
		}
		translate_item(j, dx, dy);
		drag_last_ = V2::of(double(drag_last_.x) + dx, double(drag_last_.y) + dy);
	} else if (k.rfind("corner", 0) == 0 && j.contains("rect") && is_rect(j["rect"])) {
		// the opposite corner stays; the rectangle follows the dragged one
		const json p = snap(screen, mods, &*drag_);
		const double x = j["rect"][0].get<double>(), y = j["rect"][1].get<double>(), w = j["rect"][2].get<double>(), h = j["rect"][3].get<double>();
		const int c = k.back() - '0';
		const double ox = (c == 1 || c == 2) ? x : x + w, oy = (c == 2 || c == 3) ? y : y + h;
		const double px = p[0].get<double>(), py = p[1].get<double>();
		j["rect"] = json::array({std::min(ox, px), std::min(oy, py), std::abs(px - ox), std::abs(py - oy)});
		const bool right = px > ox, down = py > oy;
		drag_->kind = "corner" + std::to_string(!down ? (right ? 1 : 0) : (right ? 2 : 3)); // (dragged past the opposite one)
	} else if ((k == "center" || k == "radius") && j.contains("circle") && is_circle(j["circle"])) {
		const json p = snap(screen, mods, &*drag_);
		if (k == "center") {
			j["circle"]["center"] = p;
		} else {
			const V2 c = pt(j["circle"]["center"]), q = to_world(screen); // (the radius is rounded, not the point)
			const double step = grid_ && !(mods & ModAlt) ? 10.0 : 1.0;
			j["circle"]["radius"] = std::max(step, std::round(std::hypot(double(q.x) - c.x, double(q.y) - c.y) / step) * step);
		}
	} else if ((k == "a" || k == "b") && j.contains("segment") && j["segment"].is_object()) {
		j["segment"][k] = snap(screen, mods, &*drag_);
	} else if ((k == "a" || k == "b" || k == "start") && is_point(j.value(k, json()))) { // Stage 164: a collider's end, the camera
		j[k] = snap(screen, mods, &*drag_);
	} else if ((k == "a" || k == "b") && j.contains("line") && j["line"].is_object()) {
		const json p = snap(screen, mods, &*drag_);
		const std::string other = k == "a" ? "b" : "a";
		if (canonical(p) == canonical(j["line"].value(other, json()))) {
			return; // (a line needs two points)
		}
		j["line"][k] = p; // the elements on it follow (the lowering projects them)
	} else {
		return;
	}
	if (canonical(before) != canonical(j)) {
		drag_changed_ = true;
		refresh();
	}
}

std::vector<Editor::Row> Editor::item_rows() const {
	std::vector<Row> out;
	const std::optional<std::string> path = item_path(item_);
	if (!path) {
		return out;
	}
	const auto [kind, name] = split(item_);
	const json &j = doc_[json::json_pointer(*path)];
	auto value = [&](const std::string &p) -> std::string {
		const json::json_pointer ptr(p);
		if (!doc_.contains(ptr)) {
			return "";
		}
		const json v = doc_[ptr];
		if (v.is_string()) {
			return v.get<std::string>();
		}
		std::string s = canonical(v);
		s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
		return s;
	};
	auto row = [&](const std::string &label, const std::string &p, const std::string &hint, const std::string &dflt = "",
					   const std::string &choice = "", std::vector<std::string> options = {}) {
		Row r;
		r.kind = Row::Value;
		r.label = label;
		r.value = value(p);
		if (r.value.empty() && !dflt.empty()) {
			r.value = "(" + dflt + ")";
		}
		r.hint = hint;
		r.field = Field{"", p};
		r.choice = choice;
		r.options = std::move(options);
		out.push_back(std::move(r));
	};
	Row head;
	head.kind = Row::Header;
	head.label = kind == "goal" || kind == "camera" || kind == "player" ? kind : kind + " " + name;
	if (kind == "carrier") {
		head.label += j.contains("line") ? " (a line)" : " (a circle)";
	}
	out.push_back(head);
	auto shape = [&](const std::string &p) {
		const json::json_pointer ptr(p);
		const bool rect = doc_.contains(ptr) && doc_[ptr].contains("rect");
		Row r;
		r.kind = Row::Value;
		r.label = "shape";
		r.value = rect ? "rect" : "circle";
		r.hint = "click: a rect <-> the circle in it";
		r.field = Field{"", p};
		r.choice = "shape";
		out.push_back(std::move(r));
		row(rect ? "rect" : "circle", p + (rect ? "/rect" : "/circle"), rect ? "[x, y, w, h]: drag its corners" : "{\"center\", \"radius\"}: drag its handles");
	};
	if (kind == "goal") {
		shape(*path);
		row("requires", "/objective/requires", "[{\"key\", \"values\"}]: the goal opens while they hold", "none: always open");
	} else if (kind == "collider") { // Stage 164
		row("a", *path + "/a", "[x, y]: one end (drag it)");
		row("b", *path + "/b", "[x, y]: the other");
		Row r;
		r.kind = Row::Text;
		r.label = "player-only: it holds the player; arrows pass";
		out.push_back(r);
	} else if (kind == "player") { // Stage 175
		row("start", *path + "/start", "[x, y]: where the player starts -- under gravity its feet (drag it)");
	} else if (kind == "camera") { // Stage 164
		row("start", *path + "/start", "[x, y]: where the camera starts (drag it)");
		row("smoothing", *path + "/smoothing", "the camera eases after the player", "false", "bool");
		row("speed", *path + "/smoothing_speed", "its easing speed", "5");
	} else if (kind == "carrier") {
		row("name", *path + "/name", "renaming carries the elements on it");
		if (j.contains("line")) {
			row("a", *path + "/line/a", "[x, y]: a defining point (drag it: the elements on the line follow)");
			row("b", *path + "/line/b", "[x, y]: the other");
		} else {
			row("center", *path + "/circle/center", "[x, y]: drag it: the elements on the circle follow");
			row("radius", *path + "/circle/radius", "drag its handle");
		}
		row("exact", *path + "/exact", "its elements' points exactly on it (snapped to its exact points; Stage 154)", "false", "bool");
		int on = 0;
		for (const json &e : doc_["elements"]) {
			on += e.is_object() && e.value("carrier", json()) == name;
		}
		Row r;
		r.kind = Row::Text;
		r.label = std::to_string(on) + " element(s) on it; Delete frees them (their own geometry)";
		out.push_back(r);
	} else if (kind == "detector") {
		row("name", *path + "/name", "renaming carries det:<name> (doors, the goal's requires)");
		row("for", *path + "/for", "who it sees: the player, objects (their centers), any", "player", "options", {"player", "object", "any"});
		row("mode", *path + "/mode", "while (inside), enter, toggle (on entering or crossing)", "while", "options", {"while", "enter", "toggle"});
		row("flag", *path + "/flag", "the flag it writes", "det:" + name);
		row("value", *path + "/value", "the value it writes", "true");
		if (j.contains("region")) {
			shape(*path + "/region");
		} else {
			row("segment", *path + "/segment", "{\"a\", \"b\"}: crossed, it writes");
		}
	} else {
		row("name", *path + "/name", "the object's name");
		row("kind", *path + "/kind", "pullable: falls, a pull brings it back; static: stays", "pullable", "options", {"pullable", "static"});
		row("face", *path + "/face", "its sides: a wall, a mirror, a pass", "wall", "options", {"wall", "mirror", "pass"});
		row("solid", *path + "/solid", "the player collides with it", "true", "bool");
		row("center", *path + "/circle/center", "[x, y]: drag it");
		row("radius", *path + "/circle/radius", "drag its handle", "16");
		row("label", *path + "/label", "the node prefix", "Object");
		row("gravity", *path + "/gravity", "its gravity, a factor of the level's (a bubble: negative, it floats up)", "1");
		row("pops", *path + "/pops", "the arrow pops it (gone for good)", "false", "bool");
		row("carries", *path + "/carries", "an object it holds until it pops (or is removed)", "none");
		row("tags", *path + "/tags", "[tag, ...]: what spread rules flow through (conductive: it carries charge)", "none");
	}
	Row keys;
	keys.kind = Row::Text;
	keys.label = kind == "player" ? "arrows: move it; ctrl+click: select it with others"
			: kind == "goal" || kind == "camera" ? "Delete: remove the " + kind + "; arrows: move it" : "Delete: remove; ctrl+D: duplicate; arrows: move";
	out.push_back(keys);
	return out;
}

json Editor::item_op(const std::string &o, const json &cmd) {
	if (mode_ == Mode::Play && o != "select") {
		refuse("playing: stop first");
	}
	const std::string item = cmd.at("item").get<std::string>();
	const std::optional<std::string> path = item_path(item);
	if (!path) {
		refuse("no item '" + item + "'");
	}
	const auto [kind, name] = split(item);
	const json::json_pointer ptr(*path);
	if (o == "select") {
		set_selection({item});
		return {};
	}
	if (kind == "player" && o != "move") {
		refuse("the player's start is one: it is moved, never " + std::string(o == "remove" ? "removed" : o == "duplicate" ? "duplicated" : "renamed"));
	}
	if (o == "remove") {
		json freed = doc_["elements"];
		if (kind == "carrier") { // the elements on it keep their places, as their own geometry
			for (json &e : freed) {
				if (e.is_object() && e.value("carrier", json()) == name) {
					e = off_carrier(e, doc_);
				}
			}
		}
		change([&](json &d) {
			if (kind == "goal") {
				d.erase("objective");
			} else if (kind == "camera") {
				d.erase("camera");
			} else {
				d[ptr.parent_pointer()].erase(std::stoul(ptr.back()));
			}
			if (kind == "carrier") {
				d["elements"] = freed;
			}
		});
		if (item_ == item) {
			item_.clear();
		}
		return {};
	}
	if (o == "move") {
		const std::string h = cmd.at("handle").get<std::string>();
		change([&](json &d) {
			json &j = d[ptr];
			if (h == "body") {
				const json by = cmd.at("by");
				translate_item(j, by[0].get<double>(), by[1].get<double>());
				return;
			}
			json &reg = kind == "detector" && j.contains("region") ? j["region"] : j;
			const json to = cmd.at("to");
			if (h.rfind("corner", 0) == 0 && reg.contains("rect") && is_rect(reg["rect"])) {
				const double x = reg["rect"][0].get<double>(), y = reg["rect"][1].get<double>(), w = reg["rect"][2].get<double>(), hh = reg["rect"][3].get<double>();
				const int c = h.back() - '0';
				const double ox = (c == 1 || c == 2) ? x : x + w, oy = (c == 2 || c == 3) ? y : y + hh;
				const double px = to[0].get<double>(), py = to[1].get<double>();
				reg["rect"] = json::array({std::min(ox, px), std::min(oy, py), std::abs(px - ox), std::abs(py - oy)});
			} else if (h == "center" && reg.contains("circle")) {
				reg["circle"]["center"] = to;
			} else if (h == "radius" && reg.contains("circle")) {
				reg["circle"]["radius"] = to;
			} else if ((h == "a" || h == "b") && j.contains("segment")) {
				j["segment"][h] = to;
			} else if ((h == "a" || h == "b" || h == "start") && is_point(j.value(h, json()))) { // Stage 164: a collider, the camera
				j[h] = to;
			} else {
				refuse("handle: body, corner0-3, center, radius, a or b expected");
			}
		});
		return {};
	}
	if (kind == "goal" || kind == "camera") {
		refuse("the " + kind + " is one: it is not " + std::string(o == "duplicate" ? "duplicated" : "renamed"));
	}
	if (kind == "collider") { // Stage 164: colliders have no names
		if (o != "duplicate") {
			refuse("a collider has no name to change");
		}
		json copy = doc_[ptr];
		const json by = cmd.value("by", jpoint(20.0, 20.0));
		translate_item(copy, by[0].get<double>(), by[1].get<double>());
		const size_t at = std::stoul(ptr.back()) + 1;
		change([&](json &d) { d["colliders"].insert(d["colliders"].begin() + long(at), copy); });
		set_selection({"collider:" + std::to_string(at)});
		return {{"item", item_}};
	}
	const char *list = kind == "detector" ? "detectors" : kind == "carrier" ? "carriers" : "objects";
	if (o == "duplicate") {
		json copy = doc_[ptr];
		const std::string base = name.find('_') != std::string::npos ? name.substr(0, name.rfind('_')) : name;
		const std::string nn = fresh(doc_, list, base);
		copy["name"] = nn;
		const json by = cmd.value("by", jpoint(20.0, 20.0));
		translate_item(copy, by[0].get<double>(), by[1].get<double>());
		change([&](json &d) { d[list].insert(d[list].begin() + long(std::stoul(ptr.back())) + 1, copy); });
		set_selection({kind + ":" + nn});
		return {{"item", item_}};
	}
	if (o == "rename") {
		const std::string to = cmd.at("to").get<std::string>();
		if (to.empty() || (to != name && index_in(doc_, list, to) >= 0)) {
			refuse(to.empty() ? "a name expected" : "'" + to + "' names another " + kind);
		}
		const std::string from_flag = "det:" + name, to_flag = "det:" + to;
		item_ = kind + ":" + to; // (before the refresh)
		change([&](json &d) {
			d[ptr]["name"] = to;
			if (kind == "carrier") { // the elements on it follow
				for (json &e : d["elements"]) {
					if (e.is_object() && e.value("carrier", json()) == name) {
						e["carrier"] = to;
					}
				}
				return;
			}
			if (kind != "detector") {
				return;
			}
			// det:<name> follows: the detector's own flag, the elements' flags, the goal's conditions
			if (d[ptr].value("flag", json()) == from_flag) {
				d[ptr].erase("flag");
			}
			for (json &e : d["elements"]) {
				if (e.is_object() && e.value("flag", json()) == from_flag) {
					e["flag"] = to_flag;
				}
			}
			if (d.contains("objective") && d["objective"].is_object() && d["objective"].contains("requires") && d["objective"]["requires"].is_array()) {
				for (json &c : d["objective"]["requires"]) {
					if (c.is_object() && c.value("key", json()) == from_flag) {
						c["key"] = to_flag;
					}
				}
			}
		});
		return {{"item", item_}};
	}
	refuse("unknown op '" + o + "'");
}

// ---------------------------------------------------------------- Stage 175: the selection

std::vector<std::string> Editor::selection() const {
	if (!group_.empty()) {
		return group_;
	}
	if (!selected_.empty()) {
		return {"element:" + selected_};
	}
	if (!item_.empty()) {
		return {item_};
	}
	return {};
}

void Editor::set_selection(std::vector<std::string> entries) {
	std::vector<std::string> once;
	for (std::string &e : entries) {
		if (std::find(once.begin(), once.end(), e) == once.end()) {
			once.push_back(std::move(e));
		}
	}
	selected_.clear();
	item_.clear();
	group_.clear();
	if (once.size() == 1) {
		if (once[0].rfind("element:", 0) == 0) {
			selected_ = once[0].substr(8);
		} else {
			item_ = once[0];
		}
	} else if (once.size() > 1) {
		group_ = std::move(once);
	}
}

bool Editor::exists(const std::string &entry) const {
	return entry.rfind("element:", 0) == 0 ? index_of(entry.substr(8)) >= 0 : item_path(entry).has_value();
}

// Scales the entries' geometry in doc: every point about (ax, ay) by (sx, sy), then by (bx, by), onto the grid
// (step; 0: float32 as it is). A rectangle stays one (its corners), a circle's radius scales by the mean factor
// (to whole units), a portal's displacement and a motion's offsets by the factors.
void Editor::scale(json &doc, const std::vector<std::string> &entries, const Scale &t, double step) const {
	auto snapped = [&](double v) { return step > 0 ? std::round(v / step) * step : double(float(v)); };
	auto map = [&](json &p) {
		if (is_point(p)) {
			p = jpoint(snapped(t.ax + (p[0].get<double>() - t.ax) * t.sx + t.bx), snapped(t.ay + (p[1].get<double>() - t.ay) * t.sy + t.by));
		}
	};
	auto radius = [&](json &r) {
		if (r.is_number()) {
			const double v = r.get<double>() * std::sqrt(std::abs(t.sx * t.sy));
			r = step > 0 ? std::max(1.0, std::round(v)) : double(float(v)); // (whole units: a radius is no point on the grid)
		}
	};
	auto circle = [&](json &c) {
		if (is_circle(c)) {
			map(c["center"]);
			radius(c["radius"]);
		}
	};
	auto region = [&](json &reg) {
		if (reg.contains("rect") && is_rect(reg["rect"])) {
			json &r = reg["rect"];
			json a = jpoint(r[0].get<double>(), r[1].get<double>()), b = jpoint(r[0].get<double>() + r[2].get<double>(), r[1].get<double>() + r[3].get<double>());
			map(a);
			map(b);
			const double x0 = a[0].get<double>(), y0 = a[1].get<double>(), x1 = b[0].get<double>(), y1 = b[1].get<double>();
			r = json::array({std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)});
		}
		if (reg.contains("circle")) {
			circle(reg["circle"]);
		}
	};
	for (const std::string &entry : entries) {
		if (entry.rfind("element:", 0) == 0) {
			const int i = index_in(doc, "elements", entry.substr(8));
			if (i < 0) {
				continue;
			}
			json &e = doc["elements"][size_t(i)];
			json &g = e.value("kind", "") == "raw" && e.contains("surface") && e["surface"].is_object() ? e["surface"] : e;
			for (const char *k : {"start", "end", "via"}) {
				if (g.contains(k)) {
					map(g[k]);
				}
			}
			for (const char *k : {"from", "to"}) { // (on a carrier: projected onto it)
				if (e.contains(k)) {
					map(e[k]);
				}
			}
			if (g.contains("circle")) {
				circle(g["circle"]);
			}
			if (e.value("kind", "") == "portal" && is_point(e.value("d", json()))) { // the image's displacement
				const json d = e["d"];
				e["d"] = jpoint(snapped(d[0].get<double>() * t.sx), snapped(d[1].get<double>() * t.sy));
			}
			if (e.contains("motion") && e["motion"].is_object()) {
				json &m = e["motion"];
				if (m.contains("pivot")) {
					map(m["pivot"]);
				}
				if (m.value("keys", json()).is_array()) {
					for (json &k : m["keys"]) {
						if (k.is_array() && k.size() == 4 && k[1].is_number() && k[2].is_number()) {
							k[1] = double(float(k[1].get<double>() * t.sx));
							k[2] = double(float(k[2].get<double>() * t.sy));
						}
					}
				}
			}
			continue;
		}
		const std::optional<std::string> path = item_path(entry);
		const json::json_pointer ptr(path ? *path : "/none");
		if (!path || !doc.contains(ptr)) {
			continue;
		}
		json &j = doc[ptr];
		const std::string kind = split(entry).first;
		if (kind == "goal") {
			region(j);
		} else if (kind == "detector") {
			if (j.contains("region") && j["region"].is_object()) {
				region(j["region"]);
			}
			if (j.contains("segment") && j["segment"].is_object()) {
				map(j["segment"]["a"]);
				map(j["segment"]["b"]);
			}
		} else if (kind == "object") {
			if (j.contains("circle")) {
				circle(j["circle"]);
			}
		} else if (kind == "carrier") {
			if (j.contains("line") && j["line"].is_object()) {
				map(j["line"]["a"]);
				map(j["line"]["b"]);
			}
			if (j.contains("circle")) {
				circle(j["circle"]);
			}
		} else {
			for (const char *k : {"a", "b", "start"}) { // a collider's ends, the camera's start
				if (j.contains(k)) {
					map(j[k]);
				}
			}
		}
	}
}

// The clipboard: the entries' JSON by where it goes ({"elements": [...], "detectors": [...], ...}) and their box. An
// element on a carrier not copied with it is copied as its own geometry.
json Editor::copied(const std::vector<std::string> &entries) const {
	json clip = json::object();
	std::vector<std::string> carriers;
	for (const std::string &e : entries) {
		if (e.rfind("carrier:", 0) == 0) {
			carriers.push_back(e.substr(8));
		}
	}
	for (const std::string &entry : entries) {
		if (entry.rfind("element:", 0) == 0) {
			const int i = index_of(entry.substr(8));
			if (i < 0) {
				continue;
			}
			json e = doc_["elements"][size_t(i)];
			if (e.contains("carrier") && std::find(carriers.begin(), carriers.end(), e.value("carrier", "")) == carriers.end()) {
				e = off_carrier(e, doc_);
			}
			clip["elements"].push_back(std::move(e));
			continue;
		}
		const std::optional<std::string> path = item_path(entry);
		if (!path) {
			continue;
		}
		const std::string kind = split(entry).first;
		const json &j = doc_[json::json_pointer(*path)];
		if (kind == "goal" || kind == "camera" || kind == "player") {
			clip[kind] = j;
		} else {
			clip[kind == "detector" ? "detectors" : kind == "object" ? "objects" : kind == "carrier" ? "carriers" : "colliders"].push_back(j);
		}
	}
	if (const std::optional<Box> b = box_of(entries)) {
		clip["box"] = json::array({b->x0, b->y0, b->x1, b->y1});
	}
	return clip;
}

// Pastes the clipboard into doc, moved by (dx, dy): fresh names (the buttons, flags and carriers among the pasted
// follow their own), the goal and the camera only where the level has none.
std::vector<std::string> Editor::paste(json &doc, const json &clip, double dx, double dy) {
	std::vector<std::string> out;
	std::vector<std::pair<std::string, std::string>> renamed_elements, renamed_carriers, renamed_detectors, renamed_objects;
	auto rename = [](const std::vector<std::pair<std::string, std::string>> &m, const std::string &n) {
		for (const auto &[from, to] : m) {
			if (from == n) {
				return to;
			}
		}
		return n;
	};
	auto base_of = [](const std::string &name) { return name.find('_') != std::string::npos ? name.substr(0, name.rfind('_')) : name; };
	auto add_list = [&](const char *list, const char *kind, std::vector<std::pair<std::string, std::string>> *names) {
		if (!clip.contains(list)) {
			return;
		}
		if (!doc.contains(list) || !doc[list].is_array()) {
			doc[list] = json::array();
		}
		for (json j : clip[list]) {
			if (names != nullptr) {
				const std::string from = j.value("name", "");
				const std::string to = fresh(doc, list, base_of(from.empty() ? std::string(kind) : from));
				names->push_back({from, to});
				j["name"] = to;
				out.push_back(std::string(kind) + ":" + to);
			} else {
				out.push_back(std::string(kind) + ":" + std::to_string(doc[list].size()));
			}
			doc[list].push_back(std::move(j));
		}
	};
	add_list("carriers", "carrier", &renamed_carriers);
	add_list("detectors", "detector", &renamed_detectors);
	add_list("objects", "object", &renamed_objects);
	add_list("colliders", "collider", nullptr);
	for (const char *k : {"goal", "camera", "player"}) {
		const bool has = std::string(k) == "goal" ? doc.contains("objective") && doc["objective"].is_object() && doc["objective"].contains("goal")
												  : doc.contains(k);
		if (clip.contains(k) && !has) {
			(std::string(k) == "goal" ? doc["objective"]["goal"] : doc[k]) = clip[k];
			out.push_back(k);
		}
	}
	if (clip.contains("elements")) {
		for (const json &e : clip["elements"]) {
			const std::string from = e.value("name", "");
			std::string to;
			for (int n = 1;; n++) { // (fresh_name's spelling, in doc)
				to = e.value("kind", "element") + "_" + std::to_string(n);
				if (index_in(doc, "elements", to) < 0 && std::none_of(renamed_elements.begin(), renamed_elements.end(), [&](const auto &p) { return p.second == to; })) {
					break;
				}
			}
			renamed_elements.push_back({from, to});
		}
		for (size_t k = 0; k < clip["elements"].size(); k++) {
			json e = clip["elements"][k];
			e["name"] = renamed_elements[k].second;
			if (e.value("opens", json()).is_string()) {
				e["opens"] = rename(renamed_elements, e["opens"].get<std::string>());
			}
			if (e.value("carrier", json()).is_string()) {
				e["carrier"] = rename(renamed_carriers, e["carrier"].get<std::string>());
			}
			if (e.value("flag", json()).is_string()) { // door:<name>, det:<name> of the pasted
				const std::string f = e["flag"].get<std::string>();
				if (f.rfind("door:", 0) == 0) {
					e["flag"] = "door:" + rename(renamed_elements, f.substr(5));
				} else if (f.rfind("det:", 0) == 0) {
					e["flag"] = "det:" + rename(renamed_detectors, f.substr(4));
				}
			}
			doc["elements"].push_back(std::move(e));
			out.push_back("element:" + renamed_elements[k].second);
		}
	}
	// (the item paths are doc_'s: doc is doc_ inside change())
	Scale t;
	t.bx = dx;
	t.by = dy;
	scale(doc, out, t, 0.0);
	return out;
}

// An op on what is selected ({"selection": true}): move, scale, remove, duplicate, copy, cut.
json Editor::selection_op(const std::string &o, const json &cmd) {
	if (mode_ == Mode::Play && o != "copy") {
		refuse("playing: stop first");
	}
	const std::vector<std::string> sel = selection();
	if (sel.empty()) {
		refuse("nothing selected");
	}
	if (o == "move") {
		const json by = cmd.at("by");
		Scale t;
		t.bx = by[0].get<double>();
		t.by = by[1].get<double>();
		change([&](json &d) { scale(d, sel, t, 0.0); });
		return {};
	}
	if (o == "scale") {
		const json s = cmd.at("by");
		const std::optional<Box> b = box_of(sel);
		if (!b) {
			refuse("the selection has no geometry");
		}
		Scale t;
		t.sx = s.is_array() ? s[0].get<double>() : s.get<double>();
		t.sy = s.is_array() ? s[1].get<double>() : s.get<double>();
		if (!(t.sx > 0 && t.sy > 0)) {
			refuse("scale: positive factors expected");
		}
		const json about = cmd.value("about", jpoint((b->x0 + b->x1) / 2, (b->y0 + b->y1) / 2));
		t.ax = about[0].get<double>();
		t.ay = about[1].get<double>();
		const double step = cmd.value("snap", true) ? (grid_ ? 10.0 : 1.0) : 0.0;
		change([&](json &d) { scale(d, sel, t, step); });
		return {};
	}
	if (o == "copy" || o == "cut") {
		clipboard_ = copied(sel);
		if (o == "cut") {
			selection_op("remove", cmd);
		}
		return {{"copied", sel.size()}};
	}
	if (o == "duplicate") {
		const json clip = copied(sel);
		const json by = cmd.value("by", jpoint(20.0, 20.0));
		std::vector<std::string> made;
		change([&](json &d) { made = paste(d, clip, by[0].get<double>(), by[1].get<double>()); });
		set_selection(made);
		return {{"selection", made}};
	}
	if (o == "remove") {
		json freed = doc_["elements"];
		for (const std::string &e : sel) { // a carrier's elements keep their places, as their own geometry
			if (e.rfind("carrier:", 0) == 0) {
				for (json &x : freed) {
					if (x.is_object() && x.value("carrier", json()) == e.substr(8)) {
						x = off_carrier(x, doc_);
					}
				}
			}
		}
		auto picked = [&](const std::string &e) { return std::find(sel.begin(), sel.end(), e) != sel.end(); };
		change([&](json &d) {
			d["elements"] = freed;
			auto keep = [&](const char *list, const char *kind, bool by_index) {
				if (!d.contains(list) || !d[list].is_array()) {
					return;
				}
				json kept = json::array();
				for (size_t i = 0; i < d[list].size(); i++) {
					const json &j = d[list][i];
					const std::string name = by_index ? std::to_string(i) : j.is_object() ? j.value("name", "") : "";
					if (!picked(std::string(kind) + ":" + name)) {
						kept.push_back(j);
					}
				}
				d[list] = kept;
			};
			keep("elements", "element", false);
			keep("detectors", "detector", false);
			keep("objects", "object", false);
			keep("carriers", "carrier", false);
			keep("colliders", "collider", true);
			if (picked("goal")) {
				d.erase("objective");
			}
			if (picked("camera")) {
				d.erase("camera");
			}
		});
		set_selection({});
		return {};
	}
	refuse("unknown op '" + o + "'");
}

} // namespace rcg
