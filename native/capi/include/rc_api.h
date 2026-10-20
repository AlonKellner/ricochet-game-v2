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
#define RC_API_VERSION_MINOR 0

/* (major << 16) | minor. A host built against another major version must not
 * use the library. */
RC_API uint32_t rc_api_version(void);

typedef int32_t rc_status;
enum {
	RC_OK = 0,
	RC_E_ARGUMENT = 1, /* a null or malformed argument, a struct too small */
	RC_E_LEVEL = 2,    /* a level that does not follow rc-level/1 */
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
	int32_t infinity;   /* the infinity switch: -1 the game's default (on), 0 off, 1 on */
	int32_t visibility; /* the visibility region: -1 default (on), 0 off, 1 on */
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
} rc_draw_list;

/* What the game draws now. Borrowed (see the rules). */
RC_API rc_status rc_game_draw_list(rc_game *game, rc_draw_list *out);

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

/* The record encoding's sha256 of a JSON value (lowercase hex, terminated). */
RC_API rc_status rc_digest_sha(const char *json, size_t length, char out[65]);

/* An arc as the polyline Godot's draw_arc strokes: arc_point_count points
 * from start to end angle (float32 arithmetic). Writes min(capacity,
 * arc_point_count) points to out_xy, the count to written. */
RC_API rc_status rc_tessellate_arc(float cx, float cy, double radius, double start_angle, double end_angle, int32_t arc_point_count,
		float *out_xy, uint32_t capacity, uint32_t *written);

#ifdef __cplusplus
}
#endif

#endif /* RC_API_H */
