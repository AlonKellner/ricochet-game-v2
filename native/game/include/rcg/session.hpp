// Stage 116.5: a game session natively (the port of game_manager.gd and of
// what the level's nodes derive from it: the preview, the hover and plan
// overlays, the edge marks, the HUD text, the visibility region, the spawn
// preview, the arrow's flight, the player's facing). Its record after a step
// is the session replay golden's (docs/research/session_digest.md): the
// native session reproduces bench/sessions byte for byte (rc_tool replay).
//
// The host feeds events (a click, a key action) and positions (the cursor; a
// teleported player); `settle()` brings the derived state up to date (what the
// GDScript nodes reach after their frames), `fire()` plays a flight frame by
// frame. The player's movement (the kinematic controller) is a later stage:
// teleport sessions only.
#pragma once

#include "rc/sweep.hpp"
#include "rc/tracer.hpp"
#include "rc/visibility.hpp"
#include "rcg/jobs.hpp"
#include "rcg/kinematics.hpp"
#include "rcg/landing.hpp"
#include "rcg/model.hpp"
#include "rcg/render.hpp"
#include "rcg/world.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rcg {

// A plan entry (PlanManager.PlanEntry): a surface side (INFINITY_ID: the ∞
// entry), where it was clicked (inf: unknown), the piece (-1: the whole surface)
// and its span.
struct PlanEntry {
	int surface_id = 0;
	Side side = Side::Left;
	V2 click = V2::inf();
	int piece = -1;
	std::optional<std::array<double, 6>> span;
};

// A chevron overlay (ChevronOverlay): float32 colours as [r, g, b, a].
struct Overlay {
	int side = -1;
	bool has_incoming = true;
	bool has_outgoing = true;
	int outgoing_side = -1;
	std::array<float, 4> incoming_color{1, 1, 1, 1};
	std::array<float, 4> outgoing_color{1, 1, 1, 1};
	std::array<float, 4> gradient_color{1, 1, 1, 1};
	int piece = -1;
	bool operator==(const Overlay &) const = default;
};

// ChevronOverlayBuilder.build_plan_overlays: the plan entries' chevrons, as
// (surface id, overlay) in plan order -- incoming valid when the physical
// preview hits the planned side on the segment (and on the entry's piece),
// outgoing valid when such a hit continues; hits: the physical hits by surface.
std::vector<std::pair<int, Overlay>> plan_overlays(const std::vector<PlanEntry> &plan,
		const std::vector<std::pair<int, std::vector<rc::PhysicalHit>>> &hits, const World &world, const rc::Flags &flags);

enum class Action { Undo, Reset, ClearPlan, CycleDisplay, ToggleGravity, ToggleVisibility, Any, Fire };
// the session scripts' names (undo, reset, clear_plan, cycle_display, toggle_gravity, toggle_visibility, any)
std::optional<Action> action_of(const std::string &name);

struct SessionConfig {
	std::optional<bool> infinity;   // the switches (default: the game's: on)
	std::optional<bool> visibility;
	// Stage 121: a live host's session -- the visibility region and the preview's
	// exact climbs are computed in the background (the frame shows the last
	// region, a provisional float64 preview meanwhile); off (the default): all in
	// the frame, in order (replays, tests: the session golden)
	bool async = false;
	// Stage 121 (async): the hovered side's region computed while idle, for its click
	bool prefetch = true;
};

struct Flight {
	int frames = 0;
	std::vector<std::pair<V2, V2>> stream; // the arrow's position and direction before every advance
};

class Session {
public:
	Session(const Level &level, const SessionConfig &config = {});

	// inputs
	void set_cursor(const V2 &p) { cursor_ = p; }
	void teleport(const V2 &p);
	void click(bool right);
	void key(Action a);
	// the fire action, then the whole flight at a fixed frame time (speedup_at:
	// the `any` key after that many frames; -1: never)
	Flight fire(int speedup_at, double frame_dt);
	// Stage 117: movement actions (move_left / move_right / move_up / move_down)
	// held for `ticks` physics ticks (the native controller, rcg/kinematics.hpp):
	// the player's position after every tick. The up action registers as just
	// pressed on the second tick (the engine's input timing: a jump with gravity).
	std::vector<V2> hold(const std::vector<std::string> &actions, int ticks);

	// Stage 118: live play, frame by frame (what a host drives; fire() and
	// hold() are these at a fixed frame time). start_fire(): the fire action --
	// false when no shot is fired (the player on the cursor; no trace); a level
	// without the arrow lands at once. While the arrow flies, advance() moves
	// it one frame (the game is paused: no physics, no settling) and
	// speed_up() (any key) makes it ten times faster; otherwise advance() runs
	// the physics ticks due (kinematics::STEP, accumulated) with the input --
	// its jump on the first of them (kept until a tick runs) -- then settles.
	bool start_fire();
	void speed_up();
	void advance(double dt, const kinematics::Input &in);
	bool flying() const { return flying_; }
	V2 player() const { return player_; }
	// Stage 122: the player before the last physics tick, and how far the time
	// not yet ticked is into the next (the presenter draws the player between)
	V2 tick_from() const { return tick_from_; }
	double tick_alpha() const { return physics_time_ / kinematics::STEP; }
	float facing() const { return facing_; }
	V2 arrow_position() const { return arrow_pos_; }
	V2 arrow_direction() const { return arrow_dir_; }
	int display_mode() const { return display_mode_; }
	V2 gravity() const { return gravity_; }
	// the preview's traces and the visibility region's computations so far (a
	// still frame computes neither; a cursor move no region)
	int preview_version() const { return trace_version_; }
	// the ladder's counters of the preview's and the region's tracers (benches; they never steer a solve)
	rc::LadderStats preview_stats() const { return renderer_tracer_.ladder_stats(); }
	rc::LadderStats region_stats() const { return vis_tracer_.ladder_stats(); }
	int visibility_version() const { return visibility_version_; }
	bool has_region() const { return region_.has_value(); }
	const std::optional<rc::vis::Result> &region() const { return region_; } // the region shown (Stage 122: the presenter's)
	const std::string &hud() const { return hud_; }
	const Level &level() const { return level_; }
	// the camera's target: the arrow in flight, else the player
	V2 camera_target() const { return flying_ ? arrow_pos_ : player_; }

	// Stage 121 (async): the background work's state; drain() waits for it and
	// applies its results (then the session is what a synchronous one is)
	bool region_pending() const;
	bool preview_exact() const { return !provisional_.has_value(); }
	void drain();

	// the derived state as the game's nodes reach it after their frames
	void settle();
	// the digest record (after settle()); extra: the step's own fields
	nlohmann::json record(const nlohmann::json &extra) const;
	// Stage 118: what the level's nodes draw (after settle()): the draw list
	// (rcg/render.hpp), one node per thing drawn, in the GDScript nodes' calls
	std::vector<render::Node> draw() const;
	// Stage 120: the same list for a host's frame -- the painters' calls shared and
	// cached by their inputs, each node with its id and content version
	std::vector<render::FrameNode> draw_frame() const;

	const std::vector<PlanEntry> &plan() const { return plan_; }
	const rc::Flags &flags() const { return flags_; }
	const World &world() const { return *world_; }

private:
	struct Node {
		int surface = 0;
		int piece = -1;
		SegGeom segment; // the piece's (its identity: rebuilt when its ends change)
		std::vector<Overlay> hover;
		std::vector<Overlay> plan;
		std::vector<int> plan_indices;
		int serial = 0; // a node made anew gets a new serial (the hover keeps the ones it set)
	};
	struct Hit {
		bool infinity = false;
		bool has_surface = false;
		int surface = 0;
		Side side = Side::Left;
		int piece_index = -1;
		const Piece *piece = nullptr;
		double distance = 0;
		std::optional<V2> point;
	};
	struct Checkpoint {
		V2 player;
		V2 velocity;
		rc::Flags flags;
		std::vector<PlanEntry> plan;
		std::vector<std::pair<int, bool>> targets;
		std::optional<World::Snapshot> world;
	};
	// PathRenderer's state: the last pair, its display paths and merge; the last
	// ladder result and physical hits stay when there is no trace (as the node keeps them)
	struct Preview {
		bool computed = false;
		std::optional<rc::TracePair> pair;
		bool has_paths = false;
		rc::TracePath physical, planned; // display paths
		std::vector<rc::TraceStep> merged;
		std::optional<rc::LadderResult> last_result;
		std::vector<std::pair<int, std::vector<rc::PhysicalHit>>> hits;
	};

	std::vector<rc::PlanItem> items(size_t n) const;
	std::vector<rc::PlanItem> items() const { return items(plan_.size()); }
	std::vector<rc::inv::SurfaceInfo> infos() const;
	bool infinity_on() const;
	bool visibility_on() const;
	rc::Rect2f bounds() const;
	std::optional<V2> pole(size_t n);
	Hit detect_click();
	V2 snap_click(const BuiltSurface &s, Side side, const V2 &click) const;
	void plan_changed() { plan_version_++; }
	void refresh_ui(); // the HUD and the plan overlays
	void update_preview();
	void update_hover();
	void update_overlays(bool force);
	void update_visibility();
	void update_spawn_preview();
	void sync_nodes();
	void land();
	Checkpoint capture() const;
	void restore(const Checkpoint &c);
	std::string hud_text() const;
	std::vector<std::pair<V2, std::array<float, 4>>> truncation_markers(const Preview &p) const;
	std::vector<std::pair<V2, std::array<float, 4>>> truncation_markers() const { return truncation_markers(preview_); }
	// the preview a path is drawn from: the provisional one while an exact climb runs
	const Preview &shown() const { return provisional_ ? *provisional_ : preview_; }
	// a traced pair made into a preview (its display paths, merge, hits)
	void make_preview(Preview &p, std::optional<rc::TracePair> pair) const;
	void collect_jobs();

	Level level_;
	std::unique_ptr<World> world_;
	rc::Tracer gm_tracer_, renderer_tracer_, vis_tracer_;
	int gm_defs_ = -1, renderer_defs_ = -1, vis_defs_ = -1; // the world version each tracer holds
	void use_defs(rc::Tracer &t, int &held);
	V2 player_;
	V2 velocity_;
	V2 cursor_;
	V2 gravity_;
	rc::Flags flags_;
	std::vector<std::pair<int, bool>> targets_; // targets_of(flags), in the flags' order
	std::vector<PlanEntry> plan_;
	int plan_version_ = 0;
	int flags_version_ = 0; // bumped when the flags change (their encoding: the record's)
	void set_flags(const rc::Flags &f);
	std::vector<Checkpoint> checkpoints_;
	Checkpoint initial_;
	int display_mode_ = 0;
	std::optional<bool> infinity_override_, visibility_override_;
	std::string hud_;
	// the nodes (one per piece of every labelled surface), the hover's tracked ones
	std::vector<Node> nodes_;
	int next_serial_ = 1;
	int hovered_serial_ = 0, partner_serial_ = 0;
	bool edge_hovered_ = false;
	std::optional<V2> edge_point_;
	// an infinity entry's mark on the edge (EdgeOverlayNode's marks)
	struct EdgeMark {
		int index = 0;
		V2 point;
		bool on_point = false;
		bool valid = false;
	};
	std::vector<EdgeMark> edge_marks_;
	// the derived state and the inputs it was made from
	Preview preview_;
	// what each derived part was made from (Stage 120: typed keys, not JSON; the
	// plan and the flags by their versions, positions by value as JSON compared them)
	struct PreviewKey {
		V2 player, cursor;
		int plan = 0, flags = 0, world = 0;
		bool operator==(const PreviewKey &) const = default;
	};
	struct HoverKey {
		V2 cursor;
		int plan = 0, flags = 0;
		bool operator==(const HoverKey &) const = default;
	};
	struct OverlayKey {
		int trace = 0, plan = 0, flags = 0;
		bool operator==(const OverlayKey &) const = default;
	};
	struct VisKey {
		V2 player;
		int plan = 0, flags = 0, world = 0;
		bool infinity = false, visibility = false;
		bool operator==(const VisKey &) const = default;
	};
	struct SpawnKey {
		int trace = 0, world = 0;
		bool operator==(const SpawnKey &) const = default;
	};
	std::optional<PreviewKey> preview_inputs_;
	int trace_version_ = 0;
	std::optional<HoverKey> hover_inputs_;
	std::optional<OverlayKey> overlay_inputs_;
	std::optional<VisKey> vis_inputs_;
	std::optional<SpawnKey> spawn_inputs_;
	// pole(n) for the current plan, flags and world (it is asked several times a settle)
	std::vector<std::optional<std::optional<V2>>> pole_cache_;
	std::array<int, 3> pole_key_{-1, -1, -1};
	bool vis_computed_ = false;
	int visibility_version_ = 0;
	std::optional<rc::vis::Result> region_;
	rc::vis::Fill fill_;
	std::vector<std::vector<V2>> whiskers_;
	nlohmann::json spawn_placed_ = nlohmann::json::array();
	std::vector<Placed> spawn_specs_; // the placed specs (the spawn preview's ghosts)
	std::vector<int> spawn_vanishing_;
	bool spawn_preview_ = false;
	float facing_ = 0.0f;
	// a fired shot: its final flags and physical trace (landed when the arrow is)
	std::optional<rc::Flags> pending_flags_;
	rc::TracePath pending_path_;
	bool on_floor_ = false;
	// Stage 121: the background jobs (async): their inputs, and what they make
	struct VisJobIn {
		std::shared_ptr<const std::vector<rc::SurfaceDef>> defs;
		int world = 0;
		bool infinity = true;
		V2 player;
		std::vector<rc::PlanItem> items;
		rc::Flags flags;
		rc::vis::Rect bounds;
	};
	struct VisJobOut {
		std::optional<rc::vis::Result> region;
		rc::vis::Fill fill;
		std::vector<std::vector<V2>> whiskers;
	};
	struct PreviewJobIn {
		std::shared_ptr<const std::vector<rc::SurfaceDef>> defs;
		int world = 0;
		bool infinity = true;
		V2 player, cursor;
		std::vector<rc::PlanItem> items;
		rc::Flags flags;
	};
	bool async_ = false;
	std::optional<Preview> provisional_; // shown while the exact climb runs
	int provisional_version_ = 0;
	long long vis_ticket_ = 0, preview_ticket_ = 0;
	long long vis_applied_ = 0; // the newest region request applied
	// Stage 121: the region a hovered side's plan entry would make, computed while
	// the frame is idle (the click then shows it at once); cancelled by any real region work
	std::optional<VisJobIn> prefetch_in_;
	long long prefetch_ticket_ = 0;
	std::optional<PlanEntry> entry_for(const Hit &r) const; // the entry a left click on r adds
	struct PrefetchKey {
		V2 cursor, player;
		int plan = -1, flags = -1, world = -1;
		long long region = -1;
		bool operator==(const PrefetchKey &) const = default;
	};
	PrefetchKey prefetch_key_;
	std::optional<std::pair<long long, long long>> adopted_; // a running prefetch taken as a region: (its ticket, the region's)
	void update_prefetch();
	void apply_region(long long ticket, VisJobOut &&out);
	static rc::PlanItem item_of(const PlanEntry &e);
	static bool same_inputs(const VisJobIn &a, const VisJobIn &b);
	std::shared_ptr<const std::vector<rc::SurfaceDef>> job_defs_;
	int job_defs_world_ = -1;
	std::shared_ptr<const std::vector<rc::SurfaceDef>> defs_snapshot();
	// the region, its fill and its whiskers (the synchronous update and the job alike)
	static VisJobOut run_visibility(rc::Tracer &t, const V2 &player, const std::vector<rc::PlanItem> &items, const rc::Flags &flags,
			const rc::vis::Rect &bounds, const std::atomic<bool> *cancel);
	bool flying_ = false; // an arrow in flight (the path and its kin dimmed; the arrow drawn)
	V2 arrow_pos_;
	V2 arrow_dir_{1.0f, 0.0f};
	// the flight (ArrowAnimator): the displayed path's steps, where the arrow is along them, its speed
	std::vector<rc::arc::StepGeom> flight_geoms_;
	int flight_index_ = 0;
	double flight_progress_ = 0.0;
	double flight_multiplier_ = 1.0;
	double physics_time_ = 0.0; // the physics ticks' time not yet run
	V2 tick_from_;              // the player before the last tick (Stage 122)
	bool jump_ = false;         // a jump pressed, not yet ticked
	void tick(const kinematics::Input &in);
	// the painters' cached output (draw_frame): a surface piece's by its inputs,
	// the path, the region and the spawn preview by their versions
	struct SurfaceDraw {
		int flags = -1;
		std::vector<Overlay> hover, plan;
		std::vector<int> indices;
		render::Calls calls;
		uint64_t version = 0;
	};
	struct CachedDraw {
		std::array<int, 3> key{-1, -1, -1};
		render::Calls calls;
		uint64_t version = 0;
	};
	mutable std::map<int, SurfaceDraw> surface_draw_; // by node serial
	mutable CachedDraw path_draw_, vis_draw_, spawn_draw_, static_draw_[3];
	void clear_hover();
	// (last: destroyed first, before the state their results go to)
	std::unique_ptr<LatestJob<VisJobIn, VisJobOut>> vis_job_;
	std::unique_ptr<LatestJob<PreviewJobIn, std::optional<rc::TracePair>>> preview_job_;
	std::unique_ptr<LatestJob<VisJobIn, VisJobOut>> prefetch_job_;
};

} // namespace rcg
