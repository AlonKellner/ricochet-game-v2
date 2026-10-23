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
