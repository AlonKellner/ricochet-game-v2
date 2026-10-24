// rc_tool regions: the visibility region of every plannable side of a level
// (each side planned alone, every projective side, the infinity entry; --pairs
// adds the first sides' two-entry plans), built at several players, timed and
// digested. The region is what the game computes while the player walks, so
// this is the cost that decides whether regions arrive within the frame
// (docs/WORKFLOW.md: timing is evidence, never a gate). Per plan it reports the
// build's wall time, the casts by kind, the refinements, the build's phases, the
// parallel batches' tail (their slowest casts summed against all casts' time),
// the union and fill after the build, the ladder's climbs, allocations per
// region (--allocs), and one hash of every region's edges: two runs that agree
// on every hash computed the same regions bit for bit (tools/perf_diff.py).
#include "regions.hpp"

#include "rc/region_digest.hpp"
#include "rc/region_union.hpp"
#include "rc/thread_alloc.hpp"
#include "rc/tracer.hpp"
#include "rc/visibility.hpp"
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace regions {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using clk = std::chrono::steady_clock;

struct Options {
	int players = 6;
	int threads = 0; // 0: the default (RC_VIS_THREADS or every core up to 8)
	int reps = 1;
	bool pairs = false;
	bool allocs = false;
	std::string only;
};

struct Row {
	std::string plan;
	std::vector<double> ms;      // one per (player, rep): the build (vis::compute)
	std::vector<double> fill_ms; // ... the union and the fill after it
	long long casts = 0, refinements = 0, spared = 0;
	std::array<long long, 6> kinds{};
	std::array<long long, 5> phase_usec{};
	long long batch_max_usec = 0, batch_sum_usec = 0, batch_wall_usec = 0;
	long long members = 0, cells = 0;
	long long walks = 0, climbs = 0, big_usec = 0, unconverged = 0, hits = 0, hits_off = 0;
	long long allocs = 0, allocs_system = 0, frees_unsized = 0;
	int regions = 0;
	uint64_t hash = 14695981039346656037ULL;
	bool supported = true;
};

double pct(std::vector<double> v, double q) {
	if (v.empty()) {
		return 0.0;
	}
	std::sort(v.begin(), v.end());
	return v[size_t(double(v.size() - 1) * q)];
}

std::string plan_label(const std::vector<rc::PlanItem> &plan) {
	if (plan.empty()) {
		return "[]";
	}
	std::string s = "[";
	for (const rc::PlanItem &it : plan) {
		s += (s.size() > 1 ? "," : "") + (it.surface_id == rc::INFINITY_ID ? std::string("inf") : std::to_string(it.surface_id)) + (it.left ? "L" : "R");
	}
	return s + "]";
}

// The plans: the empty plan, one entry per transformative or state side (and
// every projective side), the infinity entry; --pairs: the first four sides'
// pairs (test_visibility.cpp's level_plans, without the projective pair).
std::vector<std::vector<rc::PlanItem>> plans_of(const std::vector<rc::SurfaceDef> &defs, bool pairs) {
	std::vector<rc::PlanItem> singles, projs;
	for (const rc::SurfaceDef &d : defs) {
		if (d.pre_transform) {
			continue;
		}
		for (const bool left : {true, false}) {
			const std::optional<rc::SideDef> &side = left ? d.left : d.right;
			if (!side) {
				continue;
			}
			if (rc::is_transformative(side->type) || !side->states.empty()) {
				singles.push_back(rc::PlanItem{d.id, left, std::nullopt, std::nullopt});
			} else if (rc::is_projective(side->type)) {
				projs.push_back(rc::PlanItem{d.id, left, std::nullopt, std::nullopt});
			}
		}
	}
	std::vector<std::vector<rc::PlanItem>> plans{{}};
	for (const rc::PlanItem &p : singles) {
		plans.push_back({p});
	}
	for (const rc::PlanItem &p : projs) {
		plans.push_back({p});
	}
	plans.push_back({rc::PlanItem{rc::INFINITY_ID, true, std::nullopt, std::nullopt}});
	if (pairs) {
		for (size_t i = 0; i < singles.size() && i < 4; i++) {
			for (size_t j = 0; j < singles.size() && j < 4; j++) {
				if (singles[i].surface_id != singles[j].surface_id) {
					plans.push_back({singles[i], singles[j]});
				}
			}
		}
	}
	return plans;
}

std::vector<Row> run_level(const std::string &path, const Options &o) {
	const rcg::Level level = rcg::load_level(path);
	rcg::SessionConfig cfg;
	cfg.async = false;
	rcg::Session s(level, cfg);
	const std::vector<rc::SurfaceDef> defs = s.world().defs();
	const rc::Flags flags = s.flags();
	const rc::V2 home = s.player();
	const rc::vis::Rect vb{double(level.bounds.x), double(level.bounds.y), double(level.bounds.x) + double(level.bounds.w),
		double(level.bounds.y) + double(level.bounds.h)};
	// the players: the start, then steps of (73, 41) px folded into the bounds
	// grown by nothing (a player outside the level is no case)
	std::vector<rc::vis::Pt> players;
	for (int k = 0; k < o.players; k++) {
		double x = double(home.x) + 73.0 * k, y = double(home.y) + 41.0 * k;
		const double w = std::max(1.0, vb.x1 - vb.x0 - 40.0), h = std::max(1.0, vb.y1 - vb.y0 - 40.0);
		x = vb.x0 + 20.0 + std::fmod(x - vb.x0 - 20.0 + 1e6 * w, w);
		y = vb.y0 + 20.0 + std::fmod(y - vb.y0 - 20.0 + 1e6 * h, h);
		players.push_back(rc::vis::Pt{x, y});
	}
	rc::Tracer t;
	t.set_defs(defs);
	std::vector<Row> rows;
	for (const std::vector<rc::PlanItem> &plan : plans_of(defs, o.pairs)) {
		Row r;
		r.plan = plan_label(plan);
		if (!o.only.empty() && r.plan.find(o.only) == std::string::npos) {
			continue;
		}
		t.reset_ladder_stats();
		const long long a0 = rc::alloc_count(), s0 = rc::alloc_system_count(), u0 = rc::alloc_unsized_count();
		for (int rep = 0; rep < o.reps; rep++) {
			for (const rc::vis::Pt &P : players) {
				const auto t0 = clk::now();
				const rc::vis::Result res = rc::vis::compute(t, P, plan, flags, vb);
				const auto t1 = clk::now();
				r.ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
				if (res.supported) {
					const rc::vis::Outline outline = rc::vis::union_outline(res);
					const rc::vis::Fill fill = rc::vis::fill_of(outline);
					r.fill_ms.push_back(std::chrono::duration<double, std::milli>(clk::now() - t1).count());
				} else {
					r.supported = false;
				}
				if (rep == 0) {
					r.regions++;
					r.hash = rc::vis::digest_mix(r.hash, rc::vis::result_hash(res));
					r.casts += res.casts;
					r.refinements += res.refinements;
					r.spared += res.casts_spared;
					for (size_t k = 0; k < r.kinds.size(); k++) {
						r.kinds[k] += res.casts_by_kind[k];
					}
					for (size_t k = 0; k < r.phase_usec.size(); k++) {
						r.phase_usec[k] += res.phase_usec[k];
					}
					r.batch_max_usec += res.batch_max_usec;
					r.batch_sum_usec += res.batch_sum_usec;
					r.batch_wall_usec += res.batch_wall_usec;
					r.members += static_cast<long long>(res.members.size());
					r.cells += static_cast<long long>(res.cells.size());
				}
			}
		}
		const rc::LadderStats st = t.ladder_stats();
		const long long n = std::max(1, o.reps);
		r.walks = st.walks / n;
		r.climbs = (st.walks - st.f64) / n;
		r.big_usec = st.big_usec / n;
		r.unconverged = st.unconverged / n;
		r.hits = st.hits / n;
		r.hits_off = st.hits_off_nonplan / n;
		r.allocs = o.allocs ? (rc::alloc_count() - a0) / n : 0;
		r.allocs_system = o.allocs ? (rc::alloc_system_count() - s0) / n : 0;
		r.frees_unsized = o.allocs ? (rc::alloc_unsized_count() - u0) / n : 0;
		rows.push_back(r);
	}
	return rows;
}

json row_json(const Row &r) {
	const double n = double(std::max(1, r.regions));
	json j = {{"regions", r.regions}, {"median", pct(r.ms, 0.5)}, {"p95", pct(r.ms, 0.95)}, {"max", pct(r.ms, 1.0)},
		{"fill_median", pct(r.fill_ms, 0.5)}, {"casts", double(r.casts) / n}, {"refinements", double(r.refinements) / n},
		{"spared", double(r.spared) / n}, {"kinds", json::array()}, {"phases_ms", json::array()},
		{"members", double(r.members) / n}, {"cells", double(r.cells) / n},
		{"batch_max_ms", double(r.batch_max_usec) / n / 1000.0}, {"batch_sum_ms", double(r.batch_sum_usec) / n / 1000.0},
		{"batch_wall_ms", double(r.batch_wall_usec) / n / 1000.0},
		{"walks", double(r.walks) / n}, {"climbs", double(r.climbs) / n}, {"big_ms", double(r.big_usec) / n / 1000.0},
		{"unconverged", double(r.unconverged) / n}, {"hits_per_walk", r.walks > 0 ? double(r.hits) / double(r.walks) : 0.0},
		{"hits_off_nonplan_share", r.hits > 0 ? double(r.hits_off) / double(r.hits) : 0.0}, {"allocs", double(r.allocs) / n}, {"allocs_system", double(r.allocs_system) / n},
		{"frees_unsized", double(r.frees_unsized) / n}, {"supported", r.supported}};
	for (long long k : r.kinds) {
		j["kinds"].push_back(double(k) / n);
	}
	for (long long u : r.phase_usec) {
		j["phases_ms"].push_back(double(u) / n / 1000.0);
	}
	char hex[32];
	std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)r.hash);
	j["hash"] = hex;
	return j;
}

void print_rows(const std::string &level, const std::vector<Row> &rows) {
	std::printf("%s\n%-14s %7s %7s %7s %6s %5s %5s %5s %4s %4s %5s %5s %6s %6s %6s %5s %6s %8s\n", level.c_str(), "plan", "med ms", "p95",
		"max", "fill", "casts", "side", "mid", "ref", "prb", "membs", "cells", "serial", "tail", "climbs", "big", "allocs", "hash");
	for (const Row &r : rows) {
		const double n = double(std::max(1, r.regions));
		// the parallel tail: the batches' wall time beyond their casts' time shared
		// over the threads (a perfectly balanced batch has none); serial: the build
		// outside the batches (collect, cells, merge, sequential casts)
		const double tail = (double(r.batch_wall_usec) - double(r.batch_sum_usec) / double(std::max(1, rc::vis::vis_threads()))) / n / 1000.0;
		const double serial = pct(r.ms, 0.5) - double(r.batch_wall_usec) / n / 1000.0;
		std::printf("%-14s %7.2f %7.2f %7.2f %6.2f %5.0f %5.0f %5.0f %4.0f %4.0f %5.0f %5.0f %6.2f %6.2f %6.1f %5.2f %6.0f %016llx%s\n", r.plan.c_str(),
			pct(r.ms, 0.5), pct(r.ms, 0.95), pct(r.ms, 1.0), pct(r.fill_ms, 0.5), double(r.casts) / n, double(r.kinds[1]) / n, double(r.kinds[2]) / n,
			double(r.refinements) / n, double(r.kinds[4]) / n, double(r.members) / n, double(r.cells) / n, serial, tail, double(r.climbs) / n,
			double(r.big_usec) / n / 1000.0, double(r.allocs) / n, (unsigned long long)r.hash, r.supported ? "" : " (unsupported)");
	}
}

} // namespace

int run(std::vector<std::string> args) {
	Options o;
	std::string json_out, md_out;
	bool all = false;
	for (size_t i = 0; i < args.size();) {
		const bool value = i + 1 < args.size();
		if (args[i] == "--players" && value) {
			o.players = std::max(1, std::stoi(args[i + 1]));
		} else if (args[i] == "--threads" && value) {
			o.threads = std::stoi(args[i + 1]);
		} else if (args[i] == "--reps" && value) {
			o.reps = std::max(1, std::stoi(args[i + 1]));
		} else if (args[i] == "--only" && value) {
			o.only = args[i + 1];
		} else if (args[i] == "--json" && value) {
			json_out = args[i + 1];
		} else if (args[i] == "--md" && value) {
			md_out = args[i + 1];
		} else if (args[i] == "--all" || args[i] == "--pairs" || args[i] == "--allocs") {
			(args[i] == "--all" ? all : args[i] == "--pairs" ? o.pairs : o.allocs) = true;
			args.erase(args.begin() + long(i));
			continue;
		} else {
			i++;
			continue;
		}
		args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
	}
	if (args.size() != 1) {
		std::fprintf(stderr, "usage: rc_tool regions (<level json> | --all <levels dir>) [--players N] [--threads K] [--reps R] [--pairs] [--allocs] "
							 "[--only <substring>] [--json out] [--md out]\n");
		return 2;
	}
	if (o.threads > 0) {
		setenv("RC_VIS_THREADS", std::to_string(o.threads).c_str(), 1); // (read once, at the first region)
	}
	if (o.allocs) {
		rc::alloc_counting(true);
	}
	rc::set_compute_thread(true); // (the job thread of a session is one for its life: so is this one)
	std::vector<std::string> levels;
	if (all) {
		for (const fs::directory_entry &e : fs::recursive_directory_iterator(args[0])) {
			if (e.path().extension() == ".json") {
				levels.push_back(e.path().string());
			}
		}
		std::sort(levels.begin(), levels.end());
	} else {
		levels.push_back(args[0]);
	}
	json report = json::object();
	report["kind"] = "regions";
	report["players"] = o.players;
	report["threads"] = rc::vis::vis_threads();
	report["reps"] = o.reps;
	report["levels"] = json::object();
	std::ostringstream md;
	md << "| level | plan | median ms | p95 | max | fill | casts | refinements | tail ms | climbs | big ms | hash |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n";
	double worst = 0.0;
	std::string worst_name;
	for (const std::string &path : levels) {
		std::vector<Row> rows;
		try {
			rows = run_level(path, o);
		} catch (const std::exception &e) {
			std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
			return 1;
		}
		print_rows(path, rows);
		json lv = json::object();
		for (const Row &r : rows) {
			const json j = row_json(r);
			lv[r.plan] = j;
			const double n = double(std::max(1, r.regions));
			const double tail = (double(r.batch_wall_usec) - double(r.batch_sum_usec) / double(std::max(1, rc::vis::vis_threads()))) / n / 1000.0;
			char line[512];
			std::snprintf(line, sizeof line, "| %s | %s | %.2f | %.2f | %.2f | %.2f | %.0f | %.0f | %.2f | %.1f | %.2f | %s |\n", path.c_str(),
				r.plan.c_str(), pct(r.ms, 0.5), pct(r.ms, 0.95), pct(r.ms, 1.0), pct(r.fill_ms, 0.5), double(r.casts) / n, double(r.refinements) / n, tail,
				double(r.climbs) / n, double(r.big_usec) / n / 1000.0, j["hash"].get<std::string>().c_str());
			md << line;
			if (pct(r.ms, 0.5) > worst) {
				worst = pct(r.ms, 0.5);
				worst_name = path + " " + r.plan;
			}
		}
		report["levels"][path] = lv;
	}
	if (!json_out.empty()) {
		fs::create_directories(fs::path(json_out).parent_path());
		std::ofstream(json_out) << report.dump(1) << "\n";
	}
	if (!md_out.empty()) {
		fs::create_directories(fs::path(md_out).parent_path());
		std::ofstream(md_out) << "# rc_tool regions (" << o.players << " players, " << rc::vis::vis_threads() << " threads; ms per region)\n\n" << md.str();
	}
	if (o.allocs) {
		std::printf("system mallocs by class (block bytes: count): ");
		for (int k = 0; k < 72; k++) {
			if (rc::alloc_system_by_class(k) > 0) {
				std::printf("%zu:%lld ", k == 71 ? size_t(0) : rc::alloc_class_size(k), rc::alloc_system_by_class(k));
			}
		}
		std::printf("(0: over the largest class)\n");
	}
	std::printf("rc_tool regions: %zu levels at %d threads; the worst median %.2f ms (%s)\n", levels.size(), rc::vis::vis_threads(), worst,
		worst_name.c_str());
	return 0;
}

} // namespace regions
