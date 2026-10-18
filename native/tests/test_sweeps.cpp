// The invariant and visibility sweeps natively (Workflow W2): every manifest
// in bench/sweeps/ holds exactly what a GUT sweep handed to SweepKernel.run
// (the scene's defs and surface infos, the positions, the plans, the flags and
// the switches), written by SweepRunner.sweep_native under SWEEP_MANIFEST_DIR
// (make manifests) and checked against the suite by the manifests gate. The
// sweep itself is rc::sweep::run, as the binding calls it.
//
//   RC_SWEEPS_PART=k/n     the k-th player of every n (ctest's rc_sweeps_* entries)
//   RC_SWEEPS_STRIDE=s     every s-th player (the commit gate: 4)
//   RC_SWEEPS_FILTER=text  only the manifests whose file name contains text
//   RC_SWEEPS_VIOLATIONS=path  write the violations as violations.json does
#include "json_defs.hpp"
#include "parts.hpp"
#include "violations.hpp"
#include "rc/sweep.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rc;

namespace {

struct Manifest {
	std::string file;
	std::string scene;
	std::vector<SurfaceDef> defs;
	std::vector<inv::SurfaceInfo> infos;
	std::vector<V2> positions;
	std::vector<std::vector<PlanItem>> plans;
	Flags flags;
	bool skip_equal = false;
	bool emit_infinity = false;
	bool visibility = false;
	bool carrier_cursors = false;
};

V2 v2_of(const ojson &p) { return V2{float(p[0].get<double>()), float(p[1].get<double>())}; }

Manifest load_manifest(const std::filesystem::path &path) {
	std::ifstream in(path);
	REQUIRE_MESSAGE(in.good(), "manifest " << path.string());
	const ojson j = ojson::parse(in);
	Manifest m;
	m.file = path.filename().string();
	m.scene = j.value("scene", std::string());
	m.defs = defs_from_json(j["defs"]);
	for (const ojson &s : j["infos"]) {
		inv::SurfaceInfo info;
		info.id = s["id"].get<int>();
		info.has_effect = s["has_effect"].get<bool>();
		const ojson &seg = s["segment"];
		info.s = v2_of(seg["start"]);
		info.e = v2_of(seg["end"]);
		info.v = seg["via"].is_null() ? V2{INFINITY, INFINITY} : v2_of(seg["via"]);
		info.full = seg["full"].get<bool>();
		const ojson &c = s["carrier"];
		info.carrier = arc::GeneralizedCircle{c["a"].get<double>(), c["b"].get<double>(), c["c"].get<double>(), c["d"].get<double>()};
		m.infos.push_back(info);
	}
	for (const ojson &p : j["positions"]) {
		m.positions.push_back(v2_of(p));
	}
	for (const ojson &p : j["plans"]) {
		m.plans.push_back(plan_from_json(p));
	}
	m.flags = flags_from_json(j["flags"]);
	m.skip_equal = j["skip_equal"].get<bool>();
	m.emit_infinity = j["emit_infinity"].get<bool>();
	m.visibility = j["visibility"].get<bool>();
	m.carrier_cursors = j["carrier_cursors"].get<bool>();
	return m;
}

std::vector<std::filesystem::path> manifest_files() {
	std::vector<std::filesystem::path> out;
	const std::filesystem::path dir = std::filesystem::path(RC_REPO_ROOT) / "bench" / "sweeps";
	const char *filter = std::getenv("RC_SWEEPS_FILTER");
	if (std::filesystem::is_directory(dir)) {
		for (const auto &e : std::filesystem::directory_iterator(dir)) {
			if (e.path().extension() == ".json" && (filter == nullptr || e.path().filename().string().find(filter) != std::string::npos)) {
				out.push_back(e.path());
			}
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::string plan_str(const std::vector<PlanItem> &plan) {
	std::string s = "[";
	for (size_t i = 0; i < plan.size(); i++) {
		if (i > 0) {
			s += ",";
		}
		s += plan[i].is_infinity() ? "inf" : std::to_string(plan[i].surface_id) + (plan[i].left ? "/L" : "/R");
	}
	return s + "]";
}

std::string v2_str(const V2 &v) {
	char b[64];
	std::snprintf(b, sizeof b, "(%.9g, %.9g)", double(v.x), double(v.y));
	return b;
}

// The players this entry sweeps: every stride-th player (RC_SWEEPS_STRIDE),
// those dealt round-robin to the RC_SWEEPS_PART parts across the manifests (so
// every part gets its share whatever the stride and the manifest's size); the
// cursors stay whole, as SweepRunner.in_shard.
struct Selection {
	int stride = 1;
	rc_test::Part part;
	long dealt = 0;
	std::vector<size_t> players(size_t n_positions) {
		std::vector<size_t> out;
		for (size_t pi = 0; pi < n_positions; pi++) {
			if (pi % size_t(stride) != 0) {
				continue;
			}
			if (part.takes(dealt++)) {
				out.push_back(pi);
			}
		}
		return out;
	}
};

Selection selection() {
	Selection s;
	s.part = rc_test::part_of("RC_SWEEPS_PART");
	if (const char *e = std::getenv("RC_SWEEPS_STRIDE")) {
		s.stride = std::max(1, std::atoi(e));
	}
	return s;
}

struct Totals {
	long manifests = 0;
	long combos = 0;
	std::vector<std::string> violations;
	ojson dump = ojson::array();
};

void run_manifests(bool visibility, Totals &t) {
	Selection sel = selection();
	for (const std::filesystem::path &path : manifest_files()) {
		const Manifest m = load_manifest(path);
		if (m.visibility != visibility) {
			continue;
		}
		t.manifests++;
		const std::vector<size_t> players = sel.players(m.positions.size());
		t.combos += long(players.size()) * long(m.positions.size() - (m.skip_equal ? 1 : 0)) * long(m.plans.size());
		std::vector<sweep::Combo> failing;
		for (const size_t pi : players) {
			// one player: the shard rule pi % n == i with n = the position count
			std::vector<sweep::Combo> f = sweep::run(m.defs, m.infos, m.positions, m.plans, m.flags, int(pi), int(m.positions.size()), m.skip_equal, true,
				m.emit_infinity, m.visibility, m.carrier_cursors);
			failing.insert(failing.end(), f.begin(), f.end());
		}
		for (const sweep::Combo &c : failing) {
			for (const inv::Violation &v : c.violations) {
				const std::string line = "[" + m.file + "] plan=" + plan_str(m.plans[size_t(c.plan_index)]) + " player=" + v2_str(c.player) +
					" cursor=" + v2_str(c.cursor) + ": " + rc_test::render(v);
				t.violations.push_back(line);
				ojson e;
				e["scene"] = m.scene;
				e["player_pos"] = {double(c.player.x), double(c.player.y)};
				e["cursor_pos"] = {double(c.cursor.x), double(c.cursor.y)};
				ojson plan = ojson::array();
				for (const PlanItem &it : m.plans[size_t(c.plan_index)]) {
					plan.push_back({{"surface_id", it.surface_id}, {"side", it.left ? 0 : 1}});
				}
				e["plan"] = plan;
				e["violation"] = rc_test::render(v);
				t.dump.push_back(e);
			}
		}
	}
	if (const char *out = std::getenv("RC_SWEEPS_VIOLATIONS")) {
		std::ofstream f(out);
		f << t.dump.dump(2);
	}
	MESSAGE((visibility ? "visibility" : "invariant") << " sweeps: " << t.manifests << " manifests, " << t.combos << " combos (part " << sel.part.k << "/"
																		  << sel.part.n << ", stride " << sel.stride << "), " << t.violations.size() << " violations");
	for (size_t i = 0; i < t.violations.size() && i < 40; i++) {
		MESSAGE(t.violations[i]);
	}
}

} // namespace

TEST_CASE("sweeps: the invariant sweeps' manifests hold on every combo") {
	Totals t;
	run_manifests(false, t);
	CHECK(t.manifests > 0);
	CHECK(t.violations.empty());
}

TEST_CASE("sweeps: the visibility sweeps' manifests hold on every combo") {
	Totals t;
	run_manifests(true, t);
	CHECK(t.manifests > 0);
	CHECK(t.violations.empty());
}
