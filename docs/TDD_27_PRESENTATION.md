# TDD 27: The presenter — the player between ticks, the region morphing by surface (Stage 122)

The third stage of the smooth-frames program (plan approved by the user, 2026-10-20). Stage 121 took the
solver off the frame, but what it computes still lands on screen at once: a region arrives a few frames
behind the player and replaces the one shown, and a plan change swaps the whole region in one frame. This
stage animates those changes. It is presentation only: the game's state, its record, every result and the
session golden are unchanged, and replays run unanimated.

The decisions this stage follows (user):

- **Subtle and quick** (2026-10-20): about 80–120 ms, ease-out.
- **Crisp lines with a morph, no crossfades** (2026-10-20): no opacity blending of changing shapes.
- **Match by surface** (2026-10-21): "both visibility morphs and path preview morphs should try to match
  hitpoints of the before and after shapes by the surface ids, such that the morphs usually just move the
  points along the same surface".

The plan's presentation stage is split in two. This stage builds the presenter, the player's motion and
the region morph. Stage 123 brings the path morph (the same surface matching, over the path's hits), the
overlays, the spawn preview and the flight's dimming.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 122.1 | `rcg::Presenter` (`rcg/present.hpp`): the session's frame in, the frame on screen out; its clock | Done |
| 122.2 | The player drawn between its last two physics ticks (`Session::tick_from`, `tick_alpha`) | Done |
| 122.3 | The region morph (`rcg::region_field`); C API 1.3 (`rc_game_config.animate`); the Godot host animates | Done |
| 122.4 | Docs, `make gates`, sign-off | In progress |

## 122.1 The presenter

`Presenter::frame(nodes, session)` takes the session's frame (`Session::draw_frame`) and returns what is on
screen now. An unchanged node keeps its content version, so a host's change tracking (C API 1.1) still skips
it. `Presenter::advance(dt)` moves its clock. `rc_game_advance` calls it, so the clock is the host's frame
time.

The presenter only reads the session. A host without `animate` never creates one, and the replays, the
record and every golden are the session's alone.

## 122.2 The player between ticks

Physics runs on a fixed 1/60 s tick. On a display that is not 60 Hz the player would move on some frames and
not on others. The presenter draws the player at `tick_from + (player − tick_from) · alpha`, where
`tick_from` is its position before the last tick and `alpha` is the unticked time over the tick. This is the
usual render interpolation: the drawn player trails the game's by less than one tick. A jump of over 64 px
between ticks (a teleport, a reset, an undo) is drawn at once. While the arrow flies, the game is paused and
the player is drawn where it is.

`rc_frame.camera_x/y` follows the drawn player when animating, so the camera moves as smoothly as the
player. Godot's own camera smoothing still applies on top of it.

## 122.3 The region morph

**The field.** `region_field::field_of(result)` samples a region along rays from its apex `P_K`: the player,
or its image behind the planned surfaces. A region of a pencil of rays is a set of wedge cells. Each cell is
bounded by two member rays and by carrier pieces: a far one, and a near one when the region lies behind a
planned surface (Stage 113's cells; checked on every shipped level). Along each ray, a cell gives one or more
visible spans.

- **The rays.** 720 rays half a degree apart, plus each cell's two edges from just inside (1e-6 rad), so
  every corner of the region is a sample.
- **The ends.** Each span's two ends say what they lie on: a surface (its id, and the circle when the
  carrier is one), the domain's edge, or the apex.
- **The fill.** A quad joins the same span at neighbouring rays. The field's fill equals the region's fill
  within 0.5% of its area on every level tested, a region behind a mirror included.

**The morph** (`region_field::morph(a, b, e)`) runs over the union of both fields' rays. Along each ray, the
spans before and after pair up in order. A sequence alignment pairs as many as it can, preferring pairs whose
ends lie on the same surfaces. For each pair:

- **An end on the same surface before and after slides along that surface.** A straight surface's ends move
  along the line. A circle's ends move round the circle (the test checks that every such end, halfway, is on
  its circle).
- **An end on another surface** (a shadow's edge passing a corner) moves along the ray, from the old
  surface to the new one.
- **A span with no partner** grows out of its near end, or shrinks into it. Nothing appears in empty space.

**When nothing can match**, the old region collapses into the player and the new one grows out of it, as a
disk that sweeps between each mesh's nearest and farthest points from the player (by default 40% of the time
collapsing, 60% growing). The presenter does this in three cases:

- the apex jumped more than 64 px (a plan change that moves `P_K`);
- either region is not a pencil of rays (a pencil of circles after an inversion, of parallels);
- a morph from a collapse/grow in progress.

**The timeline.** A morph runs 100 ms with an ease-out (cubic). When a newer region arrives (the player keeps
walking, Stage 121's regions a few frames behind), the morph starts again from the field on screen, so the
shape never jumps. When the morph ends, the node is the session's exact region (the same calls and version).
The whiskers (1D visible sets) show as the target's.

**The cost.** A new region's field costs 0.16–0.29 ms and a morph frame 0.26–0.6 ms (fill included), on
main, crossings, hit_effects and inversion_grid. That is on the frame thread, once per new region and once per
animated frame.

## The C API (1.3)

| Field | Meaning |
|---|---|
| `rc_game_config.animate` | 1: the draw list is animated (`rcg::Presenter`); 0 (the default, and an older host's config): the game's state as it is |

With `animate`, `rc_frame.camera_x/y` follows the drawn player. The Godot host animates (its `animate`
property, on by default); its session-replay test turns it off.

## Tests

| Test | What it shows |
|---|---|
| `present: a region sampled along rays from its apex is the region` | the field's fill equals the region's (4 levels, and behind a planned mirror), its spans start on the mirror |
| `present: a morph starts on the old region, ends on the new, and its ends slide along the surfaces they share` | e = 0 and e = 1 are the two regions; halfway, every end on one circle before and after is on that circle (hit_effects) |
| `present: a span with no partner grows out of its near end` | an empty region to a full one: growing, monotone, exact at the end; and back |
| `present: a mesh clipped to a disk` | the collapse/grow's clip |
| `present: without animation the presenter's frame is the session's` | no durations: every node's key, version and position are the session's |
| `present: a region's change morphs for its duration, then shows the region` | a walk morphs then lands on the exact node; a plan change collapses then grows; switched off, no frame has an empty mesh (Godot refuses one: caught by its suite) |
| `present: the player is drawn between its last two physics ticks` | half a tick: halfway |
| `test_an_animated_game_morphs_its_region_then_draws_the_game` (Python host) | through the C API: the region morphs after a plan click, then equals the unanimated game's |

## Findings

| # | Finding | Status |
|---|---|---|
| F8 | A region and the preview arrive later than their input and replace what was shown at once | The region: resolved here (the morph). The path: Stage 123 |
| F9 | A pencil of circles (after an inversion) or of parallels is not sampled along rays: its changes collapse and grow instead of morphing | Open: sampling along the pencil's own curves would morph them too |
