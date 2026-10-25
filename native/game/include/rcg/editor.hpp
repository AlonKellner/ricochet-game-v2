// The level editor, natively (docs/EDITOR.md; the editor program, docs/TDD_48):
// an rc-level/2 document (rcg/level2.hpp) edited through commands -- JSON, the
// one path every UI action takes, so a script, a test and a click do the same
// -- and through pointer, key and text input in screen pixels, drawn as one
// draw list in screen space (rcg/render.hpp: the editor owns its pan and zoom,
// a host paints at identity). The inspector panel is part of that list. A host
// is a painter and an event forwarder: it keeps no editor state, does the file
// I/O when a save is requested, and plays the document with an ordinary game.
#pragma once

#include "rc/visibility.hpp"
#include "rcg/build.hpp"
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
		enum Kind { Header, Text, Value, Error, Buttons, Chips, Nav, Level, Tools } kind = Text; // Tools (Stage 161): the tools, one row of them // Chips: the palette's kinds (options), one row of them
		std::string label, value, hint;
		std::optional<Field> field;
		std::string choice; // a value that cycles ("enum", "bool", "kind", "tool", "place", "options", "shape") or "" (typed)
		int error_element = -1;
		float y = 0;
		std::vector<std::string> options; // "options": the values it cycles through (strings)
		std::vector<int> marks = {};      // Stage 167: a level row's characters the search matched
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
	void select_element(const std::string &name) { selected_ = name; item_.clear(); }
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
	// Stage 167: the level list's search -- the levels ranked by a soft match of the query (matches first, their
	// matched characters), and the one picked (Up/Down, Enter)
	struct Ranked {
		std::string name;
		std::vector<int> marks; // empty: no match (or no query)
	};
	std::vector<Ranked> ranked_levels() const;
	size_t level_pick_ = 0;
	std::string level_query() const; // the search's text (while it has the focus)
	uint64_t version_ = 0;
	Mode mode_ = Mode::Edit;
	Tool tool_ = Tool::Select;
	std::string kind_ = "wall"; // what the placement tools place
	bool grid_ = true;
	std::string selected_;      // an element's name, "" none
	std::string item_;          // Stage 148: the selected item: "goal", "detector:<name>", "object:<name>"; "" none
	std::vector<Error> errors_;
	std::vector<Drawn> drawn_;  // per element
	std::vector<int> shade_of_;  // Stage 153: per element, 1 a dark shade, 2 a light one
	rc::vis::Fill shade_fill_;    // ... the shadow under the starting flags (world units)
	// the view
	float width_ = 1920, height_ = 1080;
	V2 center_{960, 540};
	double zoom_ = 1.0;
	bool view_touched_ = false; // the user panned or zoomed: a resize keeps the view (else it frames the level again)
	float panel_scroll_ = 0;    // the panel scrolled down by (px)
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
	std::string hint_;
};

} // namespace rcg
