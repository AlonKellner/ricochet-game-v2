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
#include "rcg/kinematics.hpp"
#include "rcg/landing.hpp"
#include "rcg/model.hpp"
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

enum class Action { Undo, Reset, ClearPlan, CycleDisplay, ToggleGravity, ToggleVisibility, Any, Fire };
// the session scripts' names (undo, reset, clear_plan, cycle_display, toggle_gravity, toggle_visibility, any)
std::optional<Action> action_of(const std::string &name);

struct SessionConfig {
	std::optional<bool> infinity;   // the switches (default: the game's: on)
	std::optional<bool> visibility;
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

	// the derived state as the game's nodes reach it after their frames
	void settle();
	// the digest record (after settle()); extra: the step's own fields
	nlohmann::json record(const nlohmann::json &extra) const;

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
	std::vector<std::pair<V2, std::array<float, 4>>> truncation_markers() const;

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
	nlohmann::json edge_marks_ = nlohmann::json::array();
	// the derived state and the inputs it was made from
	Preview preview_;
	nlohmann::json preview_inputs_;
	int trace_version_ = 0;
	nlohmann::json hover_inputs_, overlay_inputs_, vis_inputs_, spawn_inputs_;
	bool vis_computed_ = false;
	std::optional<rc::vis::Result> region_;
	rc::vis::Fill fill_;
	std::vector<std::vector<V2>> whiskers_;
	nlohmann::json spawn_placed_ = nlohmann::json::array();
	std::vector<int> spawn_vanishing_;
	bool spawn_preview_ = false;
	float facing_ = 0.0f;
	// a fired shot: its final flags and physical trace (landed when the arrow is)
	std::optional<rc::Flags> pending_flags_;
	rc::TracePath pending_path_;
	bool on_floor_ = false;
	V2 arrow_pos_;
	V2 arrow_dir_{1.0f, 0.0f};
};

} // namespace rcg
