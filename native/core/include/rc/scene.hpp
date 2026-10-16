// A scene for the beam solver (Stage 93): segment surfaces with per-side
// effect types at one precision tier, built from Variant-free scene defs (the
// JSON-able description the game dumps and the oracle traces). Port of
// scripts/math/precision/beam_scene.gd.
#pragma once

#include "rc/beam_geom.hpp"
#include "rc/exact.hpp"
#include "rc/flags.hpp"
#include "rc/error.hpp"

#include <array>
#include <cmath>
#include <map>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rc {

enum class EffectType { Pass, Terminal, Reflection, Inversion, Portal, LineNormalProjection, CircleNormalProjection,
	LineDirectionalProjection, CircleDirectionalProjection };

constexpr bool is_transformative(EffectType t) {
	return t == EffectType::Reflection || t == EffectType::Inversion || t == EffectType::Portal;
}
constexpr bool is_projective(EffectType t) {
	return t == EffectType::LineNormalProjection || t == EffectType::CircleNormalProjection
		|| t == EffectType::LineDirectionalProjection || t == EffectType::CircleDirectionalProjection;
}
constexpr bool is_categorical_type(EffectType t) {
	return t == EffectType::Pass || t == EffectType::Terminal || t == EffectType::Reflection || t == EffectType::Inversion;
}
EffectType effect_type_from_json(const std::string &name);
const char *effect_type_json_name(EffectType t);

// Projective side parameters: [back, nx, ny] (normal for directional types).
struct Projective {
	bool back = false;
	double nx = 0.0;
	double ny = 0.0;
};

// One side's config as the defs carry it ({type, interactive, back, normal, theta, d, pair, state}).
struct SideDef {
	EffectType type = EffectType::Pass;
	bool interactive = false;
	bool back = false;
	bool has_normal = false;
	double nx = 0.0, ny = 0.0;
	double theta = 0.0, dx = 0.0, dy = 0.0; // portal
	int pair = -1;                          // portal: the paired surface id (-1: none)
	std::vector<StateChangeDef> states;     // Stage 107: the side's state changes, in order
	bool operator==(const SideDef &) const = default;
};

// Stage 107: a presence condition {key, values}: the surface exists while
// flag_key(flags[key]) is one of `values` ("DEFAULT" = the key is missing).
struct PresenceCond {
	std::string key;
	std::vector<std::string> values;
	bool operator==(const PresenceCond &) const = default;
};

// A categorical resolver: {key, table: flag_key -> {left, right}} in table order.
struct ResolverDef {
	std::string key;
	std::vector<std::pair<std::string, std::pair<std::optional<SideDef>, std::optional<SideDef>>>> table;
	bool operator==(const ResolverDef &) const = default;
};

// {id, line, start, end, via, full, circle: {center, radius} | null, left, right, is_target, resolver}
struct SurfaceDef {
	int id = 0;
	bool line = true;
	double sx = 0, sy = 0, ex = 0, ey = 0;
	bool has_via = true;
	double vx = 0, vy = 0;
	bool full = false;
	bool has_circle = false; // full circle with an exact float64 center / radius
	double cx = 0, cy = 0, radius = 0;
	std::optional<SideDef> left;
	std::optional<SideDef> right;
	bool is_target = false;
	std::optional<ResolverDef> resolver;
	// Stage 107: flag-gated existence, all conditions required (empty: always)
	std::vector<PresenceCond> presence;
	// Stage 101: part of the scene's pre-transform (a full inversion circle,
	// never hit): it moves infinity before the shot
	bool pre_transform = false;
	bool operator==(const SurfaceDef &) const = default;
};

// The reserved per-shot flag of ghost mode.
inline const std::string GHOST = "ghost";

template <Num N>
struct Surf {
	int id = 0;
	Seg<N> seg;
	EffectType left_type = EffectType::Pass;
	EffectType right_type = EffectType::Pass;
	bool is_target = false;
	// raw float64 geometry: line s -> e, or circle (cx, cy, r)
	bool line = true;
	double sx = 0, sy = 0, ex = 0, ey = 0;
	double cx = 0, cy = 0, r = 0, r2 = 0;
	// portal: rigid motion a z + d (a at the tier, its float64 twin pa + i pb), the pair id
	bool portal = false;
	CN<N> alpha;
	CN<N> dvec;
	double pa = 1.0, pb = 0.0, dx = 0.0, dy = 0.0, theta = 0.0;
	int pair = -1;
	Projective left_proj;
	Projective right_proj;
	bool has_v = false;
	double vx = 0, vy = 0;
	bool full = false;
	// state changes per side (in order, Stage 107), and the categorical table
	// (flag_key -> [left_type, right_type, left_states, right_states], in table order)
	std::vector<StateChangeDef> left_states;
	std::vector<StateChangeDef> right_states;
	std::string state_key;
	struct TableEntry {
		EffectType left_type;
		EffectType right_type;
		std::vector<StateChangeDef> left_states;
		std::vector<StateChangeDef> right_states;
	};
	// Stage 107: presence conditions (all required; empty: always present)
	std::vector<PresenceCond> presence;

	// Whether the surface exists under the flags (GAME_SPEC 9.6).
	bool present(const Flags &flags) const {
		for (const PresenceCond &c : presence) {
			const FlagValue *v = flags.get(c.key);
			const std::string k = v ? flag_key(*v) : std::string("DEFAULT");
			if (std::find(c.values.begin(), c.values.end(), k) == c.values.end()) {
				return false;
			}
		}
		return true;
	}
	std::vector<std::pair<std::string, TableEntry>> table;

	// the table entry under the flags (flag_key; "DEFAULT", then the first entry), nullptr for a fixed surface
	const TableEntry *entry(const Flags &flags) const {
		if (state_key.empty()) {
			return nullptr;
		}
		if (const FlagValue *v = flags.get(state_key)) {
			const std::string k = flag_key(*v);
			for (const auto &row : table) {
				if (row.first == k) {
					return &row.second;
				}
			}
		}
		for (const auto &row : table) {
			if (row.first == "DEFAULT") {
				return &row.second;
			}
		}
		return &table.front().second;
	}

	EffectType effect(bool left, const Flags &flags = Flags()) const {
		if (ghosted(left, flags)) {
			return EffectType::Pass;
		}
		if (const TableEntry *e = entry(flags)) {
			return left ? e->left_type : e->right_type;
		}
		return left ? left_type : right_type;
	}

	const std::vector<StateChangeDef> &base_states(bool left, const Flags &flags) const {
		if (const TableEntry *e = entry(flags)) {
			return left ? e->left_states : e->right_states;
		}
		return left ? left_states : right_states;
	}

	// The side's state changes under the flags, in order (none while ghosted).
	const std::vector<StateChangeDef> &state_changes(bool left, const Flags &flags = Flags()) const {
		static const std::vector<StateChangeDef> none;
		if (ghosted(left, flags)) {
			return none;
		}
		return base_states(left, flags);
	}

	// Ghost mode (GAME_SPEC 10.6): while the "ghost" flag is true every side
	// but a ghost toggler (one of its state changes writes "ghost") is a pass.
	bool ghosted(bool left, const Flags &flags) const {
		if (!flags.is_true(GHOST)) {
			return false;
		}
		const auto &chs = base_states(left, flags);
		return std::none_of(chs.begin(), chs.end(), [](const StateChangeDef &c) { return c.key == GHOST; });
	}

	const Projective &proj(bool left) const { return left ? left_proj : right_proj; }

	// Exact circle data (cached): homogeneous center [x, y, w], radius² times
	// w² (BigFloat), and their float64 roundings (for certified filters).
	struct ExactCircle {
		exact::Point3 C;
		BigFloat rw2;
		double fcx, fcy, frr;
	};
	const ExactCircle &exact_circle() const {
		if (!exact_circle_) {
			ExactCircle ec;
			if (full) {
				ec.C = {exact::of(cx), exact::of(cy), exact::of(1.0)};
				ec.rw2 = exact::mul(exact::of(r), exact::of(r));
			} else {
				ec.C = exact::circumcenter_h(sx, sy, vx, vy, ex, ey);
				const BigFloat ddx = exact::sub(exact::mul(exact::of(sx), ec.C[2]), ec.C[0]);
				const BigFloat ddy = exact::sub(exact::mul(exact::of(sy), ec.C[2]), ec.C[1]);
				ec.rw2 = exact::add(exact::mul(ddx, ddx), exact::mul(ddy, ddy));
			}
			const double w = ec.C[2].to_float();
			ec.fcx = ec.C[0].to_float() / w;
			ec.fcy = ec.C[1].to_float() / w;
			ec.frr = ec.rw2.to_float() / (w * w);
			exact_circle_ = std::move(ec);
		}
		return *exact_circle_;
	}

private:
	mutable std::optional<ExactCircle> exact_circle_;
};

template <Num N>
struct Scene {
	std::vector<std::shared_ptr<Surf<N>>> surfaces;
	std::map<int, Surf<N> *> by_id;
	// Stage 101: the pre-transform's surfaces, in order (not hittable); their
	// composite image of infinity is the frame the shot starts in
	std::vector<std::shared_ptr<Surf<N>>> pre_surfaces;
	// Stage 88: a straight beam passing t = infinity emits the `infinity` event
	// (counted toward MAX_HITS). Off reproduces the pre-Stage-88 traces; the
	// game turns it on (B9) and so do the Stage 88 fixture tests.
	bool emit_infinity = false;

	const Surf<N> *find(int id) const {
		auto it = by_id.find(id);
		return it == by_id.end() ? nullptr : it->second;
	}

	// `like`: a value of the tier (its limb count for BigFloat).
	static Scene from_defs(const std::vector<SurfaceDef> &defs, const N &like);
};

// True if every side config is one the beam solver implements.
bool scene_supported(const std::vector<SurfaceDef> &defs);

// ---------------------------------------------------------------- construction

template <Num N>
Scene<N> Scene<N>::from_defs(const std::vector<SurfaceDef> &defs, const N &like) {
	Scene sc;
	for (const SurfaceDef &d : defs) {
		auto s = std::make_shared<Surf<N>>();
		s->id = d.id;
		SegDef g;
		g.id = d.id;
		g.sx = d.sx;
		g.sy = d.sy;
		g.ex = d.ex;
		g.ey = d.ey;
		g.has_v = d.has_via;
		g.vx = d.vx;
		g.vy = d.vy;
		g.full = d.full;
		if (d.line) {
			g.kind = SegKind::Line;
		} else if (d.has_circle) {
			g.kind = SegKind::Circle;
			g.full = true;
			g.cx = d.cx;
			g.cy = d.cy;
			g.r = d.radius;
		} else {
			g.kind = SegKind::Circle;
		}
		s->seg = geom::seg_from_def(g, like);
		s->line = s->seg.kind == SegKind::Line;
		s->full = s->seg.full;
		if (s->seg.v) {
			s->has_v = true;
			s->vx = s->seg.v->fx();
			s->vy = s->seg.v->fy();
		}
		s->sx = d.sx;
		s->sy = d.sy;
		s->ex = d.ex;
		s->ey = d.ey;
		if (!s->line) {
			// raw circle: the tier's carrier rounded to float64
			s->cx = s->seg.circ->c.fx();
			s->cy = s->seg.circ->c.fy();
			s->r = s->seg.circ->r.to_float();
			s->r2 = s->r * s->r;
		}
		s->left_type = d.left ? d.left->type : EffectType::Pass;
		s->right_type = d.right ? d.right->type : EffectType::Pass;
		s->left_states = d.left ? d.left->states : std::vector<StateChangeDef>();
		s->right_states = d.right ? d.right->states : std::vector<StateChangeDef>();
		s->presence = d.presence;
		RC_ASSERT(d.presence.empty() || !d.pre_transform, "a pre-transform surface is always present");
		if (d.resolver) {
			s->state_key = d.resolver->key;
			for (const auto &row : d.resolver->table) {
				const auto &l = row.second.first;
				const auto &r = row.second.second;
				RC_ASSERT(is_categorical_type(l ? l->type : EffectType::Pass) && is_categorical_type(r ? r->type : EffectType::Pass),
					"categorical sides: pass / terminal / reflection / inversion");
				s->table.emplace_back(row.first, typename Surf<N>::TableEntry{l ? l->type : EffectType::Pass,
					r ? r->type : EffectType::Pass, l ? l->states : std::vector<StateChangeDef>(),
					r ? r->states : std::vector<StateChangeDef>()});
			}
		}
		for (int side = 0; side < 2; side++) {
			const std::optional<SideDef> &pc = side == 0 ? d.left : d.right;
			if (pc && is_projective(pc->type)) {
				Projective pr{pc->back, pc->has_normal ? pc->nx : 0.0, pc->has_normal ? pc->ny : 0.0};
				(side == 0 ? s->left_proj : s->right_proj) = pr;
			}
		}
		for (int side = 0; side < 2; side++) {
			const std::optional<SideDef> &cfg = side == 0 ? d.left : d.right;
			if (cfg && cfg->type == EffectType::Portal) {
				s->portal = true;
				s->theta = cfg->theta;
				s->dx = cfg->dx;
				s->dy = cfg->dy;
				s->pair = cfg->pair;
				s->alpha = CN<N>{like.like(std::cos(s->theta)), like.like(std::sin(s->theta))}.unit();
				s->dvec = CN<N>{like.like(s->dx), like.like(s->dy)};
				const double c = std::cos(s->theta);
				const double sn = std::sin(s->theta);
				const double ab = std::sqrt(c * c + sn * sn);
				s->pa = c / ab;
				s->pb = sn / ab;
			}
		}
		s->is_target = d.is_target;
		if (d.pre_transform) {
			sc.pre_surfaces.push_back(std::move(s));
			continue;
		}
		sc.by_id[s->id] = s.get();
		sc.surfaces.push_back(std::move(s));
	}
	return sc;
}

} // namespace rc
