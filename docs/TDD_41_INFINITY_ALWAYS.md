# TDD 41: Infinity always on (Stage 136)

After Stage 135 the user asked why the infinity switch existed at all: "When would I want to not count infinity? I
lean towards option 2, remove the switch." It was a leftover of the Stage 88 migration, which added the passage of
infinity behind a switch so the older goldens could be replayed unchanged. The oracle and the shipped game always ran
with it on; only the native core's default (and the goldens recorded with it) still had it off.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 136.1 | The switch removed: oracle, core, game, C API, hosts, tests | Done |
| 136.2 | The `escape` event retired with it | Done |
| 136.3 | The goldens re-traced with infinity on | Done (approved: the judge's report) |
| 136.4 | The sweep goldens' `landing` flag | Nothing to do (below) |
| 136.5 | The interactive test: a wrap's way back through crossings off the level (the arrow, the half rings) | Done (approved: 137 records) |
| 136.6 | The preview's dashes run on by the walk's length (the user) | Done (approved: 1,228 records) |

## 136.1 The switch removed

- **The oracle** (`trace2.py`): `EMIT_INFINITY` and its switched-off paths (a line's escape, a circle's unseen passage
  of `Q`); the generators' and tests' settings of it; `test_a_circle_beam_with_the_switch_off_counts_its_passages_unseen`.
- **The core:** `Scene::emit_infinity`, the ladder context's and the tracer's `set_emit_infinity` / `emit_infinity()`,
  `sweep::run`'s parameter; `behind_origin` counts every infinity and origin entry; the walker's infinity candidate is
  unconditional; its escape and unseen-passage branches and `fired_state` are gone.
- **The game:** `SessionConfig::infinity`, the session's override, the record's `infinity` field, the F12 dump's line,
  the replays' reads of it; the hover's infinity band is always live.
- **The C API:** `rc_game_config.infinity` is kept for the struct's layout and ignored (API 1.7).
- **The Godot host:** the `infinity` property.
- **The data:** the manifests' `emit_infinity` field; the session `open_room_playable__ported_edge_off` (the GUT
  ledger: its row and `test_switch_off_never_emits_the_event` dropped).
- **Tests:** the switch-off halves and comparisons (`infinity: with the switch off ...`, `infinity: with the switch on
  every older fixture differs ...`, the switch-off half of the ported Stage 88 wrap test); the 19 files that set the
  switch no longer do.

## 136.2 No escape

With infinity always on nothing emits `escape`: a line with nothing to hit wraps through infinity and goes round until
a limit (GAME_SPEC 12.5, 12.6, 21.3). `EventKind::Escape` and its readers (the tracer's point, the region's end kind,
the physics-compliance check's trailing step) are deleted.

## 136.3 The goldens

`make judge RULES=stage136` (`docs/research/stage136_judge.py`) against main (0451352): a changed trace is the old one
with passages of infinity and of the ray's origin inserted; where the old one escaped (the end of a loop with nothing
fired) the new one goes round again; the passages count, so a trace may be cut sooner; a physical walk passing
infinity unplanned before its cursor no longer lists it (a divergence, §12.5).

| What | Changed | Classes |
|---|---|---|
| Sweep combos | 128,968 | 66,711 past the old escape; 52,564 passages inserted; 9,608 cut sooner; 85 the cursor after an unplanned passage |
| Fixture / corpus fields | 1,216 | 578 cut sooner; 371 past the old escape; 196 passages inserted; 71 error bounds (consequences) |
| Sessions | 47 | the start record's `"infinity": true` gone; every other record identical |
| Manifests | 145 | the `"emit_infinity": true` line gone (they already ran with it on) |

No findings. The region digests are unchanged (the session's default was on), and the oracle comparisons pass at every
tier: the new goldens are the oracle's walks.

## 136.4 The `landing` flag

Stage 134 noted that the sweep goldens' frozen input definitions lacked the `landing` flag. On inspection no sweep
golden group uses the hit_effects level, and everything that does (the manifests, the corpus scenarios, the scene
dumps the region digests read) already carries it. Nothing to refresh.

## 136.5 A wrap's way back through crossings off the level

The user's F12 dumps (open_room_playable, no plan): a steep shot through the ceiling wraps through infinity and comes
back up through the floor, five laps. In one the off-segment crossings fall before the wrap (far above the level), in
the other after it (far below): there the way back reaches the level only on its last step, through the bottom edge.

- **The arrow** (`rc::arc::advance`): out of the level it skipped ahead to a step that starts inside, else to a step's
  end -- a step entering from outside sent it to its end (it appeared at the floor's crossing). It now resumes where the
  step enters the bounds (`segment_entry`, Liang-Barsky; an arc by `arc_reentry`).
- **The half rings** (`event_marks`): a passage of infinity off the level is marked out and back in when the step
  reaching it ends off the level. A wrap's way out is clipped onto the edge, which `has_point` counts in on the top and
  left: those wraps got a full ring cut by the edge and no half ring back in. The test is now the level's interior.

Tests: `visual: the arrow re-enters the bounds where a step from outside enters them`; `present: a wrap whose way back
crosses far off the level -- half rings and the arrow's re-entry` (both dumps: two half rings a lap, the arrow flying
from the bottom edge to the floor; the second dump fails without the fixes).

Sessions (the draw lists and flights recorded in full on both sides): 127 records gain the half rings of wraps out
through the top or left edge; 10 flights change -- 6 open_room_playable shots that vanished at the right edge now come
back in at the left one and fly the return, 4 pre_inverted shots re-enter where their arcs cross the edge instead of
at a step's end.

## 136.6 The dashes run on

The user: "the dashed phase is not aligned between them ... I expect the dashes to be determined by the total section
length from the player position, through the chain of steps and hitpoints up to that point". Each drawn step's dash
pattern starts at its distance from the player along its walk (`rcg::dash_offsets`): the common part's steps in turn,
each branch past the divergence on from the common part's length; an arc measured at its lane's radius, a line from
where it begins (not where the drawing clips it). Test: `present: the dashes run on across the steps, by the walk's
length from the player`. Sessions: 1,365 records' draw lists, only the dash strokes besides 136.5's changes.
