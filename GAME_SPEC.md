# Ricochet Game v2 — Complete Game Specification

This is the single source of truth for the ricochet game: design, principles, architecture, data structures, algorithms, game loop, player interaction, level editing, implementation phasing, and testing strategy. It is self-contained — no other document is required to implement the full game.

Engine: **Godot 4.7+** with **GDScript**. Developed against Godot 4.7.x stable. IDE: **VSCode**.

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
| Invariant sweep testing (event-based checks, all scenes) | Done |
| Compound transformative effect | Planned |
| Visibility polygon (§15) | Planned |
| State flags — ConfigResolver + StateChange (§9.4, §10.6; Stage 85) | Done |
| Ghost mode & toggler surfaces (Stage 86) | Done |
| Plannable infinity — the ∞ plan entry, the `infinity` event (§4.2, §6.3, §12.5, §13.1; TDD_10 Stage 88) | Specified (oracle + fixtures, Stage 88a); implementation pending (Stages 88b–d) |
| Level editor | Planned |
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
| **Win** | All target surfaces in the level have been hit (across one or more shots). |
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
- **Click position**: the plan entry also remembers *where* on the surface the player clicked. It settles the one kind of fork in planning. A circle projective (§13.3) can have two hit points whose projected beams both pass through the target; the one nearer the click is taken, so every plan has exactly one path. Without a click, the segment's midpoint stands in.
- **Highlighting**: when the cursor hovers over a surface, the approachable side highlights to indicate it is clickable.
- **Plan display**: planned surface sides are visually marked (e.g., numbered overlay, distinct color on the planned side).
- **Preview**: the trajectory preview updates in real time as the plan and cursor change.
- **Infinity entry** (Stage 88): clicking within the click tolerance **inside the level bounds' edge**, where no interactive surface wins, appends the entry **∞** (`INFINITY_ID = -3`, no side). It means "the beam wraps through the screen edge here" and selects the plan's second realization (§13.1). Hovering the edge band highlights it; planned ∞ entries are marked on the edge like planned sides. While the plan's simulated state is ghosted the entry is still allowed: a ghost arrow wraps.
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

The beam parameter is **projective**. From `t = 0` (at `p`) the beam runs forward through `t → +∞`, wraps to `t → −∞`, and returns towards `p`. A straight beam passes through the point at infinity. On a circle beam, `t = ∞` is the antipode of `p`, an ordinary finite point (an exact value in every precision tier). Candidate hits are ordered **from the current parameter** in this cyclic order, so a hit "beyond infinity" is simply a later candidate (§11.3). In the visual plane, wrapping through ∞ on a straight beam means leaving the screen and returning from the opposite side (§12.5). The wrap of a straight beam is itself an event, `infinity` (Stage 88): ordered between the candidates ahead (`t > t0`) and the wrapped ones, counted toward the hit limit like a pass, with no point. A circle beam never emits it (its `t = ∞` is a finite point).

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
    is_target:    bool           # true if this is a goal surface
    player_solid: bool           # true = player collides with this surface (default true)

SideConfig:
    effect:       TransformativeEffect | ProjectiveEffect | TerminalEffect | null
    state_change: StateChange | null
    interactive:  bool    # can the player add this side to the plan?
                          # default: true for transformative/projective, false for terminal, false for null
```

# player_solid affects ONLY CharacterBody2D collision. Arrow tracing and visibility always interact with all surfaces regardless.

Surface segments are defined and stored in **visual (world) coordinates**. The beam solver intersects beams with them where they are, in visual coordinates (§11, §12.1). Surfaces are never transformed. For the solver, `BeamScene.defs_from_surfaces` converts the surfaces into JSON-able scene defs, the same format the oracle traces.

`interactive` is a **static** property — it does not change with game state. It is set once in the level editor and serialized. The ConfigResolver may change the effect, but the interactivity of a side is fixed.

Terminal sides default to `interactive = false` but can be explicitly set to `interactive = true` by the level designer to allow planning shots that intentionally hit block surfaces (e.g., to trigger state changes).

Pass-through (null effect) sides must have `interactive = false`. A null-effect side has nothing to plan for — making it interactive would create entries that are always processed as pass-through. This is enforced at load time.

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

Stops the beam. No outgoing beam, no stack change. A trace cut by the **32-event limit** (§12.6) ends similarly. It ends with a "max" event at the next crossing, drawn up to that crossing with no effect.

### 10.6 State changes

> **Status: implemented (Stage 85).** `StateChange` has two ops, `SET` and `TOGGLE`; a toggle of a missing key counts the key as false.
> - **Firing:** a change fires when its side fires. In PHYSICAL that is a fully blocked hit; in PLANNED it is the consumed plan entry.
> - **Order:** the side's config is resolved before its own change is applied.
> - **Promotion:** the physical trace's final flags become the game state when the arrow lands. Undo restores the checkpoint's flags.
> - **Pops** restore beams, never flags.

Orthogonal to the three effect categories. Any surface side may carry an optional state change.

```
StateChange:
    key:   StringName
    value: Variant
```

A surface can have a transformative effect AND a state change on the same side. *(Principle 10.)*

State changes can write to **any** key in the game state dictionary, not just the surface's own `state_key`. This enables inter-surface mechanics: surface A's hit can modify surface B's behavior by writing to B's state key.

Note the asymmetry: surfaces read from their resolver's configured key(s) via `ConfigResolver`, but `StateChange` can write to any key. A surface can affect surfaces it has no read-relationship with.

State changes are applied sequentially along the trace. If multiple hits write to the same key, the last writer wins. Deterministic because hit ordering is deterministic (§11.3).

#### Ghost mode (Stage 86)

`ghost` is a reserved flag that lasts **one shot**: every trace starts with it false, and the game never keeps it.

- **Togglers:** a ghost toggler is a side whose state change toggles `ghost`. Its effect is a pass, it is plannable, and the level export is `ghost_toggler_lines`.
- **While ghosted, everything else passes.** Every other side resolves to a pass, together with its state change: mirrors, inversions, portals, projectives, terminals and room walls. A ghost arrow therefore crosses everything, can leave through a wall, and wraps through infinity, since a straight beam is a projective line.
- **Materializing:** the arrow materializes at the next toggler hit, or at the same toggler one loop later. A loop around the beam that changed the state is walked again; a loop that changed nothing ends the trace as usual. MAX_HITS bounds both.
- **Drawing:** ghost steps (`TraceStep.ghost`) are drawn paler and translucent, and ghost togglers have their own colour.
- **Planning inside a ghost stretch:** the plan's state is simulated forward from a solid start (§13.7). While it is ghosted, after an odd number of planned ghost togglers, only a ghost toggler can be added: every other side is refused at the click and gets no hover highlight. A click within reach of both a refused side and a toggler takes the toggler. A programmatic plan that still names such a side treats it as a pass (`BeamTracer.plan_refuses`).

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

**Frame identity:** a step's `frame_id` is the id of `(epoch, stack signature)`. `frame_id = 0` (`TraceStep.IDENTITY_FRAME`) is the empty stack in epoch 0. `BeamTracer` assigns ids from one dictionary shared by the planned and the physical path of a pair, so equal ids mean the same applied effects since the last projective. A projective starts a new epoch, so frames after it get new ids even when the stack signature repeats.

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
3. **Endpoint blockage:** at an interior on-segment point both sides are blocked. At an exact endpoint, a side is blocked according to the sign of `τ_h × (tangent into the segment)`. Every other segment sharing that exact endpoint (a junction) adds its blocked sides. Blockage is accumulated **per hit point only**; it never leaks into later hits.

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
    point:       CN            # null for escape, infinity, and a max at infinity
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
| Beam departing a surface | After applying at `h` on `S`, the new beam departs `S` at `h`. The departure root is excluded as an ordinary candidate and re-hit last, as an ordinary hit. No `skip_segment` and no epsilon. |
| Exact line exactly tangent to a circle carrier | Touches without crossing: no event. |
| Concurrent line carriers | On an exact line, adjacent crossings of two line carriers that meet the line at one point (exact 3×3 determinant) share one parameter. The ID rule orders them. |
| Inversion of a beam through the inversion center | Any beam through a known point that is exactly the center becomes an exactly straight beam. |
| Player or cursor exactly at an inversion center (point at ∞ in the anchored construction) | Forward walk from the exact aim direction. If the aim image itself is at ∞, the walk goes straight at the cursor. |
| Player = cursor | No aim: no trace (`BeamTracer.trace_pair` returns null). |
| Zero-length segment | Rejected at load (§23). |

### 11.5 Unbounded carrier intersection

All roots on the unbounded carrier (full line or full circle) are events, whether on or off the segment. `on_segment` only matters for whether PHYSICAL can fire (§14.1). PLANNED mode fires the next plan entry on an ID and side match **even off-segment**: the planned path uses the carrier extension.

### 11.6 See-through parameter

*(Not yet implemented — specified for the visibility system §15.)*

A `see_through: Set[Surface]` option will make the listed surfaces transparent, so the beam passes them without recording a hit. It is used by the visibility system (§15) for truncating boundaries. It is distinct from the departure rule (§11.4).

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
        cands = roots of beam with EVERY surface carrier (§11.1)
              + cursor, if the plan is complete and matched
              + the departure point (re-hit last)
        snap candidates to known points (exact incidence, §11.1)
        sort projectively from t0 (§11.3)
        for c in cands:
            count += 1
            if count > MAX_HITS: emit "max" at c (drawn up to it, no effect); stop
            if c is the cursor:
                emit "cursor"; mode = PHYSICAL      # post-cursor continuation
                continue
            classify c → point, on_segment, side, fully_blocked
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
        # no effect fired on any candidate
        emit "escape" (straight beam) or "origin" (circle beam, back at its start); stop
```

**Anchored planned path.** In PLANNED mode, and in PHYSICAL mode until its first divergence from the planned decisions, firing plan entry `j` does not map the current beam. Instead the walker swaps in the **anchored beam** `B_{j+1}` of the plan (§13.1). Each anchored beam is built independently from exact data, so errors do not accumulate along the plan. After the divergence (or after the cursor) the trace is a forward continuation from the current beam. That part is the numerically sensitive one: it is seeded for `ErrorEstimate` and solved on the `PrecisionLadder`.

**Key design decisions:**
- **One walker for all tiers and both modes.** PLANNED and PHYSICAL differ only in which effects fire (§14.1).
- **Every carrier crossing is an event** (on- or off-segment, pass-through or not). This lets the cursor compete with carrier hits by parameter and keeps both modes' events in correspondence until divergence.
- **Blockage at endpoints** is accumulated per hit point, across all segments sharing the exact endpoint (Y- and T-junctions). PHYSICAL fires only on a fully blocked hit.
- **Provenance stack:** re-applying the surface on top of the stack (or hitting a portal's pair) pops it and restores the earlier beam object exactly. This prevents infinite loops with self-inverse effects and makes reversal exact by construction (Principle 22).
- **No surface filtering:** all surfaces always participate in hit detection. The solver never filters surfaces by position, proximity, or any other spatial criterion.
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

- **Straight beam:** passing `t = ∞` means the arrow travels to the viewport edge, leaves, and returns from the opposite edge (two escape steps, §12.3). The passage is an `infinity` event (Stage 88): it counts toward the hit limit; a trace cut by the limit there ends with a `max` event at infinity (drawn up to the edge). The event is **applied** when the next plan entry is ∞ (the entry is consumed, in both modes); otherwise it passes. If no effect fires on the whole loop, the trace still ends with an `escape` event.
- **Circle beam** (e.g. after an inversion): `t = ∞` is the antipode of `p`, a finite point that the arrow simply passes. If no effect fires on the whole circle, the trace ends back at the beam's start with an `origin` event.

### 12.6 Effect limit

Maximum **32 events** per trace (`BeamSolver.MAX_HITS = 32`). **Every event counts:**
- every carrier crossing (pass-throughs and off-segment crossings included);
- the cursor;
- the final `origin`/`escape` event.

A trace cut by the limit ends with a `max` event at the next crossing, drawn up to it with no effect. This prevents infinite loops from any source, including overlapping pass-through surfaces.

When the limit is reached, the preview shows a truncation marker (small stop icon) at the final hit point. During arrow flight, the arrow stops with a distinct visual cue (flash/fade).

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
- **Two realizations** (Stage 88): path order is preserved by every effect, so the orientation of the whole first epoch is fixed by `B_0`'s aim: toward `X_0` (`P → X → Q`, the direct realization), or away from it, reaching `X_0` through ∞ (`P → Q → X`). An ∞ plan entry in the first epoch (before its first projective) selects the second: every `B_j` of that epoch is built `through(P, Q, X)` instead. In a later epoch the projective's exit already fixes the orientation (§13.3), so an ∞ entry there is only matched by the walker. The ∞ entry must sit where the wrap happens; the visual wrap occurs on leg `j` only when `B_j` is straight, so an ∞ entry right after an inversion is unrealizable and the plan shows as invalid (§13.5). *Oracle:* `trace2.via_infinity`, fixtures `bench/fixtures/stage88_infinity.json`.

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
> - **Forks:** two candidates are a fork, settled at planning time. The one nearer the point the player clicked is taken (§4.2), and without a click the segment's midpoint stands in. There is no search: one deterministic path.
> - When the target itself lies on the carrier (provenance: a later projective entry on the same surface, or the cursor exactly on it), the candidate at the target is excluded.
> - An entry with no candidate is skipped by the aim.
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

The hit point returned by `back_propagate()` must lie within the surface's segment bounds (checked using the same arc containment test as §11.1). If the carrier-level intersection exists but falls outside the segment, `back_propagate()` returns null.

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

The planned trace is the same rule walker as the physical trace (§12.1), with `mode = PLANNED` up to the cursor. After the cursor it is a PHYSICAL continuation. Both are converted to `TraceStep`s by the same `BeamTracer.to_path`. Every carrier crossing is an event in both modes, so the two event lists correspond 1:1, with the same points, **up to the first step where they apply different effects**. The step tree merge (§14.5) compares frame ids at each index.

When the two traces make identical decisions through the cursor, `BeamTracer` shares one trajectory: the planned path *is* the physical path, with the same steps.

---

## 14. Step system

### 14.1 Two trace modes — unified hitpoints

Hit-point computation uses a **unified hitpoint model**: both modes intersect the current beam with ALL surface carriers (on- or off-segment), and every crossing is an event (§11.5). The modes differ ONLY in which effects are applied at each hitpoint (`TraceMode` / `BeamSolver.Mode`):

| Mode | Effect application rule | When it runs |
|------|------------------------|-------------|
| **PHYSICAL** | An effect fires ONLY on a **fully blocked** hit: an on-segment interior point, or an exact endpoint whose blocked sides (including other segments sharing that endpoint) cover both sides. A terminal ends the trace. Everything else is a pass-through (no effect, no stack change). Plan bookkeeping is by surface ID only: an applied transformative or projective effect advances the plan if it is the next entry's surface, and otherwise marks the plan unmatched. | Origin to terminal/escape/limit. |
| **PLANNED** | The next plan entry fires when surface ID **and** side match, even off-segment. Only transformative entries change the beam. Projective entries are applied too (Stage 87). Terminal entries are consumed without effect. Everything else passes, terminals included. After the cursor, the trace continues with PHYSICAL rules. | Origin to terminal/escape/limit. |

Neither mode stops at the cursor. The cursor is a **virtual hitpoint** (§12.1) that competes with carrier intersections in projective order. It is a candidate only once the plan is complete and matched. `cursor_index` is the step index where the cursor falls.

Up to the first index where the two modes apply different effects, both traces walk the same beam (the anchored planned path, §13.1) and produce the same events. Their step arrays therefore correspond **1:1 by index** with identical `start`/`end` there. After that index they continue independently.

Both modes use the same solver, the same scene defs and the same scene state. `PrecisionLadder` lets planned share physical's precision climb when they agree through the cursor, and `BeamTracer` caches the float64 scene across frames and the last pair (§14.8).

### 14.2 Divergence: exact definition

Divergence is defined by comparing the PLANNED and PHYSICAL traces step by step. Before divergence both modes produce the **same hitpoints** (§14.1), so steps at the same index have the same `start` and `end`. The field compared is the **frame id**: the id of `(epoch, provenance stack)`, §10.7.

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

> **Status: PLANNED** — The visibility system is not yet implemented. The design below is the intended specification for future development. It was written against the deleted frame-based solver, so its pseudocode still uses that vocabulary: `current_frame`, `MobiusTransform`, `cache.apply`, `get_mobius`, `find_all_hits` in a transformed frame. Those must be re-expressed on the beam solver: casts are beams in visual space, effects are beam/point maps (§10.8), and the provenance stack replaces frames (§10.7). The game-design requirements stay as written: same scene, same state, same rules as trajectory, and Principle 14.

### 15.1 Purpose

The visibility system highlights the **set of cursor positions that will not cause divergence** for the current plan. It answers: "if I place my cursor here, will the planned path and the physical path agree?" The highlighted region is exactly the set of valid aim positions. *(Principle 14: visibility shares the world.)*

### 15.2 Computation

The visibility algorithm reuses the **same ray casting code** as the physical trace (§12.1). Instead of casting toward one cursor, it casts toward every **point of interest** — surface endpoints, tangent points, and other threshold positions — where the casting result changes. This aligns the visibility output with its purpose: showing where the plan does not diverge from the physical path.

The algorithm is a **loop** — one iteration for the initial view (no plan surface yet), then one iteration per planned surface. Every iteration runs the same core casting step.

```
func compute_visibility(origin, plan, scene, game_state) → VisibilityResult:
    current_origin = origin
    current_frame = MobiusTransform.IDENTITY
    truncating_segments = []  # empty = full 360° cone (first iteration)
    regions = null
    
    # Iteration 0: initial visibility (full 360°).
    # Iterations 1..N: one per planned surface.
    for step_index in range(len(plan) + 1):
        
        # --- Core casting step (identical every iteration) ---
        
        # Transform all surfaces into the current frame.
        frame_surfaces = transform_all(scene.surfaces, current_frame.inverse())
        
        # Collect points of interest from ALL surfaces in the current frame.
        points_of_interest = []
        for surface in frame_surfaces:
            seg = surface.segment
            points_of_interest.append(seg.start)
            points_of_interest.append(seg.end)
            carrier = derive_carrier(seg)
            if not carrier.is_line():
                for tp in compute_tangent_points(current_origin, carrier):
                    if arc_contains(seg, tp):
                        points_of_interest.append(tp)
        
        # If truncating, add truncating segment endpoints and filter
        # to only directions within the truncated cone.
        if truncating_segments:
            for ts in truncating_segments:
                points_of_interest.append(ts.start)
                points_of_interest.append(ts.end)
            points_of_interest = filter_to_cone(
                points_of_interest, current_origin, truncating_segments)
        
        # Cast rays toward each point of interest.
        # Uses the SAME find_all_hits as physical tracing (§12.1).
        cast_results = []
        for poi in points_of_interest:
            ray = Ray(current_origin, Direction(current_origin, poi))
            hit = find_all_hits(ray, frame_surfaces,
                                     see_through=truncating_segments)
            obstruction = determine_obstruction_side(hit, poi)
            cast_results.append(CastResult(poi, hit, obstruction))
        
        # Sort by radial ordering (§15.5).
        cast_results.sort(key=radial_order_key(current_origin))
        
        # Build visibility regions from sorted cast results.
        regions = build_regions_from_casts(cast_results, current_origin)
        
        # --- Propagation to next plan step ---
        
        if step_index >= len(plan):
            break
        
        entry = plan[step_index]
        lit_segments = intersect_regions_with_surface(regions, entry.surface)
        if not lit_segments:
            continue  # planned surface not reachable
        
        side_config = entry.surface.active_side_config(entry.side, game_state)
        
        if side_config.effect != null and side_config.effect.is_transformative():
            M = side_config.effect.get_mobius()
            current_frame = current_frame.compose(M)
            current_origin = cache.apply(current_origin, M.inverse())
            truncating_segments = lit_segments
        
        elif side_config.effect != null and side_config.effect.is_projective():
            current_frame = MobiusTransform.IDENTITY
            if side_config.effect is CircleNormalProjection:
                # Point source shifts to the circle center.
                current_origin = derive_carrier(entry.surface.segment).center()
                truncating_segments = lit_segments
            else:
                # Line normal or semi-circle directional: parallel ray source.
                current_origin = null  # signals parallel-source mode
                current_direction = side_config.effect.get_outgoing_direction()
                truncating_segments = lit_segments
        
        elif side_config.effect != null and side_config.effect.is_terminal():
            regions = []
            break
    
    return VisibilityResult(regions)
```

The core casting step is identical in every iteration — only `current_origin`, `current_frame`, and `truncating_segments` change. Iteration 0 (empty truncating_segments = full 360°) is the first pass of the same loop. This ensures initial visibility and all propagation steps use the **same code path**.

**Tangent point computation:** two tangent points from external point P to circle (C, r) exist when |P - C| > r. The tangent points T satisfy |T - C| = r and cross(T - P, T - C) = 0. Tangent points are filtered to the segment's arc bounds using the cross-product containment test (§11.1).

**Obstruction side determination:** each cast result records whether the hit creates a CW, CCW, or both-sided obstruction. This is the same CW/CCW logic described in §15.5.

**Parallel-source mode** (after line-normal or semi-circle-directional projection):

```
func cast_parallel_source(direction, lit_segments, scene, game_state):
    # Cast parallel rays from the lit sub-segment in the fixed direction.
    # Each ray originates from a different point on the lit segment.
    
    # Collect relevant points of interest: surface endpoints/tangents
    # that are ahead of the lit sub-segment in the outgoing direction.
    pois = []
    for surface in scene.surfaces:
        for p in [surface.segment.start, surface.segment.end]:
            for lit_seg in lit_segments:
                proj = project_point_onto_segment_along_direction(p, lit_seg, direction)
                if proj is not null:
                    pois.append({source: proj, target: p})
    
    # Sort by position along the lit sub-segment (linear ordering).
    pois.sort(key=lambda p: parameter_along_segment(p.source, lit_segments))
    
    # Cast one ray per POI from its source point in the fixed direction.
    cast_results = []
    for poi in pois:
        ray = Ray(poi.source, Direction(poi.source, poi.source + direction))
        hit = find_all_hits(ray, scene.surfaces, see_through=lit_segments)
        obstruction = determine_obstruction_linear(hit, poi)
        cast_results.append(CastResult(poi.target, hit, obstruction))
    
    # Build regions from linearly-ordered results (same logic, linear not radial).
    return build_regions_from_casts(cast_results, null)
```

`determine_obstruction_linear` is the linear analogue of `determine_obstruction_side` — instead of CW/CCW, it determines whether the hit obstructs "above" or "below" the parallel ray (relative to the segment's traversal direction). The region-building logic is identical.

### 15.3 Visibility helper function specifications

**`build_regions_from_casts(cast_results, origin):`**

```
func build_regions_from_casts(cast_results, origin):
    regions = []
    current_boundary = []
    prev_nearest = null
    
    for cr in cast_results:
        nearest = cr.hit.surface if cr.hit else null
        
        # Emit visibility point at this angle.
        if nearest == null:
            current_boundary.append(cr.poi)     # unobstructed — extend to POI
        else:
            current_boundary.append(cr.hit.point)  # nearest surface intersection
        
        # Region break: if this hit fully obstructs (both CW and CCW).
        if cr.obstruction == BOTH:
            if current_boundary:
                regions.append(GeneralizedPolygon(current_boundary))
                current_boundary = []
        
        prev_nearest = nearest
    
    if current_boundary:
        regions.append(GeneralizedPolygon(current_boundary))
    
    return regions
```

**`filter_to_cone(points, origin, truncating_segments):`** Keep only points whose direction from `origin` falls within the angular span defined by the truncating segments. Test: a point P is within the cone if, for at least one truncating segment, P is "between" the segment's endpoints when viewed from origin (using cross-product sign: `cross(seg.start - origin, P - origin)` and `cross(P - origin, seg.end - origin)` both ≥ 0, or both ≤ 0 depending on winding).

**`determine_obstruction_side(hit, poi):`** Examine the hit surface's segment orientation relative to the ray from origin through poi. If the segment extends clockwise from the poi (cross-product of ray direction × segment direction > 0): obstructs CW. If counterclockwise: obstructs CCW. If poi is an interior hit (not an endpoint): obstructs both.

**`intersect_regions_with_surface(regions, surface):`** For each region, find which portion of the surface lies inside the region. Cast rays from origin through the surface's start, end, and via points. For each ray, if the earliest hit IS the surface (not a closer obstruction), that point is illuminated. The illuminated sub-segment is the continuous portion of the surface between illuminated points.

**`see_through` parameter in `find_all_hits`:** *(Not yet implemented — specified for the visibility system §15.)* Surfaces in the `see_through` set will be treated as transparent — rays pass through them without recording a hit. This is distinct from the departure rule (the departure point is re-hit last, §11.4). Both can be active simultaneously.

### 15.4 Multiple regions

The visibility result is a **set of zero or more polygons**. Obstructions, surface geometry, and plan effects can split the light cone into disconnected regions at any step.

```
VisibilityResult:
    regions: Array[GeneralizedPolygon]
```

### 15.5 Radial ordering

At each step of the light-cone computation, event points (surface endpoints and tangent points) are sorted radially. The ordering hierarchy:

1. **Obstruction side provenance (CW/CCW)** — primary key. When a ray reaches a point of interest, it gains an obstruction on the CW side, CCW side, or both:
   - **Hit point** (mid-segment): obstructs both sides.
   - **Segment endpoint**: obstructs one side based on the surface's orientation (left-extending → CCW, right-extending → CW). Corners where surfaces meet may obstruct both.
2. **Cross-product sign** against a fixed reference direction — secondary key for angular position.
3. **Provenance ID / surface ID** — final tie-break.

This radial ordering is the **single source of truth** for all angular sorting in the game — visibility, shadow boundaries, and any other radial-ordering logic.

### 15.6 Consistency

Visibility uses the **same intersection pipeline**, **same effects**, and **same scene state** as trajectory. *(Principle 14.)* That means the same beam-solver geometry (`BeamGeom`, §11), the same effect maps (§10.8), and the same game state. If a surface is broken in the game state, it is broken for visibility too.

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

A level is a serialized resource (`.tres` in Godot) containing:

```
LevelData:
    name:            String
    surfaces:        Array[SurfaceData]
    spawn_point:     Vector2
    gravity:         Vector2                 # default (0, 0) — no gravity
    initial_flags:   Dictionary[StringName, Variant]  # initial game state (e.g. {"mirror_3_intact": true})
    surface_names:   Dictionary[int, String]  # optional display names by surface ID, default "Surface N"
    bounds:          Rect2                    # level bounds for camera, visibility, tests. Defaults to bounding box of all surfaces + **50 units** margin on each side. For arc segments, the bounding box is the axis-aligned bounding box of the arc path (computed from center, radius, and angular span), not just the three defining points.
    cache_overrides: Array[CycleOverride]    # manual cycle identities
    cached_carriers: Dictionary[int, Array]  # key = surface ID, value = [a, b, c, d] carrier coefficients
    collision_bodies: Array[CollisionBodyData]  # optional, default empty. Player-only collision geometry (§25.1).

CollisionBodyData:
    shape:    String      # "segment" or "rectangle"
    start:    Vector2     # for "segment": first endpoint. For "rectangle": top-left corner.
    end:      Vector2     # for "segment": second endpoint. For "rectangle": bottom-right corner.

CycleOverride:
    surface_ids:     Array[int]          # surfaces in cycle order
    sides:           Array[Side]         # which side at each surface
    expected_result: Variant             # the declared composition result (typically identity); representation TBD — no Möbius matrix class exists since Stage 84 (§17.4)
    test_positions:  Array[Vector2]          # custom off-grid test positions for invariant testing
```

Each `SurfaceData` includes the segment geometry (start, end, via), per-side effect configuration, resolver, is_target flag, and player_solid flag.

**Level discovery:** available levels are discovered by scanning `resources/levels/` for `.tres` files. Level ordering in the level select screen is alphabetical by filename unless a manifest file (`resources/levels/manifest.cfg`) specifies a custom order.

### 19.2 Editor requirements

> **Status: PLANNED** — The level editor is not yet implemented. Levels are currently created programmatically in code.

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

Surface IDs are assigned by a monotonically incrementing counter persisted in the level file. Deleted IDs are never reused.

### 19.3 Serialization

Levels serialize to Godot `.tres` resources. The primary data for each surface is its three points (start, end, via) plus per-side effect configs, state key, and is_target flag. The **full computation cache** — including derived carrier coefficients and any other cached values — is serialized alongside the primary data. This avoids recalculation on deserialization and ensures exact values are preserved across save/load.

Principle 21 (derive, don't duplicate) governs **runtime data flow**: the carrier is derived on demand from three points and cached. Serialization is a **persistence optimization** — it stores the cache so that loading is exact and fast. The three points remain the source of truth; the serialized cache is validated against them on load.

If the stored carrier type disagrees with the geometric configuration of the three points on load, the geometric configuration wins and the cache is recomputed. A warning is logged.

The format must be human-readable for debugging.

---

# Part VI: Implementation

## 20. Godot architecture

### 20.1 Layer separation

| Layer | Responsibility | Godot types used |
|-------|---------------|------------------|
| **Math** | Geometry primitives, effect descriptions, the precision beam solver (intersection, effect maps, anchored planning, exact predicates, dynamic precision), step types and merge, (future) visibility | **None.** Pure GDScript with `Vector2`, `float` and packed arrays. |
| **Visual** | Math→visual conversion, drawing, input | `Node2D`, `_draw()`, `draw_line()`, `draw_arc()`, `Color`. |
| **Game** | Scene management, state, shot lifecycle, player | `CharacterBody2D`, autoload singletons, scene tree. |

The math layer has **zero Godot engine dependencies** beyond `Vector2`. Testable in isolation.

**GDScript performance note:** GDScript is significantly slower than C++ for tight numerical loops. For the intersection kernel (32 events × N surfaces per frame), avoid object allocation in hot loops. This is why `FastSolver` exists: a raw-float64 `PackedFloat64Array` backend, roughly 50× faster than the precision-generic code and bit-identical to it at float64. If profiling shows the intersection kernel is the bottleneck, consider moving it to a GDExtension (C++) module. The math layer's zero-dependency design makes this migration straightforward.

**Resource mapping:** `LevelData`, `SurfaceData`, and effect configurations extend Godot `Resource`. Each has `@export` properties for serialization to `.tres`. Surfaces in the scene tree are `Node2D` instances that reference their `SurfaceData` resource.

### 20.2 Project structure

Current layout (Stage 84). Files marked *(planned)* do not exist yet.

```
ricochet-game-v2/
├── project.godot
├── GAME_SPEC.md, CHANGELOG.md
├── scripts/
│   ├── math/
│   │   ├── point.gd, direction.gd, ray.gd, segment.gd, generalized_circle.gd, side.gd
│   │   ├── trace_step.gd, trace_hit.gd, trace_path.gd, trace_mode.gd   # neutral trace types
│   │   ├── step_types.gd, step_tree_merge.gd
│   │   ├── chevron_overlay.gd, chevron_overlay_builder.gd
│   │   ├── effects/
│   │   │   ├── effect.gd, transformative_effect.gd, terminal.gd
│   │   │   ├── reflection_effect.gd, circle_inversion.gd, rigid_motion_effect.gd
│   │   │   └── line_normal_projection.gd, circle_normal_projection.gd,
│   │   │       line_directional_projection.gd, circle_directional_projection.gd
│   │   ├── precision/                     # the beam solver (§6, §11–13, §31)
│   │   │   ├── beam_solver.gd             # BeamSolver: rule walker, anchored chain, provenance stack
│   │   │   ├── fast_solver.gd             # FastSolver: raw float64 twin (bit-identical) + tangent-linear
│   │   │   ├── precision_ladder.gd        # dynamic precision driver
│   │   │   ├── error_estimate.gd          # a-priori error envelope, decision reliability
│   │   │   ├── beam_tracer.gd             # adapter: events → TracePath / TraceStep
│   │   │   ├── beam_scene.gd, beam_effects.gd, beam_geom.gd, hp_beam.gd, exact.gd
│   │   │   └── num_ctx.gd, f64_num.gd, dual_num.gd, big_float.gd, cn.gd
│   │   └── visibility.gd                  # (planned)
│   ├── visual/
│   │   ├── visual_converter.gd
│   │   ├── path_renderer.gd
│   │   └── visibility_renderer.gd         # (planned)
│   ├── game/
│   │   ├── game_manager.gd, player.gd, cursor.gd, arrow_animator.gd
│   │   ├── surface.gd, surface_node.gd, side_config.gd, side_link.gd, room_builder.gd
│   │   ├── plan_manager.gd, plan_hud.gd, click_detector.gd
│   │   ├── game_state.gd, checkpoint.gd, checkpoint_stack.gd, level_settings.gd
│   │   └── debug_dumper.gd
│   └── testing/                           # invariant checker, sweeps, TestTrace, precision bench
├── tests/                                 # GUT tests (test_stage{N}_*.gd and others)
├── docs/                                  # TDDs; docs/research/oracle/ (mpmath oracle, trace2.py = trace spec)
├── bench/                                 # oracle fixtures and scene corpora
├── scenes/
│   ├── main.tscn, player.tscn, violation_viewer.tscn, …
│   ├── test_levels/, playground/
│   ├── ui/, editor/                       # (empty; planned)
└── resources/
    └── levels/                            # (empty; planned)
```

*Removed in Stage 84:* `scripts/math/{tracer, planner, transform_cache, tracked_transform, mobius_transform, intersection}.gd`.

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

**Z-order (back to front):** surfaces → visibility regions (semi-transparent) → step tree preview → arrow (during flight) → player. Within surfaces, order is by surface ID (lower ID draws first).

Explicit z_index values: surfaces=0, visibility=10, step tree preview=20, arrow=30, player=40.

Surfaces are drawn in visual coordinates using their defining geometry (start, via, end → derive carrier → draw_line or draw_arc). The beam solver also works on surfaces in visual coordinates (§6.1). No transform is needed anywhere.

Within the step tree preview, draw order (back to front): DIVERGED_PLANNED → DIVERGED_POST_PLANNED → ALIGNED → ALIGNED_POST_PLANNED → DIVERGED_PHYSICAL. The physical path (green/yellow) always draws on top of the planned path (red).

---

## 21. Arrow flight and animation

### 21.1 Time freeze

When the player fires, **game time freezes**. All physics, surface movement, and timers stop. The shot is logically instant — the full physical trace is computed before animation begins.

**Godot mechanism:** use `get_tree().paused = true`. The arrow animator node has `process_mode = PROCESS_MODE_ALWAYS` so it continues running during pause. All other game nodes use the default process mode and freeze automatically. During arrow flight, the preview is **hidden** — only the arrow animation is shown. After the shot completes, the tree is unpaused and the preview reappears for the retained plan.

**Animation skip:** the player can skip the flight animation by pressing any non-movement key during flight. Skip instantly completes the animation: all remaining state changes are applied, all visual state changes are applied, and the camera jumps back to the player. Movement keys (WASD) pressed during flight are ignored — they do not queue movement or trigger skip. Fire during animation only skips — it does not queue a new shot. A new shot requires a separate Fire press after the animation completes and game time unfreezes.

### 21.2 Animation

The arrow is animated step-by-step along the computed physical `TracePath` (from `BeamTracer.trace_pair`):

1. For each step, the arrow tip moves from `start` to `end`:
   - Line steps (visual): linear interpolation.
   - Arc steps (visual): parametric interpolation along the arc. The parameter `t ∈ [0, 1]` interpolates from start to end via the via point. The arrow position at each t is computed by slerp (spherical-linear interpolation) around the center, with winding direction determined by the via point (cross-product sign of `(start - center) × (via - center)`).
2. At each hit point: brief bounce event (visual flash, sound cue). If the hit triggers a state change (e.g., surface breaks), the surface visually updates at this moment during the animation.
3. Speed: constant visual speed along the path (not constant parametric speed — arcs travel at the same visual speed as lines). Default arrow speed: **1600 units/sec**. Configurable per level.

### 21.3 Ray escape during flight

When the ray escapes (no hit on the full great circle), the arrow animates flying to the viewport edge and disappears. The shot still counts — checkpoint is saved, game time unfreezes, state changes from prior hits take effect. Win condition is evaluated normally.

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
| ALIGNED | Green | Solid |
| ALIGNED_POST_PLANNED | Green | Dashed |
| DIVERGED_PHYSICAL | Yellow | Dashed |
| DIVERGED_PLANNED | Red | Solid |
| DIVERGED_POST_PLANNED | Red | Dashed |

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
| Max events per trace | **32** (`BeamSolver.MAX_HITS`) | Every event counts (crossings incl. pass-through and off-segment, cursor, final origin/escape). Prevents infinite loops. |
| Inversion circle = carrier | Always | No separate inversion-circle parameter. |
| Min segment length | > 0 | Zero-length segments never hit. Validated at load. |
| Effect invertibility | Always | Every transformative effect is invertible by definition (§10.8); no matrices are composed since Stage 84, so the former "frame determinant ≠ 0" check no longer applies. Portal rotation `a = unit(cos θ, sin θ)`. |
| Side labels | LEFT / RIGHT only | Uniform for all segment types. |
| Direction representation | Two points | No stored angles or unit vectors. |
| Ordering method | Cross-product sign | No `atan2`. |
| Epsilon decisions | Forbidden | No `\|x\| < ε` for topology-changing decisions. |
| Surface filtering | Forbidden | All surfaces always participate in hit detection. No spatial filtering, proximity culling, or pre-selection. |
| UI click tolerance | 8 pixels (§4.2) | UI-layer tolerance — does NOT violate the no-epsilon constraint, which applies only to topology-changing geometric decisions in the math layer. |
| Reflection carrier | Must be a line (`a = 0`) | Validated at level load and editor save. |
| Inversion carrier | Must be a circle (`a ≠ 0`) | Validated at level load and editor save. |

---

## 24. Target mechanics

### 24.1 Default: surface targets

The default target type is a **surface** the arrow must contact during the physical trace. A surface with `is_target = true` is a goal surface.

### 24.2 Hit detection

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

---

## 25. Player character

### 25.1 Physics

The player is a `CharacterBody2D` that collides with two kinds of geometry:

1. **Collision bodies** — plain `StaticBody2D` nodes with `CollisionShape2D` children (e.g., `SegmentShape2D` for floor lines, `RectangleShape2D` for platforms). These exist purely for player physics. They are invisible to arrow tracing, visibility, and planning systems. They are standard Godot collision objects with no game-mechanic behavior.

2. **Surfaces with `player_solid = true`** (the default) — Surfaces (§9) generate collision shapes from their segment geometry. These participate in both player physics AND the arrow/visibility/planning systems. Surfaces with `player_solid = false` do not impede player movement but still interact with the arrow. `player_solid` is per-surface (not per-side). Godot's physics handles collision via `move_and_slide()` on CharacterBody2D — it doesn't distinguish surface sides.

Collision bodies are appropriate for level geometry that only constrains the player (floors, platforms, ceilings in gravity levels). Surfaces are appropriate for geometry that the arrow must also interact with (walls, mirrors, portals). A level may use both. Movement uses standard Godot physics with gravity from `LevelData.gravity` (default `(0, 0)` — no gravity).

Line surfaces generate `SegmentShape2D` collision shapes. Arc surfaces use `ConcavePolygonShape2D` with line segments approximating the arc (16 segments per full circle, scaled by arc span). Minimum **3 segments** per arc, regardless of arc span. `ConcavePolygonShape2D` handles both minor and major arcs. Since surfaces are static, the performance impact is acceptable. Godot has no native arc collision shape. Surfaces are zero-width geometry — the player slides along them.

**Arrow origin:** the arrow originates from the **center of the player's collision shape** (no offset).

Player collision shape: `CircleShape2D` with radius **12 units**.

**Arrow–player interaction:** the arrow does **not** collide with the player. The player is not a surface for ray tracing purposes. The player can stand on or near a surface without blocking their own shot.

### 25.2 Movement

- Move: WASD / left stick.
- Default speed: **200 units/sec**. Horizontal input response is instant (no acceleration ramp). Gravity applies vertically as configured in `LevelData.gravity` — the player can fall and needs platforms when gravity is non-zero. Configurable per level.
- Default jump velocity: **400 units/sec upward**. Single jump (no double jump). No coyote time. Parameters are tunable per level.
- **The arrow is never affected by gravity** — it follows pure geometric ray tracing regardless of the level's gravity setting.
- The player can move while aiming. *(Principle 16.)*

### 25.3 Spawn, checkpoints, and reset

- Player spawns at `scene.spawn_point`.
- **Undo last shot** (Z): reverts to the checkpoint before the most recent shot — player position, game state, and plan are restored. Checkpoint stack allows stepping back through multiple shots.
- **Clear plan** (C): clears the plan without affecting player position or game state.
- **Full reset** (R): reverts to the level's initial state — player returns to spawn, all surfaces restored, all state reset, checkpoint stack cleared.

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

Save data uses Godot `ConfigFile` format (`.cfg`). Human-readable, simple key-value pairs.

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

### Phase 7: Visibility system *(Planned)*
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

**Testing framework:** use GUT (Godot Unit Test) as the test framework. Invariant sweeps run as GUT test cases that iterate over the position grid programmatically.

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

**UX1. Visibility predicts non-divergence.** *(PLANNED — depends on visibility system §15.)* If the cursor lies within a visibility region, the planned and physical paths agree up to the cursor (no divergence).

**UX2. Divergence implies outside visibility.** *(PLANNED — depends on visibility system §15.)* If the planned and physical paths diverge, the cursor is outside all visibility regions. (Contrapositive of UX1.)

**UX3. Physical preview matches arrow flight.** The non-red sections of the preview (ALIGNED + ALIGNED_POST_PLANNED + DIVERGED_PHYSICAL) exactly match the arrow's actual flight path when fired. Same hit points, same surfaces, same order. *(Principle 2.)* **Tested as:** `PHYSICAL_PREVIEW_MATCH`.

**UX4. Firing the same shot twice produces the same result.** Same position, cursor, plan, and game state → identical arrow path. *(Principle 17.)*

**UX5. Undo fully restores.** After undoing a shot, the game is indistinguishable from just before that shot — same player position, surfaces, plan, and target progress. *(§3.3.)*

**UX6. All targets reachable.** If the player has hit all targets (across one or more shots), the level is complete. No target is silently "unhit" despite being contacted. *(§24.2.)*

**UX7. Plan preview shows the plan.** The solid-colored path (ALIGNED + DIVERGED_PLANNED) forms a continuous path from the player toward the cursor. *(§4.3.)* **Tested as:** `UX7`, `GREEN_FROM_PLAYER`, `SOLID_PATH_TO_CURSOR`.

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

**S13. Visibility: no self-intersection.** *(PLANNED — depends on visibility system §15.)* Region boundaries do not self-intersect.

**S14. Visibility: edges on geometry.** *(PLANNED — depends on visibility system §15.)* Every polygon edge lies on a surface carrier or a ray from the origin.

**S15. Visibility: non-overlapping.** *(PLANNED — depends on visibility system §15.)* Distinct regions do not overlap.

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
| `DIRECTION_ONLY` | The physical trace depends only on the aim direction: the planned shot's physical hits equal those of a converged shot without plan aimed at the cursor's image (bounded by tangent-linear sensitivity to that image's float64 rounding; projective plans skipped until Stage 87) |
| `NO_POST_CURSOR_DIVERGENCE` | A path fully aligned up to the cursor does not diverge after it |
| `EFFECT_LAW` | Every applied effect maps its beam by its exact law (§10.8) |
| `EVENT_ON_BEAM` | Every event point lies on its beam, and every hit on its surface's carrier |
| `BEAM_ORDER` | Consecutive events on one beam chain their parameters in walk order |

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
- Visibility computation.
- Math→visual conversion.
- Draw calls.

### 30.2 Worst case

32 steps × N surfaces per step = O(32N) intersection tests per frame. For small N (< 100), this is within budget. For larger scenes, spatial indexing (grid or BVH) may be needed.

### 30.3 Optimization targets

- **Incremental recomputation**: if only the cursor moved (not the plan or player), recompute from the last unchanged step.
- **Recompute only on change** *(implemented, Stage 100)*: the preview, the hover and the plan overlays are recomputed only when their inputs changed (player, cursor, plan entries, state flags, surfaces); a frame in which nothing moved does no solver or draw work. Scene defs for the solver are built once per level.
- **Spatial indexing**: broad-phase rejection of distant surfaces.
- **Precision on demand** *(implemented)*: the float64 `FastSolver` pass is accepted when its `ErrorEstimate` is ≤ 0.01 px and every decision margin exceeds its error. Only otherwise does `PrecisionLadder` climb BigFloat tiers (§31.5). The float64 scene and the last trace pair are cached (§17.2). *(Replaces the legacy "frame transform caching" of `M⁻¹`, which no longer exists.)*

---

## 31. Stability and numerical requirements

### 31.1 Direction representation

The aim is a two-point `Direction` (player → cursor), with no stored angle. It is immutable and shared by every step of a trajectory (§8.3). Inside the solver, a beam's direction is its tangent `τ`, a unit complex number at the tier's precision, carried exactly through the closed-form effect maps (§10.8). Exact straight beams are additionally kept as exact implicit lines (§31.4). Generalized circle coefficients are derived from points only when needed.

### 31.2 Ordering

- Cross-product sign against a fixed reference direction. Never `atan2`.
- Hits in projective order of the beam parameter `t` from the current parameter (§11.3). Never by distance from an unrelated point.
- Points known by provenance (known points) keep their exact parameter. Concurrent crossings on an exact line share one parameter (§11.4).
- Ties: cursor first, then surface ID; the departure re-hit is last. *(Deterministic, total order.)*

**Cross-products over angles:** all internal geometric decisions (arc containment, winding direction, radial ordering, sweep direction) use cross-product sign tests, never atan2. Angles appear only at the Godot API boundary (`draw_arc()` parameters) and are computed from points at the last possible moment during visual conversion (§7.3).

### 31.3 No epsilon decisions

Do not use `|x| < ε` for decisions that change topology (which hit wins, on-segment, vertex order). Epsilon logic varies with scale and platform. Prefer structural or provenance facts.

**Collinearity in particular:** whether a segment is a line or arc is determined by **construction provenance**, not by coordinate comparison. A segment is a line if its three defining points were placed collinearly by the level editor (stored as a construction fact in the serialized cache) or if the via is `Vector2(INF, INF)`. At runtime, no floating-point collinearity test is performed — the carrier type (line vs circle) is a stored decision, loaded from the serialized cache.

**How the beam solver avoids epsilons** (oracle `trace2` = solver; `docs/TDD_09_PRECISION_INTEGRATION.md`, "Exactness rules"):
- **Exact predicates** (`Exact`) decide incidence (a known point exactly on a carrier), endpoint identity, tangency of an exact line to a circle, and concurrency of line carriers. A float64 evaluation with a certified error bound handles the cheap case, and exact BigFloat arithmetic handles the rest. The bound only selects the path; it never decides a result.
- **Provenance** (known points, exact lines, the stack, §31.4) replaces coordinate comparisons. For example, a hit at a segment endpoint lying on an exact line *is* the endpoint.
- **Rounding never decides an endpoint, an incidence, a tangency, or a root at the antipode** (`t = ∞` is exact).
- The remaining tier-precision decisions (on-segment cross-ratio, side, root order) have their **margins** checked against the estimated error. `PrecisionLadder` raises the precision until the trajectory is reliable (§31.5).
- Zero-length steps are dropped by exact equality of endpoints (`start == end`) in `BeamTracer`.

*Historical:* the v0.7.0 table of epsilon replacements (`projective_sort`, `at_which_endpoint`, `endpoint_blocked_sides`, `maps_lines_to_arcs`, Tracer zero-length step) referred to the legacy solver, deleted in Stage 84.

Testing-layer tolerances (e.g., in `invariant_checker.gd`, and the 0.01 px trace tolerance) are unaffected. They measure rendered pixel positions, not topology-changing decisions.

#### 31.3.1 Three-tier endpoint detection

*Historical (legacy `Tracer`/`intersection.gd`, deleted in Stage 84):* endpoints were detected by (1) exact match with the ray's defining points, (2) exact cross product `== 0`, and (3) quadratic roots snapped to the endpoint.

**Beam solver equivalent:**
- **Known points:** every segment endpoint and via lying exactly on an exact line is a **known point** of the beam. The player, the cursor and exact departure points are known points too (§31.4).
- **Incidence:** a surface whose carrier contains a known point exactly takes that point's parameter and exact coordinates for its nearest root (§11.1). Endpoint identity is therefore decided by provenance or exact sign, never by distance.
- **Endpoint blockage:** at an exact endpoint, a side is blocked according to the sign of the beam tangent × the tangent into the segment. Blocked sides accumulate over all segments sharing that exact endpoint, **for that hit point only**. An effect fires in PHYSICAL mode only when both sides are blocked (§14.1).

### 31.4 Provenance

When the system classifies a point (on-segment, which side, intersection index), that classification is stored and reused. Recomputation from coordinates can contradict the original decision.

In the beam solver, provenance takes three forms (`beam_solver.gd` header, `trace2.py`):
- **Known points:** exact data points a beam passes through, each with its parameter. These are:
  - the player (initial beam, `t = 0`);
  - the cursor, whenever it is a candidate;
  - the departure point of a beam born at an exact hit point;
  - every known point of the previous beam lying on the carrier of the surface applied (an effect fixes its carrier pointwise);
  - every segment endpoint and via on an exact line.

  Pops restore the earlier beam's known points.
- **Exact lines:** a straight beam defined by exact data is kept in implicit form `a x + b y = c` with exact coefficients. Sources:
  - the initial beam of a trace without a plan, or of a mirror-only plan (exact rational aim image);
  - the reflection of an exact line in a line mirror (an exact map of the coefficients);
  - an exact line inverted in a circle whose center it contains;
  - an exact projective exit.
- **The provenance stack:** re-applying the surface on top of the stack (or hitting a portal's pair) pops it and restores the earlier beam *object*. A projective clears the stack and starts a new epoch. `frame_id` identifies `(epoch, stack)`.

### 31.5 Precision and stability (formerly: frame composition stability)

There is no frame composition. Numerical stability of long effect chains (inversion chains amplify errors exponentially) is handled as follows:
- **Anchored planned path:** each planned beam `B_j` is built independently from exact data, with exact Möbius difference formulas for `P − Q` (§13.1), so planned errors do not accumulate along the plan.
- **Dynamic precision** (`PrecisionLadder`):
  1. A float64 pass (`FastSolver`, with tangent-linear derivatives) yields the trace and its a-priori error envelope (`ErrorEstimate`: `est_i = C·eps·max_{j≤i} G_j`).
  2. The pass is accepted if the estimate is ≤ 0.01 px and every decision margin exceeds its error.
  3. Otherwise BigFloat tiers (from at least 112 bits, up to 1792) are climbed until two consecutive tiers agree within tolerance with identical event structure.
- **Provenance stack:** cancelling re-hits restore earlier beams exactly instead of recomputing through the chain.
- **32-event limit** bounds chain length.
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
| Beam departing a surface | Departure root excluded as an ordinary candidate; re-hit last (§11.4). |

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

**Current version: 2.11.0.** See [CHANGELOG.md](CHANGELOG.md) for revision history.
