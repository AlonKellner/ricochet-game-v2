// The level editor (Stage 148): the level's items -- the goal, the detectors,
// the objects -- selected, dragged, reshaped and edited like elements
// (rcg/editor.hpp). An item is named "goal", "detector:<name>" or
// "object:<name>"; its handles move the JSON they point at.
#include "rcg/canon.hpp"
#include "rcg/editor.hpp"
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
	if (!path) {
		return out;
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
	head.label = kind == "goal" || kind == "camera" ? kind : kind + " " + name;
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
	keys.label = kind == "goal" || kind == "camera" ? "Delete: remove the " + kind + "; arrows: move it" : "Delete: remove; ctrl+D: duplicate; arrows: move";
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
		selected_.clear();
		item_ = item;
		return {};
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
		item_ = "collider:" + std::to_string(at);
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
		item_ = kind + ":" + nn;
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

} // namespace rcg
