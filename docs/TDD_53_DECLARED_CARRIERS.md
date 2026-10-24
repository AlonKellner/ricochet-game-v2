# TDD 53: Declared carriers (a proposal: Stage E2 of the editor program)

> **Status: a proposal awaiting the user's approval.** It changes the spec (GAME_SPEC §8.4, §9.1, §19.1) and the
> solver's input, so no code is written before the approval. Nothing here changes an existing level.

## The need

You asked to edit "segments, their carriers, the defining points of the carriers, and the segment endpoints".
Today a surface is three points, and its carrier is derived from them. Two segments share a carrier only when
each one's points lie exactly on the other's circle (or line), which float32 points almost never do. Some
puzzles want several segments on one line or circle: a door in a wall, a broken arc, mirrors on one circle. They
also want them to stay on it when the carrier moves.

## The proposal

**The format.** `rc-level/2` gains
`carriers: [{"name", "line": {"a": [x, y], "b": [x, y]}} | {"name", "circle": {"center": [x, y], "radius": r}}]`,
and an element may say `"on": "<carrier>"` with `from`, `to` (and `via` on a circle) instead of `start`/`end`/`via`.

**The rule (GAME_SPEC §8.4).** A segment on a declared carrier lies on it exactly, by provenance. Its endpoints
and via are parameters that the editor snaps onto the carrier, and the solver uses them for the segment's extent
(the on-segment test, the orientation, the end tangents). The carrier, which every hit point is computed on, is
the declared one: the exact line through `a` and `b`, or the circle of `center` and `radius`. It is the same at
every precision tier.

**The solver's input.** `rc::SurfaceDef` and `geom::SegDef` gain a declared-carrier form: `Line2` (two points) or
`Circle` (center and radius, an arc's ends as parameters). `seg_from_def` builds the carrier from it, and
`same_carrier` compares declarations, so one name is one carrier. The three-point and `straight` forms are
unchanged, so every existing dump, golden and session stays byte-identical. The dumps gain a `carrier` field
only for surfaces on a declared carrier.

**The oracle first.** `trace2.py surface_from_json` and `level_json.py` mirror the declaration. New fixtures:
- a wall and a door on one line;
- two arcs on one circle, sharing their crossings;
- an endpoint snapped to a carrier.

The differential tests pin them, and a sweep manifest group covers them.

**The editor.** Carrier tools (a line by two points, a circle by center and radius), elements placed on a
hovered carrier, and dragging a carrier's defining point re-projects every segment on it. A crossing of two
carriers can be snapped to, but approximately. An exact crossing point would be a later form,
`{"cross": ["c1", "c2"]}`.

## What approval decides

1. The rule above as GAME_SPEC §8.4's: endpoints as parameters, the carrier declared.
2. Lines first and circles second, in one stage.
3. Whether the exact crossing-point form joins now or later.
