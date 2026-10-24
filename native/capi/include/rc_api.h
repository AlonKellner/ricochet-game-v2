/* rc_api.h -- the Ricochet game's C API (Stage 118): the whole game behind a
 * plain C ABI, for any engine or host (docs/PORTING.md). The host hands the
 * game a level's JSON text, its input (semantic events, the cursor, the held
 * movement keys) and the frame time; it gets back what to show (the frame:
 * the player, the arrow, the camera; the draw list: every 2D draw call in
 * CanvasItem terms) and the session record (the replay golden's).
 *
 * Rules:
 * - Every struct passed in or out starts with its `size` (the caller sets it
 *   to sizeof): a newer library reads or fills only what the caller knows.
 * - Enums are int32 values, never renumbered.
 * - The library owns all memory. A view it returns (a draw list, a text) is
 *   borrowed: valid until the next call that takes the same game non-const.
 * - No exception crosses the boundary: a failure is a status, its message in
 *   rc_last_error() (per thread) and in the log.
 * - A game is used from one thread at a time; a level is immutable and may be
 *   shared by games on any threads.
 * - Coordinates: Y down, X right, 1 unit = 1 pixel; positions are float32.
 */
#ifndef RC_API_H
#define RC_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define RC_API __declspec(dllexport)
#else
#define RC_API __attribute__((visibility("default")))
#endif

#define RC_API_VERSION_MAJOR 1
#define RC_API_VERSION_MINOR 10

/* (major << 16) | minor. A host built against another major version must not
 * use the library. */
RC_API uint32_t rc_api_version(void);

typedef int32_t rc_status;
enum {
	RC_OK = 0,
	RC_E_ARGUMENT = 1, /* a null or malformed argument, a struct too small */
	RC_E_LEVEL = 2,    /* a level that does not follow its schema (rc-level/1 or rc-level/2) */
	RC_E_INTERNAL = 3  /* a failure inside the game (a bug: see the log) */
};

/* The last failure's message on this thread ("" after a success). */
RC_API const char *rc_last_error(void);

/* The log: the game's errors and warnings (default: stderr). */
enum { RC_LOG_INFO = 0, RC_LOG_WARNING = 1, RC_LOG_ERROR = 2 };
typedef void (*rc_log_fn)(void *user, int32_t level, const char *message);
RC_API void rc_set_log(rc_log_fn fn, void *user);

/* ------------------------------------------------------------------ levels */

typedef struct rc_level rc_level;

/* A level from its JSON text (rc-level/1, docs/LEVEL_FORMAT.md; the host
 * reads the file). */
RC_API rc_status rc_level_parse(const char *json, size_t length, rc_level **out);
RC_API void rc_level_release(rc_level *level);

/* ------------------------------------------------------------------ games */

typedef struct rc_game rc_game;

typedef struct rc_game_config {
	uint32_t size;
	int32_t infinity;   /* ignored since 1.7 (Stage 136: infinity is always on); kept for the struct's layout */
	int32_t visibility; /* the visibility region: -1 default (on), 0 off, 1 on */
	/* 1.2: 1 for a live host -- the visibility region and the preview's exact
	 * climbs are computed in the background, the frame never waits for them
	 * (the last region shows meanwhile, a provisional preview): see rc_frame's
	 * region_pending / preview_exact. 0 (the default, and an older host's
	 * config): everything in the frame, in order (replays: the session golden). */
	int32_t async;
	/* 1.3: 1 for a live host -- the draw list is animated (rcg::Presenter): the
	 * player drawn between physics ticks, the visibility region morphing from
	 * the shape shown to a new one; rc_frame's camera target follows the player
	 * drawn. The game's state and its record are unchanged. 0 (the default, and
	 * an older host's config): the game's state as it is (replays). */
	int32_t animate;
	/* 1.3 (async): the milliseconds a frame waits for the work it started (the
	 * region, the preview's exact climb): what finishes in time shows that frame,
	 * exact, and nothing is smoothed; what is late shows later (animated: morphed
	 * in). 0 (the default): never wait. */
	float budget_ms;
	/* 1.4 (animate): a late region is smoothed only when its computation took at
	 * least this many milliseconds (slower than 30 Hz by default); one computed
	 * faster is drawn as it arrives. 0 or less (the default, and an older host's
	 * config): 1000 / 30. */
	float late_ms;
} rc_game_config;

/* A game on a level (config may be null: the defaults). The game keeps its
 * own reference to the level. */
RC_API rc_status rc_game_create(const rc_level *level, const rc_game_config *config, rc_game **out);
RC_API void rc_game_destroy(rc_game *game);

/* Semantic input events (the host maps its keys and buttons onto them). While
 * the arrow flies every key event speeds it up and clicks are ignored. */
enum {
	RC_EVENT_PLAN_ADD = 1,         /* left click at the cursor: plan the side there */
	RC_EVENT_PLAN_REMOVE = 2,      /* right click: unplan the surface there (nothing there: clear) */
	RC_EVENT_FIRE = 3,
	RC_EVENT_UNDO = 4,
	RC_EVENT_RESET = 5,
	RC_EVENT_CLEAR_PLAN = 6,
	RC_EVENT_CYCLE_DISPLAY = 7,    /* the path display: merged, physical, planned */
	RC_EVENT_TOGGLE_GRAVITY = 8,
	RC_EVENT_TOGGLE_VISIBILITY = 9,
	RC_EVENT_ANY_KEY = 10          /* any other key (speeds a flight up) */
};
RC_API rc_status rc_game_event(rc_game *game, int32_t event);

/* The cursor, in world coordinates. */
RC_API rc_status rc_game_set_cursor(rc_game *game, float x, float y);

/* Movement keys: held this frame, and pressed this frame (just pressed; a
 * press is kept until the next physics tick runs). */
enum { RC_KEY_LEFT = 1, RC_KEY_RIGHT = 2, RC_KEY_UP = 4, RC_KEY_DOWN = 8 };

/* One frame of dt seconds: the arrow's flight, else the player's physics (a
 * fixed 1/60 s tick, accumulated: dt 0 runs none) and the derived state (the
 * preview, hover, overlays, visibility). Call it once per frame, and after
 * events whose effect should show before the next frame (dt 0). */
RC_API rc_status rc_game_advance(rc_game *game, double dt, uint32_t held, uint32_t pressed);

/* 1.10: the mouse wheel: moves the level's cycle (moving surfaces) by delta
 * notches and pauses it; a shot, or the level's idle seconds, runs it again. */
RC_API rc_status rc_game_scroll(rc_game *game, float delta);

/* Moves the player (tests, debugging, level editors). */
RC_API rc_status rc_game_teleport(rc_game *game, float x, float y);

typedef struct rc_frame {
	uint32_t size;
	float player_x, player_y;
	float facing;            /* the player's rotation, radians */
	int32_t flying;          /* an arrow in flight */
	float arrow_x, arrow_y;  /* the arrow's tip and direction (in flight) */
	float arrow_dx, arrow_dy;
	int32_t camera;          /* the level has a camera: it follows camera_x/y */
	float camera_x, camera_y;
	int32_t camera_smoothing;
	float camera_smoothing_speed;
	float bounds_x, bounds_y, bounds_w, bounds_h; /* the level's bounds (the camera's limits) */
	int32_t display_mode;    /* 0 merged, 1 physical, 2 planned; -1 no preview */
	float gravity_x, gravity_y;
	int32_t region_pending; /* 1.2 (async): a newer visibility region is being computed */
	int32_t preview_exact;  /* 1.2 (async): 0 while the path shown is provisional (its exact climb runs) */
	int32_t won;            /* 1.9: the level is won (its goal reached, or every target hit; latched until undo or reset) */
	float phase;            /* 1.10: the level's cycle (moving surfaces), in [0, 1) */
	int32_t cycle_auto;     /* 1.10: the cycle runs on its own (0: paused by a scroll) */
} rc_frame;
RC_API rc_status rc_game_frame(const rc_game *game, rc_frame *out);

/* ------------------------------------------------------------------ the draw list */

/* A draw command, in CanvasItem terms (Godot's draw_* calls; docs/PORTING.md
 * maps each onto plain primitives). Points are float32 (x, y) pairs, colours
 * float32 (r, g, b, a). A width < 0 is a thin (1 px, unscaled) line. */
enum {
	RC_DRAW_LINE = 0,            /* points [from, to], color, width */
	RC_DRAW_ARC = 1,             /* points [center], radius, start/end angle, arc_point_count, color, width */
	RC_DRAW_POLYGON = 2,         /* points, colors (one per point): filled */
	RC_DRAW_COLORED_POLYGON = 3, /* points, color: filled */
	RC_DRAW_CIRCLE = 4,          /* points [center], radius, color: filled */
	RC_DRAW_RECT = 5,            /* points [position, size], color: filled */
	RC_DRAW_POLYLINE = 6,        /* points, color, width */
	RC_DRAW_MULTILINE = 7,       /* points (segment pairs), color, width */
	RC_DRAW_DASHED_LINE = 8,     /* points [from, to], color, width, dash */
	RC_DRAW_TEXT = 9,            /* points [baseline position], text, alignment (0 left, 1 center), font_size, color */
	RC_DRAW_TRIANGLES = 10,      /* points, indices (triples), color */
	RC_DRAW_POLYGON2D = 11,      /* points (in the node's frame, rotated by rotation), color: filled */
	RC_DRAW_LABEL = 12           /* text: the HUD, a Label at (10, 10) (the default theme font, white) */
};

typedef struct rc_draw_cmd {
	int32_t kind;
	uint32_t point_first, point_count;
	uint32_t color_first, color_count;
	uint32_t index_first, index_count;
	uint32_t text_first, text_length;
	int32_t arc_point_count;
	int32_t alignment;
	int32_t font_size;
	double radius, start_angle, end_angle, width, dash, rotation;
} rc_draw_cmd;

/* A node: one thing drawn (a surface piece, the path, ...), its commands in
 * its own frame (translated by x, y), tinted by modulate, layered by z. Its
 * key (JSON text, e.g. ["surface",3,-1]) is stable while it lives. */
typedef struct rc_draw_node {
	uint32_t key_first, key_length; /* into text */
	int32_t z;
	float x, y;
	float modulate[4];
	uint32_t cmd_first, cmd_count;
} rc_draw_node;

typedef struct rc_draw_list {
	uint32_t size;
	const rc_draw_node *nodes; uint32_t node_count; /* in draw order: by z, then the scene's order */
	const rc_draw_cmd *cmds; uint32_t cmd_count;
	const float *points; uint32_t point_count;      /* point_count (x, y) pairs */
	const float *colors; uint32_t color_count;      /* color_count (r, g, b, a) */
	const int32_t *indices; uint32_t index_count;
	const char *text; uint32_t text_length;         /* UTF-8, not terminated */
	/* 1.1: per node (node_count entries each): a stable id, a content version
	 * (equal versions, equal commands), and (rc_game_draw_changes) whether the
	 * node is unchanged since the previous rc_game_draw_changes call -- an
	 * unchanged node comes without commands (cmd_count 0): keep what you drew */
	const uint64_t *node_ids;
	const uint64_t *node_versions;
	const uint8_t *node_unchanged;
} rc_draw_list;

/* What the game draws now. Borrowed (see the rules). */
RC_API rc_status rc_game_draw_list(rc_game *game, rc_draw_list *out);

/* 1.1: the same list for a host that keeps what it drew: a node whose id and
 * version are those of the previous rc_game_draw_changes call is marked
 * unchanged and carries no commands (its position, z and modulate are given,
 * they may change); a node missing from the list is gone. Borrowed. */
RC_API rc_status rc_game_draw_changes(rc_game *game, rc_draw_list *out);

/* The HUD's text (UTF-8, terminated). Borrowed. */
RC_API rc_status rc_game_hud(rc_game *game, const char **text, size_t *length);

/* ------------------------------------------------------------------ records and helpers */

/* The session record of the game's state (docs/research/session_digest.md):
 * the golden's canonical JSON line, extra (a JSON object, may be null) merged
 * in. Borrowed (terminated). Its "draw" is the draw list's sha, or with
 * RC_RECORD_DRAW_LIST the list itself. */
enum { RC_RECORD_DRAW_LIST = 1 };
RC_API rc_status rc_game_record(rc_game *game, const char *extra_json, size_t extra_length, uint32_t flags, const char **out,
		size_t *out_length);

/* 1.5: the debug dump (a host's F12): the game's state as JSON -- the solver's
 * inputs exactly (hex floats), the scene, the plan, the preview's walks, the
 * region -- or with RC_DEBUG_SUMMARY a few lines of text (the inputs, the
 * preview, the cursor as the preview and the region classify it, the region).
 * A tool for bug reports; its contents may change. Borrowed (terminated). */
enum { RC_DEBUG_SUMMARY = 1 };
RC_API rc_status rc_game_debug_dump(rc_game *game, uint32_t flags, const char **out, size_t *out_length);

/* 1.6 (tools: the violation viewer): the plan set as it is, from a JSON array of
 * entries {"surface_id", "side": "LEFT" | "RIGHT" (or 0 | 1), "click": [x, y]?,
 * "span": {"start", "end", "via"}?} -- a piece by its span, as the sweeps write it. */
RC_API rc_status rc_game_set_plan(rc_game *game, const char *json, size_t length);

/* The record encoding's sha256 of a JSON value (lowercase hex, terminated). */
RC_API rc_status rc_digest_sha(const char *json, size_t length, char out[65]);

/* An arc as the polyline Godot's draw_arc strokes: arc_point_count points
 * from start to end angle (float32 arithmetic). Writes min(capacity,
 * arc_point_count) points to out_xy, the count to written. */
RC_API rc_status rc_tessellate_arc(float cx, float cy, double radius, double start_angle, double end_angle, int32_t arc_point_count,
		float *out_xy, uint32_t capacity, uint32_t *written);

/* ------------------------------------------------------------------ the level editor (1.8) */

/* The level editor (docs/EDITOR.md, docs/PORTING.md "Hosting the editor"): an
 * rc-level/2 document edited natively. The host forwards the pointer, keys
 * and text in window pixels and paints the editor's draw list at identity
 * (screen space: the editor owns its pan and zoom; its inspector panel is part
 * of the list). It keeps no editor state. It writes the document when a save is
 * requested, and plays it with an ordinary game (rc_level_parse on the
 * document's text) while the editor is in play mode. Every UI action is also a
 * JSON command (rc_editor_command): what scripts and tests drive. */
typedef struct rc_editor rc_editor;

/* An editor on a level's JSON text (rc-level/2; rc-level/1 is migrated), or a
 * new level (json NULL). */
RC_API rc_status rc_editor_create(const char *json, size_t length, rc_editor **out);
RC_API void rc_editor_destroy(rc_editor *editor);

/* One command, a JSON object {"op": ...} (docs/EDITOR.md); the result, a JSON
 * object {"ok": true, ...} or {"ok": false, "error": ...} (a refused command is
 * not a failure status). Borrowed (terminated). */
RC_API rc_status rc_editor_command(rc_editor *editor, const char *json, size_t length, const char **result, size_t *result_length);

/* The document in the canonical spelling (what a save writes). Borrowed (terminated). */
RC_API rc_status rc_editor_document(rc_editor *editor, const char **text, size_t *length);

/* Input, in window pixels. A press or a release names its button; a move
 * names the buttons held. Keys: letters and digits as their ASCII upper case,
 * space 32, RC_EDKEY_* for the rest; text: what a key types (UTF-8). */
enum { RC_PTR_MOVE = 0, RC_PTR_DOWN = 1, RC_PTR_UP = 2 };
enum { RC_BTN_LEFT = 1, RC_BTN_RIGHT = 2, RC_BTN_MIDDLE = 4 };
enum { RC_MOD_SHIFT = 1, RC_MOD_CTRL = 2, RC_MOD_ALT = 4 };
enum {
	RC_EDKEY_SPACE = 32,
	RC_EDKEY_ESCAPE = 256,
	RC_EDKEY_ENTER = 257,
	RC_EDKEY_TAB = 258,
	RC_EDKEY_BACKSPACE = 259,
	RC_EDKEY_DELETE = 260,
	RC_EDKEY_LEFT = 261,
	RC_EDKEY_RIGHT = 262,
	RC_EDKEY_UP = 263,
	RC_EDKEY_DOWN = 264,
	RC_EDKEY_F1 = 290 /* F1..F12: RC_EDKEY_F1 + 0..11 */
};
RC_API rc_status rc_editor_pointer(rc_editor *editor, int32_t kind, float x, float y, uint32_t buttons, uint32_t mods);
RC_API rc_status rc_editor_key(rc_editor *editor, int32_t key, uint32_t mods, int32_t down);
RC_API rc_status rc_editor_text(rc_editor *editor, const char *utf8, size_t length);
RC_API rc_status rc_editor_scroll(rc_editor *editor, float dx, float dy, uint32_t mods); /* dy > 0: the wheel down (zoom out) */
RC_API rc_status rc_editor_resize(rc_editor *editor, float width, float height);

/* What the editor draws, in window pixels (nodes keyed ["editor", layer,
 * index]); as rc_game_draw_list / rc_game_draw_changes. Empty in play mode.
 * Borrowed. */
RC_API rc_status rc_editor_draw_list(rc_editor *editor, rc_draw_list *out);
RC_API rc_status rc_editor_draw_changes(rc_editor *editor, rc_draw_list *out);

enum { RC_EDITOR_EDIT = 0, RC_EDITOR_PLAY = 1 };
typedef struct rc_editor_status {
	uint32_t size;
	int32_t dirty;          /* the document changed since it was loaded or saved */
	int32_t errors;         /* the problems the document has (a play-test needs none) */
	int32_t mode;           /* RC_EDITOR_EDIT or RC_EDITOR_PLAY (the host plays the document) */
	int32_t tool;           /* 0 select, 1 line, 2 arc, 3 circle, 4 the player's start */
	int32_t save_requested; /* write rc_editor_document, then the command {"op": "saved"} */
	uint64_t version;       /* bumped by every change of the document */
	float play_x, play_y;   /* where a play-test starts (the player's start) */
} rc_editor_status;
RC_API rc_status rc_editor_get_status(rc_editor *editor, rc_editor_status *out);

#ifdef __cplusplus
}
#endif

#endif /* RC_API_H */
