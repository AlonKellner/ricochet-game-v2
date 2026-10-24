# TDD 53: Carriers (Stage 149)

You asked to edit "segments, their carriers, the defining points of the carriers, and the segment endpoints".
Before this stage a surface was three points and its carrier was derived from them. Nothing let several segments
share a line or a circle, or move together when it moves.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 149.0 | The proposal (a solver-level declared carrier) checked against the core: unsound as proposed, see "The exact carrier" | Done |
| 149.1 | The format: `carriers` (lines, circles); elements `carrier`, `from`, `to` (`via`), lowered by projection | Done |
| 149.2 | The editor: the carrier tools (K, Shift+K), snapping onto carriers, elements placed on them and their handles, carriers as items (selected, their defining points dragged, renamed, deleted freeing their elements) | Done |
| 149.3 | Tests, docs, `make gate` | Done |
| 149.4 | The user's interactive sign-off; the exact carrier, if wanted | Done (the user's sign-off 2026-10-24); the exact carrier not requested (deferred) |

## What carriers are

A carrier is an authoring construct. A level names lines (two points) and circles (center, radius). An element on
one gives `from` and `to` (and on a circle optionally `via`, default the shorter arc; with neither `from` nor `to`
it is the whole circle). The lowering projects these onto the carrier and writes them as the element's own
start, end and via. A line element is `straight`, the exact line between its ends.

What this gives:
- **Shared points stay shared by provenance.** Two elements meeting at a point give the same `from`/`to` value,
  which projects to the same start/end. A door between two walls on one line meets them where the solver decides
  shared points by identity (Stage 123, Stage 138).
- **Editing the carrier moves its elements.** Dragging a carrier's point re-projects every element on it, the
  editing you described.
- **No solver change.** The solver sees ordinary three-point surfaces. Every older dump, record and lowering is
  byte-identical, and no oracle change was needed.

What it does not give: to the solver, two elements on one carrier are two lines or circles, each through its own
float32 points, equal to the declared one only to rounding. Two arcs on one declared circle are not exactly one
circle (Stage 109's exact same-carrier grouping does not join them). Adjacent arcs meet only at their shared,
exact endpoint, so no crossing appears between them. Overlapping arcs on one circle would be an ill-conditioned
pair, which no level needs.

## The exact carrier (analysed, deferred)

The proposal kept each segment's endpoints as parameters off the declared carrier, and computed hits on the
declared carrier. The core decides corners and shared endpoints by identity: an endpoint is a known point of
every surface it ends. An endpoint even a rounding off its carrier breaks that. A beam through the shared point
of a wall and a door on one carrier crosses the carrier at a point that is neither surface's endpoint, and the
on-segment tests at it go either way. Making the carrier exact therefore means representing endpoints
symbolically on it: a point as the carrier's parameter, computed exactly at every precision tier. That change
reaches the solver's inputs, the oracle, the pieces (`rcg/geom`), the player's controller, the drawing and the
dumps. It is worth doing only if a puzzle needs one exact circle across several arcs. Say so, and it becomes its
own stage with the oracle first.

## Tests

- `rcg_tests` "rc-level/2: carriers -- elements on a line or a circle, projected onto it (Stage 149)": the
  projections; a shared point is one value; `straight` lines; the default via on the shorter arc; an explicit via;
  a whole circle; the level builds; the errors (an unknown carrier, own geometry with a carrier, from and to on
  one point, a degenerate line, a duplicate name).
- "editor: carriers -- placed, built on, dragged, renamed, deleted (Stage 149)": K places a line carrier, two
  line clicks on it place a wall on it, dragging the carrier's point tilts the wall, a rename carries it, a
  delete frees it in place, Shift+K places a circle, three arc clicks place a mirror on it.
