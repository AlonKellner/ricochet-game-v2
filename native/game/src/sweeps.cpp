#include "rcg/sweeps.hpp"

#include "rcg/build.hpp"
#include "rcg/digest.hpp"
#include "rcg/geom.hpp"
#include "rcg/model.hpp"
#include "rcg/rng.hpp"
#include "rcg/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

namespace rcg::sweeps {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- the combos (SweepPlans)

enum Pair { REFL_REFL, REFL_SEMI, REFL_PROJ, REFL_DIR, SEMI_SEMI, SEMI_PROJ, SEMI_DIR, PROJ_PROJ, PROJ_DIR, DIR_DIR, PORTAL };
const Pair NON_PORTAL[] = {REFL_REFL, REFL_SEMI, REFL_PROJ, REFL_DIR, SEMI_SEMI, SEMI_PROJ, SEMI_DIR, PROJ_PROJ, PROJ_DIR, DIR_DIR};

SideSpec side(const std::string &type, bool interactive, json params = json::object()) {
	SideSpec s;
	json raw = {{"type", type}};
	for (auto it = params.begin(); it != params.end(); ++it) {
		raw[it.key()] = it.value();
	}
	s.effect.raw = raw;
	s.effect.type = type;
	s.effect.back = params.value("back", false);
	if (params.contains("normal")) {
		s.effect.normal = std::array<double, 2>{params["normal"][0].get<double>(), params["normal"][1].get<double>()};
	}
	if (params.contains("theta")) {
		s.effect.theta = params["theta"].get<double>();
		s.effect.d = std::array<double, 2>{params["d"][0].get<double>(), params["d"][1].get<double>()};
	}
	s.interactive = interactive;
	return s;
}

// LevelSettings._line4 / SurfaceSpec.line: a line, its via the midpoint in float64
SurfaceSpec line(double sx, double sy, double ex, double ey, const SideSpec &l, const SideSpec &r) {
	SurfaceSpec s;
	s.start = V2::of(sx, sy);
	s.end = V2::of(ex, ey);
	s.via = V2::of((sx + ex) / 2.0, (sy + ey) / 2.0);
	s.left = l;
	s.right = r;
	return s;
}

SurfaceSpec arc(const std::array<double, 6> &a, const SideSpec &l, const SideSpec &r) {
	SurfaceSpec s;
	s.start = V2::of(a[0], a[1]);
	s.end = V2::of(a[2], a[3]);
	s.via = V2::of(a[4], a[5]);
	s.left = l;
	s.right = r;
	return s;
}

// The rows a combo adds, in _specs_before_edge's order of the legacy arrays.
std::vector<std::pair<std::string, SurfaceSpec>> combo_rows(Pair lines, Pair circles) {
	const std::array<double, 4> L1{600, 300, 600, 700}, L2{1300, 300, 1300, 700};
	const std::array<double, 6> A1{400, 250, 300, 250, 350, 200}, A2{1550, 750, 1450, 750, 1500, 700};
	std::vector<std::array<double, 4>> mirror, mirror_both, normal_proj;
	std::vector<std::pair<std::array<double, 4>, std::array<double, 2>>> dir_lines;
	std::vector<std::pair<std::array<double, 4>, std::array<double, 3>>> portal_lines;
	switch (lines) {
		case REFL_REFL: mirror_both = {L1, L2}; break;
		case REFL_SEMI: mirror_both = {L1}; mirror = {L2}; break;
		case REFL_PROJ: mirror_both = {L1}; normal_proj = {L2}; break;
		case REFL_DIR: mirror_both = {L1}; dir_lines = {{L2, {1, 0}}}; break;
		case SEMI_SEMI: mirror = {L1, L2}; break;
		case SEMI_PROJ: mirror = {L1}; normal_proj = {L2}; break;
		case SEMI_DIR: mirror = {L1}; dir_lines = {{L2, {1, 0}}}; break;
		case PROJ_PROJ: normal_proj = {L1, L2}; break;
		case PROJ_DIR: normal_proj = {L1}; dir_lines = {{L2, {1, 0}}}; break;
		case DIR_DIR: dir_lines = {{L1, {1, 0}}, {L2, {1, 0}}}; break;
		case PORTAL: portal_lines = {{L1, {0, 1000, 0}}}; break;
	}
	std::vector<std::array<double, 6>> semi_arcs, refl_arcs, normal_arcs;
	std::vector<std::pair<std::array<double, 6>, std::array<double, 2>>> dir_arcs;
	std::vector<std::pair<std::array<double, 6>, std::array<double, 3>>> portal_arcs;
	switch (circles) {
		case REFL_REFL: refl_arcs = {A1, A2}; break;
		case REFL_SEMI: refl_arcs = {A1}; semi_arcs = {A2}; break;
		case REFL_PROJ: refl_arcs = {A1}; normal_arcs = {A2}; break;
		case REFL_DIR: refl_arcs = {A1}; dir_arcs = {{A2, {1, 0}}}; break;
		case SEMI_SEMI: semi_arcs = {A1, A2}; break;
		case SEMI_PROJ: semi_arcs = {A1}; normal_arcs = {A2}; break;
		case SEMI_DIR: semi_arcs = {A1}; dir_arcs = {{A2, {1, 0}}}; break;
		case PROJ_PROJ: normal_arcs = {A1, A2}; break;
		case PROJ_DIR: normal_arcs = {A1}; dir_arcs = {{A2, {1, 0}}}; break;
		case DIR_DIR: dir_arcs = {{A1, {1, 0}}, {A2, {1, 0}}}; break;
		case PORTAL: portal_arcs = {{A1, {0, 500, 0}}}; break;
	}
	const SideSpec off = side("pass", false), refl = side("reflection", true);
	std::vector<std::pair<std::string, SurfaceSpec>> out;
	for (const auto &v : mirror) out.push_back({"Mirror", line(v[0], v[1], v[2], v[3], refl, off)});
	for (const auto &v : mirror_both) out.push_back({"MirrorB", line(v[0], v[1], v[2], v[3], refl, refl)});
	for (const auto &a : semi_arcs) out.push_back({"SemiReflArc", arc(a, refl, off)});
	for (const auto &a : refl_arcs) out.push_back({"ReflArc", arc(a, refl, refl)});
	const SideSpec lnp = side("line_normal_projection", true, {{"back", false}});
	for (const auto &v : normal_proj) out.push_back({"Projection", line(v[0], v[1], v[2], v[3], lnp, lnp)});
	const SideSpec cnp = side("circle_normal_projection", true, {{"back", false}});
	for (const auto &a : normal_arcs) out.push_back({"CircleProjection", arc(a, cnp, cnp)});
	for (const auto &[a, n] : dir_arcs) {
		const SideSpec cd = side("circle_directional_projection", true, {{"back", false}, {"normal", {n[0], n[1]}}});
		out.push_back({"CircleDir", arc(a, cd, cd)});
	}
	for (const auto &[v, n] : dir_lines) {
		const SideSpec ld = side("line_directional_projection", true, {{"back", false}, {"normal", {n[0], n[1]}}});
		out.push_back({"LineDir", line(v[0], v[1], v[2], v[3], ld, ld)});
	}
	for (const auto &[v, p] : portal_lines) {
		const SideSpec pl = side("portal", true, {{"theta", p[0]}, {"d", {p[1], p[2]}}});
		out.push_back({"Portal", line(v[0], v[1], v[2], v[3], pl, pl)});
	}
	for (const auto &[a, p] : portal_arcs) {
		const SideSpec pa = side("portal", true, {{"theta", p[0]}, {"d", {p[1], p[2]}}});
		out.push_back({"Portal", arc(a, pa, pa)});
	}
	return out;
}

// combo_base with a combo's rows before its edge walls (the ids renumbered in build order)
Level combo_level(const Level &base, Pair lines, Pair circles) {
	Level lv = base;
	lv.surfaces.clear();
	int next = 1;
	auto add = [&](const SurfaceSpec &spec, const std::optional<std::string> &label) {
		LevelSurface ls;
		ls.spec = spec;
		ls.label = label;
		ls.ids = {next++};
		if (spec.is_portal()) {
			ls.ids.push_back(next++);
		}
		lv.surfaces.push_back(ls);
	};
	bool rows_added = false;
	for (const LevelSurface &ls : base.surfaces) {
		if (!ls.label && !rows_added) {
			for (const auto &[label, spec] : combo_rows(lines, circles)) {
				add(spec, label);
			}
			rows_added = true;
		}
		add(ls.spec, ls.label);
	}
	return lv;
}

// ---------------------------------------------------------------- the plans (SweepPlans)

// a plan entry: a side of a surface, or (Stage 129) one piece of it
struct Entry {
	int first = 0;
	Side second = Side::Left;
	std::optional<std::array<double, 6>> span;
	Entry(int id, Side side) : first(id), second(side) {}
	Entry(const std::pair<int, Side> &e) : first(e.first), second(e.second) {}
	Entry(int id, Side side, const std::array<double, 6> &sp) : first(id), second(side), span(sp) {}
};
using Plan = std::vector<Entry>;

struct Marked {
	int id;
	bool left, right;
};

std::vector<Marked> sides_with(const std::vector<BuiltSurface> &surfaces, bool (*test)(const BuiltEffect &)) {
	std::vector<Marked> out;
	const rc::Flags none;
	for (const BuiltSurface &s : surfaces) {
		const BuiltSide *l = active_side(s, Side::Left, none);
		const BuiltSide *r = active_side(s, Side::Right, none);
		const bool lt = l && test(l->effect);
		const bool rt = r && test(r->effect);
		if (lt || rt) {
			out.push_back({s.id, lt, rt});
		}
	}
	return out;
}

bool is_reflection(const BuiltEffect &e) { return e.type == "reflection"; }
bool is_inversion(const BuiltEffect &e) { return e.type == "inversion"; }
bool is_portal_effect(const BuiltEffect &e) { return e.type == "portal"; }
bool is_transformative(const BuiltEffect &e) { return kind_of(e) == EffectKind::Transformative; }

std::pair<int, Side> entry(const Marked &m) { return {m.id, m.left ? Side::Left : Side::Right}; }

std::vector<Plan> each_side_plans(const std::vector<Marked> &ms) {
	std::vector<Plan> plans;
	for (const Marked &m : ms) {
		if (m.left) plans.push_back({{m.id, Side::Left}});
		if (m.right) plans.push_back({{m.id, Side::Right}});
	}
	return plans;
}

std::vector<Plan> reflective_plans(const std::vector<BuiltSurface> &s) {
	const std::vector<Marked> m = sides_with(s, is_reflection);
	std::vector<Plan> plans = each_side_plans(m);
	if (m.size() >= 2) plans.push_back({entry(m[0]), entry(m[1])});
	if (m.size() >= 1) plans.push_back({entry(m[0]), entry(m[0])});
	if (m.size() >= 3) plans.push_back({entry(m[0]), entry(m[1]), entry(m[2])});
	if (m.size() >= 2) plans.push_back({entry(m[0]), entry(m[1]), entry(m[0])});
	return plans;
}

std::vector<Plan> inversion_plans(const std::vector<BuiltSurface> &s) {
	const std::vector<Marked> m = sides_with(s, is_inversion);
	std::vector<Plan> plans = each_side_plans(m);
	if (m.size() >= 1) plans.push_back({entry(m[0]), entry(m[0])});
	return plans;
}

std::vector<Plan> portal_plans(const std::vector<BuiltSurface> &s) {
	const std::vector<Marked> m = sides_with(s, is_portal_effect);
	std::vector<Plan> plans = each_side_plans(m);
	if (m.size() >= 1) plans.push_back({entry(m[0]), entry(m[0])});
	return plans;
}

std::vector<int> projective_surfaces(const std::vector<BuiltSurface> &surfaces) {
	std::vector<int> out;
	for (const BuiltSurface &s : surfaces) {
		const BuiltSide *c = active_side(s, Side::Left, rc::Flags{});
		if (c && kind_of(c->effect) == EffectKind::Projective) {
			out.push_back(s.id);
		}
	}
	return out;
}

std::vector<Plan> projective_plans(const std::vector<BuiltSurface> &s) {
	const std::vector<int> p = projective_surfaces(s);
	std::vector<Plan> plans;
	for (int id : p) {
		plans.push_back({{id, Side::Left}});
		plans.push_back({{id, Side::Right}});
	}
	if (p.size() >= 2) plans.push_back({{p[0], Side::Left}, {p[1], Side::Left}});
	return plans;
}

std::vector<Plan> composition_plans(const std::vector<BuiltSurface> &s) {
	const std::vector<int> p = projective_surfaces(s);
	const std::vector<Marked> t = sides_with(s, is_transformative);
	std::vector<Plan> plans;
	if (p.empty() || t.empty()) {
		return plans;
	}
	for (int id : p) {
		for (Side sd : {Side::Left, Side::Right}) {
			plans.push_back({{id, sd}, entry(t[0])});
			plans.push_back({entry(t[0]), {id, sd}});
		}
	}
	return plans;
}

std::vector<Plan> scene_plans(const std::vector<BuiltSurface> &s) {
	std::vector<Plan> out = reflective_plans(s);
	for (auto *f : {inversion_plans, portal_plans, projective_plans}) {
		const std::vector<Plan> more = f(s);
		out.insert(out.end(), more.begin(), more.end());
	}
	return out;
}

std::vector<Plan> infinity_plans(const std::vector<BuiltSurface> &s) {
	const std::pair<int, Side> inf{rc::INFINITY_ID, Side::Left};
	std::vector<Plan> plans{{inf}, {inf, inf}};
	for (const Plan &p : scene_plans(s)) {
		if (p.size() == 1) {
			plans.push_back({inf, p[0]});
			plans.push_back({p[0], inf});
			break;
		}
	}
	return plans;
}

// Stage 129: every piece of a split surface's plannable side, planned alone (its
// span: GAME_SPEC 9.7) -- a beam meeting another piece of the same design surface
// leaves the plan there (14.2)
std::vector<Plan> piece_plans(const std::vector<BuiltSurface> &s) {
	const Pieces pieces = build_pieces(s);
	const rc::Flags none;
	std::vector<Plan> plans;
	for (const BuiltSurface &b : s) {
		const std::vector<Piece> &ps = pieces.of(b.id);
		if (ps.size() < 2) {
			continue;
		}
		for (Side sd : {Side::Left, Side::Right}) {
			const BuiltSide *c = active_side(b, sd, none);
			if (c == nullptr || !c->interactive || kind_of(c->effect) != EffectKind::Transformative) {
				continue;
			}
			for (const Piece &pc : ps) {
				if (pc.span) {
					plans.push_back({Entry(b.id, sd, *pc.span)});
				}
			}
		}
	}
	return plans;
}

std::vector<Plan> plans_of(const std::string &rule, const std::vector<BuiltSurface> &s) {
	std::vector<Plan> out;
	size_t at = 0;
	while (at <= rule.size()) {
		const size_t plus = std::min(rule.find('+', at), rule.size());
		const std::string part = rule.substr(at, plus - at);
		std::vector<Plan> more;
		if (part == "empty") more = {Plan{}};
		else if (part == "reflective") more = reflective_plans(s);
		else if (part == "inversion") more = inversion_plans(s);
		else if (part == "scene") more = scene_plans(s);
		else if (part == "infinity") more = infinity_plans(s);
		else if (part == "projective") more = projective_plans(s);
		else if (part == "composition") more = composition_plans(s);
		else if (part == "pieces") more = piece_plans(s);
		else throw std::runtime_error("unknown plan rule '" + part + "'");
		out.insert(out.end(), more.begin(), more.end());
		at = plus + 1;
	}
	return out;
}

// ---------------------------------------------------------------- the positions (SweepRunner)

struct Bounds {
	V2 min{0.0f, 0.0f};
	V2 max{1920.0f, 1080.0f};
};

Bounds room_bounds(const Level &lv) {
	const rc::Rect2f r = rc::Rect2f::of(lv.room.x, lv.room.y, lv.room.w, lv.room.h);
	return {r.position + V2{10.0f, 10.0f}, r.position + r.size - V2{10.0f, 10.0f}};
}

// SweepRunner._add_unique: points keyed by their "%f,%f" spelling
void add_unique(std::vector<V2> &points, std::set<std::string> &seen, const V2 &p) {
	char key[96];
	std::snprintf(key, sizeof key, "%f,%f", double(p.x), double(p.y));
	if (seen.insert(key).second) {
		points.push_back(p);
	}
}

std::vector<V2> points_of_interest(const Level &lv, const std::vector<BuiltSurface> &surfaces) {
	std::vector<V2> points;
	std::set<std::string> seen;
	const rc::Rect2f r = rc::Rect2f::of(lv.room.x, lv.room.y, lv.room.w, lv.room.h);
	for (const V2 &c : {r.position, V2{r.end().x, r.position.y}, r.end(), V2{r.position.x, r.end().y}}) {
		add_unique(points, seen, c);
	}
	const rc::Rect2f screen = rc::Rect2f::of(0, 0, 1920, 1080);
	for (const V2 &c : {screen.position, V2{screen.end().x, screen.position.y}, screen.end(), V2{screen.position.x, screen.end().y}}) {
		add_unique(points, seen, c);
	}
	for (const BuiltSurface &s : surfaces) {
		add_unique(points, seen, s.seg.s);
		add_unique(points, seen, s.seg.e);
		if (!std::isinf(s.seg.v.x) && !std::isinf(s.seg.v.y)) {
			add_unique(points, seen, s.seg.v);
		}
	}
	return points;
}

std::vector<V2> build_positions(const Bounds &b, int grid, int fuzz, std::uint64_t seed, const std::vector<V2> &custom) {
	std::vector<V2> out;
	const double x0 = b.min.x, y0 = b.min.y, x1 = b.max.x, y1 = b.max.y;
	for (int y = 0; y < grid; y++) {
		for (int x = 0; x < grid; x++) {
			double px, py;
			if (grid > 1) {
				px = x0 + (x1 - x0) * x / (grid - 1);
				py = y0 + (y1 - y0) * y / (grid - 1);
			} else {
				px = (x0 + x1) / 2.0;
				py = (y0 + y1) / 2.0;
			}
			out.push_back(V2::of(px, py));
		}
	}
	GodotRng rng(seed);
	for (int i = 0; i < fuzz; i++) {
		const float fx = rng.randf_range(b.min.x, b.max.x);
		const float fy = rng.randf_range(b.min.y, b.max.y);
		out.push_back(V2{fx, fy});
	}
	out.insert(out.end(), custom.begin(), custom.end());
	return out;
}

std::vector<V2> q0_positions(const Level &lv) {
	std::vector<V2> out;
	for (const LevelSurface &ls : lv.surfaces) {
		if (ls.spec.pre_transform) {
			const V2 q0 = ls.spec.center;
			for (const V2 &d : {V2{-200, 100}, V2{-125, 125}, V2{-200, -100}, V2{-125, -125}, V2{125, 125}}) {
				out.push_back(q0 + d);
			}
			break; // the first pre-transform's Q0
		}
	}
	return out;
}

std::vector<V2> on_surface_positions(const std::vector<BuiltSurface> &surfaces) {
	std::vector<V2> out;
	for (const BuiltSurface &s : surfaces) {
		if (s.pre_transform || s.seg.is_line() || !s.seg.full) {
			continue;
		}
		const V2 c = rc::arc::center_of(s.seg.get_carrier());
		const double r = s.seg.get_carrier().radius();
		for (const V2 &d : {V2::of(r, 0), V2::of(-r, 0), V2::of(0, r), V2::of(0, -r)}) {
			out.push_back(c + d);
		}
	}
	return out;
}

// ---------------------------------------------------------------- the manifest

json vec_or_null(const V2 &p) { return p.is_inf() ? json(nullptr) : json::array({double(p.x), double(p.y)}); }

// InvariantChecker.surface_infos under the flags
json infos_of(const std::vector<BuiltSurface> &surfaces, const rc::Flags &flags) {
	json out = json::array();
	for (const BuiltSurface &s : surfaces) {
		if (s.pre_transform) {
			continue;
		}
		bool has_effect = s.player_solid;
		for (Side sd : {Side::Left, Side::Right}) {
			const BuiltSide *c = active_side(s, sd, flags);
			has_effect = has_effect || (c && kind_of(c->effect) == EffectKind::Transformative);
		}
		const rc::arc::GeneralizedCircle &g = s.seg.get_carrier();
		out.push_back({{"id", s.id}, {"has_effect", has_effect},
			{"segment", {{"start", vec_or_null(s.seg.s)}, {"end", vec_or_null(s.seg.e)}, {"via", vec_or_null(s.seg.v)}, {"full", s.seg.full}}},
			{"carrier", {{"a", g.a}, {"b", g.b}, {"c", g.c}, {"d", g.d}}}});
	}
	return out;
}

json flags_json(const rc::Flags &f) {
	json out = json::object();
	for (const auto &[k, v] : f.entries()) {
		out[k] = std::visit([](const auto &x) -> json { return x; }, v);
	}
	return out;
}

std::vector<std::string> level_names(const json &spec, const json &lists, const fs::path &levels) {
	std::vector<std::string> out;
	for (const json &item : spec) {
		if (item.is_string()) {
			out.push_back(item.get<std::string>());
		} else if (item.contains("list")) {
			for (const json &n : lists.at(item["list"].get<std::string>())) {
				out.push_back(n.get<std::string>());
			}
		} else if (item.contains("dir")) {
			const std::string dir = item["dir"].get<std::string>();
			std::vector<std::string> names;
			for (const auto &e : fs::directory_iterator(levels / dir)) {
				if (e.path().extension() == ".json") {
					names.push_back(dir + "/" + e.path().stem().string());
				}
			}
			std::sort(names.begin(), names.end());
			out.insert(out.end(), names.begin(), names.end());
		}
	}
	return out;
}

} // namespace

std::vector<std::pair<std::string, json>> manifests(const json &recipes, const std::string &levels_dir) {
	const fs::path levels = levels_dir;
	std::vector<std::pair<std::string, json>> out;
	std::map<std::string, int> seq;
	for (const json &g : recipes.at("groups")) {
		// the levels this group sweeps (a combo is combo_base with a pair of rows)
		struct Target {
			std::string name;
			Level level;
		};
		std::vector<Target> targets;
		if (g.value("combos", false)) {
			const Level base = load_level((levels / "test_levels/combo_base.json").string());
			std::vector<std::pair<Pair, Pair>> combos;
			for (Pair p : NON_PORTAL) combos.push_back({p, p});
			for (Pair p : NON_PORTAL) combos.push_back({PORTAL, p});
			for (Pair p : NON_PORTAL) combos.push_back({p, PORTAL});
			for (const auto &[l, c] : combos) {
				targets.push_back({"test_levels/combo_base", combo_level(base, l, c)});
			}
		} else {
			for (const std::string &n : level_names(g.at("levels"), recipes.value("lists", json::object()), levels)) {
				targets.push_back({n, load_level((levels / (n + ".json")).string())});
			}
		}
		const json &pos = g.at("positions");
		for (const Target &t : targets) {
			// the world: as built, or after the group's shots (played on the native session)
			std::vector<BuiltSurface> surfaces;
			rc::Flags flags;
			if (g.contains("shots")) {
				Session s(t.level);
				for (const json &shot : g["shots"]) {
					s.teleport(V2::of(shot[0][0].get<double>(), shot[0][1].get<double>()));
					s.set_cursor(V2::of(shot[1][0].get<double>(), shot[1][1].get<double>()));
					s.fire(-1, 1.0 / 60.0);
				}
				surfaces = s.world().surfaces();
				flags = s.flags();
			} else {
				surfaces = build_level(t.level);
			}
			const std::vector<Plan> plans = plans_of(g.at("plans").get<std::string>(), surfaces);
			if (g.value("skip_if_no_plans", false) && plans.empty()) {
				continue;
			}
			// the positions
			std::vector<V2> positions;
			const std::string rule = pos.at("rule").get<std::string>();
			const int grid = pos.at("grid").get<int>(), fuzz = pos.at("fuzz").get<int>();
			const auto seed = pos.at("seed").get<std::uint64_t>();
			if (rule == "sweep") {
				bool blocks = false;
				for (const LevelSurface &ls : t.level.surfaces) {
					blocks = blocks || ls.label == std::optional<std::string>("Block");
				}
				const Bounds b = t.level.room_built || blocks ? room_bounds(t.level) : Bounds{};
				positions = build_positions(b, grid, fuzz, seed, points_of_interest(t.level, surfaces));
			} else if (rule == "bounded") {
				const std::vector<V2> poi = pos.value("poi", false) ? points_of_interest(t.level, surfaces) : std::vector<V2>{};
				positions = build_positions(room_bounds(t.level), grid, fuzz, seed, poi);
				if (pos.value("q0", false)) {
					for (const V2 &p : q0_positions(t.level)) positions.push_back(p);
				}
				if (pos.value("on_surface", false)) {
					for (const V2 &p : on_surface_positions(surfaces)) positions.push_back(p);
				}
			} else {
				throw std::runtime_error("unknown position rule '" + rule + "'");
			}
			// the manifest (SweepRunner._write_manifest)
			const std::string base = fs::path(t.name).filename().string();
			const int n = seq[base]++;
			json pos_json = json::array();
			for (const V2 &p : positions) {
				pos_json.push_back(json::array({double(p.x), double(p.y)}));
			}
			json plans_json = json::array();
			for (const Plan &p : plans) {
				json entries = json::array();
				for (const Entry &e : p) {
					json je = {{"surface_id", e.first}, {"side", e.second == Side::Left ? "LEFT" : "RIGHT"}};
					if (e.span) {
						const auto &sp = *e.span;
						je["span"] = {{"start", {sp[0], sp[1]}}, {"end", {sp[2], sp[3]}}, {"via", {sp[4], sp[5]}}};
					}
					entries.push_back(je);
				}
				plans_json.push_back(entries);
			}
			json m = {
				{"schema", "sweep-manifest/1"},
				{"scene", "res://scenes/" + t.name + ".tscn"},
				{"seq", n},
				{"defs", describe_level(surfaces)},
				{"infos", infos_of(surfaces, flags)},
				{"positions", pos_json},
				{"plans", plans_json},
				{"flags", flags_json(flags)},
				{"skip_equal", g.value("skip_equal", true)},
				{"emit_infinity", true},
				{"visibility", g.value("visibility", false)},
				{"carrier_cursors", g.value("carrier_cursors", false)},
			};
			// Stage 129: the known findings this group's violations may hold (the sweep
			// test names them instead of failing), and the shots that made its world
			// (the violation viewer replays them)
			if (g.contains("known")) {
				m["known"] = g["known"];
			}
			if (g.contains("shots")) {
				m["shots"] = g["shots"];
			}
			out.push_back({base + "__" + std::to_string(n) + ".json", std::move(m)});
		}
	}
	return out;
}

} // namespace rcg::sweeps
