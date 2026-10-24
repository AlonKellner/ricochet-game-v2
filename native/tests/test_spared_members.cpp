// Stage 140 (GAME_SPEC 15.3, the cast-cost program's Stage D): two rules that
// spare the region's members -- the points of a surface that is never a
// candidate (and that no plan entry names) are no members, and a plan the player
// cannot start builds an empty region at once. Both compared against the full
// build (vis::spare_members() off) on the digest's set of regions.
#include "goldens.hpp"
#include "json_defs.hpp"
#include "rc/region_digest.hpp"
#include "rc/tracer.hpp"
#include "rc/visibility.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace rc;
using namespace rc::test;

namespace {

struct SpareOff {
	bool was;
	SpareOff() : was(vis::spare_members()) { vis::spare_members() = false; }
	~SpareOff() { vis::spare_members() = was; }
};

// The region's shape, member-independent: the loops' areas (sorted), the membership
// of a grid of points (even-odd over the loops), and the whiskers' total length.
// The members a rule spares used to split the loops' edges at their crossings, so
// the edges themselves are not compared piece by piece.
struct Shape {
	std::vector<double> areas;
	std::vector<bool> inside;
	double whisker_len = 0.0;
	size_t loops = 0;
};

Shape shape_of(const vis::Result &r) {
	Shape s;
	s.loops = r.loops.size();
	for (const vis::Loop &l : r.loops) {
		s.areas.push_back(std::round(std::abs(l.area) * 1e3) / 1e3);
	}
	std::sort(s.areas.begin(), s.areas.end());
	const vis::Rect c = r.clip;
	for (int i = 0; i <= 40; i++) {
		for (int j = 0; j <= 24; j++) {
			const vis::Pt p{c.x0 + (c.x1 - c.x0) * (double(i) + 0.37) / 41.0, c.y0 + (c.y1 - c.y0) * (double(j) + 0.41) / 25.0};
			bool in = false;
			for (const vis::Loop &l : r.loops) {
				in = in != vis::inside_loop(l.edges, p);
			}
			s.inside.push_back(in);
		}
	}
	for (const vis::Edge &e : r.whiskers) {
		s.whisker_len += vis::norm(e.b - e.a);
	}
	s.whisker_len = std::round(s.whisker_len * 1e3) / 1e3;
	return s;
}

bool same_shape(const Shape &a, const Shape &b) {
	return a.loops == b.loops && a.areas == b.areas && a.inside == b.inside && a.whisker_len == b.whisker_len;
}

std::vector<std::vector<PlanItem>> plans_of(const std::vector<SurfaceDef> &defs) {
	std::vector<std::vector<PlanItem>> plans{{}};
	for (const SurfaceDef &d : defs) {
		if (d.pre_transform) {
			continue;
		}
		for (const bool left : {true, false}) {
			const std::optional<SideDef> &side = left ? d.left : d.right;
			if (side && (is_transformative(side->type) || is_projective(side->type) || !side->states.empty())) {
				plans.push_back({PlanItem{d.id, left, std::nullopt, std::nullopt}});
			}
		}
	}
	return plans;
}

} // namespace

TEST_CASE("spared members: the Stage 140 rules leave every region's loops as the full build's, on the test levels") {
	const std::string dir = std::string(RC_REPO_ROOT) + "/bench/scenes";
	int regions = 0, empty_early = 0, members_full = 0, members_spared = 0;
	for (const auto &f : std::filesystem::directory_iterator(dir)) {
		const std::string name = f.path().filename().string();
		if (name.rfind("test_levels__", 0) != 0) {
			continue;
		}
		const ojson d = load_json("bench/scenes/" + name);
		const std::vector<SurfaceDef> defs = defs_from_json(d["surfaces"]);
		Tracer t;
		t.set_defs(defs);
		for (const std::vector<PlanItem> &plan : plans_of(defs)) {
			for (const vis::Pt player : {vis::Pt{500, 300}, vis::Pt{960, 540}}) {
				const vis::Result spared = vis::compute(t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
				vis::Result full;
				{
					SpareOff off;
					full = vis::compute(t, player, plan, Flags(), vis::Rect{0, 0, 1920, 1080});
				}
				regions++;
				const std::string where = name + " plan " + (plan.empty() ? std::string("[]") : std::to_string(plan[0].surface_id) + (plan[0].left ? "L" : "R"));
				CHECK_MESSAGE(spared.supported == full.supported, where);
				CHECK_MESSAGE(same_shape(shape_of(spared), shape_of(full)), where); // the same loops and whiskers
				CHECK_MESSAGE(spared.members.size() <= full.members.size(), where);
				members_full += int(full.members.size());
				members_spared += int(spared.members.size());
				if (spared.members.empty() && !full.members.empty()) {
					// the early empty: the full build found nothing either
					CHECK_MESSAGE(full.cells.empty(), where);
					CHECK_MESSAGE(full.loops.empty(), where);
					empty_early++;
				}
			}
		}
	}
	CHECK(regions > 100);
	CHECK(empty_early > 10);               // the rule fires on the test levels' unreachable sides
	CHECK(members_spared < members_full);  // and the never-candidate points are gone
	MESSAGE("regions " << regions << ", empty at once " << empty_early << ", members " << members_full << " -> " << members_spared);
}

TEST_CASE("spared members: a pure pass-through's points are no members, a planned one's are") {
	SurfaceDef wall;
	wall.id = 1;
	wall.sx = 100;
	wall.sy = -500;
	wall.ex = 100;
	wall.ey = 500;
	wall.vx = 100;
	wall.vy = 0;
	SideDef term;
	term.type = EffectType::Terminal;
	wall.left = term;
	wall.right = term;
	SurfaceDef pass = wall;
	pass.id = 2;
	pass.sx = -300;
	pass.sy = 200;
	pass.ex = 300;
	pass.ey = 200;
	pass.vx = 0;
	pass.vy = 200;
	SideDef p;
	p.type = EffectType::Pass;
	pass.left = p;
	pass.right = p;
	Tracer t;
	t.set_defs({wall, pass});
	const vis::Result r = vis::compute(t, vis::Pt{0, 0}, {}, Flags(), vis::Rect{-1000, -1000, 1000, 1000});
	REQUIRE(r.supported);
	int of_pass = 0, of_wall = 0;
	for (const vis::Member &m : r.members) {
		of_pass += m.why.find("of 2") != std::string::npos || m.why.find("and 2") != std::string::npos ? 1 : 0;
		of_wall += m.why.find("of 1") != std::string::npos ? 1 : 0;
	}
	CHECK(of_pass == 0);
	CHECK(of_wall > 0);
	// named by the plan (a state-less pass entry is consumed quietly): its points are members again
	const vis::Result rp = vis::compute(t, vis::Pt{0, 0}, {PlanItem{2, true, std::nullopt, std::nullopt}}, Flags(), vis::Rect{-1000, -1000, 1000, 1000});
	int of_pass_planned = 0;
	for (const vis::Member &m : rp.members) {
		of_pass_planned += m.why.find("of 2") != std::string::npos ? 1 : 0;
	}
	CHECK(of_pass_planned > 0);
}

TEST_CASE("spared members: a plan the player cannot start is empty at once, and only then") {
	SurfaceDef m;
	m.id = 1;
	m.sx = 300;
	m.sy = -200;
	m.ex = 300;
	m.ey = 200;
	m.vx = 300;
	m.vy = 0;
	SideDef refl;
	refl.type = EffectType::Reflection;
	refl.interactive = true;
	m.left = refl;
	m.right = refl;
	Tracer t;
	t.set_defs({m});
	const Seg<F64> seg{1, SegKind::Line, false, CN<F64>{F64{300}, F64{-200}}, CN<F64>{F64{300}, F64{200}}, std::nullopt, std::nullopt, std::nullopt};
	// the side the player (0, 0) is on: a beam from it approaches the carrier from that side
	const bool player_left = geom::side_left(seg, CN<F64>{F64{300}, F64{0}}, CN<F64>{F64{1}, F64{0}});
	for (const bool left : {true, false}) {
		const vis::Result r = vis::compute(t, vis::Pt{0, 0}, {PlanItem{1, left, std::nullopt, std::nullopt}}, Flags(), vis::Rect{-1000, -1000, 1000, 1000});
		REQUIRE(r.supported);
		if (left == player_left) {
			CHECK(!r.members.empty()); // reachable: built
		} else {
			CHECK(r.members.empty());  // the far side: empty at once
			CHECK(r.casts == 0);
			CHECK(r.cells.empty());
			REQUIRE(!r.notes.empty());
			CHECK(r.notes.back().find("cannot be the player's first hit") != std::string::npos);
		}
		// an infinity entry first: the beam comes back from the far side
		const vis::Result w = vis::compute(t, vis::Pt{0, 0}, {PlanItem{INFINITY_ID, true, std::nullopt, std::nullopt}, PlanItem{1, left, std::nullopt, std::nullopt}}, Flags(),
			vis::Rect{-1000, -1000, 1000, 1000});
		CHECK(w.members.empty() == (left == player_left));
	}
}
