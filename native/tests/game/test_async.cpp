// Stage 121: the session's background work (rcg/jobs.hpp, SessionConfig::async):
// the newest request wins, a superseded computation is cancelled, and an
// asynchronous session -- however its frames raced its jobs -- drains to the
// synchronous session's state (the session golden: test_sessions.cpp).
#include "rcg/jobs.hpp"
#include "rcg/level.hpp"
#include "rcg/session.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

namespace {

const std::filesystem::path ROOT = RC_REPO_ROOT;

rcg::Level level(const char *name) { return rcg::load_level((ROOT / "data" / "levels" / name).string()); }

nlohmann::json record_of(rcg::Session &s) { return s.record(nlohmann::json::object()); }

} // namespace

TEST_CASE("async: a job's newest input wins; the superseded one is cancelled") {
	std::atomic<int> started{0}, cancelled{0};
	rcg::LatestJob<int, int> job([&](const int &in, const std::atomic<bool> &cancel) {
		started++;
		for (int k = 0; k < 200; k++) { // ~200 ms unless cancelled
			if (cancel) {
				cancelled++;
				return -1;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return in * 10;
	});
	job.submit(1, 1);
	while (started == 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	job.submit(2, 2); // cancels 1
	job.submit(3, 3); // replaces 2 before it starts
	job.wait_idle();
	const auto r = job.take();
	REQUIRE(r.has_value());
	CHECK(r->first == 3);
	CHECK(r->second == 30);
	CHECK(cancelled == 1);
	CHECK(started == 2); // 1 (cancelled) and 3: 2 never ran
	CHECK_FALSE(job.take().has_value()); // taken once
	CHECK_FALSE(job.busy());
}

TEST_CASE("async: the region stops when cancelled") {
	const rcg::Level lv = level("test_levels/crossings.json");
	rcg::Session sync(lv);
	sync.settle();
	rc::Tracer t;
	t.set_defs(sync.world().defs());
	const std::atomic<bool> cancel{true};
	const rc::vis::Result r = rc::vis::compute(t, rc::vis::Pt{300.0, 500.0}, {}, rc::Flags{}, rc::vis::Rect{0, 0, 1920, 1080}, &cancel);
	CHECK(r.cancelled);
	CHECK_FALSE(r.supported);
}

TEST_CASE("async: without cancellation the running computation finishes and is kept") {
	rcg::LatestJob<int, int> job(
		[&](const int &in, const std::atomic<bool> &) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			return in * 10;
		},
		false);
	job.submit(1, 1);
	std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1 runs
	job.submit(2, 2);
	job.submit(3, 3); // replaces 2
	std::optional<std::pair<long long, int>> first;
	for (int k = 0; k < 200 && !first; k++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		first = job.take();
	}
	REQUIRE(first.has_value());
	CHECK(first->first == 1); // the running one, kept
	job.wait_idle();
	const auto last = job.take();
	REQUIRE(last.has_value());
	CHECK(last->first == 3); // then the newest; 2 never ran
}

TEST_CASE("async: a player moving faster than its regions drains to the synchronous session") {
	const rcg::Level lv = level("test_levels/crossings.json");
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session a(lv, cfg);
	rcg::Session s(lv);
	a.settle();
	s.settle();
	a.drain();
	const rc::V2 home = a.player();
	bool pending_seen = false;
	for (int k = 0; k < 12; k++) {
		const rc::V2 p = home + rc::V2::of(40.0 * std::cos(k * 0.5), 40.0 * std::sin(k * 0.5));
		a.teleport(p);
		a.settle(); // the frame: the region is requested, not waited for
		pending_seen = pending_seen || a.region_pending();
	}
	CHECK(pending_seen);
	// regions arrive while the player keeps moving (each a little behind it)
	int shown = 0;
	const int v0 = a.visibility_version();
	for (int k = 0; k < 400 && shown == 0; k++) {
		a.teleport(home + rc::V2::of(40.0 * std::cos(k * 0.05), 40.0 * std::sin(k * 0.05)));
		a.settle();
		shown = a.visibility_version() - v0;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	CHECK(shown > 0);
	s.teleport(a.player());
	s.set_cursor(rc::V2{0, 0});
	s.settle();
	a.drain();
	CHECK_FALSE(a.region_pending());
	CHECK(record_of(a) == record_of(s));
}

TEST_CASE("async: a preview that must climb shows provisionally, then the exact one") {
	// hit_effects: the cursor circling the player declines float64 on a tie almost every frame (TDD_25, F7)
	const rcg::Level lv = level("test_levels/hit_effects.json");
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session a(lv, cfg);
	rcg::Session s(lv);
	const rc::V2 home = a.player();
	int provisional = 0;
	for (int k = 0; k < 20; k++) {
		const rc::V2 c = home + rc::V2::of(420.0 * std::cos(k * 0.31), 420.0 * std::sin(k * 0.31));
		a.set_cursor(c);
		a.settle();
		provisional += a.preview_exact() ? 0 : 1;
		a.drain();
		CHECK(a.preview_exact());
		s.set_cursor(c);
		s.settle();
		CHECK(record_of(a)["preview"] == record_of(s)["preview"]); // drained: the exact preview
	}
	CHECK(provisional > 0);
}

TEST_CASE("async: the hovered side's region is prefetched, shown at the click") {
	const rcg::Level lv = level("test_levels/crossings.json");
	rcg::SessionConfig cfg;
	cfg.async = true;
	rcg::Session a(lv, cfg);
	rcg::Session s(lv);
	// a plannable side: 6 px off the middle of an interactive surface
	std::optional<rc::V2> hover;
	for (const rcg::BuiltSurface &b : a.world().surfaces()) {
		if (b.label && b.left && b.left->interactive && !b.pre_transform && b.seg.is_line()) {
			const rc::V2 d = (b.seg.e - b.seg.s).normalized();
			hover = b.seg.v + rc::V2{-d.y, d.x} * 6.0f;
			break;
		}
	}
	REQUIRE(hover);
	for (rcg::Session *x : {&a, &s}) {
		x->set_cursor(*hover);
		x->settle();
	}
	a.drain(); // the region of now, and the prefetch of the hovered entry
	const int v0 = a.visibility_version();
	a.click(false);
	a.settle();
	CHECK(a.plan().size() == 1);
	CHECK_FALSE(a.region_pending()); // shown at once: no wait for the click's region
	CHECK(a.visibility_version() == v0 + 1);
	s.click(false);
	s.settle();
	a.drain();
	CHECK(record_of(a) == record_of(s));
}
