"""A Python host of the Ricochet game through its C API (native/capi/include/rc_api.h),
with ctypes alone -- the way an engine binds a C library (Unity's P/Invoke,
Unreal's or a custom engine's FFI). docs/PORTING.md walks through it.

    game = Game(Level(open("data/levels/main.json").read()))
    game.set_cursor(1180, 520); game.advance(0)
    game.event(EVENT_FIRE)
    while game.frame().flying: game.advance(1 / 60)
    for node in game.draw_list(): ...

The library: $RC_CAPI_LIB, else native/build-core/libricochet_capi.{dylib,so,dll}.
"""
import ctypes
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

API_MAJOR = 1

OK, E_ARGUMENT, E_LEVEL, E_INTERNAL = 0, 1, 2, 3

(EVENT_PLAN_ADD, EVENT_PLAN_REMOVE, EVENT_FIRE, EVENT_UNDO, EVENT_RESET, EVENT_CLEAR_PLAN, EVENT_CYCLE_DISPLAY,
 EVENT_TOGGLE_GRAVITY, EVENT_TOGGLE_VISIBILITY, EVENT_ANY_KEY) = range(1, 11)
KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN = 1, 2, 4, 8
RECORD_DRAW_LIST = 1

DRAW_KINDS = ["line", "arc", "polygon", "colored_polygon", "circle", "rect", "polyline", "multiline", "dashed_line",
              "text", "triangles", "polygon2d", "label"]


class GameConfig(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("infinity", ctypes.c_int32), ("visibility", ctypes.c_int32),
                ("async_", ctypes.c_int32), ("animate", ctypes.c_int32), ("budget_ms", ctypes.c_float),  # 1.2, 1.3
                ("late_ms", ctypes.c_float)]  # 1.4


class Frame(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32),
                ("player_x", ctypes.c_float), ("player_y", ctypes.c_float), ("facing", ctypes.c_float),
                ("flying", ctypes.c_int32),
                ("arrow_x", ctypes.c_float), ("arrow_y", ctypes.c_float),
                ("arrow_dx", ctypes.c_float), ("arrow_dy", ctypes.c_float),
                ("camera", ctypes.c_int32), ("camera_x", ctypes.c_float), ("camera_y", ctypes.c_float),
                ("camera_smoothing", ctypes.c_int32), ("camera_smoothing_speed", ctypes.c_float),
                ("bounds_x", ctypes.c_float), ("bounds_y", ctypes.c_float),
                ("bounds_w", ctypes.c_float), ("bounds_h", ctypes.c_float),
                ("display_mode", ctypes.c_int32),
                ("gravity_x", ctypes.c_float), ("gravity_y", ctypes.c_float),
                ("region_pending", ctypes.c_int32), ("preview_exact", ctypes.c_int32)]  # 1.2


class DrawCmd(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int32),
                ("point_first", ctypes.c_uint32), ("point_count", ctypes.c_uint32),
                ("color_first", ctypes.c_uint32), ("color_count", ctypes.c_uint32),
                ("index_first", ctypes.c_uint32), ("index_count", ctypes.c_uint32),
                ("text_first", ctypes.c_uint32), ("text_length", ctypes.c_uint32),
                ("arc_point_count", ctypes.c_int32), ("alignment", ctypes.c_int32), ("font_size", ctypes.c_int32),
                ("radius", ctypes.c_double), ("start_angle", ctypes.c_double), ("end_angle", ctypes.c_double),
                ("width", ctypes.c_double), ("dash", ctypes.c_double), ("rotation", ctypes.c_double)]


class DrawNode(ctypes.Structure):
    _fields_ = [("key_first", ctypes.c_uint32), ("key_length", ctypes.c_uint32),
                ("z", ctypes.c_int32), ("x", ctypes.c_float), ("y", ctypes.c_float),
                ("modulate", ctypes.c_float * 4),
                ("cmd_first", ctypes.c_uint32), ("cmd_count", ctypes.c_uint32)]


class DrawList(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32),
                ("nodes", ctypes.POINTER(DrawNode)), ("node_count", ctypes.c_uint32),
                ("cmds", ctypes.POINTER(DrawCmd)), ("cmd_count", ctypes.c_uint32),
                ("points", ctypes.POINTER(ctypes.c_float)), ("point_count", ctypes.c_uint32),
                ("colors", ctypes.POINTER(ctypes.c_float)), ("color_count", ctypes.c_uint32),
                ("indices", ctypes.POINTER(ctypes.c_int32)), ("index_count", ctypes.c_uint32),
                ("text", ctypes.POINTER(ctypes.c_char)), ("text_length", ctypes.c_uint32),
                # 1.1
                ("node_ids", ctypes.POINTER(ctypes.c_uint64)), ("node_versions", ctypes.POINTER(ctypes.c_uint64)),
                ("node_unchanged", ctypes.POINTER(ctypes.c_uint8))]


LOG_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int32, ctypes.c_char_p)


def library_path():
    if os.environ.get("RC_CAPI_LIB"):
        return os.environ["RC_CAPI_LIB"]
    name = {"darwin": "libricochet_capi.dylib", "win32": "ricochet_capi.dll"}.get(sys.platform, "libricochet_capi.so")
    return str(ROOT / "native" / "build-core" / name)


def _load():
    lib = ctypes.CDLL(library_path())
    P = ctypes.POINTER
    sigs = {
        "rc_api_version": (ctypes.c_uint32, []),
        "rc_last_error": (ctypes.c_char_p, []),
        "rc_set_log": (None, [LOG_FN, ctypes.c_void_p]),
        "rc_level_parse": (ctypes.c_int32, [ctypes.c_char_p, ctypes.c_size_t, P(ctypes.c_void_p)]),
        "rc_level_release": (None, [ctypes.c_void_p]),
        "rc_game_create": (ctypes.c_int32, [ctypes.c_void_p, P(GameConfig), P(ctypes.c_void_p)]),
        "rc_game_destroy": (None, [ctypes.c_void_p]),
        "rc_game_event": (ctypes.c_int32, [ctypes.c_void_p, ctypes.c_int32]),
        "rc_game_set_cursor": (ctypes.c_int32, [ctypes.c_void_p, ctypes.c_float, ctypes.c_float]),
        "rc_game_advance": (ctypes.c_int32, [ctypes.c_void_p, ctypes.c_double, ctypes.c_uint32, ctypes.c_uint32]),
        "rc_game_teleport": (ctypes.c_int32, [ctypes.c_void_p, ctypes.c_float, ctypes.c_float]),
        "rc_game_frame": (ctypes.c_int32, [ctypes.c_void_p, P(Frame)]),
        "rc_game_draw_list": (ctypes.c_int32, [ctypes.c_void_p, P(DrawList)]),
        "rc_game_draw_changes": (ctypes.c_int32, [ctypes.c_void_p, P(DrawList)]),
        "rc_game_hud": (ctypes.c_int32, [ctypes.c_void_p, P(ctypes.c_char_p), P(ctypes.c_size_t)]),
        "rc_game_record": (ctypes.c_int32, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint32,
                                            P(ctypes.c_void_p), P(ctypes.c_size_t)]),
        "rc_digest_sha": (ctypes.c_int32, [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]),
        "rc_tessellate_arc": (ctypes.c_int32, [ctypes.c_float, ctypes.c_float, ctypes.c_double, ctypes.c_double,
                                               ctypes.c_double, ctypes.c_int32, P(ctypes.c_float), ctypes.c_uint32,
                                               P(ctypes.c_uint32)]),
    }
    for name, (res, args) in sigs.items():
        f = getattr(lib, name)
        f.restype = res
        f.argtypes = args
    if lib.rc_api_version() >> 16 != API_MAJOR:
        raise RuntimeError("rc_api major version %d, this host speaks %d" % (lib.rc_api_version() >> 16, API_MAJOR))
    return lib


lib = _load()


class RcError(Exception):
    def __init__(self, status, message):
        super().__init__("%d: %s" % (status, message))
        self.status = status


def check(status):
    if status != OK:
        raise RcError(status, lib.rc_last_error().decode())


def set_log(fn):
    """fn(level, message) receives the game's log; None restores stderr. The
    callback is kept alive here (ctypes would free it)."""
    global _log_cb
    _log_cb = LOG_FN(lambda user, level, msg: fn(level, msg.decode())) if fn else ctypes.cast(None, LOG_FN)
    lib.rc_set_log(_log_cb, None)


_log_cb = None


def digest_sha(value):
    text = json.dumps(value).encode()
    out = ctypes.create_string_buffer(65)
    check(lib.rc_digest_sha(text, len(text), out))
    return out.value.decode()


def tessellate_arc(cx, cy, radius, start, end, count):
    buf = (ctypes.c_float * (2 * count))()
    n = ctypes.c_uint32()
    check(lib.rc_tessellate_arc(cx, cy, radius, start, end, count, buf, count, ctypes.byref(n)))
    return [(buf[2 * i], buf[2 * i + 1]) for i in range(n.value)]


class Level:
    def __init__(self, json_text):
        data = json_text.encode()
        self._h = ctypes.c_void_p()
        check(lib.rc_level_parse(data, len(data), ctypes.byref(self._h)))

    def __del__(self):
        if getattr(self, "_h", None):
            lib.rc_level_release(self._h)
            self._h = None


class Game:
    def __init__(self, level, infinity=-1, visibility=-1, animate=0, asynchronous=0, budget_ms=0.0, late_ms=0.0):
        cfg = GameConfig(ctypes.sizeof(GameConfig), infinity, visibility, asynchronous, animate, budget_ms, late_ms)
        self._h = ctypes.c_void_p()
        check(lib.rc_game_create(level._h, ctypes.byref(cfg), ctypes.byref(self._h)))

    def __del__(self):
        if getattr(self, "_h", None):
            lib.rc_game_destroy(self._h)
            self._h = None

    def event(self, kind):
        check(lib.rc_game_event(self._h, kind))

    def set_cursor(self, x, y):
        check(lib.rc_game_set_cursor(self._h, x, y))

    def teleport(self, x, y):
        check(lib.rc_game_teleport(self._h, x, y))

    def advance(self, dt, held=0, pressed=0):
        check(lib.rc_game_advance(self._h, dt, held, pressed))

    def frame(self):
        f = Frame()
        f.size = ctypes.sizeof(Frame)
        check(lib.rc_game_frame(self._h, ctypes.byref(f)))
        return f

    def hud(self):
        text = ctypes.c_char_p()
        check(lib.rc_game_hud(self._h, ctypes.byref(text), None))
        return text.value.decode()

    def record(self, extra=None, draw_list=False):
        text = json.dumps(extra).encode() if extra is not None else None
        out = ctypes.c_void_p()
        n = ctypes.c_size_t()
        check(lib.rc_game_record(self._h, text, len(text) if text else 0, RECORD_DRAW_LIST if draw_list else 0,
                                 ctypes.byref(out), ctypes.byref(n)))
        return ctypes.string_at(out, n.value).decode()

    def draw_changes(self):
        """[(id, version, unchanged, key, z, (x, y), modulate, [command dict])] (rc_game_draw_changes)."""
        return self.draw_list(changes=True)

    def draw_list(self, changes=False):
        """[(key, z, (x, y), modulate, [command dict])]: a copy of the borrowed view."""
        dl = DrawList()
        dl.size = ctypes.sizeof(DrawList)
        check((lib.rc_game_draw_changes if changes else lib.rc_game_draw_list)(self._h, ctypes.byref(dl)))
        text = ctypes.string_at(dl.text, dl.text_length) if dl.text_length else b""
        pts = [(dl.points[2 * i], dl.points[2 * i + 1]) for i in range(dl.point_count)]
        cols = [tuple(dl.colors[4 * i + k] for k in range(4)) for i in range(dl.color_count)]
        idx = [dl.indices[i] for i in range(dl.index_count)]
        out = []
        for i in range(dl.node_count):
            n = dl.nodes[i]
            cmds = []
            for j in range(n.cmd_first, n.cmd_first + n.cmd_count):
                c = dl.cmds[j]
                cmds.append({
                    "kind": DRAW_KINDS[c.kind],
                    "points": pts[c.point_first:c.point_first + c.point_count],
                    "colors": cols[c.color_first:c.color_first + c.color_count],
                    "indices": idx[c.index_first:c.index_first + c.index_count],
                    "text": text[c.text_first:c.text_first + c.text_length].decode(),
                    "arc_point_count": c.arc_point_count, "alignment": c.alignment, "font_size": c.font_size,
                    "radius": c.radius, "start": c.start_angle, "end": c.end_angle,
                    "width": c.width, "dash": c.dash, "rotation": c.rotation,
                })
            key = json.loads(text[n.key_first:n.key_first + n.key_length])
            if changes:
                out.append((dl.node_ids[i], dl.node_versions[i], bool(dl.node_unchanged[i]), key, n.z, (n.x, n.y), tuple(n.modulate), cmds))
            else:
                out.append((key, n.z, (n.x, n.y), tuple(n.modulate), cmds))
        return out


# ---------------------------------------------------------------- the session replay (rc_tool replay's)

FRAME = 1.0 / 60.0
ACTIONS = {"undo": EVENT_UNDO, "reset": EVENT_RESET, "clear_plan": EVENT_CLEAR_PLAN,
           "cycle_display": EVENT_CYCLE_DISPLAY, "toggle_gravity": EVENT_TOGGLE_GRAVITY,
           "toggle_visibility": EVENT_TOGGLE_VISIBILITY, "any": EVENT_ANY_KEY, "fire": EVENT_FIRE}
MOVES = {"move_left": KEY_LEFT, "move_right": KEY_RIGHT, "move_up": KEY_UP, "move_down": KEY_DOWN}


def replay(script, levels_dir=ROOT / "data" / "levels", draw_list=False):
    """A session script (data/sessions, rc-session/1) played through the C API:
    the golden's records (bench/sessions/<name>.jsonl lines)."""
    level = Level((pathlib.Path(levels_dir) / (script["level"] + ".json")).read_text())
    sw = script.get("switches", {})
    game = Game(level, -1, int(sw["visibility"]) if "visibility" in sw else -1)   # (Stage 136: infinity always on)
    lines = []
    game.advance(0)
    lines.append(game.record({"i": 0, "op": "start"}, draw_list))
    for i, step in enumerate(script["steps"]):
        op = step["op"]
        extra = {"i": i + 1, "op": op}
        if op == "teleport":
            game.teleport(*step["at"])
        elif op == "cursor":
            game.set_cursor(*step["at"])
        elif op == "click":
            game.event(EVENT_PLAN_REMOVE if step["button"] == "right" else EVENT_PLAN_ADD)
        elif op == "key":
            extra["action"] = step["action"]
            game.event(ACTIONS[step["action"]])
        elif op == "fire":
            speedup_at = step.get("speedup_at", -1)
            game.event(EVENT_FIRE)
            stream = []
            f = game.frame()
            while f.flying:
                stream.append([[f.arrow_x, f.arrow_y], [f.arrow_dx, f.arrow_dy]])
                game.advance(FRAME)
                if len(stream) == speedup_at:
                    game.event(EVENT_ANY_KEY)
                f = game.frame()
            extra["flight"] = {"frames": len(stream), "stream": digest_sha(stream),
                               "end": stream[-1][0] if stream else None}
        elif op == "hold":
            held = 0
            for a in step["actions"]:
                held |= MOVES[a]
            traj = []
            for k in range(step["ticks"]):
                game.advance(FRAME, held, held & KEY_UP if k == 1 else 0)
                f = game.frame()
                traj.append([f.player_x, f.player_y])
            extra["trajectory"] = traj
        else:
            raise ValueError("unsupported op " + op)
        game.advance(0)
        lines.append(game.record(extra, draw_list))
    return lines
