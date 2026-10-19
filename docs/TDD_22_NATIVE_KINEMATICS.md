# TDD 22: The native player controller (Stage 117)

The third stage of the engine-agnostic port (TDD_20's introduction): the player's movement leaves Godot's
physics (`CharacterBody2D.move_and_slide`) for the engine-free controller every host drives the same way
(`native/game`, `rcg/kinematics.hpp`; GAME_SPEC 2.29.0 §25.1). The planned behaviour change of the program,
brought with evidence and approved.

Background: TDD_20 (the session golden: its kinematic sessions are this stage's evidence), TDD_21 (`rcg::Session`).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 117.0 | Branch `agnostic-kinematics`, this doc; the user's decisions (2026-10-19): an exact controller with Godot's feel (not an emulation of `move_and_slide`); an absent player-solid surface does not block | Done |
| 117.1 | `rcg/kinematics.hpp`: a radius-12 circle swept exactly against segments, arcs (their circle's band and their ends) and full circles; slides (at most 4 contacts a tick), de-penetration, a 0.08 skin (Godot's safe margin), a 1 px floor snap, the 45-degree floor; top-down and gravity as `player.gd` (speed 200, jump 400). The colliders: the present player-solid surfaces and the level's colliders | Done |
| 117.2 | Tests (`native/tests/game/test_kinematics.cpp`): the wall stop and the floor rest a skin away, the jump's apex v^2/2g, no jump in the air, absent surfaces; on random input over every level with solids, both modes, no penetration and no tunnelling between ticks (864k checks); a fall at 1e7 px/s^2 through a thin floor | Done |
| 117.3 | Evidence: `rcg::Session::hold`, the walk sessions replayed natively; `docs/research/kinematics_judge.py` against the CharacterBody2D recordings. A first run found a controller bug (an arc's outer circle taken as a contact for a player inside the arc's circle: stuck in `main` and `inversion_grid`), fixed: inside the circle is free unless in the arc's band. Then: at most 0.08 px (Godot rests touching a wall, the native controller a skin away); jumps equal | Done |
| 117.4 | Godot drives the native controller (`RcgKinematics`; `player.gd` a Node2D with its own velocity and floor state; `LevelSettings.player_colliders`); the surfaces' collision shapes removed; the recorded game equals the native replay on all 40 sessions (the up action just pressed on the second tick of a hold: the engine's input timing). The walk sessions re-blessed (approved by the user, 2026-10-19), every session in `rcg_sessions_*`. `tests/test_stage42_5_arc_collision.gd` retired (Godot's arc collision shapes are gone; the native tests cover arcs exactly) | Done |
| 117.5 | Docs (GAME_SPEC 2.29.0 §25.1, §25.2; CHANGELOG; the session digest's `hold`), `make gates` (735 s, green) | Done |
| 117.6 | Interactive sign-off (walking along arcs and into corners, jumps, G mid-air, undo) | Pending (user) |

## Findings

| # | Finding | Status |
|---|---|---|
| F2 (TDD_20) | `inversion_grid` with gravity: the player falls out of the level (nothing solid below) | Unchanged, as before: a level design matter (the level is a top-down playground) |
