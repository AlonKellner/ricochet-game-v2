"""The level editor in a tkinter window: a second host of the native editor (rc_editor_*, docs/EDITOR.md),
beside Godot's -- it proves the editor needs no engine. It paints the editor's draw list (window pixels)
and forwards the pointer, keys and text; on a save request it writes the document; in play mode it plays
the document with an ordinary game (rc_game_*), the view fitted to the level's bounds.

    python3 tests/host_py/rc_editor_tk.py [level.json] [--size 1600x900]

The keys are the editor's (docs/EDITOR.md): V select, L line, A arc, C circle, P the player's start,
Tab the next kind, ctrl/cmd+Z undo, ctrl/cmd+S save, F5 play (Esc back), ctrl/cmd+O the level list,
ctrl/cmd+left/right the previous/next level. In play: the mouse aims, the
left button plans, the right button unplans, Space fires, WASD moves, Z undo, R reset, C clear the plan.
"""
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rc_host  # noqa: E402

BACKGROUND = (0.07, 0.07, 0.09)


def hex_color(rgba, background=BACKGROUND):
    """A colour with alpha as Tk's opaque #rrggbb (blended over the background: Tk has no alpha)."""
    r, g, b, a = rgba
    mix = [c * a + bg * (1 - a) for c, bg in zip((r, g, b), background)]
    return "#%02x%02x%02x" % tuple(max(0, min(255, int(round(255 * c)))) for c in mix)


class Painter:
    """Draw-list commands onto a Tk-like canvas (create_line, create_polygon, create_oval,
    create_rectangle, create_text): through `transform` (world -> window: scale, ox, oy)."""

    def __init__(self, canvas, background=BACKGROUND):
        self.canvas = canvas
        self.background = background

    def paint(self, nodes, scale=1.0, ox=0.0, oy=0.0):
        for key, z, (nx, ny), modulate, cmds in nodes:
            for c in cmds:
                self.command(c, nx, ny, modulate, scale, ox, oy)

    def command(self, c, nx, ny, modulate, scale, ox, oy):
        cv = self.canvas

        def tp(p):
            return ((p[0] + nx) * scale + ox, (p[1] + ny) * scale + oy)

        def col(i=0):
            base = c["colors"][i] if c["colors"] else (1, 1, 1, 1)
            return hex_color(tuple(base[k] * modulate[k] for k in range(4)), self.background)

        def flat(pts):
            return [v for p in pts for v in tp(p)]

        width = max(1.0, c["width"] * scale) if c["width"] >= 0 else 1.0
        kind = c["kind"]
        if kind == "line":
            cv.create_line(*flat(c["points"]), fill=col(), width=width)
        elif kind == "dashed_line":
            d = max(1, int(round(c["dash"] * scale)))
            cv.create_line(*flat(c["points"]), fill=col(), width=width, dash=(d, d))
        elif kind == "arc":
            pts = rc_host.tessellate_arc(c["points"][0][0], c["points"][0][1], c["radius"], c["start"], c["end"],
                                         max(2, c["arc_point_count"]))
            cv.create_line(*flat(pts), fill=col(), width=width)
        elif kind in ("polyline",):
            if len(c["points"]) >= 2:
                cv.create_line(*flat(c["points"]), fill=col(), width=width)
        elif kind == "multiline":
            pts = c["points"]
            for i in range(0, len(pts) - 1, 2):
                cv.create_line(*flat(pts[i:i + 2]), fill=col(), width=width)
        elif kind in ("polygon", "colored_polygon"):
            if len(c["points"]) >= 3:
                cv.create_polygon(*flat(c["points"]), fill=col(), outline="")
        elif kind == "circle":
            (x, y), r = tp(c["points"][0]), c["radius"] * scale
            cv.create_oval(x - r, y - r, x + r, y + r, fill=col(), outline="")
        elif kind == "rect":
            (x, y), (w, h) = tp(c["points"][0]), c["points"][1]
            cv.create_rectangle(x, y, x + w * scale, y + h * scale, fill=col(), outline="")
        elif kind == "text":
            x, y = tp(c["points"][0])
            size = max(6, int(round(c["font_size"] * (scale if scale < 1 else 1))))
            cv.create_text(x, y, text=c["text"], fill=col(), anchor="s" if c["alignment"] == 1 else "sw",
                           font=("Helvetica", -size))
        elif kind == "triangles":
            pts, idx = c["points"], c["indices"]
            for i in range(0, len(idx) - 2, 3):
                cv.create_polygon(*flat([pts[idx[i]], pts[idx[i + 1]], pts[idx[i + 2]]]), fill=col(), outline="")
        elif kind == "polygon2d":
            ca, sa = math.cos(c["rotation"]), math.sin(c["rotation"])
            pts = [(p[0] * ca - p[1] * sa, p[0] * sa + p[1] * ca) for p in c["points"]]
            if len(pts) >= 3:
                cv.create_polygon(*flat(pts), fill=col(), outline="")
        elif kind == "label":
            cv.create_text(10, 10, text=c["text"], fill="#ffffff", anchor="nw", font=("Helvetica", -14))


# Tk's names for the editor's keys
KEYSYMS = {"Escape": rc_host.EDKEY_ESCAPE, "Return": rc_host.EDKEY_ENTER, "KP_Enter": rc_host.EDKEY_ENTER,
           "Tab": rc_host.EDKEY_TAB, "ISO_Left_Tab": rc_host.EDKEY_TAB, "BackSpace": rc_host.EDKEY_BACKSPACE,
           "Delete": rc_host.EDKEY_DELETE, "Left": rc_host.EDKEY_LEFT, "Right": rc_host.EDKEY_RIGHT,
           "Up": rc_host.EDKEY_UP, "Down": rc_host.EDKEY_DOWN, "space": rc_host.EDKEY_SPACE}


def key_code(keysym):
    if keysym in KEYSYMS:
        return KEYSYMS[keysym]
    if len(keysym) == 1 and keysym.isalnum():
        return ord(keysym.upper())
    if keysym.startswith("F") and keysym[1:].isdigit() and 1 <= int(keysym[1:]) <= 12:
        return rc_host.EDKEY_F1 + int(keysym[1:]) - 1
    return None


def mods_of(state):
    mac = sys.platform == "darwin"
    m = 0
    if state & 0x1:
        m |= rc_host.MOD_SHIFT
    if state & 0x4 or (mac and state & 0x8):  # Control, or Command on a Mac
        m |= rc_host.MOD_CTRL
    if (state & 0x10 if mac else state & 0x8) or state & 0x20000:
        m |= rc_host.MOD_ALT
    return m


def main(argv):
    import tkinter as tk
    from tkinter import filedialog

    path = None
    w, h = 1600, 900
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--size":
            w, h = (int(v) for v in args.pop(0).split("x"))
        else:
            path = pathlib.Path(a)
    editor = rc_host.Editor(path.read_text() if path and path.exists() else None)
    editor.resize(w, h)
    # Stage 155: the level menu -- the levels under data/levels, by name (their path without .json)
    levels_root = pathlib.Path(__file__).resolve().parents[2] / "data" / "levels"

    def name_of(p):
        try:
            return str(p.resolve().relative_to(levels_root).with_suffix(""))
        except ValueError:
            return None

    names = sorted(str(p.relative_to(levels_root).with_suffix("")) for p in levels_root.rglob("*.json"))
    listing = {"op": "levels", "list": names}
    if path is not None and name_of(path):
        listing["current"] = name_of(path)
    editor.command(listing)
    root = tk.Tk()
    root.title("Ricochet level editor" + (" - " + str(path) if path else ""))
    canvas = tk.Canvas(root, width=w, height=h, background=hex_color((*BACKGROUND, 1)), highlightthickness=0)
    canvas.pack(fill="both", expand=True)
    painter = Painter(canvas)
    mac = sys.platform == "darwin"
    right, middle = (2, 3) if mac else (3, 2)
    state = {"game": None, "level": None, "held": 0, "pressed": 0, "view": (1.0, 0.0, 0.0), "size": (w, h),
             "pending": False}
    MOVES = {"a": rc_host.KEY_LEFT, "d": rc_host.KEY_RIGHT, "w": rc_host.KEY_UP, "s": rc_host.KEY_DOWN}

    def button_of(num):
        return rc_host.BTN_LEFT if num == 1 else rc_host.BTN_RIGHT if num == right else rc_host.BTN_MIDDLE if num == middle else 0

    def schedule():
        if not state["pending"]:
            state["pending"] = True
            root.after_idle(repaint)

    def repaint():
        state["pending"] = False
        canvas.delete("all")
        game = state["game"]
        if game is None:
            painter.paint(editor.draw_list())
        else:
            painter.paint(game.draw_list(), *state["view"])
            canvas.create_text(10, state["size"][1] - 10, anchor="sw", fill="#bbbbbb", font=("Helvetica", -13),
                               text="playing: Esc returns to the editor")
        after_input()

    def after_input():
        nonlocal path
        st = editor.status()
        if st.open_requested:  # Stage 155: a level from the menu
            name = editor.command({"op": "open_request"}).get("name", "")
            target = levels_root / (name + ".json")
            if name and target.exists():
                editor.command({"op": "load", "text": target.read_text(), "name": name})
                path = target
                root.title("Ricochet level editor - " + str(target))
                schedule()
        if st.save_requested:
            target = path
            if target is None:
                chosen = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("rc-level/2", "*.json")])
                target = pathlib.Path(chosen) if chosen else None
            if target is not None:
                target.write_text(editor.document())
                editor.command({"op": "saved"})
                root.title("Ricochet level editor - " + str(target))
            else:
                editor.command({"op": "save_cancelled"})  # still unsaved; no dialog again until the next request
        if st.mode == rc_host.EDITOR_PLAY and state["game"] is None:
            state["level"] = rc_host.Level(editor.document())
            state["game"] = rc_host.Game(state["level"], animate=1)
            f = state["game"].frame()
            sw, sh = state["size"]
            scale = min(sw / max(1.0, f.bounds_w), sh / max(1.0, f.bounds_h))
            state["view"] = (scale, (sw - f.bounds_w * scale) / 2 - f.bounds_x * scale,
                             (sh - f.bounds_h * scale) / 2 - f.bounds_y * scale)
            tick()
        elif st.mode == rc_host.EDITOR_EDIT and state["game"] is not None:
            state["game"] = None
            state["level"] = None
            schedule()

    def tick():
        game = state["game"]
        if game is None:
            return
        game.advance(1 / 60, state["held"], state["pressed"])
        state["pressed"] = 0
        schedule()
        root.after(16, tick)

    def world(x, y):
        s, ox, oy = state["view"]
        return (x - ox) / s, (y - oy) / s

    def on_motion(e):
        if state["game"] is not None:
            state["game"].set_cursor(*world(e.x, e.y))
        else:
            editor.pointer(rc_host.PTR_MOVE, e.x, e.y, 0, mods_of(e.state))
        schedule()

    def on_press(e):
        if state["game"] is not None:
            b = button_of(e.num)
            state["game"].event(rc_host.EVENT_PLAN_ADD if b == rc_host.BTN_LEFT else rc_host.EVENT_PLAN_REMOVE)
            state["game"].advance(0)
        else:
            editor.pointer(rc_host.PTR_DOWN, e.x, e.y, button_of(e.num), mods_of(e.state))
        schedule()

    def on_release(e):
        if state["game"] is None:
            editor.pointer(rc_host.PTR_UP, e.x, e.y, button_of(e.num), mods_of(e.state))
            schedule()

    def on_wheel(e):
        if state["game"] is not None:  # playing: the wheel moves the level's cycle
            delta = e.delta if e.num not in (4, 5) else (1 if e.num == 4 else -1)
            state["game"].scroll(1.0 if delta > 0 else -1.0)
            schedule()
            return
        if state["game"] is None:
            delta = e.delta if e.num not in (4, 5) else (1 if e.num == 4 else -1)
            editor.scroll(0.0, -1.0 if delta > 0 else 1.0, mods_of(e.state))
            schedule()

    def on_key(e, down=True):
        if state["game"] is not None:
            k = e.keysym.lower()
            if k == "escape" and down:
                editor.key(rc_host.EDKEY_ESCAPE, 0, True)
            elif k in MOVES:
                if down:
                    if not state["held"] & MOVES[k]:
                        state["pressed"] |= MOVES[k]
                    state["held"] |= MOVES[k]
                else:
                    state["held"] &= ~MOVES[k]
            elif down and k in ("space", "z", "r", "c", "tab", "v", "g"):
                ev = {"space": rc_host.EVENT_FIRE, "z": rc_host.EVENT_UNDO, "r": rc_host.EVENT_RESET,
                      "c": rc_host.EVENT_CLEAR_PLAN, "tab": rc_host.EVENT_CYCLE_DISPLAY,
                      "v": rc_host.EVENT_TOGGLE_VISIBILITY, "g": rc_host.EVENT_TOGGLE_GRAVITY}[k]
                state["game"].event(ev)
                state["game"].advance(0)
            schedule()
            return "break"
        code = key_code(e.keysym)
        if code is not None:
            editor.key(code, mods_of(e.state), down)
        if down and e.char and len(e.char) == 1 and ord(e.char) >= 32 and not (mods_of(e.state) & rc_host.MOD_CTRL):
            editor.text(e.char)
        schedule()
        return "break"

    def on_configure(e):
        if (e.width, e.height) != state["size"] and e.width > 100 and e.height > 100:
            state["size"] = (e.width, e.height)
            editor.resize(e.width, e.height)
            schedule()

    canvas.bind("<Motion>", on_motion)
    for n in (1, 2, 3):
        canvas.bind("<ButtonPress-%d>" % n, on_press)
        canvas.bind("<ButtonRelease-%d>" % n, on_release)
        canvas.bind("<B%d-Motion>" % n, on_motion)
    canvas.bind("<MouseWheel>", on_wheel)
    canvas.bind("<Button-4>", on_wheel)
    canvas.bind("<Button-5>", on_wheel)
    root.bind("<KeyPress>", lambda e: on_key(e, True))
    root.bind("<KeyRelease>", lambda e: on_key(e, False))
    canvas.bind("<Configure>", on_configure)
    canvas.focus_set()
    schedule()
    root.mainloop()


if __name__ == "__main__":
    main(sys.argv[1:])
