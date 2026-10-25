#include "rcg/session.hpp"

#include "rcg/digest.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>

namespace rcg {
namespace {

using json = nlohmann::json;
using Color = std::array<float, 4>;

constexpr float CLICK_TOLERANCE = 24.0f;
constexpr double SNAP_PX = 8.0;
constexpr double ARROW_SPEED = 1600.0;
const Color HOVER_COLOR{1.0f, 1.0f, 1.0f, 0.3f};
const Color PLAN_VALID_COLOR{1.0f, 1.0f, 1.0f, 0.15f};
const Color PLAN_INVALID_COLOR{1.0f, 0.0f, 0.0f, 0.15f};
const Color WHITE{1, 1, 1, 1};

json color_json(const Color &c) { return json::array({double(c[0]), double(c[1]), double(c[2]), double(c[3])}); }

Color step_color(rc::StepType t) {
	switch (t) {
		case rc::StepType::Aligned:
		case rc::StepType::AlignedPostPlanned:
			return {0, 1, 0, 1};
		case rc::StepType::DivergedPhysical:
			return {1, 1, 0, 1};
		case rc::StepType::DivergedPlanned:
		case rc::StepType::DivergedPostPlanned:
			return {1, 0, 0, 1};
	}
	return WHITE;
}

json flags_json(const rc::Flags &f) {
	json out = json::object();
	for (const auto &[k, v] : f.entries()) {
		out[k] = std::visit([](const auto &x) -> json { return x; }, v);
	}
	return out;
}

bool has_area(const rc::Rect2f &r) { return r.size.x > 0.0f && r.size.y > 0.0f; }

// ClickDetector.edge_distance: float64 on the components (Rect2.end in float32)
double edge_distance(const V2 &p, const rc::Rect2f &b) {
	const V2 end = b.end();
	return std::min(std::min(double(p.x) - double(b.position.x), double(end.x) - double(p.x)),
		std::min(double(p.y) - double(b.position.y), double(end.y) - double(p.y)));
}

// ArrowAdvance.start_direction
V2 start_direction(const rc::TraceStep &s) {
	const rc::arc::StepGeom g = rc::sweep::step_geom(s);
	if (rc::arc::step_is_arc(g)) {
		return rc::arc::arc_interpolate(g, 0.0).direction;
	}
	return (g.end - g.start).normalized();
}

json step_json(const rc::TraceStep &s) {
	return json::array({digest::vec(s.start), digest::vec(s.end), digest::vec(s.via), s.is_arc_step, int(s.type), s.surface_id, s.hit_side,
		s.hit_on_segment, s.after_portal, s.ghost});
}

json steps_json(const std::vector<rc::TraceStep> &steps) {
	json out = json::array();
	for (const rc::TraceStep &s : steps) {
		out.push_back(step_json(s));
	}
	return out;
}

json overlay_json(const Overlay &o) {
	return json::array({o.side, o.has_incoming, o.has_outgoing, o.outgoing_side, color_json(o.incoming_color), color_json(o.outgoing_color),
		color_json(o.gradient_color), o.piece});
}

json events_json(const rc::Climb &c) {
	json out = json::array();
	auto add = [&](const auto &events) {
		for (const auto &e : events) {
			out.push_back(json::array({rc::event_kind_name(e.kind), e.surface_id, e.applied}));
		}
	};
	if (c.limbs == 0) {
		add(c.f64_events);
	} else {
		add(c.big_events);
	}
	return out;
}

// Stage 132: the consumed origin entries of a walk (applied passages of the ray's origin)
int count_origin(const rc::Climb &c) {
	int n = 0;
	auto add = [&](const auto &events) {
		for (const auto &e : events) {
			n += e.kind == rc::EventKind::Origin && e.applied ? 1 : 0;
		}
	};
	if (c.limbs == 0) {
		add(c.f64_events);
	} else {
		add(c.big_events);
	}
	return n;
}

int count_infinity(const rc::Climb &c) {
	int n = 0;
	auto add = [&](const auto &events) {
		for (const auto &e : events) {
			n += std::string(rc::event_kind_name(e.kind)) == "infinity" ? 1 : 0;
		}
	};
	if (c.limbs == 0) {
		add(c.f64_events);
	} else {
		add(c.big_events);
	}
	return n;
}

std::string display_name(const BuiltEffect &e) {
	if (e.type == "reflection") return "reflect";
	if (e.type == "terminal") return "block";
	return e.type;
}

} // namespace

std::optional<Action> action_of(const std::string &name) {
	if (name == "undo") return Action::Undo;
	if (name == "reset") return Action::Reset;
	if (name == "clear_plan") return Action::ClearPlan;
	if (name == "cycle_display") return Action::CycleDisplay;
	if (name == "toggle_gravity") return Action::ToggleGravity;
	if (name == "toggle_visibility") return Action::ToggleVisibility;
	if (name == "any") return Action::Any;
	if (name == "fire") return Action::Fire;
	return std::nullopt;
}

Session::Session(const Level &level, const SessionConfig &config)
	: level_(level), visibility_override_(config.visibility), async_(config.async), budget_(config.budget) {
	world_ = std::make_unique<World>(build_level(level_), level_.objects);
	if (!level_.motions.empty()) { // Stage 151: the moving surfaces, from their base specs
		std::vector<World::Moving> moving;
		for (size_t m = 0; m < level_.motions.size(); m++) {
			const Motion &mo = level_.motions[m];
			const SurfaceSpec &first = level_.surfaces[mo.surfaces.front()].spec;
			const V2 pivot = mo.pivot ? *mo.pivot : first.full_circle ? first.center : V2::of((double(first.start.x) + first.end.x) / 2, (double(first.start.y) + first.end.y) / 2);
			for (const size_t k : mo.surfaces) {
				const LevelSurface &ls = level_.surfaces[k];
				moving.push_back({ls.ids, ls.spec, ls.label});
				moving_motion_.push_back(m);
				moving_pivot_.push_back(pivot);
			}
		}
		world_->set_moving(std::move(moving));
		apply_phase();
	}
	player_ = level_.player_start.value_or(V2{});
	tick_from_ = player_;
	settled_player_ = player_; // (a new session is standing still)
	gravity_ = level_.gravity;
	for (rc::Tracer *t : {&gm_tracer_, &renderer_tracer_, &vis_tracer_}) {
		t->set_pair_cache(false); // the session caches by its own inputs
	}
	// rc-level/2: the flags the level starts with (none in an rc-level/1 level)
	for (const auto &[key, value] : level_.flags) {
		flags_.set(key, value);
	}
	spread(); // (Stage 152: a powered circuit is lit from the start)
	if (!level_.shades.empty()) { // Stage 153: the shades' regions, their boundary's pieces (ids taken once)
		for (const Shade &sh : level_.shades) {
			shade_regions_.push_back(shade::region_of(segment_of(level_.surfaces[sh.surface].spec), sh.side, sh.dark));
		}
		shade_pieces_ = shade::arrange(shade_regions_, level_.bounds);
		shade_first_id_ = world_->reserve_ids(int(shade_pieces_.size()));
		shadow();
	}
	// the initial checkpoint: taken before the level built its world (GameManager._ready)
	initial_ = Checkpoint{player_, V2{}, flags_, {}, {}, std::nullopt, false, {}, 0.0, true};
	hud_ = hud_text(); // (Stage 145: a goal's line; "" without a goal and a plan)
	for (const BuiltSurface &s : world_->surfaces()) {
		for (const BuiltSide *side : s.all_sides()) {
			for (const HitSpec &h : side ? side->hits : std::vector<HitSpec>{}) {
				spawn_preview_ = spawn_preview_ || h.kind == "spawn";
			}
		}
	}
	sync_nodes();
	if (async_) {
		// each job keeps its own tracer (its caches, its helpers), its defs swapped when the world changes
		auto tracer_for = [](rc::Tracer &t, int &held, int world, const std::vector<rc::SurfaceDef> &defs) {
			if (held != world) {
				t.set_pair_cache(false);
				t.set_defs(defs);
				held = world;
			}
		};
		auto vt = std::make_shared<std::pair<rc::Tracer, int>>();
		vt->second = -1;
		// the region: a running one finishes (it shows while the newest computes)
		vis_job_ = std::make_unique<LatestJob<VisJobIn, VisJobOut>>(
			[vt, tracer_for](const VisJobIn &in, const std::atomic<bool> &cancel) {
				tracer_for(vt->first, vt->second, in.world, *in.defs);
				return run_visibility(vt->first, in.player, in.items, in.flags, in.bounds, &cancel);
			},
			false);
		auto pt = std::make_shared<std::pair<rc::Tracer, int>>();
		pt->second = -1;
		// the prefetch: a superseded one is cancelled (it must not hold the pool from real work)
		if (config.prefetch) {
			auto ft = std::make_shared<std::pair<rc::Tracer, int>>();
			ft->second = -1;
			prefetch_job_ = std::make_unique<LatestJob<VisJobIn, VisJobOut>>([ft, tracer_for](const VisJobIn &in, const std::atomic<bool> &cancel) {
				tracer_for(ft->first, ft->second, in.world, *in.defs);
				return run_visibility(ft->first, in.player, in.items, in.flags, in.bounds, &cancel);
			});
		}
		// the exact preview: only the current inputs' is applied (a stale one is dropped)
		preview_job_ = std::make_unique<LatestJob<PreviewJobIn, PreviewJobOut>>(
			[pt, tracer_for](const PreviewJobIn &in, const std::atomic<bool> &) {
				const auto t0 = std::chrono::steady_clock::now();
				tracer_for(pt->first, pt->second, in.world, *in.defs);
				PreviewJobOut out{pt->first.trace_pair(in.player, in.cursor, in.items, in.flags), 0.0};
				out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
				return out;
			},
			false);
	}
}

std::shared_ptr<const std::vector<rc::SurfaceDef>> Session::defs_snapshot() {
	if (job_defs_world_ != world_->version()) {
		job_defs_ = std::make_shared<const std::vector<rc::SurfaceDef>>(world_->defs());
		job_defs_world_ = world_->version();
	}
	return job_defs_;
}

bool Session::region_pending() const { return vis_job_ && (vis_applied_ < vis_ticket_ || vis_job_->busy() || vis_job_->ready()); }

void Session::collect_jobs() {
	if (preview_job_) {
		if (auto r = preview_job_->take(); r && r->first == preview_ticket_) {
			climb_times_.add(r->second.seconds);
			preview_climb_seconds_ = r->second.seconds;
			make_preview(preview_, std::move(r->second.pair));
			provisional_.reset();
			provisional_version_++;
			trace_version_++;
		}
	}
	if (adopted_) {
		// a prefetch that became a click's region (older than any region job's request)
		if (auto r = prefetch_job_->take()) {
			if (r->first == adopted_->first) {
				apply_region(adopted_->second, std::move(r->second));
			}
			adopted_.reset();
		} else if (!prefetch_job_->busy()) {
			adopted_.reset();
		}
	}
	if (vis_job_) {
		if (auto r = vis_job_->take()) {
			apply_region(r->first, std::move(r->second));
		}
	}
}

// a newer region than the one shown (the newest request's, or one before it)
void Session::apply_region(long long ticket, VisJobOut &&out) {
	if (ticket <= vis_applied_ || ticket > vis_ticket_) {
		return;
	}
	vis_applied_ = ticket;
	if (out.region) {
		region_times_.add(double(out.region->build_usec) / 1e6); // (how long a region takes now: the budget's guess)
	}
	region_ = std::move(out.region);
	outline_ = std::move(out.outline);
	fill_ = std::move(out.fill);
	whiskers_ = std::move(out.whiskers);
	visibility_version_++;
}

void Session::drain() {
	for (int round = 0; round < 64; round++) {
		if (preview_job_) {
			preview_job_->wait_idle();
		}
		if (vis_job_) {
			vis_job_->wait_idle();
		}
		if (prefetch_job_) {
			prefetch_job_->wait_idle(); // (a hovered entry's region: ready for its click)
		}
		settle();
		if (!(preview_job_ && (preview_job_->busy() || preview_job_->ready())) && !region_pending() && !(prefetch_job_ && prefetch_job_->busy())) {
			return;
		}
	}
}

void Session::use_defs(rc::Tracer &t, int &held) {
	if (held != world_->version()) {
		t.set_defs(world_->defs());
		held = world_->version();
	}
}

bool Session::visibility_on() const { return visibility_override_.value_or(true); }
rc::Rect2f Session::bounds() const { return rc::Rect2f::of(level_.bounds.x, level_.bounds.y, level_.bounds.w, level_.bounds.h); }

std::vector<rc::PlanItem> Session::items(size_t n) const {
	std::vector<rc::PlanItem> out;
	for (size_t i = 0; i < n && i < plan_.size(); i++) {
		out.push_back(item_of(plan_[i]));
	}
	return out;
}

rc::PlanItem Session::item_of(const PlanEntry &e) {
	rc::PlanItem p;
	p.surface_id = e.surface_id;
	p.left = e.side == Side::Left;
	if (std::isfinite(e.click.x) && std::isfinite(e.click.y)) {
		p.hint = rc::effects::F32Point{e.click.x, e.click.y};
	}
	p.span = e.span;
	return p;
}

// Stage 121: while no region is computing, the region the hovered entry would
// make (a left click's entry there), so its click shows it at once
void Session::update_prefetch() {
	// (not while the player moves -- still for 100 ms: the prefetch would be stale at
	// once, and its casts would hold the pool from the region the next frame needs)
	if (!prefetch_job_ || std::chrono::steady_clock::now() - moved_at_ < std::chrono::milliseconds(100) || !visibility_on() ||
		!level_.features.preview || region_pending()) {
		return;
	}
	const PrefetchKey key{cursor_, player_, plan_version_, flags_version_, world_->version(), vis_applied_};
	if (key == prefetch_key_) {
		return;
	}
	prefetch_key_ = key;
	const std::optional<PlanEntry> e = entry_for(detect_click());
	if (!e) {
		return;
	}
	std::vector<rc::PlanItem> it = items();
	it.push_back(item_of(*e));
	const rc::Rect2f b = bounds();
	VisJobIn in{defs_snapshot(), world_->version(), player_, std::move(it), flags_,
		rc::vis::Rect{double(b.position.x), double(b.position.y), double(b.position.x) + double(b.size.x), double(b.position.y) + double(b.size.y)}};
	if (!prefetch_in_ || !same_inputs(*prefetch_in_, in)) {
		prefetch_in_ = in;
		prefetch_job_->submit(std::move(in), ++prefetch_ticket_);
	}
}

std::optional<PlanEntry> Session::entry_for(const Hit &r) const {
	if (!r.infinity && !r.has_surface) {
		return std::nullopt;
	}
	const int sid = r.infinity ? rc::INFINITY_ID : r.surface;
	const Side side = r.infinity ? Side::Left : r.side;
	V2 click = cursor_;
	if (!r.infinity) {
		click = snap_click(*world_->by_id(r.surface), side, click);
	}
	PlanEntry e{sid, side, click, -1, std::nullopt};
	if (r.piece) {
		e.piece = r.piece->index;
		e.span = r.piece->span;
	}
	return e;
}

bool Session::same_inputs(const VisJobIn &a, const VisJobIn &b) {
	// exact: the same bits (a -0 is not a 0 here)
	auto same_f = [](double x, double y) { return std::memcmp(&x, &y, sizeof x) == 0; };
	auto same_v = [&](const V2 &x, const V2 &y) { return same_f(x.x, y.x) && same_f(x.y, y.y); };
	if (a.world != b.world || !same_v(a.player, b.player) || a.items.size() != b.items.size()
		|| digest::encode(flags_json(a.flags)) != digest::encode(flags_json(b.flags))) {
		return false;
	}
	for (size_t i = 0; i < a.items.size(); i++) {
		const rc::PlanItem &x = a.items[i], &y = b.items[i];
		if (x.surface_id != y.surface_id || x.left != y.left || x.hint.has_value() != y.hint.has_value() || x.span.has_value() != y.span.has_value()) {
			return false;
		}
		if (x.hint && !(same_f(x.hint->x, y.hint->x) && same_f(x.hint->y, y.hint->y))) {
			return false;
		}
		for (size_t k = 0; x.span && k < 6; k++) {
			if (!same_f((*x.span)[k], (*y.span)[k])) {
				return false;
			}
		}
	}
	return same_f(a.bounds.x0, b.bounds.x0) && same_f(a.bounds.y0, b.bounds.y0) && same_f(a.bounds.x1, b.bounds.x1) && same_f(a.bounds.y1, b.bounds.y1);
}

std::vector<rc::inv::SurfaceInfo> Session::infos() const {
	std::vector<rc::inv::SurfaceInfo> out;
	for (const BuiltSurface &s : world_->surfaces()) {
		rc::inv::SurfaceInfo i;
		i.id = s.id;
		i.s = s.seg.s;
		i.e = s.seg.e;
		i.v = s.seg.v;
		i.full = s.seg.full;
		i.carrier = s.seg.get_carrier();
		out.push_back(i);
	}
	return out;
}

void Session::teleport(const V2 &p) {
	player_ = p;
	tick_from_ = p;
	velocity_ = V2{};
}

std::optional<V2> Session::pole(size_t n) {
	const std::array<int, 3> key{plan_version_, flags_version_, world_->version()};
	if (key != pole_key_) {
		pole_key_ = key;
		pole_cache_.assign(plan_.size() + 1, std::nullopt);
	}
	if (n < pole_cache_.size() && pole_cache_[n]) {
		return *pole_cache_[n];
	}
	use_defs(gm_tracer_, gm_defs_);
	const std::optional<V2> q = gm_tracer_.infinity_point(items(n), flags_);
	if (n < pole_cache_.size()) {
		pole_cache_[n] = q;
	}
	return q;
}

rc::Tracer::OriginPoint Session::origin_at(size_t n) {
	use_defs(gm_tracer_, gm_defs_);
	return gm_tracer_.origin_point(player_, cursor_, items(n), flags_);
}

// the origin's marker: where the plan so far leaves the walk behind it, on screen
std::optional<V2> Session::plannable_origin() {
	const rc::Tracer::OriginPoint o = origin_at(plan_.size());
	const rc::Rect2f b = bounds();
	if (!o.behind || !o.point || (has_area(b) && !b.has_point(*o.point))) {
		return std::nullopt;
	}
	return o.point;
}

void Session::set_flags(const rc::Flags &f) {
	if (digest::encode(flags_json(f)) != digest::encode(flags_json(flags_))) {
		flags_version_++;
	}
	flags_ = f;
}

// ClickDetector.detect_click (with the game manager's accept, bounds, switch, pole, pieces)
Session::Hit Session::detect_click() {
	Hit best;
	double best_dist = INFINITY;
	const Pieces &pieces = world_->pieces();
	use_defs(gm_tracer_, gm_defs_);
	const std::vector<rc::PlanItem> plan = items();
	for (const BuiltSurface &s : world_->surfaces()) {
		if (s.pre_transform) {
			continue;
		}
		for (const Piece &p : pieces.of(s.id)) {
			const double dist = p.segment.distance_to(cursor_);
			if (dist > CLICK_TOLERANCE) {
				continue;
			}
			const Side side = p.segment.determine_side(cursor_) == rc::Side::Left ? Side::Left : Side::Right;
			const BuiltSide *cfg = active_side(s, side, flags_);
			// Stage 158: a stopping side is plannable while the plan's frost is active (frost turns it)
			const bool frost = cfg != nullptr && !cfg->interactive && cfg->effect.type == "terminal" && !cfg->rigid
				&& gm_tracer_.plan_frost_turns(plan, s.id, side == Side::Left, flags_);
			if (cfg == nullptr || (!cfg->interactive && !frost)) {
				continue;
			}
			if (gm_tracer_.plan_refuses(plan, s.id, side == Side::Left, flags_)) {
				continue;
			}
			if (dist < best_dist) {
				best_dist = dist;
				best = Hit{};
				best.has_surface = true;
				best.surface = s.id;
				best.side = side;
				best.piece = &p;
				best.piece_index = p.index;
				best.distance = dist;
				best.frost = frost;
			}
		}
	}
	// Stage 132: the ray's origin, where plannable: like a surface, the nearest wins;
	// else a planned origin entry's mark (a right-click there removes the latest)
	if (const std::optional<V2> o = plannable_origin()) {
		const float d = cursor_.distance_to(*o);
		if (d <= CLICK_TOLERANCE && double(d) < best_dist) {
			best = Hit{};
			best.origin = true;
			best.point = o;
			best.distance = d;
			best_dist = d;
		}
	} else {
		for (const OriginMark &m : origin_marks_) {
			const float d = cursor_.distance_to(m.point);
			if (d <= CLICK_TOLERANCE && double(d) < best_dist) {
				best = Hit{};
				best.origin = true;
				best.origin_mark = true;
				best.point = m.point;
				best.distance = d;
				best_dist = d;
			}
		}
	}
	{
		const std::optional<V2> q = pole(plan_.size());
		const rc::Rect2f b = bounds();
		const bool on_screen = q && (!has_area(b) || b.has_point(*q));
		if (on_screen) {
			const float dq = cursor_.distance_to(*q);
			if (dq <= CLICK_TOLERANCE && double(dq) < best_dist) {
				Hit h;
				h.infinity = true;
				h.point = q;
				h.distance = dq;
				return h;
			}
		} else if (!best.has_surface && !best.origin && has_area(b)) {
			const double d = edge_distance(cursor_, b);
			if (d >= 0.0 && d <= CLICK_TOLERANCE) {
				Hit h;
				h.infinity = true;
				h.distance = d;
				return h;
			}
		}
	}
	return best;
}

// PlanManager.snap_click (B7)
V2 Session::snap_click(const BuiltSurface &s, Side side, const V2 &click) const {
	if (s.seg.is_line()) {
		return click;
	}
	const BuiltSide *cfg = active_side(s, side, flags_);
	if (cfg == nullptr || cfg->effect.type != "circle_directional_projection") {
		return click;
	}
	const V2 n = cfg->effect.normal;
	const V2 c = rc::arc::center_of(s.seg.get_carrier());
	const float along = (click - c).dot(n);
	if (std::abs(double(along)) >= SNAP_PX) {
		return click;
	}
	const double sign = (s.seg.v - c).dot(n) >= 0.0f ? 1.0 : -1.0;
	return click + n * float(sign * SNAP_PX - double(along));
}

void Session::set_plan(const std::vector<rc::PlanItem> &items) {
	plan_.clear();
	for (const rc::PlanItem &it : items) {
		PlanEntry e{it.surface_id, it.left ? Side::Left : Side::Right, it.hint ? V2::of(it.hint->x, it.hint->y) : V2::inf(), -1, it.span};
		if (it.span) {
			for (const Piece &pc : world_->pieces().of(it.surface_id)) {
				if (pc.span && *pc.span == *it.span) {
					e.piece = pc.index;
				}
			}
		}
		plan_.push_back(e);
	}
	plan_changed();
	refresh_ui();
}

void Session::click(bool right) {
	idle_ = 0.0; // (Stage 151: a plan change keeps a paused cycle paused)
	const Hit r = detect_click();
	if (r.origin_mark && !right) {
		return; // (a planned origin's mark, not plannable here)
	}
	if (!r.infinity && !r.has_surface && !r.origin) {
		if (!right) {
			return;
		}
		plan_.clear();
		plan_changed();
	} else {
		const int sid = r.infinity ? rc::INFINITY_ID : r.origin ? rc::ORIGIN_ID : r.surface;
		if (right) {
			bool has = false;
			for (const PlanEntry &e : plan_) {
				has = has || e.surface_id == sid;
			}
			if (has) {
				for (size_t i = plan_.size(); i-- > 0;) {
					if (plan_[i].surface_id == sid) {
						plan_.erase(plan_.begin() + long(i));
						plan_changed();
						break;
					}
				}
			} else {
				plan_.clear();
				plan_changed();
			}
		} else {
			const Side side = r.infinity || r.origin ? Side::Left : r.side;
			V2 click = cursor_;
			if (r.has_surface) {
				click = snap_click(*world_->by_id(r.surface), side, click);
			}
			PlanEntry e{sid, side, click, -1, std::nullopt};
			if (r.piece) {
				e.piece = r.piece->index;
				e.span = r.piece->span;
			}
			plan_.push_back(e);
			plan_changed();
		}
	}
	refresh_ui();
}

Session::Checkpoint Session::capture() const {
	return Checkpoint{player_, velocity_, flags_, plan_, targets_, world_->snapshot(), won_, inside_, phase_, cycle_auto_};
}

void Session::restore(const Checkpoint &c) {
	player_ = c.player;
	tick_from_ = c.player;
	velocity_ = c.velocity;
	set_flags(c.flags);
	world_->restore(c.world ? *c.world : world_->initial_snapshot());
	shadow(); // Stage 153: the restored flags' shadow
	sync_nodes();
	plan_ = c.plan;
	plan_changed();
	targets_ = c.targets;
	won_ = c.won;
	inside_ = c.inside;
	if (!level_.motions.empty()) {
		phase_ = c.phase;
		cycle_auto_ = c.cycle_auto;
		idle_ = 0.0;
		apply_phase();
	}
	refresh_ui();
}

void Session::key(Action a) {
	switch (a) {
		case Action::CycleDisplay:
			if (level_.features.preview) {
				display_mode_ = (display_mode_ + 1) % 3;
			}
			break;
		case Action::ToggleGravity:
			gravity_ = gravity_.length_squared() > 0.0f ? V2{} : V2{0.0f, 980.0f};
			break;
		case Action::ToggleVisibility:
			visibility_override_ = !visibility_on();
			break;
		case Action::Undo:
			if (!checkpoints_.empty()) {
				const Checkpoint c = checkpoints_.back();
				checkpoints_.pop_back();
				restore(c);
			}
			break;
		case Action::Reset:
			checkpoints_.clear();
			restore(initial_);
			break;
		case Action::ClearPlan:
			plan_.clear();
			plan_changed();
			refresh_ui();
			break;
		case Action::Any:
		case Action::Fire:
			break;
	}
}

void Session::land() {
	if (!pending_flags_) {
		return;
	}
	const Landing l = fold(pending_path_.hits, world_->surfaces(), *pending_flags_, world_->generations());
	world_->land(l);
	sync_nodes();
	rc::Flags kept = l.flags;
	kept.erase("ghost");
	// Stage 147: the per-shot flags (shot:*) last one shot, like ghost
	std::vector<std::string> per_shot;
	for (const auto &[k, v] : kept.entries()) {
		if (k.rfind("shot:", 0) == 0) {
			per_shot.push_back(k);
		}
	}
	for (const std::string &k : per_shot) {
		kept.erase(k);
	}
	set_flags(kept);
	targets_.clear();
	for (const auto &[k, v] : flags_.entries()) {
		const bool *b = std::get_if<bool>(&v);
		if (k.rfind("target:", 0) == 0 && b && *b) {
			targets_.push_back({std::stoi(k.substr(7)), true});
		}
	}
	pending_flags_.reset();
	spread(); // Stage 152: fire and charge along what touches
	shadow(); // Stage 153
	if (!level_.motions.empty() && !cycle_auto_) {
		cycle_auto_ = true; // Stage 151: a shot resumes the cycle
		idle_ = 0.0;
	}
	start_journey();
	check_won();
	// Plan entries on surfaces no longer in the world, or absent, are dropped (the ∞ entry stays)
	const size_t before = plan_.size();
	plan_.erase(std::remove_if(plan_.begin(), plan_.end(),
					[&](const PlanEntry &e) {
						if (e.surface_id == rc::INFINITY_ID || e.surface_id == rc::ORIGIN_ID) {
							return false;
						}
						const BuiltSurface *s = world_->by_id(e.surface_id);
						return s == nullptr || !present(*s, flags_);
					}),
		plan_.end());
	if (plan_.size() != before) {
		plan_changed();
	}
	refresh_ui();
}

std::vector<std::pair<int, Overlay>> plan_overlays(const std::vector<PlanEntry> &plan,
		const std::vector<std::pair<int, std::vector<rc::PhysicalHit>>> &hits, const World &world, const rc::Flags &flags,
		const std::vector<bool> &frosted) {
	std::vector<std::pair<int, Overlay>> overlays;
	auto hits_of = [&](int id) -> const std::vector<rc::PhysicalHit> * {
		for (const auto &e : hits) {
			if (e.first == id) {
				return &e.second;
			}
		}
		return nullptr;
	};
	for (size_t ei = 0; ei < plan.size(); ei++) {
		const PlanEntry &e = plan[ei];
		const BuiltSurface *s = world.by_id(e.surface_id);
		if (s == nullptr) {
			continue;
		}
		const std::optional<OverlayLink> link = overlay_link(*s, e.side, flags, ei < frosted.size() && frosted[ei]);
		if (!link) {
			continue;
		}
		bool in_ok = false, out_ok = false;
		if (const std::vector<rc::PhysicalHit> *hs = hits_of(e.surface_id)) {
			for (const rc::PhysicalHit &h : *hs) {
				const bool on_piece = !e.span || span_contains(*s, *e.span, h.point);
				if (h.side == int(e.side) && h.on_segment && on_piece) {
					in_ok = true;
					out_ok = out_ok || h.has_continuation;
				}
			}
		}
		const Color in_c = in_ok ? PLAN_VALID_COLOR : PLAN_INVALID_COLOR;
		const Color out_c = out_ok ? PLAN_VALID_COLOR : PLAN_INVALID_COLOR;
		const int side = int(e.side);
		const int out_piece = link->outgoing && link->outgoing->surface == s->id ? e.piece : -1;
		if (!link->outgoing) {
			overlays.push_back({s->id, Overlay{side, true, false, -1, in_c, out_c, in_c, e.piece}});
		} else if (*link->outgoing == link->from) {
			overlays.push_back({s->id, Overlay{side, true, true, side, in_c, out_c, in_c, e.piece}});
		} else {
			const int os = int(link->outgoing->side);
			overlays.push_back({s->id, Overlay{side, true, false, -1, in_c, out_c, in_c, e.piece}});
			overlays.push_back({link->outgoing->surface, Overlay{os, false, true, os, in_c, out_c, in_c, out_piece}});
		}
	}
	return overlays;
}

bool Session::start_fire() {
	if (flying_ || journey_ || bodies_moving_ || player_ == cursor_) {
		return false; // (Stage 146: nothing is fired while anything moves)
	}
	checkpoints_.push_back(capture());
	std::optional<rc::TracePair> pair;
	if (async_) {
		// Stage 121: the exact preview of these inputs is the shot's trace (waiting
		// for its climb if one runs); never a provisional one
		if (provisional_ && preview_job_) {
			preview_job_->wait_idle();
			collect_jobs();
		}
		const PreviewKey now{player_, cursor_, plan_version_, flags_version_, world_->version()};
		if (!provisional_ && preview_.computed && preview_inputs_ == now && preview_.pair) {
			pair = preview_.pair;
		}
	}
	if (!pair) {
		use_defs(gm_tracer_, gm_defs_);
		pair = gm_tracer_.trace_pair(player_, cursor_, items(), flags_);
	}
	if (!pair) {
		checkpoints_.pop_back();
		return false;
	}
	pending_flags_ = pair->final_flags;
	pending_path_ = pair->physical;
	if (!level_.features.arrow) {
		land(); // no arrow to animate: the shot lands at once
		return true;
	}
	const rc::TracePath path = rc::sweep::prepare_for_display(pair->physical, bounds(), infos());
	flight_geoms_.clear();
	for (const rc::TraceStep &s : path.steps) {
		flight_geoms_.push_back(rc::sweep::step_geom(s));
	}
	if (!path.steps.empty()) {
		arrow_pos_ = path.steps.front().start;
		arrow_dir_ = start_direction(path.steps.front());
	}
	flight_index_ = 0;
	flight_progress_ = 0.0;
	flight_multiplier_ = 1.0;
	flying_ = true;
	clear_hover(); // (the hover stands down during a flight)
	return true;
}

void Session::speed_up() {
	if (flying_) {
		flight_multiplier_ *= 10.0;
	}
}

void Session::clear_hover() {
	for (Node &n : nodes_) {
		if (n.serial == hovered_serial_ || n.serial == partner_serial_) {
			n.hover.clear();
		}
	}
	hovered_serial_ = partner_serial_ = 0;
	edge_hovered_ = false;
	origin_hovered_ = false;
	hover_inputs_.reset();
}

void Session::advance(double dt, const kinematics::Input &in) {
	if (journey_) {
		advance_journey(dt);
		settle();
		return;
	}
	if (flying_) {
		const double distance = ARROW_SPEED * flight_multiplier_ * dt;
		const rc::arc::AdvanceResult r = rc::arc::advance(flight_geoms_, flight_index_, flight_progress_, arrow_pos_, distance, bounds());
		flight_index_ = r.step_index;
		flight_progress_ = r.progress;
		arrow_pos_ = r.position;
		arrow_dir_ = r.direction;
		if (r.finished) {
			flying_ = false;
			flight_geoms_.clear();
			land();
			settle();
		}
		return;
	}
	physics_time_ += dt;
	jump_ = jump_ || in.jump; // a press waits for the next tick
	while (physics_time_ >= kinematics::STEP) {
		kinematics::Input t = in;
		t.jump = jump_;
		tick(t);
		jump_ = false;
		physics_time_ -= kinematics::STEP;
	}
	settle();
}

void Session::tick(const kinematics::Input &in) {
	if (!level_.motions.empty()) { // Stage 151: the cycle, per tick (every host's frames agree)
		if (cycle_auto_) {
			phase_ = std::fmod(phase_ + kinematics::STEP / level_.cycle.period, 1.0);
		} else if ((idle_ += kinematics::STEP) >= level_.cycle.resume_after) {
			cycle_auto_ = true;
			hud_ = hud_text();
		}
		apply_phase();
	}
	tick_from_ = player_;
	kinematics::Body body{player_, velocity_, on_floor_};
	kinematics::step(body, in, gravity_, kinematics::colliders_of(world_->surfaces(), flags_, level_));
	player_ = body.pos;
	velocity_ = body.vel;
	on_floor_ = body.on_floor;
	if (!world_->bodies().empty()) {
		step_bodies();
	}
	if (!level_.detectors.empty()) {
		detect(tick_from_, player_);
	}
	if (!level_.tagged.empty() || std::any_of(level_.objects.begin(), level_.objects.end(), [](const Object &o) { return !o.tags.empty(); })) {
		spread(); // Stage 152: bodies moved, detectors wrote, surfaces moved
	}
	shadow(); // Stage 153 (a no-op unless the flags or the world changed)
	if (!won_ && level_.objective) {
		check_won();
	}
}

namespace {

// The move from -> to crosses segment a-b: from strictly on one side to the
// other side or onto the line, within the segment (a move off the line is no crossing).
bool crosses(const V2 &from, const V2 &to, const V2 &a, const V2 &b) {
	auto orient = [](const V2 &p, const V2 &q, const V2 &r) {
		return (double(q.x) - p.x) * (double(r.y) - p.y) - (double(q.y) - p.y) * (double(r.x) - p.x);
	};
	const double d1 = orient(a, b, from), d2 = orient(a, b, to);
	if (!((d1 < 0 && d2 >= 0) || (d1 > 0 && d2 <= 0))) {
		return false;
	}
	const double d3 = orient(from, to, a), d4 = orient(from, to, b);
	return (d3 <= 0 && d4 >= 0) || (d3 >= 0 && d4 <= 0);
}

} // namespace

// Stage 145 (GAME_SPEC 24.5): a region's detector writes on entering (enter,
// toggle) or while the player is in it (while: the value inside, unset
// outside); a segment's when the player crosses it.
void Session::detect(const V2 &from, const V2 &to) {
	if (inside_.size() != level_.detectors.size()) {
		inside_.assign(level_.detectors.size(), false);
	}
	rc::Flags f = flags_;
	for (size_t i = 0; i < level_.detectors.size(); i++) {
		const Detector &d = level_.detectors[i];
		// the player (for player | any), the objects (for object | any: a region only, by their centers)
		bool in = false;
		if (!d.segment) {
			in = d.target != "object" && d.region.contains(to);
			if (d.target != "player") {
				for (const World::Body &b : world_->bodies()) {
					in = in || d.region.contains(b.pos);
				}
			}
		}
		const bool fire = d.segment ? d.target != "object" && crosses(from, to, d.a, d.b) : in && !inside_[i];
		if (d.mode == "while") {
			if (in != inside_[i]) {
				if (in) {
					f.set(d.flag, d.value);
				} else {
					f.erase(d.flag);
				}
			}
		} else if (fire) {
			f = f.applied(rc::StateChangeDef{d.flag, d.mode == "toggle", d.value});
		}
		inside_[i] = in;
	}
	const int before = flags_version_;
	set_flags(f);
	if (flags_version_ != before) {
		hud_ = hud_text();
	}
}

// ---------------------------------------------------------------- Stage 146: bodies and journeys

namespace {
constexpr double JOURNEY_SPEED = 900.0; // px/s along the path
constexpr double JOURNEY_SAMPLE = 4.0;  // px between the path's points
} // namespace

std::vector<kinematics::Collider> Session::colliders_except(int body) const { return colliders_except(body, flags_); }

std::vector<kinematics::Collider> Session::colliders_except(int body, const rc::Flags &flags) const {
	std::vector<kinematics::Collider> out;
	const int skip = body >= 0 ? world_->bodies()[size_t(body)].id : -1;
	for (const BuiltSurface &s : world_->surfaces()) {
		if (s.id != skip && s.player_solid && !s.pre_transform && present(s, flags)) {
			out.push_back(kinematics::collider_of(s.seg));
		}
	}
	for (const auto &k : level_.colliders) {
		out.push_back(kinematics::segment_collider(k.a, k.b));
	}
	if (body >= 0) {
		out.push_back(kinematics::circle_collider(player_, kinematics::RADIUS)); // a body meets the player
	}
	return out;
}

// Pullable bodies fall under gravity and rest on what holds them (no body meets
// another, GAME_SPEC 25.5); static ones stay. A body that moved rebuilds its surface.
void Session::step_bodies() {
	bool moved = false;
	auto index_of = [&](const std::string &name) {
		for (size_t k = 0; k < world_->bodies().size(); k++) {
			if (world_->bodies()[k].object.name == name) {
				return int(k);
			}
		}
		return -1;
	};
	auto alive = [&](const World::Body &b) { return present(b.built, flags_); }; // (a popped bubble is gone)
	// the free bodies, then (Stage 150) the carried ones, at their offset from a carrier that is still there
	for (const bool carried_pass : {false, true}) {
		for (size_t i = 0; i < world_->bodies().size(); i++) {
			const World::Body &b = world_->bodies()[i];
			if (!b.object.pullable || !alive(b)) {
				continue;
			}
			int carrier = -1;
			for (size_t k = 0; k < world_->bodies().size(); k++) {
				const World::Body &c = world_->bodies()[k];
				if (c.object.carries == b.object.name && alive(c)) {
					carrier = int(k);
				}
			}
			if ((carrier >= 0) != carried_pass) {
				continue;
			}
			if (carrier >= 0) {
				const World::Body &c = world_->bodies()[size_t(carrier)];
				const int me = index_of(b.object.name);
				const V2 offset = world_->bodies()[size_t(me)].object.center - c.object.center;
				const V2 to = c.pos + offset;
				moved = moved || to != b.pos;
				world_->set_body(i, to, V2{}, false);
				continue;
			}
			const V2 g = gravity_ * float(b.object.gravity);
			if (g.length_squared() == 0.0f && b.vel.length_squared() == 0.0f) {
				continue;
			}
			kinematics::Body k{b.pos, b.vel, b.on_floor};
			kinematics::step_body(k, kinematics::Input{}, g, colliders_except(int(i)), b.object.radius);
			moved = moved || k.pos != b.pos;
			world_->set_body(i, k.pos, k.vel, k.on_floor);
		}
	}
	if (moved) {
		sync_nodes();
	}
	bodies_moving_ = moved;
}

// Stage 159: the journey a path's first fired grapple or pull makes (and that hit's index), if any
std::optional<Session::Journey> Session::journey_of(const rc::TracePath &path, int *hit_index) const {
	for (size_t hi = 0; hi < path.hits.size(); hi++) {
		const rc::HitRecord &h = path.hits[hi];
		if (!h.fired) {
			continue;
		}
		const BuiltSurface *s = world_->by_id(h.surface_id);
		const BuiltSide *cfg = s ? active_side(*s, h.side == rc::Side::Left ? Side::Left : Side::Right, h.flags_before) : nullptr;
		if (cfg == nullptr) {
			continue;
		}
		bool grapple = false, pull = false;
		for (const HitSpec &hs : cfg->hits) {
			grapple = grapple || hs.kind == "grapple";
			pull = pull || hs.kind == "pull";
		}
		const int body = pull ? world_->body_of(h.surface_id) : -1;
		if (!grapple && body < 0) {
			continue;
		}
		// the path: the shot's steps up to the hit, every few pixels; a step that does not start where the
		// last ended (a portal) is reached by a jump; a step through infinity ends the path
		Journey j;
		j.body = grapple ? -1 : body;
		std::vector<int> leg;
		for (int k = 0; k <= h.step_index && k < int(path.steps.size()); k++) {
			const rc::TraceStep &st = path.steps[size_t(k)];
			if (st.start.is_inf() || st.end.is_inf()) {
				break;
			}
			const rc::arc::StepGeom g = rc::sweep::step_geom(st);
			const bool arc = rc::arc::step_is_arc(g);
			const double len = arc ? rc::arc::arc_length(g) : double((st.end - st.start).length());
			const int n = std::max(1, int(std::ceil(len / JOURNEY_SAMPLE)));
			for (int m = j.path.empty() ? 0 : (st.start == j.path.back() ? 1 : 0); m <= n; m++) {
				const double t = double(m) / n;
				j.path.push_back(arc ? rc::arc::arc_interpolate(g, t).position : st.start + (st.end - st.start) * float(t));
				leg.push_back(k);
			}
		}
		if (j.path.empty() || j.path.back() != h.point) {
			j.path.push_back(h.point);
			leg.push_back(leg.empty() ? 0 : leg.back());
		}
		if (j.body >= 0) {
			std::reverse(j.path.begin(), j.path.end());
			std::reverse(leg.begin(), leg.end());
		}
		j.jump.assign(j.path.size(), false);
		for (size_t m = 1; m < j.path.size(); m++) {
			j.jump[m] = leg[m] != leg[m - 1] && (j.path[m] - j.path[m - 1]).length() > 1e-3f;
		}
		// Stage 166: the guide starts where the path does (a pull: the hit; a grapple: the player), close enough
		// when it is inside the traveller
		j.guide = j.path.front();
		j.lead = j.body >= 0 ? world_->bodies()[size_t(j.body)].object.radius : kinematics::RADIUS;
		if (j.path.size() < 2) {
			return std::nullopt;
		}
		if (hit_index != nullptr) {
			*hit_index = int(hi);
		}
		return j;
	}
	return std::nullopt;
}

void Session::start_journey() {
	if (std::optional<Journey> j = journey_of(pending_path_)) {
		journey_ = std::move(j);
	}
}

// Stage 166 (the user): one tick of a journey (the live game's and the preview's). The guide moves along the raw
// path while the traveller holds it (inside its radius), and waits otherwise; the traveller moves straight toward
// the guide, sliding along what it touches. It is done when it reaches the guide at the path's end, or cannot get
// closer to a waiting guide. At a portal the guide waits at the entry until the traveller holds it; the traveller
// then crosses in and comes out of the exit (the portal's motion), and goes on.
bool Session::journey_move(Journey &j, V2 &pos, const std::vector<kinematics::Collider> &cs, double radius, double dt) const {
	const double follow = JOURNEY_SPEED * 1.25 * dt; // (a little faster than the guide: it catches up)
	auto dist = [](const V2 &a, const V2 &b) { return std::hypot(double(a.x) - double(b.x), double(a.y) - double(b.y)); };
	if (j.crossing) {
		const double step = std::min(follow, j.cross_left);
		pos = V2::of(double(pos.x) + double(j.cross_dir.x) * step, double(pos.y) + double(j.cross_dir.y) * step);
		j.cross_left -= step;
		if (j.cross_left <= 0.0) { // through: the copy goes on from the exit
			pos = j.map(pos);
			j.crossing = false;
			j.guide = j.path[j.next];
			j.next++;
		}
		return false;
	}
	bool guide_moved = false;
	if (dist(pos, j.guide) <= j.lead) {
		double left = JOURNEY_SPEED * dt;
		while (left > 0.0 && j.next < j.path.size()) {
			if (j.jump[j.next]) { // a portal's entry: the traveller holds it -- it crosses
				const V2 e = j.guide, x = j.path[j.next];
				const V2 back = j.next >= 2 ? j.path[j.next - 2] : pos;
				double bx = double(e.x) - back.x, by = double(e.y) - back.y, bl = std::hypot(bx, by);
				if (!(bl > 0.0)) {
					bx = double(e.x) - pos.x, by = double(e.y) - pos.y, bl = std::hypot(bx, by);
				}
				bx /= bl > 0.0 ? bl : 1.0;
				by /= bl > 0.0 ? bl : 1.0;
				double ax = bx, ay = by; // the way out (the path after the exit)
				if (j.next + 1 < j.path.size()) {
					ax = double(j.path[j.next + 1].x) - x.x, ay = double(j.path[j.next + 1].y) - x.y;
					const double al = std::hypot(ax, ay);
					ax = al > 0.0 ? ax / al : bx;
					ay = al > 0.0 ? ay / al : by;
				}
				const double th = std::atan2(ay, ax) - std::atan2(by, bx);
				j.rc = std::cos(th);
				j.rs = std::sin(th);
				j.rt = V2::of(double(x.x) - (j.rc * double(e.x) - j.rs * double(e.y)), double(x.y) - (j.rs * double(e.x) + j.rc * double(e.y)));
				// the entry's line: the portal surface through it (else across the way in)
				j.entry_a = V2::of(double(e.x) - by * 40, double(e.y) + bx * 40);
				j.entry_b = V2::of(double(e.x) + by * 40, double(e.y) - bx * 40);
				for (const BuiltSurface &s : world_->surfaces()) {
					if (s.seg.is_line() && s.portal_pair != 0 && s.seg.distance_to(e) < 0.5) {
						j.entry_a = s.seg.s;
						j.entry_b = s.seg.e;
						break;
					}
				}
				double nx = -(double(j.entry_b.y) - j.entry_a.y), ny = double(j.entry_b.x) - j.entry_a.x, nl = std::hypot(nx, ny);
				nx /= nl > 0.0 ? nl : 1.0;
				ny /= nl > 0.0 ? nl : 1.0;
				if (nx * bx + ny * by < 0.0) {
					nx = -nx, ny = -ny;
				}
				j.entry_n = V2::of(nx, ny);
				j.cross_dir = V2::of(bx, by);
				const double along = std::max(0.05, bx * nx + by * ny);
				const double ahead = (double(pos.x) - e.x) * nx + (double(pos.y) - e.y) * ny; // (negative: still before it)
				j.cross_left = std::max(0.0, (radius - ahead) / along);
				j.crossing = true;
				return false;
			}
			const V2 target = j.path[j.next];
			const double len = dist(j.guide, target);
			if (len <= left) {
				j.guide = target;
				j.next++;
				left -= len;
			} else {
				j.guide = V2::of(double(j.guide.x) + (double(target.x) - j.guide.x) * left / len, double(j.guide.y) + (double(target.y) - j.guide.y) * left / len);
				left = 0.0;
			}
			guide_moved = true;
		}
	}
	// the traveller: straight toward the guide, sliding along what it touches
	const double before = dist(pos, j.guide);
	if (before > 0.0) {
		const double f = std::min(1.0, follow / before);
		double dx = (double(j.guide.x) - pos.x) * f, dy = (double(j.guide.y) - pos.y) * f;
		for (int k = 0; k < 4 && (dx != 0.0 || dy != 0.0); k++) {
			const double t = kinematics::sweep_fraction(pos.x, pos.y, dx, dy, cs, radius);
			pos = V2::of(double(pos.x) + dx * t, double(pos.y) + dy * t);
			if (t >= 1.0) {
				break;
			}
			// the rest along the contact: the nearest collider's normal
			double best = INFINITY, nx = 0.0, ny = 0.0;
			for (const kinematics::Collider &c : cs) {
				double cx = 0.0, cy = 0.0;
				const double d = kinematics::distance(c, pos.x, pos.y, cx, cy);
				if (d < best) {
					best = d, nx = cx, ny = cy;
				}
			}
			const double rx = dx * (1.0 - t), ry = dy * (1.0 - t), into = rx * nx + ry * ny;
			if (into >= 0.0) {
				break;
			}
			// (the user: the slide keeps the pull's speed -- the rest's full length along the contact, not its
			// projection, which shrinks to a crawl that never converges)
			const double sx = rx - into * nx, sy = ry - into * ny, sl = std::hypot(sx, sy), rl = std::hypot(rx, ry);
			if (!(sl > 1e-9)) {
				break; // (straight into the contact: no way along it)
			}
			// ... up to the point nearest the guide on its way (a slide past it, round a curved contact, would come
			// back and oscillate)
			const double ux = sx / sl, uy = sy / sl;
			const double reach = std::min(rl, (double(j.guide.x) - pos.x) * ux + (double(j.guide.y) - pos.y) * uy);
			if (!(reach > 1e-9)) {
				break; // (along the contact leads away from the guide)
			}
			dx = ux * reach;
			dy = uy * reach;
		}
	}
	const double after = dist(pos, j.guide);
	if (j.next >= j.path.size() && after <= 1e-3) {
		return true; // at the guide, at the path's end
	}
	return !guide_moved && !(after < before - 1e-6); // no closer to a waiting guide
}

// The traveller moves along the path at JOURNEY_SPEED, swept against what it
// meets: it stops at the first contact (a fence stops it, an arrow-only barrier
// does not), or at the path's end.
void Session::advance_journey(double dt) {
	Journey &j = *journey_;
	const bool player = j.body < 0;
	const double radius = player ? kinematics::RADIUS : world_->bodies()[size_t(j.body)].object.radius;
	V2 pos = player ? player_ : world_->bodies()[size_t(j.body)].pos;
	const V2 from = pos;
	const std::vector<kinematics::Collider> cs = colliders_except(j.body);
	const bool done = journey_move(j, pos, cs, radius, dt);
	if (player) {
		player_ = pos;
		tick_from_ = pos;
		velocity_ = V2{};
		if (!level_.detectors.empty()) {
			detect(from, pos);
		}
	} else {
		world_->set_body(size_t(j.body), pos, V2{}, false);
		sync_nodes();
		if (!level_.detectors.empty()) {
			detect(player_, player_);
		}
	}
	if (done) {
		journey_.reset();
		spread();
		shadow();
		check_won();
	}
}

// Stage 153 (GAME_SPEC 9.9): the shadow under the flags -- its boundary's pieces
// (the lit side a terminal) and the surfaces lying wholly in it, darkened.
void Session::shadow() {
	if (level_.shades.empty()) {
		return;
	}
	std::vector<bool> active;
	for (const Shade &sh : level_.shades) {
		active.push_back(sh.when.empty() || flags_.is_true(sh.when));
	}
	if (active == shade_active_ && shade_world_ == world_->version()) {
		return;
	}
	if (active != shade_active_) {
		shade_fill_ = shade::fill(shade_regions_, active, level_.bounds);
		shade_serial_++;
	}
	std::vector<BuiltSurface> edge = shade::boundary(shade_pieces_, shade_regions_, active, shade_first_id_);
	const int last = shade_first_id_ + int(shade_pieces_.size());
	std::vector<int> dark;
	for (const BuiltSurface &s : world_->surfaces()) {
		if ((s.id >= shade_first_id_ && s.id < last) || s.pre_transform || s.portal_pair != 0) {
			continue; // (the boundary itself; a portal pair stays whole)
		}
		const V2 p = shade::point_of(s.seg);
		if (!shade::shadowed(shade_regions_, active, p.x, p.y)) {
			continue;
		}
		const bool crossed = std::any_of(edge.begin(), edge.end(), [&](const BuiltSurface &b) { return !crossings_of(b.seg, s.seg).empty(); });
		if (!crossed) {
			dark.push_back(s.id);
		}
	}
	std::sort(dark.begin(), dark.end());
	shade_active_ = std::move(active);
	if (world_->set_shade(std::move(edge), std::move(dark))) {
		sync_nodes();
	}
	shade_world_ = world_->version();
}

// Stage 152 (GAME_SPEC 10.6): the spreading rules over the tagged elements and
// objects. Touching: two surfaces crossing, or an end within half a pixel of the
// other; an object's circle within half a pixel of a surface or another object.
void Session::spread() {
	if (level_.spread.empty()) {
		return; // (Stage 156: no touching rules by default)
	}
	struct Node {
		std::string name;
		std::vector<std::string> tags;
		std::vector<const BuiltSurface *> surfaces; // an element's
		int body = -1;                              // an object's
	};
	std::vector<Node> nodes;
	for (const Tagged &t : level_.tagged) {
		Node n{t.name, t.tags, {}, -1};
		for (const size_t k : t.surfaces) {
			for (const int id : level_.surfaces[k].ids) {
				if (const BuiltSurface *b = world_->by_id(id)) {
					n.surfaces.push_back(b);
				}
			}
		}
		nodes.push_back(std::move(n));
	}
	for (size_t i = 0; i < world_->bodies().size(); i++) {
		const World::Body &b = world_->bodies()[i];
		if (!b.object.tags.empty()) {
			nodes.push_back({b.object.name, b.object.tags, {&b.built}, int(i)});
		}
	}
	if (nodes.empty()) {
		return;
	}
	if (touch_version_ != world_->version() || touches_.size() != nodes.size()) {
		constexpr double TOUCH = 0.5;
		auto near = [&](const SegGeom &seg, const V2 &p, double reach) {
			double nx = 0, ny = 0;
			return kinematics::distance(kinematics::collider_of(seg), p.x, p.y, nx, ny) <= reach;
		};
		auto touch = [&](const Node &a, const Node &b) {
			if (a.body >= 0 && b.body >= 0) {
				const World::Body &x = world_->bodies()[size_t(a.body)], &y = world_->bodies()[size_t(b.body)];
				return (x.pos - y.pos).length() <= x.object.radius + y.object.radius + TOUCH;
			}
			if (a.body >= 0 || b.body >= 0) {
				const World::Body &x = world_->bodies()[size_t(a.body >= 0 ? a.body : b.body)];
				for (const BuiltSurface *s : (a.body >= 0 ? b : a).surfaces) {
					if (near(s->seg, x.pos, x.object.radius + TOUCH)) {
						return true;
					}
				}
				return false;
			}
			for (const BuiltSurface *s : a.surfaces) {
				for (const BuiltSurface *t : b.surfaces) {
					if (!crossings_of(s->seg, t->seg).empty()) {
						return true;
					}
					for (const auto &[p, q] : {std::pair{s, t}, std::pair{t, s}}) {
						if (!p->seg.full && (near(q->seg, p->seg.s, TOUCH) || near(q->seg, p->seg.e, TOUCH))) {
							return true;
						}
					}
				}
			}
			return false;
		};
		touches_.assign(nodes.size(), std::vector<bool>(nodes.size(), false));
		for (size_t i = 0; i < nodes.size(); i++) {
			for (size_t j = i + 1; j < nodes.size(); j++) {
				touches_[i][j] = touches_[j][i] = touch(nodes[i], nodes[j]);
			}
		}
		touch_version_ = world_->version();
	}
	auto has = [](const Node &n, const std::string &tag) { return std::find(n.tags.begin(), n.tags.end(), tag) != n.tags.end(); };
	for (int round = 0; round < 8; round++) { // (presence follows the flags: until nothing changes)
		rc::Flags f = flags_;
		for (const SpreadRule &r : level_.spread) {
			std::vector<size_t> members;
			for (size_t i = 0; i < nodes.size(); i++) {
				if (has(nodes[i], r.tag)) {
					members.push_back(i);
				}
			}
			std::vector<bool> lit(nodes.size(), false), here(nodes.size(), false);
			for (const size_t i : members) {
				here[i] = std::any_of(nodes[i].surfaces.begin(), nodes[i].surfaces.end(), [&](const BuiltSurface *s) { return present(*s, flags_); });
				const std::string key = "el:" + nodes[i].name + ":" + r.flag;
				lit[i] = r.persist ? f.is_true(key)
								   : here[i] && std::any_of(r.sources.begin(), r.sources.end(), [&](const std::string &t) { return has(nodes[i], t); });
			}
			// the flood: persisting through every member; not, through the present ones
			for (bool grew = true; grew;) {
				grew = false;
				for (const size_t i : members) {
					if (!lit[i] || (!r.persist && !here[i])) {
						continue;
					}
					for (const size_t j : members) {
						if (!lit[j] && touches_[i][j]) {
							lit[j] = true; // (an absent one is lit, and passes nothing on)
							grew = true;
						}
					}
				}
			}
			for (const size_t i : members) {
				const std::string key = "el:" + nodes[i].name + ":" + r.flag;
				if (lit[i] && !f.is_true(key)) {
					f.set(key, true);
				} else if (!lit[i] && !r.persist && f.has(key)) {
					f.erase(key);
				}
			}
		}
		const int before = flags_version_;
		set_flags(f);
		if (flags_version_ == before) {
			break;
		}
		hud_ = hud_text();
	}
}

void Session::scroll(double delta) {
	if (level_.motions.empty() || flying()) {
		return;
	}
	phase_ = std::fmod(phase_ + delta * 0.02, 1.0); // a notch: a fiftieth of the cycle
	if (phase_ < 0) {
		phase_ += 1.0;
	}
	cycle_auto_ = false;
	idle_ = 0.0;
	apply_phase();
	hud_ = hud_text();
}

// The moving surfaces where the cycle puts them: each motion's keys interpolated at
// the phase -- an offset and a rotation about its pivot.
void Session::apply_phase() {
	std::vector<rc::Xf2> xfs;
	for (size_t i = 0; i < moving_motion_.size(); i++) {
		const std::vector<std::array<double, 4>> &keys = level_.motions[moving_motion_[i]].keys;
		std::array<double, 3> v{keys.front()[1], keys.front()[2], keys.front()[3]};
		if (keys.size() > 1) {
			size_t k = keys.size() - 1;
			for (size_t q = 0; q < keys.size(); q++) {
				if (keys[q][0] <= phase_) {
					k = q;
				}
			}
			const size_t n = (k + 1) % keys.size();
			const double p0 = keys[k][0] > phase_ ? keys[k][0] - 1.0 : keys[k][0]; // (before the first key: the last, a cycle back)
			const double p1 = n == 0 ? keys[0][0] + 1.0 : keys[n][0];
			const double t = (phase_ - p0) / (p1 - p0);
			for (size_t q = 0; q < 3; q++) {
				v[q] = keys[k][q + 1] + (keys[n][q + 1] - keys[k][q + 1]) * t;
			}
		}
		const double c = std::cos(v[2]), s = std::sin(v[2]);
		const V2 p = moving_pivot_[i];
		const rc::Xf2 xf = rc::Xf2::of(V2::of(c, s), V2::of(-s, c),
				V2::of(double(p.x) + v[0] - (c * p.x - s * p.y), double(p.y) + v[1] - (s * p.x + c * p.y)));
		xfs.push_back(xf);
	}
	if (world_->place_moving(xfs)) {
		sync_nodes();
	}
}

bool Session::goal_open() const { return level_.objective && holds(level_.objective->conditions, flags_); }

void Session::check_won() {
	if (won_) {
		return;
	}
	bool w = false;
	if (level_.objective) {
		w = goal_open() && level_.objective->goal.contains(player_);
	} else {
		int targets = 0;
		bool all = true;
		for (const BuiltSurface &s : world_->surfaces()) {
			if (s.is_target()) {
				targets++;
				all = all && flags_.is_true("target:" + std::to_string(s.id));
			}
		}
		w = targets > 0 && all;
	}
	if (w) {
		won_ = true;
		hud_ = hud_text();
	}
}

Flight Session::fire(int speedup_at, double frame_dt) {
	Flight f;
	if (!start_fire()) {
		return f;
	}
	while (flying()) {
		f.stream.push_back({arrow_pos_, arrow_dir_});
		f.frames++;
		advance(frame_dt, {});
		if (f.frames == speedup_at) {
			speed_up();
		}
	}
	return f;
}

std::vector<V2> Session::hold(const std::vector<std::string> &actions, int ticks) {
	auto held = [&](const char *a) { return std::find(actions.begin(), actions.end(), a) != actions.end(); };
	kinematics::Input in;
	in.axis_x = float(held("move_right")) - float(held("move_left"));
	in.axis_y = float(held("move_down")) - float(held("move_up"));
	std::vector<V2> out;
	for (int k = 0; k < ticks; k++) {
		// the engine reports the up action just pressed on the tick after the press
		in.jump = k == 1 && held("move_up");
		tick(in);
		out.push_back(player_);
	}
	return out;
}

void Session::sync_nodes() {
	const Pieces &pieces = world_->pieces();
	std::vector<Node> kept;
	std::vector<Node> made;
	for (const BuiltSurface &s : world_->surfaces()) {
		if (!s.label) {
			continue;
		}
		const std::vector<Piece> &want = pieces.of(s.id);
		std::vector<const Node *> have;
		for (const Node &n : nodes_) {
			if (n.surface == s.id) {
				have.push_back(&n);
			}
		}
		bool same = have.size() == want.size();
		for (size_t i = 0; same && i < have.size(); i++) {
			same = have[i]->piece == want[i].index && have[i]->segment.s == want[i].segment.s && have[i]->segment.e == want[i].segment.e;
		}
		if (same) {
			continue;
		}
		for (const Piece &p : want) {
			Node n;
			n.surface = s.id;
			n.piece = p.index;
			n.segment = p.segment;
			n.serial = next_serial_++;
			made.push_back(n);
		}
	}
	// the nodes in the scene's order: the ones kept (not remade, still live), then the new ones
	std::vector<int> remade;
	for (const Node &n : made) {
		remade.push_back(n.surface);
	}
	for (const Node &n : nodes_) {
		const bool live = world_->by_id(n.surface) != nullptr && world_->by_id(n.surface)->label.has_value();
		if (live && std::find(remade.begin(), remade.end(), n.surface) == remade.end()) {
			kept.push_back(n);
		}
	}
	kept.insert(kept.end(), made.begin(), made.end());
	nodes_ = std::move(kept);
}

void Session::refresh_ui() {
	hud_ = hud_text();
	update_overlays(true);
}

std::string Session::hud_text() const {
	// Stage 145: a level with a goal says where it stands
	std::string goal = !level_.objective ? std::string() : std::string("Goal: ") + (won_ ? "reached!" : goal_open() ? "open" : "locked");
	if (!level_.motions.empty()) { // Stage 151
		goal += std::string(goal.empty() ? "" : "\n") + (cycle_auto_ ? "Cycle: running (scroll to set it)" : "Cycle: paused (a shot runs it again)");
	}
	if (plan_.empty()) {
		return goal;
	}
	std::string out = goal.empty() ? std::string("Plan:") : goal + "\nPlan:";
	for (size_t i = 0; i < plan_.size(); i++) {
		const PlanEntry &e = plan_[i];
		out += "\n  " + std::to_string(i + 1) + ". ";
		if (e.surface_id == rc::INFINITY_ID) {
			out += "\xe2\x88\x9e"; // ∞
			continue;
		}
		if (e.surface_id == rc::ORIGIN_ID) {
			out += "origin"; // (Stage 132: the ray's origin)
			continue;
		}
		std::string name = "Surface " + std::to_string(e.surface_id);
		if (const BuiltSurface *s = world_->by_id(e.surface_id)) {
			const BuiltSide *cfg = active_side(*s, Side::Left, rc::Flags{});
			if (cfg && cfg->effect.type != "pass") {
				const std::string dn = display_name(cfg->effect);
				if (dn == "reflect") {
					name = "Mirror " + std::to_string(e.surface_id);
				} else if (dn == "block") {
					name = "Wall " + std::to_string(e.surface_id);
				}
			}
		}
		out += name + " (" + (e.side == Side::Left ? "L" : "R") + ")";
	}
	return out;
}

void Session::update_preview() {
	if (!level_.features.preview) {
		return;
	}
	const PreviewKey inputs{player_, cursor_, plan_version_, flags_version_, world_->version()};
	if (preview_.computed && inputs == preview_inputs_) {
		return;
	}
	preview_inputs_ = inputs;
	preview_.computed = true;
	preview_ticket_++; // (an exact climb still running is for older inputs now)
	preview_climb_seconds_ = 0.0;
	if (provisional_) {
		provisional_.reset();
		provisional_version_++;
	}
	if (player_ == cursor_) {
		trace_version_++;
		preview_.pair.reset();
		preview_.has_paths = false;
		preview_.merged.clear();
		preview_.dots.clear();
		return; // (the last result and physical hits stay: PathRenderer keeps them)
	}
	use_defs(renderer_tracer_, renderer_defs_);
	if (!async_) {
		trace_version_++;
		make_preview(preview_, renderer_tracer_.trace_pair(player_, cursor_, items(), flags_));
		return;
	}
	// Stage 121: the float64 tier on the frame; when the ladder would climb, that
	// result shows provisionally and the exact one is computed in the background
	bool exact = true;
	std::optional<rc::TracePair> pair = renderer_tracer_.trace_pair_provisional(player_, cursor_, items(), flags_, &exact);
	if (exact) {
		trace_version_++;
		make_preview(preview_, std::move(pair));
		return;
	}
	Preview p;
	make_preview(p, std::move(pair));
	provisional_ = std::move(p);
	provisional_version_++;
	preview_job_->submit(PreviewJobIn{defs_snapshot(), world_->version(), player_, cursor_, items(), flags_}, preview_ticket_);
}

void Session::make_preview(Preview &p, std::optional<rc::TracePair> pair) const {
	p.pair = std::move(pair);
	if (!p.pair) {
		p.last_result.reset();
		p.has_paths = false;
		p.merged.clear();
		p.dots.clear();
		return;
	}
	p.last_result = std::move(p.pair->result); // (nothing else reads the pair's)
	const std::vector<rc::inv::SurfaceInfo> in = infos();
	p.physical = rc::sweep::prepare_for_display(p.pair->physical, bounds(), in);
	p.planned = p.pair->shared ? p.physical : rc::sweep::prepare_for_display(p.pair->planned, bounds(), in);
	p.has_paths = true;
	int ci = p.planned.cursor_index;
	if (ci < 0) {
		ci = int(p.planned.steps.size());
	}
	rc::sweep::Merged merged = rc::sweep::merge_segments(p.planned.steps, p.physical.steps, ci,
		p.physical.cursor_index >= 0 ? p.physical.cursor_index : int(p.physical.steps.size()));
	p.merged = std::move(merged.steps);
	p.dots = std::move(merged.dots);
	p.hits = rc::physical_hits(p.physical.steps);
	arrow_states(p);
}

// Stage 157 (GAME_SPEC 14, the user): the arrow's states along the preview. The
// physical walk's hits change the flags; each change is placed at the merged
// step it ends (walk order, by point). A state (fire, charge, frost) is marked
// on the arrow's steps while it holds, if it began before the walks diverge;
// the things whose fire or charge changes before they diverge are the preview's
// consequences.
void Session::arrow_states(Preview &p) const {
	p.marks.assign(p.merged.size(), 0);
	p.becomes.clear();
	p.predicted.reset();
	p.trip.reset();
	if (!p.pair) {
		return;
	}
	const rc::TracePath &raw = p.pair->physical;
	struct Change {
		V2 at;
		rc::Flags after;
	};
	// a walk's hits as flag changes: after each one, the next one's flags before (the last: its side's writes)
	auto changes_of = [&](const rc::TracePath &path) {
		std::vector<Change> out;
		for (size_t i = 0; i < path.hits.size(); i++) {
			const rc::HitRecord &h = path.hits[i];
			rc::Flags after;
			if (i + 1 < path.hits.size()) {
				after = path.hits[i + 1].flags_before;
			} else {
				after = h.flags_before;
				const BuiltSurface *s = world_->by_id(h.surface_id);
				const BuiltSide *side = s && h.fired ? active_side(*s, h.side == rc::Side::Left ? Side::Left : Side::Right, h.flags_before) : nullptr;
				if (side != nullptr) {
					after = after.applied(writes(*side, s->id, false));
					if (h.flags_before.is_true(rc::FROST) && side->effect.type == "terminal" && !side->rigid) {
						after.set(rc::FROST, false);
					}
				}
			}
			out.push_back({h.point, std::move(after)});
		}
		return out;
	};
	const std::vector<Change> changes = changes_of(raw);
	static const char *const KEYS[3] = {"shot:fire", "shot:charge", "shot:frost"};
	auto mark_of = [&](const rc::Flags &f) {
		uint8_t mk = 0;
		for (int k = 0; k < 3; k++) {
			if (f.is_true(KEYS[k])) {
				mk = uint8_t(mk | (1 << k));
			}
		}
		return mk;
	};
	// Stage 160 (the user): the arrow's states are path effects -- each walk's own, marked over its own steps
	// (the common ones and its diverged ones), before the divergence and after it. The physical walk:
	rc::Flags cur = flags_, at_divergence = flags_;
	bool diverged = false;
	std::vector<bool> hit_aligned(changes.size(), false);
	size_t c = 0;
	for (size_t m = 0; m < p.merged.size(); m++) {
		const rc::TraceStep &st = p.merged[m];
		const bool aligned = st.type == rc::StepType::Aligned || st.type == rc::StepType::AlignedPostPlanned;
		if (!aligned && st.type != rc::StepType::DivergedPhysical) {
			continue; // (the planned walk's own steps)
		}
		if (!aligned && !diverged) {
			diverged = true;
			at_divergence = cur;
		}
		p.marks[m] = mark_of(cur);
		while (c < changes.size() && changes[c].at.distance_to(st.end) < 0.01f) {
			cur = changes[c].after;
			hit_aligned[c] = aligned;
			c++;
		}
	}
	// ... the planned walk's, over its diverged steps (its common steps are the physical walk's)
	if (diverged) {
		const std::vector<Change> planned = p.pair->shared ? changes : changes_of(p.pair->planned);
		rc::Flags pc = flags_;
		size_t q = 0;
		for (size_t m = 0; m < p.merged.size(); m++) {
			const rc::TraceStep &st = p.merged[m];
			const bool aligned = st.type == rc::StepType::Aligned || st.type == rc::StepType::AlignedPostPlanned;
			if (!aligned && st.type != rc::StepType::DivergedPlanned && st.type != rc::StepType::DivergedPostPlanned) {
				continue;
			}
			if (!aligned) {
				p.marks[m] = mark_of(pc);
			}
			while (q < planned.size() && planned[q].at.distance_to(st.end) < 0.01f) {
				pc = planned[q].after;
				q++;
			}
		}
	}
	if (!diverged) {
		at_divergence = cur;
	}
	// Stage 159: the shot's lasting changes to the divergence, and the journey it starts there
	{
		rc::Flags landed = at_divergence;
		std::vector<std::string> per_shot;
		for (const auto &[k, v] : landed.entries()) {
			if (k.rfind("shot:", 0) == 0) {
				per_shot.push_back(k);
			}
		}
		for (const std::string &k : per_shot) {
			landed.erase(k);
		}
		landed.erase("ghost");
		int hit = -1;
		if (std::optional<Journey> j = journey_of(raw, &hit); j && hit >= 0 && size_t(hit) < hit_aligned.size() && hit_aligned[size_t(hit)]) {
			Preview::Trip trip;
			trip.body = j->body;
			const bool player = j->body < 0;
			V2 pos = player ? player_ : world_->bodies()[size_t(j->body)].pos;
			const double radius = player ? kinematics::RADIUS : world_->bodies()[size_t(j->body)].object.radius;
			const std::vector<kinematics::Collider> cs = colliders_except(j->body, landed);
			trip.trail.push_back(pos);
			for (int tick = 0; tick < 20000; tick++) {
				const bool was_crossing = j->crossing;
				const bool done = journey_move(*j, pos, cs, radius, 1.0 / 60.0);
				if (was_crossing && !j->crossing) {
					trip.breaks.push_back(trip.trail.size()); // (through a portal: the trail goes on from the exit)
				}
				trip.trail.push_back(pos);
				if (done) {
					// (blocked: stopped short of where it arrives -- a pulled object against the player at the path's
					// end, the player at the grapple -- by a barrier on the way)
					const V2 last = j->path.back();
					const double arrive = j->lead + (j->body >= 0 ? kinematics::RADIUS : 0.0) + 1.0;
					trip.blocked = std::hypot(double(pos.x) - last.x, double(pos.y) - last.y) > arrive;
					break;
				}
			}
			p.trip = std::move(trip);
		}
		p.predicted = std::move(landed);
	}
	auto consider = [&](const std::string &name) {
		int b = 0;
		const std::string fire = "el:" + name + ":fire", charge = "el:" + name + ":charge";
		if (!flags_.is_true(fire) && at_divergence.is_true(fire)) {
			b |= 1;
		}
		if (!flags_.is_true(charge) && at_divergence.is_true(charge)) {
			b |= 2;
		} else if (flags_.is_true(charge) && !at_divergence.is_true(charge)) {
			b |= 4;
		}
		if (b != 0) {
			p.becomes.push_back({name, b});
		}
	};
	for (const Tagged &t : level_.tagged) {
		consider(t.name);
	}
	for (const World::Body &b : world_->bodies()) {
		if (!b.object.tags.empty()) {
			consider(b.object.name);
		}
	}
}

void Session::update_hover() {
	const HoverKey inputs{cursor_, plan_version_, flags_version_};
	if (hover_inputs_ == inputs) {
		return;
	}
	hover_inputs_ = inputs;
	const Hit r = detect_click();
	for (Node &n : nodes_) {
		if (n.serial == hovered_serial_ || n.serial == partner_serial_) {
			n.hover.clear();
		}
	}
	hovered_serial_ = partner_serial_ = 0;
	edge_point_ = std::nullopt;
	if (const std::optional<V2> q = pole(plan_.size()); q && bounds().has_point(*q)) {
		edge_point_ = q;
	}
	edge_hovered_ = r.infinity;
	origin_point_ = plannable_origin();
	origin_hovered_ = r.origin;
	if (!r.has_surface) {
		return;
	}
	const BuiltSurface &surf = *world_->by_id(r.surface);
	// ChevronOverlayBuilder.build_hover_overlays
	std::vector<std::pair<int, Overlay>> overlays;
	if (const std::optional<OverlayLink> link = overlay_link(surf, r.side, flags_, r.frost)) {
		const int side = int(r.side);
		if (!link->outgoing) {
			overlays.push_back({surf.id, Overlay{side, true, false, -1, HOVER_COLOR, HOVER_COLOR, HOVER_COLOR, -1}});
		} else if (*link->outgoing == link->from) {
			overlays.push_back({surf.id, Overlay{side, true, true, side, HOVER_COLOR, HOVER_COLOR, HOVER_COLOR, -1}});
		} else {
			const int os = int(link->outgoing->side);
			overlays.push_back({surf.id, Overlay{side, true, false, -1, HOVER_COLOR, HOVER_COLOR, HOVER_COLOR, -1}});
			overlays.push_back({link->outgoing->surface, Overlay{os, false, true, os, HOVER_COLOR, HOVER_COLOR, HOVER_COLOR, -1}});
		}
	}
	for (Node &n : nodes_) {
		std::vector<Overlay> mine;
		for (const auto &[id, o] : overlays) {
			if (id == n.surface) {
				mine.push_back(o);
			}
		}
		if (n.surface == surf.id && r.piece_index >= 0 && n.piece != r.piece_index) {
			continue;
		}
		if (!mine.empty()) {
			n.hover = mine;
			(n.surface == surf.id ? hovered_serial_ : partner_serial_) = n.serial;
		}
	}
}

void Session::update_overlays(bool force) {
	const OverlayKey inputs{trace_version_, plan_version_, flags_version_};
	if (!force && overlay_inputs_ == inputs) {
		return;
	}
	overlay_inputs_ = inputs;
	use_defs(gm_tracer_, gm_defs_);
	const std::vector<std::pair<int, Overlay>> overlays = plan_overlays(plan_, preview_.hits, *world_, flags_, gm_tracer_.plan_frosted(items(), flags_));
	auto for_piece = [](int of, int node) { return of < 0 || node < 0 || of == node; };
	for (Node &n : nodes_) {
		std::vector<int> indices;
		for (size_t i = 0; i < plan_.size(); i++) {
			if (plan_[i].surface_id == n.surface && for_piece(plan_[i].piece, n.piece)) {
				indices.push_back(int(i + 1));
			}
		}
		std::vector<Overlay> mine;
		for (const auto &[id, o] : overlays) {
			if (id == n.surface && for_piece(o.piece, n.piece)) {
				mine.push_back(o);
			}
		}
		n.plan = mine;
		n.plan_indices = indices;
	}
	// the edge: the infinity entries' marks
	const int wraps = preview_.last_result ? count_infinity(preview_.last_result->physical) : 0;
	edge_marks_.clear();
	int k = 0;
	const rc::Rect2f b = bounds();
	for (size_t i = 0; i < plan_.size(); i++) {
		const PlanEntry &e = plan_[i];
		if (e.surface_id != rc::INFINITY_ID) {
			continue;
		}
		k++;
		const std::optional<V2> q = pole(i);
		const bool on_point = q && b.has_point(*q);
		edge_marks_.push_back(EdgeMark{int(i + 1), on_point ? *q : e.click, on_point, k <= wraps});
	}
	edge_point_ = std::nullopt;
	if (const std::optional<V2> q = pole(plan_.size()); q && b.has_point(*q)) {
		edge_point_ = q;
	}
	// Stage 132: the origin entries' marks (valid: met by the physical walk)
	const int passes = preview_.last_result ? count_origin(preview_.last_result->physical) : 0;
	origin_marks_.clear();
	int ko = 0;
	for (size_t i = 0; i < plan_.size(); i++) {
		const PlanEntry &e = plan_[i];
		if (e.surface_id != rc::ORIGIN_ID) {
			continue;
		}
		ko++;
		const rc::Tracer::OriginPoint o = origin_at(i);
		origin_marks_.push_back(OriginMark{int(i + 1), o.point ? *o.point : e.click, ko <= passes});
	}
	origin_point_ = plannable_origin();
}

void Session::update_visibility() {
	if (!level_.features.preview) {
		return;
	}
	const VisKey inputs{player_, plan_version_, flags_version_, world_->version(), visibility_on()};
	if (vis_computed_ && vis_inputs_ == inputs) {
		return;
	}
	vis_inputs_ = inputs;
	vis_computed_ = true;
	vis_ticket_++; // (a region still computing is for older inputs now)
	const rc::Rect2f b = bounds();
	const rc::vis::Rect vb{double(b.position.x), double(b.position.y), double(b.position.x) + double(b.size.x), double(b.position.y) + double(b.size.y)};
	if (async_ && visibility_on()) {
		// Stage 121: in the background; the last region shows meanwhile
		VisJobIn in{defs_snapshot(), world_->version(), player_, items(), flags_, vb};
		if (prefetch_in_ && same_inputs(*prefetch_in_, in)) {
			// the hovered side's region, prefetched: shown now if it is ready, when it is if it runs
			prefetch_in_.reset();
			if (auto r = prefetch_job_->take(); r && r->first == prefetch_ticket_) {
				apply_region(vis_ticket_, std::move(r->second));
				return;
			}
			if (prefetch_job_->busy()) {
				adopted_ = std::make_pair(prefetch_ticket_, vis_ticket_);
				return;
			}
		}
		if (prefetch_job_ && !adopted_) {
			prefetch_job_->cancel(); // (an adopted one runs on, like a region job)
		}
		prefetch_in_.reset();
		// (regions that fit the budget: the frame waits for this one, so a running older
		// one is stale -- cancelled; slower ones: kept, a walking player still sees regions)
		vis_job_->submit(std::move(in), vis_ticket_, budget_ > 0.0 && region_times_.n > 0 && region_times_.typical() <= budget_);
		return;
	}
	vis_applied_ = vis_ticket_; // (no older region computing in the background may show after this one)
	visibility_version_++;
	region_.reset();
	outline_ = rc::vis::Outline{};
	fill_ = rc::vis::Fill{};
	whiskers_.clear();
	if (!visibility_on()) {
		return;
	}
	use_defs(vis_tracer_, vis_defs_);
	VisJobOut out = run_visibility(vis_tracer_, player_, items(), flags_, vb, nullptr);
	region_ = std::move(out.region);
	outline_ = std::move(out.outline);
	fill_ = std::move(out.fill);
	whiskers_ = std::move(out.whiskers);
}

Session::VisJobOut Session::run_visibility(rc::Tracer &t, const V2 &player, const std::vector<rc::PlanItem> &items, const rc::Flags &flags,
		const rc::vis::Rect &vb, const std::atomic<bool> *cancel) {
	VisJobOut out;
	const bool was_compute = rc::set_compute_thread(true); // (A.10: the union and the fill too)
	struct Restore {
		bool was;
		~Restore() { rc::set_compute_thread(was); }
	} restore{was_compute};
	out.region = rc::vis::compute(t, rc::vis::Pt{player.x, player.y}, items, flags, vb, cancel);
	if (out.region->cancelled || !out.region->supported) {
		return out;
	}
	// Stage 123: drawn as the union of its cells (no seam between two cells to
	// crack or overlap; overlapping sheets merged)
	out.outline = rc::vis::union_outline(*out.region);
	out.fill = rc::vis::fill_of(out.outline);
	for (const rc::vis::Edge &e : out.region->whiskers) {
		std::vector<rc::vis::Pt> pts;
		rc::vis::tessellate(e, pts);
		pts.push_back(e.b);
		std::vector<V2> line;
		for (const rc::vis::Pt &p : pts) {
			line.push_back(V2::of(p.x, p.y));
		}
		out.whiskers.push_back(line);
	}
	return out;
}

void Session::update_spawn_preview() {
	if (!spawn_preview_) {
		return;
	}
	const SpawnKey inputs{trace_version_, world_->version()};
	if (spawn_inputs_ == inputs) {
		return;
	}
	spawn_inputs_ = inputs;
	spawn_placed_ = json::array();
	spawn_specs_.clear();
	spawn_vanishing_.clear();
	if (!preview_.pair) {
		return;
	}
	const Landing l = fold(preview_.pair->physical.hits, world_->surfaces(), preview_.pair->final_flags, world_->generations());
	spawn_specs_ = l.placed;
	for (const Placed &p : l.placed) {
		spawn_placed_.push_back({{"channel", p.channel}, {"generation", p.generation}, {"spec", digest::spec_data(p.spec)}});
	}
	for (const BuiltSurface &s : world_->surfaces()) {
		if (!s.presence.empty() && present(s, flags_) && !present(s, l.flags)) {
			spawn_vanishing_.push_back(s.id);
		}
	}
}

// Stage 122: an asynchronous frame gives the background work it started its
// budget: what finishes in time is shown this frame, as a synchronous one would
// (on time: nothing for the presenter to smooth); the rest arrives later, and
// later frames do not wait for it again
void Session::settle() {
	if (!(player_ == settled_player_)) {
		moved_at_ = std::chrono::steady_clock::now();
		settled_player_ = player_;
	}
	const long long vis0 = vis_ticket_, preview0 = preview_ticket_;
	settle_once();
	if (!async_ || budget_ <= 0.0) {
		return;
	}
	// only the work this frame started (work already late is not waited for again).
	// Stage 124: whether it is still busy does not matter -- work that finished
	// before this check still has to be collected (a region done that fast waited
	// a frame uncollected)
	const bool preview_busy = preview_ticket_ != preview0 && preview_job_ && climb_times_.typical() <= budget_;
	// (regions that typically take longer than the budget: no use waiting for this one;
	// one slow region among fast ones is not typical)
	const bool region_busy = vis_ticket_ != vis0 && vis_job_ && region_times_.typical() <= budget_;
	if (!preview_busy && !region_busy) {
		return;
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(budget_));
	if (preview_busy) {
		preview_job_->wait_idle_until(deadline);
	}
	if (region_busy) {
		vis_job_->wait_idle_until(deadline);
	}
	settle_once();
}

void Session::settle_once() {
	collect_jobs();
	update_preview();
	update_hover();
	update_overlays(false);
	update_visibility();
	update_spawn_preview();
	update_prefetch();
	// the player faces the way the arrow will leave: the preview's first step, else the cursor
	facing_ = (cursor_ - player_).angle();
	if (const Preview &sp = shown(); sp.has_paths && !sp.planned.steps.empty()) {
		const V2 d = start_direction(sp.planned.steps.front());
		if (d.length_squared() > 0.0f) {
			facing_ = d.angle();
		}
	}
}

std::vector<std::pair<V2, Color>> Session::truncation_markers(const Preview &pv) const {
	std::vector<std::pair<V2, Color>> out;
	if (!pv.has_paths) {
		return out;
	}
	for (const rc::TracePath *path : {&pv.physical, &pv.planned}) {
		if (!path->truncated || path->steps.empty()) {
			continue;
		}
		const V2 end = path->steps.back().end;
		if (std::any_of(out.begin(), out.end(), [&](const auto &m) { return m.first == end; })) {
			continue;
		}
		Color col = WHITE;
		for (const rc::TraceStep &ms : pv.merged) {
			if (ms.end == end) {
				col = step_color(ms.type);
			}
		}
		out.push_back({end, col});
	}
	return out;
}

json Session::record(const json &extra) const {
	Session &self = const_cast<Session &>(*this); // the click detector and the pole read the tracer's caches
	json r = json::object();
	r["player"] = digest::vec(player_);
	r["cursor"] = level_.features.cursor ? digest::vec(cursor_) : json(nullptr);
	r["facing"] = double(facing_);
	r["camera"] = level_.camera ? digest::vec(player_) : json(nullptr);
	r["display_mode"] = level_.features.preview ? display_mode_ : -1;
	r["gravity"] = digest::vec(gravity_);
	r["visibility_on"] = visibility_on();
	r["flying"] = false;
	json plan = json::array();
	for (const PlanEntry &e : plan_) {
		json span = nullptr;
		if (e.span) {
			span = json::array();
			for (double x : *e.span) {
				span.push_back(x);
			}
		}
		plan.push_back(json::array({e.surface_id, int(e.side), digest::vec(e.click), e.piece, span}));
	}
	r["plan"] = plan;
	r["hud"] = level_.features.hud ? json(hud_) : json(nullptr);
	r["flags"] = flags_json(flags_);
	json targets = json::array();
	std::vector<int> ids;
	for (const auto &t : targets_) {
		ids.push_back(t.first);
	}
	std::sort(ids.begin(), ids.end());
	for (int id : ids) {
		targets.push_back(id);
	}
	r["targets"] = targets;
	if (level_.objective) {
		r["won"] = won_; // Stage 145 (only a level with a goal: the older records are unchanged)
	}
	if (!level_.motions.empty()) {
		r["phase"] = phase_; // Stage 151 (only a level with a cycle)
		r["cycle_auto"] = cycle_auto_;
	}
	r["checkpoints"] = int(checkpoints_.size());
	const std::optional<V2> q = self.pole(plan_.size());
	r["pole"] = q ? digest::vec(*q) : json(nullptr);
	const Hit h = self.detect_click();
	r["hover"] = h.infinity ? json{{"infinity", true}}
		: h.has_surface ? json{{"surface", h.surface}, {"side", int(h.side)}, {"piece", h.piece_index}}
						: json::object();
	// the world
	json wids = json::array();
	for (const BuiltSurface &s : world_->surfaces()) {
		wids.push_back(s.id);
	}
	json gens = json::object();
	for (const auto &[ch, n] : world_->generations()) {
		gens[ch] = n;
	}
	const Pieces &pieces = world_->pieces();
	json ps = json::array();
	for (const BuiltSurface &s : world_->surfaces()) {
		for (const Piece &p : pieces.of(s.id)) {
			json span = nullptr;
			if (p.span) {
				span = json::array();
				for (double x : *p.span) {
					span.push_back(x);
				}
			}
			ps.push_back(json::array({s.id, p.index, digest::vec(p.segment.s), digest::vec(p.segment.e), digest::vec(p.segment.v), span}));
		}
	}
	json xs = json::array();
	for (const V2 &x : pieces.crossings) {
		xs.push_back(digest::vec(x));
	}
	r["world"] = {{"ids", wids}, {"generations", gens}, {"defs", digest::sha(describe_level(world_->surfaces()))},
		{"pieces", digest::sha({{"pieces", ps}, {"crossings", xs}})}};
	// the preview
	if (level_.features.preview) {
		const bool paths = preview_.has_paths;
		auto path_json = [&](const rc::TracePath &p) -> json {
			return paths ? json{{"steps", steps_json(p.steps)}, {"cursor_index", p.cursor_index}, {"truncated", p.truncated}} : json(nullptr);
		};
		json hits = json::object();
		if (paths) {
			for (const auto &[id, list] : preview_.hits) {
				json l = json::array();
				for (const rc::PhysicalHit &ph : list) {
					l.push_back({{"side", ph.side}, {"on_segment", ph.on_segment}, {"has_continuation", ph.has_continuation}, {"point", digest::vec(ph.point)}});
				}
				hits[std::to_string(id)] = l;
			}
		}
		json stops = json::array();
		for (const auto &[pt, col] : truncation_markers()) {
			stops.push_back({{"point", digest::vec(pt)}, {"color", color_json(col)}});
		}
		const json all = {
			{"physical", path_json(preview_.physical)},
			{"planned", path_json(preview_.planned)},
			{"merged", steps_json(preview_.merged)},
			{"hits", hits},
			{"final_flags", preview_.pair ? flags_json(preview_.pair->final_flags) : json(nullptr)},
			{"cursor_point_agrees", preview_.pair ? json(preview_.pair->cursor_point_agrees) : json(nullptr)},
			{"events", preview_.last_result ? json::array({events_json(preview_.last_result->physical), events_json(preview_.last_result->planned)}) : json(nullptr)},
			{"stops", stops},
		};
		r["preview_n"] = json::array({paths ? int(preview_.physical.steps.size()) : -1, paths ? int(preview_.planned.steps.size()) : -1,
			int(preview_.merged.size()), paths ? preview_.planned.cursor_index : -1});
		r["preview"] = digest::sha(all);
	} else {
		r["preview_n"] = json::array();
		r["preview"] = digest::sha(nullptr);
	}
	// the surface nodes, by (surface id, piece)
	std::vector<const Node *> nodes;
	for (const Node &n : nodes_) {
		nodes.push_back(&n);
	}
	std::sort(nodes.begin(), nodes.end(), [](const Node *a, const Node *b) { return a->surface < b->surface || (a->surface == b->surface && a->piece < b->piece); });
	json rows = json::array();
	for (const Node *n : nodes) {
		json hov = json::array(), pl = json::array(), idx = json::array();
		for (const Overlay &o : n->hover) {
			hov.push_back(overlay_json(o));
		}
		for (const Overlay &o : n->plan) {
			pl.push_back(overlay_json(o));
		}
		for (int i : n->plan_indices) {
			idx.push_back(i);
		}
		rows.push_back(json::array({n->surface, n->piece, hov, pl, idx}));
	}
	r["overlays"] = digest::sha(rows);
	r["edge"] = {{"hovered", edge_hovered_}, {"point", edge_point_ ? digest::vec(*edge_point_) : json(nullptr)}, {"marks", [&] {
		json marks = json::array();
		for (const EdgeMark &m : edge_marks_) {
			marks.push_back({{"index", m.index}, {"point", digest::vec(m.point)}, {"on_point", m.on_point}, {"valid", m.valid}});
		}
		return marks;
	}()}};
	// Stage 132: the ray's origin (recorded where there is one to show)
	if (origin_point_ || origin_hovered_ || !origin_marks_.empty()) {
		json marks = json::array();
		for (const OriginMark &m : origin_marks_) {
			marks.push_back({{"index", m.index}, {"point", digest::vec(m.point)}, {"valid", m.valid}});
		}
		r["origin"] = {{"hovered", origin_hovered_}, {"point", origin_point_ ? digest::vec(*origin_point_) : json(nullptr)}, {"marks", marks}};
	}
	// the visibility region
	if (!level_.features.preview) {
		r["visibility"] = nullptr;
	} else if (!region_ || !region_->supported) {
		r["visibility"] = {{"region", region_.has_value()}, {"supported", false}};
	} else {
		json points = json::array();
		for (const rc::vis::Pt &p : fill_.points) {
			points.push_back(digest::vec(V2::of(p.x, p.y)));
		}
		json indices = json::array();
		for (uint32_t i : fill_.indices) {
			indices.push_back(int(i));
		}
		json whiskers = json::array();
		for (const std::vector<V2> &line : whiskers_) {
			json l = json::array();
			for (const V2 &p : line) {
				l.push_back(digest::vec(p));
			}
			whiskers.push_back(l);
		}
		r["visibility"] = {{"region", true}, {"supported", true}, {"n", json::array({int(fill_.points.size()), int(fill_.indices.size()), int(whiskers_.size())})},
			{"fill", digest::sha({{"points", points}, {"indices", indices}})}, {"whiskers", digest::sha(whiskers)}};
	}
	// the spawn preview
	if (spawn_preview_) {
		json vanishing = json::array();
		for (int id : spawn_vanishing_) {
			vanishing.push_back(id);
		}
		r["spawn_n"] = json::array({int(spawn_placed_.size()), int(spawn_vanishing_.size())});
		r["spawn"] = digest::sha({{"placed", spawn_placed_}, {"vanishing", vanishing}});
	} else {
		r["spawn_n"] = json::array();
		r["spawn"] = digest::sha(nullptr);
	}
	for (auto it = extra.begin(); it != extra.end(); ++it) {
		r[it.key()] = it.value();
	}
	return r;
}

} // namespace rcg
