#include "rcg/session.hpp"

#include "rcg/digest.hpp"

#include <algorithm>
#include <cmath>

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
	: level_(level), infinity_override_(config.infinity), visibility_override_(config.visibility) {
	world_ = std::make_unique<World>(build_level(level_));
	player_ = level_.player_start.value_or(V2{});
	gravity_ = level_.gravity;
	for (rc::Tracer *t : {&gm_tracer_, &renderer_tracer_, &vis_tracer_}) {
		t->set_emit_infinity(infinity_on());
	}
	// the initial checkpoint: taken before the level built its world (GameManager._ready)
	initial_ = Checkpoint{player_, V2{}, rc::Flags{}, {}, {}, std::nullopt};
	for (const BuiltSurface &s : world_->surfaces()) {
		for (const BuiltSide *side : s.all_sides()) {
			for (const HitSpec &h : side ? side->hits : std::vector<HitSpec>{}) {
				spawn_preview_ = spawn_preview_ || h.kind == "spawn";
			}
		}
	}
	sync_nodes();
}

void Session::use_defs(rc::Tracer &t, int &held) {
	if (held != world_->version()) {
		t.set_defs(world_->defs());
		held = world_->version();
	}
}

bool Session::infinity_on() const { return infinity_override_.value_or(true); }
bool Session::visibility_on() const { return visibility_override_.value_or(true); }
rc::Rect2f Session::bounds() const { return rc::Rect2f::of(level_.bounds.x, level_.bounds.y, level_.bounds.w, level_.bounds.h); }

std::vector<rc::PlanItem> Session::items(size_t n) const {
	std::vector<rc::PlanItem> out;
	for (size_t i = 0; i < n && i < plan_.size(); i++) {
		const PlanEntry &e = plan_[i];
		rc::PlanItem p;
		p.surface_id = e.surface_id;
		p.left = e.side == Side::Left;
		if (std::isfinite(e.click.x) && std::isfinite(e.click.y)) {
			p.hint = rc::effects::F32Point{e.click.x, e.click.y};
		}
		p.span = e.span;
		out.push_back(p);
	}
	return out;
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
	velocity_ = V2{};
}

std::optional<V2> Session::pole(size_t n) {
	if (!infinity_on()) {
		return std::nullopt;
	}
	use_defs(gm_tracer_, gm_defs_);
	return gm_tracer_.infinity_point(items(n), flags_);
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
			if (cfg == nullptr || !cfg->interactive) {
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
			}
		}
	}
	if (infinity_on()) {
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
		} else if (!best.has_surface && has_area(b)) {
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

void Session::click(bool right) {
	const Hit r = detect_click();
	if (!r.infinity && !r.has_surface) {
		if (!right) {
			return;
		}
		plan_.clear();
		plan_changed();
	} else {
		const int sid = r.infinity ? rc::INFINITY_ID : r.surface;
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
			plan_.push_back(e);
			plan_changed();
		}
	}
	refresh_ui();
}

Session::Checkpoint Session::capture() const {
	return Checkpoint{player_, velocity_, flags_, plan_, targets_, world_->snapshot()};
}

void Session::restore(const Checkpoint &c) {
	player_ = c.player;
	velocity_ = c.velocity;
	flags_ = c.flags;
	world_->restore(c.world ? *c.world : world_->initial_snapshot());
	sync_nodes();
	plan_ = c.plan;
	plan_changed();
	targets_ = c.targets;
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
	flags_ = kept;
	targets_.clear();
	for (const auto &[k, v] : flags_.entries()) {
		const bool *b = std::get_if<bool>(&v);
		if (k.rfind("target:", 0) == 0 && b && *b) {
			targets_.push_back({std::stoi(k.substr(7)), true});
		}
	}
	pending_flags_.reset();
	// Plan entries on surfaces no longer in the world, or absent, are dropped (the ∞ entry stays)
	const size_t before = plan_.size();
	plan_.erase(std::remove_if(plan_.begin(), plan_.end(),
					[&](const PlanEntry &e) {
						if (e.surface_id == rc::INFINITY_ID) {
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

Flight Session::fire(int speedup_at, double frame_dt) {
	Flight f;
	if (player_ == cursor_) {
		return f;
	}
	checkpoints_.push_back(capture());
	use_defs(gm_tracer_, gm_defs_);
	const std::optional<rc::TracePair> pair = gm_tracer_.trace_pair(player_, cursor_, items(), flags_);
	if (!pair) {
		checkpoints_.pop_back();
		return f;
	}
	pending_flags_ = pair->final_flags;
	pending_path_ = pair->physical;
	if (!level_.features.arrow) {
		land(); // no arrow to animate: the shot lands at once
		return f;
	}
	const rc::TracePath path = rc::sweep::prepare_for_display(pair->physical, bounds(), infos());
	std::vector<rc::arc::StepGeom> geoms;
	for (const rc::TraceStep &s : path.steps) {
		geoms.push_back(rc::sweep::step_geom(s));
	}
	if (!path.steps.empty()) {
		arrow_pos_ = path.steps.front().start;
		arrow_dir_ = start_direction(path.steps.front());
	}
	int index = 0;
	double progress = 0.0;
	double multiplier = 1.0;
	while (true) {
		f.stream.push_back({arrow_pos_, arrow_dir_});
		f.frames++;
		const bool speed_up = f.frames == speedup_at;
		const double distance = ARROW_SPEED * multiplier * frame_dt;
		const rc::arc::AdvanceResult r = rc::arc::advance(geoms, index, progress, arrow_pos_, distance, bounds());
		index = r.step_index;
		progress = r.progress;
		arrow_pos_ = r.position;
		arrow_dir_ = r.direction;
		if (speed_up) {
			multiplier *= 10.0;
		}
		if (r.finished) {
			land();
			break;
		}
	}
	return f;
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
	if (plan_.empty()) {
		return "";
	}
	std::string out = "Plan:";
	for (size_t i = 0; i < plan_.size(); i++) {
		const PlanEntry &e = plan_[i];
		out += "\n  " + std::to_string(i + 1) + ". ";
		if (e.surface_id == rc::INFINITY_ID) {
			out += "\xe2\x88\x9e"; // ∞
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
	json entries = json::array();
	for (const PlanEntry &e : plan_) {
		entries.push_back(json::array({e.surface_id, int(e.side), digest::vec(e.click)}));
	}
	const json inputs = json::array({digest::vec(player_), digest::vec(cursor_), entries, digest::encode(flags_json(flags_)), world_->version()});
	if (preview_.computed && inputs == preview_inputs_) {
		return;
	}
	preview_inputs_ = inputs;
	preview_.computed = true;
	trace_version_++;
	if (player_ == cursor_) {
		preview_.pair.reset();
		preview_.has_paths = false;
		preview_.merged.clear();
		return; // (the last result and physical hits stay: PathRenderer keeps them)
	}
	use_defs(renderer_tracer_, renderer_defs_);
	preview_.pair = renderer_tracer_.trace_pair(player_, cursor_, items(), flags_);
	if (!preview_.pair) {
		preview_.last_result.reset();
		preview_.has_paths = false;
		preview_.merged.clear();
		return;
	}
	preview_.last_result = preview_.pair->result;
	const std::vector<rc::inv::SurfaceInfo> in = infos();
	preview_.physical = rc::sweep::prepare_for_display(preview_.pair->physical, bounds(), in);
	preview_.planned = rc::sweep::prepare_for_display(preview_.pair->shared ? preview_.pair->physical : preview_.pair->planned, bounds(), in);
	preview_.has_paths = true;
	int ci = preview_.planned.cursor_index;
	if (ci < 0) {
		ci = int(preview_.planned.steps.size());
	}
	preview_.merged = rc::sweep::merge(preview_.planned.steps, preview_.physical.steps, ci);
	preview_.hits = rc::physical_hits(preview_.physical.steps);
}

void Session::update_hover() {
	const json inputs = json::array({digest::vec(cursor_), plan_version_, digest::encode(flags_json(flags_))});
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
	if (!r.has_surface) {
		return;
	}
	const BuiltSurface &surf = *world_->by_id(r.surface);
	// ChevronOverlayBuilder.build_hover_overlays
	std::vector<std::pair<int, Overlay>> overlays;
	if (const std::optional<OverlayLink> link = overlay_link(surf, r.side, flags_)) {
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
	const json inputs = json::array({trace_version_, plan_version_, digest::encode(flags_json(flags_))});
	if (!force && overlay_inputs_ == inputs) {
		return;
	}
	overlay_inputs_ = inputs;
	// ChevronOverlayBuilder.build_plan_overlays
	std::vector<std::pair<int, Overlay>> overlays;
	auto hits_of = [&](int id) -> const std::vector<rc::PhysicalHit> * {
		for (const auto &e : preview_.hits) {
			if (e.first == id) {
				return &e.second;
			}
		}
		return nullptr;
	};
	for (size_t i = 0; i < plan_.size(); i++) {
		const PlanEntry &e = plan_[i];
		const BuiltSurface *s = world_->by_id(e.surface_id);
		if (s == nullptr) {
			continue;
		}
		const std::optional<OverlayLink> link = overlay_link(*s, e.side, flags_);
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
	edge_marks_ = json::array();
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
		edge_marks_.push_back({{"index", int(i + 1)}, {"point", digest::vec(on_point ? *q : e.click)}, {"on_point", on_point}, {"valid", k <= wraps}});
	}
	edge_point_ = std::nullopt;
	if (const std::optional<V2> q = pole(plan_.size()); q && b.has_point(*q)) {
		edge_point_ = q;
	}
}

void Session::update_visibility() {
	if (!level_.features.preview) {
		return;
	}
	json entries = json::array();
	for (const PlanEntry &e : plan_) {
		entries.push_back(json::array({e.surface_id, int(e.side), digest::vec(e.click), e.piece}));
	}
	const json inputs = json::array({digest::vec(player_), entries, digest::encode(flags_json(flags_)), world_->version(), infinity_on(), visibility_on()});
	if (vis_computed_ && vis_inputs_ == inputs) {
		return;
	}
	vis_inputs_ = inputs;
	vis_computed_ = true;
	region_.reset();
	fill_ = rc::vis::Fill{};
	whiskers_.clear();
	if (!visibility_on()) {
		return;
	}
	use_defs(vis_tracer_, vis_defs_);
	const rc::Rect2f b = bounds();
	const rc::vis::Rect vb{double(b.position.x), double(b.position.y), double(b.position.x) + double(b.size.x), double(b.position.y) + double(b.size.y)};
	region_ = rc::vis::compute(vis_tracer_, rc::vis::Pt{player_.x, player_.y}, items(), flags_, vb);
	if (!region_->supported) {
		return;
	}
	fill_ = rc::vis::fill_triangles(*region_);
	for (const rc::vis::Edge &e : region_->whiskers) {
		std::vector<rc::vis::Pt> pts;
		rc::vis::tessellate(e, pts);
		pts.push_back(e.b);
		std::vector<V2> line;
		for (const rc::vis::Pt &p : pts) {
			line.push_back(V2::of(p.x, p.y));
		}
		whiskers_.push_back(line);
	}
}

void Session::update_spawn_preview() {
	if (!spawn_preview_) {
		return;
	}
	const json inputs = json::array({trace_version_, world_->version()});
	if (spawn_inputs_ == inputs) {
		return;
	}
	spawn_inputs_ = inputs;
	spawn_placed_ = json::array();
	spawn_vanishing_.clear();
	if (!preview_.pair) {
		return;
	}
	const Landing l = fold(preview_.pair->physical.hits, world_->surfaces(), preview_.pair->final_flags, world_->generations());
	for (const Placed &p : l.placed) {
		spawn_placed_.push_back({{"channel", p.channel}, {"generation", p.generation}, {"spec", digest::spec_data(p.spec)}});
	}
	for (const BuiltSurface &s : world_->surfaces()) {
		if (!s.presence.empty() && present(s, flags_) && !present(s, l.flags)) {
			spawn_vanishing_.push_back(s.id);
		}
	}
}

void Session::settle() {
	update_preview();
	update_hover();
	update_overlays(false);
	update_visibility();
	update_spawn_preview();
	// the player faces the way the arrow will leave: the preview's first step, else the cursor
	facing_ = (cursor_ - player_).angle();
	if (preview_.has_paths && !preview_.planned.steps.empty()) {
		const V2 d = start_direction(preview_.planned.steps.front());
		if (d.length_squared() > 0.0f) {
			facing_ = d.angle();
		}
	}
}

std::vector<std::pair<V2, Color>> Session::truncation_markers() const {
	std::vector<std::pair<V2, Color>> out;
	if (!preview_.has_paths) {
		return out;
	}
	for (const rc::TracePath *path : {&preview_.physical, &preview_.planned}) {
		if (!path->truncated || path->steps.empty()) {
			continue;
		}
		const V2 end = path->steps.back().end;
		if (std::any_of(out.begin(), out.end(), [&](const auto &m) { return m.first == end; })) {
			continue;
		}
		Color col = WHITE;
		for (const rc::TraceStep &ms : preview_.merged) {
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
	r["infinity"] = infinity_on();
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
	r["edge"] = {{"hovered", edge_hovered_}, {"point", edge_point_ ? digest::vec(*edge_point_) : json(nullptr)}, {"marks", edge_marks_}};
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
