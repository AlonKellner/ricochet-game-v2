// Stage 98: the invariant kernel detects hand-corrupted traces and accepts a
// clean one. Parity with the GDScript checker's messages is the GUT
// differential test's job (tests/native/test_diff_invariants.gd).
#include "rc/invariants.hpp"
#include "rc/sweep.hpp"

#include <doctest/doctest.h>

using namespace rc;
using namespace rc::inv;

namespace {

TraceStep step(V2 a, V2 b, bool hit, StepType type = StepType::Aligned) {
	TraceStep s = TraceStep::make(a, b, 0, hit ? std::optional<TraceHit>(TraceHit{10.0, b, 1, Side::Left, true}) : std::nullopt, std::nullopt, V2{}, false);
	s.type = type;
	if (hit) {
		s.surface_id = 1;
		s.hit_on_segment = true;
	}
	return s;
}

std::vector<std::string> kinds(const Violations &vs) {
	std::vector<std::string> out;
	for (const Violation &v : vs) {
		out.push_back(v.fmt.substr(0, v.fmt.find(':')));
	}
	return out;
}

} // namespace

TEST_CASE("invariants: helpers") {
	CHECK(is_infinity_gap(V2{20000.0f, 0.0f}, V2{0.0f, 0.0f}));
	CHECK(!is_infinity_gap(V2{100.0f, 100.0f}, V2{150.0f, 100.0f}));
	CHECK(is_infinity_gap(V2{1.0f, 500.0f}, V2{1500.0f, 500.0f})); // at the left edge, a long gap
	CHECK(is_at_bounds(V2{1.0f, 500.0f}, Rect2f::of(0, 0, 1920, 1080)));
	CHECK(!is_at_bounds(V2{100.0f, 500.0f}, Rect2f::of(0, 0, 1920, 1080)));
	const arc::GeneralizedCircle circle = arc::from_circle(V2{0.0f, 0.0f}, 100.0);
	CHECK(geometric_carrier_dist(V2{110.0f, 0.0f}, circle) == doctest::Approx(2100.0 / 220.0).epsilon(1e-6)); // |f| / |grad f|, first order
	CHECK(point_to_segment_dist(V2{5.0f, 3.0f}, V2{0.0f, 0.0f}, V2{10.0f, 0.0f}) == doctest::Approx(3.0));
	CHECK(segment_intersection(V2{0.0f, 0.0f}, V2{10.0f, 10.0f}, V2{0.0f, 10.0f}, V2{10.0f, 0.0f}).has_value());
	CHECK(!segment_intersection(V2{0.0f, 0.0f}, V2{10.0f, 0.0f}, V2{0.0f, 1.0f}, V2{10.0f, 1.0f}).has_value());
	// collinear stages merge; a turn does not
	TracePath p;
	p.steps = {step(V2{0, 0}, V2{10, 0}, false), step(V2{10, 0}, V2{20, 0}, false), step(V2{20, 0}, V2{20, 10}, false)};
	p.steps[2].frame_id = 1;
	const auto geo = extract_trace_geometry(&p);
	REQUIRE(geo.size() == 2);
	CHECK(geo[0].end == V2{20.0f, 0.0f});
}

TEST_CASE("invariants: a clean straight path has no violations") {
	TracePath physical;
	physical.steps = {step(V2{100, 100}, V2{500, 100}, true), step(V2{500, 100}, V2{500, 400}, false)};
	std::vector<TraceStep> typed = {physical.steps[0].with_type(StepType::Aligned), physical.steps[1].with_type(StepType::AlignedPostPlanned)};
	Input in;
	in.player = V2{100, 100};
	in.cursor = V2{300, 100};
	in.physical = &physical;
	in.planned = &physical;
	in.typed = &typed;
	// no ladder result, no surfaces: the data-only checks
	const Violations vs = check_all(in);
	// the trace ends mid-air without surfaces or a result: TRACE-ENDS fires for both named traces, nothing else
	CHECK(kinds(vs) == std::vector<std::string>{"TRACE-ENDS", "TRACE-ENDS"});
}

TEST_CASE("invariants: deliberate violations are detected") {
	TracePath physical;
	physical.steps = {step(V2{100, 100}, V2{500, 100}, true), step(V2{520, 100}, V2{1920, 100}, true)}; // a 20 px gap between hits
	std::vector<TraceStep> typed = {physical.steps[0].with_type(StepType::DivergedPhysical), physical.steps[1].with_type(StepType::Aligned)};
	Input in;
	in.player = V2{100, 100};
	in.cursor = V2{300, 100};
	in.physical = &physical;
	in.planned = &physical;
	in.typed = &typed;
	const Violations vs = check_all(in);
	const auto k = kinds(vs);
	auto has = [&](const char *name) { return std::find(k.begin(), k.end(), name) != k.end(); };
	CHECK(has("NOGAPS"));
	CHECK(has("PHYSICAL-CONTINUITY"));
	CHECK(has("GREEN-FROM-PLAYER"));     // the first typed step is yellow
	CHECK(has("SINGLE-DIVERGENCE"));     // yellow then green
	// fewer non-red typed steps than physical steps
	std::vector<TraceStep> short_typed = {physical.steps[0].with_type(StepType::Aligned)};
	Input pm = in;
	pm.typed = &short_typed;
	const auto kp = kinds(check_PHYSICAL_PREVIEW_MATCH(pm));
	CHECK(kp == std::vector<std::string>{"PHYSICAL-PREVIEW-MATCH"});
	// zero-length step with a positive hit parameter
	TracePath zero;
	zero.steps = {step(V2{100, 100}, V2{100, 100}, true)};
	Input z;
	z.player = V2{100, 100};
	z.cursor = V2{300, 100};
	z.physical = &zero;
	CHECK(kinds(check_ORIGIN_NOT_REHIT(z)) == std::vector<std::string>{"ORIGIN-NOT-REHIT"});
	// NaN
	TracePath nanp;
	nanp.steps = {step(V2{std::numeric_limits<float>::quiet_NaN(), 100}, V2{200, 100}, false)};
	Input n;
	n.player = V2{100, 100};
	n.cursor = V2{300, 100};
	n.physical = &nanp;
	CHECK(kinds(check_S16(n)) == std::vector<std::string>{"S16"});
	// UX7: no line while the cursor differs from the player
	Input u;
	u.player = V2{100, 100};
	u.cursor = V2{300, 100};
	CHECK(kinds(check_UX7(u)) == std::vector<std::string>{"UX7"});
}

TEST_CASE("invariants: display paths and merged steps of a core path") {
	TracePath raw;
	raw.steps = {step(V2{100, 100}, V2{200, 100}, true)};
	TraceStep esc = TraceStep::make(V2{200, 100}, V2::inf(), 0, std::nullopt, Ray{V2{100, 100}, V2{100, 100}, V2{300, 100}}, V2::inf(), false);
	raw.steps.push_back(esc);
	const std::vector<inv::SurfaceInfo> surfaces;
	const TracePath disp = sweep::prepare_for_display(raw, Rect2f::of(0, 0, 1920, 1080), surfaces);
	REQUIRE(disp.steps.size() == 2);
	CHECK(disp.steps[1].end == V2{1920.0f, 100.0f});
	CHECK(!disp.steps[1].hit.has_value());
	const std::vector<TraceStep> merged = sweep::merge(disp.steps, disp.steps, 1);
	REQUIRE(merged.size() == 2);
	CHECK(merged[0].type == StepType::Aligned);
	CHECK(merged[1].type == StepType::AlignedPostPlanned);
}
