#!/usr/bin/env python3
"""Stage 119: session scripts for the GUT tests the ledger ports to the session
golden (docs/research/gut_ledger.tsv, docs/TDD_24_ENGINE_SWITCH.md): the
interactions those tests played on the game's levels, as rc-session/1 scripts
(docs/research/session_digest.md). Each was recorded by the GDScript game and
replayed natively, equal byte for byte, before the GDScript game was deleted.

    python3 -I docs/research/sessions/ported_sessions.py

Writes data/sessions/<level>__ported_<topic>.json. Coordinates are multiples of 1/4 px.
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]


def tp(x, y): return {"op": "teleport", "at": [float(x), float(y)]}
def cur(x, y): return {"op": "cursor", "at": [float(x), float(y)]}
def left(): return {"op": "click", "button": "left"}
def right(): return {"op": "click", "button": "right"}
def key(a): return {"op": "key", "action": a}
def fire(speedup_at=None): return {"op": "fire"} if speedup_at is None else {"op": "fire", "speedup_at": speedup_at}


SESSIONS = {
    # test_stage32_checkpoint (undo after the player moved), test_stage5_preview /
    # test_preview / test_stage100 (the cursor on the player: no preview, no shot),
    # test_stage31 (the camera back on the player after a flight, an escaping one too)
    ("main", "undo"): [
        tp(400, 600), cur(500, 600), fire(20), tp(200, 400),
        cur(300, 400), fire(20), tp(300, 400),
        cur(400, 400), fire(20), tp(999, 999),
        key("undo"), key("undo"), key("undo"), key("undo"),
        tp(500, 500), cur(500, 500), fire(), key("cycle_display"), key("cycle_display"), key("cycle_display"),
        cur(1200, 540), fire(20),
        cur(500, 1075), fire(20), tp(600, 450), key("reset"),
    ],
    # test_stage101_infinity_point: Q0 drawn and plannable (hover, click, HUD "∞",
    # right click on the edge), the unplanned passage vs the planned ∞, ∞ first
    ("playground/pre_inverted", "infinity"): [
        tp(620, 420), cur(968, 534), left(), cur(1910, 540), right(),
        cur(760, 470), cur(968, 534), left(), cur(760, 470), fire(20),
        key("reset"), tp(620, 420), cur(968, 534), left(), cur(1300, 420), fire(20),
    ],
    # test_stage88_infinity_ui: the edge band (hover 10 px in, not 40 px in, not outside),
    # a click near the mirror is the mirror, right clicks on the edge remove the latest ∞
    ("open_room_playable", "edge"): [
        tp(960, 270), cur(1910, 270), cur(1880, 540), cur(1930, 540), cur(806, 540),
        cur(1910, 270), left(), cur(1205, 540), left(), cur(960, 1075), left(),
        cur(1910, 270), right(), right(), right(),
    ],
    # test_stage88_infinity_ui: the switch off -- no edge hover, no ∞ entry
    ("open_room_playable", "edge_off"): [
        tp(960, 270), cur(1910, 270), left(), cur(960, 1075), left(),
    ],
    # test_stage86_ghost: inside a ghost stretch only togglers are plannable (hover
    # and click refused on the mirror), then the mirror again after the second toggler
    ("test_levels/ghost_room", "refuse"): [
        tp(900, 540), cur(995, 540), left(), cur(745, 530), left(),
        cur(655, 550), left(), cur(745, 530), left(), cur(1300, 540), fire(20),
    ],
    # test_stage107_level: through host A, through the combined side G (target,
    # toggle, spawn; the gate opens), the in-shot reset along the floor re-arms A;
    # test_stage107_spawn_preview: the preview before each shot is its landing
    ("test_levels/hit_effects", "effects"): [
        tp(300, 220), cur(400, 220), fire(20),
        tp(800, 690), cur(900, 690), fire(20),
        tp(350, 800), cur(450, 900), fire(20),
    ],
    # test_stage108_level: a spawn trimmed by the floor
    ("test_levels/crossings", "floor"): [
        tp(300, 800), cur(400, 800), fire(20),
    ],
    # test_stage115_live_check: a landing that changes the flags (the region follows the preview)
    ("test_levels/flag_switches", "landing"): [
        tp(700, 520), cur(1100, 520), fire(20), cur(1100, 560), fire(20),
    ],
}


def main():
    for (level, topic), steps in SESSIONS.items():
        doc = {"schema": "rc-session/1", "level": level, "kind": "teleport", "steps": steps}
        if topic == "edge_off":
            doc["switches"] = {"infinity": False}
        path = ROOT / "data" / "sessions" / ("%s__ported_%s.json" % (level.replace("/", "__"), topic))
        path.write_text(json.dumps(doc, indent=1) + "\n")
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
