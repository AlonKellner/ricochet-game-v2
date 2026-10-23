# TDD 37: The ray's origin as a plan entry (Stage 132)

Stage 131 made the ray's origin significant: a walk that passes it unplanned before the cursor diverges. The user
asked for it to be plannable, like ∞, "only when the plan so far provably reaches it". Their decisions
(2026-10-22):

- **Plannable when the path is behind the ray's origin.** That is usually after going through infinity, and may also
  be after an inner circle normal projection.
- **The UI:** a marker at the origin's point, a circled cross like an on-screen `Q`.
- **Where the rule comes from** (the user): "the orientation code uses that info". The half of the ray's line the walk
  is on is the realization's own parity.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 132.1 | The entry `O` (`ORIGIN_ID = -4`): consumed by the passage in both walks (spec, oracle, native) | Done |
| 132.2 | The realization by the parity of ∞ and origin entries (`behind_origin`) | Done |
| 132.3 | The region from the ray's origin after an origin entry; the checks | Done |
| 132.4 | The session: plannable where the plan leaves the walk behind the origin; marker, marks, click, record | Done |
| 132.5 | Sessions judged and re-recorded (approved); docs; `make gates`; sign-off | Done (user, 2026-10-23) |
| 132.6 | The user's interactive findings: a portal's half rings along its normal; a wrap's half rings on screen | Done |

## 132.1 The entry

`(ORIGIN_ID, "")` / `PlanItem::is_origin()`. The passage of the ray's origin (Stage 131: a walk candidate, or the
lap's end at a departure that is the origin) consumes it when it is next in the plan, in both walks. The passage is
then applied, like infinity's.

- In the physical walk, the B5 shadow steps over it (`point_step`); an unconsumed passage before the cursor still
  diverges.
- An applied origin step carries the id `ORIGIN_ID`, for the plan's highlighting and PLAN_EFFECTS_APPLIED.
- The oracle has `_origin_passage`, native `Solver::origin_passage`.

## 132.2 The half of the ray's line

Along a beam, the ray's origin and the frame's infinity split the line (or circle) in two halves. A walk leaves the
origin **in front**.

- A passage of either point turns it: a planned ∞ entry leaves it **behind**, and a planned origin entry **in front**
  again.
- Maps keep the order of the beam's points, so they keep the half.

`behind_origin(plan)` is the parity of the ∞ and origin entries. The realization reads it:

- R3 (`via_infinity`): the first epoch's entries;
- R2: the entries before the first one.

Before, a single ∞ entry reversed the aim. Now `[∞, O]` aims at the cursor: the walk passes it with the entries
pending, wraps, passes the player, and reaches it. `[∞, ∞]` aims at it too. It diverges at the unplanned origin
between the wraps whichever way it aims, so only the path shown changes.

## 132.3 The region

After an origin entry, the near end is the ray's origin:

- `End::PoleP` when it is `P_K` (every frame map, and a circle normal's centre);
- its own point otherwise (a line normal or directional projection's departure).

The consumption scan (`eK`), the read horizon, ON-SEGMENT, PLAN-EFFECTS, VIS-REGULAR and VIS-ITERATION-CHAIN
step over the entry as they do ∞'s. Two new oracle-labelled cases: `empty_room_infinity_origin` and
`mirror_and_wall_infinity_origin` (with cursors either side of the player's image).

## 132.4 The session

`Solver::plan_origin` gives the origin's point after the plan and whether the plan leaves the walk behind it.

- **The point:** the player, or the last projection's new ray's origin, mapped by the frame maps after it. A circle
  normal's origin is its centre; a line projection's is its departure, the back-propagated hit from the cursor's
  anchor chain.
- **Behind:** a circle normal projecting toward its centre starts behind. Then the realization's parity
  (`behind_origin`) applies over the entries since the last projection.

The session (`plannable_origin`):

- draws a circled cross at the origin where the plan leaves the walk behind it;
- hovers and plans it like a surface (the nearest within the click tolerance wins);
- numbers the planned origin entries at their points, valid when the physical walk consumed them;
- a right-click on a planned entry's mark removes the latest origin entry;
- records `origin` (hovered, point, marks) when there is one to show.

## Tests

| Test | What it checks |
|---|---|
| `test_stage132.py` (oracle) | the halves; `[∞, O]` reaches the cursor on the next lap; the user's dump planned `[5 L, ∞, O]` is visible (`[5 L, O]` is not: the wrap comes first); an inner circle normal plans its centre |
| `origin entry: the Stage 132 fixtures` (rc_tests) | `stage132_origin_entry.json`: 14 walks at f64, Dual and 4 limbs, 8 origin entries consumed |
| `visibility: ... oracle's labels` | the two origin cases |
| `region reports: open_room_playable [5 L, inf] -- the ray's origin is plannable` | no marker after `[5 L]`; the marker at the player's image after `[5 L, ∞]`; hovered and planned by a click; the user's cursor visible; the mark valid; right-click removes it |

## The golden (approved by the user 2026-10-22)

| Golden | Changed | What |
|---|---|---|
| sessions | 20 of 48 | 19 with an ∞ plan: the origin's marker after `[∞]` (at the player) and the record's `origin` field (64 records). The 15 tours also plan `[∞, ∞]`, which now aims at the cursor (the parity): preview, facing and 12 flights. In hit_effects' tour the shot lands elsewhere after that. `[∞, ∞]` diverges at the unplanned origin between its wraps either way |
| everything else | none | the fixtures (but the two new region cases), the corpus, the sweep goldens and the region digests are unchanged |

## 132.6 The user's interactive findings (2026-10-23)

The user signed off with two findings on the Stage 131 marks:

- **A portal's half rings were aligned with the path.** They are now normal to the portal surface: at the entry the
  half faces the side the beam came from, at the exit the side it leaves by. The session gives the painter the
  surface's normal (a line's, or an arc's radius at the point), of the pair at the exit.
- **A wrap's half rings were not on screen.** The step that reaches infinity, and the one after it, usually lie off
  the level already: the path left the level a step or two earlier. The halves were looked for on those steps, and
  only when the step carried ∞'s id, which an unplanned wrap never does. The wrap is now found by the step after it
  (`after_infinity`). The outgoing half sits where the path last leaves the level before the wrap, the incoming half
  where it first enters again after it, each normal to the level's edge.

The test `present: a wrap and a portal are marked by two half rings` now uses the steps as the game draws them (a
wrap from open_room_playable; portals 10 and 11 of the portals playground, a beam in at a slant). The sessions'
draw lists are re-recorded: 38 sessions, 588 records, the draw only.
