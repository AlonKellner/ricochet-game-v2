// The level editor: the document and its commands (rcg/editor.hpp,
// docs/EDITOR.md). Input and drawing: editor_view.cpp.
#include "rcg/shade.hpp"
#include "rcg/editor.hpp"

#include "rcg/canon.hpp"
#include "rcg/elements.hpp"
#include "rcg/level2.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rcg {
namespace {

using json = nlohmann::json;

[[noreturn]] void refuse(const std::string &why) { throw std::runtime_error(why); }

json new_level() {
	return json::parse(R"({"elements": [], "name": "untitled", "player": {"start": [960.0, 540.0]},
		"room": {"built": true, "rect": [560.0, 240.0, 800.0, 600.0]}, "schema": "rc-level/2"})");
}

const char *const TOOLS[] = {"select", "line", "arc", "circle", "player", "goal", "detector", "object", "carrier_line", "carrier_circle", "collider"};

// The object an element's geometry lives in: the element, a raw element's surface.
json *geometry_of(json &e) {
	if (e.value("kind", "") == "raw") {
		return e.contains("surface") && e["surface"].is_object() ? &e["surface"] : nullptr;
	}
	return &e;
}

json point(double x, double y) { return json::array({x, y}); }

bool is_point(const json &p) { return p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(); }

void translate(json &g, double dx, double dy) {
	for (const char *k : {"start", "end", "via"}) {
		if (g.contains(k) && is_point(g[k])) {
			g[k] = point(g[k][0].get<double>() + dx, g[k][1].get<double>() + dy);
		}
	}
	if (g.contains("circle") && g["circle"].is_object() && is_point(g["circle"].value("center", json()))) {
		const json &c = g["circle"]["center"];
		g["circle"]["center"] = point(c[0].get<double>() + dx, c[1].get<double>() + dy);
	}
}

// A key code from a command: an int, a one-character string, or a name.
int key_code(const json &k) {
	if (k.is_number_integer()) {
		return k.get<int>();
	}
	std::string s = k.get<std::string>();
	if (s.size() == 1) {
		return std::toupper(static_cast<unsigned char>(s[0]));
	}
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	static const std::pair<const char *, int> NAMES[] = {{"space", 32}, {"escape", Editor::KeyEscape}, {"enter", Editor::KeyEnter},
		{"tab", Editor::KeyTab}, {"backspace", Editor::KeyBackspace}, {"delete", Editor::KeyDelete}, {"left", Editor::KeyLeft},
		{"right", Editor::KeyRight}, {"up", Editor::KeyUp}, {"down", Editor::KeyDown}};
	for (const auto &[name, code] : NAMES) {
		if (s == name) {
			return code;
		}
	}
	if (s.size() >= 2 && s[0] == 'f') {
		const int n = std::stoi(s.substr(1));
		if (n >= 1 && n <= 12) {
			return Editor::KeyF1 + n - 1;
		}
	}
	refuse("unknown key '" + s + "'");
}

unsigned mods_of(const json &m) {
	if (m.is_null()) {
		return 0;
	}
	if (m.is_number_integer()) {
		return m.get<unsigned>();
	}
	unsigned out = 0;
	for (const json &s : m) {
		const std::string v = s.get<std::string>();
		out |= v == "shift" ? Editor::ModShift : v == "ctrl" ? Editor::ModCtrl : v == "alt" ? Editor::ModAlt : 0u;
	}
	return out;
}

} // namespace

Editor::Editor(const json *document) {
	if (document != nullptr) {
		json doc = *document;
		if (doc.is_object() && doc.value("schema", "") == "rc-level/1") {
			doc = migrate_level(doc);
		}
		lower_level(doc); // an error here is the caller's
		set_document(std::move(doc), true);
	} else {
		set_document(new_level(), true);
	}
}

std::string Editor::document_text() const { return canonical(doc_); }

std::string Editor::lowered_text() const { return canonical(lower_level(doc_).level); }

json Editor::kinds() const {
	json out = json::object();
	for (const auto &[kind, tmpl] : elements::builtin()) {
		out[kind] = tmpl;
	}
	if (doc_.contains("templates") && doc_["templates"].is_object()) {
		for (auto it = doc_["templates"].begin(); it != doc_["templates"].end(); ++it) {
			try {
				elements::check_template(it.value(), "level.templates." + it.key());
				json t = it.value();
				if (!t.contains("params")) {
					t["params"] = json::object();
				}
				out[it.key()] = std::move(t);
			} catch (const LevelError &) {
				// reported by refresh()
			}
		}
	}
	return out;
}

std::vector<std::string> Editor::palette() const {
	const json ks = kinds();
	std::vector<std::string> builtin, own;
	for (auto it = ks.begin(); it != ks.end(); ++it) {
		(elements::builtin().count(it.key()) ? builtin : own).push_back(it.key());
	}
	std::stable_sort(builtin.begin(), builtin.end(), [&](const std::string &a, const std::string &b) {
		return ks[a].value("priority", 100.0) < ks[b].value("priority", 100.0);
	});
	builtin.insert(builtin.end(), own.begin(), own.end());
	return builtin;
}

int Editor::index_of(const std::string &name) const {
	const json &es = doc_["elements"];
	for (size_t i = 0; i < es.size(); i++) {
		if (es[i].value("name", "") == name) {
			return int(i);
		}
	}
	return -1;
}

std::string Editor::fresh_name(const std::string &kind) const {
	for (int n = 1;; n++) {
		const std::string name = kind + "_" + std::to_string(n);
		if (index_of(name) < 0) {
			return name;
		}
	}
}

void Editor::set_document(json doc, bool clean) {
	if (!doc.contains("elements") || !doc["elements"].is_array()) {
		doc["elements"] = json::array();
	}
	doc_ = std::move(doc);
	// every element named: the editor's identity of it (the edge walls are not edited)
	for (json &e : doc_["elements"]) {
		if (e.is_object() && !e.contains("name") && e.contains("kind") && e["kind"].is_string() && e["kind"] != "edges") {
			e["name"] = fresh_name(e["kind"].get<std::string>());
		}
	}
	undo_.clear();
	redo_.clear();
	dirty_ = !clean;
	save_requested_ = false;
	selected_.clear();
	item_.clear();
	clicks_.clear();
	drag_.reset();
	focus_.reset();
	refresh();
	frame_all();
}

void Editor::change(const std::function<void(json &)> &edit) {
	json before = doc_;
	edit(doc_);
	if (canonical(before) == canonical(doc_)) {
		return;
	}
	undo_.push_back(std::move(before));
	redo_.clear();
	dirty_ = true;
	refresh();
}

void Editor::refresh() {
	version_++;
	errors_.clear();
	const json &es = doc_["elements"];
	drawn_.assign(es.size(), Drawn{});
	json single = {{"schema", "rc-level/2"}, {"room", {{"rect", {0.0, 0.0, 1.0, 1.0}}, {"built", false}}}};
	for (const char *k : {"bounds", "templates", "carriers"}) {
		if (doc_.contains(k)) {
			single[k] = doc_[k];
		}
	}
	for (size_t i = 0; i < es.size(); i++) {
		const json &e = es[i];
		const std::string name = e.is_object() ? e.value("name", "") : "";
		const std::string at = "level.elements[" + std::to_string(i) + "]";
		auto report = [&](std::string m) {
			const std::string zero = "level.elements[0]";
			if (m.rfind(zero, 0) == 0) {
				m = at + m.substr(zero.size());
			}
			errors_.push_back({int(i), name, m});
		};
		single["elements"] = json::array({e});
		try {
			const Lowered low = lower_level(single);
			const Level lv = parse_level_json(low.level);
			for (size_t k = 0; k < lv.surfaces.size(); k++) {
				if (low.origins[k].rfind("level.elements[0]", 0) != 0) {
					continue; // the edge walls
				}
				const LevelSurface &ls = lv.surfaces[k];
				const std::vector<std::string> why = validate_spec(ls.spec);
				if (why.empty()) {
					for (BuiltSurface &b : build_spec(ls.spec, 1, ls.label)) {
						drawn_[i].built.push_back(std::move(b));
					}
					continue;
				}
				drawn_[i].broken.push_back(segment_of(ls.spec));
				for (const std::string &w : why) {
					report(at + " (" + name + "): " + w);
				}
			}
		} catch (const std::exception &ex) {
			report(ex.what());
			// draw what geometry there is
			json copy = e;
			const json *g = geometry_of(copy);
			if (g != nullptr && g->contains("circle") && (*g)["circle"].is_object() && is_point((*g)["circle"].value("center", json()))
					&& (*g)["circle"].value("radius", json()).is_number()) {
				const json &c = (*g)["circle"];
				drawn_[i].broken.push_back(rc::arc::SegGeom::full_from_carrier(
						rc::arc::from_circle(V2::of(c["center"][0].get<double>(), c["center"][1].get<double>()), c["radius"].get<double>())));
			} else if (g != nullptr && is_point(g->value("start", json())) && is_point(g->value("end", json()))) {
				const V2 s = V2::of((*g)["start"][0].get<double>(), (*g)["start"][1].get<double>());
				const V2 en = V2::of((*g)["end"][0].get<double>(), (*g)["end"][1].get<double>());
				const V2 v = is_point(g->value("via", json())) ? V2::of((*g)["via"][0].get<double>(), (*g)["via"][1].get<double>())
															   : V2::of((double(s.x) + en.x) / 2, (double(s.y) + en.y) / 2);
				drawn_[i].broken.push_back(rc::arc::SegGeom::make(s, en, v));
			}
		}
	}
	// what only the whole level shows (names, the level's own keys, ...)
	shade_fill_ = {};
	shade_of_.assign(es.size(), 0);
	if (errors_.empty()) {
		try {
			const Level lv = parse_level_json(doc_);
			build_level(lv);
			// Stage 153: the shadow under the level's starting flags, and which elements are shades
			if (!lv.shades.empty()) {
				std::vector<shade::Region> regions;
				std::vector<bool> active;
				for (const Shade &sh : lv.shades) {
					regions.push_back(shade::region_of(segment_of(lv.surfaces[sh.surface].spec), sh.side, sh.dark));
					bool on = sh.when.empty();
					for (const auto &[k, v] : lv.flags) {
						on = on || (k == sh.when && std::holds_alternative<bool>(v) && std::get<bool>(v));
					}
					active.push_back(on);
					for (size_t i = 0; i < es.size(); i++) {
						if (es[i].is_object() && es[i].value("name", "") == sh.name) {
							shade_of_[i] = sh.dark ? 1 : 2;
						}
					}
				}
				shade_fill_ = shade::fill(regions, active, lv.bounds);
			}
		} catch (const std::exception &ex) {
			errors_.push_back({-1, "", ex.what()});
		}
	}
	if (index_of(selected_) < 0) {
		selected_.clear();
	}
	if (!item_.empty() && !item_path(item_)) {
		item_.clear(); // (an undo took it away)
	}
}

Editor::Status Editor::status() const {
	Status s;
	s.dirty = dirty_;
	s.errors = int(errors_.size());
	s.mode = mode_;
	s.tool = tool_;
	s.save_requested = save_requested_;
	s.version = version_;
	s.open_requested = !open_request_.empty();
	const json p = doc_.contains("player") && doc_["player"].is_object() ? doc_["player"].value("start", json()) : json();
	if (is_point(p)) {
		s.play_from = V2::of(p[0].get<double>(), p[1].get<double>());
	}
	return s;
}

json Editor::command(const json &cmd) {
	try {
		json r = op(cmd);
		if (!r.is_object()) {
			r = json::object();
		}
		r["ok"] = true;
		return r;
	} catch (const std::exception &e) {
		return {{"ok", false}, {"error", e.what()}};
	}
}

json Editor::op(const json &cmd) {
	if (!cmd.is_object() || !cmd.contains("op") || !cmd["op"].is_string()) {
		refuse("a command {\"op\": ...} expected");
	}
	const std::string o = cmd["op"].get<std::string>();
	auto editing = [&] {
		if (mode_ == Mode::Play) {
			refuse("playing: stop first");
		}
	};
	auto element = [&](const char *key) -> int {
		const std::string name = cmd.at(key).get<std::string>();
		const int i = index_of(name);
		if (i < 0) {
			refuse("no element named '" + name + "'");
		}
		return i;
	};
	if (cmd.contains("item") && (o == "select" || o == "remove" || o == "duplicate" || o == "move" || o == "rename")) {
		// Stage 148: the goal, a detector, an object; Stage 149: a carrier
		return item_op(o, cmd); // Stage 148: the goal, a detector, an object
	}
	if (o == "new") {
		editing();
		set_document(new_level(), true);
		return {};
	}
	if (o == "load") {
		editing();
		json doc = cmd.contains("document") ? cmd["document"] : json::parse(cmd.at("text").get<std::string>());
		if (doc.is_object() && doc.value("schema", "") == "rc-level/1") {
			doc = migrate_level(doc);
		}
		lower_level(doc); // refused unless it is a level
		set_document(std::move(doc), true);
		if (cmd.contains("name") && cmd["name"].is_string()) { // Stage 155: the level it is
			level_name_ = cmd["name"].get<std::string>();
		}
		open_request_.clear();
		confirm_open_.clear();
		level_menu_ = false;
		return {};
	}
	// Stage 155: the level menu -- the host lists the levels and opens the one asked for
	if (o == "levels") {
		levels_.clear();
		for (const json &n : cmd.at("list")) {
			levels_.push_back(n.get<std::string>());
		}
		if (cmd.contains("current") && cmd["current"].is_string()) {
			level_name_ = cmd["current"].get<std::string>();
		}
		return {{"count", levels_.size()}};
	}
	if (o == "level_menu") {
		level_menu_ = cmd.contains("show") ? cmd["show"].get<bool>() : !level_menu_;
		panel_scroll_ = 0;
		return {{"show", level_menu_}};
	}
	if (o == "open" || o == "next_level") {
		std::string name;
		if (o == "open") {
			name = cmd.at("name").get<std::string>();
		} else {
			if (levels_.empty()) {
				refuse("no levels listed (the host lists them: {\"op\": \"levels\"})");
			}
			const long n = long(levels_.size());
			const auto at = std::find(levels_.begin(), levels_.end(), level_name_);
			const long by = cmd.value("by", 1);
			const long k = at == levels_.end() ? (by > 0 ? -1 : 0) : long(at - levels_.begin());
			name = levels_[size_t(((k + by) % n + n) % n)];
		}
		if (dirty_ && confirm_open_ != name) {
			confirm_open_ = name;
			refuse("unsaved changes: ask for '" + name + "' again to discard them, or save first");
		}
		open_request_ = name;
		confirm_open_.clear();
		return {{"name", name}};
	}
	if (o == "open_request") { // the host takes the request (then loads the level with its name)
		const std::string name = open_request_;
		open_request_.clear();
		return {{"name", name}};
	}
	if (o == "add") {
		editing();
		json e = cmd.at("element");
		if (!e.is_object() || !e.contains("kind") || !e["kind"].is_string()) {
			refuse("an element {\"kind\": ..., ...} expected");
		}
		if (!e.contains("name")) {
			e["name"] = fresh_name(e["kind"].get<std::string>());
		} else if (index_of(e["name"].get<std::string>()) >= 0) {
			refuse("'" + e["name"].get<std::string>() + "' names another element");
		}
		const std::string name = e["name"].get<std::string>();
		change([&](json &d) { d["elements"].push_back(e); });
		select_element(name);
		return {{"name", name}};
	}
	if (o == "remove") {
		editing();
		const int i = element("name");
		change([&](json &d) { d["elements"].erase(size_t(i)); });
		return {};
	}
	if (o == "rename") {
		editing();
		const int i = element("name");
		const std::string from = cmd["name"].get<std::string>(), to = cmd.at("to").get<std::string>();
		if (to.empty() || (to != from && index_of(to) >= 0)) {
			refuse(to.empty() ? "a name expected" : "'" + to + "' names another element");
		}
		if (selected_ == from) {
			selected_ = to; // before the refresh, which drops a selection whose name is gone
		}
		change([&](json &d) {
			d["elements"][size_t(i)]["name"] = to;
			for (json &e : d["elements"]) { // the buttons that open it follow
				if (e.value("opens", json()) == from) {
					e["opens"] = to;
				}
			}
		});
		return {};
	}
	if (o == "set") {
		editing();
		const json value = cmd.value("value", json());
		if (cmd.contains("path")) {
			const json::json_pointer ptr(cmd["path"].get<std::string>());
			if (ptr.empty()) {
				refuse("set: a path inside the document expected");
			}
			change([&](json &d) {
				if (value.is_null()) {
					json &parent = d[ptr.parent_pointer()];
					if (parent.is_object()) {
						parent.erase(ptr.back());
					}
				} else {
					d[ptr] = value;
				}
			});
			return {};
		}
		const int i = element("name");
		const std::string key = cmd.at("key").get<std::string>();
		if (key == "name") {
			return op({{"op", "rename"}, {"name", cmd["name"]}, {"to", value}});
		}
		if (key == "kind" && !value.is_string()) {
			refuse("an element's kind is a string");
		}
		change([&](json &d) {
			json &e = d["elements"][size_t(i)];
			if (value.is_null()) {
				e.erase(key);
			} else {
				e[key] = value;
			}
		});
		return {};
	}
	if (o == "move") {
		editing();
		const int i = element("name");
		const std::string h = cmd.at("handle").get<std::string>();
		change([&](json &d) {
			json *g = geometry_of(d["elements"][size_t(i)]);
			if (g == nullptr) {
				refuse("the element has no geometry");
			}
			if (h == "body") {
				const json by = cmd.at("by");
				translate(*g, by[0].get<double>(), by[1].get<double>());
			} else if (h == "start" || h == "end" || h == "via") {
				(*g)[h] = cmd.at("to");
			} else if (h == "center") {
				(*g)["circle"]["center"] = cmd.at("to");
			} else if (h == "radius") {
				(*g)["circle"]["radius"] = cmd.at("to");
			} else {
				refuse("handle: start, end, via, center, radius or body expected");
			}
		});
		return {};
	}
	if (o == "duplicate") {
		editing();
		const int i = element("name");
		json e = doc_["elements"][size_t(i)];
		const std::string name = fresh_name(e.value("kind", "element"));
		e["name"] = name;
		const json by = cmd.value("by", point(20.0, 20.0));
		if (json *g = geometry_of(e)) {
			translate(*g, by[0].get<double>(), by[1].get<double>());
		}
		change([&](json &d) { d["elements"].insert(d["elements"].begin() + i + 1, e); });
		select_element(name);
		return {{"name", name}};
	}
	if (o == "select") {
		if (cmd.value("name", json()).is_null()) {
			selected_.clear();
			item_.clear();
		} else {
			select_element(doc_["elements"][size_t(element("name"))]["name"].get<std::string>());
		}
		return {};
	}
	if (o == "tool") {
		const std::string t = cmd.at("tool").get<std::string>();
		for (int k = 0; k < int(std::size(TOOLS)); k++) {
			if (t == TOOLS[k]) {
				tool_ = Tool(k);
				clicks_.clear();
				return {};
			}
		}
		refuse("tool: select, line, arc, circle, player, goal, detector, object, carrier_line, carrier_circle or collider expected");
	}
	if (o == "kind") {
		const std::string k = cmd.at("kind").get<std::string>();
		if (!kinds().contains(k)) {
			refuse("unknown element kind '" + k + "'");
		}
		kind_ = k;
		return {};
	}
	if (o == "snap") {
		grid_ = cmd.at("grid").get<bool>();
		return {};
	}
	if (o == "view") {
		if (cmd.contains("center")) {
			center_ = V2::of(cmd["center"][0].get<double>(), cmd["center"][1].get<double>());
		}
		if (cmd.contains("zoom")) {
			zoom_ = std::clamp(cmd["zoom"].get<double>(), 0.05, 20.0);
		}
		return {};
	}
	if (o == "frame") {
		frame_all();
		return {};
	}
	if (o == "undo" || o == "redo") {
		editing();
		auto &from = o == "undo" ? undo_ : redo_;
		auto &to = o == "undo" ? redo_ : undo_;
		if (from.empty()) {
			refuse("nothing to " + o);
		}
		to.push_back(doc_);
		doc_ = std::move(from.back());
		from.pop_back();
		dirty_ = true;
		refresh();
		return {};
	}
	if (o == "validate") {
		json errs = json::array();
		for (const Error &e : errors_) {
			errs.push_back({{"element", e.element}, {"name", e.name}, {"message", e.message}});
		}
		return {{"errors", errs}};
	}
	if (o == "save") {
		save_requested_ = true;
		return {};
	}
	if (o == "save_cancelled") { // the host could not save (a dialog cancelled): the document stays unsaved
		save_requested_ = false;
		return {};
	}
	if (o == "saved") {
		save_requested_ = false;
		dirty_ = false;
		return {};
	}
	if (o == "play") {
		if (!errors_.empty()) {
			refuse(std::to_string(errors_.size()) + " problem(s): " + errors_.front().message);
		}
		mode_ = Mode::Play;
		clicks_.clear();
		drag_.reset();
		const Status s = status();
		return {{"from", point(s.play_from.x, s.play_from.y)}};
	}
	if (o == "stop") {
		mode_ = Mode::Edit;
		return {};
	}
	if (o == "document") {
		return {{"document", doc_}, {"text", document_text()}};
	}
	if (o == "lower") {
		return {{"level", lower_level(doc_).level}};
	}
	if (o == "state") {
		const Status s = status();
		return {{"tool", TOOLS[int(tool_)]}, {"kind", kind_}, {"selected", selected_.empty() ? json() : json(selected_)},
				{"item", item_.empty() ? json() : json(item_)},
				{"mode", mode_ == Mode::Play ? "play" : "edit"}, {"dirty", dirty_}, {"errors", s.errors}, {"grid", grid_},
				{"placing", clicks_.size()}, {"undo", undo_.size()}, {"redo", redo_.size()},
				{"view", {{"center", point(center_.x, center_.y)}, {"zoom", zoom_}}}, {"focus", focus_ ? json(focus_->key) : json()},
				{"save_requested", save_requested_}, {"hint", hint_}, {"level", level_name_}, {"levels", levels_.size()},
				{"level_menu", level_menu_}, {"open_requested", !open_request_.empty()}};
	}
	if (o == "panel") { // the panel's rows as drawn (tests, debugging): label, value, y
		json rs = json::array();
		for (const Row &r : rows()) {
			static const char *const KINDS[] = {"header", "text", "value", "error", "buttons", "chips", "nav", "level", "tools"};
			rs.push_back({{"kind", KINDS[int(r.kind)]}, {"label", r.label}, {"value", r.value}, {"y", r.y},
					{"x", width_ - PANEL_WIDTH + 20}});
		}
		return {{"rows", rs}};
	}
	if (o == "pointer") {
		const std::string k = cmd.value("kind", "move");
		const int kind = k == "down" ? PtrDown : k == "up" ? PtrUp : PtrMove;
		const std::string b = cmd.value("button", "left");
		const unsigned button = b == "right" ? BtnRight : b == "middle" ? BtnMiddle : b == "none" ? 0u : BtnLeft;
		pointer(kind, cmd.at("x").get<float>(), cmd.at("y").get<float>(), button, mods_of(cmd.value("mods", json())));
		return {};
	}
	if (o == "key") {
		const unsigned mods = mods_of(cmd.value("mods", json()));
		const int code = key_code(cmd.at("key"));
		if (cmd.contains("down")) {
			key(code, mods, cmd["down"].get<bool>());
		} else {
			key(code, mods, true);
			key(code, mods, false);
		}
		return {};
	}
	if (o == "text") {
		text(cmd.at("text").get<std::string>());
		return {};
	}
	if (o == "scroll") {
		scroll(cmd.value("dx", 0.0f), cmd.value("dy", 0.0f), mods_of(cmd.value("mods", json())));
		return {};
	}
	if (o == "resize") {
		resize(cmd.at("w").get<float>(), cmd.at("h").get<float>());
		return {};
	}
	refuse("unknown op '" + o + "'");
}

} // namespace rcg
