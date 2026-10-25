# Ricochet Game v2 — Complete Game Specification

This is the single source of truth for the ricochet game: design, principles, architecture, data structures, algorithms, game loop, player interaction, level editing, implementation phasing, and testing strategy. It is self-contained — no other document is required to implement the full game.

Engine: **engine-agnostic** (Stage 119): the game is native C++20 (`native/core`, `native/game`) behind a C API (`native/capi/include/rc_api.h`); an engine hosts it (docs/PORTING.md). The shipped host is **Godot 4.7+** (`engines/godot`: one GDExtension node, `native/hosts/godot`). IDE: **VSCode**.

> **At a glance:** A 2D puzzle game where the player plans ricochet shots through mirrors, portals, and lenses. The core loop: aim → plan a sequence of surface bounces → fire → watch the arrow follow (or diverge from) the plan. The engine uses Möbius geometry to unify lines and circles. The hardest implementation areas are: the planning algorithm (§13), the visibility system (§15), and the precision beam solver with its exactness rules (§6, §11–12, §31). Start with Phase 0 (§28).

### Implementation Status

| Feature | Status |
|---------|--------|
| Math layer (generalized circles, segments, effect maps) | Done |
| Precision beam solver — `BeamSolver` rule walker, `FastSolver` float64 twin, `PrecisionLadder` dynamic precision, `ErrorEstimate` (§6, §11–12; TDD_09 Stages 80–83) | Done |
| Exactness rules — exact predicates and provenance (known points, exact lines), no epsilons (§31.3) | Done |
| Legacy frame-based solver (`Tracer`, `Planner`, `TransformCache`, `MobiusTransform`, normalized frames) | Removed (Stage 84) |
| Reflection effect | Done |
| Terminal (block) effect | Done |
| Plan construction and removal | Done |
| Planned trace — anchored image chain inside the beam solver (§13) | Done |
| Step tree merge and divergence | Done |
| Circle inversion effect (full and partial arcs) | Done |
| Rigid motion effect (portal pairs, Stage 82) | Done |
| Projective effects — physical tracing (Stage 83) | Done |
| Projective effects — planning through them (§13.3/13.4, Stage 87) | Done |
| Arrow flight with freeze/animation/skip | Done |
| Camera tracking during arrow flight (§4.5) | Done |
| Game systems: checkpoint, undo, reset (§3.3) | Done |
| Effect.Kind enum + TransformativeEffect base class | Done |
| Full segments / unbounded tracing; wrap through infinity | Done |
| Provenance (transform) stack with pop on re-hit | Done |
| No surface filtering principle | Done |
| Invariant sweep testing (event-based checks, all scenes) | Done: every level swept; plans on every planned scene incl. infinity plans (Stage 103); one known finding (K1, TDD_10) |
| Compound transformative effect | Planned |
| Visibility polygon (§15) | Done (Stage 110; projective plans Stage 113) |
| State flags — ConfigResolver + StateChange (§9.4, §10.6; Stage 85) | Done |
| Ghost mode & toggler surfaces (Stage 86) | Done |
| Hit effects as a second axis: any number per side (state change, target, spawn, reset), presence (§9.1, §9.6, §10.6; TDD_11 Stage 107) | Done (user sign-off 2026-10-16) |
| Crossing surfaces: design surfaces split into pieces at crossings; spawned surfaces trimmed (§9.7, §10.6.3; TDD_12 Stage 108) | Done (user sign-off 2026-10-16) |
| Plannable infinity — the ∞ plan entry, the `infinity` event (§4.2, §6.3, §12.5, §13.1; TDD_10 Stages 88, 101) | Done (B9 approved 2026-10-15): the frame's infinity `Q`, the edge or an on-screen point, plannable; the pre-transform (§9.5) |
| Engine-agnostic port: the levels as data (`rc-level/1`, §19.1), the session replay golden, the headless host `rc_tool` (docs/TDD_20, Stage 115) | Done |
| Engine-agnostic port: the game model and session natively (`rcg::Session`, byte-identical to the GDScript game on every recorded session), the sweep manifests without Godot (docs/TDD_21, Stage 116) | Done |
| Engine-agnostic port: the native player controller (§25.1; docs/TDD_22, Stage 117) replacing Godot's physics; every session, walking included, replays natively | Done (the drawing and the C API follow) |
| Level format `rc-level/2`: levels as elements (data templates, `data/elements`), initial flags, the lowering to `rc-level/1`, `rc_tool migrate/lower` (§19.1; docs/TDD_47, Stage 142) | Done |
| Moving surfaces: elements keyed over the level's cycle (offset and rotation), the cycle running on its own, set by the wheel, run again by a shot or idle seconds (§9.8; docs/TDD_56, Stage 151) | Done |
| Fire, charge and frost: the arrow's states and the tags `flammable`, `conductive`, `frosted`, `burning`, `charged`; conditional state changes and the frost rule in the solver and the oracle; touching rules opt-in (§10.6; docs/TDD_61, Stage 156) | Done |
| Exact carriers: a carrier's elements on float32 points exactly on its line or circle (`exact: true`; lattice points; no solver change) (§19.1; docs/TDD_59, Stage 154) | Done |
| Light and shadow: dark and light regions bounded by elements (sides of lines, insides or outsides of circles, under flags); the arrow stops entering the shadow, surfaces wholly in it are passes (§9.9; docs/ELEMENTS.md, docs/TDD_58, Stage 153) | Done |
| Spreading: tags on elements and objects, the level's spread rules (fire kept for good; charge recomputed from powered sources through what touches and is present); conductor, battery and charged door templates (§10.6; docs/ELEMENTS.md, docs/TDD_57, Stage 152) | Done |
| Bubbles: objects with their own gravity (floating up), popped by the arrow, carrying an object until they pop (§25.5; docs/TDD_55, Stage 150) | Done |
| Carriers: lines and circles elements are placed on (`carrier`, `from`, `to`), an authoring construct the lowering projects onto (§19.1; docs/TDD_53, Stage 149) | Done |
| The novel-element kit: per-shot flags (`shot:*`, erased at landing) and templates for breakable, toggle, frost, fire and charge elements (§10.6; docs/ELEMENTS.md, docs/TDD_52, Stage 147) | Done |
| Objects, the pull and the grapple: circle bodies (pullable ones fall under gravity); a pull brings an object back along the arrow's path, a grapple takes the player along it, either stopped by what blocks the traveller (§10.6, §25.5-25.6; docs/TDD_51, Stage 146) | Done |
| The goal and detectors: a level's goal region gated by flag conditions, won when the player stands in it; detectors writing flags as the player moves; a level without a goal won when every target is hit (§3.2, §24.5-24.6; docs/TDD_50, Stage 145) | Done |
| Level editor: native (`rcg::Editor`, the C API's `rc_editor_*`), its tools, panel, validation and play-test; the Python host (tkinter) and the Godot host (`RicochetEditor`) (§19.2; docs/EDITOR.md, docs/TDD_48-49, Stages 143-144) | Done |
| Menus, HUD, save system | Planned |

*Historical:* versions v0.7.0–v1.6.0 of the legacy `Tracer` (three-tier endpoint detection, boundless tracer, Hermitian carrier transform, DRY refactor) were superseded by the beam solver and deleted in Stage 84. See `docs/TDD_09_PRECISION_INTEGRATION.md`.

---

# Part I: Game Design

## 1. Game overview

The player is an archer in a 2D puzzle space. They **choose a sequence of reflective surfaces** to bounce off (the *plan*), **aim** at a target, and **shoot**. The arrow travels along straight lines and circular arcs, bouncing off surfaces according to their configured effects. The puzzle is figuring out *which* bounces reach the goal — not twitch aiming.

A **trajectory preview** updates in real time as the player aims. That preview must be trustworthy: if the preview says the shot works, the arrow must follow the same rules when fired. The preview and the physical arrow use the same geometry engine, the same effects, and the same game state.

**The player fantasy:** composing elaborate trick shots through a chain of mirrors, portals, and lenses. The satisfaction is the "aha" moment when the right sequence clicks, not dexterity.

> **Reading note:** this document uses forward references — some terms appear before their formal definition. §32 (Terminology) provides a complete glossary. Sections 5–17 are dense mathematical framework. Here's a quick mapping to player-visible effects:
> - **Beam** → the arrow's current path, a line or circle in screen space; an effect maps it to a new beam in closed form (this is how the arrow curves after an inversion).
> - **Image chain** → how the preview computes where the arrow would go if the plan works.
> - **Provenance stack / frame id** → the record of which transformative effects are currently "applied"; two traces with the same frame id at a step have applied the same effects.
> - **Generalized circle** → the unified representation for both straight-line and curved surfaces.

---

## 2. Core principles

These 25 rules are non-negotiable. Every system in the game must satisfy them.

1. **Planning is the puzzle.** The player commits to a sequence of ricochets; the game validates and shows the outcome. Preview exists so planning is trustworthy, not so aim skill dominates.

2. **One truth, many views.** There is one physical story of what happens on a shot. Preview, arrow flight, and "did the plan work?" are different readings of that story — not separate simulations compared after the fact.

3. **Geometry and behavior stay separate.** Shapes and positions are facts; what happens on contact (reflect, stop, break, pass through) is policy. Policy can change with game state without rewriting geometry.

4. **Side and phase matter.** The same edge can behave differently depending on which side was hit and what mode the shot is in (ideal plan vs physical reality). Behavior is keyed to context, not inferred ad hoc.

5. **Provenance over re-guessing.** When the system decides something once (order along a ray, which side, whether a point blocks), that decision is remembered and reused. Recomputing from coordinates alone is how subtle bugs appear.

6. **Stable math, stable outcomes.** Directions and ordering are defined so they do not drift with floating-point noise. Same inputs produce the same outputs.

7. **No sacred boundaries.** Room edges and obstacles follow the same rules. Special cases for "the screen" multiply code paths and break invariants.

8. **Earliest obstruction wins.** Along a ray, the first meaningful contact drives what happens next. Competing hits need a clear, deterministic rule — not tolerance bands or distance fallbacks for collinear cases.

9. **Effects are a small contract.** Every contact type must answer the same questions: what changes in the world, what happens to the ray next, how planning mirrors reality backward, and how to resolve an image back to its source. That unifies reflection, portals, breaks, and future gimmicks.

10. **State changes are explicit.** When a hit changes the world (e.g. a surface breaks), that change happens in one place and everything downstream — later hits, visibility, planning — sees the new state unless a deliberate snapshot says otherwise.

11. **Local direction at the hit.** What the ray does at a surface depends on how it arrived there, not on how the player first aimed. Multi-bounce shots fail when initial direction is reused at every bounce.

12. **Plan vs physical can diverge once.** Ideal paths may use extended geometry; physical paths use what is actually there. When they split, that split is a first-class event (one branch ends, others begin) — not a rendering trick.

13. **Plan validity is visual.** The preview shows which planned entries will be reached and which won't — the player builds any plan they like and sees the consequences in real time.

14. **Visibility shares the world.** What you can see or validly aim at uses the same scene and state as trajectory. Two different worlds for light and arrows guarantees inconsistency.

15. **Branches are explicit.** When outcomes fork (stop vs continue, physical vs planned continuation), the fork is recorded structurally — not only as different colors on screen.

16. **Simultaneous play.** Movement and aiming serve different goals (position vs plan). Neither should lock the other; the fantasy is composing a shot while moving.

17. **Determinism is a feature.** Trick-shot games need reproducible previews and fair debugging. Randomness and hidden state belong only where deliberately designed.

18. **Extend by adding policies, not patching paths.** New surfaces, shapes, and phases plug into the same hit → effect → next-step loop. The core loop stays; behavior varies by registration.

19. **Invariants over examples.** Correctness is "these properties always hold," tested across many positions — not a handful of hand-tuned scenes.

20. **Requirements before patches.** Stated behavior should be identifiable and testable so design and implementation anchor to agreed rules instead of accumulating one-off fixes.

21. **Derive, don't duplicate.** When one representation fully determines another, store only one and derive the rest on demand. Redundant stored values drift apart under floating-point arithmetic and create contradictions between subsystems.

22. **Exact reversal by construction.** Every reversible computation must round-trip exactly — `f(f⁻¹(x)) == x`, not `≈ x`. This is achieved by caching both directions on first evaluation; reversal returns the stored original, never a recomputed approximation.

23. **Designer intent over arithmetic.** When floating-point cannot guarantee an algebraic identity the level design depends on (e.g., a cycle of reflections = identity), the designer declares the intended result. The system uses the declaration, validates that computation is close, and flags discrepancies. The designer's intent is the truth, not the accumulated arithmetic.

24. **Compute in the simplest frame.** Transform the problem into a coordinate system where the math has the fewest cases and best conditioning, compute there, and transform results back. Pay the cost of frame management to eliminate the cost of complex algorithms.

25. **Simulation is instantaneous; presentation is animated.** The physical outcome of a shot is computed atomically and completely before any visual animation begins. Game time freezes — the simulation sees an instant event, the player sees a step-by-step replay. No gameplay logic depends on the visual timeline, and no visual state feeds back into simulation.

---

## 3. Puzzle structure and game loop

### 3.1 What is a puzzle

A **puzzle** is a level: a scene containing surfaces, a player spawn point, and one or more **target surfaces**. The player's goal is to hit every target surface. This may require **multiple shots** — a single shot might clear an obstruction, change game state, or reposition the player so that a subsequent shot can reach the remaining targets.

### 3.2 Win and fail conditions

| Condition | Trigger |
|-----------|---------|
| **Win** | A level with a **goal** (§24.6, Stage 145): the player stands in the goal region while the goal's conditions hold. A level without one: all target surfaces in the level have been hit (across one or more shots). The win is latched: it holds until an undo or a reset takes the game back before it. |
| **No failure state** | There is no intended failure. As long as the player has not won, they can continue making shot attempts without limit. A shot that misses all targets is not "failure" — it is simply an attempt. The player can always try again. |
| **Exception** | A specific puzzle may explicitly design a failure condition (e.g., a breakable surface that cannot be restored). This must be opt-in per level, not a default engine behavior. |

### 3.3 Checkpoints

The game maintains **automatic checkpoints** — snapshots of the scene state taken just before each shot. The player can revert to a previous checkpoint at any time:

- **Undo last shot**: reverts to just before the most recent shot. All of the following are restored: player position, game state flags (full copy), current plan, and the set of targets hit so far.
- **Full reset**: reverts to the level's initial state (all surfaces restored, player at spawn point, checkpoint stack cleared).
- **Checkpoint stack**: checkpoints accumulate per shot. The player can step back through them one at a time.

```
CheckpointData:
    player_position: Vector2
    player_velocity: Vector2
    game_state:      GameState    # deep copy of all flags
    plan:            Array[{surface_id: int, side: Side}]
    targets_hit:     Set[int]    # surface IDs of targets hit so far
```

The game manager tracks `targets_hit: Set[int]` — the set of target surface IDs contacted across all shots in the current attempt. This set is part of the checkpoint and is restored on undo.

### 3.4 Game loop

```
Main Menu
  └─ Level Select
       └─ Level Loaded
            ├─ PLAY PHASE (repeating):
            │   ├─ Player moves and positions themselves
            │   ├─ Player constructs/modifies the plan (adds/removes surfaces)
            │   ├─ Player aims (cursor position updates preview continuously)
            │   ├─ Player fires
            │   │   ├─ Checkpoint saved (pre-shot state)
            │   │   ├─ Game time FREEZES
            │   │   ├─ Physical trace computed instantly
            │   │   ├─ Arrow flight animated step-by-step (visual only)
            │   │   ├─ State changes promoted (game state updated)
            │   │   ├─ Plan is RETAINED (the same list of {surface_id, side} entries, which may now resolve to different effects if the game state changed)
            │   │   ├─ Game time UNFREEZES
            │   │   ├─ Evaluate: have all targets been hit (across all shots)?
            │   │   │   ├─ YES → Level Complete → return to Level Select (or next level)
            │   │   │   └─ NO  → remain in play phase, player can continue
            │   ├─ Player can clear the plan (without resetting level or respawning)
            │   ├─ Player can undo last shot (revert to previous checkpoint)
            │   └─ Player can full-reset (revert to initial level state)
            └─ Pause Menu → Resume / Quit to Level Select
```

**On level load:** the player sees surfaces drawn in their effect colors, the player at the spawn point, the cursor tracking the mouse. The visibility region is shown immediately (with an empty plan, this is the standard visibility polygon from the player). The preview updates as soon as the cursor moves.

The initial cursor is at the mouse position in world coords. If the mouse hasn't moved, the cursor defaults to the player's spawn position. If cursor == player position, no preview is drawn (§8.3).

A brief level name overlay (1-2 seconds, fading) is displayed on entry. No transition animation — the level appears immediately.

### 3.5 Mechanic introduction

Levels should introduce mechanics progressively. The spec does not prescribe a fixed progression order, but the natural teaching sequence is:

1. **Reflection only** — straight-line bounces off mirrors. The core mechanic.
2. **Block surfaces** — walls the arrow cannot pass through. Forces routing.
3. **Rigid motion** — portals/teleports that shift and rotate the arrow.
4. **Circle inversion** — arcs appear in the visual path. The arrow curves.
5. **Projective effects** — surfaces that normalize direction regardless of incoming angle.
6. **Mixed chains** — plans combining transformative and projective surfaces.
7. **State-changing surfaces** — surfaces that break, open, or toggle on hit.

### 3.6 Progression structure

Left open. The spec defines what a level is and how it loads (§19), but does not prescribe level unlock order, world/chapter grouping, or scoring. Those are content design decisions.

---

## 4. Player interaction and UX

### 4.1 Input model

Movement and aiming are independent and simultaneous. *(Principle 16.)*

| Action | Input | Behavior |
|--------|-------|----------|
| **Move (zero gravity)** | WASD / left stick | 4-directional movement. W = up, S = down, A = left, D = right. |
| **Move (with gravity)** | AD / left stick horizontal | Horizontal movement only. |
| **Aim** | Mouse position / right stick | Updates cursor point continuously. Preview updates every frame. |
| **Add to plan** | Left-click on a surface | Appends the clicked surface to the plan. |
| **Remove / Clear plan** | Right-click | If cursor is over a **planned surface**: removes the **latest instance** of that surface from the plan. If cursor is over an unplanned surface or empty space: **clears the entire plan**. |
| **Fire** | Spacebar / trigger | Locks the plan, freezes game time, runs physical trace, animates arrow. No-op if cursor is at the player position (zero-length Direction — see §8.3). The player can **skip** the animation by pressing any key during flight. |
| **Clear plan** | C / dedicated button | Clears the plan without resetting the level or respawning the player. |
| **Undo last shot** | Z / dedicated button | Reverts to the checkpoint saved before the most recent shot. |
| **Full reset** | R / dedicated button | Reverts to the level's initial state (all surfaces restored, player at spawn). |
| **Jump (with gravity)** | W / Up / left-stick-up | Standard platformer jump. No effect when gravity is zero. |
| **Pause** | Escape / start | Opens pause menu. |

**Cursor coordinate space:** the cursor position is in **world space**, not screen space. The visual layer converts mouse screen position to world position using the camera's inverse transform (`get_global_mouse_position()` in Godot). The initial ray (§12.1) uses the world-space cursor directly.

### 4.2 Plan construction

The player builds a plan by clicking on **surface sides** in order. The plan is an **ordered list of `{surface, side}` entries** — each entry identifies a specific surface and which side (left or right) the arrow should approach from. Both sides are available as options unless a side's `interactive` flag is `false` (default for null/pass-through and terminal/block sides; configurable per side in the level editor). Non-interactive sides cannot be clicked.

- **Click detection**: surfaces are clickable within a tolerance band of **8 pixels** from the rendered line/arc. When multiple surfaces are within tolerance, the nearest one wins. For arc segments, the nearest point on the arc is found by projecting the cursor onto the carrier circle (nearest point on circle), then clamping to the arc's bounds using the same cross-product containment test as §11.1. This is a UI-layer concern, not math-layer. When multiple surfaces coincide geometrically within the tolerance band, repeated clicks cycle through them by surface ID order.
- **Side selection**: when the player clicks near a surface, the side is determined by which side of the surface the cursor is on (left or right of the traversal direction). The selected side is locked into the plan entry. When the cursor is exactly on the surface carrier (`f(P) = 0`), the side is determined by the cursor's previous position (the last non-zero side evaluation). If no previous position exists, default to LEFT.
- **Click position**: the plan entry also remembers *where* on the surface the player clicked. It settles the one kind of fork in planning. A circle projective (§13.3) can have two hit points whose projected beams both pass through the target: the ones **on the segment** are preferred, and among what remains the one nearer the click is taken; with none on the segment, the nearer off-segment one (a planned-only firing, §13.3). So every plan has exactly one path, and an arc under a semicircle (it holds at most one of two antipodes) never depends on the click. Without a click, the segment's midpoint stands in. (Stage 114; before it, the click alone decided.) For a circle directional projection the two hit points are the ends of the chord along its normal, and the click picks the same end for every target exactly when it is off the diameter across the normal: a click within the UI tolerance (8 px) of that diameter is moved off it, to the side of the segment's midpoint (`PlanManager.snap_click`; B7, Stage 113), so no plan rests on float32 noise.
- **Highlighting**: when the cursor hovers over a surface, the approachable side highlights to indicate it is clickable.
- **Plan display**: planned surface sides are visually marked (e.g., numbered overlay, distinct color on the planned side).
- **Preview**: the trajectory preview updates in real time as the plan and cursor change.
- **Infinity entry** (Stages 88, 101): the entry **∞** (`INFINITY_ID = -3`, no side) means "the beam passes the frame's infinity here"; it is matched by the walker at that passage, and selects the plan's second realization only where the first entry leaves both open (§13.1, Stage 111). *Where* infinity is depends on the frame the plan has reached (§13.1: `Q = F(∞)`, the image of infinity under the plan's effects on the provenance stack, starting from the level's pre-transform, §9.5). When `Q` is the real infinity or lies off screen, clicking within the click tolerance **inside the level bounds' edge**, where no interactive surface wins, appends the entry; hovering the edge band highlights it and planned ∞ entries are marked on the edge where they were clicked. When `Q` is a point on screen (after an inversion, or in a level with a pre-transform), it is drawn as a **circled cross** and is plannable like any surface: the nearest within tolerance wins, planned entries are numbered at it (each at the point of its own frame). Hover and plan overlays of infinity use the surfaces' language (§4.3): a gradient band fading away from it and incoming / outgoing chevrons, one pair per side of the level bounds inside the edge, four radial pairs around the point. The player faces the preview's first step, so it faces the way the arrow will leave (toward or away from the cursor, along the frame's circles). While the plan's simulated state is ghosted the entry is still allowed: a ghost arrow wraps.
- **Origin entry** (Stage 132, the user): the entry **O** (`ORIGIN_ID = -4`, no side) means "the beam passes the ray's origin here" (§12.6): the player's image in the plan's frame, a circle normal projection's centre, or a line normal / directional projection's departure. It is **plannable only when the plan so far leaves the walk behind the ray's origin** -- heading toward the origin rather than toward the frame's infinity. Along the beam the origin and the infinity point split the ray's line (or circle) in two halves, and a walk starts **in front** (it leaves the player). A planned ∞ entry turns it **behind**, a planned origin entry **in front** again; a map (a reflection, an inversion, a portal) keeps the order of the beam's points and so the half; a projection starts a new ray: in front, except a circle normal projection whose ray heads **toward the centre** (behind: an inner projection -- through from outside, or back from inside). The rule is the plan's, not the cursor's: it is decided by the plan's entries alone, and the walk then shows whether the entry is met (an unmet entry diverges, §13.5). Where the origin is plannable it is drawn as a **circled cross** at its point and is plannable like any surface: the nearest within the click tolerance wins; planned origin entries are numbered at their points; hover and plan overlays as ∞'s on-screen point (four radial pairs of chevrons). Right-clicking it removes the latest origin entry.
- **Right-click removal**: right-clicking over a planned surface removes the **latest instance** of that surface from the plan (right-clicking the edge band removes the latest ∞ entry). Right-clicking over an unplanned surface or empty space **clears the entire plan**.
- **Clear plan**: dedicated input (C) clears the plan without resetting level state or player position.
- **Duplicate entries allowed**: a surface side may appear in the plan **multiple times**, including **consecutively** (e.g., plan a circle-inversion surface's left side twice to invert then un-invert). The player is free to construct any plan; the preview shows which entries are active.

Plan entries reference surfaces by **surface ID** (§9.3), not by object reference. When the game state changes a surface's behavior, the plan entry's ID still resolves to the current (possibly modified) surface.

### 4.3 Preview visualization

The preview draws the step tree (§14). Each step has one of five types, each with a distinct visual:

| Step type | Color | Style | Meaning |
|-----------|-------|-------|---------|
| **ALIGNED** | Green | Solid | Planned and physical paths agree. |
| **ALIGNED_POST_PLANNED** | Green | Dashed | Still aligned, but past the cursor (post-planned continuation). |
| **DIVERGED_PHYSICAL** | Yellow | Dashed | Where the physical arrow actually goes after divergence. |
| **DIVERGED_PLANNED** | Red | Solid | Where the plan says the arrow should go, but the physical arrow diverged. |
| **DIVERGED_POST_PLANNED** | Red | Dashed | Post-cursor continuation of the planned path, after divergence. |

**The look** (Stage 130, the user, 2026-10-22):
- A **solid** step is a slightly dark stroke (green `#00B300`, red `#B30000`) with a slightly bright dashed stroke over it (`#80FF80`, `#FF8080`).
- **Dashes run** along the way the arrow flies (start to end of each step), about 30 px per second. They are presentation only: a replay draws them still. The pattern is the walk's, not each step's (Stage 136, the user): it stands at the distance from the player along the walk, through every hit point, so the dashes run on from one step to the next; each branch past a divergence goes on from the common part's length.
- **Event marks** (Stage 131, the user): where a step ends at a **significant event** -- an applied effect, a firing state change, a passage of the frame's infinity, a passage of the ray's origin (§12.6: a shot makes at most ten) -- a ringed dot in the step's colour (white ring, radius 5.5; the dot 4); where it ends at any other crossing (a candidate's crossing that fires nothing: on its segment with a side that passes, outside a planned piece, a plan surface's off its segment) a small dot at half opacity (radius 2), for debugging, coloured by the walk that computed it: green where both did, yellow the physical walk alone, red the planned walk alone (Stage 134, the user). A crossing off its segment on a surface no remaining plan entry names is no event and has no dot (Stage 139, the user, §12.1). A passage of an infinity point off the level -- the real infinity of a straight beam, or a circle beam's `Q` outside the level's bounds -- is marked twice: where the path last leaves the level's bounds before it (the step that reaches it often lies off the level already) and where it first enters them again after it; each is half a ring, normal to the level's edge and facing into the level, and the two halves make the one mark (Stage 132, the user: on screen). A **portal** is marked the same way: half a ring at its entry, facing back the side the beam came from, and half at its exit, facing the side it leaves by -- each along the portal surface's normal, not the beam's way (Stage 132, the user); on a circular portal not a half ring but the ring's part on that side of the portal's circle: its arc meets the circle at both ends, and the circle closes it (the user) -- one significant event. Other crossings of the bounds are not marks. Marks sit at their steps' ends as the steps are drawn, so the marks of steps drawn on one another (a loop's repeats) are side by side too (the user).
- **Steps drawn on one another are set side by side.** Two steps of the preview on one carrier (the same line, or the same circle, to half a pixel) that run together for more than 2 px are drawn on one another: the same step either way round, or steps with different ends (the planned path cut at the cursor while the physical one runs on to its next hit). Lanes go by **passes**, not steps (Stage 135, the user): a pass is a run of one walk's consecutive steps that go on from one another along one carrier, the same way round -- through the cursor, the crossings that fire nothing and the ray's origin -- with no step of it on another (a wrap through infinity, a turn onto another carrier, the way back, or a second lap starts a new pass). Passes drawn on one another form groups, transitively. In path order each pass takes the lowest lane no pass on it holds, so a group has as many lanes as its passes run together, and each pass keeps one lane along its whole length. The lanes are 3.5 px apart (a 1.5 px margin between 2 px strokes) and centred on the path; where both walks are diverged the planned walk's passes take the lanes on the left of the group's first step's way, the physical walk's those on the right. A line moves along its normal, and an arc moves to a larger or smaller radius on its circle. A whole pass moves, so each path stays joined along the carrier. Sections of different carriers that cross or lie close are drawn as they are, for now.

**Visual invariants:**
- A **solid path** always leads from the player to the cursor — this is the plan's intended route.
- A **dashed path** always shows post-cursor continuation (what happens after the planned endpoint).
- The **physical path** (what the arrow actually does) is always green or yellow — never red. Red shows only where the plan *would have* gone but the arrow didn't.

**Special states:**
- **No plan**: physical trace only from origin through cursor direction (all green/yellow).
- **Invalid plan**: path drawn up to the first invalid point. Remaining plan surfaces shown as inactive/dimmed.

### 4.4 Arrow flight animation

When the player fires:

1. **Game time freezes.** Physics objects and all gameplay timers stop. The shot is logically instant.
2. **Physical trace is computed** in full (all steps, all effects, all state changes).
3. **Arrow is animated** step-by-step along the traced path:
   - For line steps: the arrow tip lerps from `start` to `end`.
   - For arc steps (visual frame): the arrow tip follows the arc by interpolating the angle parameter.
   - At each hit point: a brief bounce event (visual flash, sound cue).
4. **After the last step**: evaluate win/fail.
5. **Game time unfreezes.** State changes from the shot (broken surfaces, opened gates) take effect.

### 4.5 Camera and viewport

- **Camera**: follows the player. During arrow flight, the camera smoothly tracks the arrow tip. If the path extends beyond the viewport, the camera scrolls to follow. After the shot completes, the camera smoothly returns to the player. Camera uses Godot's built-in smoothing (`Camera2D.position_smoothing_enabled = true`, `position_smoothing_speed = 5.0`). Camera is clamped to `LevelData.bounds` (§19.1).
- **Coordinate system**: Godot's default (Y-down, X-right). The math layer is coordinate-agnostic; the visual layer handles the Y-flip if needed.
- **Scale**: 1 unit = 1 pixel at default zoom. Levels define their own bounds.
- **Resolution**: the game targets a fixed viewport size (1920×1080). Fullscreen toggle available. Letterbox scaling at non-native aspect ratios. UI elements scale with the viewport.

---

# Part II: Mathematical Framework

## 5. Generalized circles and Möbius geometry

### 5.1 Generalized circles

Lines and circles are unified into a single family. On the Riemann sphere (the complex plane plus a point at infinity), a line is a circle that passes through ∞. Every Möbius transformation maps generalized circles to generalized circles.

Both are represented by four coefficients:

```
GeneralizedCircle:
    a: float    # coefficient of (x² + y²)
    b: float    # coefficient of x
    c: float    # coefficient of y
    d: float    # constant term
    # Equation: a(x² + y²) + bx + cy + d = 0
    # When a = 0: a line (bx + cy + d = 0)
    # When a ≠ 0: a circle, center = (-b/2a, -c/2a), radius = √((b²+c²-4ad)/(4a²))
```

There is **no LineShape vs ArcShape distinction** in the math layer. The generalized circle handles both cases. Internal dispatch on `a == 0` vs `a != 0` is an implementation detail.

`GeneralizedCircle` is a **derived** representation, not a primary one. Segments are defined by three points (start, end, via — see §8.4). The carrier is computed from the three points on first access and memoized on the segment (`Segment.get_carrier()`). The three points remain stored, so the via never has to be recovered from the carrier (§17). The beam solver does not use these float coefficients for decisions: it works from the segment's defining points, using exact predicates (§11, §31.3).

### 5.2 Möbius transformations

A Möbius transformation is a map of the extended complex plane:

```
z → (αz + β) / (γz + δ),    αδ − βγ ≠ 0
```

Mathematically it is a 2×2 complex matrix `[[α, β], [γ, δ]]` (up to scale), together with a flag saying whether the map conjugates its argument.

**Conformal vs anti-conformal:** Rigid motion is **conformal**: it maps `z → (αz + β) / (γz + δ)`. Reflection and circle inversion are **anti-conformal**: they map `z → (αz̄ + β) / (γz̄ + δ)`, where `z̄` is the complex conjugate.

| Operation | Math |
|-----------|-----|
| **Apply to a point** | `w = (α·z' + β) / (γ·z' + δ)` with `z' = z` (conformal) or `z' = z̄` (anti-conformal). |
| **Compose** | Depends on whether each operand conjugates (see the composition table below). |
| **Invert** | `M⁻¹ = [[δ, -β], [-γ, α]] / (αδ - βγ)`. An anti-conformal map has an anti-conformal inverse. |
| **Map a generalized circle** | Generalized circles map to generalized circles (Hermitian form `M⁻ᴴ · H · M⁻¹`, adjusted for conjugation). |
| **Identity** | `[[1,0],[0,1]]`, conformal. |

**Implementation note (Stage 84):** the game has no Möbius matrix class, no frame composition and no carrier transformation. The legacy `MobiusTransform`, `TrackedTransform`, `GeneralizedCircle.transformed_by` and `Effect.get_mobius()` were deleted with the frame-based solver. Each effect is implemented in the beam solver as a **closed-form map of the beam state** `(p, τ, κ)` at the hit (§6, §10.8). Its point map is used only inside the solver for the anchored planned path (§13). The matrices and the composition rules below are the mathematical model those maps realise.

**Composition table:** When composing `M₁ ∘ M₂` (apply M₂ first, then M₁):

| M₁ | M₂ | Matrix operation | Result |
|----|-----|-----------------|--------|
| conformal | conformal | `M₁ × M₂` | conformal |
| conformal | anti-conformal | `M₁ × M₂` | anti-conformal |
| anti-conformal | conformal | `M₁ × conj(M₂)` | anti-conformal |
| anti-conformal | anti-conformal | `M₁ × conj(M₂)` | conformal |

Where `conj(M)` means conjugating each complex entry of the matrix: `[[ᾱ, β̄], [γ̄, δ̄]]`.

The rule is: when the outer transform is anti-conformal, it conjugates everything downstream — so the inner matrix's entries must be conjugated before multiplication.

The three transformative effects — reflection, circle inversion, and rigid motion — are all Möbius transformations (conformal or anti-conformal). Composition follows the table above, making chaining correct for any combination of effects. The beam solver never multiplies these matrices. A chain of effects is applied one map at a time to the beam (forward). For the planned aim, it is applied to points and poles, with the difference `P − Q` carried by exact difference formulas (§13.1).

### 5.3 Complex number operations in GDScript

Complex numbers are represented as `Vector2(real, imag)`. Key operations:

```
# Complex multiply: (a + bi)(c + di) = (ac - bd) + (ad + bc)i
static func cmul(a: Vector2, b: Vector2) -> Vector2:
    return Vector2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x)

# Complex conjugate: conj(a + bi) = a - bi
static func cconj(a: Vector2) -> Vector2:
    return Vector2(a.x, -a.y)

# Complex divide: (a + bi) / (c + di)
static func cdiv(a: Vector2, b: Vector2) -> Vector2:
    var d = b.x*b.x + b.y*b.y
    return Vector2((a.x*b.x + a.y*b.y)/d, (a.y*b.x - a.x*b.y)/d)

# Complex modulus squared: |a + bi|² = a² + b²
static func cmod2(a: Vector2) -> float:
    return a.x*a.x + a.y*a.y
```

These four operations, plus `Vector2` addition and scalar multiplication, cover the float32 helpers of the game and visual layers. The beam solver uses its own precision-generic complex type `CN` (`scripts/math/precision/cn.gd`) over a number context (`NumCtx`: float64 `F64Num`, tangent-linear `DualNum`, or arbitrary-precision `BigFloat`). Every tier runs the same formulas.

---

## 6. The beam model (formerly: the normalized coordinate system)

> **Stage 84:** the legacy design computed everything in a per-step *normalized frame*, where rays were always lines and surfaces were mapped by the inverse of an accumulated Möbius frame `M`. That design was deleted together with `Tracer`, `MobiusTransform` and `TransformCache`. The sections below describe its replacement, the precision beam solver (`scripts/math/precision/`). Its specification is the mpmath oracle `docs/research/oracle/trace2.py`; see also `docs/TDD_09_PRECISION_INTEGRATION.md` ("Target architecture", "Exactness rules").

### 6.1 One space: the visual plane

All computation happens in **visual (world) space**, the plane the player sees. There is no second frame. The arrow's current path is a **beam**: an oriented generalized circle (a line or a circle), stored as a point `p`, a unit tangent `τ` and a signed curvature `κ` (`HPBeam`):

```
z(t) = p + t·τ / (1 − iκt/2)
```

`κ = 0` is a straight beam. Surfaces are intersected exactly where they are, in visual coordinates (§11). Every visual step is a piece of a beam, so it is drawn directly as a line or an arc (§7.3, §12.3).

### 6.2 Why this works

Every transformative effect maps a generalized circle to a generalized circle. Its action on the beam at the hit point `h` is therefore a **closed-form map of `(p, τ, κ)`** (table in §10.8). An inversion simply produces a curved beam, and beam–line and beam–circle intersections are both quadratic equations in `t`, so arcs need no special machinery. Nothing is mapped back to an origin frame. This avoids the numerical collapse that normalized coordinates suffer after deep inversion chains. When float64 is not enough, `PrecisionLadder` re-runs the same rules at higher precision (§30, §31.5).

### 6.3 Lines through infinity

The beam parameter is **projective**. From `t = 0` (at `p`) the beam runs forward through `t → +∞`, wraps to `t → −∞`, and returns towards `p`. A straight beam passes through the point at infinity. On a circle beam, `t = ∞` is the antipode of `p`, an ordinary finite point (an exact value in every precision tier). Distinct from the parameter's `t = ∞` is **the frame's infinity** `Q = F(∞)` (§13.1): the image of the point at infinity under the effects applied so far. A beam is the image of a straight line, so it passes `Q`: at `t = ∞` when `Q` is the real infinity (a straight beam in the identity frame), at a finite parameter otherwise (every circle beam; a straight beam through an inversion center). That passage is the `infinity` event (§12.5). Candidate hits are ordered **from the current parameter** in this cyclic order, so a hit "beyond infinity" is simply a later candidate (§11.3). In the visual plane, wrapping through ∞ on a straight beam means leaving the screen and returning from the opposite side (§12.5). The wrap of a straight beam is itself an event, `infinity` (Stage 88): ordered between the candidates ahead (`t > t0`) and the wrapped ones, counted toward the hit limit like a pass, with no point. A circle beam never emits it (its `t = ∞` is a finite point).

### 6.4 Per-step frames → frame identity

There are no per-step Möbius frames. Each step instead carries a **frame id** that identifies *which transformative effects are currently applied*. It is the id of `(epoch, provenance stack)` (§10.7, §14.8). Two steps with the same frame id are on beams produced by the same sequence of applied effects. Divergence detection compares frame ids (§14.2).

---

## 7. Dual representation system

### 7.1 Mathematical representation

The math layer uses **uniform types** with no LineRay/ArcRay or LineSegment/ArcSegment distinction:

| Primitive | Representation |
|-----------|---------------|
| Point | `Point` wrapping a `Vector2` (§8.1). The provenance of exact points lives in the beam solver (§31.4). |
| Direction | See §8.2 |
| Ray | See §8.3 |
| Segment | `{start: Vector2, end: Vector2, via: Vector2}` — three points fully define the segment including its carrier (see §8.4) |
| Generalized circle | `{a, b, c, d: float}` — **derived** from a segment's three points, not stored. Memoized on the segment (§17). |
| Beam (solver) | `HPBeam {p, τ, κ}` at the tier's precision (§6.1), or a raw float64 array in `FastSolver` |
| Arc chain | `Array[Segment]` (open) |
| Generalized polygon | `Array[Segment]` (closed) |

### 7.2 Visual representation

The visual layer uses Godot-native types for rendering:

| Primitive | Representation |
|-----------|---------------|
| Visual line segment | `{start: Vector2, end: Vector2}` — drawn with `draw_line()` |
| Visual arc segment | `{center: Vector2, radius: float, start_angle: float, end_angle: float, clockwise: bool}` — drawn with `draw_arc()` |
| Visual path | `Array[VisualLineSegment \| VisualArcSegment]` |

The type distinction exists **only** in the visual layer.

Note: `start_angle` and `end_angle` are needed at the Godot `draw_arc()` API boundary. Angles are computed from the segment's three points ONLY during visual conversion (§7.3). No other system stores or computes angles.

**draw_arc() conversion:** Godot's `draw_arc()` always draws CCW from start_angle to end_angle. When `VisualArcSegment.clockwise = true`, swap start_angle and end_angle before calling `draw_arc()`. The clockwise flag is determined during math→visual conversion using the cross-product sign of `(start - center) × (end - center)` relative to the via point's winding.

### 7.3 Conversion pipeline

There is no frame to apply: solver events are already in visual space.

1. `BeamTracer` turns each solver event (an arc of a beam from `t_from` to `t_to`) into one or more `TraceStep`s with visual `start`, `end` and a `via` on the beam between them (§12.3). A step that wraps through infinity on a straight beam is split into an outgoing and an incoming escape step.
2. `VisualConverter` derives the carrier from the step's three points.
3. If the carrier is a line, or a circle above `VisualConverter.MAX_ARC_RADIUS`, it produces a line. Otherwise it produces an arc.
4. Escape steps (`end` or `start` = `Vector2(INF, INF)`, with `via` holding the travel direction) are drawn to or from the viewport edge.

Conversion is one-way (math → visual). Nothing is converted back.

---

# Part III: Game Objects

## 8. Geometry primitives

### 8.1 Point

A 2D position in visual (world) coordinates.

```
Point:
    coords: Vector2
```

**Construction:** `Point.at(position)`. **Comparison:** `same_position(other)` checks `coords == other.coords` (exact equality).

**Provenance of exact points (Stage 84):** the legacy `Point` carried a transform history (`original`, `transforms: Array[TrackedTransform]`, `frame`) so that inverse pairs could cancel exactly. That machinery was removed with the frame-based solver (*historical:* `DESIGN_PROVENANCE_PRIMITIVES.md`). Exact round-trips are now guaranteed inside the beam solver:
- **known points:** exact data points a beam passes through, kept with their parameter;
- **exact lines:** straight beams kept in exact implicit form;
- **the provenance stack:** a pop restores the earlier beam *object*, not a recomputed one.

See §10.7 and §31.4.

### 8.2 Direction

An immutable two-point definition of a line's orientation.

```
Direction:
    start: Point
    end:   Point
    # Defines the line and its forward direction. Immutable once created.
    # Two-point representation: no stored angle, no unit vector.
```

In GDScript, Direction is a RefCounted (reference-counted, immutable) object. It is never mutated after creation.

### 8.3 Ray

```
Ray:
    origin:    Point
    direction: Direction
```

`Ray` is a game-facing value. The **aim ray** (player → cursor) is shared by reference by every step of one trajectory (`TraceStep.ray`). Escape steps carry a ray along their exact tangent. The solver itself does not trace `Ray`s: after the first effect the arrow's path is a beam (§6.1), and the beam's tangent changes at every transformative or projective hit.

**Degenerate case:** if the cursor is exactly at the player position, the Direction has zero length — the aim line is undefined. The system treats this as "no aim": no preview is drawn and fire is a no-op.

### 8.4 Segment

A segment is defined by **three points only**: `start`, `end`, and `via`. The `via` point is any point that lies on the intended path between start and end — it is the point the path passes through, not necessarily the geometric center.

```
Segment:
    start:    Vector2
    end:      Vector2
    via:      Vector2   # any point on the segment between start and end
    full:     bool      # true = unbounded (full circle or full line, no endpoints)
```

When `full = true`, the segment represents the entire carrier (a full circle or a full line) with no endpoint clipping. All intersection points on the carrier are valid hits regardless of arc containment. Full segments are created via `Segment.full_from_carrier()`.

These three points **fully determine the segment**, including its carrier:

| Three-point configuration | Carrier determined | Segment path |
|--------------------------|-------------------|-------------|
| **Collinear** (all three on a line) | The unique **line** through the points. | Finite segment from `start` to `end`. |
| **Via = INF** | The unique **line** through `start` and `end`. | The segment from `start` → ∞ → `end` — the "long way around" on the Riemann sphere. In GDScript: `via = Vector2(INF, INF)`. The carrier derivation function checks for this sentinel. |
| **Non-collinear** (three distinct non-collinear points) | The unique **circle** through all three. | The arc from `start` to `end` that passes through `via`. |

No explicit `carrier` field is stored. The carrier is a **derived value**, computed from the three points on first access (`Segment.derive_carrier(start, end, via)`) and memoized on the segment (`get_carrier()`).

**Carrier ↔ via:** the three points stay stored, so the exact original `via` is always available and never has to be recovered from the carrier. *(Principle 5.)* (*Historical:* the bidirectional `derive_via` cache entry of the earlier design (§17) is not implemented.) The beam solver does not use the float carrier coefficients. It builds its own carrier geometry from the defining points at the tier's precision (`BeamGeom`), and it decides incidences, endpoints and tangencies with exact predicates on the data (`Exact`, §31.3).

The `via` point representation is more stable and continuous than a winding flag — it moves smoothly as the segment deforms, whereas a discrete flag can flip abruptly.

**Side convention:** Every segment has a well-defined **left** and **right** side, determined by looking from `start` toward `via`/`end`. This applies uniformly to line segments and arc segments.

### 8.5 Composite structures

**Arc chain** (open): `Array[Segment]` where each segment's `end` connects to the next's `start`. Used for traced paths.

**Generalized polygon** (closed): `Array[Segment]` forming a closed loop. Used for visibility regions.

---

## 9. Surfaces

### 9.1 Surface = Segment + Policy

```
Surface:
    id:           int            # stable, unique, never reused
    segment:      Segment
    left:         SideConfig     # effect for the left side
    right:        SideConfig     # effect for the right side
    resolver:     ConfigResolver  # default: FixedResolver
    presence:     Array[PresenceCond]  # flag-gated existence, AND of conditions; empty = always (§9.6)
    is_target:    bool           # derived: a side carries a TargetHit (§10.6)
    player_solid: bool           # true = player collides with this surface (default true)

SideConfig:
    effect:       TransformativeEffect | ProjectiveEffect | TerminalEffect | null   # the PATH axis: exactly one
    hit_effects:  Array[HitEffect]   # the HIT axis: ordered, any number, any mix (§10.6)
    interactive:  bool    # can the player add this side to the plan?
                          # default: true for transformative/projective, false for terminal, false for null
```

A side has effects on **two orthogonal axes** (Stage 107). Its **path effect** acts on the beam: exactly one per side, `null` being the pass. Its **hit effects** act on everything but the beam's geometry, such as flags, targets and the world's surfaces, and a side may carry any number of them. Any path effect combines with any list of hit effects.

# player_solid affects ONLY CharacterBody2D collision. Arrow tracing and visibility always interact with all surfaces regardless.

Surface segments are defined and stored in **visual (world) coordinates**. The beam solver intersects beams with them where they are, in visual coordinates (§11, §12.1). Surfaces are never transformed. For the solver, `BeamScene.defs_from_surfaces` converts the surfaces into JSON-able scene defs, the same format the oracle traces.

`interactive` is a **static** property — it does not change with game state. It is set once in the level editor and serialized. The ConfigResolver may change the effect, but the interactivity of a side is fixed.

Terminal sides default to `interactive = false` but can be explicitly set to `interactive = true` by the level designer to allow planning shots that intentionally hit block surfaces (e.g., to trigger state changes).

Pass-through (null effect) sides without hit effects must have `interactive = false`. Such a side has nothing to plan for — making it interactive would create entries that are always processed as pass-through. This is enforced at load time.

*(Principle 3: geometry and behavior stay separate.)*

### 9.2 Side convention

**Left** and **right** are determined by the segment's traversal direction (from `start` toward `via`/`end`). This is uniform for all segment types — no separate inner/outer labeling for arcs.

**Side determination formula:** To determine which side of a surface a point `P` is on:

1. **Evaluate the carrier** at `P`: compute `f(P) = a(Px² + Py²) + b·Px + c·Py + d` using the carrier's `(a, b, c, d)` coefficients.
2. **Determine the segment's winding**: compute the signed area of triangle `(start, via, end)` = `cross(via - start, end - start)`. Positive = CCW winding, negative = CW winding.
3. **Map sign to side**: if winding is CCW (positive signed area), then `f(P) > 0` = LEFT and `f(P) < 0` = RIGHT. If winding is CW (negative signed area), the mapping is reversed.

This formula works uniformly for both line and arc carriers. For lines (`a = 0`), it reduces to the standard cross-product side test. For circles (`a ≠ 0`), `f(P) > 0` means P is outside the circle and `f(P) < 0` means inside — the winding direction then maps outside/inside to left/right.

Which side the beam approaches from is computed once at the hit point and stored in the hit event. *(Principle 5.)* `BeamGeom` evaluates the same convention without probe points: approach = `−τ_h·∇f(h)`, winding = `−(E − S) × ∇f(V)`, and LEFT iff `(winding ≥ 0) == (approach > 0)`. The result does not depend on how the carrier function `f` is scaled.

### 9.3 Surface identity

Each surface has a stable `id` assigned at creation, never reused. Ties at identical beam parameters are broken cursor first, then by surface ID; the departure re-hit is always last (§11.3).

### 9.4 State-conditional behavior

> **Status: implemented (Stage 85).**
> - **Resolvers:** `FixedResolver` and `CategoricalResolver` in `scripts/game/`; `Surface.resolver`.
> - **Table keys:** the flag value's JSON spelling (`StateChange.flag_key`).
> - **Categorical sides:** pass, terminal, reflection or inversion. Portals and projectives are fixed.
> - **Level exports:** `toggle_lines`, `gate_lines` and `flag_mirror_lines`, keyed by `state_key`.

A surface's active configuration is determined by a **ConfigResolver** — an abstraction with two built-in implementations:

```
ConfigResolver (abstract):
    func resolve(side: Side, game_state: GameState) → SideConfig

FixedResolver extends ConfigResolver:
    # Default. One fixed configuration per side. No game state dependency.
    left: SideConfig
    right: SideConfig
    func resolve(side, game_state):
        return left if side == LEFT else right

CategoricalResolver extends ConfigResolver:
    # Maps a game state key to a table of configurations.
    state_key: StringName
    config_table: Dictionary[Variant, {left: SideConfig, right: SideConfig}]
    func resolve(side, game_state):
        return config_table[game_state[state_key]][side]
```

The surface stores a `resolver: ConfigResolver`. The call site is simply:

```
func active_side_config(side: Side, game_state: GameState) → SideConfig:
    return resolver.resolve(side, game_state)
```

Future implementations can extend `ConfigResolver` for arbitrary logic (multi-flag conditions, scripted behavior). The abstraction is open for extension without modifying existing code.

Example: a breakable mirror uses `CategoricalResolver` with `state_key = "mirror_3_intact"` and `config_table = {true: {left: Reflection, right: null}, false: {left: null, right: null}}`.

**Missing key behavior:** if `state_key` is not in `game_state`, `CategoricalResolver` returns the config for a sentinel `DEFAULT` key in the table. If neither the actual value nor `DEFAULT` is in `config_table`, it falls back to the first entry in the table. This is flagged as a warning in debug builds.

---

### 9.5 Pre-transform

> **Status: implemented (Stage 101).**

A level may start **after** a transform: `LevelData.pre_transform` is an ordered list of inversion circles (`LevelSettings.pre_transform_inversions`, `(cx, cy, r)` triples) that are never hit and never clickable. They are part of the surfaces list flagged `pre_transform` (so every consumer sees them, drawn as a faint circle, the "horizon"), and define the frame the shot starts in: `Q_0`, their composite image of the point at infinity. In that frame the arrow's natural paths are the images of straight lines, circles through `Q_0`: the initial beam is the oriented circle through the player, the aim point and `Q_0` (`P → X → Q_0`, or `P → Q_0 → X`: the realization the plan's first entry fixes, with an ∞ entry first where both are open, §13.1); a direction shot starts along its direction on the circle through `Q_0`. Effects act on these beams by the ordinary beam maps; the frame's infinity moves with them (`Q_{j+1} = T_j(Q_j)`, restored by a pop, the real ∞ again after a projective, §12.5). `Q_0` on screen is the infinity marker (§4.2): planning it first disambiguates the short way round from the long way, which is what makes a pre-transformed level a puzzle. Demo: `scenes/playground/pre_inverted.tscn`.

### 9.6 Presence

> **Status: implemented (Stage 107, `docs/TDD_11_HIT_EFFECTS.md`).**

A surface may exist only under some game state. `Surface.presence` is a list of conditions that are **all** required (AND). Each condition names one flag and the set of its values (their `flag_key` spellings, §9.4) under which the surface exists. The token `DEFAULT` matches a missing key. An empty list means the surface always exists. The def form is `presence: [{key, values: [...]}]`.

```
PresenceCond:
    key:    String
    values: Array[String]   # flag_key spellings; "DEFAULT" = the key is missing
```

Presence differs from a pass. A pass side is still hit: it produces events and step boundaries, and fires its hit effects. An **absent** surface does not exist for the solver:
- It has no candidates.
- It plays no part in endpoint blocking (§11).
- It cannot be clicked into a plan.
- It is drawn as a faint dashed outline.

Presence is evaluated under the walk's **current** flags, so a surface can vanish or appear mid-shot when a hit effect writes its key (§10.6). The planned path evaluates presence under the plan's simulated flags (§13.7). A plan entry on a surface that is absent at that point is not reached, and the preview shows this (principle 13).

Presence is orthogonal to the resolver. The resolver chooses *how* a present surface behaves, presence chooses *whether* it is there, and both may read flags. Unlike categorical sides, presence works with every path effect, portals and projectives included (a portal pair shares one presence).

### 9.8 Moving surfaces

> **Status: implemented (Stage 151, docs/TDD_56_MOTION.md).**

An element may move over the level's **cycle**: its `motion` keys an offset and a rotation (about a pivot,
default its first surface's middle) at phases in [0, 1), interpolated linearly and looping. All its surfaces
move together; a portal's image moves with it. The cycle:
- runs on its own, a period per cycle (default 4 s), advanced per physics tick (so every host's frames agree);
- the **mouse wheel** sets it: each notch moves it a fiftieth of a cycle and **pauses** it;
- a paused cycle runs again after a **shot** lands, or after `resume_after` seconds (default 10) with no scroll and
  no plan change;
- it is frozen while the arrow flies (the game is paused), and the checkpoint holds it (an undo restores it).

The surfaces are where the cycle puts them when a shot is fired; the shot's world is that one. A level with a
cycle shows `Cycle: running` or `Cycle: paused` in its HUD and records `phase` and `cycle_auto`.

### 9.7 Design surfaces and pieces (crossings)

> **Status: implemented (Stage 108, `docs/TDD_12_CROSSINGS.md`).**

Levels are designed with **design surfaces**, which may cross each other. The player plans and clicks **pieces**: the design surfaces split at every crossing, so that no two pieces cross.

**Crossings.** Two carriers meet in exactly two points of the extended plane, counted with multiplicity (two lines: their finite point and ∞). A crossing is identified by the two carriers and one bit, *enters*: along carrier A's orientation (start → via → end), A enters B at one of the two points (A's tangent opposes B's gradient there) and exits at the other. Only a **proper crossing** splits: a point interior to both segments where they cross transversally. These do not split:
- a touch (tangent carriers);
- an endpoint lying on another segment (a T-junction);
- endpoints that coincide (a corner).

**Pieces.** A design surface crossed at k points becomes k + 1 pieces. A full circle crossed at k ≥ 1 points becomes k arcs. Examples:
- two crossing lines: 4 pieces;
- one line crossing two others: 3 + 2 + 2 = 7 pieces;
- three lines crossing in a triangle: 3 × 3 = 9 pieces.

A piece spans its design surface's carrier between two consecutive cut points: design endpoints, or crossings. A crossing's position is computed once, rounded to float32, and shared by the pieces that end there. **The walk and the region read a piece's end that is a crossing as the crossing itself** (Stage 133, the user): an end within one float32 ulp, in each coordinate, of a crossing of its carrier with another surface's carrier is that crossing by provenance -- computed at the walk's own precision -- not its rounding, which lies off the other carrier and would leave a sliver between the two (F23).

**The solver works on design surfaces.** Splitting would not change any physical outcome:
- away from crossings, a piece behaves exactly as its design surface (same carrier, sides, effects);
- a beam exactly through a crossing hits both design surfaces in their interiors at one point: a **shared point** (§11.1, Stage 138), where the arrow stops unless every surface there passes -- the same outcome as the junction of the four pieces that end there.

So the solver traces whole design surfaces, and pieces live where they matter.

**Pieces are the plannable units.**
- Clicks and hovers address one piece.
- A plan entry may name a piece. It then matches only a hit inside that piece's span, in the planned and the physical path alike. A planned hit outside it is off its segment, and a physical hit outside it is not the planned one, so the paths diverge. They diverge there even when the hit is on another piece of the same design surface, which applies the same effect on the same beam: the physical walk has left the plan (§14.2). Both paths are then drawn diverged from that hit on (red and yellow), although they coincide (Stage 129, the user, 2026-10-22).
- An entry without a piece (a programmatic plan) matches the whole design surface, as before.

**Everything else is the design surface's:** hit effects (one target, `target:<id>`), presence, the resolver, consumption of a spawn host. **Splitting ignores presence**, so the pieces never change within a shot.

**Not split (v1).** Portal pairs and pre-transform circles. A design surface crossing a portal is reported by level validation.

### 9.9 Light and shadow

> **Status: implemented (Stage 153, docs/TDD_58_SHADOW.md).**

An element whose kind has a `shade` bounds a **region**: dark or light, one side of its line (a half-plane,
clipped to the level bounds) or the inside or outside of its circle (an arc: its carrier circle), active while its
flag `when` holds (always without one). The element's own surface is never present. A point is in the **shadow**
when it lies in an active dark region and in no active light one (a point on a region's boundary is in neither).

- **The shadow's edge stops the arrow from the light.** The regions' boundaries, split where they cross, are the
  candidate pieces (their ids fixed when the level starts, after the bodies). Under the current flags a piece with
  the shadow on one side and the light on the other is present: its lit side terminal, its shadowed side a pass,
  neither plannable, not drawn. An arrow in the shadow passes out.
- **A surface wholly in the shadow is darkened:** a point of it (a line's middle, an arc's via, a point of a circle)
  is in the shadow and no present edge piece crosses it. A darkened surface is a pass on both sides, without hits or
  a resolver, for the arrow. Its presence and player solidity are kept, and it is drawn as it is, under the shadow.
  Portal pairs and pre-transform circles are never darkened.
- **When.** The shadow follows the flags and the world: at the start, when a shot lands, every physics tick (a
  detector, a moving surface, an object), after a journey and an undo. A shot sees the shadow of the moment it was
  fired. Shades do not move (v1).
- **Display.** The shadow (the dark regions' union minus the light ones', within the bounds) is drawn over the
  surfaces it hides, below the path and the arrow (the node `["shadow"]`, z 15); each shade's edge is dashed in its
  colour, faint while inactive. The editor shows the shadow under the starting flags.

## 10. Effect system

### 10.1 Effect base class and hierarchy

All effects extend a common `Effect` base class. Dispatch uses `kind()` (returning `Effect.Kind` enum) and method calls, not type checks.

```
Effect (extends RefCounted):
    enum Kind { PASS, TERMINAL, TRANSFORMATIVE, PROJECTIVE }

    func kind() → Kind                 # default: PASS
    func is_terminal() → bool          # kind() == TERMINAL
    func is_transformative() → bool    # kind() == TRANSFORMATIVE
    func is_self_inverse() → bool      # default: false; an involution links its side to itself and pops on re-hit
    func get_display_name() → String
    func get_display_color() → Color

TerminalEffect (extends Effect):
    func kind() → TERMINAL

TransformativeEffect (extends Effect):
    _carrier: GeneralizedCircle
    func kind() → TRANSFORMATIVE

ReflectionEffect (extends TransformativeEffect):
    func is_self_inverse() → true

CircleInversionEffect (extends TransformativeEffect):
    func is_self_inverse() → true
    # Requires circle carrier (a ≠ 0)

RigidMotionEffect (extends TransformativeEffect):
    _theta: float, _d: Vector2          # T(z) = e^{iθ}·z + d
    func partner() → RigidMotionEffect  # the paired (inverse) portal side
    static func create_portal_pair(source_seg, theta, d) → {source_effect, target_effect, target_segment}

LineNormalProjection, CircleNormalProjection,
LineDirectionalProjection, CircleDirectionalProjection (extend Effect):
    func kind() → PROJECTIVE
    _project_back: bool                 # "through" vs "back" (§10.4)
    _normal: Vector2                    # directional projections only
```

Effect objects are **descriptions**: kind, parameters, display name and colour. They hold no transform math. The beam solver reads them through `BeamScene.describe_config` (effect type string plus parameters) and applies the closed-form beam maps of §10.8 (`BeamEffects`). *(Removed in Stage 84: `get_mobius`, `get_inverse_mobius`, `get_tracked_transform`, `normalized`, and projections' `apply_forward`.)*

**Implemented effects:** `TerminalEffect`, `ReflectionEffect`, `CircleInversionEffect`, `RigidMotionEffect` (portals, Stage 82), and the four projective effects (physical tracing, Stage 83; planning through them, Stage 87).

**Planned effects:** `CompoundTransformativeEffect` (§10.3).

A surface side's effect is an `Effect` instance, or `null` (pass-through). A surface with `null` on both sides and `interactive = false` on both sides has no gameplay effect and is not clickable for plan construction. However, it still participates in ray tracing as a pass-through — the ray passes through it, producing a step. If `is_target = true`, the pass-through still registers a target hit.

### 10.2 Transformative effects

All are Möbius transformations (§5.2). The beam solver applies each one as a closed-form map of the beam at the hit (§10.8). Their composition along a shot is a sequence of such maps, recorded on the provenance stack (§10.7).

**Reflection:** Mirrors the beam across the surface's carrier line. Carrier must be a line (`a = 0`). Self-inverse. (The solver traces a reflection side on a circle carrier as inversion in that carrier. Some legacy test scenes use this; see §23.)

**Circle inversion:** Inverts the beam through the surface's carrier circle. Carrier must be a circle (`a ≠ 0`). **The inversion circle is always the surface's own carrier** — no separate parameter. Self-inverse. Full circles and partial arcs are both supported. Circle inversion is what makes arc paths appear.

**Rigid motion (portal):** Rotation by θ and translation by d, `T(z) = e^{iθ}·z + d`. General Euclidean isometry excluding reflections. Not self-inverse. Portals come in **pairs**: `RigidMotionEffect.create_portal_pair(source_seg, θ, d)` builds the target segment directly as `T(source_seg)`, point by point, plus the two effects `A → B` (θ, d) and `B → A` (−θ, −e^{−iθ}·d). Each is the other's `partner()`. A hit on A maps the beam onto B and departs from B. A hit on B while A's application is on top of the stack pops it (§10.7). Enables teleport/portal mechanics.

### 10.3 Compound transformative effects

> **Status: PLANNED** — `CompoundTransformativeEffect` is not yet implemented.

A single surface may apply multiple transformative effects in sequence. The compound is their composition (§5.2). `CompoundTransformativeEffect` **is a subtype of** `TransformativeEffect`, so it fits in the `SideConfig` type union without modification. On the beam solver it would be applied as the sequence of its elementary beam maps at the hit, and pushed and popped as one stack entry. That design is not yet specified in TDD_09.

```
CompoundTransformativeEffect extends TransformativeEffect:
    elementary: Array[TransformativeEffect]
```

Examples: reflect + translate = glide reflection. Reflect + rotate = rotated reflection.

### 10.4 Projective effects

> **Status: physical tracing DONE (Stage 83); planning through projectives DONE (Stage 87).**
> - **Implemented types:** `LineNormalProjection`, `CircleNormalProjection`, `LineDirectionalProjection` and `CircleDirectionalProjection`. Each has a `project_back` variant.
> - **Exit rule:** at the hit `h` the beam leaves as a straight line from `h` along ±axis. The axis is the carrier normal (line normal), the radius `h − c` (circle normal), or the effect's fixed normal (directional). The sign is exact. *Through* exits to the far side: `sign(exit·∇f) = sign(dir·∇f)`. *Back* returns to the approach side. An axis tangent to the carrier exits along +axis (through) or −axis (back).
> - **PLANNED mode** applies a projective plan entry like any other (Stage 87). The aim back-propagates its hit point (§13.3).
> - **Directional projections:** the implemented ones take an explicit normal direction plus through/back. They do not derive the direction from a semicircle's diameter and the approach side, as described below.

All produce an outgoing **straight beam** in visual space, determined solely by the hit point (and the approach side). Incoming direction is discarded. After a projective effect the **provenance stack is cleared and a new epoch begins** (§10.7). Nothing applied before the projective can be popped afterwards.

**Line normal projection:** Outgoing ray ⊥ to the surface line at the hit point. Back-propagation: `H` = orthogonal projection of target onto surface line.

**Circle normal projection:** Outgoing ray along the radius at the hit point. Back-propagation: `H` = intersection of line(center, target) with arc.

**Semi-circle directional projection:** Outgoing ray in the direction **normal to the semicircle's diameter line**, regardless of hit position. The semicircle's carrier circle defines the geometry; the diameter is determined by the segment's start and end points (the diameter endpoints). The diameter has two normals (opposite directions); which one is used depends on the **side** of approach (from the hit record) — the ray exits toward the side opposite the approach. This is consistent with projective semantics: the incoming *angle* is discarded, but the *side* still matters (Principle 4). A sub-segment of the semicircle may be used as the surface, exactly like any other effect. Back-propagation: `H` = intersection of line(target, −normal) with the arc segment. No separate direction parameter is needed — the direction is derived from the segment geometry and the side.

### 10.5 Terminal effect: block

Stops the beam. No outgoing beam, no stack change. A trace cut by the **event limits** (§12.6) ends similarly. It ends with a "max" event at the crossing that would exceed a limit, drawn up to that crossing with no effect.

### 10.6 Hit effects (state changes, targets, spawn, reset)

> **Status:** implemented (state changes: Stage 85; the general hit-effect model: Stage 107, `docs/TDD_11_HIT_EFFECTS.md`).
> - **StateChange:** `StateChange` has two ops, `SET` and `TOGGLE`; a toggle of a missing key counts the key as false.
> - **Firing:** a change fires when its side fires. In PHYSICAL that is a solid hit (§11.1: an on-segment hit that is not a lone endpoint's graze); in PLANNED it is the consumed plan entry.
> - **Order:** the side's config is resolved before its own changes are applied.
> - **Promotion:** the physical trace's final flags become the game state when the arrow lands. Undo restores the checkpoint's flags.
> - **Pops** restore beams, never flags.

Hit effects are the second axis of a side (§9.1), orthogonal to the path effect. A side carries an ordered list of them, of any kinds and any length. *(Principle 10.)*

```
HitEffect:
    deferred: bool      # per instance: apply at landing instead of in the shot (only if it has an in-shot form)

GrappleHit                        # at landing (Stage 146): the player travels the shot's path to the hit (§25.6)
PullHit                           # at landing (Stage 146): the hit object travels the path back to the player (§25.6)
StateChangeHit(key, op, value)    # in shot: the flag write
TargetHit                         # in shot: SET "target:<surface id>" = true
ResetHit(channels)                # in shot: SET "chan:<c>" = "armed" for each channel
SpawnHit(channel, template)       # in shot: SET "chan:<c>" = "pending" (consumes the host);
                                  # at landing, always: place the template at the firing hit
```

**Timing.**
- **IN_SHOT is the default.** A hit effect is applied *in the shot* (the solver sees it) whenever it has an elegant in-shot form, which is a flag write. Flags drive resolvers (§9.4) and presence (§9.6), so an in-shot write can change the rest of the same shot.
- **ON_LANDING** is used only for what has no in-shot form: placing a spawned surface, whose geometry comes from the shot itself (letting the shot see a surface it creates would make the path depend on itself), and a journey (§25.6), which moves the player or an object along the finished path.
- **Deferral is per instance.** Any in-shot effect may be marked `deferred`: it then fires by the same rule but applies only when the arrow lands. The reverse is not possible in general.

**Firing.** One rule applies to every hit effect, whatever its timing: it fires when its side fires.
- **PHYSICAL:** a solid hit (§11.1).
- **PLANNED:** the consumed plan entry.
- **Ghost:** suppressed while the side is ghosted.

The side's config, including its hit-effect list, is the one resolved under the flags *at that hit*. A side's effects apply in list order, and along the trace the last writer wins. Landing-time parts replay the physical trace's firings in order, after the final flags are known, with the same ordering and the same resolved configs (taken from the trace's provenance, never re-derived; principle 5).

**Targets.** A `TargetHit` writes `target:<id>`. `targets_hit` and the win check (§3.2) read these flags, and they are promoted, checkpointed and undone like any flag. *(Stage 107 moves target registration from "any on-segment hit" (§24.2) to the side firing rule. The two differ only at a shared endpoint that does not fully block.)*

**Spawn and reset (channels).** A channel `c` is the flag `chan:<c>`, which takes one of these values:
- missing or `"armed"`;
- `"pending"`, while a spawn awaits landing;
- `"g<N>"`, while the N-th spawned group exists.

Presence (§9.6) reads it:
- **Hosts.** The surfaces carrying a `SpawnHit` on `c` exist while `c` is armed. A host spawning into several channels needs them all armed.
- **The spawned group of generation N** (one surface, or a portal pair) exists while `c = "g<N>"`.

The sequence:
1. A firing `SpawnHit` sets `c` to `"pending"`. The hosts vanish for the rest of the shot (or at landing, if deferred). A host only exists, and so only fires, while its channel is armed, so no group of that channel exists at that point.
2. A firing `ResetHit` sets its channels to `"armed"`. Groups vanish and hosts reappear in the same shot.
3. **At landing,** a channel still `"pending"` gets generation `N + 1`: the template is placed at the hit that last fired its `SpawnHit` (§10.6.1), and `c` is set to `"g<N+1>"`. A spawn cancelled by a reset in the same shot never lands.

**The spawned surface never takes part in the shot that created it.** It is an ordinary surface from the next shot on: plannable, and carrying any path and hit effects except `SpawnHit` (no nesting). Plan entries on surfaces that left the world are dropped when the arrow lands. Undo and reset restore the flags and the world together.

#### 10.6.1 Spawn placement

A `SpawnTemplate` is a surface description (geometry plus both sides) in a **local frame**, plus an orientation mode and an optional `offset`. The surface is authored centred on the local origin, which lands on the hit point. `offset` (in the frame's axes, default none) moves it away deliberately. At the firing hit:
- **origin:** the hit point.
- **u (the first local axis):** chosen by the mode.
- **v (the second axis):** `u` rotated by +90°, i.e. `(x, y) → (−y, x)`, flipped on a RIGHT-side hit if `mirror_with_side` is set.

| Mode | u |
|---|---|
| `BEAM_TANGENT` | the incoming beam's travel direction at the hit |
| `BEAM_NORMAL` | the incoming travel direction rotated by +90°, `(x, y) → (−y, x)` |
| `CARRIER_TANGENT` | the host carrier's tangent at the hit, oriented along the segment (start → via → end; a full circle by the same three points) |
| `CARRIER_NORMAL` | the host carrier's normal at the hit, pointing toward the side the beam came from |
| `BEAM_REFLECTED` | the incoming direction mirrored in the carrier tangent |
| `WORLD` | +x (position only) |

The incoming direction is the solver's own: the beam's unit travel tangent at the hit, which the trace records for every hit event (`TracePath.hits`). It is exact on arcs, and it is still there when the step leading to the hit has zero length.

Placement is plain float64 arithmetic on the solver's outputs. The template's points map through the frame, and a portal template's displacement rotates with the frame.

#### 10.6.2 State changes

State changes can write to **any** key in the game state dictionary, not just the surface's own `state_key`. This enables inter-surface mechanics: surface A's hit can modify surface B's behavior by writing to B's state key.

Note the asymmetry: surfaces read from their resolver's configured key(s) via `ConfigResolver`, but `StateChange` can write to any key. A surface can affect surfaces it has no read-relationship with.

State changes are applied sequentially along the trace. If multiple hits write to the same key, the last writer wins. Deterministic because hit ordering is deterministic (§11.3).

#### Per-shot flags (Stage 147)

A flag whose key starts with `shot:` is a state of the arrow, not of the world: like `ghost`, it is erased from
the flags promoted when the arrow lands, so every shot starts without it. Elements use per-shot flags for what the
arrow carries (fire, frost, charge) and for what a shot does to the world only while it lasts (a breakable surface
gone for the rest of the shot). docs/ELEMENTS.md ("The kit") lists the patterns.

#### Fire, charge and frost (Stage 156)

The arrow, every surface and every object is **on fire** or not and **charged** or not; the arrow is also
**frosted** or not (the user's rules, 2026-10-25). The arrow's states are per-shot flags (`shot:fire`,
`shot:charge`, `shot:frost`); a thing's are kept (`el:<name>:fire`, `el:<name>:charge`). What a thing does is given
by its tags (docs/ELEMENTS.md):

- **Fire.** An arrow on fire touching a `flammable` thing sets it on fire. An arrow not on fire touching a thing on
  fire catches fire. An arrow passing an extinguishing barrier (`water`) is put out.
- **Charge.** An arrow touching a `conductive` thing swaps charge with it: a charged arrow charges an uncharged thing
  and is discharged; an uncharged arrow takes a charged thing's charge. Both charged or both not: nothing.
- **Frost.** An arrow touching a `frosted` thing (its sides pass, reflect or invert) becomes frosted. A frosted arrow
  that is not stopped keeps the frost. A frosted arrow that a side would stop (a terminal) is reflected by a line or
  inverted by a circle instead, and the frost is used up. A `rigid` side (the shadow's edge, §9.9) is never turned.

The rules are played out by the solver within the shot. Fire and charge are **conditional state changes** (`when`:
each applies only while its conditions hold under the flags its side fired under, before any of the side's changes).
Frost is a rule of side resolution, like ghost mode: while `shot:frost` is true a terminal, non-rigid side resolves to
a reflection (line) or an inversion (circle) and appends the write `shot:frost = false`. Ghost mode takes precedence.
A frosted reflection is not plannable unless the side is interactive (v1). The oracle (`trace2.py`) has both rules;
fixtures `bench/fixtures/stage156_arrow_states.json`.

#### Spreading (Stage 152)

Some states flow from thing to thing without an arrow: fire through what burns, charge through what conducts.
Elements and objects carry **tags**; a level's **spread rules** (none by default since Stage 156; for example `fire`
through `flammable`, kept for good; `charge` through `conductive`, sourced by `powered`) write `el:<name>:<flag>` = true on every tagged thing
connected to a lit one by touching (a segment, arc or object circle within 0.5 px of another).

- **Persistent** rules (fire): a lit thing stays lit; the flow passes through every tagged thing, present or not
  (a burnt barrier is gone, yet what touched it burns).
- **Recomputed** rules (charge): the flags are recomputed from the sources whenever the world changes (a shot
  lands, a detector or a journey changes flags, an object or a moving surface moves). The flow passes only
  through present things. An absent thing it reaches is lit but passes nothing on, so a charged door stays open
  without closing the circuit through itself.

Spreading runs when the level starts, when a shot lands (after the landing's writes), every physics tick, and when
a journey ends; the result rides the checkpoint like any flag. It is a game-layer rule: the solver sees only
the flags (docs/ELEMENTS.md, "Tags and spreading"; docs/TDD_57_SPREAD.md).

#### Ghost mode (Stage 86)

`ghost` is a reserved flag that lasts **one shot**: every trace starts with it false, and the game never keeps it.

- **Togglers:** a ghost toggler is a side whose state changes include a write to `ghost`. Its effect is a pass, it is plannable, and the level export is `ghost_toggler_lines`.
- **While ghosted, everything else passes.** Every other side resolves to a pass, together with its state change: mirrors, inversions, portals, projectives, terminals and room walls. A ghost arrow therefore crosses everything, can leave through a wall, and wraps through infinity, since a straight beam is a projective line.
- **Materializing:** the arrow materializes at the next toggler hit, or at the same toggler one loop later. Every loop is walked (there is no loop detection, Stage 131); the event limits bound them (§12.6).
- **Drawing:** ghost steps (`TraceStep.ghost`) are drawn paler and translucent, and ghost togglers have their own colour.
- **Planning inside a ghost stretch:** the plan's state is simulated forward from a solid start (§13.7). While it is ghosted, after an odd number of planned ghost togglers, only a ghost toggler can be added: every other side is refused at the click and gets no hover highlight. A click within reach of both a refused side and a toggler takes the toggler. A programmatic plan that still names such a side treats it as a pass (`BeamTracer.plan_refuses`).

#### 10.6.3 Spawn trimming

> **Status: implemented (Stage 108).**

A spawned surface is **trimmed** when it lands (`SpawnTemplate.trim`, on by default). The placed surface is split by its proper crossings with every surface present under the landed flags: level surfaces and walls, other channels' groups, present hosts. Only the piece containing the placed surface's **midpoint** (its via point) is kept. A full circle has no midpoint, and a spawned circle is centred on the hit point by default, so every point of it is equally near; it keeps its **longest arc**. The kept piece becomes the spawned surface. A spawned surface therefore never crosses what is there when it lands. Something that appears later (a reset restoring a host) is split against it into pieces by §9.7. The preview shows the trimmed surface (the same function).

### 10.7 Provenance stack and frame identity by effect type

*(Replaces the legacy "frame behaviour" `M' = M ∘ T` / `M' = I`. There is no frame matrix.)*

The solver keeps a **provenance (transform) stack**. Each entry holds the beam *before* an applied transformative effect, the surface it was applied on, its known points and exact-line data, and the id of the surface whose hit pops it (`BeamSolver.StackEntry`).

| Effect type applied | Stack | Next beam |
|---------------------|-------|-----------|
| Transformative, surface **not** the top's pop id | **push** `(beam before, surface)` | the effect's beam map (§10.8), departing the surface at `h` (a portal departs the paired surface at `T(h)`) |
| Transformative, surface **is** the top's pop id (re-hit of the same self-inverse surface, or a portal's pair) | **pop** | the earlier beam object, restored exactly, resumed at the shared point actually hit (`T⁻¹` of it for a portal) |
| Projective | **clear**, start a new **epoch** | straight exit beam from `h` (§10.4) |
| Terminal | unchanged — trace ends | none |
| Pass-through (or not fired) | unchanged | same beam continues |

**Frame identity:** a step's `frame_id` is the id of `(epoch, stack signature)`. `frame_id = 0` (`TraceStep.IDENTITY_FRAME`) is the empty stack in epoch 0. `BeamTracer` assigns ids from one dictionary shared by the planned and the physical path of a pair, so equal ids mean the same applied effects since the last projective. A projective starts a new epoch, so frames after it get new ids even when the stack signature repeats. An epoch is identified by the projective firings that opened it (surface and side, in order), not by their count: two walks that fire different projectives at one point (a line normal through the centre of a circle normal, hit where its carrier meets the circle) leave it in different frames (K14, Stage 113).

### 10.8 Effect maps (beam maps and Möbius matrices)

**Beam maps (what the solver implements, `BeamEffects` / `HPBeam` / `FastSolver`).** The beam `z(t) = p + tτ/(1 − iκt/2)` is hit at `h` with tangent `τ_h`:

| Effect | Beam map at hit `h` |
|---|---|
| Reflection, line with unit direction `u` | `p' = h`, `τ' = u²·conj(τ_h)`, `κ' = −κ` |
| Reflection, circle carrier | = inversion in the carrier |
| Inversion in circle `(c, R)`, `ν = (h − c)/R` | `p' = h`, `τ' = −conj(τ_h)·ν²`, `κ' = −κ − 2·cross(τ_h, h − c)/R²` |
| Rigid motion `T(z) = e^{iθ}z + d` (portal A → B) | `p' = T(h)`, `τ' = e^{iθ}·τ_h`, `κ' = κ` |
| Projective | `p' = h`, `τ'` = exit direction (§10.4), `κ' = 0` |

The point maps of the same effects (with `null` for the point at infinity) are used only for the anchored planned path (§13.1).

**Möbius matrices (mathematical reference).** The same transformative effects written as Möbius matrices. No matrix objects exist in the code. In all formulas, complex numbers are written as `x + iy` and the matrix is `[[α, β], [γ, δ]]`.

**Reflection** across line `bx + cy + d = 0` (general form, no normalization required):

Let `n = b + ic` (the complex normal, unnormalized). Let `norm² = b² + c²`. Then:
```
α = -n̄         β = -2d · n̄ / norm²
γ = 0           δ = n
anti-conformal
```
Anti-conformal: applies as `z → (α·z̄ + β) / (γ·z̄ + δ)`. Self-inverse.

Constraint: `b² + c² > 0` (the carrier must be a non-degenerate line). Validated at load time.

**Circle inversion** through circle `a(x²+y²) + bx + cy + d = 0` (carrier of the surface):

Let `center = -b/(2a) - i·c/(2a)` (complex center) and `r² = (b² + c² - 4ad) / (4a²)`. Then:
```
α = center      β = r² - |center|²
γ = 1           δ = -conj(center)
anti-conformal
```
Anti-conformal: applies as `z → center + r² / (conj(z) - conj(center))`, equivalently `z → center + r² · conj(z - center) / |z - center|²`. Self-inverse: `M² = r² · I` for any center. Verification: a point on the circle maps to itself (e.g., for center=1, r=1: z=2 → w = 1 + 1/(2-1) = 2).

**Rigid motion** (rotation by θ, translation by d = d_x + i·d_y):
```
α = e^{iθ}      β = d
γ = 0            δ = 1
conformal
```
Conformal: applies as `z → e^{iθ}·z + d`. Inverse: `α = e^{-iθ}, β = -e^{-iθ}·d, γ = 0, δ = 1`, conformal. This inverse is the paired portal's `(−θ, −e^{−iθ}·d)` (§10.2). `create_portal_pair` computes the target segment by applying `T` to the source segment's three points directly. The beam solver uses `a = unit(cos θ, sin θ)` of the float64 values.

---

# Part IV: Simulation

*Pseudocode in this part is language-agnostic and illustrative, not prescriptive GDScript. Data definitions (§8–9) use a schema notation. The implementation should follow GDScript conventions.*

## 11. Intersection system

> **Stage 84:** the legacy `intersection.gd` (ray in a normalized frame vs. transformed carriers, three-tier endpoint snapping, `skip_segment`) was deleted. Intersection is now part of the beam solver: `BeamGeom` holds the segment geometry and `HPBeam` the beam–circle roots; `FastSolver` is its float64 twin. Specification: `docs/research/oracle/trace2.py`, plus the "Exactness rules" in `docs/TDD_09_PRECISION_INTEGRATION.md`.

### 11.1 Single intersection primitive

```
BeamGeom.hits(beam, seg, depart_on) → Array[t]      # roots of the beam with seg's carrier
```

Surfaces are intersected in visual space (§6.1). For the beam `z(t) = p + tτ/(1 − iκt/2)`:

| Carrier | Equation in `t` |
|---------|-----------------|
| Line, `f(z) = cross(E − S, z − S)` | `f(p)(1 + κ²t²/4) + (∇f·τ)·t + (κ/2)·cross(τ, ∇f)·t² = 0`. Up to 2 roots, or 1 for a straight beam. |
| Circle (arc or full circle) | The analogous quadratic (`HPBeam.hits`). Up to 2 roots. |

A root may be `t = ∞`, the antipode of a circle beam, where degree-dropping roots lie. `t = ∞` is an exact value in every tier.

The roots are **not** filtered by segment bounds: **every carrier crossing is an event** (§11.5). Each crossing is then classified:

1. **On-segment** (`on_segment`): the point P is on the segment from start S through via V to end E iff `Re(cross_ratio(S, P; E, V)) >= 0`. The test is computed in homogeneous coordinates (V = the point at infinity for full lines), at the tier's precision:
   1. Compute homogeneous determinants: `hdet(S,V)`, `hdet(E,P)`, `hdet(S,P)`, `hdet(E,V)`, where `hdet(A,B) = zA·wB - zB·wA` treating each point as `(z, w)` with `w = 1` for finite points and `w = 0` for the point at infinity.
   2. Compute `num = hdet(S,V)·hdet(E,P)` and `den = hdet(S,P)·hdet(E,V)` (complex products).
   3. The point is on the segment iff `Re(num · conj(den)) >= 0`.

   This works for arcs of any span (including major arcs > 180°), lines through infinity, and full circles (all carrier points are contained). No center computation, no angle comparison, no epsilon threshold.
2. **Side** of approach (§9.2).
3. **Solid hits and shared points** (Stage 138, the user). An on-segment hit in the interior of the segment is **solid**: the surface's effect and state changes apply. A hit point is **shared** when two or more present surfaces hold it: an exact endpoint of one on the segment of another (a T-junction), coinciding endpoints (a corner), or a proper crossing, interior to both (a planned piece's end, §9.7). The walk knows a shared point exactly, never by distance: by provenance (a known point; an endpoint equal by data) or by an exact tie (two crossings at one parameter, §11.4). At a shared point the hit belongs to the surfaces whose approached side has a **consequence** there -- an effect other than a pass, a state change, a landing effect, a target: a candidate's side (§12.1), ghost aside (§10.6). If **two or more** do, no single surface owns the hit and nothing is chosen by the id: the hit is **terminal** in the physical walk -- the arrow stops, nothing fires, and the event carries the lowest id among them. If exactly one does, it owns the hit: a solid hit of that surface, as anywhere on its segment. (A pure pass-through is never computed, §12.1, so it holds no point.) The planned walk computes its entries' surfaces alone (§12.1), so it cannot know the point is shared: it follows its plan there, and the two walks diverge (§14.2) -- a cursor beyond such a point is outside the region (§15). A **lone endpoint** -- an exact endpoint no other present surface holds -- is a graze: no effect, no state change, the beam goes on. (Until Stage 138 an endpoint's blocked sides accumulated over the segments sharing it, and an effect fired only when both sides were blocked, the lowest id first: a corner reflected or blocked by the id, a lone endpoint grazed.)

**Exact incidence:** when a surface's carrier contains a **known point** of the beam exactly (§31.4), its root nearest that point takes the known point's parameter and the exact point. The decision uses exact predicates on the data (`Exact`). No rounding decides an endpoint or an incidence.

### 11.2 Hit record

The solver emits **events** (`BeamSolver.Event`):

```
Event:
    kind:        "hit" | "cursor" | "origin" | "escape" | "max" | "infinity"
    surface_id:  int           # -1 for non-hit events; INFINITY_ID (-3) for infinity
    left:        bool          # side the beam comes from
    applied:     bool          # the side's effect fired (terminal included)
    terminal, popped, teleport, projective: bool
    on_segment:  bool
    rehit:       bool          # the departure point, re-hit after the full loop
    epoch:       int           # projectives start a new epoch
    point:       CN            # null for escape, the real infinity, and a max at infinity; Q for a finite frame's infinity
    beam, t_from, t_to         # the arc leading to this event (t_to null = infinity)
    margin:      float         # px gap to the nearest competing decision (precision control)
```

`BeamTracer` converts events into `TraceStep`s (§14.7). It attaches a `TraceHit` (`t`, `point`, `segment`, `side`, `on_segment`) to the last step of each hit event. The `at_endpoint` / `blocked_left` / `blocked_right` fields of `TraceHit` are legacy and are not filled by the adapter, because blockage is decided inside the solver. `is_fully_blocked()` therefore has no meaning on adapter hits.

### 11.3 Earliest hit selection

Candidates are ordered **projectively from the current parameter `t0`** (§6.3): first the roots with `t > t0` in increasing order, then the wrapped roots (`t ≤ t0`) in increasing order.

- **Ties:** at an identical parameter the cursor comes first, then surfaces by smaller ID.
- **Departure re-hit:** the departure point is re-hit **last**, after the full loop.
- **Cursor at the departure point:** a cursor exactly at the departure point is reached immediately, before everything else.

The walker visits the candidates **in this order, one event each**, until an effect fires (§12.1). The losers at a tied parameter are not ignored: each is its own event, unless an earlier one changes the beam.

### 11.4 Degenerate cases

| Case | Resolution |
|------|-----------|
| Beam departing a surface | After applying at `h` on `S`, the new beam departs `S` at `h`. The departure root is excluded as an ordinary candidate and re-hit last, as an ordinary hit. No `skip_segment` and no epsilon. A **pop** (the stack top's surface hit again, the old beam restored) departs that beam at the pop point, tagged like a push's hit point (Stage 106): the known point that is the pop point, by data or by its parameter on the beam, becomes the departure point, so it never snaps the departing carrier's other crossing onto itself. On a resumed beam the departing carrier's root *nearest* the departure parameter is the departure (identified, never compared for equality: the roots are recomputed on the restored beam). |
| Exact line exactly tangent to a circle carrier | Touches without crossing: no event. |
| Beam tangent to a carrier at the frame's infinity point `Q` (Stage 105) | Touches without crossing: no event. Decided exactly from provenance: beside `Q` the walk keeps an exact **pole record** (`Q` and the beam's direction there, §31.3), so a carrier through `Q` is tangent there exactly when its direction at `Q` is parallel to the record's. Where the frame is not exact data (a rotated portal, an aim image beyond the exact budget, a shot from a rounded direction) there is no record and the precision ladder decides, as for any near-tangency. |
| Carriers through the frame's infinity point | Crossed exactly at `Q` (the root nearest `Q` takes `Q`'s parameter): simultaneous crossings there tie by id, then the passage of infinity. When the beam departs from `Q` (an effect fired at `Q`), another carrier through `Q` is crossed at the departure point at the end of the loop, and the departing carrier's root at `Q` is the departure. |
| Concurrent line carriers | On an exact line, adjacent crossings of two line carriers that meet the line at one point (exact 3×3 determinant) share one parameter. The ID rule orders them. On a circular beam with an **exact circle** (§31.4), adjacent crossings of two line carriers at their common point, which the circle passes exactly, share one parameter too, and the ID rule orders them (Perf P2b): never rounding. |
| Exact circle tangent to a carrier | Its two roots there are one point: one decision, not two neighbours whose gap the precision ladder must resolve (Perf P2b). |
| A hit at a shared point (Stage 138) | Crossings of several present surfaces at one exact parameter (a tie, above, or one known point), or a crossing at another present surface's exact endpoint: the arrow stops there when two or more of the surfaces holding the point have a consequence (§11.1): the physical walk's terminal event carries the lowest id among them, nothing fires, and the passages of infinity or the origin at that parameter are not reached; the planned walk follows its plan. |
| Inversion of a beam through the inversion center | Any beam through a known point that is exactly the center becomes an exactly straight beam. |
| Player or cursor exactly at an inversion center (point at ∞ in the anchored construction) | Forward walk from the exact aim direction. If the aim image itself is at ∞, or is the player (the cursor's image under the plan is the player: every direction solves the plan), the walk goes straight at the cursor: the beam through the player and the cursor (in a pre-transform frame the circle through the player, the cursor and `Q_0`), the same in the oracle and natively (Stage 106, K3). |
| Player = cursor | No aim: no trace (`BeamTracer.trace_pair` returns null). |
| Zero-length segment | Rejected at load (§23). |

### 11.5 Unbounded carrier intersection

All roots on the unbounded carrier (full line or full circle) are events, whether on or off the segment. `on_segment` only matters for whether PHYSICAL can fire (§14.1). PLANNED mode fires the next plan entry on an ID and side match **even off-segment**: the planned path uses the carrier extension.

### 11.6 See-through parameter

*(Retired, Stage 110.)* The legacy visibility design needed surfaces made transparent for truncating boundaries. The visibility region on the beam solver (§15) casts ordinary pair traces instead, so the solver has no such option.

---

## 12. Ray tracing

> **Stage 84:** the legacy `Tracer` (stage-based walk in a normalized frame, `TrackedTransform` stack, `frame.apply` per step) was deleted. The only solver is the precision beam solver in `scripts/math/precision/`. Its rules are those of the mpmath oracle `docs/research/oracle/trace2.py`, which is the specification. See `docs/TDD_09_PRECISION_INTEGRATION.md`, Stages 80–83.

### 12.1 The tracing loop

**Entry point.** `BeamTracer.trace_pair(surfaces, player, cursor, plan)` builds the scene defs. It then runs `PrecisionLadder.solve`, which returns the **planned** and **physical** event lists, and converts both to `TracePath`s. The renderer, the firing code (arrow flight), the debug dump and the invariant checker all call it.

**Components.**
- **`BeamSolver`** — the single rule walker (`walk_from`). It implements the trace rules for every precision tier, over a backend:
  - `NumBackend`: `HPBeam` over any `NumCtx`;
  - `FastSolver.Backend`: raw float64 with the tangent-linear derivative inline, bit-identical to `NumBackend` over `F64Num`.

  Topology decisions (on-segment, side, endpoint blockage) always run the shared `BeamGeom` code.
- **`BeamScene`** (the scene at one tier), **`BeamEffects`** (beam, point and pole maps), **`BeamGeom`** (segment geometry), **`HPBeam`** (the beam), and **`Exact`** (exact predicates).
- **`ErrorEstimate`** (tangent-linear a-priori error envelope, plus decision margins) and **`PrecisionLadder`** (dynamic precision, §31.5).

**Rule walker** (illustrative; the authoritative statement is the `beam_solver.gd` header and `trace2.py`):

```
walk(beam, depart, t0):
    loop:
        cands = roots of beam with every CANDIDATE surface carrier (§11.1; Stage 134, below)
              + cursor, if the plan is complete and matched
              + the departure point (re-hit last)
        snap candidates to known points (exact incidence, §11.1)
        sort projectively from t0 (§11.3)
        for c in cands:
            if c is the cursor:
                emit "cursor"; mode = PHYSICAL      # post-cursor continuation (never counted, §12.6)
                continue
            classify c → point, on_segment, side, fully_blocked
            if not on_segment and no remaining plan entry names c's surface:
                continue                            # not an event (Stage 139, §12.6): a byproduct of the carrier
            if events + 1 > 64: emit "max" at c (drawn up to it, no effect); stop
            # (c's decision below; a significant one -- an applied effect, a state change, the passage of
            #  infinity -- past the 10th: emit "max" at c instead, no effect; stop)
            events += 1
            if mode == PHYSICAL:
                do = side's effect if fully_blocked else none
                if do is transformative/projective: plan bookkeeping by surface ID
            else:  # PLANNED
                if the next plan entry matches surface ID AND side:
                    plan_index += 1
                    do = side's effect if transformative else none
                    # terminal entries are consumed without effect; projectives are applied (Stage 87)
                else: do = none                     # everything else passes, terminals included
            emit "hit"
            if do is terminal: stop
            if do is transformative: push/pop the provenance stack (§10.7); beam = mapped or restored beam; restart loop
            if do is projective:     clear stack, epoch += 1; beam = exit line (§10.4); restart loop
        # no effect fired on the whole loop (Stage 131: no loop detection)
        if a straight beam and infinity is off: emit "escape"; stop   # it leaves through infinity
        restart loop on the same beam                                  # the next loop: the limits bound it
```

**Anchored planned path.** In PLANNED mode, and in PHYSICAL mode until its first divergence from the planned decisions, firing plan entry `j` does not map the current beam. Instead the walker swaps in the **anchored beam** `B_{j+1}` of the plan (§13.1). Each anchored beam is built independently from exact data, so errors do not accumulate along the plan. After the divergence (or after the cursor) the trace is a forward continuation from the current beam. That part is the numerically sensitive one: it is seeded for `ErrorEstimate` and solved on the `PrecisionLadder`.

**Key design decisions:**
- **One walker for all tiers and both modes.** PLANNED and PHYSICAL differ only in which effects fire (§14.1).
- **Candidate surfaces** (Stage 134, the user). A walk computes the crossings of a surface only where hitting it could matter -- where it could change the path, the game's state (a flag, a target) or give a significant event. In the walk's current state:
  - **PLANNED** (before the cursor): the surfaces of the plan's remaining entries -- nothing else fires;
  - **PHYSICAL** (and the planned walk past its cursor): a surface present under the walk's flags with a side whose effect is not a pass, which changes the state, or whose hit changes the game at the landing (a deferred hit effect, ghost or not: the side's `landing`), a target, and the surfaces of the plan's remaining entries (its bookkeeping and the planned shadow, B5, read them);
  - always: the passages of the frame's infinity and of the ray's origin, and the cursor.
  A surface that would be passed with no consequence is never computed (a pure pass-through; in ghost mode, what the ghost passes). Where the walk's state changes at an event -- its flags, an entry consumed, the cursor -- the candidates are **rebuilt** for the rest of the loop. Every candidate's crossing **on its segment** is an event: a significant one, or another (outside a planned piece, a side that passes) kept as a mark (§4.3); so is every crossing of a surface a remaining plan entry names, on or off its segment (the plan's bookkeeping, the quiet entries and the planned shadow read them). The two walks' events no longer correspond one to one; the merge pairs their paths by segments between significant events (§14.5).
- **Off-segment crossings are not events** (Stage 139, the user: "the goal of the walks is to produce the sequence of significant hitpoints; non-significant hitpoints such as off-segment are a byproduct, but should not be a requirement"). A candidate's crossing that lies off its segment, on a surface no remaining plan entry names, is no event: not counted by the limits (§12.6), not drawn (§4.3), no decision. The walker may skip computing it where a rigorous test proves it off the segment -- a line beam whose exact line has both endpoints strictly on one side, by its error-bounded float coefficients (§31.3: a rigorous bound, never an epsilon; where the test cannot decide, the crossing is computed and classified at the tier's precision, and its decision margin -- the gap that made it off the segment -- is kept for the ladder (§31.5) as the margin of the next event, with its gain, so a crossing a hair off an endpoint still climbs). A carrier's roots come from its representative surface (the lowest id on it, §11.1 K6), candidate or not: one source, whatever the walk's candidates are.
- **Shared points stop the arrow** (Stage 138, the user, §11.1): a hit exactly where two or more present surfaces meet -- a junction of endpoints, a proper crossing, a planned piece's end -- is terminal in the physical walk when two or more surfaces there have a consequence (the planned walk, computing its entries alone, follows its plan: the walks diverge there); a lone endpoint is a graze. PHYSICAL fires only on a solid hit.
- **Provenance stack:** re-applying the surface on top of the stack (or hitting a portal's pair) pops it and restores the earlier beam object exactly. This prevents infinite loops with self-inverse effects and makes reversal exact by construction (Principle 22).
- **No spatial filtering:** the solver never filters surfaces by position, proximity, or any other spatial criterion -- only by whether hitting them now could matter (candidates, above), and by an exact or rigorously bounded proof that a crossing lies off its segment (Stage 139, above): a proof, never a heuristic or a distance.
- **No epsilons:** decisions use exact predicates and provenance (§31.3), and precision is raised until the trajectory is reliable (§31.5).

### 12.2 Step record

See §14.7 for the `TraceStep` definition and §14.6 for the five step types. Steps are in visual coordinates. Each step carries a **frame id** (§10.7) instead of a frame, and its line/arc nature comes from the beam's curvature.

### 12.3 Converting to visual

`BeamTracer.to_path(events, …)` produces a `TracePath`:
- **Steps per event:** each event is the arc of its beam from `t_from` to `t_to`. Its start is exactly the previous event's point.
- **Arc vs line:** a step is an arc step iff the beam's `κ ≠ 0`. Curvature is exact, so reflections keep a straight beam exactly straight.
- **Wrapping through infinity:** an arc that wraps through infinity on a straight beam is split into an outgoing and an incoming escape step (`end` or `start` = `Vector2(INF, INF)`, `via` = travel direction). A very large-radius arc that goes "the long way round" is drawn the same way.
- **Zero-length steps** are not emitted; the event's effect still applies.
- **Metadata:** the first step after a portal is flagged `after_portal` (an intended gap). Each step records `est_error` (px) and `precision_bits`.
- **Trace-level fields:** `cursor_index` is the step index of the cursor event. `targets_hit` collects target surfaces hit on-segment. `hit_count` is the number of events.

### 12.4 Frame updates → beam updates

There is no frame. After an applied effect the walker continues on a new beam:
- **Transformative:** the beam map (§10.8), departing the surface at `h`, or the paired surface at `T(h)` for a portal; push. On a pop, the earlier beam is restored instead.
- **Projective:** a straight exit beam from `h`; the stack is cleared and a new epoch starts.

The frame id of subsequent steps follows the stack (§10.7).

### 12.5 Beyond-infinity in the visual frame

- **The frame's infinity** `Q = F(∞)` (§6.3, §13.1) is passed exactly once per loop of a beam; the passage is the `infinity` event (Stages 88, 101): it counts toward the hit limit; a trace cut by the limit there ends with a `max` event at `Q` (surface id `INFINITY_ID`; no point when `Q` is the real infinity). The event is **applied** when the next plan entry is ∞ (the entry is consumed, in both modes); otherwise it **passes**. **An unplanned passage before the cursor is a divergence** (Stage 131, the user, 2026-10-22): the physical walk leaves the plan there (§14.2), so a cursor reached only through infinity unplanned is not visible, and the path is drawn diverged from the passage on. The aim prefers not going through infinity; when only the way through infinity reaches the cursor, the aim takes it all the same, shown as a divergence. After the cursor a passage is the continuation, not a divergence. An ∞ entry that is not reached when another entry fires is a divergence too, like any unmet entry.
- **Straight beam, `Q = ∞`:** passing `t = ∞` means the arrow travels to the viewport edge, leaves, and returns from the opposite edge (two escape steps, §12.3; the outgoing one carries `surface_id = INFINITY_ID`). If no effect fires on the whole loop, the arrow loops again: there is no loop detection (Stage 131); the event limits end the trace (§12.6). There is no escape: infinity is always on (Stage 136, the user: the switch that let a line leave through infinity is gone).
- **A finite `Q`** (every circle beam, after an inversion or in a pre-transform level; a straight beam through an inversion center): the arrow simply passes the point `Q`; the step ending there carries `surface_id = INFINITY_ID` and the next step continues from it. `t = ∞` of a circle beam (the antipode of `p`) is an ordinary point. If no effect fires on the whole circle, the arrow goes round again (Stage 131): every circle beam passes `Q` once per cycle, a significant hit, so the event limits end it (§12.6). A straight beam with a finite `Q` passes the real `t = ∞` silently (its pre-image there is a finite point).
- **Ties:** at one parameter the cursor comes first, then the hits by id, then the passage of infinity.
- **A touch at `Q`** (Stage 105): a beam tangent to a surface's carrier at `Q` touches it without crossing (no event; §11.4). A pre-transform level with a mirror through `Q_0` makes such grazes reachable on purpose (`scenes/playground/tangent_infinity.tscn`).

### 12.6 Event limits

There is **no loop detection** (Stage 131, the user, 2026-10-22). A trace ends at a block (§10.5) or at a limit:
- at most **10 significant events**: an applied effect (reflection, inversion, portal, projective, block, a pop), a hit whose state change fires, the passage of the frame's infinity (`infinity` events, applied or not), and the passage of the **ray's origin** (`origin` events, below);
- at most **64 events** in all: every crossing that is an event -- a candidate's on its segment, a plan surface's on or off it (Stage 134, Stage 139: a candidate's crossing off its segment, on a surface no remaining entry names, is no event and does not count) -- significant or not. A safety limit, not a game rule (the user).

These are the only two limits (the user, 2026-10-22). Nothing counts or reads laps (Stage 134, the user): every loop of a beam passes the frame's infinity and the ray's origin, both significant, so even a beam with nothing on it ends within five loops. A loop that passes nothing significant is an invariant violation, not a case.

**The ray's origin** (Stage 131, the user). The ray's origin in a frame is the point the ray radiates from there: the player, carried by every map applied since (a reflection, an inversion, a portal) -- the player's image in that frame, which lies on the beam's carrier (a map takes the line through the player to a line or circle through its image); after a projective effect, which starts a new ray, its departure point -- except a circle normal projection's, whose rays all run along the circle's radii: their origin is the circle's centre (the user), so passing the centre (or its image under the maps after) is a significant event that needs planning, on whichever side of the circle the ray was projected. When the walk passes it -- on the first beam only after a loop that fires no effect (or only changes the state), when the beam comes back to the player; after a reflection, wherever its carrier passes the player's image (a straight beam behind its departure, once it wraps through infinity) -- that passage is an `origin` event at that point, a significant event. A departure point that is not the ray's origin is no event when a loop comes back to it: the beam goes round again. An origin at infinity (the player at an inversion's centre) is the passage of infinity itself. Before the cursor, an unplanned passage of the ray's origin is a **divergence**, like an unplanned passage of infinity (§12.5): the physical walk leaves the plan there, so a cursor reached only on a later lap is not visible. A straight beam with the infinity switch off does not come back (it escapes). **Planning it** (Stage 132): an origin entry next in the plan is consumed by the passage, in both walks (applied, like ∞'s, §12.5): no divergence there, and the cursor beyond it is reached on the plan. Where it is plannable: §4.2. The cursor is not counted: it is not a crossing, and the two walks get the same budget. A trace cut by a limit ends with a `max` event at the crossing that would exceed it, drawn up to it with no effect (a crossing at the infinity point: at `Q`, no point when `Q` is the real infinity). The planned walk decides the plan's realization with wider limits (8 × each, §13.1).

When the limit is reached, the preview shows a truncation marker (small stop icon) at the final hit point *(implemented, Stage 110: `TracePath.truncated`, `PathRenderer.truncation_markers`)*. The cursor of a cut path is not reached, so it is not a valid aim and lies outside the visibility region (§15). During arrow flight, the arrow stops with a distinct visual cue (flash/fade) *(not yet implemented)*.

---

## 13. Planning algorithm

### 13.1 Image chain method (from first principles)

The core planning trick: a path that bounces off a surface via a transformative effect can be "unfolded" by applying the **inverse transformation** to the target, producing an **image point**. For several transformative surfaces in a fixed order, chain those inverse transformations. The aim is then the line from the player to the **fully unfolded image** of the cursor, so there is no step-by-step "what did the ray hit first?" search.

**Forward images:** transform the origin through planned surfaces' effects in order.
**Backward images:** inverse-transform the cursor through planned surfaces' effects in reverse order.

The bounce order is **fixed by the plan**. Each leg is a piece of a generalized circle (a line, or an arc after an inversion), and it ends where it meets the next planned surface's carrier.

**As implemented (anchored planned path, inside `BeamSolver`).** There is no separate planner. Let `F_j` be the composition of the transformative effects of plan entries `1..j`, taken on the provenance stack: a re-hit of the top's surface or of a portal's pair cancels it. The planned beam after entry `j` is the oriented generalized circle

```
B_j = circle through  P_j = F_j(player),  X_j = F_j(F_K⁻¹(cursor)),  Q_j = F_j(∞),   oriented P → X → Q
```

`K` is the plan length. `B_0` is the aim from the player through the fully unfolded image `X_0 = F_K⁻¹(cursor)`.
- **Poles:** the poles `P_j` and `Q_j` are mapped forward through the point maps of the effects (`BeamEffects.map_pole`). The difference `P − Q` is carried by **exact Möbius difference formulas**, so it does not cancel catastrophically.
- **Image chain:** `X_j` is mapped backward from the cursor (`BeamEffects.map_point`) and memoized per stack frame, so a pop returns exactly the earlier image.
- **Points at infinity** are allowed. If the construction degenerates (player or cursor exactly at an inversion center), see §11.4.
- **Two realizations** (Stages 88, 101, 111): path order is preserved by every effect, so the orientation of the whole first epoch is fixed by `B_0`'s: toward `X_0` (`P → X → Q`, the direct realization), or away from it, reaching `X_0` through `Q_0` (`P → Q → X`: the same circle the other way round). **Which one is the plan's is fixed by the plan's first entry, not by where the cursor's image falls** (Stage 111; the image lies ahead of or behind the player by an accident of the geometry). Let `e` be the first planned entry of the first epoch that names a present surface (∞ entries and absent surfaces skipped; a pass side, a terminal, a toggler, a portal or a projective counts as much as a mirror: the planned walk fires only on an id and side match):
  - **R1.** When only one orientation of `B_0` crosses `e`'s carrier from `e`'s side, that one. A straight `B_0` meets a line carrier once per loop, from opposite sides in the two orientations, so this always decides a line-first plan: on the planned side, aim at the carrier; on the other side, aim away and come back through infinity. ∞ entries play no part.
  - **R2.** When both orientations fire `e` (a circle carrier, or a circle `B_0` in a pre-transform frame; the crossing at `e`'s own carrier through `Q_0` is the one the pole record pins there, §31.4), the one whose planned walk consumes the whole plan and reaches the cursor — decided with wide event limits (8 × each, §12.6), the plan's order being a question of geometry, not of the budget. When both or neither do and the two fire `e` on different sides of the passage of `Q_0`, the one that fires `e` before `Q_0`, or after it when the entries before `e` leave the walk behind the ray's origin (an odd number of ∞ and origin entries, §4.2): "∞ = the beam passes the frame's infinity here", positionally.
  - **R3.** Otherwise (no such entry; no orientation fires `e`, the aim parallel to its line carrier or missing its circle; both fire it alike): toward `X_0`, reversed when the first epoch's ∞ and origin entries leave the walk behind the ray's origin (an odd number of them: `via_infinity`, the rule of Stages 88 and 101; Stage 132 counts the origin entries: `[∞, O]` aims at the cursor, passes it with the entries pending, wraps, passes the player and reaches it).
  In a later epoch the projective's exit already fixes the orientation (§13.3), so an ∞ entry there is only matched by the walker. The ∞ entry must sit where the beam of its frame passes that frame's infinity `Q_j = F_j(∞)` (§12.5): the real infinity on a straight beam, the inversion's center image on a circle beam. Whether an ∞ entry at a given plan position is met depends on the geometry (the passage of `Q_j` may come after the cursor); an unmet entry shows as invalid (§13.5): a cursor passed with the entry pending is not reached. With a pre-transform (§9.5) `Q_0` is finite and `B_0` is already a circle through it. The decisions are the walker's own predicates (the carrier's roots on `B_0`, the side at a crossing, the projective order with the pole; for a straight `B_0` and a line carrier the one sign `dot(X_0 − P, grad)`, from the homogeneous aim image with rigorous bounds and exactly when they straddle zero), never an epsilon. *Oracle:* `trace2.realization`, `first_entry`, `via_infinity`, `beam_through`; fixtures `bench/fixtures/stage88_infinity.json`, `stage101_infinity_point.json`, `stage111_orientation.json`.

Each `B_j` is built **independently** (`BeamSolver.AnchorChain`). It is swapped in when plan entry `j` fires on the anchored path (§12.1), so planned-path errors never accumulate along the plan. A mirror-only plan (line reflections and translation-only portals) has an exact rational aim image. `B_0` is then an **exact line** (§31.4).

### 13.2 Transformative sub-chains

For a contiguous sub-sequence of transformative entries, the anchored construction of §13.1 *is* the sub-chain solution. No bounce points are computed in advance. The walker reaches each planned surface's carrier as an ordinary event on the anchored beam, and fires the entry on an ID and side match (even off-segment, §11.5).

*Historical:* the legacy `Planner` computed the image chain with `MobiusTransform`s in the normalized frame, through a `TransformCache` (`plan_transformative_subchain`). It intersected the aim line with each carrier to precompute bounce points, and then traced the plan as a separate PLANNED pass in normalized coordinates. It was deleted in Stage 84.

### 13.3 Projective break points

> **Status: implemented (Stage 87).** In PLANNED mode a projective plan entry is applied, even off-segment.
>
> **Back-propagation** happens on the carrier.
> - A line normal has one candidate H; so does a directional on a line.
> - A circle normal has two candidates, c ± R·u; so can a directional on a circle.
> - **Forks (the rule of Stage 114, for every fork present and future):** two candidates are a fork, settled at planning time. The candidates on the segment are preferred; among what remains, the one nearer the point the player clicked is taken (§4.2), and without a click the segment's midpoint stands in; with no candidate on the segment the nearer off-segment one is taken (a planned-only firing). There is no search: one deterministic path. Whether a candidate is on the segment is the segment predicate on it -- exactly on the data when the candidate is a data point (K11), by rounding otherwise (a rounded antipode a hair inside or outside an arc's end: a measure-zero crack, except for an exact semicircle, whose antipodes are both data points).
> - When the target itself lies on the carrier (provenance: a later projective entry on the same surface, or the cursor exactly on it), the candidate at the target **is the target, exactly** -- never its rounded recomputation -- and the fork rule chooses between it and the other. (Before Stage 114 it was excluded.) A cursor on the segment is thus its own hit point when the click prefers it, and reached at once (K12).
> - An entry with no candidate is skipped by the aim.
> - **Exact hit points (Stage 113, K11).** When the target behind the entry is exact data (the cursor, or its exact image through later entries), whether `H` is a **data point** of the segment (an endpoint, the via) is decided by an exact predicate on the data, never by the rounding of `c ± R·u`: the foot of the target on the line (line normal: `dot(T − D, E − S) = 0`), a data point collinear with the target and the exact centre (circle normal), a data point `T + s·n` (directional). `H` is then that point exactly, a known point of the beams that reach it (§31.3), so the physical hit is at the endpoint itself: blocked on one side, it does not fire (§11.1), while the planned walk fires the entry there: the walks diverge. The aim image behind such an entry stays exact data (`exact_image` runs through it), and its exit line is exact. **Stage 125:** when no data point is the hit point, `H` computed exactly from the exact target -- a line normal's foot on its line, a directional's crossing of its line carrier (rationals; a circle carrier is not extended) -- is exact data too when both its coordinates are binary floats exactly (a cursor straight across an axis-aligned mirror, say): `H` is then that point exactly, as a data point would be. *Oracle:* `trace2._exact_data_hit`, `_exact_float_hit`, `_backward`; fixtures `bench/fixtures/stage113_exact_hits.json`.
> - **The cursor at the departure point (Stage 113, K12; every projective since Stage 114).** A cursor exactly on a projective's segment that is its own hit point (always, for a line; by the fork rule, for a circle): the sub-chain after the entry is the exit beam alone, and the cursor is reached at once on it (as at any departure point, K7).
> - **A beam through an inversion's centre (Stage 114, K15).** A circle normal's exit beam passes the circle's centre, exactly, whatever the rounding of its hit point (the beam carries that provenance). An inversion centred on that same point (exactly, on the data) maps the line to itself, straight, the centre becoming the frame's image of infinity; the rounded beam inverted numerically is a circle of enormous radius whose wrap overflows, which the walker never builds.
> - **Two projectives at one point (Stage 113, K14).** When the planned walk fires one projective and the physical walk another at the same point (both carriers through it), the frames after it differ: an epoch is identified by its firings (§10.7), so the walks are divergent there and the cursor beyond is not visible. Counting epochs read the two as one frame.
>
> **Firing:** the planned trace fires at the first crossing that matches id and side. If that crossing is not H, the anchored path ends there and the trace continues by the physical exit rule. That plan then does not reach the cursor.
>
> **Exit direction:** the sub-chain after a projective runs along that projective's exit. When the exit points away from the target, the target is reached through infinity.
>
> **Oracle:** `docs/research/oracle/trace2.py` (`back_propagate`).

Projective surfaces are **break points** that partition the plan into transformative sub-chains. At each break point:

1. Back-propagate: find the hit point from which the projected beam reaches the current target.
2. That hit point becomes the new target for the preceding sub-chain.
3. The provenance stack clears and a new epoch begins (§10.7).

Back-propagation per effect:

| Effect | Given target T, find hit point H |
|--------|----------------------------------|
| Line normal | H = orthogonal projection of T onto the surface line. |
| Circle normal | H = intersection of line(center, T) with the arc. |
| Semi-circle directional | H = intersection of line(T, −normal) with the arc, where normal is perpendicular to the segment's diameter line. |

The hit point is taken on the **carrier**, on or off the segment (Stage 113, B8; as implemented since Stage 87): the planned walk fires the entry there even off-segment (§11.5) while the physical walk passes, so a cursor whose hit point falls off the segment diverges at `H` and lies outside the visibility region (§15). `back_propagate()` returns null only without a carrier-level candidate: a target at a circle normal's centre, or a directional normal parallel to its line carrier (§23).

Back-propagation targets are in **visual space**, like everything in the beam model (§6.1).

### 13.4 Mixed chain algorithm

> **Status: implemented (Stage 87)** as anchored sub-chains (§13.1), one per epoch, in `BeamSolver.AnchorChain.build`:
> - **Backward pass:** it back-propagates each projective's hit point H from the target behind it.
> - **Sub-chains:** each is anchored from its origin (the player or the previous H) to its target (the next H or the cursor).
>
> The pseudocode below is the original design sketch; read `plan_transformative_subchain` as that anchored construction.

```
func plan_mixed(origin, cursor, plan, game_state):
    # plan is Array[{surface: Surface, side: Side}]
    
    # --- Pass 1: Backward geometry ---
    # Walk the plan in reverse. Record sub-chain boundaries.
    real_target = cursor
    sub_chains = []              # list of {entries, target}
    transform_buffer = []
    
    for i in reverse(range(len(plan))):
        entry = plan[i]
        side_config = entry.surface.active_side_config(entry.side, game_state)
        
        if side_config.effect != null and side_config.effect.is_transformative():
            transform_buffer.insert(0, entry)
        
        elif side_config.effect != null and side_config.effect.is_terminal():
            # Terminal: the arrow stops here. Clear post-terminal entries.
            transform_buffer = []
            # closest point of the terminal segment toward real_target (design sketch)
            terminal_point = closest_point_on_segment(entry.surface.segment, real_target)
            real_target = terminal_point
            sub_chains.insert(0, {entries: [], target: terminal_point, is_terminal: true})
        
        elif side_config.effect == null:
            # Pass-through: still produces a step (ensures 1:1 index correspondence).
            pass
    
    # Save the final sub-chain (from origin to real_target).
    if transform_buffer:
        sub_chains.insert(0, {entries: transform_buffer, target: real_target})
    
    # --- Pass 2: Forward origin fill ---
    current_origin = origin
    all_steps = []
    
    for sc in sub_chains:
        subpath = plan_transformative_subchain(      # anchored construction, §13.1
            current_origin, sc.target, sc.entries)
        all_steps.extend(subpath.steps)
        current_origin = sc.target
    
    return PlannedPath(all_steps)
```

### 13.5 Plan validation

A plan is valid for a given origin and cursor if all back-propagations succeed, all image-chain legs cross their surfaces, and the aim ray reaches the first planned surface.

When invalid, the preview shows the partial path up to the first failure.

An empty plan is valid — it means 'fire straight in the aim direction.' A plan consisting only of interactive terminal entries is valid — the trace ends at the first terminal.

### 13.6 Side in planning

Each plan entry is `{surface, side}` — the side is selected by the player at plan-construction time (§4.2) and is **fixed** for that entry. This eliminates a potential circular dependency: the effect (and its map) depends on the side, and the image-chain geometry depends on the effect. Because the side is known before the trace runs, the effect is unambiguous at every step.

The solver never guesses or resolves the side. It reads `entry.side` (`BeamSolver.PlanItem.left`) and looks up that side's effect. In PLANNED mode an entry fires only on a surface ID **and** side match. If the side's effect is not transformative (pass-through, terminal, or projective until Stage 87), the entry is consumed with no beam change and no stack change, and the anchored aim skips it. PHYSICAL mode does its plan bookkeeping by surface ID only (§14.1).

### 13.7 State simulation during planning

> **Status: implemented (Stage 85).** The anchored construction resolves every plan entry under the flags left by the entries before it.
> - An entry that resolves to a pass is consumed without a frame change, and the cursor becomes reachable on the same beam.
> - The physical trace stays on the anchored path only while it fires the same effects and state changes as the plan.

Before computing the backward image chain, the planned trace walks the plan **forward** and applies state changes to a **temporary copy** of the game state. This ensures that later plan entries see the post-effect state of earlier entries. For example, if plan entry 1 breaks a surface (state change), and plan entry 2 relies on that surface being broken (conditional effect), it correctly sees the broken state when evaluating entry 2.

The temporary state is discarded after planning — it does not affect the actual game state until the shot is fired and the physical trace runs.

### 13.8 Planned trace as Steps

The planned trace is the same rule walker as the physical trace (§12.1), with `mode = PLANNED` up to the cursor. After the cursor it is a PHYSICAL continuation. Both are converted to `TraceStep`s by the same `BeamTracer.to_path`. Every carrier crossing was an event in both modes, so the two event lists corresponded 1:1, with the same points, **up to the first step where they apply different effects**, and the step tree merge compared frame ids at each index. Since Stage 134 each walk computes only its candidates (§12.1), and the merge pairs the paths by segments between significant events (§14.5).

When the two traces make identical decisions through the cursor, `BeamTracer` shares one trajectory: the planned path *is* the physical path, with the same steps.

---

## 14. Step system

### 14.1 Two trace modes — unified hitpoints

Hit-point computation uses a **unified hitpoint model**: both modes intersect the current beam with ALL surface carriers (on- or off-segment), and every crossing is an event (§11.5). The modes differ ONLY in which effects are applied at each hitpoint (`TraceMode` / `BeamSolver.Mode`):

| Mode | Effect application rule | When it runs |
|------|------------------------|-------------|
| **PHYSICAL** | An effect fires ONLY on a **solid** hit (§11.1): an on-segment interior point, or a shared point (a lone endpoint grazes). A hit at a shared point where two or more surfaces have a consequence is terminal, nothing firing (Stage 138). A terminal ends the trace. Everything else is a pass-through (no effect, no stack change). Plan bookkeeping is by surface ID: an event that changes the frame (an applied transformative or projective effect, or a state change that changes the flags) advances the plan if it is the next entry's surface, and otherwise marks the plan unmatched. An event at which the planned walk consumes the next entry without changing the frame (a pass side, a terminal crossed off its segment, a write of the value a flag already has) is consumed here too (B1, Stage 110). At a point where several surfaces are crossed together (one carrier, K6), an out-of-plan event that only changes the flags is matched by the frame: if the walk leaves the point in the flags the planned walk leaves it in, the planned entries there count as consumed, whatever the order of the surfaces (B5, Stage 110). Since Stage 138 a point held by two surfaces with a consequence stops the arrow (§11.1), so the matching by frame remains where one surface fires at the point and the planned shadow holds the others (an absent surface, §9.6). | Origin to terminal/escape/limit. |
| **PLANNED** | The next plan entry fires when surface ID **and** side match, even off-segment (at a shared point too, §11.1: the physical walk stops there, so the walks diverge). Only transformative entries change the beam. Projective entries are applied too (Stage 87). Terminal entries are consumed without effect. Everything else passes, terminals included. After the cursor, the trace continues with PHYSICAL rules. | Origin to terminal/escape/limit. |

Neither mode stops at the cursor. The cursor is a **virtual hitpoint** (§12.1) that competes with carrier intersections in projective order. It is a candidate only once the plan is complete and matched. `cursor_index` is the step index where the cursor falls.

Up to the first index where the two modes apply different effects, both traces walk the same beam (the anchored planned path, §13.1) and produce the same events. Their step arrays therefore correspond **1:1 by index** with identical `start`/`end` there. After that index they continue independently.

Both modes use the same solver, the same scene defs and the same scene state. `PrecisionLadder` lets planned share physical's precision climb when they agree through the cursor, and `BeamTracer` caches the float64 scene across frames and the last pair (§14.8).

### 14.2 Divergence: exact definition

Divergence is defined by comparing the PLANNED and PHYSICAL traces step by step. Before divergence both modes produce the **same hitpoints** (§14.1), so steps at the same index have the same `start` and `end`. The field compared is the **frame id**: the id of `(epoch, provenance stack, flags, on the plan)`, §10.7.

**On the plan** (Stage 129). A walk leaves the plan at the first hit that fires out of plan while plan entries remain: an effect applied (or a state change) at a surface that is not the next plan entry, or at the next entry's surface outside its planned piece (§9.7). From that hit on, its frame says so. The planned walk never leaves the plan, since it applies only its entries until they are spent. So a physical walk that fires out of plan diverges at that hit, even when it applies the same effect to the same beam as the planned walk: another piece of the planned design surface. A spent plan is not left: the hits after it (after the cursor, or the physical walk's after its last entry) change no frame's "on the plan".

**Aligned** — step `i` is aligned if the planned and physical traces have the same **frame ID** at that step. This means both modes applied the same effects up to and including step `i`.

**Diverged** — step `i` is diverged if the frame IDs differ. This means a surface effect was applied in one mode but not the other (e.g., physical hit was on-segment but planned didn't have it in the queue, or vice versa).

Once divergence occurs, the traces never re-converge — divergence is permanent for the remainder of the shot. *(Principle 12.)* After divergence the two traces are on different beams, so their subsequent hitpoints differ. Each is still computed by the same rules within its own mode.

### 14.3 Divergence examples (non-exhaustive)

The following are common scenarios that trigger divergence, but they are all instances of the two cases above:

- Physical ray hits a different surface before the planned one (case 1: different hit point).
- Physical ray hits the planned surface on a different side, triggering a different effect (case 2: different frame id after the hit).
- A projective effect's back-propagation was valid in planning, but the physical ray arrives at a different point on the surface.
- Circle inversion curves the physical ray into missing a later planned surface.
- The planned path uses the infinite carrier extension of a surface, but the physical ray hits the segment's finite edge first.
- The physical ray hits the planned design surface outside the planned piece (§9.7): the same effect on the same beam, but out of plan (the frame's "on the plan" part). Both paths are drawn diverged from that hit on, although they coincide.

### 14.4 Step tree data structure

The step tree is **not** a recursive tree. It has at most one branch point (the divergence). It is a struct:

```
StepTree:
    planned_steps:    Array[Step]    # the full planned trace
    physical_steps:   Array[Step]    # the full physical trace
    divergence_index: int?           # index of first divergence (null if fully aligned)
    merged_steps:     Array[Step]    # the merged view with 5 step types (built by merge algorithm below)
```

### 14.5 Step tree merge algorithm

**Stage 134 (the user): by segments.** The two walks compute different candidates (§12.1), so their steps no longer correspond one to one. Each path is cut into **segments** between significant events -- the planned path's cursor is a boundary too -- and a segment is drawn as one stroke. The merge pairs the segments in order: a pair is aligned while both end at the same significant event (the same point, surface, side and decision) in the same frame. At the first pair that is not, the two run together from their common start to the nearer of their ends -- aligned -- and diverge there: the rest of each is diverged. Every other event either walk computed is a dot on the drawn path, coloured by the walks that computed it (§4.3; since Stage 139 an off-segment crossing of an unplanned surface is no event: no dot). The rest of this section (the algorithm by index, Stage 13) is history.

The merge (`StepTreeMerge.merge`) walks both traces **by index**, up to the longer of the two. Before divergence, steps at the same index have identical `start` and `end` (§14.1). The merge compares only the **frame ID** at each step. A missing step on either side counts as divergence.

```
func merge(planned_steps, physical_steps, cursor_index) → Array[MergedStep]:
    merged = []
    diverged = false
    
    # The arrays may differ in length after divergence; a missing step is null.
    for idx in range(max(len(planned_steps), len(physical_steps))):
        p = planned_steps[idx] if idx < len(planned_steps) else null
        r = physical_steps[idx] if idx < len(physical_steps) else null
        past_cursor = (idx >= cursor_index)
        
        if not diverged and p != null and r != null and p.frame_id == r.frame_id:
            # Aligned — same effects applied (or both passed through).
            merged.append(MergedStep(p, ALIGNED if not past_cursor else ALIGNED_POST_PLANNED))
        else:
            # Diverged here or earlier — all subsequent steps are diverged.
            diverged = true
            if p != null:
                merged.append(MergedStep(p, DIVERGED_PLANNED if not past_cursor else DIVERGED_POST_PLANNED))
            if r != null:
                merged.append(MergedStep(r, DIVERGED_PHYSICAL))
    
    return merged
```

The merge is trivial because hitpoints are mode-independent up to divergence. No partial alignment or sub-segmentation is needed. *(§14.8.)*

After divergence, the planned and physical traces are on different beams, so their subsequent hitpoints differ. Both traces continue independently, and the merge emits both as diverged steps.

### 14.6 Five step types

| Type | Meaning | Visual (§4.3) |
|------|---------|---------------|
| **ALIGNED** | Planned and physical agree, before cursor. | Solid green |
| **ALIGNED_POST_PLANNED** | Still aligned, past cursor. | Dashed green |
| **DIVERGED_PHYSICAL** | Where the physical arrow actually goes after divergence. | Dashed yellow |
| **DIVERGED_PLANNED** | Where the plan says the arrow should go, but the physical arrow diverged. | Solid red |
| **DIVERGED_POST_PLANNED** | Post-cursor continuation of the planned path, after divergence. | Dashed red |

**Visual invariants:**
- A **solid path** always leads from the player to the cursor (ALIGNED + DIVERGED_PLANNED).
- A **dashed path** always shows post-cursor or post-divergence continuation.
- The **physical path** (green + yellow) never contains red — red shows only where the plan *would have* gone.

### 14.7 Step record

```
TraceStep:
    start, end:     Vector2      # visual coords (INF, INF for the far end of an escape step)
    via:            Vector2      # a point on the beam between start and end (escape steps: travel direction)
    is_arc_step:    bool         # the beam's curvature κ ≠ 0
    frame_id:       int          # id of (epoch, provenance stack), §10.7; 0 = IDENTITY_FRAME
    ray:            Ray          # the trajectory's aim ray (player → cursor), shared by reference
    hit:            TraceHit?    # set on the last step of a hit event (§11.2); null otherwise
    surface_id, hit_side, hit_on_segment     # the hit's metadata (-1 / unset when no hit)
    after_portal:   bool         # first step after a portal (intended gap)
    est_error:      float        # estimated px error of `end` (-1 = unknown)
    precision_bits: int          # arithmetic precision the step was computed at
    type:           StepType     # ALIGNED, ALIGNED_POST_PLANNED, DIVERGED_PHYSICAL,
                                 # DIVERGED_PLANNED, DIVERGED_POST_PLANNED
```

**Shared aim ray:** every step of one `trace_pair` references the same aim `Ray` object (player → cursor). It is provenance for consumers: nothing re-intersects it, because the solver's events are final. Escape steps instead carry a ray along their exact tangent. *(Removed in Stage 84: `TraceStep.frame`, the per-step Möbius frame.)*

```
TracePath:
    steps:        Array[TraceStep]
    targets_hit:  Dictionary[int, bool]   # target surfaces hit on-segment during this trace
    cursor_index: int                     # step index of the cursor (-1 if not reached)
    hit_count:    int                     # number of solver events
    # final_state: the physical trace's final flags (BeamTracer.Pair.final_flags; Stage 85)
```

`VisualPath = Array[VisualLineSegment | VisualArcSegment]`, plus escape segments drawn from `start` along a direction to the viewport edge.

The step tree is the **single source of truth** for what happens on a shot. *(Principles 2, 15.)*

### 14.8 Mode interaction and caching

The PLANNED and PHYSICAL traces are solved together (`PrecisionLadder.solve`) and converted together (`BeamTracer.trace_pair`):
- **Frame ids** come from one `(epoch, stack) → id` dictionary shared by both conversions. The same applied effects since the last projective therefore give the same id in both traces, and aligned steps have identical frame ids by construction.
- **One trajectory:** when the two event lists agree (kind, surface, side, applied) through the cursor, the planned path reuses the physical one.
- **Caches:** `BeamTracer` caches the float64 `FastSolver.Scene` across frames (keyed by the scene defs) and the last `(player, cursor, plan, scene)` → pair. `PrecisionLadder` lets planned share physical's precision climb when they agree through the cursor.

**Precondition:** both traces start in frame 0 (empty stack, epoch 0) from the same player and aim.

*(Historical: the legacy design aligned frames by `MobiusTransform` IDs from a shared `TransformCache`, §17.)*

### 14.9 Empty-plan step tree

With an empty plan the plan is complete from the start, so the cursor is a candidate immediately. The PLANNED trace applies no effects before the cursor: it is the straight aim line from the player to the cursor, and it continues with PHYSICAL rules after the cursor. The PHYSICAL trace (no anchored chain: a forward walk from the aim) applies effects normally.
- If the physical arrow reaches the cursor without an applied effect, the two traces coincide and every step is **ALIGNED** (before the cursor) or **ALIGNED_POST_PLANNED** (after it).
- If the physical arrow applies an effect (or is blocked) before the cursor, its frame id or event list departs from the planned one there. The merge marks that index as the divergence: the red solid planned line continues straight to the cursor (UX11, "empty plan = fire straight").

### 14.10 Post-cursor computation

There is no separate post-cursor computation. Both traces run the full path from origin to escape/block in a single pass. The cursor is a virtual hitpoint (§12.1) that creates a step boundary. `cursor_index` is the step index at the cursor position. Steps before `cursor_index` are pre-cursor; steps at or after are post-cursor. The step type (ALIGNED vs ALIGNED_POST_PLANNED, or DIVERGED_PLANNED vs DIVERGED_POST_PLANNED) is determined by comparing against `cursor_index`.

---

## 15. Visibility

> **Status: implemented (Stage 110, `docs/TDD_14_VISIBILITY.md`; projective plans Stage 113, `docs/TDD_18_PROJECTIVE_VISIBILITY.md`)** for every plan. The native construction is `rc/visibility.hpp`, its invariants `rc/visibility_checks.hpp`, the drawing `scripts/visual/visibility_renderer.gd`.

### 15.1 Purpose and definition

The visibility region highlights the **set of cursor positions that will not cause divergence** for the current plan: if the cursor is placed there, the planned and the physical paths agree up to it. *(Principle 14: visibility shares the world.)*

**Definition.** A cursor is in the region iff (V1) the preview's planned path reaches it, (V2) every merged step before it is ALIGNED (§14.2: the same frame ids, epoch, stack, flags and on the plan; an epoch is its projective firings, §10.7), and (V3) both walks leave the cursor's own point in the same frame. **Stage 134:** the walks compute different candidates (§12.1), so they are compared by their **significant points** -- the drawn points where a run of events holds a significant event (§12.6), each with the frame the run leaves it in (surfaces crossed at one point in another order leave it in one frame: B5; since Stage 138 a point held by two surfaces with a consequence stops the physical walk, §11.1): (V1) both walks reach the cursor (a physical walk that ends at a terminal at the cursor's own point reaches it), (V2) with the same significant points before it, and (V3) leave its point in one frame. A state change no entry plans is fired by the physical walk only -- a planned walk waiting on its entries computes nothing else -- so before the cursor it is a divergence, like an unplanned infinity (the user, Stage 134). V1 and V2 are what the preview shows: an all-green solid path from the player to the cursor. V3 counts the decisions made exactly at the cursor, which fall between the last step before it and the first after it (B4, Stage 110). When the physical walk has no event at the cursor's point, it passes the point in the frame it had. The region never depends on the cursor, only on the player, the plan (with its clicks and pieces), the game state's flags and the surfaces.
- A cursor that the trace never reaches (cut by the event limit, §12.6) is **outside**.
- A cursor on the player (no aim), on the player's image `P_K` or on the frame's infinity `Q_K` is **not classified**: the aim degenerates there (§11.4). After a projective the poles are the pencil's (§15.2): the image of a circle normal's centre (every exit line passes it) and the frame's infinity.
- A cursor exactly where the last entry is crossed off its segment is **outside**: the planned walk applies the entry there and the physical one passes, so they leave the point in different frames (V3). Before B4 these departure points formed thin lines along the planned carrier.
- The only 1D parts of the region are **whiskers**: along a member tangent at a frame's finite infinity point to a carrier through it (every other member crosses that carrier there and diverges, §11.4); on the last planned entry's segment when that side is a terminal (the arrow ends exactly at the cursor). (Stage 129's third kind, along a member whose walk met a planned piece exactly at an end where another surface cuts it, is gone with Stage 138: that end is a shared point, where the physical walk stops and the walks diverge.) A whisker's end at exact data (a segment endpoint) belongs to the region exactly when the preview reaches a cursor there: at a planned terminal's lone endpoint the hit does not fire physically, so the walks diverge and the endpoint is outside (K7).

### 15.2 Members

Every cursor whose aim at the player has the direction `d` lies on one beam of the plan's final frame: the circle through the player's image `P_K = F_K(player)`, the cursor and the frame's infinity `Q_K = F_K(∞)` (§13.1), run the way the plan's realization runs. It is a line when `Q_K` is infinity. This beam is the **member** of `d`. Every point of the plane lies on one member, on the arc from `P_K` to `Q_K` that `d` **owns** (the other arc belongs to the opposite direction).

**After a projective (Stage 113).** The last epoch's beams are straight lines from the last projective's hit point `H` (§10.4, §13.3), one per `H` on its carrier, and the last epoch's transformatives map them to the final frame: a **pencil** (`Builder::PencilKind`), keyed by a real parameter, not by the aim:

| Projective | The exit lines (the epoch's own frame) | The pencil | The key |
|---|---|---|---|
| Line normal, line directional, circle directional | all parallel (to the carrier's normal, or to the given normal `n`) | *parallel*: one pole, the frame's infinity `B = F'(∞)` (circles tangent there to `F'_*(n)`, lines when `B` is infinity) | the offset of `H` across the family's direction |
| Circle normal | all through the centre `c` (converging and expanding alike) | *radial*: the poles `A = F'(c)` and `B = F'(∞)` (today's `P_K`/`Q_K` shape with `A` as `P_K`); an inversion centred on `c` after the projective sends `c` to infinity, and the pencil keeps its shape with the poles swapped (`A = ∞`, `B = F'(∞)` finite) | the line's direction, a diamond pseudo-angle folded to a half turn |

A cursor's member is the line through its back-propagated hit point. The candidate of a circle (two per line, §13.3) is a property of the line, not of the cursor's place on it: the pair `c ± R·u` is the same for every cursor on a radial line, and every chord of a directional circle shares one perpendicular bisector. A **walk owns its whole member**: from `H` along the exit, through the frame's infinity, and round to `H` (there is no opposite aim). A directional normal along its own line carrier has no hit point at all: the region is empty.

The members through a point of an earlier epoch are found by **pushing it through** the projectives: the earlier epoch's member through the point, mapped to the frame of the next projective, cut by its carrier (two points at most), and so on. The fork line of a circle normal (through `c`, across the click, §13.3) is **two members**, one per candidate, with the sheets they bound. The radial pencil's cells come in two **sheets** that wind opposite ways (toward the centre and away from it; a sheet beyond the wrap through infinity likewise), merged and checked apart (`Cell.wind`).

On its member, the visible cursors form intervals of the walk:
- from the departure point of the last plan entry (the player, for an empty plan; `Q_K` after an infinity entry; the ray's origin after an origin entry: `P_K`, or its own point after a line normal or directional projection, Stage 132);
- to the first event at which the physical walk changes the frame (the **stop**: an applied effect, or a state change that changes the flags) or passes the frame's infinity or the ray's origin (unplanned past the last entry: a divergence), or to the last event the event limits leave (§12.6; the cursor counts toward neither). Every loop passes infinity and the origin, so the stop comes within the walk's first pass: nothing reads laps (Stage 134, the user). A return to the beam's departure (after an origin entry the walk starts at the origin, not there) is a plain stop;
- inside the owned arc.

Ties are the solver's: the cursor comes before a crossing at its own parameter, so a stop is visible.

### 15.3 Construction

The region is built from **casts**: ordinary pair traces (`Tracer::solve`, the same ladder as the preview) whose cursor lies on a member, read by their events. Nothing is re-derived: the departure point, the stop and the budget are the solver's events.

1. **Critical members.** A walk's shape can change only where its member passes:
   - a segment endpoint;
   - a tangency to a carrier;
   - a proper crossing of two segments (§9.7);
   - a planned piece's end;
   - the tangency at a finite `Q` of a carrier through it (§11.4);
   - for a finite `Q_K`, the line through `P_K` and `Q_K`.

   These points, in every frame of the plan, are mapped to the final frame. Each bounds two members, one each way along its line, since a walk passes the point after its wrap too. With no plan in the direct realization, the second way is not needed. The axes keep every gap below a quarter turn. Members are ordered by a pseudo-angle of `d`, a monotone key against the fixed reference +x with no trigonometry (§31.2).

   **Spared points** (Stage 140, the cast-cost program's Stage D). The points of a surface that is **never a candidate** -- a pure pass-through in every state (both sides pass, no state change, no landing effect, in every table row) and not a target -- that **no plan entry names** are no members: no walk computes it (§12.1), so no walk's shape changes at its endpoints, its tangencies or its crossings with other carriers, and the cells either side of such a member would be one cell. The level's bounds, drawn as pass-through lines, are the usual case.

   **A plan the player cannot start** (Stage 140, F28). With no pre-transform the physical walk from the player `P` is a straight beam, and before the plan's first surface entry every other event -- another surface's effect or state change, an unplanned passage of infinity -- is a divergence (§14.2): the entry must be the beam's first crossing of its carrier, approached from `P`'s side (from the far side after an odd number of leading infinity entries). When that is impossible the region is empty and nothing is cast: a line carrier with `P` strictly on the other side; a circle with `P` strictly inside and the entry its outer side (a straight beam leaves a circle once, from inside); a full circle with `P` strictly outside, the entry its inner side, and the outer side applying an effect (the first crossing is a divergence). Decided exactly on the data (§31.3), never when `P` lies on the carrier; arcs with `P` outside, pre-transformed scenes and the other cases build as before.
2. **Cells.** Each member is cast, and so are its two sides (±ε), and each gap between members is cast in its middle. Where a gap's casts disagree on the walk's shape, it is bisected and a member is inserted at the switch. A cell is a curvilinear quadrilateral: its two side members, the carrier arc its stops lie on, and the carrier arc its departures lie on. Its corners are where each side member crosses those carriers. Every cell that shares a corner computes it the same way, so shared corners are equal.
3. **Clipping.** A cell through infinity runs outside the clip rectangle. Every cell is cut by the rectangle (which holds the level bounds grown by 50 and every finite corner), walking its boundary the way the cells wind. Each reflection or inversion of the final frame reverses that way, and so does the realization through infinity.
4. **Regions.** Adjacent cells merge: shared member pieces cancel, and so do carrier pieces shared by two layers of one walk. What remains is chained into simple loops. A loop through one vertex twice (a pinched region) is cut there. A **dead member between two live cells** (the aim exactly tangent to the projective's circle at the member's hit point, Stage 113) is a **crack**: its pieces stay as a boundary, one edge each way, never inclusive.

### 15.4 Regions and membership

```
VisibilityRegion:
    loops:    closed loops of edges (member arcs, carrier arcs, the clip rectangle)
    whiskers: the region's 1D parts (no area; see 15.1)
    cells:    the cells (a partition of the region; the checks read them)
    fill:     one triangle mesh of the cells, tessellated conformingly
    outline:  the union of the cells (outer rings and holes) and its triangulation -- what is drawn (Stage 123)
    contains(c) -> Inside | Outside | Unclassified
```

`contains` is the even-odd rule over the loops. It is half-open at the test ray's height (arcs are cut into pieces monotone in y). A point on a boundary edge takes the edge's flag:
- **stop and departure arcs** are inclusive;
- **a member piece** is inclusive where that member's own cast sees it (a point of interest on the projective's own carrier aims at the other candidate: its own cast decides a cursor exactly there, the member's walk is cast from a cursor along its exit);
- **a crack** is never inclusive.

A vertex at a member's own point of interest takes the solver's answer for a cursor exactly there. Cursors outside the level bounds grown by 50 are not classified.

**The boundary band (Stage 114, K13).** The rules above apply to a cursor *exactly* on an edge or at a vertex. A cursor within a float32 cursor's rounding of the boundary (4 ulps of its coordinates) but not on it is **not classified**: a computed boundary -- a tangency, a refinement, a fork, a point pushed through a projective -- is placed by rounding, and the solver decides such a cursor by the rounding of its own hit point. The region's members are placed where the solver's own answer switches (the solver's choice of orientation on a split plan is part of a cast's shape, so a refinement finds that switch; a point of an earlier frame that lies on a projective's carrier is that carrier's hit, never a tangent beam's double root), so beside the band the region and the solver agree, and the band itself is below a pixel. A switch within a bounding member's resolution is that member's own sliver: the gap is one cell.

### 15.5 Ordering

Members are ordered by their direction's pseudo-angle against +x, a real key and a strict order. All geometric decisions inside the solver remain cross-product signs (§31.2). The region's own geometry (circle centres, crossings with the clip rectangle) is computed in float64 from each carrier's canonical data, so equal inputs give equal points.

### 15.6 Consistency and invariants

Visibility uses the **same casts**, the same scene defs, the same flags and the same ladder as the preview. *(Principle 14.)* `VISIBILITY-EQUALS-NON-DIVERGENCE` pins the region against the preview cursor by cursor (§29.3, UX1 and UX2). The structure invariants VIS-* hold for every region: valid, non-overlapping polygons, and every edge either a member (a beam through the player's image) or a piece of a surface's carrier. The oracle (`docs/research/oracle/visibility.py`) states the definition at 50 digits and labels cursors that the native region and traces must match.

---

## 16. Worked examples

### 16.1 Simple two-mirror reflection

**Scene:** Two line-segment surfaces (mirrors) and a target.

```
Surface A: start=(100, 400), end=(100, 200), via=(100, 300)
    Carrier: vertical line x=100. Left side: Reflection. Right side: null.

Surface B: start=(300, 100), end=(500, 100), via=(400, 100)
    Carrier: horizontal line y=100. Left side: null. Right side: Reflection.

Target T:  start=(450, 350), end=(500, 350), via=(475, 350)
    is_target=true. Both sides: pass-through.

Player:    position=(50, 450)
Cursor:    (475, 300) — on target T
Plan:      [{A, left}, {B, right}]
```

**Planning (backward image chain):**
1. Start with cursor C = (475, 300).
2. Inverse-transform through B (reflect across y=100): image₁ = (475, -100).
3. Inverse-transform through A (reflect across x=100): image₂ = (-275, -100).
4. Aim direction: player (50, 450) → image₂ (-275, -100).

**Bounce points:**
- Leg 1: line from (50, 450) toward (-275, -100). Intersect with carrier of A (x=100) → bounce₁ = (100, 367).
- Leg 2: line from bounce₁ through image of cursor after partial unfolding. Intersect with carrier of B (y=100) → bounce₂ = (340, 100).
- Leg 3: line from bounce₂ to cursor → arrives at (475, 300).

**Step tree (all aligned):**
All three legs match in PLANNED and PHYSICAL modes → 3 steps of type ALIGNED.

### 16.2 Circle inversion

**Scene:** One arc surface with circle inversion.

```
Surface C: start=(200, 100), end=(200, 300), via=(300, 200)
    Carrier: circle centered at (200, 200), radius=100.
    Left (outer) side: Circle Inversion. Right (inner) side: null.

Player:    position=(50, 200)
Cursor:    (400, 200)
Plan:      [{C, left}]
```

**Planning:**
1. Inverse-transform cursor (400, 200) through inversion at center=(200,200), r=100.
   Using the general formula: `w = center + r² · conj(z - center) / |z - center|²`.
   `z - center = (400-200, 200-200) = (200, 0)`. `|z - center|² = 40000`. `conj(z - center) = (200, 0)`.
   `w = (200, 200) + 10000 · (200, 0) / 40000 = (200, 200) + (50, 0) = (250, 200)`.
2. Aim: player (50, 200) → image (250, 200). This is the horizontal line y=200.
3. Bounce: line y=200 intersects carrier circle at (300, 200) → bounce₁ = (300, 200).

**Beam after hit:** the inversion beam map (§10.8) at the bounce point gives the new beam, which departs the carrier there; inversion C is pushed on the provenance stack. In the anchored construction (§13.1), B₁ is the generalized circle through P₁ = F₁(player) ≈ (133.3, 200), X₁ = cursor (400, 200) and Q₁ = F₁(∞) = center (200, 200). These are collinear because the aim passes through the inversion center, so this second leg is straight, on y = 200. In general (an aim not through the center) the second leg renders as a **circular arc**, the image of a straight line under inversion.

### 16.3 Divergence

**Scene:** A mirror and a blocking wall. The plan goes through the mirror, but a wall intercepts the physical path first.

```
Surface M: start=(200, 100), end=(200, 400), via=(200, 250)
    Left: Reflection. Right: null.

Surface W: start=(150, 200), end=(150, 350), via=(150, 275)
    Left: Block. Right: Block.

Player:    position=(50, 300)
Cursor:    (400, 300)
Plan:      [{M, left}]
```

**Planned trace:** image of cursor reflected across M (x=200) → image = (0, 300). Aim: (50, 300) → (0, 300), heading left. Planned leg crosses M at (200, 300), reflects, continues to cursor.

**Physical trace:** the ray from (50, 300) heading toward (0, 300) hits wall W at (150, 300) first — before reaching mirror M. Wall blocks. Physical path ends at (150, 300).

**Step tree:**
- Step 0: start=(50, 300), end=(150, 300). Both traces share this prefix up to (150, 300). But planned end is (200, 300) and physical end is (150, 300) — **different first hit point** (divergence case 1). Split at the shorter one:
  - ALIGNED: (50, 300) → (150, 300).
  - DIVERGED_PLANNED: (150, 300) → (200, 300) → reflected → cursor (red, solid).
  - DIVERGED_PHYSICAL: (150, 300) → blocked (yellow, dashed — the path ends here).

### 16.4 Projective break point

**Scene:** A mirror, then a line-normal-projection surface, then another mirror.

```
Surface M1: start=(100, 100), end=(100, 400), via=(100, 250)
    Left: Reflection.

Surface P:  start=(250, 200), end=(250, 350), via=(250, 275)
    Left: LineNormalProjection. (Outgoing ray ⊥ to x=250, pointing right.)

Surface M2: start=(400, 100), end=(400, 400), via=(400, 250)
    Right: Reflection.

Player:     position=(50, 300)
Cursor:     (500, 200)
Plan:       [{M1, left}, {P, left}, {M2, right}]
```

**Planning (mixed chain, backward):**
1. Start with cursor C = (500, 200).
2. M2 is transformative: reflect C across x=400 → image = (300, 200).
3. P is projective (break point): back-propagate. The outgoing ray from P must reach (300, 200). Line-normal projection: outgoing ray is ⊥ to P (horizontal, pointing right). Hit point H = orthogonal projection of (300, 200) onto x=250 → H = (250, 200).
4. M1 is transformative: reflect H across x=100 → image = (-50, 200).
5. Aim: player (50, 300) → image (-50, 200).

**Result:** two transformative sub-chains ({M1} and {M2}) separated by projective break point P. The provenance stack clears and a new epoch begins at P.

*(Status: implemented in Stage 87: the planned preview follows this path, §13.3.)*

### 16.5 State change (multi-shot)

**Scene:** A breakable wall blocking the target.

```
Surface W: start=(300, 100), end=(300, 400), via=(300, 250)
    Resolver: CategoricalResolver(
        state_key="wall_intact",
        config_table={
            true:  {left: Block + StateChange("wall_intact", false), right: Block},
            false: {left: PassThrough, right: PassThrough}
        }
    )

Target T:  start=(400, 200), end=(400, 350), via=(400, 275)
    is_target=true. Both sides: pass-through.

Player:    position=(50, 300)
Initial flags: {"wall_intact": true}
```

**Shot 1:** Player fires at the wall. Arrow hits W at (300, 300) on the left side. Block effect stops the arrow. State change fires: `wall_intact = false`. The wall is now pass-through. Target T is NOT hit. Checkpoint saved.

**Shot 2:** Player fires again toward the target. The wall is now pass-through (game state changed). Arrow passes through W and hits target T. Level complete.

This is a multi-shot puzzle where the first shot changes the game state to make the second shot possible.

### 16.6 Shot lifecycle walkthrough

End-to-end walkthrough for a single shot:

1. **Player presses Fire.** Checkpoint saved (player position, game state, plan, targets_hit).
2. **Game time freezes** (`get_tree().paused = true`).
3. **Physical trace computed** on a copy of game state. `targets_hit` updated during trace.
4. **Planned trace computed** (anchored image chain in the beam solver, §13.1; back-propagation through projectives from Stage 87). Produces planned Steps. (Steps 3–4 are one `BeamTracer.trace_pair` call.)
5. **Step tree merged** from planned and physical traces (§14.5).
6. **Arrow flight animated** step-by-step. Surfaces visually update at state-change hit points.
7. **Animation completes** (or player skips). Arrow disappears.
8. **State promoted:** the trace copy's game state and targets_hit replace the real ones.
9. **Win check:** if all targets hit → level complete. Otherwise → play phase continues.
10. **Game time unfreezes.** Player physics resume with updated state.

---

# Part V: Infrastructure

## 17. Computation cache

> **Stage 84:** the legacy `TransformCache` was deleted together with the frame-based solver. It was a provenance-keyed bidirectional store of point/carrier transforms with `MobiusTransform` IDs. Exact reversal (Principles 5, 22) is now guaranteed **by construction inside the beam solver**, not by a cache. See `docs/TDD_09_PRECISION_INTEGRATION.md` ("Exactness rules") and `docs/research/oracle/trace2.py`.

### 17.1 Reversibility guarantee

Reversal never recomputes. When a beam re-hits the surface on top of the provenance stack (or a portal's pair), the stack **pops and restores the earlier beam object**, together with its known points and exact-line data, resumed at the shared point actually hit (§10.7). The anchored planned path memoizes its image chain per stack frame, so a pop in the plan returns exactly the earlier image and beam (§13.1). This guarantees `f(f⁻¹(x)) == x` exactly where the game relies on it.

### 17.2 What gets cached

| Computation | Where |
|-------------|-------|
| Carrier derivation | `Segment.get_carrier()` memoizes `derive_carrier(start, end, via)` on the segment. The three points stay stored, so no reverse derivation is needed. |
| Earlier beams on cancellation | Provenance stack entries (`BeamSolver.StackEntry`) |
| Anchored beams / image chain per plan frame | `BeamSolver.AnchorChain` (memo by stack signature) |
| Exact incidence of known points on carriers | Per-walk dictionary (`Walk.incidence`) |
| Float64 solver scene | `BeamTracer` (keyed by the scene defs, reused across frames) |
| Last planned/physical pair | `BeamTracer` (keyed by player, cursor, plan and scene) |

### 17.3 Cache architecture

There is no global cache and no transform IDs. Identity of *frames* is the `frame_id` of `(epoch, provenance stack)` assigned per `trace_pair` (§10.7, §14.8). Identity of *exact points* is provenance: the known points of the current beam (§31.4).

### 17.4 Manual overrides

*(Not implemented; the design intent is unchanged.)* Principle 23 (designer intent over arithmetic) called for level-declared cycle identities enforced by the cache, e.g. four mirrors in a `+` shape whose clockwise cycle is the identity. With the beam solver, only a re-hit of the surface on top of the stack (or a portal's pair) cancels exactly. A cycle through *different* surfaces, such as the `+` shape, is not recognised as the identity; its accuracy is controlled by dynamic precision (§31.5). If overrides are added, they will need a beam-solver design.

### 17.5 Cache lifetime

`BeamTracer`'s scene and pair caches persist across frames until the scene defs or the inputs change (`BeamTracer.clear_cache()` resets them). All per-walk state (stack, anchored chain, known points, incidence memo) lives for one trace.

---

## 18. Scene and game state

### 18.1 Scene

```
Scene:
    surfaces:    Array[Surface]
    spawn_point: Vector2
    # Targets are surfaces with is_target = true.
```

Room edges are surfaces like any other. *(Principle 7.)*

Room boundaries are manually placed block surfaces — the engine does not auto-generate them. A level without boundaries allows the arrow to wrap through infinity (§6.3) and return from the opposite direction. Levels without any surfaces at all result in true ray escape.

Levels may also contain **collision bodies** — `StaticBody2D` nodes that constrain the player but are invisible to the arrow tracing and visibility systems (§25.1). These are not part of the `surfaces` array. Principle 7 governs the arrow/visibility systems: room edges that interact with arrows must be surfaces. Physical environment geometry that only constrains the player (floors, platforms) may be simple collision bodies.

### 18.2 Game state

```
GameState:
    flags: Dictionary[StringName, Variant]
```

Mutable facts changed by hit effects. *(Principle 10.)*

### 18.3 Scene state

Scene state = Scene + GameState. Both trajectory and visibility use the same scene state. *(Principle 14.)*

### 18.4 State ownership

| State | Owner | Mutable during | Checkpointed |
|-------|-------|---------------|-------------|
| GameState.flags | Game manager | trace (copy), shot completion (promote) | Yes |
| targets_hit | Game manager | trace (copy), shot completion (promote) | Yes |
| Plan | Player input | play phase (continuous) | Yes |
| Player position | CharacterBody2D | play phase (continuous) | Yes |
| Checkpoint stack | Game manager | after each shot | No (it IS the checkpoint system) |

---

## 19. Level design and editing

### 19.1 Level data format

A level is a JSON file, `data/levels/<dir>/<name>.json` (the full schema: `docs/LEVEL_FORMAT.md`). It is
engine-neutral and the source of truth of the level: every host reads it through the native reader
(`rcg::parse_level`), which is strict (an unknown key, a missing one or a value of the wrong type is an error
naming its path) and correctly rounded (no engine's own decimal parser is used).

Levels are written in **`rc-level/2`** (Stage 142): the level's parts, its initial flags and its **elements**.
An element is an instance of a kind -- a wall, a mirror, a door, a button, ... -- with a name, a geometry and
parameters. Every kind is a **template**, data that expands the element into surfaces (§9.1): the builtin kinds
are `data/elements/<kind>.json`, and a level may add or replace kinds with its own `templates`
(`docs/ELEMENTS.md`). New elements are designed as templates, without code, as long as they are expressible with
surfaces' path effects, hit effects, resolvers and presence (§9.4, §9.6, §10.6); behaviour beyond that is a new
primitive in the game.

```
Level (rc-level/2):
    schema:      "rc-level/2"
    name, bounds, gravity, room, player, camera, background, colliders, features   # as in rc-level/1
    flags:       {key: value}                # the flags the level starts with (default none)
    elements:    [Element]                   # in build order
    templates:   {kind: Template}            # the level's own kinds (docs/ELEMENTS.md)
    carriers:    [{name, line {a, b} | circle {center, radius}, exact?}]   # Stage 149: what elements are placed on (154: exact)

Element:
    kind:        String                      # a template's kind, "edges" (the edge walls here) or "raw"
    name:        String                      # unique; flags derive from it (door:<name>)
    start, end, via | circle {center, radius}    # the geometry, shared by the element's surfaces
      | carrier, from, to (, via)            # ... or on a carrier: projected onto it by the lowering
    straight, label, flip                    # flip: left and right swapped
    <parameters>                             # the kind's own
```

The reader **lowers** an `rc-level/2` level to `rc-level/1`: the room's walls when the room is built, every
element's surfaces in order, the four edge walls (where an `edges` element stands, else here), the
pre-transform circles last, with consecutive ids in that order (a portal two). `rc-level/1` is read as it is:

```
Level (rc-level/1):
    schema:      "rc-level/1"
    name:        String                      # its path under data/levels
    bounds:      [x, y, w, h]                # the level's edge (default 0, 0, 1920, 1080)
    gravity:     [gx, gy]                    # default (0, 0) -- no gravity
    room:        {rect: [x, y, w, h], built: bool}
    player:      {start: [x, y]}
    camera:      {start, smoothing, smoothing_speed} or null
    background:  {rect, color} or null
    colliders:   [{a: [x, y], b: [x, y]}]    # player-only collision segments (§25.1)
    features:    {game, preview, arrow, hud, cursor}
    surfaces:    [Surface]                   # every surface the level builds, in build order

Surface:
    ids:            [int]                    # a portal [source, image]: the image is built from the source
    label:          String or null           # the node prefix; null: not drawn (the edge walls)
    start, end, via | circle {center, radius}
    straight:       bool                     # the exact line through start and end
    pre_transform:  bool                     # a pre-transform circle (§9.5)
    left, right:    {effect: {type, back, normal, theta, d}, interactive: bool, hits: [HitEffect]} or null
    resolver:       {key, table: [[value, {left, right}], ...]}   # categorical sides (§9.4)
    presence:       [{key, values}]          # §9.6
    player_solid:   bool
```

Every point is read as float32 (the game's vectors); other numbers stay float64. Surface ids are derived from
the build order and never written in `rc-level/2`; an element's identity is its name. Cycle overrides (§17) are
not part of the format.

**Exact carriers (Stage 154).** A carrier is an authoring construct: the lowering projects each element's points
onto it and rounds them to float32, so two elements on one carrier lie, to the exact predicates, on two slightly
different lines or circles. A carrier with `exact: true` places its elements' points on float32 points lying
exactly on it instead (docs/TDD_59_EXACT_CARRIER.md): a line through a grid point in a primitive direction
`(q, p)`, `|q|, |p| <= 1024`, its points `A + k h (q, p)`; a circle about a grid point with `radius^2 = N h^2`, `N` an
integer with many representations as `X^2 + Y^2` within half a pixel of the declared radius, its points
`C + h (X, Y)` (`h`: the finest grid float32 holds at the level's magnitudes). Every element on an exact carrier is
then exactly on one line or circle (the solver's carrier identity holds), and a point two of them share is one
value. The cost is a snap: a point moves to the nearest exact point (a line's are a fraction of a pixel apart, a
circle's about 1 px apart at radius 40, up to 3 px at 150, 5 px at 400 and 12 px at 900). An exact circle holds
arcs only: its whole has no float32 form. Crossings of two exact carriers are not exact points (v1).

**Level discovery:** the levels are the files under `data/levels/`.

### 19.2 Editor requirements

> **Status: implemented (Stage 143, docs/EDITOR.md, docs/TDD_48_NATIVE_EDITOR.md).** The editor is native (`rcg::Editor`) behind the C API (`rc_editor_*`); a host paints its draw list and forwards input. Hosts: the Python host (tkinter) and Godot (`RicochetEditor`, Stage 144). Since Stage 155 the panel lists the levels (the host's `data/levels`): a click, Ctrl+O, or the previous/next buttons open another level in place, asking again before discarding unsaved changes. Of the table below, everything but compound effects and cache overrides (neither is in the format) is there: effects are assigned by an element's kind and parameters (docs/ELEMENTS.md); a target is the `target` kind; validation runs live, per element.

The level editor must support:

| Feature | Behavior |
|---------|----------|
| **Place surface** | **Line**: click start, click end. **Arc**: click start, click end, click via (three clicks). |
| **Move/resize** | Drag endpoints. The `via` point stays at its **absolute position**; the carrier is recomputed from the new three points. If `via` is no longer geometrically valid (not on the carrier), the editor snaps it to the nearest valid position on the new carrier. |
| **Delete** | Select and delete a surface. |
| **Assign effect** | Per side: select effect type from a panel. For compound effects: ordered list of elementary effects configured in a sub-panel. For rigid motion: rotation angle and displacement vector fields. |
| **Set target** | Toggle a surface as a goal target. |
| **Set spawn** | Click to place the player spawn point. |
| **Cache overrides** | Define cycle identities: select surfaces in order, declare the composed transform = identity (or other specified value). |
| **Test mode** | Play the level within the editor. Full simulation, preview, plan, fire. |
| **Validation** | On save: check all constraints (§23). Report errors. |

The level editor's internal UI layout, undo/redo system, and advanced features (multi-select, snap-to-grid, etc.) are left to the implementer. The spec defines what the editor must be able to **produce** (a valid `LevelData`), not how the editor's own UI is organized.

The editor previews the compound effect's combined Mobius matrix and indicates whether it is conformal or anti-conformal. No geometric preview of the combined effect is required for v1.

The editor validates constraints in real time as surfaces are edited (not just on save). Invalid surfaces (zero-length, wrong carrier type for effect) are highlighted in red. Deleting a surface referenced in a cache override shows a warning.

Elements are identified by their names (§19.1); surface ids are derived from the build order, never stored.

### 19.3 Serialization

Levels serialize to JSON (§19.1) in one canonical spelling: object keys sorted, one per line, indented by one
space per level; an array of scalars on one line, any other array one element per line; every float as its
shortest round-trip decimal (`rc_tool fmt`; ctest `rcg_levels_canonical`). The data is the authoring form:
the elements, each with the three points of its surfaces (or their circle) and its parameters; the surfaces,
their ids, the carriers and everything else are derived when the level is built (Principle 21). The lowering
(`rc_tool lower` -> `bench/lowered`) is what the oracle's independent builder reads; `rc_tool migrate` turns an
`rc-level/1` level into elements, exactly (its lowering is the original byte for byte). The scene defs the solver reads are derived from it too
(`rc_tool dumps` -> `bench/scenes`).

---

# Part VI: Implementation

## 20. Architecture

### 20.1 Layer separation

Since Stage 119 every layer but the host is native and engine-free (lints: `*_no_engine`, `godot_adapter_includes`, `gdscript_budget`):

| Layer | Directory | Responsibility | Engine types used |
|-------|-----------|----------------|-------------------|
| **Core** (`rc::`) | `native/core` | Geometry, effects, the precision beam solver (intersection, effect maps, anchored planning, exact predicates, dynamic precision), the tracer, step merge, visibility, invariants | **None** |
| **Game** (`rcg::`) | `native/game` | Level data, the surface model, crossings and pieces, landing, the world, the player's movement, the session (plan, clicks, preview, overlays, flight, undo), the draw list | **None** |
| **C API** | `native/capi` | `rc_api.h`: the game for any engine or language | **None** |
| **Hosts** | `native/hosts` | `headless` (`rc_tool`: data pipelines, replay, bench); `godot` (`RicochetGame`: input, frame, camera, drawing the list) | Godot in `hosts/godot` only |

The historical layer table (GDScript Math / Visual / Game) described the game before the port; its GDScript was deleted in Stage 119 (docs/TDD_24_ENGINE_SWITCH.md).

**GDScript performance note:** GDScript is significantly slower than C++ for tight numerical loops. For the intersection kernel (up to 64 events × N surfaces per frame), avoid object allocation in hot loops. This is why `FastSolver` exists: a raw-float64 `PackedFloat64Array` backend, roughly 50× faster than the precision-generic code and bit-identical to it at float64. If profiling shows the intersection kernel is the bottleneck, consider moving it to a GDExtension (C++) module. The math layer's zero-dependency design makes this migration straightforward.

**Level data (Stage 115):** a level scene points at its data (`LevelSettings.level_path`); `LevelData` turns the natively read data into `SurfaceSpec` / `SideSpec` / `SpawnTemplate` resources in build order, and the surfaces in the scene tree are `Node2D` instances drawing them. The engine-free game layer (`native/game`, `rcg::`) reads, validates and builds the same data; the headless host `rc_tool` runs the data pipelines without Godot. Stage 116: the game's model and session are native too (`rcg::Session`: the plan, clicks, overlays, preview, visibility, spawn preview, fire / flight / landing, undo, reset; `rcg/world.hpp`, `rcg/landing.hpp`, `rcg/geom.hpp`), checked against the GDScript game by the session replay golden; the GDScript game still runs the shipped build until the engine boundary (a C API) replaces it.

**The engine boundary (Stage 118, docs/TDD_23_ENGINE_BOUNDARY.md):** the whole game sits behind a C API, `native/capi/include/rc_api.h` (`extern "C"`; size-prefixed structs, int32 enums, borrowed views, statuses and a per-thread last error, a log sink). A host hands it a level's JSON text, semantic input events (plan add / remove at the cursor, fire, undo, reset, clear plan, cycle display, toggle gravity / visibility, any key), the cursor, and once per frame the frame time with the held and just-pressed movement keys; it gets back the frame (player, facing, the arrow in flight, the camera's target, smoothing and limits, display mode, gravity), the draw list (§20.4), the HUD text and the session record. Hosts on it: the headless `rc_tool` (its replay), a Python ctypes host (`tests/host_py`), and the Godot adapter `RicochetGame` (`native/hosts/godot`, which includes only `rc_api.h`; `scenes/rc_level.tscn`). Since Stage 120 (C API 1.1) every draw node has a stable id and a content version, and `rc_game_draw_changes` sends commands only for the nodes that changed: a host keeps what it drew. Since Stage 121 (C API 1.2) a live host creates its game asynchronous (`rc_game_config.async`, §30.3): the frame reports `region_pending` and `preview_exact`. Since Stage 122 (C API 1.3) it also animates its draw list (`rc_game_config.animate`, §30.3); the game's state and record are unchanged.

### 20.2 Project structure

Current layout (Stage 119).

```
ricochet-game-v2/
├── GAME_SPEC.md, CHANGELOG.md, CLAUDE.md, Makefile, gates.sh
├── native/
│   ├── core/include/rc/       # the solver, tracer, visibility, invariants (header-only C++)
│   ├── game/                  # rcg::: level data, model, geom, landing, world, kinematics, session, render
│   ├── capi/include/rc_api.h  # the C API
│   ├── hosts/headless/        # rc_tool: fmt, dumps, manifests, replay (on the C API), bench
│   ├── hosts/godot/           # the Godot host: RicochetGame (includes only rc_api.h)
│   └── tests/                 # rc_tests, rcg_tests (doctest), the C11 smoke host
├── data/levels/, data/sessions/   # the levels (rc-level/1), the session scripts
├── bench/                     # goldens, fixtures, corpora, sweeps, sessions, gut_traces
├── tests/host_py/             # the Python host (ctypes): the C API end to end
├── engines/godot/             # the Godot project: project.godot, scenes (one RicochetGame each), the host's GUT suite
├── docs/                      # TDDs, PORTING.md, LEVEL_FORMAT.md, research/ (oracle, judges, ledger)
└── tools/                     # lints, the ledger check
```

### 20.3 Data flow per frame

```
player_position + cursor_position + plan
        │
        ▼
  BeamTracer.trace_pair (scene defs; cached float64 scene and last pair)
        │
        ▼
  PrecisionLadder.solve ── float64 FastSolver pass + ErrorEstimate;
        │                  BigFloat climb when needed (§31.5)
        ├──→ planned events  (BeamSolver, PLANNED; anchored image chain, §13.1)
        ├──→ physical events (BeamSolver, PHYSICAL; anchored until divergence)
        ▼
  BeamTracer.to_path ──→ planned / physical TracePath (visual steps, frame ids)
        │
        ▼
  merge ──→ step_tree (5 step types, divergence index)
        │
        ▼
  visual_convert (three points per step → line or arc; escape steps to the edge)
        │
        ▼
  draw_calls (_draw: draw_line / draw_arc per segment)
        │
        ▼
  visibility_compute (plan-aware, §15) ──→ region polygons ──→ draw
```

### 20.4 Rendering

- Wireframe prototype aesthetic: colored lines, simple shapes, no art assets.
- Trajectory: `_draw()` on a preview Node2D child of the player.
- Surfaces: drawn as lines/arcs with color-coded effect types.
- Visibility: filled polygon regions (tessellated for arc edges).
- Preview updates via `queue_redraw()` every frame the cursor moves.

**Z-order (back to front):** z 0, then the edge overlay (z 5), visibility regions (semi-transparent, z 10), the spawn preview (z 19), the step tree preview (z 20), the arrow (during flight, z 30). Within z 0, in the level's node order: the background, the player, the cursor, the HUD, then the surfaces (one node per piece, by surface in build order; spawned surfaces after). The player and the cursor draw under the surfaces (Stage 118 states the order the game has always drawn: an earlier revision gave the player z 40, never implemented).

**The draw list (Stage 118, `rcg/render.hpp`):** what the game draws is decided natively, as a list of nodes -- a key (`["surface", id, piece]`, `["edge"]`, `["visibility"]`, `["spawn"]`, `["path"]`, `["arrow"]`, `["cursor"]`, `["player"]`, `["background"]`, `["hud"]`), a z-index, a position, a modulate -- each with its calls in CanvasItem terms (line, arc with `draw_arc`'s parameters, polygon, coloured polygon, filled circle, rect, polyline, multiline, dashed line, text, a triangle array, the player's rotated polygon, the HUD label), in draw order. A host maps each call onto its own 2D drawing; `rc_tessellate_arc` gives an arc as `draw_arc`'s polyline. The session record holds the list's sha (`draw`).

Surfaces are drawn in visual coordinates using their defining geometry (start, via, end → derive carrier → draw_line or draw_arc). The beam solver also works on surfaces in visual coordinates (§6.1). No transform is needed anywhere.

Within the step tree preview, draw order (back to front): DIVERGED_PLANNED → DIVERGED_POST_PLANNED → ALIGNED → ALIGNED_POST_PLANNED → DIVERGED_PHYSICAL. The physical path (green/yellow) always draws on top of the planned path (red).

---

## 21. Arrow flight and animation

### 21.1 Time freeze

When the player fires, **game time freezes**. All physics, surface movement, and timers stop. The shot is logically instant — the full physical trace is computed before animation begins.

**Native (Stage 118):** the game session pauses itself: while the arrow flies, `advance` moves only the arrow (no physics, no preview), any key event speeds it up, clicks are ignored; the host keeps calling it every frame. **Godot mechanism (the GDScript game):** use `get_tree().paused = true`. The arrow animator node has `process_mode = PROCESS_MODE_ALWAYS` so it continues running during pause. All other game nodes use the default process mode and freeze automatically. During arrow flight, the preview is **hidden** — only the arrow animation is shown. After the shot completes, the tree is unpaused and the preview reappears for the retained plan.

**Animation skip:** the player can skip the flight animation by pressing any non-movement key during flight. Skip instantly completes the animation: all remaining state changes are applied, all visual state changes are applied, and the camera jumps back to the player. Movement keys (WASD) pressed during flight are ignored — they do not queue movement or trigger skip. Fire during animation only skips — it does not queue a new shot. A new shot requires a separate Fire press after the animation completes and game time unfreezes.

### 21.2 Animation

The arrow is animated step-by-step along the computed physical `TracePath` (from `BeamTracer.trace_pair`):

1. For each step, the arrow tip moves from `start` to `end`:
   - Line steps (visual): linear interpolation.
   - Arc steps (visual): parametric interpolation along the arc. The parameter `t ∈ [0, 1]` interpolates from start to end via the via point. The arrow position at each t is computed by slerp (spherical-linear interpolation) around the center, with winding direction determined by the via point (cross-product sign of `(start - center) × (via - center)`).
2. At each hit point: brief bounce event (visual flash, sound cue). If the hit triggers a state change (e.g., surface breaks), the surface visually updates at this moment during the animation.
3. Speed: constant visual speed along the path (not constant parametric speed — arcs travel at the same visual speed as lines). Default arrow speed: **1600 units/sec**. Configurable per level.

### 21.3 Ray escape during flight

A ray never escapes (Stage 136: infinity is always on): a beam with nothing to hit wraps through infinity and loops until a limit (§12.6), and the arrow flies it. The shot still counts — checkpoint is saved, game time unfreezes, state changes from prior hits take effect. Win condition is evaluated normally.

### 21.4 After the shot

1. Evaluate: did the arrow hit all target surfaces?
2. Unfreeze game time. State changes from the shot take effect.
3. If win: level complete transition.
4. If fail: player remains in play phase, can reset and try again.

The arrow disappears immediately when the animation completes (or when it escapes the viewport). No fade, no lingering.

When game time unfreezes, if a surface the player was standing on has been broken by the shot, the player falls immediately. Physics resume from the pre-shot position with the updated game state.

---

## 22. Visual language

### 22.1 Wireframe aesthetic

V1 uses a wireframe/debug aesthetic. No art assets required.

| Element | Visual |
|---------|--------|
| **Surfaces** | Colored lines/arcs. Color indicates effect type. |
| **Player** | Small triangle or circle at spawn/current position. |
| **Arrow** | Line segment (length ~40 units) with a triangular head (30° angle, ~16 units). Single draw call. Rotates to follow the path tangent direction (including arc tangents). |
| **Targets** | Distinct color (e.g., gold/yellow) and shape (e.g., thicker line, pulsing). |
| **Cursor** | Crosshair or dot at mouse position. |
| **Unhit targets** | Pulse or glow animation. |
| **Hit targets (prior shots)** | Drawn dimmer with a checkmark overlay. |

For arc steps in the visual frame, the tangent direction is perpendicular to the radius at the arrow's current position: `tangent = perpendicular(arrow_position - arc_center)`, oriented in the traversal direction.

### 22.2 Color scheme

**Surfaces:**

| Surface type | Color |
|-------------|-------|
| Reflection | Blue |
| Inversion | Purple |
| Rigid motion | Cyan |
| Projective | Orange |
| Block | Red |
| Pass-through | Gray |
| Target | Gold |

Target surfaces display their effect color on each side plus a **gold outline** or gold endpoint markers to indicate target status.

**Step types (preview path):**

| Step type | Color | Style |
|-----------|-------|-------|
| ALIGNED | Green | Solid (dark, with bright dashes over it; §4.3) |
| ALIGNED_POST_PLANNED | Green | Dashed |
| DIVERGED_PHYSICAL | Yellow | Dashed |
| DIVERGED_PLANNED | Red | Solid (dark, with bright dashes over it; §4.3) |
| DIVERGED_POST_PLANNED | Red | Dashed |

Dashes run along the arrow's way; coinciding steps are drawn side by side (§4.3).

**Other:**

| Element | Visual |
|---------|--------|
| Visibility region | White, semi-transparent fill |
| Planned surface overlay | Numbered label, brightened |

### 22.3 Surface side indicators

Each side of the surface is drawn in its own effect color. The left half (relative to the segment's traversal direction) uses the left effect's color; the right half uses the right effect's color. Implementation: draw the surface as two parallel lines/arcs slightly offset to each side of the carrier, each in its respective effect color. This makes it immediately clear what each side does. For surfaces where one side is null (pass-through), that side is drawn in gray.

Offset distance: **2 pixels** per side from the carrier center line. Total visual width: 5px (1px center + 2px each side).

Non-interactive sides are drawn at **50% opacity** compared to interactive sides.

### 22.4 Line widths

Default surface line width: **3 pixels**. Default trajectory/preview line width: **2 pixels**. Arc rendering uses Godot's `draw_arc()` which handles arc geometry natively.

Default `draw_arc()` point_count: **64 per full circle**, scaled by arc span (e.g., 90° arc = 16 points).

---

## 23. Constraints and limits

| Constraint | Value | Rationale |
|------------|-------|-----------|
| Max significant events per trace | **10** (`MAX_SIGNIFICANT`) | Applied effects, firing state changes, passages of infinity (§12.6). No loop detection: the limits end every loop (Stage 131). |
| Max events per trace | **64** (`MAX_HITS`) | Every carrier crossing, pass-through and off-segment included; not the cursor (§12.6). |
| Inversion circle = carrier | Always | No separate inversion-circle parameter. |
| Min segment length | > 0 | Zero-length segments never hit. Validated at load. |
| Effect invertibility | Always | Every transformative effect is invertible by definition (§10.8); no matrices are composed since Stage 84, so the former "frame determinant ≠ 0" check no longer applies. Portal rotation `a = unit(cos θ, sin θ)`. |
| Side labels | LEFT / RIGHT only | Uniform for all segment types. |
| Direction representation | Two points | No stored angles or unit vectors. |
| Ordering method | Cross-product sign | No `atan2`. |
| Epsilon decisions | Forbidden | No `\|x\| < ε` for topology-changing decisions. |
| Surface filtering | Forbidden | All surfaces always participate in hit detection. No spatial filtering, proximity culling, or pre-selection (§12.1: the candidates are chosen by consequence, and a crossing is skipped only on a rigorous proof that it lies off its segment, Stage 139). |
| UI click tolerance | 8 pixels (§4.2) | UI-layer tolerance — does NOT violate the no-epsilon constraint, which applies only to topology-changing geometric decisions in the math layer. |
| Reflection carrier | Must be a line (`a = 0`) | Validated at level load and editor save. |
| Inversion carrier | Must be a circle (`a ≠ 0`) | Validated at level load and editor save. |
| Line directional projection's normal | Not along its line carrier (`cross(n, end − start) ≠ 0`) | A normal along the line leaves it nowhere: no beam departs, nothing is reachable through it (§15, an empty region). Validated at level load and editor save (`EffectSpec.validate`; B9, Stage 113). |

---

## 24. Target mechanics

### 24.1 Default: surface targets

The default target type is a **surface** the arrow must contact during the physical trace. A surface with `is_target = true` is a goal surface.

### 24.2 Hit detection

> **Stage 107:** a target is a side's `TargetHit` hit effect (§10.6). It registers when its side *fires*, which is a solid hit (§11.1). That is the same as the on-segment rule below except at a lone endpoint (a graze) and at a shared point that stops the arrow, where nothing fires (Stage 138). The registration is the flag `target:<id>`. The rest of this section describes the pre-Stage-107 rule.

A target is hit if the physical trace produces an **on-segment** hit event on a surface with `is_target == true` (off-segment carrier crossings are events too, §11.5, but do not count). The target hit is registered at the moment of contact (when the hit event is produced), **regardless of the surface's effect**:

- **Pass-through target**: arrow passes through, hit registered.
- **Reflective target**: arrow bounces, hit registered.
- **Block target**: arrow stops, hit registered.
- **Any other effect**: hit registered, then effect applied normally.

A target surface can have any effect. The target flag and the effect are independent concerns.

`targets_hit` is updated during the trace (on the copy of game state). After the shot completes, the game manager promotes the copy's `targets_hit` to the persistent set. `targets_hit` is NOT part of `GameState` — it is tracked separately by the game manager alongside the checkpoint stack.

Promotion is cumulative: `targets_hit = existing_targets_hit.union(trace_result.targets_hit)`. Targets hit in previous shots are preserved.

`targets_hit` is initialized as an empty set on level load and on full reset.

### 24.3 Multiple targets

A level may have multiple target surfaces. The level is complete when all targets have been hit **across one or more shots** in the current attempt (§3.2). A single shot does not need to hit all targets — intermediate shots may clear obstructions or change game state to make remaining targets reachable. Order does not matter (unless a future mechanic adds ordered targets).

### 24.4 Extensibility

The target system is extensible to point targets (tolerance-based) or region targets. The spec defines surface targets as the default.

### 24.5 Detectors

> **Status: implemented (Stage 145, docs/TDD_50_OBJECTIVE.md).**

A **detector** writes a flag as the player moves, so a level reacts to where the player goes: a door opens as
the player steps on a plate. It is a region (a rectangle or a circle) or a segment, with a flag (default
`det:<name>`), a value (default `true`) and a mode:

| Mode | Writes |
|---|---|
| `while` | the value while the player's center is in the region; the flag is unset outside (a region only) |
| `enter` | the value when the player enters the region, or crosses the segment |
| `toggle` | the flag's negation (a missing flag is false) on entering, or on crossing |

Detectors are checked after every physics tick, against the player's center: a region on its boundary counts
as inside, and a segment is crossed when a tick's move goes from one side of its line onto or past it within the
segment (a move off the line is no crossing). Their writes are ordinary flag writes (§10.6): presence (§9.6) and
resolvers (§9.4) follow at once, so does the player's collision. The flags, and which regions the player is in,
are part of the checkpoint (§3.3). A detector `for` objects waits for objects (a later stage).

### 24.6 The goal

> **Status: implemented (Stage 145).**

A level may have a **goal**: a region and conditions (the presence form, §9.6: `{key, values}`, all required).
The goal is **open** while its conditions hold. The player wins by standing in it while it is open (checked
after every physics tick and when a shot lands). Targets are then ordinary flag writers that open doors or the
goal; a level without a goal keeps the all-targets rule (§3.2). The goal is drawn green: dashed while locked,
solid when open, filled when won. A level with a goal shows `Goal: locked | open | reached!` as the HUD's first
line, and its session record holds `won`.

---

## 25. Player character

### 25.1 Physics

The player is a circle of radius **12 units** moved by the native player controller (Stage 117,
`rcg/kinematics.hpp`; every host drives the same one): at a fixed physics step of 1/60 s it is swept exactly
(no tunnelling at any speed) against what it collides with, slides along what it meets (at most four contacts
a step), is pushed out of any overlap, and rests a **0.08-unit skin** away from what it touches. It collides
with two kinds of geometry:

1. **Colliders** — the level's player-only segments (`colliders`, §19.1: floors, platforms, ceilings in
   gravity levels). They are invisible to arrow tracing, visibility, and planning.
2. **Surfaces with `player_solid = true`** that are **present** (§9.6) — their exact geometry: a line, an arc
   (its part of the circle from start through via to end, and its two ends), a full circle. An absent surface
   does not block the player (Stage 117, as the arrow passes it). Surfaces with `player_solid = false` do not
   impede the player. `player_solid` is per-surface (not per-side); sides do not matter to the player.

A **floor** is a contact whose normal is within 45° of up `(0, -1)`. With gravity, a player that stood on a
floor and is not rising keeps to it within **1 unit** (a floor snap: walking off a ledge drops, walking along
a floor stays on it).

**Arrow origin:** the arrow originates from the **center of the player's collision shape** (no offset).

**Arrow–player interaction:** the arrow does **not** collide with the player. The player is not a surface for ray tracing purposes. The player can stand on or near a surface without blocking their own shot.

### 25.2 Movement

- Move: WASD / left stick.
- Default speed: **200 units/sec**. Horizontal input response is instant (no acceleration ramp). Gravity applies vertically as configured in `LevelData.gravity` — the player can fall and needs platforms when gravity is non-zero. Configurable per level.
- Default jump velocity: **400 units/sec upward**, from a floor only (the up action just pressed). Single jump (no double jump). No coyote time. Without gravity the movement is the input direction, normalized, times the speed.
- **The arrow is never affected by gravity** — it follows pure geometric ray tracing regardless of the level's gravity setting.
- The player can move while aiming. *(Principle 16.)*

### 25.3 Spawn, checkpoints, and reset

- Player spawns at `scene.spawn_point`.
- **Undo last shot** (Z): reverts to the checkpoint before the most recent shot — player position, game state, and plan are restored. Checkpoint stack allows stepping back through multiple shots.
- **Clear plan** (C): clears the plan without affecting player position or game state.
- **Full reset** (R): reverts to the level's initial state — player returns to spawn, all surfaces restored, all state reset, checkpoint stack cleared.

### 25.5 Objects

> **Status: implemented (Stage 146, docs/TDD_51_BODIES.md).**

An **object** is a circle body with a surface: a full circle whose sides are a wall's (terminal), a mirror's or a
pass, player-solid by default. A **pullable** object falls under the level's gravity and rests on what holds it,
moved by the player's controller at its own radius. It meets the player (a falling object stops on the player),
but not other objects (v1). A **static** object stays. Its surface takes the next ids after the level's
surfaces, so it is planned and hit like any surface, and it moves with the body. While an object moves, nothing
is fired. The bodies' positions are part of the checkpoint, and detectors `for` objects see their centers
(§24.5, regions only).

**Bubbles** (Stage 150). An object's `gravity` is a factor of the level's: a bubble's is negative, so it floats up
and rests against what is above it. An object that `pops` is popped by the arrow's hit: the hit sets
`obj:<name>:popped` and the object is gone from then on (its surface absent, so the arrow passes on, and its body
still), until an undo. An object may `carry` another, which then keeps its offset from the carrier and does not
fall, until the carrier pops; then it falls (one level of carrying).

### 25.6 The pull and the grapple

A **pull** (a pullable object's sides carry it) and a **grapple** (the `grapple` element's sides) are hit
effects applied when the shot lands (§10.6). The first one the shot fired starts a **journey** along the
physical path:
- a grapple takes the player forward, from the origin to the hit;
- a pull takes the object back, from the hit to the player, keeping its center's offset from the hit point.

The traveller moves at 900 px/s, swept against what it collides with: present player-solid surfaces and
colliders, and for an object the player. It stops at the first contact or at the path's end. A fence (an
arrow-pass barrier) stops it; an arrow-block barrier is not solid and does not. A step that starts where the last
did not end (a portal) is crossed by a jump, and a step through infinity ends the path. The game is paused during
a journey as during a flight (`flying`), and the journey is part of the shot.

### 25.4 Visual

Simple shape (triangle pointing toward cursor, or circle). Wireframe aesthetic.

---

## 26. UI/HUD

> **Status: PLANNED** — The HUD and menu systems are not yet implemented. The game currently runs with a basic plan display (`PlanHUD`) but no menus, level select, or pause screen.

### 26.1 Gameplay HUD

| Element | Content |
|---------|---------|
| Plan list | Ordered list of planned surface sides, shown by name (§19.1) with their CURRENT effect (resolved via ConfigResolver). Changed effects are visually marked (e.g., strikethrough or warning icon). |
| Shot counter | Number of shots fired this attempt. |
| Level name | Current level identifier. |
| Target progress | "Targets: 2/3 hit" (or equivalent count). |
| Reset hint | "R to reset" (or equivalent). |

### 26.2 Menu structure

- **Main menu**: Play, Level Editor, Settings, Quit.
- **Level select**: Grid or list of available levels. Shows completion status (checkmark if completed, shot count if available).
- **Pause menu**: Resume, Reset, Quit to Level Select.
- **Settings**: Audio volume, controls, display.

### 26.3 In-game overlay

- Plan display: planned surfaces highlighted with numbers.
- Undo indicator: visual feedback when right-click removes a surface.
- Invalid plan warning: brief text or icon when the plan cannot reach the cursor.

---

## 27. Save system and progression

> **Status: PLANNED** — The save system is not yet implemented.

### 27.1 What is persisted

- Level completion (per level: completed yes/no, best shot count).
- Settings (audio, controls, display).
- Last played level (for "continue" functionality).

The host persists the save data in its engine's own storage (Stage 119: the game is engine-free; the Godot host would use `ConfigFile`). Human-readable, simple key-value pairs.

### 27.2 Progression structure

Left open. The spec defines what a level is and how it loads, not the unlock order, world grouping, or scoring system. Those are content design decisions that can vary without changing the engine.

---

## 28. Implementation phases

> **Implementation status:** Phases 0–5 are complete. Phase 6 is partially done (rigid motion and physical projectives). The TDD documents map phases to numbered stages: see TDD_01 through TDD_09 for details. Stages 44–51 were repurposed during development; see TDD_05 for the reconciliation table.
>
> Stages 80–84 (TDD_09) replaced the frame-based solver of Phases 0–5 with the precision beam solver. Phase items below that name `MobiusTransform`, `TransformCache`, `Planner`, `trace()`, `intersect_line_with_gcircle()` or frame tracking are **historical**: they were built, then deleted in Stage 84. Their roles are now filled by `BeamSolver`, `PrecisionLadder` and `BeamTracer` (§12). Stages 85 and 86 (state flags, ghost mode) are done; Stage 87 (plannable projectives) is planned.

### Phase 0: Core abstractions *(Done)*
- `Direction`, `Ray`, `Segment` data classes (§8).
- `GeneralizedCircle` class with fields `(a, b, c, d)` — method implementations (`is_line()`, `center()`, `radius()`, `contains_point()`) are added in Phase 1.
- `MobiusTransform` struct with `conjugating` flag *(historical — deleted in Stage 84)*, complex number helpers (§5.2, §5.3).
- `Surface`, `SideConfig` (§9). *(ConfigResolver deferred to Phase 8.)*
- `Effect` base with `Kind` enum, `TransformativeEffect`, `TerminalEffect` (§10.1). *(ProjectiveEffect deferred to Phase 6.)*
- `HitRecord`, `Step`, `StepTree` data structures (§11.2, §14). *(Now `TraceHit`, `TraceStep`, `TracePath`.)*
- `GameState`, `CheckpointData` data structures (§18). *(LevelData deferred to Phase 8.)*
- `TransformCache` with provenance-keyed lookup (§17). *(Historical — deleted in Stage 84.)*

**Deliverable:** all core types compile, have correct field definitions, and can be instantiated. No algorithms — just the type system skeleton that subsequent phases build on.

### Phase 1: Math foundations *(Done)*
- `GeneralizedCircle` class with `is_line()`, `center()`, `radius()`, `contains_point()`.
- `MobiusTransform` class with apply, compose, invert, transform_carrier. *(Historical — deleted in Stage 84.)*
- `Segment` class with start, end, via (carrier derived via cache), side determination.
- `Direction` and `Ray` classes.
- `intersect_line_with_gcircle()` — the single intersection primitive. *(Historical — replaced by `BeamGeom.hits`, §11.1.)*
- Unit tests for all math classes.

**Deliverable:** math layer passes unit tests for intersection, transform composition, and carrier transformation.

### Phase 2: Basic effects and ray tracing *(Done)*
- `TransformativeEffect` (reflection only), `TerminalEffect` (block).
- `Surface` and `SideConfig` classes.
- `trace()` function — the physical ray tracing loop. *(Historical — replaced by `BeamSolver`, §12.)*
- `Step` and `TracedPath` classes (now `TraceStep`, `TracePath`).
- Frame tracking (identity only — no inversions yet). *(Historical.)*
- Unit tests: trace through a scene with reflecting and blocking surfaces.

**Deliverable:** ray tracing works with reflection and block effects. Traced paths are correct.

### Phase 3: Preview rendering and player input *(Done)*
- `VisualConverter` — math→visual conversion (line segments only at this phase).
- `PathRenderer` — draws traced paths via `_draw()`.
- `Player` — CharacterBody2D with movement and aiming.
- Input handling: aim, fire, reset.
- Preview updates every frame.

**Deliverable:** player can move, aim, and see a real-time trajectory preview with reflections.

### Phase 4: Planning algorithm *(Done)*
- `Planner` — image chain method for transformative sub-chains. *(Historical — replaced by the anchored planned path in `BeamSolver`, §13.1.)*
- Plan construction input: click to add, right-click to remove.
- Step tree: planned vs physical paths, divergence detection.
- Preview shows aligned/diverged colors.

**Deliverable:** player can build plans, see the ideal path, and observe divergence when the plan doesn't match the physical trace.

### Phase 5: Circle inversion and arc visualization *(Done)*
- `CircleInversionEffect` (transformative).
- Frame transform composition (non-identity frames). *(Historical — replaced by the inversion beam map, §10.8.)*
- `VisualConverter` extended: math→visual now produces arc segments.
- `PathRenderer` extended: draws arcs via `draw_arc()`.
- Beyond-infinity hit handling.

**Deliverable:** circle inversion works. Arrow paths curve in the visual frame. Arcs render correctly.

### Phase 6: Projective effects and mixed planning *(Partially done — rigid motion: Stage 82; physical projectives: Stage 83; projective planning: Stage 87; compound: planned)*
- `LineNormalProjection`, `CircleNormalProjection`, `LineDirectionalProjection`, `CircleDirectionalProjection` (implemented in place of the originally planned `SemicircleDirectionalProjection`).
- Back-propagation for each projective effect.
- Mixed chain planning algorithm.
- Stack clear / new epoch on projective effects.
- `RigidMotionEffect` (rotation + translation).
- `CompoundTransformativeEffect`.

**Deliverable:** all effect types work. Mixed plans with transformative and projective surfaces produce correct previews.

### Phase 7: Visibility system *(Done, Stage 110; projective plans Stage 113)*
- Visibility polygon computation (multiple regions).
- Rendering visibility regions.
- Consistency with trajectory (same pipeline, same scene state).

**Deliverable:** visibility overlay shows where the player can validly aim.

### Phase 8: Game loop *(Planned)*
- Level loading from `.tres` resources.
- Target surface mechanics (hit detection, win condition).
- Shot lifecycle (freeze, trace, animate, evaluate, unfreeze).
- Arrow flight animation.
- Main menu, level select, pause menu.
- Save system (level completion, settings).

**Deliverable:** complete game loop. Player can select a level, play it, win, and return to level select.

Minimal sound events for Phase 8: `fire`, `bounce`, `break`, `target_hit`, `level_complete`. Use placeholder sounds (simple .wav files). Full audio design is deferred (§33).

### Phase 9a: Basic level editor *(Planned)*
- Surface placement (line and arc segments).
- Effect assignment (per side, including interactive flag).
- Target and spawn point placement.
- Serialization to `.tres`.

**Deliverable:** levels can be created, saved, and loaded.

### Phase 9b: Advanced level editor *(Planned)*
- Cache override definition.
- Test mode (play within editor).
- Validation on save (all constraints from §23).
- Initial game state flags editing.

**Deliverable:** levels can be tested within the editor with full simulation. Validation catches constraint violations before save.

### Phase 10: Polish and testing *(Partially done — invariant sweep operational)*
- Invariant-based testing (§29.3).
- Performance profiling and optimization.
- Computation cache implementation and validation. *(Superseded: the legacy `TransformCache` was deleted in Stage 84; see §17.)*
- Manual override testing. *(No beam-solver design yet; §17.4.)*
- Edge case fixes.

**Deliverable:** all invariant tests pass. Performance meets frame budget.

**Phase dependencies:** each phase depends on all prior phases. Phase 7 (visibility) requires Phase 6 (all effects). Phase 8 (game loop) requires Phase 7. No phases are independent enough to parallelize safely.

---

## 29. Testing strategy

**Testing (Stage 119):** the game is tested natively -- doctest (`rc_tests`, `rcg_tests`) with ctest, the mpmath oracle (pytest), the goldens and fixtures, the native invariant sweeps over the sweep manifests, the session golden replayed through the C API in C++ and in Python (`tests/host_py`). GUT tests only the Godot host (`engines/godot/tests`). The GUT suite's former tests are accounted for row by row in `docs/research/gut_ledger.tsv`. The sections below describe what is tested; where they name GUT, read the native equivalent.

### 29.1 Math layer unit tests

- Intersection: all carrier types (line-line, line-circle), boundary cases, degenerate cases.
- Beam solver: effect beam maps against closed forms; exact predicates; FastSolver ↔ generic bit-identical equivalence; traces against the mpmath oracle (`docs/research/oracle/`, fixtures in `bench/`).
- Provenance: cancelling re-hits restore the earlier beam exactly; known-point incidence; exact lines.
- *(Historical: Möbius-matrix and `TransformCache` round-trip tests were retired with the legacy solver in Stage 84; see `docs/stage84_test_migration.md`.)*

### 29.2 Integration tests

- Plan ↔ physical path agreement: for a valid plan, the physical trace should match up to the cursor (no divergence).
- Effect consistency: transformative inverse matches forward. Projective back-propagation is consistent with forward application.

### 29.3 Invariant-based testing

For each level, sweep a **grid** of player positions × cursor positions. Plus a custom list of off-grid positions for both (corners, surface endpoints, near-degenerate points). All combinations must preserve the invariants below.

**Grid**: evenly spaced within the level bounds. Resolution configurable (e.g., 20×20 player × 20×20 cursor = 160,000 combinations).

**Custom positions**: per-level `test_positions` array in the level data, targeting known sensitive locations. test_positions are used as both additional player positions AND additional cursor positions in the sweep grid.

Invariants are split into two tiers: **user experience invariants** (observable by the player — if violated, the game visibly misbehaves) and **systemic invariants** (internal correctness — if violated, the engine produces subtle bugs). Both tiers are dynamically checked at runtime for every test combination.

#### Tier 1: User experience invariants

**UX1. Visibility predicts non-divergence.** If the cursor lies within a visibility region, the planned and physical paths agree up to the cursor (no divergence). **Tested as:** `VISIBILITY_EQUALS_NON_DIVERGENCE` (`false visible`).

**UX2. Divergence implies outside visibility.** If the planned and physical paths diverge, or the cursor is not reached, the cursor is outside every visibility region. The converse also holds: a cursor reached with aligned paths is inside (§15.1). **Tested as:** `VISIBILITY_EQUALS_NON_DIVERGENCE` (`false shadow`).

**UX3. Physical preview matches arrow flight.** The non-red sections of the preview (ALIGNED + ALIGNED_POST_PLANNED + DIVERGED_PHYSICAL) exactly match the arrow's actual flight path when fired. Same hit points, same surfaces, same order. *(Principle 2.)* Since Stage 134 (the merge by segments, §14.5) the check reads the physical path's significant points and its end: each, in order, the end of a non-red step, and the non-red steps one unbroken route (as `PHYSICAL_CONTINUITY` reads one). **Tested as:** `PHYSICAL_PREVIEW_MATCH`.

**UX4. Firing the same shot twice produces the same result.** Same position, cursor, plan, and game state → identical arrow path. *(Principle 17.)*

**UX5. Undo fully restores.** After undoing a shot, the game is indistinguishable from just before that shot — same player position, surfaces, plan, and target progress. *(§3.3.)*

**UX6. All targets reachable.** If the player has hit all targets (across one or more shots), the level is complete. No target is silently "unhit" despite being contacted. *(§24.2.)*

**UX7. Plan preview shows the plan.** The solid-colored path (ALIGNED + DIVERGED_PLANNED) forms a continuous path from the player toward the cursor (Stage 134: unless the walks leave the player apart, a realization the other way round a circle, and share no run to draw). *(§4.3.)* **Tested as:** `UX7`, `GREEN_FROM_PLAYER`, `SOLID_PATH_TO_CURSOR`.

**UX8. Plan feedback is immediate.** The preview updates every frame as the cursor moves, showing which planned entries are active. *(§4.2.)* Not directly testable as a unit invariant.

**UX9. Block surfaces stop the arrow.** When the arrow hits a terminal surface, it stops. No pass-through. *(§10.5.)* **Tested as:** `TRACE_ENDS_AT_SURFACE_OR_BOUNDS`.

**UX10. State changes are visible during flight.** *(PLANNED — depends on state changes §10.6.)* Surface state changes appear at the moment the arrow reaches the triggering hit point.

**UX11. Empty plan = fire straight.** With no plan entries, the arrow fires straight toward the cursor. *(§13.5.)*

#### Tier 2: Systemic internal invariants

**S1. Carrier ↔ via.** *(Retired as a cache check: no `derive_via` exists; segments keep their three points, §8.4.)* The carrier derived from start/end/via contains all three points (see S11).

**S2. Exact reversal.** A cancelling re-hit restores the earlier beam exactly (provenance stack, §10.7, §17.1). *(The legacy `BACK_TRANSFORM_ALIGNMENT` check over `TransformCache` Point IDs was retired in Stage 83/84.)* **Tested as:** `EFFECT_LAW` / `BEAM_ORDER` (sweep) and Stage 81–82 unit tests.

**S3. Determinism.** Same inputs → identical step trees (same count, types, coordinates, frame IDs). *(Principle 17.)*

**S4. Divergence is monotonic.** Once divergence occurs at index `i`, all steps at `≥ i` are diverged types. No re-convergence. *(§14.2.)* **Tested as:** `SINGLE_DIVERGENCE`.

**S5. Aligned steps share provenance.** For ALIGNED/ALIGNED_POST_PLANNED steps, planned and physical have identical `start` and `frame_id`. *(§14.8.)*

**S6. Aligned steps match.** Before divergence: same hit surface ID, side, and frame ID. Checked by provenance, not coordinates. *(§14.8.)* **Tested as:** `PLAN_EFFECTS_APPLIED`.

**S7. Per-entry state matches.** *(PLANNED — depends on state changes §10.6.)* For aligned steps: the planned trace's `state_at[i]` equals the physical trace's game state at step `i`.

**S8. Projective hit ordering.** Events on one beam follow the projective order from the departure parameter (§11.3); each arc starts where the previous one ended. **Tested as:** `BEAM_ORDER`, `PHYSICS_COMPLIANCE`. *(`PARAMETER_MONOTONICITY` on legacy ray parameters was retired.)*

**S9. Departure respected.** The departure point is not re-hit as an ordinary candidate before the full loop, and no zero-length step is emitted (§11.4). **Tested as:** `ORIGIN_NOT_REHIT`.

**S10. Projective clears the stack.** After every applied projective hit the provenance stack is empty and a new epoch begins, so the next steps get a fresh frame id (§10.7). (Replaces the legacy "frame resets to identity"; the frame id is *not* `IDENTITY_FRAME` after a projective.)

**S11. Three points on carrier.** Start/end/via evaluate to ≈0 under the carrier equation. *(Derivation validation.)* **Tested as:** `HITPOINT_ON_CARRIER`.

**S12. Side determination consistent.** LEFT/RIGHT at every hit matches the cross-product formula (§9.2). *(§9.2.)* **Tested as:** `ON_SEGMENT_CONSISTENCY`.

**S13. Visibility: no self-intersection.** Region boundaries do not self-intersect. **Tested as:** `VIS_SIMPLE`, `VIS_CLOSED`.

**S14. Visibility: edges on geometry.** Every polygon edge lies on a surface carrier (on its segment where the walk fires there, and at a departure whose entry transforms; a pass or state entry is consumed anywhere on its carrier, B1), on a member (a beam through the player's image `P_K` and the frame's infinity `Q_K`: a ray from `P_K` when `Q_K` is infinity), or on the clip rectangle. **Tested as:** `VIS_EDGE_ON_GEOMETRY`, `VIS_VERTEX_PROVENANCE`, `VIS_SHADOW_ANCHORED`.

**S15. Visibility: non-overlapping.** Distinct regions do not overlap, and their boundaries share no edge piece. **Tested as:** `VIS_DISJOINT`, `VIS_ORIENTED`.

**S16. No NaN/Inf in output.** No NaN/Inf in any coordinate field (except the `Vector2(INF, INF)` escape sentinel). *(§31.)* **Tested as:** `S16`.

**S17. Provenance IDs unique.** *(Retired: `Point` has no ID since Stage 84; exact-point provenance is the solver's known points, §31.4.)*

**S18. Frame determinant non-zero.** *(Retired in Stage 83/84: there are no frame matrices.)*

**S19. Trace preserves real state.** *(PLANNED — depends on state changes §10.6.)* After a preview trace, `GameState.flags` is unchanged.

#### Additional implemented invariants

The following invariants are tested by the sweep but not in the original specification:

| Check name | Description |
|------------|-------------|
| `PREVIEW_NOGAPS` | No gaps between consecutive step endpoints in the preview path |
| `PHYSICAL_CONTINUITY` | Step endpoints connect seamlessly (end of step N = start of step N+1) |
| `VIA_ON_ARC` | Step via points lie on the step's arc carrier |
| `VISUAL_ON_PHYSICAL_CARRIER` | Visual segment points lie on the physical carrier |
| `DIRECTION_ONLY` | The physical trace depends only on the aim direction: the planned shot's physical hits on their segments (Stage 139: a plan surface's crossing off its segment is an event of the planned shot alone) equal those of a converged shot without plan aimed at the cursor's image (computed from exact data and rounded once where the plan's maps are exact data, Stage 105; bounded by tangent-linear sensitivity to that image's float64 rounding; projective plans skipped until Stage 87) |
| `NO_POST_CURSOR_DIVERGENCE` | A path fully aligned up to the cursor does not diverge after it. The cursor is the planned display path's `cursor_index` (a divergence exactly at the cursor is after it); the check holds only when both walks leave the cursor's point in one frame (§15.1 V3), and the shorter of two truncated paths is not read past its end (Stage 110). Stage 134: "fully aligned" is no planned step diverged, and a tail from the end of a walk the limits cut is not a divergence |
| `EFFECT_LAW` | Every applied effect maps its beam by its exact law (§10.8) |
| `EVENT_ON_BEAM` | Every event point lies on its beam, and every hit on its surface's carrier |
| `REAPPLY_IN_PLACE` | No zero-length loop: an applied hit, then only cursor / infinity events at the same point, then an applied hit on the same surface at the same point never happens (a teleport moves the beam: a round trip through a portal pair may re-hit the same portal at the same point) (§11.4, Stage 106) |
| `BEAM_ORDER` | Consecutive events on one beam chain their parameters in walk order |
| `VIS_FINITE` | Every point of a visibility region is finite |
| `VIS_NO_BLOCKER_INSIDE` | No region lies inside a two-sided blocking surface (not planned, not through a pole) |
| `VIS_SOURCE_ON_BOUNDARY` | The last entry's departure points belong to the region |
| `VIS_STAR` | Each cell's middle member lies in the region over its visible intervals |
| `VIS_CELL_CONSTANT` | A cast inside a cell has the cell's walk shape (no critical direction missed) |
| `VIS_ITERATION_CHAIN` | The departure points lie in the region of the plan without its last entry |
| `VIS_BOUNDARY_DECIDED` | At every exact point of interest (an endpoint, a piece's end) the region decides as the solver |
| `VIS_CONTAINS_MATCHES_POLYGON` | The merged loops and the cells agree away from the boundary |
| `VIS_DRAWN_MATCHES` | The drawn fill (one mesh of the cells) covers a point once where the region contains it and never elsewhere; its area is the cells' (Stage 110, F8) |
| `VIS_DETERMINISTIC` | The same inputs give the same region (S3) |
| `VIS_REGULAR` | The region has area wherever it is: its only 1D parts are an arc of a member tangent at a frame's finite infinity point (`Q_j`) to a carrier through it, a piece of the last planned terminal's segment, and (Stage 113) a crack along a dead member; no loop doubles back on itself (B4, Stage 110) |

*Retired with the legacy frames (Stage 83):* `SHARED_RAY`, `RAY_ALIGNMENT`, `ARC_MIDPOINT_ALIGNMENT`, `POST_INVERSION_ARC`, `PARAMETER_MONOTONICITY`, `BACK_TRANSFORM_ALIGNMENT`, `S18_FRAME_DETERMINANT`.

### 29.4 Test level data

Phase 10 must define at least **4 test levels** corresponding to the worked examples in §16 (simple reflection, circle inversion, divergence, projective break point). Each test level includes expected outputs (step coordinates, step types, bounce points) stored alongside the level data for automated comparison.

### 29.5 Visual regression

Screenshot comparison for known test scenes at known positions. Catches rendering bugs (wrong arc direction, missing segments, incorrect colors).

---

## 30. Performance budget

### 30.1 Frame budget

Preview must update **every frame at 60fps** (~16.6ms per frame). Target: preview computation (planning + physical trace + visibility + visual conversion) should complete in under **5ms** on a mid-range CPU, leaving ~11ms for rendering and input processing. The preview computation includes:
- Planning algorithm.
- Physical trace (up to 32 steps).
- Visibility computation. *(Stage 110: recomputed only when the player, the plan, the flags or the surfaces change, never for the cursor; a region costs about 6 ms with no plan and 15–27 ms with a one-entry plan on `main.tscn`, more than this budget, so it is computed on a worker thread and the frame stays within budget -- docs/TDD_14_VISIBILITY.md, "Performance".)*
- Math→visual conversion.
- Draw calls.

### 30.2 Worst case

32 steps × N surfaces per step = O(32N) intersection tests per frame. For small N (< 100), this is within budget. For larger scenes, spatial indexing (grid or BVH) may be needed.

### 30.3 Optimization targets

- **Incremental recomputation**: if only the cursor moved (not the plan or player), recompute from the last unchanged step.
- **Recompute only on change** *(implemented, Stage 100)*: the preview, the hover and the plan overlays are recomputed only when their inputs changed (player, cursor, plan entries, state flags, surfaces); a frame in which nothing moved does no solver or draw work. Scene defs for the solver are built once per level.
- **The frame never waits for the solver** *(implemented, Stage 121, docs/TDD_26_ASYNC.md)*: in a live game (asynchronous) the visibility region is computed in the background and the frame shows the last finished one meanwhile (while the player keeps moving a running region finishes and shows; only a newer one replaces it). When the float64 preview cannot decide exactly, the frame shows it provisionally while the exact climb runs in the background; only the exact preview drives the rules (hover and plan overlays, the spawn preview, the record, a fire, which waits for it). While the cursor rests on a plannable side and nothing else computes, the region that side's click would make is computed ahead (prefetch), so the click shows its region at once. Replays and tests run synchronously (everything in the frame, in order): the asynchronous game, once its work is done, reaches the synchronous game's state byte for byte.
- **Every visual change is animated** *(Stage 122, docs/TDD_27_PRESENTATION.md; presentation only: the game's state, its record and every result are unchanged)*: the player is drawn between its last two physics ticks, so motion is smooth at any display rate. The smoothing covers lateness only: an asynchronous frame waits for the work it started up to its budget (`rc_game_config.budget_ms`), and what finishes in time is drawn as it is, with no smoothing. Only a late visibility region -- one that missed its frame and whose computation took 1/30 s or more, slower than 30 Hz (Stages 124, 126: the computation, not the wait behind the region computing before it) -- morphs in from the shape on screen, in about 100 ms (ease-out), as a diff of shapes, with crisp straight edges and no blending: its pieces (the cells) pair by place, each pair's vertices are aligned like a diff (a vertex on the same surfaces before and after pairs with itself), and an unpaired vertex moves to (from) the closest point of the other shape between its paired neighbours; a piece with no partner grows out of the other shape's closest edge. While a region is late (a newer one computing, the shown one computed slower than 30 Hz), the shown one, when its rays leave the player, follows the drawn player: its shadows' far ends recast through their occluding corners. A newer region retargets the morph from the shape shown; every morph ends on the exact region. The path preview likewise (Stage 126): a preview shown provisionally whose exact climb took 1/30 s or more morphs in from the path shown, in about 80 ms -- its steps aligned like a diff by the surfaces they hit, a paired step's points interpolated (segments and arcs through three points), a step without a partner growing out of (shrinking into) the point where it joins, its colour switching at the midpoint; any other preview is drawn as it is.
- **Regions on time** *(Stage 123, docs/TDD_28_FAST_REGIONS.md)*: the target is at least 120 Hz (an 8.3 ms frame; the Godot host waits up to 4 ms for a frame's work), so that the smoothing above does not trigger. Precision is spent only where a decision can differ (§31.4 ties by identity, §31.5 what a caller reads). A region with no plan is built within 4 ms on every shipped level (Stage 124: within 1.6 ms); a region with a plan is not yet (Stage 124: 4.8–17.6 ms on 11 levels, docs/TDD_29_PLANNED_REGIONS.md), and is smoothed meanwhile.
- **Spatial indexing**: broad-phase rejection of distant surfaces.
- **Precision on demand** *(implemented)*: the float64 `FastSolver` pass is accepted when its `ErrorEstimate` is ≤ 0.01 px and every decision margin exceeds its error. Only otherwise does `PrecisionLadder` climb BigFloat tiers (§31.5). The float64 scene and the last trace pair are cached (§17.2). *(Replaces the legacy "frame transform caching" of `M⁻¹`, which no longer exists.)*

---

## 31. Stability and numerical requirements

### 31.1 Direction representation

The aim is a two-point `Direction` (player → cursor), with no stored angle. It is immutable and shared by every step of a trajectory (§8.3). Inside the solver, a beam's direction is its tangent `τ`, a unit complex number at the tier's precision, carried exactly through the closed-form effect maps (§10.8). Exact straight beams are additionally kept as exact implicit lines (§31.4). Generalized circle coefficients are derived from points only when needed.

### 31.2 Ordering

- Cross-product sign against a fixed reference direction. Never `atan2`.
- Hits in projective order of the beam parameter `t` from the current parameter (§11.3). Never by distance from an unrelated point.
- Points known by provenance (known points) keep their exact parameter. Concurrent crossings on an exact line, or at a common point an exact circle passes, share one parameter (§11.4).
- Ties: cursor first, then surface ID; the departure re-hit is last. *(Deterministic, total order.)*

**Cross-products over angles:** all internal geometric decisions (arc containment, winding direction, radial ordering, sweep direction) use cross-product sign tests, never atan2. Angles appear only at the Godot API boundary (`draw_arc()` parameters) and are computed from points at the last possible moment during visual conversion (§7.3).

### 31.3 No epsilon decisions

Do not use `|x| < ε` for decisions that change topology (which hit wins, on-segment, vertex order). Epsilon logic varies with scale and platform. Prefer structural or provenance facts.

**Collinearity in particular:** whether a segment is a line or arc is determined by **construction provenance**, not by coordinate comparison. A segment is a line if its three defining points were placed collinearly by the level editor (stored as a construction fact in the serialized cache) or if the via is `Vector2(INF, INF)`. At runtime, no floating-point collinearity test is performed — the carrier type (line vs circle) is a stored decision, loaded from the serialized cache.

**How the beam solver avoids epsilons** (oracle `trace2` = solver; `docs/TDD_09_PRECISION_INTEGRATION.md`, "Exactness rules"):
- **Exact predicates** (`Exact`) decide incidence (a known point exactly on a carrier), endpoint identity, tangency of an exact line to a circle, and concurrency of line carriers. A float64 evaluation with a certified error bound handles the cheap case, and exact BigFloat arithmetic handles the rest. The bound only selects the path; it never decides a result.
- **Provenance** (known points, exact lines, the stack, §31.4) replaces coordinate comparisons. For example, a hit at a segment endpoint lying on an exact line *is* the endpoint.
- **Rounding never decides an endpoint, an incidence, a tangency, or a root at the antipode** (`t = ∞` is exact).
- **Parallel carriers** (Stage 112, K10): a beam carrying an exact line (the aim, a projective's exit line, their reflections and translations) never meets a line carrier parallel to it: the crossing exists iff the exact cross product of the two directions is nonzero (`a·Δx + b·Δy ≠ 0` on the line's exact coefficients and the segment's float64 endpoints). A tier's rounded tangent lies an ulp off the exact line, so its root on a parallel carrier would sit at `|y| ≈ 2⁶¹` (or `10⁸⁰` in the oracle), before the wrap through infinity: an event that does not exist. *Oracle:* `trace2._parallel`; native `exact::Line::parallel_to`; fixtures `bench/fixtures/stage112_parallel.json`.
- **The departure point is a record, not a comparison** (Stage 106): push, pop and portal tag the point the beam departs from in the known list (`skip` = the departing surface); on a resumed beam the departing carrier's root nearest the departure parameter is the departure. The same point on the same beam is recognised by its parameter as well as by its data.
- **The pole record** (Stage 105): every beam of an epoch is the image `F(ℓ)` of a straight line under the frame's maps and passes `Q = F(∞)`, its direction there being the image of `ℓ`'s direction. Beside the approximate `Q` the walk keeps `{Q, D}` as exact homogeneous data: set by the pre-transform (the start record of a direction or aimed shot) or by the first inversion of an exact straight beam (`Q` the center, `D` the line's direction); carried through reflections (`Q` reflected, `D' ∝ M²·conj(D)`), inversions (`Q` inverted, `D' ∝ conj(D)·U²`, `U = Q − C`), translation portals (`Q + d`); saved and restored by the stack; dropped by a rotated portal, a projective, an inversion about `Q` itself (the beam becomes straight) or the exact-data budget. Incidence of a carrier with `Q` and tangency there are exact predicates on the record.
- The remaining tier-precision decisions (on-segment cross-ratio, side, root order) have their **margins** checked against the estimated error. `PrecisionLadder` raises the precision until the trajectory is reliable (§31.5).
- Zero-length steps are dropped by exact equality of endpoints (`start == end`) in `BeamTracer`.

*Historical:* the v0.7.0 table of epsilon replacements (`projective_sort`, `at_which_endpoint`, `endpoint_blocked_sides`, `maps_lines_to_arcs`, Tracer zero-length step) referred to the legacy solver, deleted in Stage 84.

Testing-layer tolerances (e.g., in `invariant_checker.gd`, and the 0.01 px trace tolerance) are unaffected. They measure rendered pixel positions, not topology-changing decisions.

#### 31.3.1 Three-tier endpoint detection

*Historical (legacy `Tracer`/`intersection.gd`, deleted in Stage 84):* endpoints were detected by (1) exact match with the ray's defining points, (2) exact cross product `== 0`, and (3) quadratic roots snapped to the endpoint.

**Beam solver equivalent:**
- **Known points:** every segment endpoint and via lying exactly on an exact line is a **known point** of the beam. The player, the cursor and exact departure points are known points too (§31.4), and so is a planned projective's hit point that is exact data (K11, §13.3: a data point of its segment, or, Stage 125, a point with float coordinates exactly), on every beam whose exact line or circle passes it.
- **Incidence:** a surface whose carrier contains a known point exactly takes that point's parameter and exact coordinates for its nearest root (§11.1). Endpoint identity is therefore decided by provenance or exact sign, never by distance.
- **Shared points:** whether a hit point is held by another present surface is decided by provenance (a known point, an endpoint equal by data) or by an exact tie (§11.4), never by distance; a shared point where two or more surfaces have a consequence stops the arrow, a lone endpoint grazes (§11.1, Stage 138).

### 31.4 Provenance

When the system classifies a point (on-segment, which side, intersection index), that classification is stored and reused. Recomputation from coordinates can contradict the original decision.

In the beam solver, provenance takes three forms (`beam_solver.gd` header, `trace2.py`):
- **Known points:** exact data points a beam passes through, each with its parameter. These are:
  - the player (initial beam, `t = 0`);
  - the cursor, whenever it is a candidate, and on every beam whose exact circle (§31.4) passes it (K7: a carrier through the cursor is crossed exactly there, so a departure point at the cursor is the cursor exactly and the cursor is reached at once);
  - the departure point of a beam born at an exact hit point;
  - every known point of the previous beam lying on the carrier of the surface applied (an effect fixes its carrier pointwise);
  - every segment endpoint and via on an exact line;
  - a planned projective's hit point that is exact data -- a data point of its segment (K11, Stage 113), or a line normal's foot or a line directional's crossing with float coordinates exactly (Stage 125) -- on every beam whose exact line or circle passes it;
  - for a visibility cast along a member through exact data -- a segment's endpoint or a piece's end, or a crossing of two carriers whose coordinates lie exactly on both (§15.3) -- found after the plan's first j entries: that point on the beam after those entries, when the beam passes it to rounding (Stages 123–124: the member passes it by construction, both ways; in every pencil; not a point on the carrier the beam departs after entry j, whose departure point is its own).

  Pops restore the earlier beam's known points.
- **Exact lines:** a straight beam defined by exact data is kept in implicit form `a x + b y = c` with exact coefficients. Sources:
  - the initial beam of a trace without a plan, or of a mirror-only plan (exact rational aim image);
  - the reflection of an exact line in a line mirror (an exact map of the coefficients);
  - an exact line inverted in a circle whose center it contains;
  - an exact projective exit.
- **Exact circles** (Perf P2b): every beam of an epoch is the image of a line under the frame's maps, so a beam that had an exact line (or, K9, an aimed pre-transform start: the circle through the player, the exact aim image and `Q0`) keeps an exact **generalized circle** `A (x² + y²) + B x + C y + D = 0` through every reflection, inversion and portal translation since. It is carried lazily: the exact line and the maps, made exact only when a decision needs it, within an exact-data budget. Whether it passes a point is decided by pulling the point back through the maps to the exact line (Stage 124: a reflection and an inversion are their own inverses), the same exact answer on smaller numbers. The stack saves and restores it. A projective, a rotated portal or a beam that never had an exact line has none, and makes no exact claim.
- **Ties by identity** (Stage 123: points equal by definition are never decided by precision): crossings of carriers concurrent at one point of an exact beam are one tie group, ordered by id, however many carriers meet there; a cursor at a known point is that point (its parameter and position); surfaces on one carrier cross it at one shared root, the same parameter, one decision ordered by id.
- **The provenance stack:** re-applying the surface on top of the stack (or hitting a portal's pair) pops it and restores the earlier beam *object*. A projective clears the stack and starts a new epoch. `frame_id` identifies `(epoch, stack)`.

### 31.5 Precision and stability (formerly: frame composition stability)

There is no frame composition. Numerical stability of long effect chains (inversion chains amplify errors exponentially) is handled as follows:
- **Anchored planned path:** each planned beam `B_j` is built independently from exact data, with exact Möbius difference formulas for `P − Q` (§13.1), so planned errors do not accumulate along the plan.
- **Dynamic precision** (`PrecisionLadder`):
  1. A float64 pass (`FastSolver`, with tangent-linear derivatives) yields the trace and its a-priori error envelope (`ErrorEstimate`: `est_i = C·eps·max_{j≤i} G_j`).
  2. The pass is accepted if the estimate is ≤ 0.01 px and every decision margin exceeds its error.
  3. Otherwise BigFloat tiers (from at least 112 bits, up to 1792) are climbed until two consecutive tiers agree within tolerance with identical event structure. A tier measures a gap within float64's reach of zero at its own precision.
  4. A caller that reads only part of the walks asks for precision only there (Stage 123): a visibility cast reads its physical walk through the first stop after the plan's entries (a hit applied or changing the flags) and its planned walk through the events at its cursor (Stage 124: with no cursor, through the event consuming the plan's last entry); a decision past them is not climbed for, and its walks end two events past them (Stage 124). The preview and the fire read whole walks.
- **Provenance stack:** cancelling re-hits restore earlier beams exactly instead of recomputing through the chain.
- **Event limits** (10 significant, 64 in all; §12.6) bound chain length.
- **Projectives** clear the stack and start from an exact line when the hit and axis are exact.

*(Manual overrides, §17.4, are not implemented.)*

### 31.6 Degenerate cases

| Case | Resolution |
|------|-----------|
| Inversion of a point at the center | Maps to ∞ (an exact `null` point in the anchored chain). Player or cursor exactly at an inversion center: forward walk from the exact aim direction (§11.4). |
| Inversion of a beam through the center | Result is exactly straight when the center is a known point of the beam (exact line). |
| Zero rotation and displacement | Identity — valid no-op. |
| Parallel straight beam and line surface | No crossing (the linear equation has no finite root). |
| Exact line tangent to a circle carrier | No event (§11.4). |
| Beam departing a surface | Departure root excluded as an ordinary candidate; re-hit last; a pop tags the pop point as the departure point (§11.4). |

### 31.7 Consistency

- Trajectory and visibility use the same intersection pipeline, same scene state.
- Planning and physical tracing use the same effect implementations.
- Projective back-propagation and forward application are consistent.
- Screen boundaries are surfaces, not special cases. *(Principle 7.)*

### 31.8 Error handling policy

In **debug/development** builds: crash with a descriptive message on violated invariants (carrier-type mismatch, NaN in output). `BeamTracer.trace_pair` returns null, so nothing is drawn or fired, for degenerate input (player = cursor) and for traces with non-finite event points. Early failure surfaces bugs before they compound.

In **release** builds: log the error and recover gracefully — skip the problematic step, continue the trace, and display a visual indicator that something went wrong. The game should never hard-crash during gameplay.

---

## 32. Terminology

| Term | Definition |
|------|-----------|
| **Point** | 2D position (`Point.coords`). Exact-point provenance lives in the solver (known points). |
| **Provenance** | The stored reason a point or beam exists (known points, exact lines, the provenance stack) — reused, not recomputed (§31.4). |
| **Direction** | Immutable two-point line orientation. Used for the aim (player → cursor). |
| **Ray** | Origin + Direction. The aim ray shared by a trajectory's steps; escape steps carry tangent rays. |
| **Beam** | The arrow's current path in visual space: an oriented generalized circle `z(t) = p + tτ/(1 − iκt/2)` (`HPBeam`). Effects map it in closed form. |
| **Event** | One solver record along a trace: a carrier crossing (`hit`), the `cursor`, `origin`/`escape`, or `max`. Every event counts toward the 32 limit. |
| **Generalized circle** | A line or circle, unified by the equation `a(x²+y²) + bx + cy + d = 0`. A derived value — computed from a segment's three points and cached bidirectionally. |
| **Carrier** | The unbounded generalized circle a segment lives on. Derived from the segment's three points, not stored. |
| **Segment** | Three points — start, end, via — that fully define a bounded path and its carrier. |
| **Via** | Any point on the segment between start and end. The point the path passes through — disambiguates which arc/path is intended. `INF` for segments through infinity. Recoverable exactly from a cached carrier via reverse derivation. |
| **Collision body** | A `StaticBody2D` with `CollisionShape2D` used for player physics only. Invisible to arrow tracing, visibility, and planning. Not a Surface. Defined in `LevelData.collision_bodies`. |
| **Surface** | Segment + per-side effect configuration + stable ID. |
| **Side** | LEFT or RIGHT, determined by traversal direction (start → via/end). Uniform for all segments. |
| **Plan** | Ordered list of `{surface, side}` entries the player intends the arrow to hit. Each entry specifies which surface and which side. |
| **Hit** | Where a beam crosses a surface's carrier. A `hit` event with parameter, point, surface, side, on-segment flag (adapter: `TraceHit`). |
| **Step** | One leg of a path: start → end (visual), with frame id and hit info (`TraceStep`). |
| **Step chain** | Ordered list of steps forming a continuous path. |
| **Step tree** | All paths for one shot sharing a prefix, with explicit branch points at divergence. |
| **Divergence** | Where planned and physical paths split. A first-class event, not a rendering trick. |
| **Normalized frame** | *Historical (removed in Stage 84):* the legacy per-step coordinate system where rays were always lines. All computation is now in visual space (§6). |
| **Visual frame** | The coordinate system the player sees, and the only one the solver uses. Beams may be lines or arcs. |
| **Frame transform** | *Historical (removed in Stage 84):* the legacy per-step Möbius map normalized → visual. Replaced by the provenance stack and frame id. |
| **Provenance stack** | The solver's stack of applied transformative effects (each with the beam before it). Push on apply; pop on re-hit of the top's surface or a portal's pair; cleared by a projective (new epoch). |
| **Frame id** | Id of `(epoch, provenance stack)` carried by every step; equal ids = same applied effects. Compared by the step tree merge. |
| **Anchored planned path** | The planned beams `B_j` built independently from the image chain (`AnchorChain`), §13.1. |
| **Transformative effect** | Fully invertible Möbius transformation (reflection, inversion, rigid motion), applied as a closed-form beam map. Supports image-chain planning. |
| **Projective effect** | One-way effect where outgoing ray depends only on hit point. Supports back-propagation planning. |
| **Terminal effect** | Block — stops the ray. |
| **Compound effect** | Multiple transformative effects composed into one effect (planned). |
| **Rigid motion** | Rotation + translation (Euclidean isometry excluding reflections). |
| **Transformative sub-chain** | A contiguous sequence of plan entries with transformative effects, bounded by projective break points (or the start/end of the plan). Solved as a unit using image chains. |
| **Break point** | A projective surface that clears the provenance stack (new epoch) and partitions the plan into transformative sub-chains (planning: Stage 87). |
| **Image** | Virtual point from reflecting/inverting a real point through surfaces — used to "unfold" bounces into straight lines. |
| **Back-propagation** | Finding the hit point on a projective surface given a target on the outgoing side. |
| **Beyond-infinity hit** | A hit reached after the beam parameter wraps through `t = ∞` (projective order, §6.3). |
| **Pass-through** | A surface side with no effect. The ray continues as if the surface is absent on that side. |
| **Arc chain** | Open sequence of connected segments. Used for paths. |
| **Generalized polygon** | Closed chain of segments with line and/or arc edges. |
| **Visibility region set** | Zero or more polygons representing valid aim regions. May be disconnected. |
| **Scene state** | Scene geometry + current game state. What the simulation sees. |
| **Computation cache** | *Historical:* the legacy `TransformCache` (removed in Stage 84). Exact reversal is now by the provenance stack (§17). |
| **Manual override** | Level-declared exact algebraic identity (Principle 23). Not implemented (§17.4). |

---

## 33. Open design decisions

The following are intentionally left open — they are content or product design decisions that can vary without changing the engine. They are listed here to distinguish "the spec intentionally does not prescribe this" from "the spec accidentally omitted this."

| Decision | Notes |
|----------|-------|
| **Level progression structure** | Linear, world-based, hub, or open — not prescribed (§3.6). |
| **Scoring and par** | Whether levels track shot count, time, or have par/star ratings. |
| **Art direction** | V1 is wireframe (§22.1). Future art style is open. |
| **Sound design and audio system** | Bounce cues and UI sounds are mentioned but sound architecture (positional audio, sound list, audio channels), music, and audio mixing are not specified. |
| **Advanced level editor features** | Undo/redo in editor, copy/paste surfaces, snap-to-grid, etc. |
| **Settings screen contents** | What display settings exist (fullscreen, vsync), whether controls are rebindable, audio channel breakdown (master/SFX/music) — all left to the implementer. |
| **Multiplayer** | Not addressed. The engine is single-player. |
| **Mobile/touch input** | Input model assumes mouse+keyboard and gamepad. Touch adaptation is open. |
| **Accessibility** | Colorblind modes, input remapping, difficulty options — not specified. |
| **Localization** | Not addressed. |
| **Level load transitions** | Whether to add animated transitions between levels. |
| **Plan undo** | Ctrl+Z to restore a cleared plan — not specified for v1. |
| **Gamepad plan construction** | Surface cycling and selection via controller — not specified for v1. Keyboard+mouse is the primary input method. |
| **Moving/animated surfaces** | Dynamic surface positions and animation — not in scope for v1. |
| **Godot signal architecture** | Signal/event design between game manager, player, UI — left to the implementer. |

---

**Current version: 2.66.0.** See [CHANGELOG.md](CHANGELOG.md) for revision history.
