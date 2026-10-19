// Stage 110: the visibility region (GAME_SPEC 15) against its definition -- a
// cursor is inside iff the preview's merged steps before it are ALIGNED and it
// is reached -- on the shipped levels' scene dumps.
#include "support/json_defs.hpp"
#include "support/violations.hpp"
#include "support/png_debug.hpp"
#include "rc/sweep.hpp"
#include "rc/earcut.hpp"
#include "rc/visibility.hpp"

#include <doctest/doctest.h>

#include <bit>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

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

// RC_VIS_PART=k/n (ctest's rc_tests_visibility_levels_k): a level loop takes
// every n-th of its regions from the k-th on, so one costly level spreads over
// the cores too. Unset: every region.
struct VisPart {
	size_t k = 0;
	size_t n = 1;
	size_t next = 0;
	VisPart() {
		if (const char *p = std::getenv("RC_VIS_PART")) {
			if (std::sscanf(p, "%zu/%zu", &k, &n) != 2 || n == 0) {
				k = 0;
				n = 1;
			}
		}
	}
	bool take() { return next++ % n == k; }
};

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
	return inv::preview_visible(dl, sweep::merge(dl.steps, dp.steps, dl.cursor_index), pair->cursor_point_agrees);
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
		const std::vector<PlanItem> plan = plan_from_json(c["plan"]); // (Stage 113: with the clicks)
		const V2 player{float(c["player"][0].get<double>()), float(c["player"][1].get<double>())};
		const vis::Result r = vis::compute(t, vis::Pt{player.x, player.y}, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
		// Stage 113: every case builds its region (the projective plans included)
		REQUIRE_MESSAGE(r.supported, name << ": " << r.reason);
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

// B9 (Stage 113): a directional normal along its line carrier is rejected at
// load (GAME_SPEC 23); the region of such a scene (a kept dump) is supported and
// empty: no beam leaves the line, nothing is visible through it.
TEST_CASE("visibility: a directional normal along its line carrier is a supported empty region") {
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/fixtures/stage113_degenerate_directional.json");
	Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(defs_from_json(d["surfaces"]));
	for (const bool left : {true, false}) {
		const std::vector<PlanItem> plan{PlanItem{5, left, std::nullopt, std::nullopt}};
		const vis::Result r = vis::compute(t, vis::Pt{760, 400}, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
		REQUIRE_MESSAGE(r.supported, r.reason);
		CHECK(r.cells.empty());
		for (const vis::Pt c : {vis::Pt{900, 400}, vis::Pt{750, 200}, vis::Pt{700, 500}, vis::Pt{1300, 700}}) {
			CHECK(vis::contains(r, c) == vis::Membership::Outside);
		}
		const inv::VisInput in{&r, &t, vis::Pt{760, 400}, plan, Flags(), vis::Rect{0, 0, 1920, 1080}};
		CHECK(inv::check_visibility(in).empty());
	}
}

// K15 (open, found by the projective_concentric playground, 2026-10-19): after a
// circle normal fires, the beam runs through the circle's centre by provenance;
// an inversion centred on that same point maps the line to itself (the centre
// becoming the frame's image of infinity). The walker inverts the rounded beam
// instead and gets a circle of enormous radius whose wrap overflows, so the solve
// returns nothing where the oracle traces (the oracle, at 50 digits, gets away
// with it). The fix is provenance: the exit beam knows it passes the centre, and
// an inversion centred there keeps the line (the oracle first). Expected to fail
// until then (doctest may_fail).
TEST_CASE("K15: an inversion centred on the circle normal's centre, on the beam through it" * doctest::may_fail(true)) {
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/playground__projective_concentric.surfaces.json");
	Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(defs_from_json(d["surfaces"]));
	const std::vector<PlanItem> plan{PlanItem{7, true, std::nullopt, std::nullopt}, PlanItem{6, true, std::nullopt, std::nullopt},
		PlanItem{5, true, std::nullopt, std::nullopt}};
	const std::optional<LadderResult> lr = t.solve(1100, 700, 960, 840, plan, Flags());
	CHECK_MESSAGE(lr.has_value(), "the planned walk through the real infinity meets the inversion on the beam through its centre");
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

using rc_test::render;

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
	// Stage 113: every projective side as a plan of its own, and the first two
	// projectives as one plan (after the pairs: the digest's indices of the
	// plans above stay)
	std::vector<PlanItem> projs;
	for (const SurfaceDef &d : defs) {
		if (d.pre_transform) {
			continue;
		}
		for (const bool left : {true, false}) {
			const std::optional<SideDef> &side = left ? d.left : d.right;
			if (side && is_projective(side->type)) {
				projs.push_back(PlanItem{d.id, left, std::nullopt, std::nullopt});
			}
		}
	}
	for (const PlanItem &p : projs) {
		plans.push_back({p});
	}
	if (projs.size() >= 3) {
		plans.push_back({projs[0], projs[2]});
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
	VisPart part;
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
				if (!part.take()) {
					continue;
				}
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
	// RC_VIS_EMIT=0: no infinity events (as a sweep manifest without them); RC_VIS_BOUNDS="x0,y0,x1,y1": the level bounds
	t.set_emit_infinity(std::getenv("RC_VIS_EMIT") == nullptr || std::string(std::getenv("RC_VIS_EMIT")) != "0");
	t.set_defs(defs_from_json(d["surfaces"]));
	vis::Rect vbounds{0, 0, 1920, 1080};
	if (const char *pb = std::getenv("RC_VIS_BOUNDS")) {
		std::sscanf(pb, "%lf,%lf,%lf,%lf", &vbounds.x0, &vbounds.y0, &vbounds.x1, &vbounds.y1);
	}
	std::vector<PlanItem> plan;
	if (const char *ps = std::getenv("RC_VIS_PLAN")) {
		std::string str = ps;
		size_t pos = 0;
		while (pos < str.size()) {
			size_t comma = str.find(',', pos);
			if (comma == std::string::npos) comma = str.size();
			const std::string tok = str.substr(pos, comma - pos);
			if (!tok.empty()) {
				// "5L" or "5L@650;370" (the click, Stage 114)
				const size_t at = tok.find('@');
				const std::string head = at == std::string::npos ? tok : tok.substr(0, at);
				PlanItem item{std::stoi(head.substr(0, head.size() - 1)), head.back() == 'L', std::nullopt, std::nullopt};
				if (at != std::string::npos) {
					float hx = 0.0f;
					float hy = 0.0f;
					std::sscanf(tok.c_str() + at + 1, "%f;%f", &hx, &hy);
					item.hint = effects::F32Point{hx, hy};
				}
				plan.push_back(item);
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
		// RC_VIS_HALF=1|2: the cast in that forced orientation (Stage 111)
		const char *ph = std::getenv("RC_VIS_HALF");
		vis::Builder b(t, P, plan, Flags(), vbounds, ph ? vis::Half(std::atoi(ph)) : vis::Half::Single);
		const std::optional<vis::Cast> k = b.probe(c);
		const vis::Result &rr = b.result();
		std::printf("P_K (%.3f,%.3f) Q_K %s (%.3f,%.3f)\n", rr.pk.x, rr.pk.y, rr.qk_inf ? "inf" : "", rr.qk.x, rr.qk.y);
		if (k) {
			std::printf("cast: %s cursor_ok %d met %d ek %d chosen %d tie %d near (%.3f,%.3f) end (%.3f,%.3f)\n", k->shape().c_str(), int(k->cursor_ok), int(k->met), k->ek, int(k->chosen), int(k->has_tie), k->near_pt.x, k->near_pt.y,
				k->end_pt.x, k->end_pt.y);
			for (const auto &iv : k->iv) std::printf("  interval %s %.6g -> %s %.6g\n", vis::end_name(iv.a.kind), iv.a.t, vis::end_name(iv.b.kind), iv.b.t);
		}
		std::optional<LadderResult> lr = t.solve(P.x, P.y, c.x, c.y, plan, Flags());
		if (!lr) {
			// the solve filtered the result (empty or non-finite): the raw ladder, for the look
			std::printf("solve: none (the raw ladder follows)\n");
			LadderContext ctx(defs_from_json(d["surfaces"]));
			Ladder ladder(ctx);
			lr = ladder.solve(P.x, P.y, c.x, c.y, plan, MAX_HITS, MAX_HITS, TRACE_TOL, false, Flags(), std::nullopt);
		}
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
	const vis::Result r = vis::compute(t, P, plan, Flags(), vbounds);
	std::printf("supported %d %s casts %d refinements %d members %zu cells %zu loops %zu\n", int(r.supported), r.reason.c_str(), r.casts,
		r.refinements, r.members.size(), r.cells.size(), r.loops.size());
	for (const std::string &n : r.notes) std::printf("note: %s\n", n.c_str());
	{
		inv::VisInput in{&r, &t, P, plan, Flags(), vbounds};
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
		// the boundary edge at a mismatch on the boundary, with its member's walk data
		for (const std::string &sm : st.samples) {
			double cx, cy;
			const char *at = std::strstr(sm.c_str(), "cursor (");
			if (at == nullptr || std::sscanf(at, "cursor (%lf, %lf)", &cx, &cy) != 2) continue;
			const vis::Pt c{cx, cy};
			if (vis::margin(r, c) > 1e-6) continue;
			for (size_t li = 0; li < r.loops.size(); li++) {
				for (size_t ei = 0; ei < r.loops[li].edges.size(); ei++) {
					const vis::Edge &e = r.loops[li].edges[ei];
					if (vis::edge_dist(e, c) > 1e-6) continue;
					std::printf("  at (%g,%g): loop %zu edge %zu kind %d m%d s%d %s ta %.6g tb %.6g incl %d incl_a %d crack %d\n", cx, cy, li, ei, int(e.kind), e.member,
						e.surface, vis::end_name(e.role), e.ta, e.tb, int(e.inclusive), int(e.incl_a), int(e.crack));
					if (e.member >= 0) {
						const vis::Member &m = r.members[size_t(e.member)];
						std::printf("    member %d key %.9g t0 %.6g tq %.6g tp %.6g own_a %d crit %s near_pt (%.3f,%.3f) beam p (%.3f,%.3f) tau (%.4f,%.4f) kappa %.6g\n", e.member, m.key, m.t0, m.tq, m.tp, int(m.own_a), m.crit.shape().c_str(),
							m.crit.near_pt.x, m.crit.near_pt.y, m.beam.p.fx(), m.beam.p.fy(), m.beam.tau.fx(), m.beam.tau.fy(), m.beam.kappa.v);
						const Scene<F64> sc = Scene<F64>::from_defs(t.defs(), F64{1.0});
						if (const Surf<F64> *ns = sc.find(m.crit.near_sid)) {
							for (const F64 &rt : geom::hits(m.beam, ns->seg, false)) {
								const CN<F64> q = m.beam.at(rt);
								std::printf("      root on %d: t %.6g at (%.3f,%.3f)\n", m.crit.near_sid, rt.v, q.fx(), q.fy());
							}
						}
						for (const auto &iv : m.crit.iv) std::printf("      interval %s %.6g -> %s %.6g\n", vis::end_name(iv.a.kind), iv.a.t, vis::end_name(iv.b.kind), iv.b.t);
					}
				}
			}
		}
		std::printf("pin: %d cursors, %d mismatches\n", st.checked, st.mismatches);
	}
	for (size_t i = 0; i < r.members.size() && std::getenv("RC_VIS_PIN") == nullptr; i++) {
		const auto &m = r.members[i];
		std::printf("member %zu dir(%.6f,%.6f) key %.9g sub %g sigma %g %s cursor(%.3f,%.3f) beam p(%.3f,%.3f) tau(%.4f,%.4f) t0 %.4g ok %d chosen %d tie %d\n  crit %s\n  minus %s\n  plus %s\n", i, m.dir.x, m.dir.y, m.key, m.sub, m.sigma, m.why.c_str(), m.cursor.x,
			m.cursor.y, m.beam.p.fx(), m.beam.p.fy(), m.beam.tau.fx(), m.beam.tau.fy(), m.t0, int(m.crit.cursor_ok), int(m.crit.chosen), int(m.crit.has_tie), m.crit.shape().c_str(), m.minus.shape().c_str(), m.plus.shape().c_str());
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
			std::printf("cell %zu members %d-%d half %d wind %d loops %zu mid %s cursor (%.3f,%.3f) key %.9g chosen %d near (%.3f,%.3f) end (%.3f,%.3f)\n", ci, c.left, c.right, c.half, c.wind, c.loops.size(),
				c.mid.shape().c_str(), c.mid.cursor.x, c.mid.cursor.y, c.mid.key, int(c.mid.chosen), c.mid.near_pt.x, c.mid.near_pt.y, c.mid.end_pt.x, c.mid.end_pt.y);
			for (const auto &iv : c.mid.iv) std::printf("  interval %s %.6g -> %s %.6g\n", vis::end_name(iv.a.kind), iv.a.t, vis::end_name(iv.b.kind), iv.b.t);
			for (const auto &e : c.edges()) {
				std::printf("  kind %d m%d s%d %s (%.3f,%.3f) via (%.3f,%.3f) -> (%.3f,%.3f) arc%d\n", int(e.kind), e.member, e.surface, vis::end_name(e.role), e.a.x,
					e.a.y, e.via.x, e.via.y, e.b.x, e.b.y, int(e.arc));
			}
		}
	}
	if (std::getenv("RC_VIS_FILL")) {
		// RC_VIS_FILL=1: the drawn fill -- each loop's clipped ring, and the triangles' area
		for (size_t li = 0; li < r.loops.size(); li++) {
			const std::vector<vis::Pt> ring = vis::clip_polygon(vis::polyline(r.loops[li].edges), r.domain);
			std::printf("ring %zu: %zu points, area %.3f, first (%.3f,%.3f)\n", li, ring.size(), vis::signed_area(ring), ring.empty() ? 0.0 : ring[0].x,
				ring.empty() ? 0.0 : ring[0].y);
			if (std::getenv("RC_VIS_FILL")[0] == '2') {
				for (const auto &q : ring) std::printf("  (%.6f,%.6f)\n", q.x, q.y);
			}
		}
		{
			// each hole with the largest ring alone, through earcut
			std::vector<std::vector<earcut::XY>> rs;
			size_t outer = 0;
			double big = 0.0;
			for (size_t li = 0; li < r.loops.size(); li++) {
				std::vector<earcut::XY> ring;
				for (const auto &q : vis::clip_polygon(vis::polyline(r.loops[li].edges), r.domain)) ring.push_back(earcut::XY{q.x, q.y});
				const double ar = std::abs(vis::signed_area(vis::clip_polygon(vis::polyline(r.loops[li].edges), r.domain)));
				if (ar > big) { big = ar; outer = li; }
				rs.push_back(ring);
			}
			auto tri_area = [](const std::vector<std::vector<earcut::XY>> &g) {
				std::vector<earcut::XY> pts;
				for (const auto &x : g) pts.insert(pts.end(), x.begin(), x.end());
				earcut::Triangulator tr;
				const auto idx = tr.run(g);
				double sum = 0.0;
				for (size_t k = 0; k + 2 < idx.size(); k += 3) {
					const auto &p0 = pts[idx[k]], &p1 = pts[idx[k + 1]], &p2 = pts[idx[k + 2]];
					sum += std::abs((p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x)) / 2.0;
				}
				return sum;
			};
			std::printf("earcut outer %zu alone: %.3f\n", outer, tri_area({rs[outer]}));
			for (size_t li = 0; li < r.loops.size(); li++) {
				const std::vector<vis::Pt> ring = vis::clip_polygon(vis::polyline(r.loops[li].edges), r.domain);
				std::vector<std::vector<vis::Pt>> parts;
				const double a0 = vis::signed_area(ring);
				vis::simple_rings(ring, a0 > 0 ? 1.0 : -1.0, parts);
				std::printf("simple_rings %zu: %zu parts:", li, parts.size());
				for (const auto &pp : parts) std::printf(" %.3f", vis::signed_area(pp));
				std::printf("\n");
			}
			for (size_t li = 0; li < rs.size(); li++) {
				if (li != outer) std::printf("earcut outer %zu with hole %zu: %.3f\n", outer, li, tri_area({rs[outer], rs[li]}));
			}
			std::vector<std::vector<earcut::XY>> all{rs[outer]};
			for (size_t li = 0; li < rs.size(); li++) if (li != outer) all.push_back(rs[li]);
			std::printf("earcut outer %zu with every other ring: %.3f\n", outer, tri_area(all));
			if (const char *g = std::getenv("RC_VIS_FILL_GROUP")) {
				// RC_VIS_FILL_GROUP="0,1,2": these rings, the first the outer
				std::vector<std::vector<earcut::XY>> grp;
				for (const char *c = g; *c; c++) if (*c >= '0' && *c <= '9') grp.push_back(rs[size_t(*c - '0')]);
				std::printf("earcut group %s: %.3f\n", g, tri_area(grp));
			}
			// nesting as the fill computes it (middle of the longest side)
			std::vector<std::vector<vis::Pt>> vr;
			for (size_t li = 0; li < r.loops.size(); li++) vr.push_back(vis::clip_polygon(vis::polyline(r.loops[li].edges), r.domain));
			for (size_t i = 0; i < vr.size(); i++) {
				size_t best = 0;
				double len = -1;
				for (size_t k = 0; k < vr[i].size(); k++) {
					const double dd = vis::norm(vr[i][(k + 1) % vr[i].size()] - vr[i][k]);
					if (dd > len) { len = dd; best = k; }
				}
				const vis::Pt rp = (vr[i][best] + vr[i][(best + 1) % vr[i].size()]) * 0.5;
				std::printf("ring %zu rep (%.3f,%.3f) inside:", i, rp.x, rp.y);
				for (size_t j = 0; j < vr.size(); j++) if (j != i && vis::in_ring(vr[j], rp)) std::printf(" %zu", j);
				std::printf("\n");
			}
		}
		const vis::Fill f = vis::fill_triangles(r);
		double a = 0.0;
		int neg = 0, pos = 0;
		for (size_t k = 0; k + 2 < f.indices.size(); k += 3) {
			const double c = vis::cross(f.points[f.indices[k + 1]] - f.points[f.indices[k]], f.points[f.indices[k + 2]] - f.points[f.indices[k]]) / 2.0;
			a += std::abs(c);
			(c < 0 ? neg : pos)++;
		}
		std::printf("fill: %zu points, %zu triangles (%d one way, %d the other), area %.3f\n", f.points.size(), f.indices.size() / 3, pos, neg, a);
		if (const char *cs = std::getenv("RC_VIS_FILL_CELL")) {
			// RC_VIS_FILL_CELL=i: that cell's conforming ring and its simple parts
			std::map<vis::fill_detail::GeomKey, std::vector<vis::Pt>> corners;
			for (const auto &c : r.cells) for (const auto &l : c.loops) for (const auto &e : l) {
				const auto k = vis::fill_detail::key_of(e);
				if (k.kind != 0) { corners[k].push_back(e.a); corners[k].push_back(e.b); }
			}
			const auto &cell = r.cells[size_t(std::atoi(cs))];
			for (const auto &l : cell.loops) {
				std::vector<vis::Pt> poly;
				for (const auto &e : l) vis::fill_detail::conforming(e, corners, poly);
				const std::vector<vis::Pt> ring = vis::clip_polygon(poly, r.domain);
				std::vector<std::vector<vis::Pt>> parts;
				const double a0 = vis::signed_area(ring);
				vis::simple_rings(ring, a0 > 0 ? 1.0 : -1.0, parts);
				if (std::getenv("RC_VIS_FILL_DUMP")) {
					std::printf("conforming:");
					for (const auto &q : ring) if (q.y > 870) std::printf(" (%.2f,%.2f)", q.x, q.y);
					std::printf("\nplain:");
					for (const auto &q : vis::clip_polygon(vis::polyline(l), r.domain)) if (q.y > 870) std::printf(" (%.2f,%.2f)", q.x, q.y);
					std::printf("\n");
				}
				std::printf("cell ring: %zu points, area %.3f (plain chords %.3f); %zu parts:", ring.size(), a0,
					vis::signed_area(vis::clip_polygon(vis::polyline(l), r.domain)), parts.size());
				for (const auto &pp : parts) std::printf(" %.3f", vis::signed_area(pp));
				std::printf("\n");
				for (const auto &pp : parts) {
					std::vector<earcut::XY> xy;
					for (const auto &q : pp) xy.push_back(earcut::XY{q.x, q.y});
					earcut::Triangulator tr;
					const auto idx = tr.run({xy});
					double sum = 0.0;
					for (size_t k = 0; k + 2 < idx.size(); k += 3) {
						const auto &p0 = xy[idx[k]], &p1 = xy[idx[k + 1]], &p2 = xy[idx[k + 2]];
						sum += std::abs((p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x)) / 2.0;
					}
					std::printf("  earcut: %zu triangles, area %.3f\n", idx.size() / 3, sum);
					if (const char *at2 = std::getenv("RC_VIS_FILL_AT")) {
						vis::Pt q;
						std::sscanf(at2, "%lf,%lf", &q.x, &q.y);
						int cov = 0;
						for (size_t k = 0; k + 2 < idx.size(); k += 3) {
							const vis::Pt p0{xy[idx[k]].x, xy[idx[k]].y}, p1{xy[idx[k + 1]].x, xy[idx[k + 1]].y}, p2{xy[idx[k + 2]].x, xy[idx[k + 2]].y};
							const double d1 = vis::cross(p1 - p0, q - p0), d2 = vis::cross(p2 - p1, q - p1), d3 = vis::cross(p0 - p2, q - p2);
							cov += ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0)) ? 1 : 0;
						}
						std::printf("  the conforming ring contains it: %d; its triangles cover it %d times\n", int(vis::in_ring(pp, q)), cov);
					}
					if (std::getenv("RC_VIS_FILL_DUMP")) for (const auto &q : pp) std::printf("    %.17g %.17g\n", q.x, q.y);
				}
			}
		}
		if (const char *at = std::getenv("RC_VIS_FILL_AT")) {
			// RC_VIS_FILL_AT=x,y: the cells covering that point, and their rings' areas
			vis::Pt q;
			std::sscanf(at, "%lf,%lf", &q.x, &q.y);
			int cover = 0;
			for (size_t k = 0; k + 2 < f.indices.size(); k += 3) {
				const vis::Pt &p0 = f.points[f.indices[k]], &p1 = f.points[f.indices[k + 1]], &p2 = f.points[f.indices[k + 2]];
				const double d1 = vis::cross(p1 - p0, q - p0), d2 = vis::cross(p2 - p1, q - p1), d3 = vis::cross(p0 - p2, q - p2);
				if ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0)) {
					cover++;
					std::printf("covered by triangle %zu: (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)\n", k / 3, p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
				}
			}
			std::printf("fill covers (%.3f,%.3f) %d times; contains %d\n", q.x, q.y, cover, int(vis::contains(r, q)));
			for (size_t ci = 0; ci < r.cells.size(); ci++) {
				for (const auto &l : r.cells[ci].loops) {
					const std::vector<vis::Pt> poly = vis::clip_polygon(vis::polyline(l), r.domain);
					if (vis::in_ring(poly, q)) {
						std::printf("cell %zu covers it: ring area %.3f, %zu points, %zu edges:\n", ci, vis::signed_area(poly), poly.size(), l.size());
						for (const auto &e : l) {
							std::printf("  kind %d m%d s%d (%.4f,%.4f) via (%.4f,%.4f) -> (%.4f,%.4f) arc%d gline%d gr %.17g gc (%.17g,%.17g)\n", int(e.kind), e.member, e.surface,
								e.a.x, e.a.y, e.via.x, e.via.y, e.b.x, e.b.y, int(e.arc), int(e.gline), e.gr, e.gc.x, e.gc.y);
						}
					}
				}
			}
		}
	}
	if (std::getenv("RC_VIS_POLYS")) {
		// RC_VIS_POLYS=1: every cell's drawn polygon (what VisibilityRegion.cell_polygons hands the renderer)
		for (size_t ci = 0; ci < r.cells.size(); ci++) {
			for (const auto &l : r.cells[ci].loops) {
				const std::vector<vis::Pt> poly = vis::clip_polygon(vis::polyline(l), r.domain);
				std::printf("poly cell %zu: %zu points, area %.3f:", ci, poly.size(), vis::signed_area(poly));
				for (const auto &q : poly) std::printf(" (%.3f,%.3f)", q.x, q.y);
				std::printf("\n");
			}
		}
	}
	if (const char *near = std::getenv("RC_VIS_NEAR")) {
		vis::Pt c;
		std::sscanf(near, "%lf,%lf", &c.x, &c.y);
		std::printf("contains(%.3f,%.3f) = %d, margin %.3g\n", c.x, c.y, int(vis::contains(r, c)), vis::margin(r, c));
		{
			// the definition at this cursor, the preview's steps and the cursor-relative invariants
			const std::optional<bool> dv = defined_visible(t, V2{float(P.x), float(P.y)}, V2{float(c.x), float(c.y)}, plan, Flags());
			std::printf("defined visible: %s\n", dv ? (*dv ? "yes" : "no") : "unclassified");
			if (const std::optional<TracePair> pr = t.trace_pair(V2{float(P.x), float(P.y)}, V2{float(c.x), float(c.y)}, plan, Flags())) {
				const std::vector<inv::SurfaceInfo> none;
				const TracePath dp = sweep::prepare_for_display(pr->physical, Rect2f::of(0, 0, 1920, 1080), none);
				const TracePath dl = sweep::prepare_for_display(pr->planned, Rect2f::of(0, 0, 1920, 1080), none);
				const std::vector<TraceStep> ty = sweep::merge(dl.steps, dp.steps, dl.cursor_index);
				for (const auto &[which, tp] : {std::pair<const char *, const TracePath *>{"planned", &dl}, {"physical", &dp}}) {
					std::printf("%s steps %zu cursor_index %d:", which, tp->steps.size(), tp->cursor_index);
					for (const TraceStep &st : tp->steps) std::printf(" [(%g,%g)->(%g,%g) f%d]", st.start.x, st.start.y, st.end.x, st.end.y, st.frame_id);
					std::printf("\n");
				}
				inv::Input in;
				in.has_renderer = true;
				in.player = V2{float(P.x), float(P.y)};
				in.cursor = V2{float(c.x), float(c.y)};
				in.typed = &ty;
				in.physical = &dp;
				in.planned = &dl;
				for (const inv::Violation &vi : inv::check_NO_POST_CURSOR_DIVERGENCE(in)) std::printf("invariant: %s\n", render(vi).c_str());
				for (const inv::Violation &vi : inv::check_PHYSICAL_PREVIEW_MATCH(in)) std::printf("invariant: %s\n", render(vi).c_str());
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
			std::printf("  kind %d m%d s%d %s (%.3f,%.3f) via (%.3f,%.3f) ->(%.3f,%.3f) arc%d incl%d crack%d\n", int(e.kind), e.member, e.surface, vis::end_name(e.role), e.a.x, e.a.y, e.via.x, e.via.y,
				e.b.x, e.b.y, int(e.arc), int(e.inclusive), int(e.crack));
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
	VisPart part;
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
			if (!part.take()) {
				continue;
			}
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
// The region's cost (skipped; run with --no-skip): RC_VIS_SCENE=<dump name>
// (default main), RC_VIS_BENCH_PLANS=<n> plans after the empty one (default 3),
// 12 players. Per plan: build time (median, p95, max), casts per region by kind,
// and the ladder's census over those casts (walks accepted at float64, declined
// for a margin or for the estimate, BigFloat walks per tier, their time).
TEST_CASE("visibility bench" * doctest::skip(true)) {
	const char *sc = std::getenv("RC_VIS_SCENE");
	const std::string scene = sc ? sc : "main";
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/" + scene + ".surfaces.json");
	const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
	Tracer t;
	t.set_emit_infinity(true);
	t.set_defs(defs);
	const char *np = std::getenv("RC_VIS_BENCH_PLANS");
	const size_t n_plans = np ? size_t(std::atoi(np)) : 3;
	std::vector<std::vector<PlanItem>> plans{{}};
	const auto lp = level_plans(defs);
	for (size_t i = 0; i < lp.size() && plans.size() < n_plans + 1; i++) {
		plans.push_back(lp[(i * 7) % lp.size()]);
	}
	std::printf("%s: %zu surfaces\n", scene.c_str(), defs.size());
	for (const auto &plan : plans) {
		std::vector<double> ms;
		std::array<long long, 6> kinds{};
		t.reset_ladder_stats();
		for (int k = 0; k < 12; k++) {
			const vis::Pt P{400.0 + k * 97.0, 260.0 + k * 53.0};
			const vis::Result r = vis::compute(t, P, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
			ms.push_back(double(r.build_usec) / 1000.0);
			for (size_t q = 0; q < kinds.size(); q++) kinds[q] += r.casts_by_kind[q];
		}
		const LadderStats st = t.ladder_stats();
		std::sort(ms.begin(), ms.end());
		std::string pl;
		for (const PlanItem &it : plan) pl += (pl.empty() ? "" : ",") + std::to_string(it.surface_id) + (it.left ? "L" : "R");
		long long casts = 0;
		for (long long c : kinds) casts += c;
		std::printf("plan [%s]: median %.1f ms, p95 %.1f, max %.1f; casts/region %lld (crit %lld side %lld mid %lld refine %lld probe %lld)\n",
			pl.c_str(), ms[ms.size() / 2], ms[size_t(double(ms.size()) * 0.95) < ms.size() ? size_t(double(ms.size()) * 0.95) : ms.size() - 1], ms.back(),
			casts / 12, kinds[0] / 12, kinds[1] / 12, kinds[2] / 12, kinds[3] / 12, kinds[4] / 12);
		std::printf("  ladder: walks %lld, f64 %lld, declined margin %lld, estimate %lld; BigFloat walks 4:%lld 8:%lld 16:%lld 32:%lld 64:%lld, %.1f ms\n",
			st.walks, st.f64, st.unreliable, st.over_tol, st.tier_solves[1], st.tier_solves[2], st.tier_solves[3], st.tier_solves[4], st.tier_solves[5],
			double(st.big_usec) / 1000.0);
	}
}

namespace {

// RC_GOLDEN_REBLESS=1: a pinned value that differs is printed as
// "REBLESS-JSON\t<golden file>\t<key>\t<field path>\t<json>" for
// docs/research/apply_rebless.py (as test_goldens.cpp prints).
void rebless_line(const char *file, const std::string &key, const std::string &path, const std::string &got_json, const std::string &want_json) {
	if (got_json != want_json && std::getenv("RC_GOLDEN_REBLESS")) {
		std::printf("REBLESS-JSON\t%s\t%s\t%s\t%s\n", file, key.c_str(), path.c_str(), got_json.c_str());
	}
}

// The region digest's set: every test level and playground dump, the empty
// plan, every 4th of the level's plans and the last one, players (500, 300)
// and (960, 540); a region's hash covers its exact edge bits, flags, whiskers
// and cell count. Pinned in bench/goldens/regions.json (Workflow W4).
std::vector<std::string> digest_files() {
	const std::string dir = std::string(RC_REPO_ROOT) + "/bench/scenes";
	std::vector<std::string> files;
	for (const auto &f : std::filesystem::directory_iterator(dir)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("test_levels__", 0) == 0 || name.rfind("playground__", 0) == 0) {
			files.push_back(f.path().string());
		}
	}
	std::sort(files.begin(), files.end());
	return files;
}

uint64_t digest_mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }

uint64_t region_hash(const vis::Result &r) {
	auto bits = [](double x) { return std::bit_cast<uint64_t>(x); };
	auto edge_hash = [&](uint64_t h, const vis::Edge &e) {
		for (double x : {e.a.x, e.a.y, e.via.x, e.via.y, e.b.x, e.b.y}) h = digest_mix(h, bits(x));
		h = digest_mix(h, uint64_t(int(e.kind)) | uint64_t(uint32_t(e.member)) << 8 | uint64_t(uint32_t(e.surface)) << 40);
		return digest_mix(h, uint64_t(e.inclusive) | uint64_t(e.incl_a) << 1 | uint64_t(e.arc) << 2);
	};
	uint64_t h = digest_mix(14695981039346656037ULL, uint64_t(r.supported));
	h = digest_mix(h, r.cells.size());
	for (const vis::Loop &l : r.loops) {
		h = digest_mix(h, l.edges.size());
		for (const vis::Edge &e : l.edges) h = edge_hash(h, e);
	}
	for (const vis::Edge &e : r.whiskers) h = edge_hash(h, e);
	return h;
}

std::vector<std::vector<PlanItem>> digest_plans(const std::vector<SurfaceDef> &defs) {
	std::vector<std::vector<PlanItem>> plans{{}};
	const std::vector<std::vector<PlanItem>> lp = level_plans(defs);
	for (size_t i = 0; i < lp.size(); i++) {
		if (i % 4 == 0 || i + 1 == lp.size()) plans.push_back(lp[i]);
	}
	return plans;
}

std::string plan_label(const std::vector<PlanItem> &plan) {
	std::string ps;
	for (const PlanItem &it : plan) ps += (ps.empty() ? "" : ",") + std::to_string(it.surface_id) + (it.left ? "L" : "R");
	return ps;
}

std::string hex16(uint64_t h) {
	char b[17];
	std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
	return b;
}

} // namespace

// The regions' digest as a golden: bench/goldens/regions.json pins every
// region of the digest's set by its hash (dump name -> "plan|player" ->
// hash). A changed region is a behaviour change: RC_GOLDEN_REBLESS=1 prints
// the REBLESS-JSON lines for apply_rebless.py (after the user approved the
// judged report); RC_REGIONS_WRITE=<path> writes the whole golden (a new
// level). RC_VIS_PART parts (ctest's rc_tests_regions_k).
TEST_CASE("visibility: the regions' digest matches bench/goldens/regions.json") {
	const char *write = std::getenv("RC_REGIONS_WRITE");
	ojson gold;
	if (write == nullptr) {
		gold = load_dump(std::string(RC_REPO_ROOT) + "/bench/goldens/regions.json");
	}
	ojson out;
	out["_schema"] = {{"generator", "native/tests/test_visibility.cpp (RC_REGIONS_WRITE)"},
		{"what", "the region digest's set: hash per dump, plan and player; see the visibility digest case"}};
	VisPart part;
	int regions = 0, changed = 0;
	for (const std::string &file : digest_files()) {
		const ojson d = load_dump(file);
		const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs);
		const std::string name = std::filesystem::path(file).filename().string();
		ojson &level = out[name];
		level = ojson::object();
		for (const std::vector<PlanItem> &plan : digest_plans(defs)) {
			for (const vis::Pt player : {vis::Pt{500, 300}, vis::Pt{960, 540}}) {
				if (!part.take()) {
					continue;
				}
				const std::string key = "[" + plan_label(plan) + "]|" + std::to_string(int(player.x)) + "," + std::to_string(int(player.y));
				const vis::Result r = vis::compute(t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
				const std::string h = hex16(region_hash(r));
				level[key] = h;
				regions++;
				if (write != nullptr) {
					continue;
				}
				const std::string want = gold.contains(name) && gold[name].contains(key) ? gold[name][key].get<std::string>() : "";
				if (h != want) {
					changed++;
					MESSAGE(name << " " << key << ": " << h << " (golden " << (want.empty() ? "none" : want) << ")");
					rebless_line("regions", name, key, ojson(h).dump(), ojson(want).dump());
				}
			}
		}
	}
	if (write != nullptr) {
		REQUIRE_MESSAGE(part.n == 1, "RC_REGIONS_WRITE needs the whole set (no RC_VIS_PART)");
		std::ofstream f(write);
		f << out.dump();
		MESSAGE("wrote " << regions << " regions to " << std::string(write));
		return;
	}
	MESSAGE("regions " << regions << ", changed " << changed << " (part " << part.k << "/" << part.n << ")");
	CHECK(changed == 0);
}

// A digest of the regions on the shipped levels (skipped; run with --no-skip):
// a hash of every region's loops, whiskers and cell count (exact bits), per
// level and in total, for the empty plan, every fourth plan and the infinity
// plan, players (500, 300) and (960, 540). Optimizations must leave it
// unchanged (docs/TDD_15_PERFORMANCE.md). RC_VIS_DIGEST_VERBOSE=1: per region.
TEST_CASE("visibility digest" * doctest::skip(true)) {
	const bool verbose = std::getenv("RC_VIS_DIGEST_VERBOSE") != nullptr;
	uint64_t total = 14695981039346656037ULL;
	int regions = 0;
	for (const std::string &file : digest_files()) {
		const ojson d = load_dump(file);
		const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs);
		const std::vector<std::vector<PlanItem>> plans = digest_plans(defs);
		uint64_t fh = 14695981039346656037ULL;
		const std::string name = std::filesystem::path(file).filename().string();
		for (size_t pi = 0; pi < plans.size(); pi++) {
			for (const vis::Pt player : {vis::Pt{500, 300}, vis::Pt{960, 540}}) {
				const vis::Result r = vis::compute(t, player, plans[pi], Flags(), vis::Rect{0, 0, 1920, 1080});
				const uint64_t h = region_hash(r);
				regions++;
				fh = digest_mix(fh, h);
				if (verbose) {
					std::printf("%s plan %zu [%s] player (%g, %g): %s\n", name.c_str(), pi, plan_label(plans[pi]).c_str(), player.x, player.y, hex16(h).c_str());
				}
			}
		}
		std::printf("%-44s %s\n", name.c_str(), hex16(fh).c_str());
		total = digest_mix(total, fh);
	}
	std::printf("digest of %d regions: %s\n", regions, hex16(total).c_str());
}

// A census over the shipped levels (skipped; run with --no-skip): regions,
// whiskers on carriers and on members, and departure edges the region excludes
// (near_incl). VIS-REGULAR allows only member whiskers along a member tangent at
// a finite pole. RC_VIS_FULL=1 for every plan and two players.
TEST_CASE("visibility census" * doctest::skip(true)) {
	const std::string dir = std::string(RC_REPO_ROOT) + "/bench/scenes";
	std::vector<std::string> files;
	for (const auto &f : std::filesystem::directory_iterator(dir)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("test_levels__", 0) == 0 || name.rfind("playground__", 0) == 0) {
			files.push_back(f.path().string());
		}
	}
	std::sort(files.begin(), files.end());
	int regions = 0, with_whiskers = 0, carrier_whiskers = 0, member_whiskers = 0, near_edges = 0, near_excluded = 0, casts = 0;
	for (const std::string &file : files) {
		const ojson d = load_dump(file);
		const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
		Tracer t;
		t.set_emit_infinity(true);
		t.set_defs(defs);
		std::vector<std::vector<PlanItem>> plans{{}};
		const std::vector<std::vector<PlanItem>> lp = level_plans(defs);
		for (size_t i = 0; i < lp.size(); i += full() ? 1 : 4) {
			plans.push_back(lp[i]);
		}
		const std::vector<V2> players = full() ? std::vector<V2>{V2{960, 540}, V2{500, 300}} : std::vector<V2>{V2{500, 300}};
		for (const auto &plan : plans) {
			for (const V2 player : players) {
				const vis::Result r = vis::compute(t, vis::Pt{player.x, player.y}, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
				if (!r.supported) {
					continue;
				}
				regions++;
				casts += r.casts;
				for (const auto &w : r.whiskers) {
					(w.kind == vis::EdgeKind::Member ? member_whiskers : carrier_whiskers)++;
					if (w.kind != vis::EdgeKind::Member && carrier_whiskers <= 10) {
						std::string pl;
						for (const PlanItem &it : plan) pl += std::to_string(it.surface_id) + (it.left ? "L," : "R,");
						const std::string m = std::filesystem::path(file).filename().string() + " plan " + pl + " player " + std::to_string(player.x) + ","
							+ std::to_string(player.y) + ": a carrier whisker on " + std::to_string(w.surface);
						MESSAGE(m);
					}
				}
				with_whiskers += r.whiskers.empty() ? 0 : 1;
				for (const auto &c : r.cells) {
					for (const auto &e : c.edges()) {
						if (e.kind == vis::EdgeKind::Carrier && e.role == vis::End::Near) {
							near_edges++;
							near_excluded += e.inclusive ? 0 : 1;
						}
					}
				}
			}
		}
	}
	const std::string s = "regions " + std::to_string(regions) + ", casts " + std::to_string(casts) + ", with whiskers " + std::to_string(with_whiskers)
		+ " (carrier " + std::to_string(carrier_whiskers) + ", member " + std::to_string(member_whiskers) + "), departure edges "
		+ std::to_string(near_edges) + " (excluded " + std::to_string(near_excluded) + ")";
	MESSAGE(s);
}

namespace {
vis::Result region_of(Tracer &t, const char *scene, const std::vector<PlanItem> &plan, vis::Pt player) {
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/" + scene + ".surfaces.json");
	t.set_emit_infinity(true);
	t.set_defs(defs_from_json(d["surfaces"]));
	return vis::compute(t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
}

int count_kind(const inv::Violations &vs, const std::string &kind) {
	int n = 0;
	for (const inv::Violation &v : vs) {
		n += render(v).rfind(kind, 0) == 0 ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("visibility: VIS-REGULAR flags parts of no area") {
	// three_mirrors, plan [4 L]: mirror 4 spans x = 800, y 300..780
	Tracer t;
	const std::vector<PlanItem> plan{PlanItem{4, true, std::nullopt, std::nullopt}};
	vis::Result r = region_of(t, "test_levels__three_mirrors", plan, vis::Pt{960, 827.999});
	REQUIRE(r.supported);
	inv::VisInput in{&r, &t, vis::Pt{960, 827.999}, plan, Flags(), vis::Rect{0, 0, 1920, 1080}};
	CHECK(count_kind(inv::check_visibility(in), "VIS-REGULAR") == 0);
	// a departure whisker off the planned segment (the B4 report's thin lines)
	vis::Edge w;
	w.kind = vis::EdgeKind::Carrier;
	w.surface = 4;
	w.role = vis::End::Near;
	w.a = vis::Pt{800, 790};
	w.via = vis::Pt{800, 810};
	w.b = vis::Pt{800, 830};
	r.whiskers.push_back(w);
	CHECK(count_kind(inv::check_visibility(in), "VIS-REGULAR") == 1);
	r.whiskers.clear();
	// a loop that doubles back on itself
	REQUIRE(!r.loops.empty());
	vis::Edge back = r.loops[0].edges[0];
	std::swap(back.a, back.b);
	r.loops[0].edges.insert(r.loops[0].edges.begin() + 1, back);
	CHECK(count_kind(inv::check_VIS_REGULAR(r, nullptr, Flags(), plan), "VIS-REGULAR") >= 1);
}

TEST_CASE("visibility: VIS-REGULAR allows a member tangent at Q and a planned terminal's segment") {
	Tracer t;
	// tangent_infinity, plan [infinity]: the only live member is tangent at Q to surface 6
	const std::vector<PlanItem> inf{PlanItem{INFINITY_ID, true, std::nullopt, std::nullopt}};
	const vis::Result r = region_of(t, "playground__tangent_infinity", inf, vis::Pt{760, 440});
	REQUIRE(r.supported);
	int members = 0;
	for (const auto &w : r.whiskers) members += w.kind == vis::EdgeKind::Member ? 1 : 0;
	CHECK(members >= 1);
	inv::VisInput in{&r, &t, vis::Pt{760, 440}, inf, Flags(), vis::Rect{0, 0, 1920, 1080}};
	CHECK(count_kind(inv::check_visibility(in), "VIS-REGULAR") == 0);
	// hit_effects, plan [11 L]: surface 11 is a terminal; cursors on its segment end the arrow there
	Tracer t2;
	const std::vector<PlanItem> term{PlanItem{11, true, std::nullopt, std::nullopt}};
	const vis::Result r2 = region_of(t2, "test_levels__hit_effects", term, vis::Pt{960, 540});
	REQUIRE(r2.supported);
	CHECK(!r2.whiskers.empty());
	inv::VisInput in2{&r2, &t2, vis::Pt{960, 540}, term, Flags(), vis::Rect{0, 0, 1920, 1080}};
	CHECK(count_kind(inv::check_visibility(in2), "VIS-REGULAR") == 0);
}

TEST_CASE("visibility: the display's cursor index counts drawn steps (B3)") {
	// directional_arcs, plan [infinity]: the walk wraps through infinity on the
	// bounds' edge, and the wrap's incoming half is zero-length there and not
	// drawn; the cursor index used to keep counting it (one past the steps)
	Tracer t;
	const ojson d = load_dump(std::string(RC_REPO_ROOT) + "/bench/scenes/playground__directional_arcs.surfaces.json");
	t.set_emit_infinity(true);
	t.set_defs(defs_from_json(d["surfaces"]));
	const std::vector<PlanItem> inf{PlanItem{INFINITY_ID, true, std::nullopt, std::nullopt}};
	const V2 cursor{1360, 300};
	const std::optional<TracePair> pair = t.trace_pair(V2{500, 300}, cursor, inf, Flags());
	REQUIRE(pair.has_value());
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	const std::vector<inv::SurfaceInfo> none;
	const TracePath dl = sweep::prepare_for_display(pair->planned, bounds, none);
	REQUIRE(dl.cursor_index > 0);
	REQUIRE(dl.cursor_index <= int(dl.steps.size()));
	CHECK(dl.steps[size_t(dl.cursor_index) - 1].end == cursor);
	CHECK(defined_visible(t, V2{500, 300}, cursor, inf, Flags()) == std::optional<bool>(true));
}

TEST_CASE("visibility: the member tangent at Q is cast exactly both ways (K8)") {
	// tangent_infinity, plan [infinity], player (760, 440): the member through
	// the player tangent at Q = (960, 540) to surface 6 is the circle centred at
	// (835, 540), radius 125; (800, 420) lies exactly on its other arc. A rounded
	// aim for the other way crosses surface 6 at Q, so its cast died and the
	// region excluded the cursor the preview shows.
	Tracer t;
	const std::vector<PlanItem> inf{PlanItem{INFINITY_ID, true, std::nullopt, std::nullopt}};
	const vis::Result r = region_of(t, "playground__tangent_infinity", inf, vis::Pt{760, 440});
	REQUIRE(r.supported);
	int exact_other = 0;
	for (const vis::Member &m : r.members) {
		if (m.tangent_q_sid == 6 && m.why.find("the other way (exact)") != std::string::npos) {
			exact_other++;
			CHECK(m.cursor.x == 910.0);
			CHECK(m.cursor.y == 440.0);
			CHECK(m.crit.cursor_ok);
		}
	}
	CHECK(exact_other == 1);
	CHECK(vis::contains(r, vis::Pt{800, 420}) == vis::Membership::Inside);
	CHECK(defined_visible(t, V2{760, 440}, V2{800, 420}, inf, Flags()) == std::optional<bool>(true));
}

namespace {
// The kinds a check run reports (the prefix before ':' or ' (').
std::set<std::string> kinds_of(const inv::Violations &vs) {
	std::set<std::string> out;
	for (const inv::Violation &v : vs) {
		const std::string m = render(v);
		out.insert(m.substr(0, m.find_first_of(":( ")));
	}
	return out;
}
std::string joined(const std::set<std::string> &ks) {
	std::string s;
	for (const std::string &k : ks) s += (s.empty() ? "" : ", ") + k;
	return s;
}
} // namespace

// Each region check catches what it names (F7): a valid region, corrupted one
// way per check, trips that check. The others a corruption trips too are its
// consequences, listed per case (VIS-DETERMINISTIC sees every change to the
// loops or cells: the corrupted region is not what the inputs compute); none
// beyond them fires. VIS-REGULAR has its own cases above.
TEST_CASE("visibility: every region check detects its corruption") {
	Tracer t;
	const std::vector<PlanItem> plan{PlanItem{4, true, std::nullopt, std::nullopt}};
	const vis::Pt player{960, 827.999};
	const vis::Result base = region_of(t, "test_levels__three_mirrors", plan, player);
	REQUIRE(base.supported);
	REQUIRE(!base.loops.empty()); // (Stage 111: the realization through infinity adds a loop of its own)
	REQUIRE(base.loops[0].edges.size() > 4);
	REQUIRE(base.loops[0].edges[1].kind == vis::EdgeKind::Member);
	REQUIRE(base.loops[0].edges[2].kind == vis::EdgeKind::Bounds);
	REQUIRE(base.loops[0].edges[4].kind == vis::EdgeKind::Carrier);
	REQUIRE(base.loops[0].edges[4].surface != 1);
	auto fired = [&](const vis::Result &r) {
		inv::VisInput in{&r, &t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080}};
		return kinds_of(inv::check_visibility(in));
	};
	CHECK(fired(base).empty());
	auto reverse_loop = [](std::vector<vis::Edge> &l) {
		std::reverse(l.begin(), l.end());
		for (vis::Edge &e : l) std::swap(e.a, e.b);
	};
	struct Case {
		const char *what;
		std::set<std::string> targets;
		std::set<std::string> consequences;
		std::function<void(vis::Result &)> corrupt;
	};
	const std::string C = "VIS-CONTAINS-MATCHES-POLYGON", D = "VIS-DRAWN-MATCHES", DET = "VIS-DETERMINISTIC";
	const std::vector<Case> cases = {
		{"a loop with an edge missing", {"VIS-CLOSED"}, {C, D, DET, "VIS-NO-BLOCKER-INSIDE", "VIS-STAR"},
			[](vis::Result &r) { r.loops[0].edges.erase(r.loops[0].edges.begin() + 2); }},
		{"a cell wound the other way", {"VIS-ORIENTED"}, {}, [&](vis::Result &r) {
			 for (auto &l : r.cells[0].loops) reverse_loop(l);
		 }},
		{"a cell twice", {"VIS-DISJOINT"}, {D, DET}, [](vis::Result &r) { r.cells.push_back(r.cells[0]); }},
		{"a whisker to infinity", {"VIS-FINITE"}, {"VIS-REGULAR"}, [](vis::Result &r) {
			 vis::Edge w = r.loops[0].edges[4];
			 w.b = vis::Pt{vis::INF, vis::INF};
			 r.whiskers.push_back(w);
		 }},
		{"an edge on the wrong carrier", {"VIS-EDGE-ON-GEOMETRY"}, {}, [](vis::Result &r) { r.loops[0].edges[4].surface = 1; }},
		{"a shadow edge ending on no carrier", {"VIS-VERTEX-PROVENANCE", "VIS-SHADOW-ANCHORED"}, {"VIS-EDGE-ON-GEOMETRY", C, D, DET, "VIS-STAR"},
			[](vis::Result &r) {
				auto &es = r.loops[0].edges;
				const vis::Pt m = es[1].via; // on member 1's edge, away from every carrier
				es[1].b = m;
				es[2].a = m;
			}},
		{"the whole clip rectangle", {"VIS-NO-BLOCKER-INSIDE"}, {"VIS-BOUNDARY-DECIDED", C, D, DET, "VIS-ORIENTED"}, [](vis::Result &r) {
			 const vis::Rect &c = r.clip;
			 const vis::Pt q[4] = {{c.x0, c.y0}, {c.x1, c.y0}, {c.x1, c.y1}, {c.x0, c.y1}};
			 vis::Loop l;
			 for (int k = 0; k < 4; k++) {
				 vis::Edge e;
				 e.kind = vis::EdgeKind::Bounds;
				 e.a = q[k];
				 e.b = q[(k + 1) % 4];
				 e.via = (e.a + e.b) * 0.5;
				 l.edges.push_back(e);
			 }
			 l.area = vis::loop_area(l.edges);
			 r.loops = {l};
		 }},
		{"no loops for the cells", {"VIS-SOURCE-ON-BOUNDARY"}, {"VIS-BOUNDARY-DECIDED", C, D, DET, "VIS-ORIENTED", "VIS-STAR"},
			[](vis::Result &r) { r.loops.clear(); }},
		{"a cell's middle member elsewhere", {"VIS-STAR"}, {}, [](vis::Result &r) { r.cells[0].mid_beam = r.members[0].beam; }},
		{"a cell's middle cast of another shape", {"VIS-CELL-CONSTANT"}, {}, [](vis::Result &r) { r.cells[0].mid.iv.push_back(r.cells[0].mid.iv[0]); }},
		{"an exact point decided the other way", {"VIS-BOUNDARY-DECIDED"}, {}, [](vis::Result &r) {
			 // (Stage 111: a split plan holds the point in both halves; the check
			 // reads the orientation the solver picks, so every copy is flipped)
			 for (vis::Member &m : r.members) {
				 if (m.exact && m.crit.valid && m.crit.chosen && vis::contains(r, m.cursor) != vis::Membership::Unclassified) {
					 const vis::Pt c = m.cursor;
					 for (vis::Member &o : r.members) {
						 if (o.cursor == c) {
							 o.crit.cursor_ok = !o.crit.cursor_ok;
							 o.poi_ok = !o.poi_ok;
						 }
					 }
					 return;
				 }
			 }
			 FAIL("no exact member");
		 }},
		{"a cell missing", {C, D}, {DET}, [](vis::Result &r) { r.cells.erase(r.cells.begin()); }},
		{"an inclusive flag flipped", {DET}, {}, [](vis::Result &r) { r.loops[0].edges[0].inclusive = !r.loops[0].edges[0].inclusive; }},
		{"a departure the empty plan cannot reach", {"VIS-ITERATION-CHAIN"}, {"VIS-EDGE-ON-GEOMETRY", "VIS-SOURCE-ON-BOUNDARY"}, [](vis::Result &r) {
			 // (800, 120): on mirror 4's carrier, outside the empty plan's region
			 for (auto &l : r.cells[0].loops) {
				 for (vis::Edge &e : l) {
					 if (e.kind == vis::EdgeKind::Carrier && e.role == vis::End::Near) {
						 e.via = vis::Pt{800, 120};
						 return;
					 }
				 }
			 }
			 FAIL("no departure edge");
		 }},
	};
	for (const Case &c : cases) {
		vis::Result r = base;
		c.corrupt(r);
		const std::set<std::string> got = fired(r);
		for (const std::string &k : c.targets) {
			const std::string msg = std::string(c.what) + ": " + k + " missed (got " + joined(got) + ")";
			CHECK_MESSAGE(got.count(k) == 1, msg);
		}
		for (const std::string &k : got) {
			const std::string msg = std::string(c.what) + ": " + k + " fired too";
			CHECK_MESSAGE((c.targets.count(k) + c.consequences.count(k)) == 1, msg);
		}
	}
}

TEST_CASE("visibility: VISIBILITY-EQUALS-NON-DIVERGENCE detects a region that disagrees with the preview") {
	Tracer t;
	const std::vector<PlanItem> plan{PlanItem{4, true, std::nullopt, std::nullopt}};
	const vis::Pt player{960, 827.999};
	vis::Result r = region_of(t, "test_levels__three_mirrors", plan, player);
	REQUIRE(r.supported);
	REQUIRE(!r.cells.empty());
	const vis::Pt c = r.cells[0].inside;
	const std::optional<TracePair> pair = t.trace_pair(V2{float(player.x), float(player.y)}, V2{float(c.x), float(c.y)}, plan, Flags());
	REQUIRE(pair.has_value());
	const Rect2f bounds = Rect2f::of(0, 0, 1920, 1080);
	const std::vector<inv::SurfaceInfo> none;
	const TracePath dp = sweep::prepare_for_display(pair->physical, bounds, none);
	const TracePath dl = sweep::prepare_for_display(pair->planned, bounds, none);
	const std::vector<TraceStep> typed = sweep::merge(dl.steps, dp.steps, dl.cursor_index);
	const V2 cur{float(c.x), float(c.y)};
	CHECK(inv::check_VISIBILITY_EQUALS_NON_DIVERGENCE(r, cur, &dl, &typed, pair->cursor_point_agrees).empty());
	r.loops.clear(); // the region excludes it; the preview still shows it
	CHECK(count_kind(inv::check_VISIBILITY_EQUALS_NON_DIVERGENCE(r, cur, &dl, &typed, pair->cursor_point_agrees), "VISIBILITY-EQUALS-NON-DIVERGENCE") == 1);
}

TEST_CASE("visibility: the drawn fill covers the region once (F8)") {
	// pre_inverted, empty plan, the user's player (float32): a member nearly
	// tangent to surface 4's carrier made the tessellated hole cross itself, and
	// the triangulation overlapped itself in a thin triangle
	Tracer t;
	const vis::Result r = region_of(t, "playground__pre_inverted", {}, vis::Pt{1277.0030517578125, 387.4353942871094});
	REQUIRE(r.supported);
	CHECK(inv::check_VIS_DRAWN_MATCHES(r).empty());
}

TEST_CASE("visibility: the drawn fill has no seams (F8)") {
	// drawn cell by cell, two cells tessellated a shared arc differently: hairline
	// seams (106 T-junctions on pre_inverted, 66 on tangent_infinity, empty
	// plan). A seam is a mesh edge used by one triangle only that lies inside the
	// region: every such edge must lie on the region's boundary (within the
	// tessellation's bound) or on the domain's edge, or face another open edge
	// across less than 0.1 px.
	struct Case {
		const char *scene;
		vis::Pt player;
	};
	for (const Case &c : {Case{"playground__pre_inverted", {500, 300}}, Case{"playground__tangent_infinity", {608.4711, 635.5568}},
			 Case{"playground__pre_inverted", {1277.0030517578125, 387.4353942871094}}, Case{"test_levels__room_with_inversion", {760, 400}}}) {
		Tracer t;
		const vis::Result r = region_of(t, c.scene, {}, c.player);
		REQUIRE(r.supported);
		const vis::Fill f = vis::fill_triangles(r);
		std::map<std::pair<std::pair<double, double>, std::pair<double, double>>, int> uses;
		auto key = [](const vis::Pt &a, const vis::Pt &b) {
			std::pair<double, double> u{a.x, a.y}, w{b.x, b.y};
			return u < w ? std::make_pair(u, w) : std::make_pair(w, u);
		};
		for (size_t k = 0; k + 2 < f.indices.size(); k += 3) {
			for (int s = 0; s < 3; s++) {
				uses[key(f.points[f.indices[k + size_t(s)]], f.points[f.indices[k + size_t((s + 1) % 3)]])]++;
			}
		}
		const vis::Rect &d = r.domain;
		// the mesh's open edges inside the region
		std::vector<std::pair<vis::Pt, vis::Pt>> open;
		for (const auto &[e, n] : uses) {
			if (n != 1) {
				continue;
			}
			const vis::Pt a{e.first.first, e.first.second};
			const vis::Pt b{e.second.first, e.second.second};
			const vis::Pt m = (a + b) * 0.5;
			const bool on_domain = std::abs(m.x - d.x0) < 1e-6 || std::abs(m.x - d.x1) < 1e-6 || std::abs(m.y - d.y0) < 1e-6 || std::abs(m.y - d.y1) < 1e-6;
			if (!on_domain && vis::margin(r, m) > 0.5) {
				open.push_back({a, b});
			}
		}
		// a seam is as wide as the gap to the open edge facing it
		auto seg_d = [](const vis::Pt &p, const vis::Pt &a, const vis::Pt &b) {
			const vis::Pt ab = b - a;
			const double L = vis::dot(ab, ab);
			const double t = L > 0 ? std::clamp(vis::dot(p - a, ab) / L, 0.0, 1.0) : 0.0;
			return vis::norm(p - (a + ab * t));
		};
		int seams = 0;
		double widest = 0.0;
		for (size_t i = 0; i < open.size(); i++) {
			const vis::Pt m = (open[i].first + open[i].second) * 0.5;
			double w = std::numeric_limits<double>::infinity();
			for (size_t j = 0; j < open.size(); j++) {
				if (j != i) w = std::min(w, seg_d(m, open[j].first, open[j].second));
			}
			widest = std::max(widest, std::isfinite(w) ? w : 0.0);
			// (thin cells where members converge at a pole leave gaps of hundredths of a
			// pixel; the seams this guards against were up to the chords' 0.25 px)
			if (std::isfinite(w) && w > 0.1) {
				seams++;
				if (std::getenv("RC_VIS_TJ")) std::printf("seam %s: (%.4f,%.4f)-(%.4f,%.4f) width %.3g\n", c.scene, open[i].first.x, open[i].first.y, open[i].second.x, open[i].second.y, w);
			}
		}
		const std::string info = std::string(c.scene) + ": open interior edges " + std::to_string(open.size()) + ", widest gap " + std::to_string(widest);
		MESSAGE(info);
		const std::string msg = std::string(c.scene) + ": " + std::to_string(seams) + " seams";
		CHECK_MESSAGE(seams == 0, msg);
		CHECK(inv::check_VIS_DRAWN_MATCHES(r).empty());
	}
}
