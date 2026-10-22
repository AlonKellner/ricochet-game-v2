# TDD 35: The path preview's look (Stage 130)

The user asked (2026-10-22) for four changes to the path preview:

- the solid green path becomes a slightly dark green stroke with slightly bright green dashes over it;
- the solid red path likewise, in red;
- dashed preview elements move along the arrow's direction;
- sections drawn on one another are set side by side, 1–2 px apart, so each can be seen. Full step overlaps came
  first; the user then asked for steps on one carrier with different ends too. Partial overlaps of different
  carriers are left for later.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 130.1 | The colours: a dark solid stroke under bright dashes (GAME_SPEC 4.3) | Done |
| 130.2 | The dashes run along the arrow's way (the presenter's clock) | Done |
| 130.3 | Steps on one carrier that run together are drawn side by side | Done |
| 130.4 | Sessions re-recorded (draw only; approved), docs, sign-off | Done (user, 2026-10-22) |

## 130.1 The colours

A solid step (ALIGNED, DIVERGED_PLANNED) is drawn twice:

- first a stroke in `#00B300` / `#B30000`;
- then dashes in `#80FF80` / `#FF8080` over it.

The dashed-only steps keep their colours (green, yellow, red).

## 130.2 Running dashes

`rc::arc::dash_line` and `dash_arc` take a phase in px that moves the pattern forward along the stroke (0 is the
original pattern, bit for bit). `Session::paint_path(out, phase)` paints the path node, and the presenter repaints
it every frame at `now × PresentConfig::dash_speed` (30 px/s), during a path morph too.

The session's own node keeps phase 0, so replays and goldens stay deterministic. With `dash_speed = 0` the
presenter's path is the session's.

## 130.3 Side by side

`side_by_side` groups the merged steps that are drawn on one another, transitively:

- they share a carrier: the same line, or the same circle (the same centre and radius), to 0.5 px;
- they run together for more than 2 px (sampled), or have the same ends either way round.

A group is drawn in two lanes 3.5 px apart (a 1.5 px margin between 2 px strokes):

- the planned path's steps on the left of the group's first step's way, the physical path's on the right;
- a line moves along its normal, and an arc moves to a larger or smaller radius;
- a whole step moves, so each path stays joined along the carrier;
- a path that runs on itself is spread the same way.

Endpoint circles stay where they are.

The user's case (F12 dump, 15:43): after a piece divergence on crossings, the planned arc ends at the cursor while
the physical arc runs on to the next hit. These are steps on one circle with different ends, and they are now in
two lanes.

## The golden

The sessions were re-recorded: only `draw` changed, in 1,372 of 1,414 records (the other 42 draw no path). The
re-record was approved by the user (2026-10-22).

## Tests

| Test | What it checks |
|---|---|
| `present: a solid step is a dark stroke under bright dashes, and the dashes run along the arrow's way` | the two strokes and their colours; 4 px of phase moves the dashes 4 px along |
| `present: steps drawn on one another are set side by side, the planned on the left` | either way round; steps apart stay put |
| `present: steps on one carrier with different ends are set side by side` | the user's dump: the planned and physical arcs 1.75 px either side of their circle |
| `present: without animation the presenter's frame is the session's` | with the dashes still |
