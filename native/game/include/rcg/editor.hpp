// The level editor, natively (docs/EDITOR.md; the editor program, docs/TDD_48):
// an rc-level/2 document (rcg/level2.hpp) edited through commands -- JSON, the
// one path every UI action takes, so a script, a test and a click do the same
// -- and through pointer, key and text input in screen pixels, drawn as one
// draw list in screen space (rcg/render.hpp: the editor owns its pan and zoom,
// a host paints at identity). The inspector panel is part of that list. A host
// is a painter and an event forwarder: it keeps no editor state, does the file
// I/O when a save is requested, and plays the document with an ordinary game.
#pragma once

#include "rc/flags.hpp"
#include "rc/visibility.hpp"
#include "rcg/build.hpp"
#include "rcg/level2.hpp"
#include "rcg/render.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rcg {

class Editor {
public:
	enum class Mode { Edit, Play };
	enum class Tool { Select, Line, Arc, Circle, Player, Goal, Detector, Object, CarrierLine, CarrierCircle, Collider }; // (Collider: Stage 164)
	// pointer events and buttons (rc_api.h's RC_PTR_* / RC_BTN_* / RC_MOD_*)
	enum : int { PtrMove = 0, PtrDown = 1, PtrUp = 2 };
	enum : unsigned { BtnLeft = 1, BtnRight = 2, BtnMiddle = 4 };
	enum : unsigned { ModShift = 1, ModCtrl = 2, ModAlt = 4 };
	// keys (rc_api.h's RC_KEY_*): letters and digits as their ASCII upper case, space 32, then these
	enum : int { KeyEscape = 256, KeyEnter = 257, KeyTab = 258, KeyBackspace = 259, KeyDelete = 260, KeyLeft = 261, KeyRight = 262,
		KeyUp = 263, KeyDown = 264, KeyF1 = 290 }; // F1..F12: KeyF1 + 0..11

	struct Error {
		int element = -1; // the element's index; -1: the level
		std::string name; // the element's name
		std::string message;
	};
	struct Status {
		bool dirty = false;
		int errors = 0;
		Mode mode = Mode::Edit;
		Tool tool = Tool::Select;
		bool save_requested = false;
		uint64_t version = 0; // bumped by every change of the document
		V2 play_from;         // where a play-test starts (the player's start)
		bool open_requested = false; // Stage 155: open the level {"op": "open_request"} names
	};

	// A new level (null) or the given document (rc-level/2; rc-level/1 is migrated).
	explicit Editor(const nlohmann::json *document = nullptr);

	// The document in the canonical spelling (rc_tool fmt), and its rc-level/1 lowering.
	const nlohmann::json &document() const { return doc_; }
	std::string document_text() const;
	std::string lowered_text() const; // throws LevelError when the document does not lower

	// One command ({"op": ...}, docs/EDITOR.md): {"ok": true, ...} or {"ok": false, "error": ...}.
	nlohmann::json command(const nlohmann::json &cmd);

	// Input, in screen pixels (the canvas is the window but the panel on the right).
	void pointer(int kind, float x, float y, unsigned buttons, unsigned mods);
	void key(int code, unsigned mods, bool down);
	void text(const std::string &utf8);
	void scroll(float dx, float dy, unsigned mods);
	void resize(float width, float height);

	// What the editor draws (screen space, by z), each node with its content version.
	std::vector<render::FrameNode> draw() const;
	Status status() const;
	const std::vector<Error> &errors() const { return errors_; }

	// The view: world -> screen.
	V2 to_screen(const V2 &w) const;
	V2 to_world(const V2 &s) const;

	static constexpr float PANEL_WIDTH = 340.0f;
	static constexpr float STATUS_HEIGHT = 26.0f;

private:
	struct Drawn {             // an element's surfaces as the game builds them
		std::vector<BuiltSurface> built;      // valid ones (a portal's image included)
		std::vector<rc::arc::SegGeom> broken; // the geometry of ones that do not build
	};
	struct Handle {
		std::string element; // "" with kind "player": the player's start; an item's handle: the JSON pointer of what it moves
		std::string kind;    // start, end, via, mid, center, radius, body, player; an item's: corner0-3, center, radius, a, b, body
		V2 at;
		std::string item;    // Stage 148: the item it belongs to ("" an element's or the player's)
	};
	struct Field {           // a panel row's editable value
		std::string element; // "": the level
		std::string key;     // an element key, or a JSON pointer into the document (level fields)
	};
	struct Row {
		enum Kind { Header, Text, Value, Error, Buttons, Chips, Nav, Level, Tools, Actions, Complete, Found } kind = Text; // Complete (Stage 217): a flag the focused field may take; Found (Stage 219): what Ctrl+F finds // Actions (Stage 203): options run their cmds // Tools (Stage 161): the tools, one row of them // Chips: the palette's kinds (options), one row of them
		std::string label, value, hint;
		std::optional<Field> field;
		std::string choice; // a value that cycles ("enum", "bool", "kind", "tool", "place", "options", "shape") or "" (typed)
		int error_element = -1;
		float y = 0;
		std::vector<std::string> options; // "options": the values it cycles through (strings)
		std::vector<int> marks = {};      // Stage 167: a level row's characters the search matched
		std::vector<nlohmann::json> cmds = {}; // Stage 203: an Actions row's commands, one per option
		int lit = -1;                          // ... the option lit (Stage 204: the flag it has)
		std::vector<char> on = {};             // ... the options lit, several (Stage 215: the tags it has)
		std::string draft = {};                // Stage 218: the text editing a default starts from (none: empty)
	};

	// the document
	void set_document(nlohmann::json doc, bool clean);
	void change(const std::function<void(nlohmann::json &)> &edit); // one undoable edit, then refresh
	void refresh();
	int index_of(const std::string &name) const;
	std::string fresh_name(const std::string &kind) const;
	// Stage 148: the level's items -- the goal, a detector, an object -- selected and edited like elements
	std::optional<std::string> item_path(const std::string &item) const; // its JSON pointer ("/objective/goal", "/detectors/2")
	std::vector<Handle> item_handles() const;
	std::string item_at(const V2 &screen) const;
	void drag_item(const V2 &screen, unsigned mods);
	std::vector<Row> item_rows() const;
	nlohmann::json item_op(const std::string &op, const nlohmann::json &cmd);
	void select_element(const std::string &name) {
		selected_ = name;
		item_.clear();
		group_.clear();
		if (step_place_ && step_place_->element != name) {
			step_place_.reset(); // (Stage 203: a placement is the selected element's)
		}
	}
	// Stage 175: the selection -- one element or item, or several (elements as "element:<name>", items as
	// their names), picked by a marquee or ctrl+click, moved and scaled by their box, copied and pasted
	struct Box {
		double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	};
	struct Scale { // p -> about + (sx, sy) (p - about) + by
		double ax = 0, ay = 0, sx = 1, sy = 1, bx = 0, by = 0;
	};
	std::vector<std::string> selection() const;
	void set_selection(std::vector<std::string> entries);
	bool exists(const std::string &entry) const;
	std::optional<Box> box_of(const std::vector<std::string> &entries) const; // their bounds (world)
	std::vector<std::string> in_rect(const V2 &a, const V2 &b) const;        // what lies wholly inside (world corners)
	void scale(nlohmann::json &doc, const std::vector<std::string> &entries, const Scale &t, double step) const; // step 0: no grid
	nlohmann::json selection_op(const std::string &op, const nlohmann::json &cmd);
	nlohmann::json copied(const std::vector<std::string> &entries) const;    // the clipboard's content
	std::vector<std::string> paste(nlohmann::json &doc, const nlohmann::json &clip, double dx, double dy); // the pasted entries
	void drag_group(const V2 &screen, unsigned mods);
	nlohmann::json kinds() const; // kind -> template (builtin and the level's)
	std::vector<std::string> palette() const; // the kinds in the palette's order (builtin by priority, then the level's)
	nlohmann::json op(const nlohmann::json &cmd); // throws std::runtime_error / LevelError

	// input helpers
	std::vector<Handle> handles() const;
	std::optional<Handle> handle_at(const V2 &screen) const;
	int element_at(const V2 &screen) const;
	nlohmann::json snap(const V2 &screen, unsigned mods, const Handle *moving, std::string *carrier = nullptr) const;
	std::string carrier_near(const V2 &screen) const; // Stage 149: the carrier within reach of the pointer ("": none)
	void place(const nlohmann::json &p, unsigned mods);
	void drag_to(const V2 &screen, unsigned mods);
	void commit_field();
	std::vector<Row> rows() const;
	std::vector<Row> laid_rows() const; // (rows() lays them out, then settles the scroll: Stage 218)
	std::vector<Row> marker_rows(const Field &owner) const; // Stage 179: an element's or a detector's marker (shape, colour)
	std::vector<Row> chip_rows(const Row &r) const;          // Stage 212: an enum's (or a few options') values as chips
	void click_row(const Row &r, unsigned mods);
	void frame_all();

	// drawing helpers
	void draw_canvas(std::vector<render::FrameNode> &out) const;
	void draw_panel(std::vector<render::FrameNode> &out) const;

	nlohmann::json doc_;
	std::vector<nlohmann::json> undo_, redo_;
	bool dirty_ = false, save_requested_ = false;
	// Stage 155: the levels the host offers (names under data/levels, no .json), the open one, the menu
	std::vector<std::string> levels_;
	std::string level_name_, open_request_, confirm_open_;
	bool level_menu_ = false;
	// Stage 165: a new level -- the copy being asked for (its new name), the name prompt, picking what to copy
	std::string open_as_, new_from_;
	bool new_prompt_ = false, copy_pick_ = false;
	void start_new_prompt(const std::string &from);
	// Stage 170: the level renamed -- the name its file has on disk until the save under the new name (none: no
	// rename waiting), the rename's prompt
	std::optional<std::string> rename_from_;
	bool rename_prompt_ = false;
	void start_rename_prompt();
	// Stage 167: the level list's search -- the levels ranked by a soft match of the query (matches first, their
	// matched characters), and the one picked (Up/Down, Enter)
	struct Ranked {
		std::string name;
		std::vector<int> marks; // empty: no match (or no query)
	};
	std::vector<Ranked> ranked_levels() const;
	size_t level_pick_ = 0;
	// Stage 217: completing a flag in the focused field -- the word at the caret, the level's flags ranked by it (the
	// level search's soft match), the one picked (Up/Down; Tab or a click puts it in), the list closed (Esc) until the
	// text changes
	struct Completion {
		std::string kind;   // flag (a flag written), expr (an expression read), door (a door's name), json (a key in JSON)
		std::string word;   // the word at the caret
		size_t from = 0, to = 0;
		struct Item {
			std::string flag, who;
			std::vector<int> marks;
			std::vector<std::string> elements, detectors;
			bool written = true;
		};
		std::vector<Item> items; // ranked: matches first (best first), then the rest; empty word: all, written ones first
	};
	std::optional<Completion> completion() const;
	std::string flag_field(const Field &f) const; // the field's kind of flag ("": none)
	void accept_completion(size_t i);
	// Stage 219: Ctrl+F -- what the level holds (elements, detectors, objects, carriers, the goal, the player), ranked by
	// its name, its kind or a flag it touches; the one picked (Up/Down), selected and shown on Enter or a click
	struct Found {
		std::string entry, name, what; // the selection's entry ("element:<name>", "detector:<name>", "goal", ...)
		std::vector<int> marks;
	};
	std::vector<Found> found() const;
	void show_found(const std::string &entry);
	std::string item_of_problem(const std::string &message) const; // a level problem's item (none: "")
	size_t find_pick_ = 0;
	std::optional<std::string> focus_draft_; // Stage 218: the default the field started from (applied unchanged: nothing written)
	size_t complete_pick_ = 0;
	bool complete_closed_ = false;
	mutable uint64_t flags_at_ = ~uint64_t(0);
	mutable std::vector<FlagUse> flags_; // (the level's flags at version flags_at_)
	std::string level_query() const; // the search's text (while it has the focus)
	uint64_t version_ = 0;
	Mode mode_ = Mode::Edit;
	Tool tool_ = Tool::Select;
	std::string kind_ = "wall"; // what the placement tools place
	bool grid_ = true;
	std::string selected_;      // an element's name, "" none
	std::string item_;          // Stage 148: the selected item: "goal", "detector:<name>", "object:<name>"; "" none
	std::vector<std::string> group_; // Stage 175: several selected (then selected_ and item_ are empty)
	std::optional<V2> marquee_;      // ... the marquee's first corner (screen) while it is dragged
	std::vector<std::string> marquee_keep_; // ... what stays selected with it (ctrl held)
	nlohmann::json group_from_;      // ... the document when the box's drag began
	Box box_from_;                   // ... the box then
	V2 press_;                       // ... the press (world)
	nlohmann::json clipboard_;       // ... what ctrl+C copied (kept across levels; null: nothing)
	std::vector<Error> errors_;
	std::vector<Drawn> drawn_;  // per element
	FlagMarkers markers_;       // Stage 179: the markers the flags give (the whole level's; none while it does not lower)
	Drawn edges_drawn_;         // Stage 169: the configured edges (none: they pass)
	std::vector<int> shade_of_;  // Stage 153: per element, 1 a dark shade, 2 a light one
	rc::Flags start_flags_;      // Stage 208: the level's starting flags (the elements drawn under them, as the game does)
	std::vector<std::optional<BuiltSurface>> object_built_; // ... and its objects' surfaces (none while it does not build)
	// Stage 203: an element's motion's steps or keys as the editor shows and edits them -- each part's two
	// points (a translation's from and to, a rotation's pivot and aim; handles "step:<k>:<part>:<0|1>"), and the
	// poses they lead to (an opening's after each step, the last its open pose; a motion's at each key), drawn faint
	struct Moves {
		struct Part {
			size_t index = 0;   // the step's or the key's
			bool rotate = false;
			PointPair points{};
		};
		bool cycle = false; // (Stage 204: the cycle plays it; else its flag, an opening)
		bool keys = false;  // ... by keys (else by steps)
		std::vector<Part> parts;
		std::vector<std::vector<rc::arc::SegGeom>> poses;
	};
	std::vector<Moves> moves_; // per element
	// ... a part being placed by two clicks (the element, its list's index: -1 a new step or key; its part)
	struct StepPlace {
		std::string element;
		int index = -1;
		bool rotate = false;
	};
	std::optional<StepPlace> step_place_;
	static void put_step(nlohmann::json &e, int index, bool rotate, const nlohmann::json &points);
	static void carry_moves(nlohmann::json &e, double dx, double dy); // (Stage 202: its pivots, Stage 203: its points too)
	static double phase_of(const nlohmann::json &key);
	std::string step_hint() const; // what the next click of a placement places
	rc::vis::Fill shade_fill_;    // ... the shadow under the starting flags (world units)
	// the view
	float width_ = 1920, height_ = 1080;
	V2 center_{960, 540};
	double zoom_ = 1.0;
	bool view_touched_ = false; // the user panned or zoomed: a resize keeps the view (else it frames the level again)
	mutable float panel_scroll_ = 0; // the panel scrolled down by (px) -- Stage 218: settled by rows()
	mutable std::string scrolled_for_, followed_; // (the selection it was scrolled for; the field it last showed)
	bool kind_pick_ = false;    // the next palette click sets the selected element's kind
	// the pointer
	V2 mouse_;
	unsigned buttons_ = 0;
	std::vector<nlohmann::json> clicks_; // a placement's points so far
	std::vector<std::string> click_carriers_; // ... the carrier each landed on ("" none)
	std::string carrier_of_click_;            // the carrier the click being placed snapped onto
	std::optional<Handle> drag_;          // the handle being dragged
	bool drag_changed_ = false;
	V2 drag_last_;                        // a body drag's last world point
	bool panning_ = false;
	bool space_ = false;
	V2 pan_last_;
	// the panel's text field
	std::optional<Field> focus_;
	std::string buffer_;
	size_t caret_ = 0; // Stage 191: where a field's text is edited (a byte of buffer_, between characters)
	std::string hint_;
};

} // namespace rcg
