/* Stage 118: a C11 host of the C API (rc_api.h): it compiles as plain C and
 * plays a level -- a plan, a shot flown frame by frame, the draw list, the
 * record -- and checks the error paths. Exit status 0: every check held. */
#include "rc_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			failures++;                                                   \
		}                                                                 \
	} while (0)

static char *read_file(const char *path, size_t *length) {
	FILE *f = fopen(path, "rb");
	char *text;
	long n;
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = (char *)malloc((size_t)n + 1);
	if (text != NULL && fread(text, 1, (size_t)n, f) == (size_t)n) {
		text[n] = '\0';
		*length = (size_t)n;
	}
	fclose(f);
	return text;
}

static int logged = 0;
static void on_log(void *user, int32_t level, const char *message) {
	(void)message;
	(void)level;
	*(int *)user += 1;
}

int main(void) {
	size_t length = 0;
	char *json = read_file(RC_REPO_ROOT "/data/levels/main.json", &length);
	rc_level *level = NULL;
	rc_game *game = NULL;
	rc_game_config config;
	rc_frame frame;
	rc_draw_list list;
	const char *text = NULL;
	size_t text_length = 0;
	uint32_t i;
	int frames = 0;
	char sha[65];
	float arc[2 * 16];
	uint32_t written = 0;

	CHECK(rc_api_version() >> 16 == RC_API_VERSION_MAJOR);
	rc_set_log(on_log, &logged);
	CHECK(json != NULL);
	if (json == NULL) {
		return 1;
	}

	/* the error paths: a status and a message, never a crash */
	CHECK(rc_level_parse(NULL, 0, &level) == RC_E_ARGUMENT);
	CHECK(rc_level_parse("{\"schema\": \"nope\"}", 18, &level) == RC_E_LEVEL);
	CHECK(level == NULL);
	CHECK(strlen(rc_last_error()) > 0);
	CHECK(rc_game_create(NULL, NULL, &game) == RC_E_ARGUMENT);
	CHECK(rc_game_event(NULL, RC_EVENT_FIRE) == RC_E_ARGUMENT);

	CHECK(rc_level_parse(json, length, &level) == RC_OK);
	CHECK(strcmp(rc_last_error(), "") == 0);
	config.size = sizeof config;
	config.infinity = -1;
	config.visibility = 1;
	config.async = 0;
	config.animate = 0;
	config.budget_ms = 0.0f;
	CHECK(rc_game_create(level, &config, &game) == RC_OK);
	rc_level_release(level); /* the game keeps its own reference */
	CHECK(rc_game_event(game, 99) == RC_E_ARGUMENT);
	CHECK(rc_game_advance(game, -1.0, 0, 0) == RC_E_ARGUMENT);

	/* aim, plan the surface under the cursor, fire, fly */
	CHECK(rc_game_teleport(game, 760.0f, 600.0f) == RC_OK);
	CHECK(rc_game_set_cursor(game, 1180.0f, 520.0f) == RC_OK);
	CHECK(rc_game_advance(game, 0.0, 0, 0) == RC_OK);
	frame.size = sizeof frame;
	CHECK(rc_game_frame(game, &frame) == RC_OK);
	CHECK(frame.player_x == 760.0f && frame.player_y == 600.0f);
	CHECK(!frame.flying);
	CHECK(frame.bounds_w > 0.0f && frame.display_mode == 0);
	CHECK(rc_game_event(game, RC_EVENT_FIRE) == RC_OK);
	CHECK(rc_game_frame(game, &frame) == RC_OK);
	CHECK(frame.flying);
	while (frame.flying && frames < 100000) {
		CHECK(rc_game_advance(game, 1.0 / 60.0, 0, 0) == RC_OK);
		if (++frames == 20) {
			CHECK(rc_game_event(game, RC_EVENT_ANY_KEY) == RC_OK);
		}
		CHECK(rc_game_frame(game, &frame) == RC_OK);
	}
	CHECK(frames > 0 && !frame.flying);

	/* the draw list: nodes over commands over the pools */
	list.size = sizeof list;
	CHECK(rc_game_draw_list(game, &list) == RC_OK);
	CHECK(list.node_count > 0 && list.cmd_count > 0 && list.point_count > 0);
	for (i = 0; i < list.node_count; i++) {
		const rc_draw_node *n = &list.nodes[i];
		CHECK(n->key_first + n->key_length <= list.text_length);
		CHECK(n->cmd_first + n->cmd_count <= list.cmd_count);
		CHECK(i == 0 || list.nodes[i - 1].z <= n->z);
	}
	for (i = 0; i < list.cmd_count; i++) {
		const rc_draw_cmd *c = &list.cmds[i];
		CHECK(c->kind >= RC_DRAW_LINE && c->kind <= RC_DRAW_LABEL);
		CHECK(c->point_first + c->point_count <= list.point_count);
		CHECK(c->color_first + c->color_count <= list.color_count);
		CHECK(c->index_first + c->index_count <= list.index_count);
	}

	/* 1.1: ids and versions; a second rc_game_draw_changes with nothing changed sends no commands */
	CHECK(list.node_ids != NULL && list.node_versions != NULL && list.node_unchanged != NULL);
	CHECK(rc_game_draw_changes(game, &list) == RC_OK);
	CHECK(list.cmd_count > 0);
	CHECK(rc_game_draw_changes(game, &list) == RC_OK);
	CHECK(list.cmd_count == 0 && list.node_count > 0);
	for (i = 0; i < list.node_count; i++) {
		CHECK(list.node_unchanged[i] == 1);
	}

	/* a smaller struct (an older host) is filled as far as it reaches */
	{
		rc_frame small;
		memset(&small, 0x7f, sizeof small);
		small.size = (uint32_t)offsetof(rc_frame, facing);
		CHECK(rc_game_frame(game, &small) == RC_OK);
		CHECK(small.player_x == frame.player_x);
		CHECK(small.size == (uint32_t)offsetof(rc_frame, facing));
	}

	CHECK(rc_game_hud(game, &text, &text_length) == RC_OK);
	CHECK(text != NULL && strlen(text) == text_length);
	CHECK(rc_game_record(game, "{\"i\": 0, \"op\": \"start\"}", 23, 0, &text, &text_length) == RC_OK);
	CHECK(text_length > 0 && text[0] == '{' && strstr(text, "\"draw\":\"") != NULL);
	CHECK(rc_game_record(game, "[1]", 3, 0, &text, &text_length) == RC_E_ARGUMENT);
	/* 1.5: the debug dump */
	CHECK(rc_game_debug_dump(game, 0, &text, &text_length) == RC_OK);
	CHECK(text_length > 0 && text[0] == '{' && strlen(text) == text_length && strstr(text, "\"cursor\"") != NULL);
	CHECK(rc_game_debug_dump(game, RC_DEBUG_SUMMARY, &text, &text_length) == RC_OK);
	CHECK(text_length > 0 && strlen(text) == text_length && strstr(text, "cursor: preview") != NULL);
	CHECK(rc_game_debug_dump(NULL, 0, &text, &text_length) == RC_E_ARGUMENT);
	/* 1.6: the plan set as it is */
	CHECK(rc_game_set_plan(game, "[]", 2) == RC_OK);
	CHECK(rc_game_set_plan(game, "{}", 2) == RC_E_ARGUMENT);
	CHECK(rc_digest_sha("[1.0,2]", 7, sha) == RC_OK);
	CHECK(strlen(sha) == 64);
	CHECK(rc_tessellate_arc(0.0f, 0.0f, 10.0, 0.0, 3.14159265358979, 16, arc, 16, &written) == RC_OK);
	CHECK(written == 16 && fabsf(arc[0] - 10.0f) < 1e-5f && fabsf(arc[30] + 10.0f) < 1e-4f);

	rc_game_destroy(game);

	/* 1.8: the level editor -- a command, a line drawn with the pointer, the
	 * draw list, the status, the document played as a level */
	{
		rc_editor *ed = NULL;
		rc_editor_status st;
		rc_draw_list el;
		rc_level *played = NULL;
		const char *res = NULL;
		size_t res_length = 0;
		const char *cmd = "{\"op\": \"add\", \"element\": {\"kind\": \"mirror\", \"start\": [700, 300], \"end\": [700, 700]}}";
		CHECK(rc_editor_create(json, length, &ed) == RC_OK && ed != NULL);
		CHECK(rc_editor_resize(ed, 1600.0f, 900.0f) == RC_OK);
		CHECK(rc_editor_command(ed, cmd, strlen(cmd), &res, &res_length) == RC_OK);
		CHECK(strstr(res, "\"ok\":true") != NULL && strstr(res, "\"name\":\"mirror_") != NULL);
		CHECK(rc_editor_command(ed, "{\"op\": 1}", 10, &res, &res_length) == RC_OK && strstr(res, "\"ok\":false") != NULL);
		CHECK(rc_editor_command(ed, "{", 1, &res, &res_length) == RC_OK && strstr(res, "not JSON") != NULL);
		CHECK(rc_editor_key(ed, 'L', 0, 1) == RC_OK);
		CHECK(rc_editor_pointer(ed, RC_PTR_DOWN, 300.0f, 300.0f, RC_BTN_LEFT, 0) == RC_OK);
		CHECK(rc_editor_pointer(ed, RC_PTR_UP, 300.0f, 300.0f, RC_BTN_LEFT, 0) == RC_OK);
		CHECK(rc_editor_pointer(ed, RC_PTR_DOWN, 400.0f, 300.0f, RC_BTN_LEFT, 0) == RC_OK);
		CHECK(rc_editor_pointer(ed, RC_PTR_UP, 400.0f, 300.0f, RC_BTN_LEFT, 0) == RC_OK);
		CHECK(rc_editor_text(ed, "x", 1) == RC_OK);
		CHECK(rc_editor_scroll(ed, 0.0f, 1.0f, 0) == RC_OK);
		memset(&el, 0, sizeof el);
		el.size = sizeof el;
		CHECK(rc_editor_draw_list(ed, &el) == RC_OK && el.node_count > 3 && el.text_length > 0);
		CHECK(rc_editor_draw_changes(ed, &el) == RC_OK);
		CHECK(rc_editor_draw_changes(ed, &el) == RC_OK && el.node_count > 3 && el.node_unchanged[0] == 1);
		memset(&st, 0, sizeof st);
		st.size = sizeof st;
		CHECK(rc_editor_get_status(ed, &st) == RC_OK && st.dirty == 1 && st.errors == 0 && st.mode == RC_EDITOR_EDIT && st.tool == 1);
		CHECK(rc_editor_document(ed, &res, &res_length) == RC_OK && strstr(res, "\"rc-level/2\"") != NULL);
		CHECK(rc_level_parse(res, res_length, &played) == RC_OK); /* the host plays the document */
		rc_level_release(played);
		CHECK(rc_editor_key(ed, RC_EDKEY_F1 + 4, 0, 1) == RC_OK);
		CHECK(rc_editor_get_status(ed, &st) == RC_OK && st.mode == RC_EDITOR_PLAY);
		CHECK(rc_editor_create("[", 1, &ed) == RC_E_LEVEL);
		CHECK(rc_editor_pointer(NULL, RC_PTR_MOVE, 0.0f, 0.0f, 0, 0) == RC_E_ARGUMENT);
		rc_editor_destroy(ed);
	}
	rc_set_log(NULL, NULL);
	free(json);
	if (failures == 0) {
		printf("capi_c_smoke: ok (%d flight frames)\n", frames);
	}
	return failures == 0 ? 0 : 1;
}
