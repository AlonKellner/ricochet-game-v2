"""The level editor through the C API (rc_editor_*, docs/EDITOR.md) from Python: commands, pointer
placement, the draw list, the status, every level loaded and saved byte for byte, a play-test with an
ordinary game, and the tkinter host's painter (rc_editor_tk.Painter) on every draw command kind."""
import json
import pathlib

import rc_host
from rc_editor_tk import Painter, hex_color, key_code

ROOT = rc_host.ROOT


def levels():
    return sorted((ROOT / "data" / "levels").rglob("*.json"))


def test_every_level_loads_and_saves_byte_for_byte():
    for p in levels():
        text = p.read_text()
        ed = rc_host.Editor(text)
        assert ed.document() == text, p
        st = ed.status()
        assert st.errors == 0 and st.dirty == 0 and st.mode == rc_host.EDITOR_EDIT


def test_commands_and_undo():
    ed = rc_host.Editor()
    empty = ed.document()
    r = ed.command({"op": "add", "element": {"kind": "door", "start": [900, 300], "end": [900, 500]}})
    assert r == {"ok": True, "name": "door_1"}
    assert ed.command({"op": "add", "element": {"kind": "button", "opens": "door_1", "start": [700, 300],
                                                 "end": [800, 300]}})["ok"]
    assert ed.status().dirty == 1
    doc = json.loads(ed.document())
    assert [e["kind"] for e in doc["elements"]] == ["door", "button"]
    assert ed.command({"op": "nope"}) == {"ok": False, "error": "unknown op 'nope'"}
    assert ed.command({"op": "undo"})["ok"] and ed.command({"op": "undo"})["ok"]
    assert ed.document() == empty
    lowered = ed.command({"op": "lower"})["level"]
    assert lowered["schema"] == "rc-level/1" and len(lowered["surfaces"]) == 4  # the wall edges (Stage 180: no room walls)


def test_the_pointer_places_a_line_and_the_draw_list_shows_it():
    ed = rc_host.Editor()
    ed.resize(1600, 900)
    ed.command({"op": "kind", "kind": "mirror"})
    ed.key(ord("L"))
    ed.key(ord("L"), down=False)
    for x in (300, 420):
        ed.pointer(rc_host.PTR_MOVE, x, 300)
        ed.pointer(rc_host.PTR_DOWN, x, 300, rc_host.BTN_LEFT)
        ed.pointer(rc_host.PTR_UP, x, 300, rc_host.BTN_LEFT)
    doc = json.loads(ed.document())
    assert len(doc["elements"]) == 1 and doc["elements"][0]["kind"] == "mirror"
    nodes = ed.draw_list()
    assert all(key[0] == "editor" for key, *_ in nodes)
    assert ["editor", 3, 0] in [key for key, *_ in nodes]  # the mirror's node
    texts = [c["text"] for *_, cmds in nodes for c in cmds if c["kind"] == "text"]
    assert "Ricochet level editor" in texts and "mirror_1" in texts
    first = ed.draw_list(changes=True)
    again = ed.draw_list(changes=True)
    assert all(unchanged for _, _, unchanged, *_ in again) and len(first) == len(again)


def test_a_play_test_plays_the_document():
    ed = rc_host.Editor((ROOT / "data" / "levels" / "main.json").read_text())
    ed.command({"op": "add", "element": {"kind": "two_sided_mirror", "start": [1300, 300], "end": [1300, 600]}})
    r = ed.command({"op": "play"})
    assert r["ok"] and ed.status().mode == rc_host.EDITOR_PLAY
    assert ed.draw_list() == []  # the host shows the game
    game = rc_host.Game(rc_host.Level(ed.document()))
    game.set_cursor(1180, 520)
    game.advance(1 / 60)
    assert game.frame().size > 0 and game.draw_list()
    assert ed.command({"op": "add", "element": {"kind": "wall", "start": [0, 0], "end": [9, 9]}})["error"] == "playing: stop first"
    ed.key(rc_host.EDKEY_ESCAPE)
    assert ed.status().mode == rc_host.EDITOR_EDIT


def test_the_save_handshake():
    ed = rc_host.Editor()
    ed.command({"op": "add", "element": {"kind": "wall", "start": [600, 300], "end": [600, 400]}})
    ed.key(ord("S"), rc_host.MOD_CTRL)
    assert ed.status().save_requested == 1
    ed.command({"op": "save_cancelled"})
    assert ed.status().save_requested == 0 and ed.status().dirty == 1
    ed.command({"op": "save"})
    ed.command({"op": "saved"})
    assert ed.status().dirty == 0


class RecordingCanvas:
    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        return lambda *args, **kw: self.calls.append((name, args, kw))


def test_the_tk_painter_draws_every_command_kind():
    def cmd(kind, points, **kw):
        c = {"kind": kind, "points": points, "colors": [(1.0, 0.5, 0.0, 0.5)], "indices": [], "text": "",
             "arc_point_count": 8, "alignment": 0, "font_size": 14, "radius": 10.0, "start": 0.0, "end": 3.14,
             "width": 2.0, "dash": 4.0, "rotation": 0.5}
        c.update(kw)
        return c
    cmds = [cmd("line", [(0, 0), (10, 0)]), cmd("dashed_line", [(0, 0), (10, 0)]), cmd("arc", [(5, 5)]),
            cmd("polyline", [(0, 0), (1, 1), (2, 0)]), cmd("multiline", [(0, 0), (1, 1), (2, 2), (3, 3)]),
            cmd("polygon", [(0, 0), (1, 0), (0, 1)]), cmd("colored_polygon", [(0, 0), (1, 0), (0, 1)]),
            cmd("circle", [(3, 3)]), cmd("rect", [(0, 0), (5, 5)]), cmd("text", [(1, 1)], text="hi"),
            cmd("triangles", [(0, 0), (1, 0), (0, 1)], indices=[0, 1, 2]), cmd("polygon2d", [(0, 0), (1, 0), (0, 1)]),
            cmd("label", [], text="hud")]
    canvas = RecordingCanvas()
    Painter(canvas).paint([(["editor", 0, 0], 0, (0.0, 0.0), (1, 1, 1, 1), cmds)], 2.0, 10.0, 20.0)
    made = [name for name, *_ in canvas.calls]
    assert made.count("create_line") == 6 and made.count("create_polygon") == 4
    assert made.count("create_oval") == 1 and made.count("create_rectangle") == 1 and made.count("create_text") == 2
    line = canvas.calls[0]
    assert line[1] == (10.0, 20.0, 30.0, 20.0) and line[2]["width"] == 4.0
    assert hex_color((1.0, 1.0, 1.0, 1.0)) == "#ffffff"
    # the editor's real list paints too
    canvas = RecordingCanvas()
    Painter(canvas).paint(rc_host.Editor((ROOT / "data" / "levels" / "playground" / "portals.json").read_text()).draw_list())
    assert len(canvas.calls) > 50


def test_tk_key_names():
    assert key_code("l") == ord("L") and key_code("Escape") == rc_host.EDKEY_ESCAPE
    assert key_code("F5") == rc_host.EDKEY_F1 + 4 and key_code("Shift_L") is None


def test_the_level_menu_handshake():
    """Stage 155: the host lists the levels; a request is taken and answered with the level's text."""
    ed = rc_host.Editor()
    ed.resize(1600, 900)
    root = pathlib.Path(__file__).resolve().parents[2] / "data" / "levels"
    names = sorted(str(p.relative_to(root).with_suffix("")) for p in root.rglob("*.json"))
    assert ed.command({"op": "levels", "list": names})["count"] == len(names)
    assert ed.status().open_requested == 0
    assert ed.command({"op": "open", "name": "objectives/lantern"})["ok"]
    assert ed.status().open_requested == 1
    name = ed.command({"op": "open_request"})["name"]
    assert ed.command({"op": "load", "text": (root / (name + ".json")).read_text(), "name": name})["ok"]
    assert ed.command({"op": "state"})["level"] == "objectives/lantern"
    assert ed.status().open_requested == 0
    assert ed.command({"op": "next_level", "by": 1})["name"] == names[(names.index(name) + 1) % len(names)]


def test_a_new_level_and_a_copy():
    """Stage 165: a new empty level is the editor's alone; a copy is read by the host and loaded under its new name."""
    ed = rc_host.Editor()
    ed.resize(1600, 900)
    root = pathlib.Path(__file__).resolve().parents[2] / "data" / "levels"
    names = sorted(str(p.relative_to(root).with_suffix("")) for p in root.rglob("*.json"))
    ed.command({"op": "levels", "list": names})
    assert ed.command({"op": "new_level", "name": "scratch/empty"})["ok"]
    assert ed.command({"op": "state"})["level"] == "scratch/empty" and ed.status().dirty == 1
    ed.command({"op": "saved"})
    assert ed.command({"op": "new_level", "name": "scratch/copy", "copy_of": "objectives/lantern"})["ok"]
    assert ed.status().open_requested == 1
    req = ed.command({"op": "open_request"})
    assert req["name"] == "objectives/lantern" and req["as"] == "scratch/copy"
    text = (root / (req["name"] + ".json")).read_text()
    assert ed.command({"op": "load", "text": text, "name": req["as"], "unsaved": True})["ok"]
    doc = json.loads(ed.document())
    assert doc["name"] == "scratch/copy" and len(doc["elements"]) == len(json.loads(text)["elements"])
    assert ed.status().dirty == 1


def test_a_level_renamed():
    """Stage 170: F2 renames the level -- a save asked for under the new name, the old name for the host to remove."""
    ed = rc_host.Editor()
    ed.resize(1600, 900)
    ed.command({"op": "levels", "list": ["a/one", "a/two"], "current": "a/one"})
    ed.command({"op": "load", "text": '{"schema": "rc-level/2", "name": "a/one", "elements": []}', "name": "a/one"})
    ed.key(rc_host.EDKEY_F1 + 1)
    assert ed.command({"op": "state"})["rename_prompt"]
    ed.text("_b")
    ed.key(rc_host.EDKEY_ENTER)
    st = ed.command({"op": "state"})
    assert st["level"] == "a/one_b" and st["renamed_from"] == "a/one"
    assert ed.status().save_requested == 1
    assert json.loads(ed.document())["name"] == "a/one_b"
    ed.command({"op": "saved"})
    assert ed.command({"op": "state"})["renamed_from"] is None
    assert not ed.command({"op": "rename_level", "to": "a/two"})["ok"]
