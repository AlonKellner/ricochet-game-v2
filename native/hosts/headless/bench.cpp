// rc_tool bench: what a frame costs the game (Stage 120, docs/TDD_25_*; the
// port of the GDScript frame bench, Stage 119). Per level, phases of frames:
//   static        no input                    plan_static   a two-entry plan, no input
//   cursor_orbit  the cursor circles the player  plan_orbit  the plan, the cursor circling
//   hover_static  the cursor on a surface      player_orbit  the player circling (a region a frame)
//   plan_click    the frame of a plan click (the hitch: the preview and the region recomputed)
//   walk          the player walking (physics ticks, then the derived state)
//   flight        an arrow in flight, frame by frame (shots fired again and again)
//   capi          the C API frame of a host: advance(0) and the draw list, the cursor circling
// A frame is the game's work and the draw list (what a host's frame costs the game).
// Timing is reported, never judged (machines differ); the counts are checked: a
// still phase computes the preview and the region at most once (its first frame),
// a cursor phase no region after its first frame. Exit 1 when a check fails.
#include "bench.hpp"

#include "rc_api.h"
#include "rcg/level.hpp"
#include "rcg/render.hpp"
#include "rcg/session.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bench {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using clk = std::chrono::steady_clock;

constexpr double FRAME = 1.0 / 60.0;
constexpr double TAU = 6.283185307179586;

struct Options {
	int frames = 250;
	int click_frames = 10;
	double budget_ms = 8.0;
	bool strict = false; // a mean over the budget fails too
	bool async = false;  // Stage 121: the live host's session (the region and climbs in the background)
	bool prefetch = true; // (async) the hovered side's region computed while idle
	double dwell_ms = -1; // (async) how long the cursor rests on a side before its click (< 0: until idle)
};

struct Row {
	std::string name;
	std::vector<double> ms;
	std::vector<double> shown; // (async plan_click) ms from the click until its region shows
	long long previews = 0, regions = 0, climbs = 0, region_climbs = 0;
	long long unreliable = 0, over_tol = 0; // why the preview's walks climbed
	std::string check; // empty: ok
};

double pct(std::vector<double> v, double q) {
	if (v.empty()) {
		return 0.0;
	}
	std::sort(v.begin(), v.end());
	return v[size_t(double(v.size() - 1) * q)];
}

double mean(const std::vector<double> &v) {
	double m = 0.0;
	for (double x : v) {
		m += x / double(v.size());
	}
	return m;
}

// walks the ladder could not accept at float64 (climbed or verified)
long long climbs(const rc::LadderStats &s) { return s.walks - s.f64; }

std::string read(const fs::path &p) {
	std::ifstream in(p, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::vector<Row> run_level(const std::string &path, const Options &o) {
	const rcg::Level level = rcg::load_level(path);
	rcg::SessionConfig cfg;
	cfg.async = o.async;
	cfg.prefetch = o.prefetch;
	rcg::Session s(level, cfg);
	const rc::V2 home = s.player();
	const rc::V2 aim = home + rc::V2{300.0f, -100.0f};
	// the plannable sides: 6 px off the middle of a surface, on its left
	std::vector<rc::V2> plannable;
	for (const rcg::BuiltSurface &b : s.world().surfaces()) {
		if (b.label && b.left && b.left->interactive && !b.pre_transform) {
			const rc::V2 d = (b.seg.e - b.seg.s).normalized();
			plannable.push_back(b.seg.v + rc::V2{-d.y, d.x} * 6.0f);
		}
	}
	auto orbit = [&](const rc::V2 &c, double r, int k, int n) {
		const double a = TAU * k / n;
		return rc::V2::of(double(c.x) + r * std::cos(a), double(c.y) + r * std::sin(a));
	};
	auto reset = [&] {
		s.key(rcg::Action::Reset);
		s.teleport(home);
		s.set_cursor(aim);
	};
	auto plan_two = [&] {
		reset();
		for (size_t i = 0; i < plannable.size() && i < 2; i++) {
			s.set_cursor(plannable[i]);
			s.settle();
			s.click(false);
		}
		s.set_cursor(aim);
	};
	// a phase: setup (untimed), then `frames` timed frames of step + settle + draw
	auto phase = [&](const char *name, int frames, const std::function<void()> &setup, const std::function<void(int)> &step,
					 bool settle = true) {
		Row r;
		r.name = name;
		setup();
		const int p0 = s.preview_version(), v0 = s.visibility_version();
		const long long c0 = climbs(s.preview_stats()), rc0 = climbs(s.region_stats());
		const long long u0 = s.preview_stats().unreliable, o0 = s.preview_stats().over_tol;
		for (int k = 0; k < frames; k++) {
			const auto t0 = clk::now();
			step(k);
			if (settle) {
				s.settle();
			}
			const std::vector<rcg::render::Node> list = s.draw();
			r.ms.push_back(std::chrono::duration<double, std::milli>(clk::now() - t0).count());
		}
		r.previews = s.preview_version() - p0;
		r.regions = s.visibility_version() - v0;
		r.climbs = std::max(0LL, climbs(s.preview_stats()) - c0);
		r.region_climbs = std::max(0LL, climbs(s.region_stats()) - rc0);
		r.unreliable = std::max(0LL, s.preview_stats().unreliable - u0);
		r.over_tol = std::max(0LL, s.preview_stats().over_tol - o0);
		return r;
	};
	const int n = o.frames;
	std::vector<Row> rows;
	rows.push_back(phase("static", n, reset, [](int) {}));
	rows.push_back(phase("cursor_orbit", n, reset, [&](int k) { s.set_cursor(orbit(home, 420.0, k, n)); }));
	rows.push_back(phase("plan_static", n, plan_two, [](int) {}));
	rows.push_back(phase("plan_orbit", n, plan_two, [&](int k) { s.set_cursor(orbit(home, 420.0, k, n)); }));
	rows.push_back(phase("hover_static", n, [&] {
		reset();
		if (!plannable.empty()) {
			s.set_cursor(plannable[0]);
		}
	}, [](int) {}));
	rows.push_back(phase("player_orbit", n, reset, [&](int k) { s.teleport(orbit(home, 60.0, k, n)); }));
	// the click frame: each frame a click on the next plannable side (the plan cleared, untimed, between)
	{
		Row r;
		r.name = "plan_click";
		reset();
		s.settle();
		const int p0 = s.preview_version(), v0 = s.visibility_version();
		const long long c0 = climbs(s.preview_stats()), rc0 = climbs(s.region_stats());
		for (int k = 0; k < o.click_frames && !plannable.empty(); k++) {
			s.key(rcg::Action::ClearPlan);
			s.set_cursor(plannable[size_t(k) % plannable.size()]);
			s.settle();
			if (o.async) { // the cursor rests on the side (idle frames)
				if (o.dwell_ms < 0) {
					s.drain();
				} else {
					for (const auto d0 = clk::now(); std::chrono::duration<double, std::milli>(clk::now() - d0).count() < o.dwell_ms;) {
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
						s.settle();
					}
				}
			}
			const auto t0 = clk::now();
			s.click(false);
			s.settle();
			const std::vector<rcg::render::Node> list = s.draw();
			r.ms.push_back(std::chrono::duration<double, std::milli>(clk::now() - t0).count());
			if (o.async) { // then frames until the click's region shows
				while (s.region_pending()) {
					std::this_thread::sleep_for(std::chrono::microseconds(250));
					s.settle();
				}
				r.shown.push_back(std::chrono::duration<double, std::milli>(clk::now() - t0).count());
			}
		}
		r.previews = s.preview_version() - p0;
		r.regions = s.visibility_version() - v0;
		r.climbs = std::max(0LL, climbs(s.preview_stats()) - c0);
		r.region_climbs = std::max(0LL, climbs(s.region_stats()) - rc0);
		rows.push_back(r);
	}
	// walking: right for a second, then left (advance settles)
	rows.push_back(phase("walk", n, reset, [&](int k) {
		rcg::kinematics::Input in;
		in.axis_x = (k / 60) % 2 == 0 ? 1.0f : -1.0f;
		s.advance(FRAME, in);
	}, false));
	// a flight: frames of an arrow in flight; a landed shot is fired again
	rows.push_back(phase("flight", n, [&] {
		reset();
		s.settle();
	}, [&](int) {
		if (!s.flying()) {
			s.teleport(home);
			s.set_cursor(aim);
			s.settle();
			s.start_fire();
		}
		s.advance(FRAME, {});
	}, false));
	// the C API frame of a host
	{
		Row r;
		r.name = "capi";
		const std::string text = read(path);
		rc_level *lv = nullptr;
		rc_game *g = nullptr;
		const rc_game_config gc{sizeof(rc_game_config), -1, -1, o.async ? 1 : 0};
		if (rc_level_parse(text.data(), text.size(), &lv) == RC_OK && rc_game_create(lv, &gc, &g) == RC_OK) {
			rc_game_teleport(g, home.x, home.y);
			rc_game_advance(g, 0.0, 0, 0);
			for (int k = 0; k < n; k++) {
				const rc::V2 c = orbit(home, 420.0, k, n);
				const auto t0 = clk::now();
				rc_game_set_cursor(g, c.x, c.y);
				rc_game_advance(g, 0.0, 0, 0);
				rc_draw_list l{};
				l.size = sizeof l;
				rc_game_draw_list(g, &l);
				r.ms.push_back(std::chrono::duration<double, std::milli>(clk::now() - t0).count());
			}
		}
		rc_game_destroy(g);
		rc_level_release(lv);
		rows.push_back(r);
	}
	// the counts
	for (Row &r : rows) {
		const bool still = r.name == "static" || r.name == "plan_static" || r.name == "hover_static";
		const bool cursor = r.name == "cursor_orbit" || r.name == "plan_orbit";
		if (still && (r.previews > 1 || r.regions > 1)) {
			r.check = "a still frame recomputed";
		} else if (cursor && r.regions > 1) {
			r.check = "a cursor move recomputed the region";
		} else if (o.strict && r.name != "player_orbit" && r.name != "plan_click" && mean(r.ms) > o.budget_ms) {
			r.check = "mean over budget";
		}
	}
	return rows;
}

json row_json(const Row &r) {
	json j = {{"frames", r.ms.size()}, {"mean", mean(r.ms)}, {"p50", pct(r.ms, 0.5)}, {"p95", pct(r.ms, 0.95)}, {"p99", pct(r.ms, 0.99)},
		{"max", pct(r.ms, 1.0)}, {"previews", r.previews}, {"regions", r.regions}, {"climbs", r.climbs}, {"region_climbs", r.region_climbs},
		{"unreliable", r.unreliable}, {"over_tol", r.over_tol}, {"check", r.check.empty() ? "ok" : r.check}};
	if (!r.shown.empty()) {
		j["shown_mean"] = mean(r.shown);
		j["shown_max"] = pct(r.shown, 1.0);
	}
	return j;
}

void print_rows(const std::string &level, const std::vector<Row> &rows, double budget) {
	std::printf("%s\n%-13s %8s %8s %8s %8s %6s %6s %7s %9s %6s %6s\n", level.c_str(), "phase", "mean ms", "p95", "p99", "max", "prev",
		"regs", "climbs", "reg.climb", "unrel", ">tol");
	for (const Row &r : rows) {
		std::printf("%-13s %8.3f %8.3f %8.3f %8.3f %6lld %6lld %7lld %9lld %6lld %6lld %s%s\n", r.name.c_str(), mean(r.ms), pct(r.ms, 0.95),
			pct(r.ms, 0.99), pct(r.ms, 1.0), r.previews, r.regions, r.climbs, r.region_climbs, r.unreliable, r.over_tol,
			mean(r.ms) > budget ? "(over budget) " : "",
			r.check.empty() ? "" : ("FAIL: " + r.check).c_str());
		if (!r.shown.empty()) {
			std::printf("%-13s %8.3f %8.3f %8s %8.3f   (ms from the click until its region shows)\n", "  shown", mean(r.shown), pct(r.shown, 0.95), "",
				pct(r.shown, 1.0));
		}
	}
}

} // namespace

int run(std::vector<std::string> args) {
	Options o;
	std::string json_out, md_out;
	bool all = false;
	for (size_t i = 0; i < args.size();) {
		const bool value = i + 1 < args.size();
		if (args[i] == "--frames" && value) {
			o.frames = std::stoi(args[i + 1]);
		} else if (args[i] == "--click-frames" && value) {
			o.click_frames = std::stoi(args[i + 1]);
		} else if (args[i] == "--dwell" && value) {
			o.dwell_ms = std::stod(args[i + 1]);
		} else if (args[i] == "--budget" && value) {
			o.budget_ms = std::stod(args[i + 1]);
		} else if (args[i] == "--json" && value) {
			json_out = args[i + 1];
		} else if (args[i] == "--md" && value) {
			md_out = args[i + 1];
		} else if (args[i] == "--all" || args[i] == "--strict" || args[i] == "--async") {
			(args[i] == "--all" ? all : args[i] == "--strict" ? o.strict : o.async) = true;
			args.erase(args.begin() + long(i));
			continue;
		} else if (args[i] == "--no-prefetch") {
			o.prefetch = false;
			args.erase(args.begin() + long(i));
			continue;
		} else {
			i++;
			continue;
		}
		args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
	}
	if (args.size() != 1) {
		std::fprintf(stderr, "usage: rc_tool bench (<level json> | --all <levels dir>) [--frames N] [--click-frames N] [--budget ms] "
							 "[--strict] [--async [--no-prefetch] [--dwell ms]] [--json out] [--md out]\n");
		return 2;
	}
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
	report["frames"] = o.frames;
	report["budget_ms"] = o.budget_ms;
	report["levels"] = json::object();
	bool failed = false;
	std::ostringstream md;
	md << "| level | phase | mean ms | p95 | p99 | max | previews | regions | climbs | region climbs | check |\n"
	   << "|---|---|---|---|---|---|---|---|---|---|---|\n";
	for (const std::string &path : levels) {
		const std::vector<Row> rows = run_level(path, o);
		print_rows(path, rows, o.budget_ms);
		json lv = json::object();
		for (const Row &r : rows) {
			lv[r.name] = row_json(r);
			failed = failed || !r.check.empty();
			char line[512];
			std::snprintf(line, sizeof line, "| %s | %s | %.3f | %.3f | %.3f | %.3f | %lld | %lld | %lld | %lld | %s |\n", path.c_str(), r.name.c_str(),
				mean(r.ms), pct(r.ms, 0.95), pct(r.ms, 0.99), pct(r.ms, 1.0), r.previews, r.regions, r.climbs, r.region_climbs,
				r.check.empty() ? "ok" : r.check.c_str());
			md << line;
		}
		report["levels"][path] = lv;
	}
	if (!json_out.empty()) {
		fs::create_directories(fs::path(json_out).parent_path());
		std::ofstream(json_out) << report.dump(1) << "\n";
	}
	if (!md_out.empty()) {
		fs::create_directories(fs::path(md_out).parent_path());
		std::ofstream(md_out) << "# rc_tool bench (" << o.frames << " frames a phase; ms of game time per frame)\n\n" << md.str();
	}
	std::printf("rc_tool bench: %s\n", failed ? "FAIL (a still or cursor phase recomputed)" : "ok");
	return failed ? 1 : 0;
}

} // namespace bench
