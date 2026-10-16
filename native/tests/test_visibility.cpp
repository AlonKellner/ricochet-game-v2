// Stage 110: the visibility region (GAME_SPEC 15) against its definition -- a
// cursor is inside iff the preview's merged steps before it are ALIGNED and it
// is reached -- on the shipped levels' scene dumps.
#include "support/json_defs.hpp"
#include "support/png_debug.hpp"
#include "rc/sweep.hpp"
#include "rc/visibility.hpp"

#include <doctest/doctest.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace rc;

namespace {

// RC_VIS_FULL=1: every player, plan and cursor (minutes); by default a sample
// (the sharded GUT sweep, test_visibility_sweep.gd, covers the levels widely).
bool full() { return std::getenv("RC_VIS_FULL") != nullptr; }

// Under the sanitizers (make native-asan) a third of the sample: every third file.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RC_VIS_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define RC_VIS_ASAN 1
#endif
#ifdef RC_VIS_ASAN
constexpr size_t FILE_STRIDE = 3;
#else
constexpr size_t FILE_STRIDE = 1;
#endif
bool sampled_out(size_t file_index) { return !full() && file_index % FILE_STRIDE != 0; }

ojson load_dump(const std::string &path) {
	std::ifstream is(path);
	return ojson::parse(is);
}

// The definition: what the preview shows (nullopt: no trace, an unclassified cursor).
std::optional<bool> defined_visible(Tracer &t, V2 player, V2 cursor, const std::vector<PlanItem> &plan, const Flags &flags) {
	const std::optional<TracePair> pair = t.trace_pair(player, cursor, plan, flags);
	if (!pair) {
		return std::nullopt;
	}
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	const std::vector<inv::SurfaceInfo> none;
	const TracePath dp = sweep::prepare_for_display(pair->physical, bounds, none);
	const TracePath dl = sweep::prepare_for_display(pair->planned, bounds, none);
	if (dl.cursor_index < 0) {
		return false;
	}
	return inv::preview_visible(dl, sweep::merge(dl.steps, dp.steps, dl.cursor_index));
}

struct PinStats {
	int checked = 0;
	int mismatches = 0;
	std::vector<std::string> samples;
	std::map<std::string, int> by_label;
	std::map<std::string, int> known;
};

void pin(Tracer &t, const vis::Result &r, V2 player, const std::vector<V2> &cursors, const std::vector<PlanItem> &plan, PinStats &st,
		const std::string &label) {
	for (const V2 &c : cursors) {
		const vis::Membership m = vis::contains(r, vis::Pt{c.x, c.y});
		if (m == vis::Membership::Unclassified) {
			continue;
		}
		const std::optional<bool> d = defined_visible(t, player, c, plan, Flags());
		if (!d) {
			continue;
		}
		st.checked++;
		if ((m == vis::Membership::Inside) != *d) {
			const std::string tag = inv::pin_tag(r, vis::Pt{c.x, c.y});
			if (!tag.empty()) {
				st.known[tag]++; // K7, K8 (docs/TDD_14_VISIBILITY.md)
				continue;
			}
			st.mismatches++;
			char lb[256];
			std::snprintf(lb, sizeof lb, "%s player (%g, %g)", label.c_str(), player.x, player.y);
			st.by_label[lb]++;
			if (st.samples.size() < 12) {
				char buf[256];
				std::snprintf(buf, sizeof buf, "%s player (%g, %g) cursor (%g, %g): region %s, preview %s, margin %.3g", label.c_str(), player.x,
					player.y, c.x, c.y, m == vis::Membership::Inside ? "in" : "out", *d ? "visible" : "not", vis::margin(r, vis::Pt{c.x, c.y}));
				st.samples.push_back(buf);
			}
		}
	}
}

} // namespace

TEST_CASE("visibility: the region and the traces agree with the oracle's labels") {
	const ojson fx = load_dump(std::string(RC_REPO_ROOT) + "/bench/fixtures/stage110_visibility.json");
	int checked = 0;
	for (const ojson &c : fx["cases"]) {
		const std::string name = c["name"].get<std::string>();
		const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/" + c["scene"].get<std::string>() + ".surfaces.json");
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs_from_json(d["surfaces"]));
		std::vector<PlanItem> plan;
		for (const ojson &p : c["plan"]) {
			plan.push_back(PlanItem{p["surface_id"].get<int>(), p["side"].get<std::string>() == "LEFT", std::nullopt, std::nullopt});
		}
		const V2 player{float(c["player"][0].get<double>()), float(c["player"][1].get<double>())};
		const vis::Result r = vis::compute(t, vis::Pt{player.x, player.y}, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
		REQUIRE_MESSAGE(r.supported, name);
		for (const ojson &x : c["cursors"]) {
			const V2 cur{float(x[0].get<double>()), float(x[1].get<double>())};
			const bool label = x[2].get<bool>();
			char where[128];
			std::snprintf(where, sizeof where, "%s cursor (%g, %g): oracle %s", name.c_str(), cur.x, cur.y, label ? "visible" : "not");
			const vis::Membership m = vis::contains(r, vis::Pt{cur.x, cur.y});
			if (m != vis::Membership::Unclassified) {
				CHECK_MESSAGE((m == vis::Membership::Inside) == label, (std::string(where) + ", region disagrees"));
				checked++;
			}
			const std::optional<bool> dv = defined_visible(t, player, cur, plan, Flags());
			CHECK_MESSAGE((dv && *dv == label), (std::string(where) + ", the native preview disagrees"));
		}
	}
	CHECK(checked > 800);
}

TEST_CASE("visibility: the empty plan's region is the non-divergence set on every test level") {
	const std::string dir = std::string(RC_REPO_ROOT) + "/bench/scenes";
	std::vector<std::string> files;
	for (const auto &f : std::filesystem::directory_iterator(dir)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("test_levels__", 0) == 0) {
			files.push_back(f.path().string());
		}
	}
	std::sort(files.begin(), files.end());
	PinStats st;
	int regions = 0;
	for (size_t fi = 0; fi < files.size(); fi++) {
		if (sampled_out(fi)) {
			continue;
		}
		const std::string &file = files[fi];
		const ojson d = load_dump(file);
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs_from_json(d["surfaces"]));
		std::vector<V2> cursors;
		for (int i = 0; i <= 16; i++) {
			for (int j = 0; j <= 9; j++) {
				cursors.push_back(V2{float(60 + i * 112.5), float(40 + j * 111.0)});
			}
		}
		for (const auto &s : d["surfaces"]) {
			cursors.push_back(V2{s["start"][0].get<float>(), s["start"][1].get<float>()});
			cursors.push_back(V2{s["end"][0].get<float>(), s["end"][1].get<float>()});
		}
		const std::string name = std::filesystem::path(file).filename().string();
		const std::vector<V2> players = full() ? std::vector<V2>{V2{960, 540}, V2{500, 300}, V2{1400, 800}} : std::vector<V2>{V2{500, 300}};
		for (const V2 player : players) {
			const vis::Result r = vis::compute(t, vis::Pt{player.x, player.y}, {}, Flags(), vis::Rect{0, 0, 1920, 1080});
			REQUIRE_MESSAGE(r.supported, (name + ": " + r.reason));
			regions++;
			for (const std::string &n : r.notes) {
				const std::string msg = name + ": " + n;
				MESSAGE(msg);
			}
			pin(t, r, player, cursors, {}, st, name);
		}
	}
	for (const std::string &s : st.samples) {
		MESSAGE(s);
	}
	const std::string summary = "regions " + std::to_string(regions) + ", cursors checked " + std::to_string(st.checked) + ", mismatches "
		+ std::to_string(st.mismatches);
	MESSAGE(summary);
	for (const auto &kv : st.known) {
		const std::string line = "known finding (" + kv.first + "): " + std::to_string(kv.second) + " cursors";
		MESSAGE(line);
	}
	CHECK(st.mismatches == 0);
}

namespace {

// A violation's message with its arguments (Godot's % formatting, the subset used).
std::string render(const inv::Violation &vi) {
	std::string out;
	size_t ai = 0;
	for (size_t i = 0; i < vi.fmt.size(); i++) {
		if (vi.fmt[i] != '%' || i + 1 >= vi.fmt.size()) {
			out += vi.fmt[i];
			continue;
		}
		size_t j = i + 1;
		while (j < vi.fmt.size() && (std::isdigit(static_cast<unsigned char>(vi.fmt[j])) || vi.fmt[j] == '.')) j++;
		const std::string spec = vi.fmt.substr(i, j - i + 1);
		const inv::Arg &a = ai < vi.args.size() ? vi.args[ai++] : inv::Arg(std::string("?"));
		char buf[256];
		if (const auto *x = std::get_if<int64_t>(&a)) std::snprintf(buf, sizeof buf, "%lld", (long long)*x);
		else if (const auto *d = std::get_if<double>(&a)) std::snprintf(buf, sizeof buf, spec.c_str(), *d);
		else if (const auto *str = std::get_if<std::string>(&a)) std::snprintf(buf, sizeof buf, "%s", str->c_str());
		else std::snprintf(buf, sizeof buf, "?");
		out += buf;
		i = j;
	}
	return out;
}

// Plans on a level: one entry per transformative side (and pass / state sides),
// then pairs of the first few mirror entries, then an infinity entry.
std::vector<std::vector<PlanItem>> level_plans(const std::vector<SurfaceDef> &defs) {
	std::vector<PlanItem> singles;
	for (const SurfaceDef &d : defs) {
		if (d.pre_transform) {
			continue;
		}
		for (const bool left : {true, false}) {
			const std::optional<SideDef> &side = left ? d.left : d.right;
			if (side && (is_transformative(side->type) || !side->states.empty())) {
				singles.push_back(PlanItem{d.id, left, std::nullopt, std::nullopt});
			}
		}
	}
	std::vector<std::vector<PlanItem>> plans;
	for (const PlanItem &p : singles) {
		plans.push_back({p});
	}
	for (size_t i = 0; i < singles.size() && i < 4; i++) {
		for (size_t j = 0; j < singles.size() && j < 4; j++) {
			if (singles[i].surface_id != singles[j].surface_id) {
				plans.push_back({singles[i], singles[j]});
			}
		}
	}
	plans.push_back({PlanItem{INFINITY_ID, true, std::nullopt, std::nullopt}});
	return plans;
}

} // namespace

TEST_CASE("visibility: plans' regions are the non-divergence set on the shipped levels") {
	const std::string dir = std::string(RC_REPO_ROOT) + "/bench/scenes";
	std::vector<std::string> files;
	for (const auto &f : std::filesystem::directory_iterator(dir)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("test_levels__", 0) == 0 || name.rfind("playground__", 0) == 0) {
			files.push_back(f.path().string());
		}
	}
	std::sort(files.begin(), files.end());
	PinStats st;
	int regions = 0;
	int unsupported = 0;
	int notes = 0;
	for (size_t fi = 0; fi < files.size(); fi++) {
		if (sampled_out(fi)) {
			continue;
		}
		const std::string &file = files[fi];
		const ojson d = load_dump(file);
		const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs);
		std::vector<V2> cursors;
		for (int i = 0; i <= 12; i++) {
			for (int j = 0; j <= 7; j++) {
				cursors.push_back(V2{float(60 + i * 150.0), float(40 + j * 142.0)});
			}
		}
		for (const auto &s : d["surfaces"]) {
			cursors.push_back(V2{s["start"][0].get<float>(), s["start"][1].get<float>()});
			cursors.push_back(V2{s["end"][0].get<float>(), s["end"][1].get<float>()});
		}
		const std::string name = std::filesystem::path(file).filename().string();
		const std::vector<std::vector<PlanItem>> all_plans = level_plans(defs);
		for (size_t pi = 0; pi < all_plans.size(); pi++) {
			const std::vector<PlanItem> &plan = all_plans[pi];
			if (!full() && pi % 4 != 0 && pi + 1 != all_plans.size()) {
				continue; // a sample: every fourth plan, and the infinity plan
			}
			const std::vector<V2> players = full() ? std::vector<V2>{V2{960, 540}, V2{500, 300}} : std::vector<V2>{V2{500, 300}};
			for (const V2 player : players) {
				const vis::Result r = vis::compute(t, vis::Pt{player.x, player.y}, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
				if (!r.supported) {
					unsupported++;
					continue;
				}
				regions++;
				std::string label = name + " plan";
				for (const PlanItem &p : plan) {
					label += " " + std::to_string(p.surface_id) + (p.left ? "L" : "R");
				}
				for (const std::string &n : r.notes) {
					if (notes++ < 12) {
						const std::string msg = label + ": " + n;
						MESSAGE(msg);
					}
				}
				pin(t, r, player, cursors, plan, st, label);
			}
		}
	}
	for (const std::string &s : st.samples) {
		MESSAGE(s);
	}
	for (const auto &kv : st.by_label) {
		const std::string line = kv.first + ": " + std::to_string(kv.second);
		MESSAGE(line);
	}
	const std::string summary = "regions " + std::to_string(regions) + " (unsupported " + std::to_string(unsupported) + "), notes "
		+ std::to_string(notes) + ", cursors checked " + std::to_string(st.checked) + ", mismatches " + std::to_string(st.mismatches);
	MESSAGE(summary);
	for (const auto &kv : st.known) {
		const std::string line = "known finding (" + kv.first + "): " + std::to_string(kv.second) + " cursors";
		MESSAGE(line);
	}
	CHECK(st.mismatches == 0);
}

// RC_VIS_SCENE=<dump name> RC_VIS_PLAN="1L,2R" RC_VIS_PLAYER="x,y" RC_VIS_SVG=<file>:
// one region drawn with its cells and members, the definition sampled on a grid.
TEST_CASE("visibility debug" * doctest::skip(true)) {
	const char *scene = std::getenv("RC_VIS_SCENE");
	if (scene == nullptr) {
		return;
	}
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/" + scene + ".surfaces.json");
	Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(defs_from_json(d["surfaces"]));
	std::vector<PlanItem> plan;
	if (const char *ps = std::getenv("RC_VIS_PLAN")) {
		std::string str = ps;
		size_t pos = 0;
		while (pos < str.size()) {
			size_t comma = str.find(',', pos);
			if (comma == std::string::npos) comma = str.size();
			const std::string tok = str.substr(pos, comma - pos);
			if (!tok.empty()) {
				plan.push_back(PlanItem{std::stoi(tok.substr(0, tok.size() - 1)), tok.back() == 'L', std::nullopt, std::nullopt});
			}
			pos = comma + 1;
		}
	}
	vis::Pt P{960, 540};
	if (const char *pp = std::getenv("RC_VIS_PLAYER")) {
		std::sscanf(pp, "%lf,%lf", &P.x, &P.y);
	}
	if (const char *pc = std::getenv("RC_VIS_PROBE")) {
		vis::Pt c;
		std::sscanf(pc, "%lf,%lf", &c.x, &c.y);
		vis::Builder b(t, P, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
		const std::optional<vis::Cast> k = b.probe(c);
		const vis::Result &rr = b.result();
		std::printf("P_K (%.3f,%.3f) Q_K %s (%.3f,%.3f)\n", rr.pk.x, rr.pk.y, rr.qk_inf ? "inf" : "", rr.qk.x, rr.qk.y);
		if (k) {
			std::printf("cast: %s cursor_ok %d near (%.3f,%.3f) end (%.3f,%.3f)\n", k->shape().c_str(), int(k->cursor_ok), k->near_pt.x, k->near_pt.y,
				k->end_pt.x, k->end_pt.y);
			for (const auto &iv : k->iv) std::printf("  interval %s %.6g -> %s %.6g\n", vis::end_name(iv.a.kind), iv.a.t, vis::end_name(iv.b.kind), iv.b.t);
		}
		const std::optional<LadderResult> lr = t.solve(P.x, P.y, c.x, c.y, plan, Flags());
		if (lr) {
			for (const char *which : {"planned", "physical"}) {
				const auto ev = vis::cast_events(std::string(which) == "planned" ? lr->planned : lr->physical, Flags().canonical_key());
				std::printf("%s:\n", which);
				for (const auto &e : ev) {
					std::printf("  %-8s s%d %s applied %d (%.3f,%.3f) flags %s\n", event_kind_name(e.kind), e.sid, e.left ? "L" : "R", int(e.applied), e.pt.x, e.pt.y,
						e.flags.c_str());
				}
			}
		}
	}
	const vis::Result r = vis::compute(t, P, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
	std::printf("supported %d %s casts %d refinements %d members %zu cells %zu loops %zu\n", int(r.supported), r.reason.c_str(), r.casts,
		r.refinements, r.members.size(), r.cells.size(), r.loops.size());
	for (const std::string &n : r.notes) std::printf("note: %s\n", n.c_str());
	{
		inv::VisInput in{&r, &t, P, plan, Flags(), vis::Rect{0, 0, 1920, 1080}};
		int shown = 0;
		for (const inv::Violation &vi : inv::check_visibility(in)) {
			if (shown++ < 25) std::printf("check: %s\n", render(vi).c_str());
		}
		std::printf("checks: %d violations\n", shown);
	}
	if (const char *ps = std::getenv("RC_VIS_PIN")) {
		// RC_VIS_PIN=<step>: the pin on a grid over the level
		const double step = std::max(1.0, std::atof(ps));
		std::vector<V2> cursors;
		for (double x = 0; x <= 1920; x += step) {
			for (double y = 0; y <= 1080; y += step) {
				cursors.push_back(V2{float(x), float(y)});
			}
		}
		PinStats st;
		pin(t, r, V2{float(P.x), float(P.y)}, cursors, plan, st, std::string(scene));
		for (const std::string &sm : st.samples) std::printf("pin: %s\n", sm.c_str());
		std::printf("pin: %d cursors, %d mismatches\n", st.checked, st.mismatches);
	}
	for (size_t i = 0; i < r.members.size() && std::getenv("RC_VIS_PIN") == nullptr; i++) {
		const auto &m = r.members[i];
		std::printf("member %zu dir(%.6f,%.6f) %s cursor(%.3f,%.3f)\n  crit %s\n  minus %s\n  plus %s\n", i, m.dir.x, m.dir.y, m.why.c_str(), m.cursor.x,
			m.cursor.y, m.crit.shape().c_str(), m.minus.shape().c_str(), m.plus.shape().c_str());
	}
	if (const char *cl = std::getenv("RC_VIS_CELLS")) {
		std::string str = cl;
		for (size_t ci = 0; ci < r.cells.size(); ci++) {
			double area = 0.0;
			for (const auto &l : r.cells[ci].loops) area += vis::loop_area(l);
			if (str == "pos" && area > 0) str += "," + std::to_string(ci) + ",";
		}
		for (size_t ci = 0; ci < r.cells.size(); ci++) {
			if (str.find("," + std::to_string(ci) + ",") == std::string::npos) continue;
			const auto &c = r.cells[ci];
			std::printf("cell %zu members %d-%d loops %zu mid %s cursor (%.3f,%.3f) near (%.3f,%.3f) end (%.3f,%.3f)\n", ci, c.left, c.right, c.loops.size(),
				c.mid.shape().c_str(), c.mid.cursor.x, c.mid.cursor.y, c.mid.near_pt.x, c.mid.near_pt.y, c.mid.end_pt.x, c.mid.end_pt.y);
			for (const auto &iv : c.mid.iv) std::printf("  interval %s %.6g -> %s %.6g\n", vis::end_name(iv.a.kind), iv.a.t, vis::end_name(iv.b.kind), iv.b.t);
			for (const auto &e : c.edges()) {
				std::printf("  kind %d m%d s%d %s (%.3f,%.3f) via (%.3f,%.3f) -> (%.3f,%.3f) arc%d\n", int(e.kind), e.member, e.surface, vis::end_name(e.role), e.a.x,
					e.a.y, e.via.x, e.via.y, e.b.x, e.b.y, int(e.arc));
			}
		}
	}
	if (const char *near = std::getenv("RC_VIS_NEAR")) {
		vis::Pt c;
		std::sscanf(near, "%lf,%lf", &c.x, &c.y);
		std::printf("contains(%.3f,%.3f) = %d, margin %.3g\n", c.x, c.y, int(vis::contains(r, c)), vis::margin(r, c));
		{
			const std::optional<bool> dv = defined_visible(t, V2{float(P.x), float(P.y)}, V2{float(c.x), float(c.y)}, plan, Flags());
			std::printf("defined visible: %s\n", dv ? (*dv ? "yes" : "no") : "unclassified");
			if (const std::optional<TracePair> pair = t.trace_pair(V2{float(P.x), float(P.y)}, V2{float(c.x), float(c.y)}, plan, Flags())) {
				for (const char *which : {"planned", "physical"}) {
					const TracePath tp = sweep::prepare_for_display(std::string(which) == "planned" ? pair->planned : pair->physical,
						Rect2f::of(0, 0, 1920, 1080), std::vector<inv::SurfaceInfo>{});
					std::printf("%s steps %zu cursor_index %d:", which, tp.steps.size(), tp.cursor_index);
					for (const TraceStep &st : tp.steps) std::printf(" [(%g,%g)->(%g,%g) f%d]", st.start.x, st.start.y, st.end.x, st.end.y, st.frame_id);
					std::printf("\n");
				}
			}
		}
		for (size_t li = 0; li < r.loops.size(); li++) {
			for (const auto &e : r.loops[li].edges) {
				const double dd = vis::edge_dist(e, c);
				if (dd < 5.0) {
					std::printf("  loop %zu kind %d m%d s%d %s (%.6f,%.6f) via (%.3f,%.3f) -> (%.6f,%.6f) incl %d incl_a %d dist %.3g\n", li, int(e.kind), e.member,
						e.surface, vis::end_name(e.role), e.a.x, e.a.y, e.via.x, e.via.y, e.b.x, e.b.y, int(e.inclusive), int(e.incl_a), dd);
				}
			}
		}
	}
	for (const auto &w : r.whiskers) {
		std::printf("whisker s%d (%.3f,%.3f) via (%.3f,%.3f) -> (%.3f,%.3f)\n", w.surface, w.a.x, w.a.y, w.via.x, w.via.y, w.b.x, w.b.y);
	}
	for (size_t li = 0; li < r.loops.size(); li++) {
		std::printf("loop %zu area %.1f edges %zu\n", li, r.loops[li].area, r.loops[li].edges.size());
		for (const auto &e : r.loops[li].edges) {
			std::printf("  kind %d m%d s%d %s (%.3f,%.3f)->(%.3f,%.3f) arc%d incl%d\n", int(e.kind), e.member, e.surface, vis::end_name(e.role), e.a.x, e.a.y,
				e.b.x, e.b.y, int(e.arc), int(e.inclusive));
		}
	}
	if (const char *png = std::getenv("RC_VIS_PNG")) {
		// 0.5 px per unit over (-100, -100) .. (2020, 1180), or RC_VIS_VIEW="x0,y0,x1,y1"
		double vx0 = -100, vy0 = -100, vx1 = 2020, vy1 = 1180;
		if (const char *vw = std::getenv("RC_VIS_VIEW")) std::sscanf(vw, "%lf,%lf,%lf,%lf", &vx0, &vy0, &vx1, &vy1);
		const double sc = 1060.0 / (vx1 - vx0);
		test::Image img(1060, int((vy1 - vy0) * sc), 24, 24, 32);
		auto X = [&](double x) { return (x - vx0) * sc; };
		auto Y = [&](double y) { return (y - vy0) * sc; };
		for (int py = 0; py < img.h; py++) {
			for (int px = 0; px < img.w; px++) {
				const vis::Pt q{px / sc + vx0 + 0.25 / sc, py / sc + vy0 + 0.25 / sc};
				const vis::Membership m = vis::contains(r, q);
				if (m == vis::Membership::Inside) img.set(px, py, 70, 70, 80);
			}
		}
		for (const auto &s : d["surfaces"]) {
			if (s["line"].get<bool>()) {
				img.line(X(s["start"][0].get<double>()), Y(s["start"][1].get<double>()), X(s["end"][0].get<double>()), Y(s["end"][1].get<double>()), 255, 140, 0);
			} else if (!s["circle"].is_null()) {
				const double cx = s["circle"]["center"][0].get<double>(), cy = s["circle"]["center"][1].get<double>(), rr = s["circle"]["radius"].get<double>();
				for (int k = 0; k < 720; k++) img.set(int(X(cx + rr * std::cos(k * vis::PI / 360))), int(Y(cy + rr * std::sin(k * vis::PI / 360))), 255, 140, 0);
			}
		}
		for (const auto &c : r.cells) for (const auto &cl : c.loops) {
			const auto pl = vis::polyline(cl);
			for (size_t i = 0; i < pl.size(); i++) {
				const auto &a = pl[i];
				const auto &b = pl[(i + 1) % pl.size()];
				img.line(X(a.x), Y(a.y), X(b.x), Y(b.y), 60, 160, 60);
			}
		}
		for (const auto &l : r.loops) {
			const auto pl = vis::polyline(l.edges);
			for (size_t i = 0; i < pl.size(); i++) {
				const auto &a = pl[i];
				const auto &b = pl[(i + 1) % pl.size()];
				img.line(X(a.x), Y(a.y), X(b.x), Y(b.y), 0, 200, 255);
			}
		}
		for (int i = 0; i <= 48; i++) {
			for (int j = 0; j <= 27; j++) {
				const V2 c{float(vx0 + (vx1 - vx0) * (i + 0.5) / 49.0), float(vy0 + (vy1 - vy0) * (j + 0.5) / 28.0)};
				const vis::Membership m = vis::contains(r, vis::Pt{c.x, c.y});
				const std::optional<bool> dv = defined_visible(t, V2{float(P.x), float(P.y)}, c, plan, Flags());
				if (m == vis::Membership::Unclassified || !dv) continue;
				const bool in = m == vis::Membership::Inside;
				if (in == *dv) img.dot(X(c.x), Y(c.y), 1, in ? 0 : 90, in ? 220 : 90, in ? 0 : 90);
				else img.dot(X(c.x), Y(c.y), 4, 255, 0, in ? 255 : 0);
			}
		}
		img.dot(X(P.x), Y(P.y), 5, 255, 255, 0);
		img.write_png(png);
	}
	if (const char *svg = std::getenv("RC_VIS_SVG")) {
		std::FILE *f = std::fopen(svg, "w");
		std::fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' viewBox='-100 -100 2120 1280' width='1590' height='960'><rect x='-100' y='-100' width='2120' height='1280' fill='#222'/>\n");
		for (const auto &c : r.cells) {
			std::string pts;
			for (const auto &q : vis::polyline(c.edges())) pts += std::to_string(q.x) + "," + std::to_string(q.y) + " ";
			std::fprintf(f, "<polygon points='%s' fill='rgba(255,255,255,0.18)' stroke='#557' stroke-width='0.5'/>\n", pts.c_str());
		}
		for (const auto &l : r.loops) {
			std::string pts;
			for (const auto &q : vis::polyline(l.edges)) pts += std::to_string(q.x) + "," + std::to_string(q.y) + " ";
			std::fprintf(f, "<polygon points='%s' fill='none' stroke='#0cf' stroke-width='1.5'/>\n", pts.c_str());
		}
		for (const auto &s : d["surfaces"]) {
			if (s["line"].get<bool>()) {
				std::fprintf(f, "<line x1='%g' y1='%g' x2='%g' y2='%g' stroke='#f80' stroke-width='3'/>\n", s["start"][0].get<double>(), s["start"][1].get<double>(),
					s["end"][0].get<double>(), s["end"][1].get<double>());
			} else if (!s["circle"].is_null()) {
				std::fprintf(f, "<circle cx='%g' cy='%g' r='%g' fill='none' stroke='#f80' stroke-width='3'/>\n", s["circle"]["center"][0].get<double>(),
					s["circle"]["center"][1].get<double>(), s["circle"]["radius"].get<double>());
			}
		}
		for (int i = 0; i <= 48; i++) {
			for (int j = 0; j <= 27; j++) {
				const V2 c{float(i * 40.0), float(j * 40.0)};
				const vis::Membership m = vis::contains(r, vis::Pt{c.x, c.y});
				const std::optional<bool> dv = defined_visible(t, V2{float(P.x), float(P.y)}, c, plan, Flags());
				if (m == vis::Membership::Unclassified || !dv) continue;
				const bool in = m == vis::Membership::Inside;
				const char *col = in == *dv ? (in ? "#0f0" : "#555") : (in ? "#f0f" : "#f00");
				std::fprintf(f, "<circle cx='%g' cy='%g' r='%g' fill='%s'/>\n", c.x, c.y, in == *dv ? 2.5 : 6.0, col);
			}
		}
		std::fprintf(f, "<circle cx='%g' cy='%g' r='8' fill='yellow'/>\n", P.x, P.y);
		std::fprintf(f, "</svg>\n");
		std::fclose(f);
	}
}

TEST_CASE("visibility: the regions' structure checks hold on the shipped levels") {
	const std::string dir = std::string(RC_REPO_ROOT) + "/bench/scenes";
	std::vector<std::string> files;
	for (const auto &f : std::filesystem::directory_iterator(dir)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("test_levels__", 0) == 0 || name.rfind("playground__", 0) == 0) {
			files.push_back(f.path().string());
		}
	}
	std::sort(files.begin(), files.end());
	std::map<std::string, int> by_name;
	std::map<std::string, std::string> per_region; // label -> kinds
	std::vector<std::string> samples;
	int regions = 0;
	for (size_t fi = 0; fi < files.size(); fi++) {
		if (sampled_out(fi)) {
			continue;
		}
		const std::string &file = files[fi];
		const ojson d = load_dump(file);
		const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs);
		const std::string name = std::filesystem::path(file).filename().string();
		std::vector<std::vector<PlanItem>> plans{{}};
		const std::vector<std::vector<PlanItem>> lp = level_plans(defs);
		for (size_t i = 0; i < lp.size(); i += full() ? 1 : 6) {
			plans.push_back(lp[i]);
		}
		for (const std::vector<PlanItem> &plan : plans) {
			const vis::Pt player{500, 300};
			const vis::Result r = vis::compute(t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
			if (!r.supported) {
				continue;
			}
			regions++;
			inv::VisInput in{&r, &t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080}};
			for (const inv::Violation &vi : inv::check_visibility(in)) {
				const std::string kind = vi.fmt.substr(0, vi.fmt.find(':'));
				by_name[kind]++;
				std::string label = name + " plan";
				for (const PlanItem &p : plan) {
					label += " " + std::to_string(p.surface_id) + (p.left ? "L" : "R");
				}
				if (per_region[label].find(kind) == std::string::npos) {
					per_region[label] += kind + " ";
				}
				if (samples.size() < 40) {
					samples.push_back(label + ": " + render(vi));
				}
			}
		}
	}
	for (const std::string &s : samples) {
		MESSAGE(s);
	}
	for (const auto &kv : per_region) {
		const std::string line = kv.first + ": " + kv.second;
		MESSAGE(line);
	}
	std::string summary = "regions " + std::to_string(regions);
	for (const auto &kv : by_name) {
		summary += ", " + kv.first + " " + std::to_string(kv.second);
	}
	MESSAGE(summary);
	CHECK(by_name.empty());
}

// RC_VIS_BENCH=1: the time one region takes on the main level, by plan.
TEST_CASE("visibility bench" * doctest::skip(true)) {
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/main.surfaces.json");
	const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
	Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(defs);
	std::vector<std::vector<PlanItem>> plans{{}};
	const auto lp = level_plans(defs);
	for (size_t i = 0; i < lp.size() && plans.size() < 4; i++) {
		plans.push_back(lp[i]);
	}
	for (const auto &plan : plans) {
		std::vector<double> ms;
		int casts = 0;
		for (int k = 0; k < 12; k++) {
			const vis::Pt P{400.0 + k * 37.0, 300.0 + k * 21.0};
			const auto t0 = std::chrono::steady_clock::now();
			const vis::Result r = vis::compute(t, P, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
			ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
			casts += r.casts;
		}
		std::sort(ms.begin(), ms.end());
		std::printf("plan of %zu entries: median %.1f ms, max %.1f ms, casts per region %d\n", plan.size(), ms[ms.size() / 2], ms.back(), casts / 12);
	}
}
