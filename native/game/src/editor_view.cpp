// The level editor: input, the view and the draw list (rcg/editor.hpp).
#include "rcg/editor.hpp"

#include "rcg/canon.hpp"
#include "rcg/elements.hpp"
#include "rcg/kinematics.hpp"
#include "rcg/level2.hpp"

#include <algorithm>
#include <cmath>

namespace rcg {
namespace {

using json = nlohmann::json;
using render::Call;
using render::Color;
using render::Kind;

constexpr double TAU = 6.283185307179586;
constexpr float HANDLE = 5.0f;       // a handle's half size (px)
constexpr float PICK = 8.0f;         // how near a click picks (px)
constexpr float ROW = 21.0f;         // a panel row
constexpr int FONT = 14;
constexpr float VALUE_X = 120.0f;    // the panel's value column
constexpr size_t WRAP = 42;          // the panel's text width (characters)

Color rgba(double r, double g, double b, double a = 1.0) { return {float(r), float(g), float(b), float(a)}; }

// The game's colours (session_draw.cpp: SurfaceNode.config_color and its neighbours)
const Color WHITE{1, 1, 1, 1};
const Color GRAY = render::hex(0xBEBEBEFF);
const Color BLUE = rgba(0.25, 0.45, 1.0); // the game's blue, lifted for the dark canvas
const Color PURPLE = render::hex(0xA020F0FF);
const Color RED = render::hex(0xFF0000FF);
const Color CYAN = render::hex(0x00FFFFFF);
const Color GREEN = rgba(0.3, 0.9, 0.4);
const Color GHOST_TOGGLER = rgba(0.75, 0.95, 1.0);
const Color FLAG_TOGGLER = rgba(1.0, 0.85, 0.2);
const Color TARGET = rgba(1.0, 0.85, 0.3);
const Color SPAWN = rgba(0.6, 0.95, 1.0);
const Color RESET = rgba(1.0, 0.55, 0.85);
const Color PRE_TRANSFORM = rgba(0.55, 0.45, 0.85, 0.5);
const Color CANVAS = rgba(0.07, 0.07, 0.09);
const Color GRID_FINE = rgba(1, 1, 1, 0.04);
const Color GRID_COARSE = rgba(1, 1, 1, 0.09);
const Color BOUNDS = rgba(0.6, 0.6, 0.6, 0.6);
const Color PANEL = rgba(0.12, 0.12, 0.15, 0.97);
const Color TEXT = rgba(0.9, 0.9, 0.92);
const Color DIM = rgba(0.6, 0.6, 0.66);
const Color ACCENT = rgba(1.0, 0.8, 0.3);
const Color ERROR_TEXT = rgba(1.0, 0.45, 0.4);
const Color SELECT = rgba(1, 1, 1, 0.35);
const Color HOVER = rgba(1, 1, 1, 0.15);
const Color GOAL = rgba(0.3, 0.95, 0.45);
const Color DETECTOR = rgba(1.0, 0.8, 0.25, 0.85);
const Color OBJECT = rgba(0.95, 0.6, 0.3);
const Color CARRIER = rgba(0.45, 0.7, 1.0, 0.45);

// An element's layers (the draw nodes' keys: ["editor", layer, index])
enum Layer { L_CANVAS, L_GRID, L_ROOM, L_ELEMENT, L_PLAYER, L_PREVIEW, L_HANDLES, L_PANEL, L_STATUS, L_GOALS, L_CARRIERS, L_SHADOW };

const char *const TOOL_NAMES[] = {"select", "line", "arc", "circle", "player", "goal", "detector", "object", "carrier_line", "carrier_circle"};
// Stage 161: the tool palette's labels (with their keys)
const char *const TOOL_LABELS[] = {"select  V", "line  L", "arc  A", "circle  C", "player  P", "goal  O", "detector  D", "object  B",
	"line carrier  K", "circle carrier  shift K"};

Call line(const V2 &a, const V2 &b, const Color &c, double w) {
	Call k;
	k.kind = Kind::Line;
	k.pts = {a, b};
	k.colors = {c};
	k.width = w;
	return k;
}

Call dashed(const V2 &a, const V2 &b, const Color &c, double w, double dash) {
	Call k = line(a, b, c, w);
	k.kind = Kind::DashedLine;
	k.dash = dash;
	return k;
}

Call rect(const V2 &pos, const V2 &size, const Color &c) {
	Call k;
	k.kind = Kind::Rect;
	k.pts = {pos, size};
	k.colors = {c};
	return k;
}

Call circle(const V2 &c, double r, const Color &col) {
	Call k;
	k.kind = Kind::Circle;
	k.pts = {c};
	k.radius = r;
	k.colors = {col};
	return k;
}

Call arc(const V2 &c, double r, double a0, double a1, const Color &col, double w) {
	Call k;
	k.kind = Kind::Arc;
	k.pts = {c};
	k.radius = r;
	k.start = a0;
	k.end = a1;
	k.point_count = std::clamp(int(std::abs(a1 - a0) * r / 4.0), 8, 256);
	k.colors = {col};
	k.width = w;
	return k;
}

Call text_at(const V2 &p, const std::string &s, const Color &c, int size = FONT) {
	Call k;
	k.kind = Kind::Text;
	k.pts = {p};
	k.text = s;
	k.font_size = size;
	k.colors = {c};
	return k;
}

double wrap_angle(double a) {
	a = std::fmod(a, TAU);
	return a < 0 ? a + TAU : a;
}

V2 pt(const json &p) { return V2::of(p[0].get<double>(), p[1].get<double>()); }
bool is_point(const json &p) { return p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(); }
json jpoint(double x, double y) { return json::array({x, y}); }

std::string spell(const json &v) {
	if (v.is_string()) {
		return v.get<std::string>();
	}
	if (v.is_null()) {
		return "";
	}
	std::string s = canonical(v);
	if (!s.empty() && s.back() == '\n') {
		s.pop_back();
	}
	// a multi-line value on one line
	std::string out;
	bool space = false;
	for (const char c : s) {
		if (c == '\n') {
			space = true;
			continue;
		}
		if (space && c == ' ') {
			continue;
		}
		if (space && !out.empty() && out.back() != '[' && out.back() != '{' && c != ']' && c != '}') {
			out += ' ';
		}
		space = false;
		out += c;
	}
	return out;
}

std::vector<std::string> wrap(const std::string &s, size_t width) {
	std::vector<std::string> out;
	std::string line, word;
	auto flush_word = [&] {
		if (word.empty()) {
			return;
		}
		if (!line.empty() && line.size() + 1 + word.size() > width) {
			out.push_back(line);
			line.clear();
		}
		while (word.size() > width) {
			out.push_back(word.substr(0, width));
			word = word.substr(width);
		}
		line += (line.empty() ? "" : " ") + word;
		word.clear();
	};
	for (const char c : s) {
		if (c == ' ') {
			flush_word();
		} else {
			word += c;
		}
	}
	flush_word();
	if (!line.empty()) {
		out.push_back(line);
	}
	return out;
}

// The colour of a side, as the game colours it (SurfaceNode.config_color).
Color side_color(const BuiltSide *s) {
	if (s == nullptr) {
		return GRAY;
	}
	const std::string &t = s->effect.type;
	if (t == "pass") {
		for (const HitSpec &h : s->hits) {
			if (h.kind == "state" && h.key == "ghost") {
				return GHOST_TOGGLER;
			}
			if (h.kind == "target") {
				return TARGET;
			}
			if (h.kind == "spawn") {
				return SPAWN;
			}
			if (h.kind == "reset") {
				return RESET;
			}
		}
		return s->hits.empty() ? GRAY : FLAG_TOGGLER;
	}
	if (t == "terminal") return RED;
	if (t == "reflection") return BLUE;
	if (t == "inversion") return PURPLE;
	if (t == "portal") return CYAN;
	return s->effect.back ? rgba(1.0, 0.4, 0.1) : rgba(1.0, 0.8, 0.2);
}

// A surface's side config as the editor shows it: fixed, or a resolver's default row.
const BuiltSide *shown_side(const BuiltSurface &b, bool left) {
	if (b.resolver.empty()) {
		return left ? (b.left ? &*b.left : nullptr) : (b.right ? &*b.right : nullptr);
	}
	const BuiltRow *row = &b.resolver.front();
	for (const BuiltRow &r : b.resolver) {
		if (r.value == "DEFAULT") {
			row = &r;
		}
	}
	return left ? (row->left ? &*row->left : nullptr) : (row->right ? &*row->right : nullptr);
}

} // namespace

// ---------------------------------------------------------------- the view

V2 Editor::to_screen(const V2 &w) const {
	const double cx = (width_ - PANEL_WIDTH) / 2.0, cy = (height_ - STATUS_HEIGHT) / 2.0;
	return V2::of((double(w.x) - center_.x) * zoom_ + cx, (double(w.y) - center_.y) * zoom_ + cy);
}

V2 Editor::to_world(const V2 &s) const {
	const double cx = (width_ - PANEL_WIDTH) / 2.0, cy = (height_ - STATUS_HEIGHT) / 2.0;
	return V2::of((double(s.x) - cx) / zoom_ + center_.x, (double(s.y) - cy) / zoom_ + center_.y);
}

void Editor::frame_all() {
	json b = doc_.value("bounds", json::array({0.0, 0.0, 1920.0, 1080.0}));
	if (!b.is_array() || b.size() != 4) {
		b = json::array({0.0, 0.0, 1920.0, 1080.0});
	}
	const double bx = b[0].get<double>(), by = b[1].get<double>(), bw = std::max(1.0, b[2].get<double>()), bh = std::max(1.0, b[3].get<double>());
	const double cw = std::max(100.0, double(width_ - PANEL_WIDTH)), ch = std::max(100.0, double(height_ - STATUS_HEIGHT));
	zoom_ = std::min(cw / bw, ch / bh) * 0.95;
	center_ = V2::of(bx + bw / 2, by + bh / 2);
}

void Editor::resize(float width, float height) {
	width_ = std::max(width, PANEL_WIDTH + 100.0f);
	height_ = std::max(height, STATUS_HEIGHT + 100.0f);
	if (!view_touched_) {
		frame_all(); // (the level fits the window until the user moves the view)
	}
}

void Editor::scroll(float, float dy, unsigned) {
	if (mode_ == Mode::Play || dy == 0.0f) {
		return;
	}
	if (mouse_.x >= width_ - PANEL_WIDTH) {
		// the panel: scrolled, as far as its rows reach
		const float before_scroll = panel_scroll_;
		panel_scroll_ = 0;
		const std::vector<Row> rs = rows();
		const float bottom = rs.empty() ? 0.0f : rs.back().y + ROW + 20.0f;
		panel_scroll_ = std::clamp(before_scroll + dy * 3 * ROW, 0.0f, std::max(0.0f, bottom - height_));
		return;
	}
	view_touched_ = true;
	const V2 before = to_world(mouse_);
	zoom_ = std::clamp(zoom_ * std::pow(1.1, -double(dy)), 0.05, 20.0);
	const V2 after = to_world(mouse_);
	center_ = V2::of(double(center_.x) + before.x - after.x, double(center_.y) + before.y - after.y);
}

// ---------------------------------------------------------------- picking

std::vector<Editor::Handle> Editor::handles() const {
	std::vector<Handle> out;
	const int i = index_of(selected_);
	if (i >= 0) {
		json e = doc_["elements"][size_t(i)];
		const json &g = e.value("kind", "") == "raw" ? e.value("surface", json::object()) : e;
		if (e.contains("carrier")) {
			// Stage 149: on a carrier -- its from, to (and via) where the lowering puts them (the first surface's)
			const size_t k = size_t(i);
			if (k < drawn_.size() && !drawn_[k].built.empty()) {
				const rc::arc::SegGeom &sg = drawn_[k].built.front().seg;
				if (!sg.full) {
					out.push_back({selected_, "from", sg.s, ""});
					out.push_back({selected_, "to", sg.e, ""});
					if (!sg.is_line()) {
						out.push_back({selected_, "via", sg.v, ""});
					}
				}
			}
		} else if (g.contains("circle") && g["circle"].is_object() && is_point(g["circle"].value("center", json()))) {
			const V2 c = pt(g["circle"]["center"]);
			out.push_back({selected_, "center", c, ""});
			if (g["circle"].value("radius", json()).is_number()) {
				out.push_back({selected_, "radius", V2::of(double(c.x) + g["circle"]["radius"].get<double>(), c.y), ""});
			}
		} else if (is_point(g.value("start", json())) && is_point(g.value("end", json()))) {
			const V2 s = pt(g["start"]), en = pt(g["end"]);
			out.push_back({selected_, "start", s, ""});
			out.push_back({selected_, "end", en, ""});
			if (is_point(g.value("via", json()))) {
				out.push_back({selected_, "via", pt(g["via"]), ""});
			} else {
				out.push_back({selected_, "mid", V2::of((double(s.x) + en.x) / 2, (double(s.y) + en.y) / 2), ""});
			}
		}
	}
	for (Handle &h : item_handles()) {
		out.push_back(std::move(h));
	}
	if (doc_.contains("player") && doc_["player"].is_object() && is_point(doc_["player"].value("start", json()))) {
		out.push_back({"", "player", pt(doc_["player"]["start"]), ""});
	}
	return out;
}

std::optional<Editor::Handle> Editor::handle_at(const V2 &screen) const {
	std::optional<Handle> best;
	double best_d = PICK;
	for (const Handle &h : handles()) {
		const V2 s = to_screen(h.at);
		const double d = std::hypot(double(s.x) - screen.x, double(s.y) - screen.y);
		if (d <= best_d) {
			best_d = d;
			best = h;
		}
	}
	return best;
}
// (the item helpers: editor_items.cpp)

int Editor::element_at(const V2 &screen) const {
	const V2 w = to_world(screen);
	const double tol = PICK / zoom_;
	int best = -1;
	double best_d = tol;
	for (size_t i = 0; i < drawn_.size(); i++) {
		auto test = [&](const rc::arc::SegGeom &seg) {
			const kinematics::Collider c = kinematics::collider_of(seg);
			double nx = 0, ny = 0;
			const double d = kinematics::distance(c, w.x, w.y, nx, ny);
			if (d <= best_d) { // the later element wins a tie (drawn on top)
				best_d = d;
				best = int(i);
			}
		};
		for (const BuiltSurface &b : drawn_[i].built) {
			test(b.seg);
		}
		for (const rc::arc::SegGeom &s : drawn_[i].broken) {
			test(s);
		}
	}
	return best;
}

// A point placed at the pointer: another handle's point exactly (the same
// value: an endpoint shared by provenance, GAME_SPEC 31.4), else the grid (10
// units; off: whole units), Alt: the pointer as it is (float32).
json Editor::snap(const V2 &screen, unsigned mods, const Handle *moving, std::string *carrier) const {
	const V2 w = to_world(screen);
	if (carrier != nullptr) {
		carrier->clear();
	}
	if (mods & ModAlt) {
		return jpoint(w.x, w.y);
	}
	std::optional<json> best;
	double best_d = PICK;
	auto consider = [&](const json &p, const std::string &element, const std::string &kind) {
		if (!is_point(p) || (moving != nullptr && moving->element == element && moving->kind == kind)) {
			return;
		}
		const V2 s = to_screen(pt(p));
		const double d = std::hypot(double(s.x) - screen.x, double(s.y) - screen.y);
		if (d <= best_d) {
			best_d = d;
			best = p;
		}
	};
	for (const json &e : doc_["elements"]) {
		if (!e.is_object()) {
			continue;
		}
		const std::string name = e.value("name", "");
		const json &g = e.value("kind", "") == "raw" ? e.value("surface", json::object()) : e;
		for (const char *k : {"start", "end", "via"}) {
			consider(g.value(k, json()), name, k);
		}
		if (g.contains("circle") && g["circle"].is_object()) {
			consider(g["circle"].value("center", json()), name, "center");
		}
	}
	if (doc_.contains("player") && doc_["player"].is_object()) {
		consider(doc_["player"].value("start", json()), "", "player");
	}
	if (best) {
		return *best;
	}
	// Stage 149: onto a carrier within reach (its point nearest the pointer, a grid step along it)
	if (moving == nullptr || moving->item.rfind("carrier:", 0) != 0) {
		const std::string near = carrier_near(screen);
		if (!near.empty()) {
			const json &cs = doc_["carriers"];
			for (const json &c : cs) {
				if (c.value("name", "") != near) {
					continue;
				}
				if (carrier != nullptr) {
					*carrier = near;
				}
				const double step = grid_ ? 10.0 : 1.0;
				if (c.contains("line")) {
					const double ax = c["line"]["a"][0].get<double>(), ay = c["line"]["a"][1].get<double>();
					const double dx = c["line"]["b"][0].get<double>() - ax, dy = c["line"]["b"][1].get<double>() - ay;
					const double len = std::hypot(dx, dy);
					const double t = std::round(((double(w.x) - ax) * dx + (double(w.y) - ay) * dy) / len / step) * step / len;
					return jpoint(ax + t * dx, ay + t * dy);
				}
				const double cx = c["circle"]["center"][0].get<double>(), cy = c["circle"]["center"][1].get<double>(), r = c["circle"]["radius"].get<double>();
				const double a = std::atan2(double(w.y) - cy, double(w.x) - cx);
				const double da = step / r; // a grid step along the arc
				const double sa = std::round(a / da) * da;
				return jpoint(cx + r * std::cos(sa), cy + r * std::sin(sa));
			}
		}
	}
	const double step = grid_ ? 10.0 : 1.0;
	return jpoint(std::round(double(w.x) / step) * step, std::round(double(w.y) / step) * step);
}

std::string Editor::carrier_near(const V2 &screen) const {
	if (!doc_.contains("carriers") || !doc_["carriers"].is_array()) {
		return "";
	}
	const V2 w = to_world(screen);
	std::string best;
	double best_d = PICK / zoom_;
	for (const json &c : doc_["carriers"]) {
		if (!c.is_object()) {
			continue;
		}
		double d = 1e300;
		if (c.contains("line") && c["line"].is_object() && is_point(c["line"].value("a", json())) && is_point(c["line"].value("b", json()))) {
			const double ax = c["line"]["a"][0].get<double>(), ay = c["line"]["a"][1].get<double>();
			const double dx = c["line"]["b"][0].get<double>() - ax, dy = c["line"]["b"][1].get<double>() - ay;
			const double len = std::hypot(dx, dy);
			if (len > 0) {
				d = std::abs((double(w.x) - ax) * dy - (double(w.y) - ay) * dx) / len;
			}
		} else if (c.contains("circle") && c["circle"].is_object() && is_point(c["circle"].value("center", json()))
				&& c["circle"].value("radius", json()).is_number()) {
			const V2 ctr = pt(c["circle"]["center"]);
			d = std::abs(std::hypot(double(w.x) - ctr.x, double(w.y) - ctr.y) - c["circle"]["radius"].get<double>());
		}
		if (d <= best_d) {
			best_d = d;
			best = c.value("name", "");
		}
	}
	return best;
}

// ---------------------------------------------------------------- input

void Editor::place(const json &p, unsigned) {
	const json ks = kinds();
	const json tmpl = ks.value(kind_, json::object());
	if (!clicks_.empty() && canonical(clicks_.back()) == canonical(p)) {
		return; // the same point twice
	}
	if (tool_ == Tool::Line && tmpl.value("geometry", "any") == "circle") {
		hint_ = kind_ + " needs an arc or a circle (A, C)";
		return;
	}
	clicks_.push_back(p);
	click_carriers_.resize(clicks_.size());
	click_carriers_.back() = carrier_of_click_;
	const size_t need = tool_ == Tool::Arc ? 3 : 2;
	if (clicks_.size() < need) {
		return;
	}
	if (tool_ == Tool::CarrierLine || tool_ == Tool::CarrierCircle) { // Stage 149: a carrier
		json cs = doc_.value("carriers", json::array());
		if (!cs.is_array()) {
			cs = json::array();
		}
		std::string name;
		for (int n = 1;; n++) {
			name = "carrier_" + std::to_string(n);
			bool used = false;
			for (const json &c : cs) {
				used = used || (c.is_object() && c.value("name", "") == name);
			}
			if (!used) {
				break;
			}
		}
		json c = {{"name", name}};
		if (tool_ == Tool::CarrierLine) {
			c["line"] = {{"a", clicks_[0]}, {"b", clicks_[1]}};
		} else {
			const V2 ctr = pt(clicks_[0]), q = pt(clicks_[1]);
			const double step = grid_ ? 10.0 : 1.0;
			c["circle"] = {{"center", clicks_[0]}, {"radius", std::max(step, std::round(std::hypot(double(q.x) - ctr.x, double(q.y) - ctr.y) / step) * step)}};
		}
		clicks_.clear();
		click_carriers_.clear();
		cs.push_back(c);
		const json res = command({{"op", "set"}, {"path", "/carriers"}, {"value", cs}});
		hint_ = res.value("ok", false) ? name + " placed: line and arc clicks on it place elements on it" : res.value("error", std::string());
		return;
	}
	if (tool_ == Tool::Goal || tool_ == Tool::Detector) {
		// two corners: a rectangle
		const double x0 = clicks_[0][0].get<double>(), y0 = clicks_[0][1].get<double>(), x1 = clicks_[1][0].get<double>(), y1 = clicks_[1][1].get<double>();
		clicks_.clear();
		const json r = json::array({std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)});
		if (r[2].get<double>() <= 0 || r[3].get<double>() <= 0) {
			hint_ = "a rectangle needs two corners apart in x and y";
			return;
		}
		json res;
		if (tool_ == Tool::Goal) {
			res = command({{"op", "set"}, {"path", "/objective/goal"}, {"value", {{"rect", r}}}});
			hint_ = res.value("ok", false) ? "the goal placed" : res.value("error", std::string());
			return;
		}
		json ds = doc_.value("detectors", json::array());
		if (!ds.is_array()) {
			ds = json::array();
		}
		std::string name;
		for (int n = 1;; n++) {
			name = "detector_" + std::to_string(n);
			bool used = false;
			for (const json &d : ds) {
				used = used || (d.is_object() && d.value("name", "") == name);
			}
			if (!used) {
				break;
			}
		}
		ds.push_back({{"name", name}, {"region", {{"rect", r}}}, {"mode", "enter"}});
		res = command({{"op", "set"}, {"path", "/detectors"}, {"value", ds}});
		hint_ = res.value("ok", false) ? name + " placed: it writes det:" + name : res.value("error", std::string());
		return;
	}
	json e = {{"kind", kind_}};
	if (tmpl.contains("example")) {
		for (auto it = tmpl["example"].begin(); it != tmpl["example"].end(); ++it) {
			e[it.key()] = it.value();
		}
	}
	// Stage 149: every click on one carrier (a line's two, an arc's three on a circle): the element is on it
	const bool on_one = (tool_ == Tool::Line || tool_ == Tool::Arc) && !click_carriers_.empty() && !click_carriers_[0].empty()
			&& std::all_of(click_carriers_.begin(), click_carriers_.end(), [&](const std::string &c) { return c == click_carriers_[0]; });
	if (on_one) {
		e["carrier"] = click_carriers_[0];
		e["from"] = clicks_[0];
		e["to"] = clicks_[1];
		if (tool_ == Tool::Arc) {
			e["via"] = clicks_[2];
		}
		clicks_.clear();
		click_carriers_.clear();
		const json r = command({{"op", "add"}, {"element", e}});
		hint_ = r.value("ok", false) ? "placed " + r.value("name", std::string()) + " on " + e["carrier"].get<std::string>() : r.value("error", std::string());
		return;
	} else if (tool_ == Tool::Circle) {
		const V2 c = pt(clicks_[0]), q = pt(clicks_[1]);
		const double step = grid_ ? 10.0 : 1.0;
		const double r = std::max(step, std::round(std::hypot(double(q.x) - c.x, double(q.y) - c.y) / step) * step);
		e["circle"] = {{"center", clicks_[0]}, {"radius", r}};
	} else {
		e["start"] = clicks_[0];
		e["end"] = clicks_[1];
		if (tool_ == Tool::Arc) {
			e["via"] = clicks_[2];
		}
	}
	clicks_.clear();
	click_carriers_.clear();
	const json r = command({{"op", "add"}, {"element", e}});
	hint_ = r.value("ok", false) ? "placed " + r.value("name", std::string()) : r.value("error", std::string());
}

void Editor::drag_to(const V2 &screen, unsigned mods) {
	if (!drag_->item.empty()) {
		drag_item(screen, mods);
		return;
	}
	const int i = drag_->element.empty() ? -1 : index_of(drag_->element);
	if (drag_->kind == "player") {
		const json p = snap(screen, mods, &*drag_);
		if (!doc_.contains("player") || !doc_["player"].is_object()) {
			doc_["player"] = json::object();
		}
		if (canonical(doc_["player"].value("start", json())) != canonical(p)) {
			doc_["player"]["start"] = p;
			drag_changed_ = true;
			refresh();
		}
		return;
	}
	if (i < 0) {
		return;
	}
	json &e = doc_["elements"][size_t(i)];
	json &g = e.value("kind", "") == "raw" ? e["surface"] : e;
	if (drag_->kind == "body") {
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
		for (const char *k : {"start", "end", "via", "from", "to"}) {
			if (is_point(g.value(k, json()))) {
				g[k] = jpoint(g[k][0].get<double>() + dx, g[k][1].get<double>() + dy);
			}
		}
		if (g.contains("circle") && g["circle"].is_object() && is_point(g["circle"].value("center", json()))) {
			const json c = g["circle"]["center"];
			g["circle"]["center"] = jpoint(c[0].get<double>() + dx, c[1].get<double>() + dy);
		}
		drag_last_ = V2::of(double(drag_last_.x) + dx, double(drag_last_.y) + dy);
		drag_changed_ = true;
		refresh();
		return;
	}
	const json p = snap(screen, mods, &*drag_);
	if (drag_->kind == "radius") {
		const V2 c = pt(g["circle"]["center"]), q = to_world(screen); // (the radius is rounded, not the point)
		const double step = grid_ && !(mods & ModAlt) ? 10.0 : 1.0;
		const double r = std::max(step, std::round(std::hypot(double(q.x) - c.x, double(q.y) - c.y) / step) * step);
		if (g["circle"]["radius"] != r) {
			g["circle"]["radius"] = r;
			drag_changed_ = true;
			refresh();
		}
		return;
	}
	if (drag_->kind == "from" || drag_->kind == "to" || (drag_->kind == "via" && e.contains("carrier"))) {
		const json p2 = snap(screen, mods, &*drag_);
		if (canonical(e.value(drag_->kind, json())) != canonical(p2)) {
			e[drag_->kind] = p2;
			drag_changed_ = true;
			refresh();
		}
		return;
	}
	std::string key = drag_->kind == "center" ? "" : drag_->kind == "mid" ? "via" : drag_->kind;
	json &slot = key.empty() ? g["circle"]["center"] : g[key];
	if (canonical(slot) != canonical(p)) {
		slot = p;
		if (drag_->kind == "mid") {
			drag_->kind = "via"; // the line bends: an arc from now on
		}
		drag_changed_ = true;
		refresh();
	}
}

void Editor::pointer(int kind, float x, float y, unsigned buttons, unsigned mods) {
	mouse_ = V2{x, y};
	if (mode_ == Mode::Play) {
		return;
	}
	const bool in_panel = x >= width_ - PANEL_WIDTH;
	if (kind == PtrMove) {
		if (panning_) {
			center_ = V2::of(double(center_.x) - (double(x) - pan_last_.x) / zoom_, double(center_.y) - (double(y) - pan_last_.y) / zoom_);
			pan_last_ = mouse_;
			view_touched_ = true;
		} else if (drag_) {
			drag_to(mouse_, mods);
		}
		return;
	}
	if (kind == PtrUp) {
		if (panning_ && (buttons & (BtnMiddle | BtnLeft))) {
			panning_ = false;
		}
		if (drag_ && (buttons & BtnLeft)) {
			if (drag_changed_) {
				redo_.clear();
				dirty_ = true;
			} else {
				undo_.pop_back(); // a click, not a drag
			}
			drag_.reset();
		}
		return;
	}
	// a press
	if ((buttons & BtnMiddle) || ((buttons & BtnLeft) && space_)) {
		panning_ = true;
		pan_last_ = mouse_;
		return;
	}
	if (in_panel) {
		if (buttons & BtnLeft) {
			for (const Row &r : rows()) {
				if (y >= r.y && y < r.y + ROW) {
					click_row(r, mods);
					return;
				}
			}
		}
		return;
	}
	if (focus_) {
		commit_field();
	}
	if (buttons & BtnRight) {
		if (!clicks_.empty()) {
			clicks_.clear();
		} else {
			selected_.clear();
			item_.clear();
		}
		return;
	}
	if (!(buttons & BtnLeft)) {
		return;
	}
	switch (tool_) {
		case Tool::Select: {
			std::optional<Handle> h = handle_at(mouse_);
			if (!h) {
				const int i = element_at(mouse_);
				if (i >= 0) {
					select_element(doc_["elements"][size_t(i)].value("name", ""));
					h = Handle{selected_, "body", to_world(mouse_), ""};
				} else {
					// Stage 148: an item (the goal, a detector, an object) under the pointer
					const std::string it = item_at(mouse_);
					selected_.clear();
					item_ = it;
					if (it.empty()) {
						return;
					}
					h = Handle{*item_path(it), "body", to_world(mouse_), it};
				}
			} else if (!h->item.empty()) {
				selected_.clear();
				item_ = h->item;
			} else if (!h->element.empty()) {
				select_element(h->element);
			}
			undo_.push_back(doc_);
			drag_ = h;
			drag_changed_ = false;
			drag_last_ = to_world(mouse_);
			return;
		}
		case Tool::Player: {
			const json p = snap(mouse_, mods, nullptr);
			command({{"op", "set"}, {"path", "/player/start"}, {"value", p}});
			return;
		}
		case Tool::Object: { // one click: a pullable object there
			json os = doc_.value("objects", json::array());
			if (!os.is_array()) {
				os = json::array();
			}
			std::string name;
			for (int n = 1;; n++) {
				name = "object_" + std::to_string(n);
				bool used = false;
				for (const json &o : os) {
					used = used || (o.is_object() && o.value("name", "") == name);
				}
				if (!used) {
					break;
				}
			}
			os.push_back({{"name", name}, {"circle", {{"center", snap(mouse_, mods, nullptr)}, {"radius", 16.0}}}});
			const json r = command({{"op", "set"}, {"path", "/objects"}, {"value", os}});
			hint_ = r.value("ok", false) ? name + " placed (a pull brings it back)" : r.value("error", std::string());
			return;
		}
		default: {
			const json p = snap(mouse_, mods, nullptr, &carrier_of_click_);
			place(p, mods);
		}
	}
}

void Editor::key(int code, unsigned mods, bool down) {
	if (code == 32 && !down) {
		space_ = false;
		return;
	}
	if (!down) {
		return;
	}
	if (mode_ == Mode::Play) {
		if (code == KeyEscape || code == KeyF1 + 4) {
			command({{"op", "stop"}});
		}
		return;
	}
	if (focus_) {
		if (code == KeyEnter) {
			commit_field();
		} else if (code == KeyEscape) {
			focus_.reset();
		} else if (code == KeyBackspace && !buffer_.empty()) {
			size_t n = buffer_.size() - 1;
			while (n > 0 && (static_cast<unsigned char>(buffer_[n]) & 0xC0) == 0x80) {
				n--; // a whole UTF-8 character
			}
			buffer_.erase(n);
		}
		return;
	}
	auto run = [&](const json &c) {
		const json r = command(c);
		hint_ = r.value("ok", false) ? "" : r.value("error", std::string());
	};
	if (mods & ModCtrl) {
		if (code == 'Z') {
			run({{"op", (mods & ModShift) ? "redo" : "undo"}});
		} else if (code == 'Y') {
			run({{"op", "redo"}});
		} else if (code == 'S') {
			run({{"op", "save"}});
		} else if (code == 'O') {
			run({{"op", "level_menu"}}); // Stage 155
		} else if (code == KeyLeft || code == KeyRight) {
			run({{"op", "next_level"}, {"by", code == KeyLeft ? -1 : 1}});
		} else if (code == 'D' && !selected_.empty()) {
			run({{"op", "duplicate"}, {"name", selected_}});
		} else if (code == 'D' && !item_.empty()) {
			run({{"op", "duplicate"}, {"item", item_}});
		}
		return;
	}
	const std::vector<std::string> pal = palette();
	const auto at = std::find(pal.begin(), pal.end(), kind_);
	switch (code) {
		case 32: space_ = true; return;
		case 'V': tool_ = Tool::Select; clicks_.clear(); return;
		case 'L': tool_ = Tool::Line; clicks_.clear(); return;
		case 'A': tool_ = Tool::Arc; clicks_.clear(); return;
		case 'C': tool_ = Tool::Circle; clicks_.clear(); return;
		case 'P': tool_ = Tool::Player; clicks_.clear(); return;
		case 'O': tool_ = Tool::Goal; clicks_.clear(); return;
		case 'D': tool_ = Tool::Detector; clicks_.clear(); return;
		case 'B': tool_ = Tool::Object; clicks_.clear(); return;
		case 'K': tool_ = (mods & ModShift) ? Tool::CarrierCircle : Tool::CarrierLine; clicks_.clear(); click_carriers_.clear(); return;
		case 'G': grid_ = !grid_; return;
		case 'F': frame_all(); view_touched_ = false; return;
		case KeyDelete:
		case KeyBackspace:
			if (!selected_.empty()) {
				run({{"op", "remove"}, {"name", selected_}});
			} else if (!item_.empty()) {
				run({{"op", "remove"}, {"item", item_}});
			}
			return;
		case KeyEscape:
			if (level_menu_) {
				level_menu_ = false; // Stage 155
			} else if (kind_pick_) {
				kind_pick_ = false;
				hint_.clear();
			} else if (!clicks_.empty()) {
				clicks_.clear();
			} else {
				selected_.clear();
				item_.clear();
			}
			return;
		case KeyTab:
			if (!pal.empty()) {
				const long n = long(pal.size());
				const long k = at == pal.end() ? 0 : long(at - pal.begin());
				kind_ = pal[size_t(((k + ((mods & ModShift) ? -1 : 1)) % n + n) % n)];
			}
			return;
		case KeyLeft:
		case KeyRight:
		case KeyUp:
		case KeyDown:
			if (!selected_.empty() || !item_.empty()) {
				const double step = (grid_ ? 10.0 : 1.0) * ((mods & ModShift) ? 10.0 : 1.0);
				const double dx = code == KeyLeft ? -step : code == KeyRight ? step : 0.0;
				const double dy = code == KeyUp ? -step : code == KeyDown ? step : 0.0;
				if (!selected_.empty()) {
					run({{"op", "move"}, {"name", selected_}, {"handle", "body"}, {"by", jpoint(dx, dy)}});
				} else {
					run({{"op", "move"}, {"item", item_}, {"handle", "body"}, {"by", jpoint(dx, dy)}});
				}
			}
			return;
		default:
			break;
	}
	if (code >= '1' && code <= '9' && size_t(code - '1') < pal.size()) {
		kind_ = pal[size_t(code - '1')];
		return;
	}
	if (code == KeyF1 + 4) {
		run({{"op", "play"}});
	}
}

void Editor::text(const std::string &utf8) {
	if (!focus_ || mode_ == Mode::Play) {
		return;
	}
	for (const char c : utf8) {
		if (static_cast<unsigned char>(c) >= 32 && c != 127) {
			buffer_ += c;
		}
	}
}

void Editor::commit_field() {
	const Field f = *focus_;
	focus_.reset();
	json value;
	if (!buffer_.empty()) {
		try {
			value = json::parse(buffer_);
		} catch (const json::exception &) {
			value = buffer_; // a bare word: a string
		}
	}
	json r;
	const bool item_name = f.element.empty() && !item_.empty() && f.key.size() > 5 && f.key.compare(f.key.size() - 5, 5, "/name") == 0;
	if (item_name) {
		r = command({{"op", "rename"}, {"item", item_}, {"to", value.is_string() ? value : json(buffer_)}});
	} else if (f.element.empty()) {
		r = command({{"op", "set"}, {"path", f.key}, {"value", value}});
	} else if (f.key == "name") {
		r = command({{"op", "rename"}, {"name", f.element}, {"to", value.is_string() ? value : json(buffer_)}});
	} else {
		r = command({{"op", "set"}, {"name", f.element}, {"key", f.key}, {"value", value}});
	}
	hint_ = r.value("ok", false) ? "" : r.value("error", std::string());
}

// ---------------------------------------------------------------- the panel

std::vector<Editor::Row> Editor::rows() const {
	std::vector<Row> out;
	float y = 10.0f - panel_scroll_;
	auto add = [&](Row r) {
		r.y = y;
		y += ROW;
		out.push_back(std::move(r));
	};
	auto text_rows = [&](const std::string &s, Row::Kind kind, int element) {
		for (const std::string &l : wrap(s, WRAP)) {
			Row r;
			r.kind = kind;
			r.label = l;
			r.error_element = element;
			add(r);
		}
	};
	add({Row::Header, "Ricochet level editor", "", "", std::nullopt, "", -1, 0, {}});
	add({Row::Buttons, "", "", "undo (ctrl+z), redo (ctrl+shift+z), save (ctrl+s), play (F5)", std::nullopt, "", -1, 0, {}});
	// Stage 155: the level menu (the previous level, the list, the next level)
	add({Row::Nav, level_name_.empty() ? std::string("(untitled)") : level_name_, "",
			"the levels: ctrl+O the list, ctrl+left / ctrl+right the previous / next level", std::nullopt, "", -1, 0, {}});
	if (level_menu_) {
		if (levels_.empty()) {
			text_rows("no levels listed by the host", Row::Text, -1);
		}
		for (const std::string &n : levels_) {
			Row r;
			r.kind = Row::Level;
			r.label = n;
			r.hint = confirm_open_ == n ? "unsaved changes: click again to discard them" : "open this level";
			add(r);
		}
		return out;
	}
	add({Row::Value, "tool", TOOL_NAMES[int(tool_)], "V select, L line, A arc, C circle, P the player's start, O the goal, D a detector, B an object, K a line carrier, shift+K a circle carrier", std::nullopt,
			"tool", -1, 0, {}});
	// Stage 161: the tool palette -- every tool, three to a row (a click: use it)
	for (size_t k = 0; k < std::size(TOOL_NAMES); k += 3) {
		Row r;
		r.kind = Row::Tools;
		r.hint = "a tool: click to use it (or its key)";
		for (size_t q = k; q < std::min(k + 3, std::size(TOOL_NAMES)); q++) {
			r.options.push_back(TOOL_NAMES[q]);
		}
		add(r);
	}
	const json ks = kinds();
	add({Row::Value, "place", kind_, "Tab / shift+Tab / 1-9, or a kind below: what the line, arc and circle tools place", std::nullopt, "place", -1, 0, {}});
	{
		// the palette: every kind, three to a row (a click: place it)
		const std::vector<std::string> pal = palette();
		for (size_t k = 0; k < pal.size(); k += 3) {
			Row r;
			r.kind = Row::Chips;
			r.hint = kind_pick_ ? "a kind: the selected element becomes one" : "a kind: the line, arc and circle tools place it";
			r.options.assign(pal.begin() + long(k), pal.begin() + long(std::min(k + 3, pal.size())));
			add(r);
		}
	}
	if (ks.contains(kind_)) {
		text_rows(ks[kind_].value("doc", ""), Row::Text, -1);
	}
	y += 8;
	const int i = index_of(selected_);
	if (i >= 0) {
		const json &e = doc_["elements"][size_t(i)];
		const std::string kind = e.value("kind", "");
		add({Row::Header, "element", "", "", std::nullopt, "", -1, 0, {}});
		add({Row::Value, "name", selected_, "unique; flags derive from it (door:<name>)", Field{selected_, "name"}, "", -1, 0, {}});
		add({Row::Value, "kind", kind_pick_ ? kind + "  (pick one in the palette)" : kind, "click, then a kind in the palette (shift+click: the next kind)",
				Field{selected_, "kind"}, "kind", -1, 0, {}});
		if (kind == "raw") {
			add({Row::Value, "surface", spell(e.value("surface", json())), "an rc-level/1 surface (JSON)", Field{selected_, "surface"}, "", -1, 0, {}});
		} else if (kind != "edges") {
			add({Row::Value, "label", e.contains("label") ? spell(e["label"]) : "(the kind's)", "the node prefix; empty: the kind's",
					Field{selected_, "label"}, "", -1, 0, {}});
			add({Row::Value, "flip", e.value("flip", false) ? "true" : "false", "left and right swapped", Field{selected_, "flip"}, "bool", -1, 0, {}});
			for (const char *g : {"start", "end", "via"}) {
				if (e.contains(g)) {
					add({Row::Value, g, spell(e[g]), "[x, y]", Field{selected_, g}, "", -1, 0, {}});
				}
			}
			if (e.contains("circle")) {
				add({Row::Value, "circle", spell(e["circle"]), "{\"center\": [x, y], \"radius\": r}", Field{selected_, "circle"}, "", -1, 0, {}});
			}
			add({Row::Value, "motion", e.contains("motion") ? spell(e["motion"]) : "(none)",
					"{\"keys\": [[phase, dx, dy, angle], ...], \"pivot\": [x, y]?}: it moves over the level's cycle (its keys drawn faint)",
					Field{selected_, "motion"}, "", -1, 0, {}});
			add({Row::Value, "tags", e.contains("tags") ? spell(e["tags"]) : "(the kind's)",
					"[tag, ...]: added to the kind's tags; the level's spread rules flow through them (flammable, conductive, powered)",
					Field{selected_, "tags"}, "", -1, 0, {}});
			const json params = ks.contains(kind) ? ks[kind].value("params", json::object()) : json::object();
			for (auto it = params.begin(); it != params.end(); ++it) {
				const json &spec = it.value();
				const bool set = e.contains(it.key());
				std::string value = set ? spell(e[it.key()]) : spec.contains("default") ? "(" + spell(spec["default"]) + ")" : "(required)";
				std::string choice;
				if (spec.contains("enum")) {
					choice = "enum";
				} else if (spec.value("default", json()).is_boolean() || spec.value("type", "") == "bool") {
					choice = "bool";
				}
				add({Row::Value, it.key(), value, spec.value("doc", ""), Field{selected_, it.key()}, choice, -1, 0, {}});
			}
		}
	} else if (!item_.empty()) {
		for (Row r : item_rows()) {
			add(std::move(r));
		}
	} else {
		add({Row::Header, "level", "", "", std::nullopt, "", -1, 0, {}});
		auto field = [&](const char *label, const std::string &path, const std::string &hint, const std::string &choice = "") {
			const json::json_pointer ptr(path);
			const json v = doc_.contains(ptr) ? doc_[ptr] : json();
			add({Row::Value, label, v.is_null() ? std::string("(none)") : spell(v), hint, Field{"", path}, choice, -1, 0, {}});
		};
		field("name", "/name", "the level's path under data/levels");
		field("bounds", "/bounds", "[x, y, w, h]: the arrow wraps there");
		field("gravity", "/gravity", "[gx, gy] px/s^2 (the player's)");
		field("room", "/room/rect", "[x, y, w, h]: the room's walls");
		field("room built", "/room/built", "the room's walls are built", "bool");
		field("player", "/player/start", "[x, y]: the player's start (P)");
		field("flags", "/flags", "{key: value}: the flags the level starts with");
		field("goal", "/objective/goal", "{\"rect\": [x, y, w, h]} or {\"circle\": ...}: reach it to win (O)");
		field("requires", "/objective/requires", "[{\"key\", \"values\"}]: the goal opens when they hold");
		field("detectors", "/detectors", "[{name, region | segment, mode, flag?}]: they write flags (D)");
		field("objects", "/objects", "[{name, circle, kind, face, solid}]: bodies a pull brings back (B)");
		field("cycle", "/cycle", "{\"period\": s, \"resume_after\": s}: the moving elements' cycle");
		field("spread", "/spread", "[{flag, tag, persist?, sources?}]: flags flowing through tagged things that touch (default: fire and charge)");
	}
	y += 8;
	add({Row::Header, errors_.empty() ? "no problems" : std::to_string(errors_.size()) + " problem(s)", "", "", std::nullopt, "", -1, 0, {}});
	for (const Error &er : errors_) {
		text_rows(er.message, Row::Error, er.element);
	}
	return out;
}

void Editor::click_row(const Row &r, unsigned mods) {
	if (kind_pick_ && r.kind != Row::Chips && r.choice != "kind") {
		kind_pick_ = false; // (any other click ends the pick)
	}
	auto run = [&](const json &c) {
		const json res = command(c);
		hint_ = res.value("ok", false) ? "" : res.value("error", std::string());
	};
	if (focus_) {
		commit_field();
	}
	if (r.kind == Row::Buttons) {
		const float x0 = width_ - PANEL_WIDTH + 10;
		const int b = std::clamp(int((mouse_.x - x0) / ((PANEL_WIDTH - 20) / 4)), 0, 3);
		static const char *const OPS[] = {"undo", "redo", "save", "play"};
		run({{"op", OPS[b]}});
		return;
	}
	if (r.kind == Row::Tools) { // Stage 161
		const float x0 = width_ - PANEL_WIDTH + 10;
		const size_t c = size_t(std::clamp(int((mouse_.x - x0) / ((PANEL_WIDTH - 20) / 3)), 0, 2));
		if (c < r.options.size()) {
			run({{"op", "tool"}, {"tool", r.options[c]}});
			click_carriers_.clear();
		}
		return;
	}
	if (r.kind == Row::Nav) {
		const float x0 = width_ - PANEL_WIDTH + 10, w = PANEL_WIDTH - 20;
		const float at = mouse_.x - x0;
		if (at < w * 0.18f) {
			run({{"op", "next_level"}, {"by", -1}});
		} else if (at > w * 0.82f) {
			run({{"op", "next_level"}, {"by", 1}});
		} else {
			run({{"op", "level_menu"}});
		}
		return;
	}
	if (r.kind == Row::Level) {
		run({{"op", "open"}, {"name", r.label}});
		return;
	}
	if (r.kind == Row::Chips) {
		const float x0 = width_ - PANEL_WIDTH + 10;
		const size_t c = size_t(std::clamp(int((mouse_.x - x0) / ((PANEL_WIDTH - 20) / 3)), 0, 2));
		if (c < r.options.size() && kind_pick_ && !selected_.empty()) {
			kind_pick_ = false;
			hint_.clear();
			run({{"op", "set"}, {"name", selected_}, {"key", "kind"}, {"value", r.options[c]}});
			return;
		}
		if (c < r.options.size()) {
			kind_ = r.options[c];
			if (tool_ == Tool::Select || tool_ == Tool::Player || tool_ == Tool::Goal || tool_ == Tool::Detector || tool_ == Tool::Object) {
				const json ks = kinds();
				tool_ = ks.contains(kind_) && ks[kind_].value("geometry", "any") == "circle" ? Tool::Circle : Tool::Line; // ready to place
				clicks_.clear();
			}
		}
		return;
	}
	if (r.kind == Row::Error && r.error_element >= 0 && size_t(r.error_element) < doc_["elements"].size()) {
		select_element(doc_["elements"][size_t(r.error_element)].value("name", ""));
		return;
	}
	const bool back = (mods & ModShift) != 0;
	auto cycle = [&](const std::vector<std::string> &opts, const std::string &cur) {
		const long n = long(opts.size());
		const long k = long(std::find(opts.begin(), opts.end(), cur) - opts.begin());
		return opts[size_t(((k + (back ? -1 : 1)) % n + n) % n)];
	};
	if (r.choice == "tool") {
		const std::string t = cycle({"select", "line", "arc", "circle", "player", "goal", "detector", "object", "carrier_line", "carrier_circle"}, r.value);
		run({{"op", "tool"}, {"tool", t}});
		return;
	}
	if (r.choice == "place") {
		kind_ = cycle(palette(), kind_);
		return;
	}
	if (!r.field) {
		return;
	}
	const Field &f = *r.field;
	if (r.choice == "kind") { // the palette picks it (Shift: the next kind)
		if (back) {
			run({{"op", "set"}, {"name", f.element}, {"key", "kind"}, {"value", cycle(palette(), r.value)}});
		} else {
			kind_pick_ = !kind_pick_;
			hint_ = kind_pick_ ? "pick the element's kind in the palette (Esc: no change)" : "";
		}
		return;
	}
	if (r.choice == "bool") {
		const bool now = r.value == "true" || r.value == "(true)";
		if (f.element.empty()) {
			run({{"op", "set"}, {"path", f.key}, {"value", !now}});
		} else {
			run({{"op", "set"}, {"name", f.element}, {"key", f.key}, {"value", !now}});
		}
		return;
	}
	if (r.choice == "options" && !r.options.empty()) {
		run({{"op", "set"}, {"path", f.key}, {"value", cycle(r.options, r.value)}});
		return;
	}
	if (r.choice == "shape") { // a region: a rect <-> the circle in it
		const json::json_pointer ptr(f.key);
		if (!doc_.contains(ptr) || !doc_[ptr].is_object()) {
			return;
		}
		const json reg = doc_[ptr];
		json next;
		if (reg.contains("rect") && reg["rect"].is_array() && reg["rect"].size() == 4) {
			const double x = reg["rect"][0].get<double>(), y = reg["rect"][1].get<double>(), w = reg["rect"][2].get<double>(), h = reg["rect"][3].get<double>();
			next = {{"circle", {{"center", jpoint(x + w / 2, y + h / 2)}, {"radius", std::max(1.0, std::min(w, h) / 2)}}}};
		} else if (reg.contains("circle") && reg["circle"].is_object() && is_point(reg["circle"].value("center", json()))) {
			const V2 c = pt(reg["circle"]["center"]);
			const double r2 = reg["circle"].value("radius", 10.0);
			next = {{"rect", json::array({double(c.x) - r2, double(c.y) - r2, 2 * r2, 2 * r2})}};
		} else {
			return;
		}
		run({{"op", "set"}, {"path", f.key}, {"value", next}});
		return;
	}
	if (r.choice == "enum") {
		const json ks = kinds();
		const json &e = doc_["elements"][size_t(index_of(f.element))];
		const json &spec = ks[e.value("kind", "")]["params"][f.key];
		std::vector<std::string> opts;
		for (const json &v : spec["enum"]) {
			opts.push_back(spell(v));
		}
		const std::string cur = e.contains(f.key) ? spell(e[f.key]) : spell(spec.value("default", json()));
		const std::string next = cycle(opts, cur);
		for (const json &v : spec["enum"]) {
			if (spell(v) == next) {
				run({{"op", "set"}, {"name", f.element}, {"key", f.key}, {"value", v}});
			}
		}
		return;
	}
	focus_ = f;
	buffer_ = r.value.size() >= 2 && r.value.front() == '(' && r.value.back() == ')' ? "" : r.value;
}

// ---------------------------------------------------------------- drawing

namespace {

struct Out {
	std::vector<render::FrameNode> &nodes;
	void add(int layer, int b, int z, std::vector<Call> calls) {
		render::FrameNode n;
		n.key.kind = render::NodeKind::Editor;
		n.key.a = layer;
		n.key.b = b;
		n.z = z;
		n.version = render::content_hash(calls);
		n.calls = std::make_shared<const std::vector<Call>>(std::move(calls));
		nodes.push_back(std::move(n));
	}
};

} // namespace

std::vector<render::FrameNode> Editor::draw() const {
	std::vector<render::FrameNode> out;
	if (mode_ == Mode::Play) {
		return out; // the host shows the game
	}
	draw_canvas(out);
	draw_panel(out);
	std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.z < b.z; });
	return out;
}

void Editor::draw_canvas(std::vector<render::FrameNode> &nodes) const {
	Out out{nodes};
	const float cw = width_ - PANEL_WIDTH, ch = height_ - STATUS_HEIGHT;
	out.add(L_CANVAS, 0, 0, {rect(V2{0, 0}, V2{cw, ch}, CANVAS)});
	// the grid: every 10 units when it is 6 px or more, every 50 when it is 6 px or more
	{
		std::vector<Call> g;
		const V2 w0 = to_world(V2{0, 0}), w1 = to_world(V2{cw, ch});
		for (const double step : {10.0, 50.0}) {
			if (step * zoom_ < 6.0) {
				continue;
			}
			const Color c = step == 50.0 ? GRID_COARSE : GRID_FINE;
			for (double x = std::ceil(double(w0.x) / step) * step; x <= w1.x && g.size() < 600; x += step) {
				const double sx = to_screen(V2::of(x, 0)).x;
				g.push_back(line(V2::of(sx, 0), V2::of(sx, ch), c, -1));
			}
			for (double y = std::ceil(double(w0.y) / step) * step; y <= w1.y && g.size() < 1200; y += step) {
				const double sy = to_screen(V2::of(0, y)).y;
				g.push_back(line(V2::of(0, sy), V2::of(cw, sy), c, -1));
			}
		}
		out.add(L_GRID, 0, 1, std::move(g));
	}
	// the bounds (the edge walls) and the room's walls
	{
		std::vector<Call> r;
		auto box = [&](const json &b, const Color &c, bool dash) {
			if (!b.is_array() || b.size() != 4) {
				return;
			}
			const double x = b[0].get<double>(), y = b[1].get<double>(), w = b[2].get<double>(), h = b[3].get<double>();
			const V2 p[4] = {to_screen(V2::of(x, y)), to_screen(V2::of(x + w, y)), to_screen(V2::of(x + w, y + h)), to_screen(V2::of(x, y + h))};
			for (int k = 0; k < 4; k++) {
				r.push_back(dash ? dashed(p[k], p[(k + 1) % 4], c, 1.0, 8.0) : line(p[k], p[(k + 1) % 4], c, 2.0));
			}
		};
		box(doc_.value("bounds", json::array({0.0, 0.0, 1920.0, 1080.0})), BOUNDS, true);
		const json room = doc_.value("room", json::object());
		if (!room.is_object() || room.value("built", true)) {
			box(room.is_object() ? room.value("rect", json::array({560.0, 240.0, 800.0, 600.0})) : json(), RED, false);
		}
		out.add(L_ROOM, 0, 2, std::move(r));
	}
	// Stage 149: the carriers, faint across the canvas
	{
		std::vector<Call> cg;
		if (doc_.contains("carriers") && doc_["carriers"].is_array()) {
			for (const json &c : doc_["carriers"]) {
				if (!c.is_object()) {
					continue;
				}
				const bool picked = item_ == "carrier:" + c.value("name", "");
				const Color col = picked ? rgba(0.6, 0.85, 1.0, 0.9) : CARRIER;
				V2 label_at;
				if (c.contains("line") && c["line"].is_object() && is_point(c["line"].value("a", json())) && is_point(c["line"].value("b", json()))) {
					const V2 a = pt(c["line"]["a"]), b = pt(c["line"]["b"]);
					const double dx = double(b.x) - a.x, dy = double(b.y) - a.y, len = std::hypot(dx, dy);
					if (len <= 0) {
						continue;
					}
					const double far = 4.0 * (cw + ch) / zoom_; // past the canvas both ways
					const V2 p0 = to_screen(V2::of(a.x - dx / len * far, a.y - dy / len * far)), p1 = to_screen(V2::of(a.x + dx / len * far, a.y + dy / len * far));
					cg.push_back(dashed(p0, p1, col, picked ? 2.0 : 1.0, 12.0));
					label_at = to_screen(a);
				} else if (c.contains("circle") && c["circle"].is_object() && is_point(c["circle"].value("center", json()))
						&& c["circle"].value("radius", json()).is_number()) {
					const V2 ctr = to_screen(pt(c["circle"]["center"]));
					const double r = c["circle"]["radius"].get<double>() * zoom_;
					for (int k = 0; k < 48; k += 2) {
						cg.push_back(arc(ctr, r, TAU * k / 48, TAU * (k + 1) / 48, col, picked ? 2.0 : 1.0));
					}
					label_at = V2::of(ctr.x, double(ctr.y) - r - 6);
				} else {
					continue;
				}
				cg.push_back(text_at(V2::of(double(label_at.x) + 6, double(label_at.y) - 6), c.value("name", ""), col, 11));
			}
		}
		out.add(L_CARRIERS, 0, 2, std::move(cg));
	}
	// the goal and the detectors
	{
		std::vector<Call> g;
		auto region = [&](const json &reg, const Color &c, const std::string &label, bool picked = false) {
			if (!reg.is_object()) {
				return;
			}
			V2 at;
			if (reg.contains("circle") && reg["circle"].is_object() && is_point(reg["circle"].value("center", json()))
					&& reg["circle"].value("radius", json()).is_number()) {
				at = to_screen(pt(reg["circle"]["center"]));
				const double r = reg["circle"]["radius"].get<double>() * zoom_;
				if (picked) {
					g.push_back(arc(at, r, 0, TAU, SELECT, 9.0));
				}
				for (int k = 0; k < 24; k += 2) {
					g.push_back(arc(at, r, TAU * k / 24, TAU * (k + 1) / 24, c, 2.0));
				}
				at = V2::of(at.x, double(at.y) - r - 6); // (the label above: the center has a handle)
			} else if (reg.contains("rect") && reg["rect"].is_array() && reg["rect"].size() == 4) {
				const json &b = reg["rect"];
				const double x = b[0].get<double>(), y = b[1].get<double>(), w = b[2].get<double>(), h = b[3].get<double>();
				const V2 p[4] = {to_screen(V2::of(x, y)), to_screen(V2::of(x + w, y)), to_screen(V2::of(x + w, y + h)), to_screen(V2::of(x, y + h))};
				for (int k = 0; k < 4; k++) {
					if (picked) {
						g.push_back(line(p[k], p[(k + 1) % 4], SELECT, 9.0));
					}
					g.push_back(dashed(p[k], p[(k + 1) % 4], c, 2.0, 8.0));
				}
				at = to_screen(V2::of(x + w / 2, y + h / 2));
			} else {
				return;
			}
			g.push_back(text_at(at, label, c, 13));
			g.back().alignment = 1;
		};
		if (doc_.contains("objective") && doc_["objective"].is_object()) {
			region(doc_["objective"].value("goal", json()), GOAL, "goal", item_ == "goal");
		}
		if (doc_.contains("detectors") && doc_["detectors"].is_array()) {
			for (const json &d : doc_["detectors"]) {
				if (!d.is_object()) {
					continue;
				}
				const std::string name = d.value("name", "");
				const bool picked = item_ == "detector:" + name;
				if (d.contains("segment") && d["segment"].is_object() && is_point(d["segment"].value("a", json())) && is_point(d["segment"].value("b", json()))) {
					const V2 a = to_screen(pt(d["segment"]["a"])), b = to_screen(pt(d["segment"]["b"]));
					if (picked) {
						g.push_back(line(a, b, SELECT, 9.0));
					}
					g.push_back(dashed(a, b, DETECTOR, 2.0, 8.0));
					g.push_back(text_at(V2::of((double(a.x) + b.x) / 2 + 6, (double(a.y) + b.y) / 2 - 6), name, DETECTOR, 12));
				} else {
					region(d.value("region", json()), DETECTOR, name, picked);
				}
			}
		}
		if (doc_.contains("objects") && doc_["objects"].is_array()) {
			for (const json &o : doc_["objects"]) {
				if (!o.is_object() || !o.contains("circle") || !o["circle"].is_object() || !is_point(o["circle"].value("center", json()))) {
					continue;
				}
				const V2 c = to_screen(pt(o["circle"]["center"]));
				const double r = o["circle"].value("radius", 16.0) * zoom_;
				const bool pullable = o.value("kind", "pullable") == "pullable";
				if (item_ == "object:" + o.value("name", "")) {
					g.push_back(arc(c, r, 0, TAU, SELECT, 9.0));
				}
				g.push_back(arc(c, r, 0, TAU, pullable ? OBJECT : GRAY, 2.5));
				g.push_back(text_at(V2::of(c.x, double(c.y) - r - 4), o.value("name", ""), pullable ? OBJECT : GRAY, 12));
				g.back().alignment = 1;
			}
		}
		out.add(L_GOALS, 0, 2, std::move(g));
	}
	// Stage 153: the shadow under the starting flags, faint
	if (!shade_fill_.indices.empty()) {
		Call k;
		k.kind = Kind::Triangles;
		for (const rc::vis::Pt &p : shade_fill_.points) {
			k.pts.push_back(to_screen(V2::of(p.x, p.y)));
		}
		for (uint32_t i : shade_fill_.indices) {
			k.indices.push_back(int(i));
		}
		k.colors = {rgba(0.0, 0.0, 0.05, 0.45)};
		out.add(L_SHADOW, 0, 1, {k});
	}
	// the elements
	const int sel = index_of(selected_);
	const bool hovering = tool_ == Tool::Select && !drag_ && mouse_.x < cw && mouse_.y < ch;
	const int hov = hovering ? element_at(mouse_) : -1;
	std::vector<bool> bad(drawn_.size(), false);
	for (const Error &e : errors_) {
		if (e.element >= 0 && size_t(e.element) < bad.size()) {
			bad[size_t(e.element)] = true;
		}
	}
	for (size_t i = 0; i < drawn_.size(); i++) {
		std::vector<Call> calls;
		auto stroke = [&](const rc::arc::SegGeom &seg, const Color &c, double w, bool dash) {
			if (seg.is_line()) {
				const V2 a = to_screen(seg.s), b = to_screen(seg.e);
				calls.push_back(dash ? dashed(a, b, c, w, 8.0) : line(a, b, c, w));
				return;
			}
			const rc::arc::GeneralizedCircle &g = seg.get_carrier();
			const V2 ctr = rc::arc::center_of(g);
			const double rad = g.radius();
			if (rad > rc::arc::MAX_ARC_RADIUS && !seg.full) {
				calls.push_back(line(to_screen(seg.s), to_screen(seg.e), c, w));
				return;
			}
			double a0 = 0.0, span = TAU;
			if (!seg.full) {
				a0 = std::atan2(double(seg.s.y) - ctr.y, double(seg.s.x) - ctr.x);
				const double am = wrap_angle(std::atan2(double(seg.v.y) - ctr.y, double(seg.v.x) - ctr.x) - a0);
				const double a1 = wrap_angle(std::atan2(double(seg.e.y) - ctr.y, double(seg.e.x) - ctr.x) - a0);
				span = am < a1 ? a1 : a1 - TAU;
			}
			const V2 sc = to_screen(ctr);
			const double sr = rad * zoom_;
			if (!dash) {
				calls.push_back(arc(sc, sr, a0, a0 + span, c, w));
				return;
			}
			const int n = std::clamp(int(std::abs(span) * sr / 8.0), 8, 256);
			for (int k = 0; k < n; k += 2) {
				calls.push_back(arc(sc, sr, a0 + span * k / n, a0 + span * (k + 1) / n, c, w));
			}
		};
		// a point on the segment, its tangent, and the unit normal toward its left side
		auto frame_of = [&](const rc::arc::SegGeom &seg, V2 &m, V2 &n) {
			V2 t;
			if (seg.is_line()) {
				m = V2::of((double(seg.s.x) + seg.e.x) / 2, (double(seg.s.y) + seg.e.y) / 2);
				t = seg.e - seg.s;
			} else {
				const V2 ctr = rc::arc::center_of(seg.get_carrier());
				m = seg.full ? V2::of(double(ctr.x) + seg.get_carrier().radius(), ctr.y) : seg.v;
				const V2 r = m - ctr;
				t = V2{-r.y, r.x};
			}
			const double len = std::hypot(double(t.x), double(t.y));
			n = len > 0 ? V2::of(-double(t.y) / len, double(t.x) / len) : V2{0, -1};
			if (seg.determine_side(V2::of(double(m.x) + n.x, double(m.y) + n.y)) != rc::Side::Left) {
				n = V2{-n.x, -n.y};
			}
		};
		const bool picked = int(i) == sel, hover = int(i) == hov;
		for (const BuiltSurface &b : drawn_[i].built) {
			if (picked || hover) {
				stroke(b.seg, picked ? SELECT : HOVER, 9.0, false);
			}
		}
		for (const BuiltSurface &b : drawn_[i].built) {
			if (i < shade_of_.size() && shade_of_[i] != 0) { // Stage 153: a shade, in its colour
				stroke(b.seg, shade_of_[i] == 1 ? rgba(0.55, 0.45, 0.95) : rgba(1.0, 0.85, 0.35), 2.0, true);
				continue;
			}
			if (b.pre_transform) {
				stroke(b.seg, PRE_TRANSFORM, 1.5, true);
				continue;
			}
			const Color left = side_color(shown_side(b, true)), right = side_color(shown_side(b, false));
			const bool gated = std::any_of(b.presence.begin(), b.presence.end(), [](const PresenceSpec &c) { return c.key.rfind("chan:", 0) != 0; });
			stroke(b.seg, left, 3.0, gated); // (dashed: present only under a flag; a spawner's channel is not drawn so)
			V2 m, n;
			frame_of(b.seg, m, n);
			const V2 sm = to_screen(m);
			calls.push_back(line(sm, V2::of(double(sm.x) + n.x * 9, double(sm.y) + n.y * 9), left, 2.0));
			if (right != left || shown_side(b, false) != nullptr) {
				calls.push_back(line(sm, V2::of(double(sm.x) - n.x * 9, double(sm.y) - n.y * 9), right, 2.0));
			}
		}
		for (const rc::arc::SegGeom &s : drawn_[i].broken) {
			stroke(s, picked ? SELECT : ERROR_TEXT, picked ? 9.0 : 2.0, !picked);
			if (picked) {
				stroke(s, ERROR_TEXT, 2.0, true);
			}
		}
		if (bad[i]) {
			for (const BuiltSurface &b : drawn_[i].built) {
				stroke(b.seg, ERROR_TEXT, 1.5, true);
			}
		}
		// Stage 151: a moving element, faint where its keys put it
		const json &me = doc_["elements"][i];
		if (me.is_object() && me.contains("motion") && me["motion"].is_object() && me["motion"].value("keys", json()).is_array() && !drawn_[i].built.empty()) {
			const rc::arc::SegGeom &f0 = drawn_[i].built.front().seg;
			V2 pivot = f0.full ? rc::arc::center_of(f0.get_carrier()) : V2::of((double(f0.s.x) + f0.e.x) / 2, (double(f0.s.y) + f0.e.y) / 2);
			if (is_point(me["motion"].value("pivot", json()))) {
				pivot = pt(me["motion"]["pivot"]);
			}
			for (const json &k : me["motion"]["keys"]) {
				if (!k.is_array() || k.size() != 4 || !k[1].is_number() || !k[2].is_number() || !k[3].is_number()) {
					continue;
				}
				const double dx = k[1].get<double>(), dy = k[2].get<double>(), a = k[3].get<double>();
				if (dx == 0 && dy == 0 && a == 0) {
					continue;
				}
				const double c = std::cos(a), sn = std::sin(a);
				auto T = [&](const V2 &p) {
					const double x = double(p.x) - pivot.x, y = double(p.y) - pivot.y;
					return V2::of(pivot.x + c * x - sn * y + dx, pivot.y + sn * x + c * y + dy);
				};
				for (const BuiltSurface &b : drawn_[i].built) {
					rc::arc::SegGeom g;
					if (b.seg.full) {
						const rc::arc::GeneralizedCircle &gc = b.seg.get_carrier();
						g = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(T(rc::arc::center_of(gc)), gc.radius()));
					} else if (b.seg.is_line()) {
						const V2 s0 = T(b.seg.s), e0 = T(b.seg.e);
						g = rc::arc::SegGeom::make(s0, e0, V2::of((double(s0.x) + e0.x) / 2, (double(s0.y) + e0.y) / 2));
						g.carrier = rc::arc::straight_carrier(s0, e0);
					} else {
						g = rc::arc::SegGeom::make(T(b.seg.s), T(b.seg.e), T(b.seg.v));
					}
					stroke(g, rgba(0.75, 0.75, 0.85, 0.35), 1.0, true);
				}
			}
		}
		// the name, by the element's first surface
		const json &e = doc_["elements"][i];
		const rc::arc::SegGeom *first = !drawn_[i].built.empty() ? &drawn_[i].built.front().seg
				: !drawn_[i].broken.empty()						  ? &drawn_[i].broken.front()
																  : nullptr;
		if (first != nullptr && zoom_ >= 0.3) {
			V2 m, n;
			frame_of(*first, m, n);
			const V2 sm = to_screen(m);
			calls.push_back(text_at(V2::of(double(sm.x) + n.x * 14 + 4, double(sm.y) + n.y * 14 + 4), e.value("name", ""), picked ? WHITE : DIM, 12));
		}
		out.add(L_ELEMENT, int(i), 3, std::move(calls));
	}
	// the player's start
	if (doc_.contains("player") && doc_["player"].is_object() && is_point(doc_["player"].value("start", json()))) {
		const V2 p = to_screen(pt(doc_["player"]["start"]));
		out.add(L_PLAYER, 0, 4, {arc(p, kinematics::RADIUS * zoom_, 0, TAU, GREEN, 2.0), circle(p, 2.5, GREEN)});
	}
	// a placement under way, and where the pointer snaps
	{
		std::vector<Call> pv;
		const bool placing = tool_ != Tool::Select;
		if (placing && mouse_.x < cw && mouse_.y < ch) {
			const V2 at = to_screen(pt(snap(mouse_, 0, nullptr)));
			pv.push_back(line(V2::of(double(at.x) - 6, at.y), V2::of(double(at.x) + 6, at.y), ACCENT, 1.0));
			pv.push_back(line(V2::of(at.x, double(at.y) - 6), V2::of(at.x, double(at.y) + 6), ACCENT, 1.0));
			std::vector<V2> pts;
			for (const json &c : clicks_) {
				pts.push_back(to_screen(pt(c)));
				pv.push_back(circle(pts.back(), 3.0, ACCENT));
			}
			if ((tool_ == Tool::Circle || tool_ == Tool::CarrierCircle) && pts.size() == 1) {
				pv.push_back(arc(pts[0], std::hypot(double(at.x) - pts[0].x, double(at.y) - pts[0].y), 0, TAU, ACCENT, 1.0));
			} else if (tool_ == Tool::Arc && pts.size() == 2) {
				const rc::arc::SegGeom seg = rc::arc::SegGeom::make(pts[0], pts[1], at);
				if (seg.is_line() || seg.get_carrier().radius() > rc::arc::MAX_ARC_RADIUS) {
					pv.push_back(line(pts[0], pts[1], ACCENT, 1.0));
				} else {
					const V2 c = rc::arc::center_of(seg.get_carrier());
					const double a0 = std::atan2(double(pts[0].y) - c.y, double(pts[0].x) - c.x);
					const double am = wrap_angle(std::atan2(double(at.y) - c.y, double(at.x) - c.x) - a0);
					const double a1 = wrap_angle(std::atan2(double(pts[1].y) - c.y, double(pts[1].x) - c.x) - a0);
					pv.push_back(arc(c, seg.get_carrier().radius(), a0, a0 + (am < a1 ? a1 : a1 - TAU), ACCENT, 1.0));
				}
			} else if ((tool_ == Tool::Goal || tool_ == Tool::Detector) && pts.size() == 1) {
				const V2 c[4] = {pts[0], V2{at.x, pts[0].y}, at, V2{pts[0].x, at.y}};
				for (int k = 0; k < 4; k++) {
					pv.push_back(dashed(c[k], c[(k + 1) % 4], tool_ == Tool::Goal ? GOAL : DETECTOR, 1.0, 6.0));
				}
			} else if (!pts.empty()) {
				pv.push_back(dashed(pts.back(), at, ACCENT, 1.0, 6.0));
			}
		}
		out.add(L_PREVIEW, 0, 5, std::move(pv));
	}
	// the handles
	{
		std::vector<Call> hs;
		const std::optional<Handle> over = drag_ ? drag_ : handle_at(mouse_);
		for (const Handle &h : handles()) {
			const V2 s = to_screen(h.at);
			const bool hot = over && over->element == h.element && over->kind == h.kind;
			const Color c = h.kind == "player" ? GREEN : h.kind == "mid" ? DIM : WHITE;
			hs.push_back(rect(V2::of(double(s.x) - HANDLE - 1, double(s.y) - HANDLE - 1), V2{2 * HANDLE + 2, 2 * HANDLE + 2}, CANVAS));
			hs.push_back(rect(V2::of(double(s.x) - HANDLE, double(s.y) - HANDLE), V2{2 * HANDLE, 2 * HANDLE}, hot ? ACCENT : c));
		}
		out.add(L_HANDLES, 0, 6, std::move(hs));
	}
}

void Editor::draw_panel(std::vector<render::FrameNode> &nodes) const {
	Out out{nodes};
	const float x0 = width_ - PANEL_WIDTH;
	std::vector<Call> p;
	p.push_back(rect(V2{x0, 0}, V2{PANEL_WIDTH, height_}, PANEL));
	p.push_back(line(V2{x0, 0}, V2{x0, height_}, rgba(1, 1, 1, 0.15), 1.0));
	const std::vector<Row> rs = rows();
	std::string hover_hint;
	for (const Row &r : rs) {
		if (r.y < -ROW || r.y > height_) {
			continue; // (scrolled out)
		}
		const float base = r.y + ROW - 6;
		const bool over = mouse_.x >= x0 && mouse_.y >= r.y && mouse_.y < r.y + ROW;
		if (over) {
			hover_hint = r.hint;
		}
		switch (r.kind) {
			case Row::Header:
				p.push_back(text_at(V2{x0 + 10, base}, r.label, ACCENT, FONT + 1));
				break;
			case Row::Text:
				p.push_back(text_at(V2{x0 + 14, base}, r.label, DIM, FONT - 2));
				break;
			case Row::Error:
				if (over) {
					p.push_back(rect(V2{x0 + 4, r.y + 1}, V2{PANEL_WIDTH - 8, ROW - 2}, rgba(1, 0.4, 0.3, 0.12)));
				}
				p.push_back(text_at(V2{x0 + 14, base}, r.label, ERROR_TEXT, FONT - 2));
				break;
			case Row::Chips: {
				const float cw = (PANEL_WIDTH - 20) / 3;
				for (size_t c = 0; c < r.options.size(); c++) {
					const float cx = x0 + 10 + float(c) * cw;
					const bool cur = r.options[c] == kind_;
					const bool hot = over && mouse_.x >= cx && mouse_.x < cx + cw;
					p.push_back(rect(V2{cx + 2, r.y + 2}, V2{cw - 4, ROW - 4}, cur ? rgba(1.0, 0.8, 0.3, 0.30) : hot ? rgba(1, 1, 1, 0.16) : rgba(1, 1, 1, 0.05)));
					std::string label = r.options[c];
					if (label.size() > 15) {
						label = label.substr(0, 14) + ".";
					}
					p.push_back(text_at(V2{cx + cw / 2, base}, label, cur ? ACCENT : TEXT, FONT - 3));
					p.back().alignment = 1;
				}
				break;
			}
			case Row::Tools: { // Stage 161: like the palette's chips, the current tool lit
				const float cw = (PANEL_WIDTH - 20) / 3;
				for (size_t c = 0; c < r.options.size(); c++) {
					const float cx = x0 + 10 + float(c) * cw;
					const bool cur = r.options[c] == TOOL_NAMES[int(tool_)];
					const bool hot = over && mouse_.x >= cx && mouse_.x < cx + cw;
					p.push_back(rect(V2{cx + 2, r.y + 2}, V2{cw - 4, ROW - 4}, cur ? rgba(1.0, 0.8, 0.3, 0.30) : hot ? rgba(1, 1, 1, 0.16) : rgba(1, 1, 1, 0.05)));
					std::string label = r.options[c];
					for (size_t t = 0; t < std::size(TOOL_NAMES); t++) {
						if (label == TOOL_NAMES[t]) {
							label = TOOL_LABELS[t];
							break;
						}
					}
					p.push_back(text_at(V2{cx + cw / 2, base}, label, cur ? ACCENT : TEXT, FONT - 3));
					p.back().alignment = 1;
				}
				break;
			}
			case Row::Nav: {
				const float w = PANEL_WIDTH - 20;
				const float cells[4] = {x0 + 10, x0 + 10 + w * 0.18f, x0 + 10 + w * 0.82f, x0 + 10 + w};
				for (int b = 0; b < 3; b++) {
					const bool hot = over && mouse_.x >= cells[b] && mouse_.x < cells[b + 1];
					const bool on = b == 1 && level_menu_;
					p.push_back(rect(V2{cells[b] + 2, r.y + 1}, V2{cells[b + 1] - cells[b] - 4, ROW - 2},
							on ? rgba(1.0, 0.8, 0.3, 0.30) : hot ? rgba(1, 1, 1, 0.18) : rgba(1, 1, 1, 0.07)));
					std::string label = b == 0 ? "<" : b == 2 ? ">" : r.label;
					if (label.size() > 30) {
						label = "..." + label.substr(label.size() - 27);
					}
					p.push_back(text_at(V2{(cells[b] + cells[b + 1]) / 2, base}, label, on ? ACCENT : TEXT, FONT - 1));
					p.back().alignment = 1;
				}
				break;
			}
			case Row::Level: {
				const bool cur = r.label == level_name_;
				if (cur || over) {
					p.push_back(rect(V2{x0 + 4, r.y + 1}, V2{PANEL_WIDTH - 8, ROW - 2}, cur ? rgba(1.0, 0.8, 0.3, 0.22) : rgba(1, 1, 1, 0.08)));
				}
				p.push_back(text_at(V2{x0 + 14, base}, r.label, confirm_open_ == r.label ? ERROR_TEXT : cur ? ACCENT : TEXT, FONT - 1));
				break;
			}
			case Row::Buttons: {
				static const char *const LABELS[] = {"undo", "redo", "save", "play"};
				const float bw = (PANEL_WIDTH - 20) / 4;
				for (int b = 0; b < 4; b++) {
					const float bx = x0 + 10 + float(b) * bw;
					const bool hot = over && mouse_.x >= bx && mouse_.x < bx + bw;
					p.push_back(rect(V2{bx + 2, r.y + 1}, V2{bw - 4, ROW - 2}, hot ? rgba(1, 1, 1, 0.18) : rgba(1, 1, 1, 0.07)));
					std::string label = LABELS[b];
					if (b == 2 && dirty_) {
						label += " *";
					}
					p.push_back(text_at(V2{bx + bw / 2, base}, label, TEXT, FONT - 1));
					p.back().alignment = 1;
				}
				break;
			}
			case Row::Value: {
				const bool focused = focus_ && r.field && focus_->element == r.field->element && focus_->key == r.field->key;
				if (focused) {
					p.push_back(rect(V2{x0 + VALUE_X - 4, r.y + 1}, V2{PANEL_WIDTH - VALUE_X - 6, ROW - 2}, rgba(1, 1, 1, 0.12)));
				} else if (over) {
					p.push_back(rect(V2{x0 + 4, r.y + 1}, V2{PANEL_WIDTH - 8, ROW - 2}, rgba(1, 1, 1, 0.06)));
				}
				p.push_back(text_at(V2{x0 + 14, base}, r.label, DIM, FONT - 1));
				std::string v = focused ? buffer_ + "|" : r.value;
				const size_t room = WRAP - 14;
				if (v.size() > room) {
					v = focused ? "..." + v.substr(v.size() - room + 3) : v.substr(0, room - 3) + "...";
				}
				const bool dflt = !focused && v.size() >= 2 && v.front() == '(';
				p.push_back(text_at(V2{x0 + VALUE_X, base}, v, dflt ? DIM : r.choice.empty() ? TEXT : ACCENT, FONT - 1));
				break;
			}
		}
	}
	out.add(L_PANEL, 0, 10, std::move(p));
	// the status bar
	std::vector<Call> s;
	s.push_back(rect(V2{0, height_ - STATUS_HEIGHT}, V2{x0, STATUS_HEIGHT}, PANEL));
	std::string line_text = std::string(TOOL_NAMES[int(tool_)]) + "  |  place: " + kind_ + "  |  grid " + (grid_ ? "10" : "off")
			+ "  |  " + (errors_.empty() ? std::string("ok") : std::to_string(errors_.size()) + " problem(s)") + (dirty_ ? "  |  unsaved" : "");
	const std::string extra = !hint_.empty() ? hint_ : hover_hint;
	if (!extra.empty()) {
		line_text += "  |  " + extra;
	}
	s.push_back(text_at(V2{10, height_ - 8}, line_text, errors_.empty() ? TEXT : ERROR_TEXT, FONT - 1));
	out.add(L_STATUS, 0, 11, std::move(s));
}

} // namespace rcg
