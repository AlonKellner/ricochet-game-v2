# TDD 34: A hit on another piece diverges; the violation viewer (Stage 129)

The user reported (2026-10-22, with an F12 dump) a strange situation on crossings. The plan was the mirror circle 17,
left side, on its western piece. The path hit the circle's eastern piece instead, and the preview showed it aligned.
After the cursor it diverged where both paths were identical, and invariants failed.

The user chose option A: the first hit point is a divergence. The paths that follow are drawn red and yellow, as in
any divergence, although their steps coincide.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 129.1 | "On the plan" is part of a walk's frame (spec, oracle, native); the piece-aware checker; re-blessed (approved) | Done |
| 129.2 | Piece plans in the sweeps; known findings in a sweep group | Done |
| 129.3 | A whisker through a planned piece's end (GAME_SPEC 15.1) | Done |
| 129.4 | The violation viewer, restored in the Godot host (C API 1.6 `rc_game_set_plan`, `make violations`) | Done |
| 129.5 | Docs, `make gates`, sign-off | Pending |

## 129.1 Off the plan is part of the frame

**The cause.** GAME_SPEC 9.7 already said that a physical hit outside the planned piece "is not the planned one, so
the paths diverge". The physical walk followed it: it left the plan there and stopped emitting its cursor. The merge
and the region compared frames only, and a frame was (epoch, transform stack, flags). A reflection on either piece
pushes surface 17, so the frames were equal and the steps aligned. Every symptom followed from that:

- the green path off the wrong piece;
- after the cursor, the planned path had one step more than the physical one (its cursor's split), so the merge,
  pairing by index, found a divergence where the two coincide;
- NO_POST_CURSOR_DIVERGENCE and PHYSICAL_PREVIEW_MATCH failed on 497 of the region's 4326 cursors in that dump.

**The rule** (GAME_SPEC 14.2). A walk leaves the plan at the first hit that fires out of plan while entries remain:
an effect or state change at a surface that is not the next entry, or at the next entry's surface outside its
piece. From there its frame says so (`Event::off_plan`; frame keys gain "|off"; `vis::cast_events`,
`same_decision`). Before the cursor the planned walk never leaves the plan, so such a hit diverges even on the same
beam. A spent plan is not left: the hits after it change no frame. That keeps every aligned path's frames as they
were.

**Spec first, then the oracle and native.** The oracle's `_append` records the walk's `left_plan`, and
`visibility._frames_after` reads it. `test_stage129.py` covers the dump's case, its cursor and a whole-surface
control. The oracle's fixtures and the scene corpus are unchanged (`make regen`).

**The checker.** ON_SEGMENT_CONSISTENCY re-tested every hit against the whole design surface. The planned walk's
hit consuming a piece entry is now judged against the piece, within rounding of the piece's ends (crossings,
float32). The test support's renderer, which crashed on a float printed with `%s`, is fixed.

**Judged and approved** (the user, 2026-10-22):

| Golden | Changed | What |
|---|---|---|
| sweep goldens | 451 combos | the frame ids only (frames off the plan get new keys, and the planned and physical paths share one id map, so ids renumber); events, aims, tiers and the aligned prefix identical in all 451 |
| region digests | 1 | tangent_infinity [∞] at (500, 300): a whisker gone. The physical walk reflects off 6 twice at Q instead of wrapping through infinity, so the ∞ entry is never met (an unmet entry diverges) |
| sessions | 4 of 48 | inversion_grid shots and tour: the preview's frame ids only (the draw unchanged); crossings shots and tour, step 23 (9R on a piece): the region 44 cells to 4 and a whisker |

For crossings at step 23, 25 sampled cursors that left the region were all visible to the oracle under the old
rule and none under the new one; 10 kept cursors agree too.

## 129.2 Piece plans in the sweeps

The sweeps never planned a piece: crossings had only empty plans. The plan rule `pieces` plans every piece of a
split surface's transformative side alone, with its span. Two groups on crossings add 40 plans each: the
invariants (22,080 combos, about 5 s) and the visibility region (13,680 combos, about 23 s).

On main, the invariant group had 51,026 violations: the checker's 50,255 and the rule's 770. Now it has none.

A sweep group may list **known findings** (`"known"`: a finding, the surfaces of the plan's first entry, the
violation kinds). The sweep test counts them under the finding's name instead of failing, and the violations file
marks them.

## 129.3 A whisker through a planned piece's end

Under the rule, a member exactly through a crossing where the planned piece ends meets the piece there; the members
beside it meet the other piece, or the cutting surface, and leave the plan. The cursors along that walk are a 1D
part of the region (67 on the new sweep: lines 9, 11, 14, 15, 17). The user chose to keep them as true geometry.
GAME_SPEC 15.1 lists them as a third kind of whisker, and VIS-REGULAR accepts a member whisker whose planned walk
consumes a piece entry exactly at its span's end (`consumes_at_piece_end`).

## 129.4 The violation viewer

The GDScript viewer (`scripts/testing/violation_viewer.gd`) went with the GDScript game in Stage 119.4. It is now
part of the Godot host:

- **`RicochetGame.violations_path`**: the viewer mode (`scenes/violation_viewer.tscn`, `res://violations.json`).
  Each violation is shown with its level, the shots that made its world (fired and landed), its player, its plan
  (pieces by span, clicks) and its cursor, all fixed and computed in the frame.
- **Keys:** ←/→ (↑/↓) step through the violations; Tab and V work as in play; F12 dumps.
- **The label** names the manifest, the positions, the plan with its pieces, the violation, and its known finding.
- **`make violations [FILTER=…]`** runs the sweeps and writes `engines/godot/violations.json`. Its entries carry the
  level, the plan's spans and clicks, and the shots.
- **C API 1.6 `rc_game_set_plan`**: a plan from JSON (`Session::set_plan`, pieces matched by their span).

## Findings

| # | Finding | Status |
|---|---|---|
| F23 | The visibility region on the pieces of crossings' overlapping circles 19 and 20 (19L, 20L): VIS-CLOSED (open boundary chains), with VIS-CELL-CONSTANT, VIS-REGULAR, VIS-NO-BLOCKER-INSIDE and VIS-BOUNDARY-DECIDED, 337 violations. Main had 666 | Open: a known finding of the piece-plan sweep; `make violations FILTER=crossings__4` |
| F24 | DIRECTION-ONLY on a piece plan of 15L (player (1000, 250), cursor (0, 540)): the physical walk against the unplanned shot at its image. The same on main; frames play no part | Open: a known finding of the piece-plan sweep (the trimmed positions no longer meet it) |
| F21 (TDD_33) | projective 9R: casts through an exact carrier crossing climb to 64 limbs unconverged | Open |

## Tests

| Test | What it checks |
|---|---|
| `test_stage129.py` (oracle) | the physical walk leaves the plan at the other piece; the cursor beyond is not visible; the whole surface planned stays aligned |
| `region reports: crossings [17 L, western piece] -- a hit on the eastern piece diverges` | the dump replayed through `Session::set_plan`: aligned to the hit, diverged from there; preview and region agree; the control |
| the piece-plan sweeps (`crossings__3`, `crossings__4`) | every invariant, F23 and F24 named |
| `test_violation_viewer.gd` (GUT) | a violation shown as recorded; the arrow keys step |
| `capi_c_smoke` | `rc_game_set_plan` (1.6) |
