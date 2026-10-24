# TDD 50: The goal and the detectors (Stage 145)

The editor program's fourth stage (the plan's first wave of new mechanics, part one). A level now has an
objective worth editing. A **goal** region is gated by flag conditions and won by standing in it. **Detectors**
write flags as the player moves, so a door opens when the player steps on a plate. Targets become ordinary flag
writers: a button opens the door to the goal. Everything here is data and one primitive each, and nothing
reaches the solver. The rules: GAME_SPEC 3.2, 24.5, 24.6.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 145.1 | The format: `objective` and `detectors` (`rcg::Objective`, `rcg::Detector`, `Region`), strict, outside the lowering | Done |
| 145.2 | The session: detectors after every tick, the latched win (goal, or every target), checkpoints, the HUD's goal line, `won` in a goal level's record | Done |
| 145.3 | Drawing (the goal, the detectors: new node kinds), the C API 1.9 (`rc_frame.won`) | Done |
| 145.4 | The editor: tools O (goal) and D (detector), their drawing, the panel's fields | Done |
| 145.5 | Two demo levels and their sessions; tests; docs; `make gate` | Done |
| 145.6 | The user's interactive sign-off (play both demo levels; author a goal level in the editor) | Open |

## Decisions

- **Older records unchanged.** `won` enters the record, and the goal line the HUD, only in a level with a
  goal. A level without one gets the all-targets win in `rc_frame.won` alone. The goal and the detectors draw as
  new node kinds, appended to the enum, in levels that have them. Every existing record, dump and lowering is
  byte-identical, and the two new levels add files only.
- **The win is latched, and part of the checkpoint.** Walking out of the goal does not unwin. An undo to before
  the win does.
- **Detectors write ordinary flags**, so presence and resolvers follow on the same tick, and so does the
  collision, which reads presence every tick. `while` unsets its flag outside: unset is `DEFAULT`, which a door
  reads as closed.
- **A segment's crossing is decided by orientation signs:** from strictly one side onto or past the line, within
  the segment. A move off the line is no crossing, so standing on the line does not fire twice.
- **Detectors `for` objects are accepted and wait for objects** (Stage E6); they never fire today.
- **The kinematics random test** seeded each level by its index among the levels with solids. Adding levels
  changed every later level's walk and exposed a chord between two tick positions that cut a corner the player
  slid around: 0.0018 px within the radius, no penetration at any tick. Each level is now seeded by its own path
  (FNV-1a), with the tolerance unchanged, so a new level changes no other level's walk.
- **The Godot host skips polygons Godot cannot triangulate.** The new sessions shifted the Godot replay's
  sample (every fourth teleport session by its place in the sorted list). The newly sampled
  `playground__portals__shots` makes, at Godot's own frame times during a flight, a polygon so thin that
  `canvas_item_add_polygon` cannot triangulate it. Godot fills nothing for it and reports an error. The game and
  its records are unchanged: the C API hosts' frames at 60 Hz never produce it. `RicochetLayer` now checks
  `Geometry2D::triangulate_polygon` before a fill. A pre-existing host issue, found by the sample shift.
- **The migration round trip** holds exactly for the 37 levels Stage 142 migrated. A level written in
  `rc-level/2` (the demo levels) has its author's names and parameters, which surfaces do not tell, so for it the
  test checks that the migration lowers to the same surfaces.

## Tests

- `rcg_tests` "objective: ..." and "detectors: ..." (6): the goal wins only after a tick inside, latched,
  recorded, reset; locked by its conditions until a detector opens it; `while`, `enter`, `toggle` and a segment
  crossed both ways; the all-targets win (a shot at a target); both demo levels played to their goals (a door
  that holds, a button shot that opens it, a plate that opens a door); the schema's errors.
- "editor: the goal and the detector tools (Stage 145)": two corners place a goal and a detector, a door and
  conditions validate, both are drawn, a goal's mistake is named.
- The session golden gains `objectives__button_door__play` and `objectives__plate_door__play` (every host
  replays them).
