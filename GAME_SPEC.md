# Ricochet Game v2 — Complete Game Specification

This is the single source of truth for the ricochet game: design, principles, architecture, data structures, algorithms, game loop, player interaction, level editing, implementation phasing, and testing strategy. It is self-contained — no other document is required to implement the full game.

Engine: **Godot 4.7+** with **GDScript**. Developed against Godot 4.7.x stable. IDE: **VSCode**.

> **At a glance:** A 2D puzzle game where the player plans ricochet shots through mirrors, portals, and lenses. The core loop: aim → plan a sequence of surface bounces → fire → watch the arrow follow (or diverge from) the plan. The engine uses Möbius geometry to unify lines and circles. The hardest implementation areas are: the planning algorithm (§13), the visibility system (§15), and the Möbius frame transform (§5–6). Start with Phase 0 (§28).

### Implementation Status

| Feature | Status |
|---------|--------|
| Math layer (generalized circles, segments, Möbius) | Done |
| Physical tracing — stage-based hitpoint walk (§12) | Done |
| Reflection effect | Done |
| Terminal (block) effect | Done |
| Plan construction and removal | Done |
| Planned trace with image chains | Done |
| Step tree merge and divergence | Done |
| Circle inversion effect — math, rendering, trace, planner | Done |
| Arrow flight with freeze/animation/skip | Done |
| Camera tracking during arrow flight (§4.5) | Done |
| Game systems: checkpoint, undo, reset (§3.3) | Done |
| Epsilon removal — three-tier provenance endpoint detection (§31.3) | Done |
| Effect.Kind enum + TransformativeEffect base class (v0.8.0) | Done |
| Boundless tracer — beyond-infinity handling (v1.0.0) | Done |
| Full segments / unbounded tracing (v1.0.0–v1.1.0) | Done |
| Source-based cancellation / transform stack (v1.1.0) | Done |
| Hermitian carrier transform (v1.2.0) | Done |
| No surface filtering principle (v1.3.0) | Done |
| Zero invariant violations (v1.4.0) | Done |
| Multi-bounce validation (v1.5.0) | Done |
| DRY refactor — Tracer, PathRenderer, Effects (v1.6.0) | Done |
| Invariant sweep testing (24 checks, 10 scenes) | Done |
| Rigid motion effect | Planned |
| Projective effects | Planned |
| Compound transformative effect | Planned |
| Visibility polygon (§15) | Planned |
| State-changing surfaces (CategoricalResolver) | Planned |
| Level editor | Planned |
| Menus, HUD, save system | Planned |

---

# Part I: Game Design

## 1. Game overview

The player is an archer in a 2D puzzle space. They **choose a sequence of reflective surfaces** to bounce off (the *plan*), **aim** at a target, and **shoot**. The arrow travels along straight lines and circular arcs, bouncing off surfaces according to their configured effects. The puzzle is figuring out *which* bounces reach the goal — not twitch aiming.

A **trajectory preview** updates in real time as the player aims. That preview must be trustworthy: if the preview says the shot works, the arrow must follow the same rules when fired. The preview and the physical arrow use the same geometry engine, the same effects, and the same game state.

**The player fantasy:** composing elaborate trick shots through a chain of mirrors, portals, and lenses. The satisfaction is the "aha" moment when the right sequence clicks, not dexterity.

> **Reading note:** this document uses forward references — some terms appear before their formal definition. §32 (Terminology) provides a complete glossary. Sections 5–17 are dense mathematical framework. Here's a quick mapping to player-visible effects:
> - **Möbius frame transform** → the engine's way of making the arrow curve through an inversion circle.
> - **Image chain** → how the preview computes where the arrow would go if the plan works.
> - **Normalized frame** → an internal coordinate system where all arrow paths are straight lines, simplifying the math.
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
- **Highlighting**: when the cursor hovers over a surface, the approachable side highlights to indicate it is clickable.
- **Plan display**: planned surface sides are visually marked (e.g., numbered overlay, distinct color on the planned side).
- **Preview**: the trajectory preview updates in real time as the plan and cursor change.
- **Right-click removal**: right-clicking over a planned surface removes the **latest instance** of that surface from the plan. Right-clicking over an unplanned surface or empty space **clears the entire plan**.
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

`GeneralizedCircle` is a **derived** representation, not a primary one. Segments are defined by three points (start, end, via — see §8.4). The carrier is computed from the three points on first access and cached bidirectionally, so the original via can be exactly recovered from the carrier (§17).

### 5.2 Möbius transformations

A Möbius transformation is a map of the extended complex plane:

```
z → (αz + β) / (γz + δ),    αδ − βγ ≠ 0
```

Represented as a 2×2 matrix of complex numbers. In GDScript: 8 floats (real and imaginary parts of α, β, γ, δ).

```
MobiusTransform:
    a_re, a_im: float    # α
    b_re, b_im: float    # β
    c_re, c_im: float    # γ
    d_re, d_im: float    # δ
    conjugating: bool     # true for anti-conformal maps (reflection, inversion)
```

**Conformal vs anti-conformal:** Rigid motion is **conformal** (`conjugating = false`): it maps `z → (αz + β) / (γz + δ)`. Reflection and circle inversion are **anti-conformal** (`conjugating = true`): they map `z → (αz̄ + β) / (γz̄ + δ)`, where `z̄` is the complex conjugate. The `conjugating` flag must be tracked through all operations.

| Operation | How |
|-----------|-----|
| **Apply to a point** | Let `z = x + iy`. If `conjugating`: use `z̄` (conjugate z). Compute `w = (α·z' + β) / (γ·z' + δ)` where `z' = z` if conformal, `z' = z̄` if conjugating. Result is `(Re(w), Im(w))`. |
| **Compose** | Depends on the conjugating flags of both operands (see composition table below). |
| **Invert** | `M⁻¹ = [[δ, -β], [-γ, α]] / (αδ - βγ)`. The `conjugating` flag is preserved (anti-conformal inverse is still anti-conformal). |
| **Transform a carrier** | Hermitian matrix form: `M⁻ᴴ · H · M⁻¹`, adjusted for conjugation. Implemented in `GeneralizedCircle.transformed_by(mobius)`. For anti-conformal (conjugating) transforms, the conjugate transpose is adjusted so the result stays Hermitian. |
| **Identity** | `[[1,0],[0,1]]`, `conjugating = false`. |

**Composition table:** When composing `M₁ ∘ M₂` (apply M₂ first, then M₁):

| M₁ | M₂ | Matrix operation | Result |
|----|-----|-----------------|--------|
| conformal | conformal | `M₁ × M₂` | conformal |
| conformal | anti-conformal | `M₁ × M₂` | anti-conformal |
| anti-conformal | conformal | `M₁ × conj(M₂)` | anti-conformal |
| anti-conformal | anti-conformal | `M₁ × conj(M₂)` | conformal |

Where `conj(M)` means conjugating each complex entry of the matrix: `[[ᾱ, β̄], [γ̄, δ̄]]`.

The rule is: when the outer transform is anti-conformal, it conjugates everything downstream — so the inner matrix's entries must be conjugated before multiplication.

The three transformative effects — reflection, circle inversion, and rigid motion — are all Möbius transformations (conformal or anti-conformal). Composition follows the table above, making chaining correct for any combination of effects.

**Code convention:** `frame.compose(T)` means `frame ∘ T` (mathematical convention: apply T first, then frame). This is the standard order for 'append a new transformation to the current frame.'

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

These four operations, plus `Vector2` addition and scalar multiplication, are sufficient to implement all `MobiusTransform` operations (apply, compose, invert).

---

## 6. The normalized coordinate system

### 6.1 Two frames

All mathematical calculations happen in the **normalized frame** — the standard Euclidean plane where rays are always lines. The **visual frame** is what the player sees.

| Frame | Rays | Surfaces | Visibility |
|-------|------|----------|------------|
| **Normalized** | Always **lines** | Generalized circles (lines or circles) | Simple polygons (straight edges) |
| **Visual** | Lines **or arcs** | Line segments or circular arcs | Generalized polygons (edges may be arcs) |

A **Möbius frame transform** `M` maps normalized → visual. It starts as the identity and is updated each time a transformative effect is applied.

Convention: M always maps normalized → visual. After composing with effect T: M' = M ∘ T. M' still maps the *new* normalized frame → visual. The effect T maps old-normalized → new-normalized.

### 6.2 Why this works

Instead of allowing the ray to become an arc after a circle inversion (which would require arc-vs-arc intersection), we **compose the inversion into the frame transform** and keep the ray as a line. The surfaces, transformed into the normalized frame by `M⁻¹`, may be circles — but the ray is always a line. This reduces all intersection math to:

- **Line vs line** (surface is a line in the normalized frame)
- **Line vs circle** (surface is a circle in the normalized frame)

No arc-ray parameterization, no arc-vs-arc systems.

### 6.3 Lines through infinity

In the normalized frame, a line is a **closed loop** on the Riemann sphere, passing through the point at infinity. A ray starting at `O` going toward `T`:

1. Traverses the **forward** direction (`t > 0`).
2. Reaches infinity (`t → +∞`).
3. Continues from the **back side** of infinity (`t → −∞`), looping back toward the origin.

The **first hit** may be beyond infinity. Hit ordering:

1. If any `t > 0` exists: earliest hit is `min(positive t values)`.
2. If no `t > 0`: earliest hit is `min(negative t values)` (most negative = closest to infinity from the back). Geometrically: the most-negative t is the point that just passed through ∞ going the other way — the first point the ray would reach if it continued past the horizon.
3. `t = 0` (origin on a surface) is excluded.

In the visual frame, "beyond infinity" may correspond to passing through a finite point (the image of ∞ under the frame transform).

### 6.4 Per-segment normalization

Every segment in a traced path can be individually normalized to a straight line using its own Möbius frame transform. All segments normalize to the **same original ray line** (same `Direction`). The ray's `origin` advances to each hit point; the `direction` stays fixed.

This does **not** mean a single transform can make all segments straight simultaneously. Each segment carries its own frame `M_i`.

---

## 7. Dual representation system

### 7.1 Mathematical representation

The math layer uses **uniform types** with no LineRay/ArcRay or LineSegment/ArcSegment distinction:

| Primitive | Representation |
|-----------|---------------|
| Point | `Vector2` + provenance tag (§8.1) |
| Direction | See §8.2 |
| Ray | See §8.3 |
| Segment | `{start: Vector2, end: Vector2, via: Vector2}` — three points fully define the segment including its carrier (see §8.4) |
| Generalized circle | `{a, b, c, d: float}` — **derived** from a segment's three points, not stored. Cached bidirectionally (§17). |
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

Conversion uses the Möbius frame transform `M` associated with each segment:

1. Transform `start`, `end`, and `via` through `M`.
2. Derive the carrier from the transformed three points (cached).
3. If the derived carrier is a line (`a == 0`): produce a `VisualLineSegment`.
4. If the derived carrier is a circle (`a != 0`): produce a `VisualArcSegment`.

Every conversion is **cached** (§17) so round-tripping returns the exact original value.

---

# Part III: Game Objects

## 8. Geometry primitives

### 8.1 Point

A 2D position with **transform provenance** — a record of how the point was derived from its original coordinates. *(See `DESIGN_PROVENANCE_PRIMITIVES.md` for full rationale.)*

```
Point:
    original:   Vector2                  # source coordinates (immutable after creation)
    coords:     Vector2                  # current position after all transforms
    transforms: Array[TrackedTransform]  # ordered sequence of transforms applied
    frame:      MobiusTransform          # aggregated transform (product of sequence)
```

**Invariant:** `frame.apply(original) == coords`. When inverse-pair simplification reduces the transform sequence to empty, `coords == original` exactly — no floating-point drift.

**Construction:** `Point.at(position)` creates a point with `original = coords = position`, empty transforms, identity frame.

**Transformation:** `point.transformed(t)` returns a NEW point with the `TrackedTransform` appended. Adjacent inverse pairs cancel: if the last transform in the sequence is `t.inverse`, the pair is removed instead of appended. This guarantees exact round-trip: `p.transformed(t).transformed(t.inverse).coords == p.coords`.

**Comparison:** `same_origin(other)` checks `original == other.original`; `same_position(other)` checks `coords == other.coords`. Both use exact equality.

### 8.2 Direction

An immutable two-point definition of a line's orientation.

```
Direction:
    start: Vector2
    end:   Vector2
    # Defines the line and its forward direction. Immutable once created.
    # Shared across all ray propagation steps within a transformative sub-chain.
    # Two-point representation: no stored angle, no unit vector.
```

In GDScript, Direction is a RefCounted (reference-counted, immutable) object. 'Shared' means multiple Ray instances hold a reference to the same Direction instance. Direction is never mutated after creation.

### 8.3 Ray

```
Ray:
    origin:    Vector2     # where the ray currently is (advances to hit points)
    direction: Direction   # which line the ray travels on (immutable within a sub-chain)
```

The `origin` changes at each hit; the `direction` stays fixed through transformative effects. Only projective effects create a new `Direction`.

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

No explicit `carrier` field is stored. The carrier is a **derived value**, computed from the three points on first access and **cached bidirectionally** through the computation cache (§17).

**Carrier ↔ via caching:** This derivation is reversible. Given a carrier and two endpoints, the via can be recovered (it lies on the intended arc). Given three points, the carrier can be derived (unique line or circle). Both directions are cached:

- **Forward (three points → carrier):** `derive_carrier(start, end, via) → GeneralizedCircle`. Stored in the cache.
- **Reverse (carrier + endpoints → via):** `derive_via(start, end, carrier) → Vector2`. Returns the cached original `via` exactly, not a recomputed approximation.

This guarantees that a segment constructed from three points, whose carrier is later derived, can reconstruct the **exact** original via from that carrier — no floating-point drift from the round-trip. *(Principle 5.)*

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

Surface segments are defined and stored in **visual (world) coordinates**. The trace loop transforms them into the current normalized frame for intersection computation (§12.1).

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

Which side a ray approaches from is computed once at the hit point (using a point slightly before the hit along the ray) and stored in the hit record. *(Principle 5.)*

### 9.3 Surface identity

Each surface has a stable `id` assigned at creation, never reused. Tie-breaking at identical ray parameters uses surface ID, then provenance rank.

### 9.4 State-conditional behavior

> **Status: PLANNED** — The `ConfigResolver` abstraction (including `FixedResolver` and `CategoricalResolver`) is not yet implemented. Surfaces currently hold fixed `SideConfig` per side directly, without a resolver layer. The `SideConfig.state_change` field exists structurally but is not applied during tracing.

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

    func kind() → Kind                         # default: PASS
    func is_terminal() → bool                  # kind() == TERMINAL
    func is_transformative() → bool            # kind() == TRANSFORMATIVE
    func get_mobius() → MobiusTransform         # default: identity
    func get_inverse_mobius() → MobiusTransform  # default: identity
    func get_tracked_transform() → TrackedTransform
    func normalized(carrier: GeneralizedCircle) → Effect  # default: self
    func get_display_name() → String
    func get_display_color() → Color

TerminalEffect (extends Effect):
    func kind() → TERMINAL

TransformativeEffect (extends Effect):
    _mobius:   MobiusTransform
    _tracked:  TrackedTransform
    _carrier:  GeneralizedCircle

    func kind() → TRANSFORMATIVE
    func get_mobius() → _mobius
    func get_inverse_mobius() → _tracked.inverse.mobius
    func get_tracked_transform() → _tracked
    func _create_normalized() → TransformativeEffect   # subclass override
    func normalized(carrier) → _create_normalized()

ReflectionEffect (extends TransformativeEffect):
    func _create_normalized() → ReflectionEffect.new(carrier)

CircleInversionEffect (extends TransformativeEffect):
    func _create_normalized() → CircleInversionEffect.new(carrier)
    # Requires circle carrier (a ≠ 0)
```

**Implemented effects:** `TerminalEffect`, `ReflectionEffect`, `CircleInversionEffect`. The `Kind.PROJECTIVE` enum value exists for forward compatibility but no projective effects are implemented yet.

**Planned effects:** `RigidMotionEffect` (§10.2), `ProjectiveEffect` and subtypes (§10.4), `CompoundTransformativeEffect` (§10.3).

A surface side's effect is an `Effect` instance, or `null` (pass-through). A surface with `null` on both sides and `interactive = false` on both sides has no gameplay effect and is not clickable for plan construction. However, it still participates in ray tracing as a pass-through — the ray passes through it, producing a step. If `is_target = true`, the pass-through still registers a target hit.

### 10.2 Transformative effects

All are Möbius transformations. Composition = matrix multiplication.

**Reflection:** Mirrors the ray across the surface's carrier line. Carrier must be a line (`a = 0`). Self-inverse.

**Circle inversion:** Inverts the ray through the surface's carrier circle. Carrier must be a circle (`a ≠ 0`). **The inversion circle is always the surface's own carrier** — no separate parameter. Self-inverse. Circle inversion is what makes arc paths appear in the visual frame.

**Rigid motion:** *(Status: PLANNED)* Rotation by θ and translation by d. General Euclidean isometry excluding reflections. Möbius matrix: `[[e^{iθ}, d], [0, 1]]`. Not self-inverse; inverse is precomputed and cached. Enables teleport/portal mechanics.

### 10.3 Compound transformative effects

> **Status: PLANNED** — `CompoundTransformativeEffect` is not yet implemented.

A single surface may apply multiple transformative effects in sequence. The compound is their composition, precomputed as a single `MobiusTransform`. `CompoundTransformativeEffect` **is a subtype of** `TransformativeEffect` — it implements the same interface (`get_mobius()`, `get_inverse_mobius()`) and fits in the `SideConfig` type union without modification.

```
CompoundTransformativeEffect extends TransformativeEffect:
    elementary:       Array[TransformativeEffect]
    combined_mobius:   MobiusTransform    # precomputed product (with correct conjugating flag)
    combined_inverse:  MobiusTransform    # precomputed inverse product
```

Examples: reflect + translate = glide reflection. Reflect + rotate = rotated reflection.

### 10.4 Projective effects

> **Status: PLANNED** — No projective effects are implemented. `Effect.Kind.PROJECTIVE` exists in the enum for forward compatibility.

All produce an outgoing **line ray** in the visual frame, determined solely by the hit point. Incoming direction is discarded. The Möbius frame **resets to identity** after a projective effect.

**Line normal projection:** Outgoing ray ⊥ to the surface line at the hit point. Back-propagation: `H` = orthogonal projection of target onto surface line.

**Circle normal projection:** Outgoing ray along the radius at the hit point. Back-propagation: `H` = intersection of line(center, target) with arc.

**Semi-circle directional projection:** Outgoing ray in the direction **normal to the semicircle's diameter line**, regardless of hit position. The semicircle's carrier circle defines the geometry; the diameter is determined by the segment's start and end points (the diameter endpoints). The diameter has two normals (opposite directions); which one is used depends on the **side** of approach (from the hit record) — the ray exits toward the side opposite the approach. This is consistent with projective semantics: the incoming *angle* is discarded, but the *side* still matters (Principle 4). A sub-segment of the semicircle may be used as the surface, exactly like any other effect. Back-propagation: `H` = intersection of line(target, −normal) with the arc segment. No separate direction parameter is needed — the direction is derived from the segment geometry and the side.

### 10.5 Terminal effect: block

Stops the ray. No outgoing ray, no frame update. Also the **implicit effect after 32 hits** (§12.6).

### 10.6 State changes

> **Status: PLANNED** — The `SideConfig.state_change` field exists structurally but state changes are not applied during tracing. `GameState.flags` exists but is not mutated by surface hits.

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

### 10.7 Frame behavior by effect type

| Effect type | Frame update | Direction change |
|-------------|-------------|-----------------|
| Transformative | `M' = M ∘ T` | No — `Direction` stays the same |
| Projective | `M' = Identity` (reset) | Yes — new `Direction` from the projection |
| Terminal | No update — path ends | N/A |
| Pass-through | No update | No |

Terminal effects do NOT reset the Möbius frame — they simply end the path. In the planner, post-terminal entries end the trace, so no sub-chain inherits a post-terminal frame.

### 10.8 Effect Möbius matrices

Explicit formulas for each transformative effect's Möbius matrix. In all formulas, complex numbers are written as `x + iy` and the matrix is `[[α, β], [γ, δ]]`.

**Reflection** across line `bx + cy + d = 0` (general form, no normalization required):

Let `n = b + ic` (the complex normal, unnormalized). Let `norm² = b² + c²`. Then:
```
α = -n̄         β = -2d · n̄ / norm²
γ = 0           δ = n
conjugating = true
```
Anti-conformal: applies as `z → (α·z̄ + β) / (γ·z̄ + δ)`. Self-inverse.

Constraint: `b² + c² > 0` (the carrier must be a non-degenerate line). Validated at load time.

**Circle inversion** through circle `a(x²+y²) + bx + cy + d = 0` (carrier of the surface):

Let `center = -b/(2a) - i·c/(2a)` (complex center) and `r² = (b² + c² - 4ad) / (4a²)`. Then:
```
α = center      β = r² - |center|²
γ = 1           δ = -conj(center)
conjugating = true
```
Anti-conformal: applies as `z → center + r² / (conj(z) - conj(center))`, equivalently `z → center + r² · conj(z - center) / |z - center|²`. Self-inverse: `M² = r² · I` for any center. Verification: a point on the circle maps to itself (e.g., for center=1, r=1: z=2 → w = 1 + 1/(2-1) = 2).

**Rigid motion** (rotation by θ, translation by d = d_x + i·d_y):
```
α = e^{iθ}      β = d
γ = 0            δ = 1
conjugating = false
```
Conformal: applies as `z → e^{iθ}·z + d`. Inverse: `α = e^{-iθ}, β = -e^{-iθ}·d, γ = 0, δ = 1, conjugating = false`.

---

# Part IV: Simulation

*Pseudocode in this part is language-agnostic and illustrative, not prescriptive GDScript. Data definitions (§8–9) use a schema notation. The implementation should follow GDScript conventions.*

## 11. Intersection system

### 11.1 Single intersection primitive

```
func intersect_line_with_gcircle(ray: Ray, segment: Segment) → Array[HitCandidate]
```

The function derives the segment's carrier from its three points (cached — §17). Dispatches internally on `carrier.is_line()`:

| Carrier | Algorithm |
|---------|-----------|
| Line (`a = 0`) | 2×2 linear system. At most 1 candidate. |
| Circle (`a ≠ 0`) | Substitute ray parametric form into circle equation. Quadratic → 0, 1, or 2 candidates. |

Candidates are filtered:

1. **Segment bounds** (`is_on_segment`): the candidate P must lie on the arc/line from start S through via V to end E. This is determined by the **cross-ratio containment test**, which unifies line and arc containment under a single algebraic formula:

   The point P is on the segment iff `Re(cross_ratio(S, P; E, V)) >= 0`.

   Computed in homogeneous coordinates to handle the point at infinity (line segments with `via = INF`):
   1. Compute homogeneous determinants: `hdet(S,V)`, `hdet(E,P)`, `hdet(S,P)`, `hdet(E,V)`, where `hdet(A,B) = zA·wB - zB·wA` treating each point as `(z, w)` with `w = 1` for finite points and `w = 0` for the point at infinity.
   2. Compute `num = cmul(hdet(S,V), hdet(E,P))` and `den = cmul(hdet(S,P), hdet(E,V))` where `cmul` is complex multiplication (treating `Vector2` as `(real, imag)`).
   3. Compute `product = cmul(num, conj(den))`.
   4. The point is on the segment iff `product.x >= 0`.

   This works correctly for arcs of any span (including major arcs > 180°), lines through infinity, and full circles. No center computation, no angle comparison, no epsilon threshold.

For full-circle segments (where start equals end after traversing the full carrier), all points on the carrier are considered contained.

2. **Side determination**: left or right of the segment's traversal direction.

### 11.2 Hit record

```
HitRecord:
    t:             float       # ray parameter (t > 0 forward, t < 0 beyond-infinity)
    point:         Point       # intersection point (provenance-aware, §8.1)
    segment:       Segment     # the segment hit (Surface found via norm_to_surface mapping)
    side:          Side        # LEFT or RIGHT
    on_segment:    bool        # true if the hit point lies on the segment arc/line
    at_endpoint:   int         # 0 = interior, 1 = at start, 2 = at end (§31.3.1)
    blocked_left:  bool        # whether the LEFT side is blocked at this hitpoint
    blocked_right: bool        # whether the RIGHT side is blocked at this hitpoint
```

The `point` field is a `Point` (§8.1), not a raw `Vector2`. The HitRecord references the normalized `Segment`; the original `Surface` is found via a `norm_to_surface` mapping dictionary maintained by the tracer (§12.1).

The `at_endpoint` field is set by the three-tier provenance endpoint detection system (§31.3.1). When `at_endpoint > 0`, the `point.coords` are the exact defining coordinates of the endpoint — snapped from provenance, not from the quadratic solver.

`is_fully_blocked()` returns `blocked_left and blocked_right`. An effect fires only when the hitpoint is fully blocked (§12.1).

### 11.3 Earliest hit selection

1. Partition hits into `forward` (t > 0) and `beyond` (t < 0).
2. If `forward` non-empty: winner = `min(forward, key=t)`.
3. Else if `beyond` non-empty: winner = `min(beyond, key=t)` (most negative t).
4. Ties: surface ID (smaller wins), then provenance rank.

When multiple surfaces produce hits at the same `t`, only the tie-breaking winner is processed. Other surfaces at that `t` are ignored for that ray step. A subsequent ray cast (after the winner's effect) may hit one of the previously-losing surfaces.

### 11.4 Degenerate cases

| Case | Resolution |
|------|-----------|
| Ray origin on a surface | Exclude that surface from this cast. |
| Ray parallel to line surface | No intersection. |
| Ray tangent to circle surface | Single hit. |
| Ray carrier coincides with surface carrier | No intersection — ray travels along the surface. |
| Zero-length segment | Never produces a hit. |

The tracer passes a `skip_segment` parameter — the segment from the most recent hit, excluded by **reference equality** (object identity), not by epsilon comparison on the `t` parameter. This is consistent with §31.3 (no epsilon decisions).

### 11.5 Unbounded carrier intersection

```
func intersect_line_with_carrier(ray: Ray, carrier: GeneralizedCircle) → Array[HitCandidate]
```

Same quadratic intersection as §11.1 but **without the segment-bounds filter**. Returns all intersection points on the unbounded carrier (the full line or full circle). Used internally and available for future PLANNED mode extensions.

### 11.6 See-through parameter

*(Not yet implemented — specified for the visibility system §15.)*

`find_all_hits` will accept an optional `see_through: Set[Surface]` parameter. Surfaces in this set will be treated as transparent — rays pass through them without recording a hit. Used by the visibility system (§15) for truncating boundaries. Distinct from `skip_segment` (departure surface exclusion, §11.4).

---

## 12. Ray tracing

### 12.1 The tracing loop

The tracer uses a **stage-based hitpoint walk**: each stage intersects the shared ray with ALL surface segments at once, sorts the hitpoints in projective order, and walks them sequentially. A "stage" ends when a transformative effect fires — the frame changes, surfaces are re-normalized, and a new stage begins.

The initial ray is constructed from the player and cursor positions:
```
initial_direction = Direction(player_position, cursor_position)
shared_ray = Ray(player_position, initial_direction)
```

The two modes (PHYSICAL and PLANNED, §14.1) differ ONLY in which effects are applied at each hitpoint.

```
enum TraceMode { PHYSICAL, PLANNED }

func trace(shared_ray: Ray, surfaces: Array, mode: TraceMode,
           plan_entries: Array = [], game_state: GameState,
           bounds: Rect2, cursor_pos: Vector2) → TracedPath:
    game_state = game_state.copy()
    transform_stack = []           # Array[TrackedTransform]
    frame = MobiusTransform.IDENTITY
    ray = shared_ray               # working copy — origin advances, direction unchanged
    steps = []
    hit_count = 0
    last_hit_segment = null        # skip_segment: excluded by reference equality (§11.4)
    targets_hit = {}
    plan_index = 0
    frame_dirty = true
    norm_surfaces = []
    norm_to_surface = {}           # normalized Segment → original Surface mapping

    while hit_count < 32:
        # === Stage boundary: recompute frame and normalize surfaces ===
        if frame_dirty:
            frame = compose_all(transform_stack)       # product of TrackedTransforms
            norm_surfaces = normalize_all(surfaces, frame.inverse())
            norm_to_surface = build_mapping(norm_surfaces, surfaces)
            frame_dirty = false

        # === Collect ALL hits in this stage ===
        carrier_hits = find_all_hits(ray, norm_segments, skip=last_hit_segment)

        # Inject virtual hitpoints
        origin_hp = HitRecord(t=0.0, point=ray.origin, segment=null)
        hitpoints = carrier_hits + [origin_hp]

        if cursor is reachable:
            cursor_t = project_point_on_ray(ray, cursor_in_frame)
            cursor_hp = HitRecord(t=cursor_t, point=cursor_in_frame, segment=null)
            hitpoints.append(cursor_hp)

        # === Projective sort ===
        # Order: positive t ascending, then negative t ascending, then t=0 last.
        # Ties broken by segment instance ID.
        hitpoints = projective_sort(hitpoints)

        # === Walk sorted hitpoints ===
        step_left_blocked = false
        step_right_blocked = false
        stage_ended = false

        for hp in hitpoints:
            hit_count += 1
            vis_start = frame.apply(step_origin)
            vis_end = frame.apply(hp.point)

            # Zero-length skip (§31.3 — exact equality, no epsilon)
            if vis_start == vis_end:
                ... (handle cursor injection or origin escape)
                continue

            # Generate visual step
            steps.append(Step(vis_start, vis_end, frame, hp, shared_ray))

            # Cursor check
            if hp == cursor_hp:
                path.cursor_index = len(steps)
                break to next stage

            # Target tracking
            orig_surf = norm_to_surface[hp.segment]
            if orig_surf.is_target and hp.on_segment:
                targets_hit[orig_surf.id] = true

            # === Blockage accumulation at endpoints ===
            step_left_blocked |= hp.blocked_left
            step_right_blocked |= hp.blocked_right

            if hp.at_endpoint > 0 and not fully_blocked:
                # Check ALL other segments sharing this endpoint
                for other_seg in norm_segments:
                    if other_seg == hp.segment: continue
                    ep = at_which_endpoint(hp.point, other_seg)  # exact equality
                    if ep > 0:
                        sides = endpoint_blocked_sides(hp.point, other_seg, ray, ep)
                        step_left_blocked |= sides[0]
                        step_right_blocked |= sides[1]

            fully_blocked = step_left_blocked and step_right_blocked

            # === Effect application (mode-dependent) ===
            if mode == PHYSICAL:
                if fully_blocked and effect.is_transformative():
                    apply_effect = true
            elif mode == PLANNED:
                if plan_index < plan_entries.size() and surface matches:
                    apply_effect = true
                    plan_index += 1

            if fully_blocked:
                step_left_blocked = false    # reset for next hitpoint
                step_right_blocked = false

            if apply_effect:
                # Push transform, advance ray, start new stage
                transform_stack.append(effect.get_tracked_transform())
                ray = Ray(origin=inverse.apply(hp.point), direction=ray.direction)
                last_hit_segment = null
                frame_dirty = true
                stage_ended = true
                break   # → next iteration of outer while loop

            # No effect — advance within this stage
            last_hit_segment = hp.segment
            step_origin = hp.point

        if not stage_ended:
            break   # no more stages to process

    return TracedPath(steps, targets_hit)
```

**Key design decisions:**
- **All hits precomputed per stage**, not one-at-a-time. This enables projective sort and the cursor to compete naturally with carrier hits.
- **`skip_segment`** (single segment, reference equality) replaces the `excluded_surfaces` set. Only the most recently hit segment is excluded — the next stage clears it.
- **Blockage accumulation** at endpoints: `blocked_left` and `blocked_right` accumulate across all segments sharing the endpoint. An effect fires only when both sides are blocked (`is_fully_blocked()`). This handles Y-junctions and T-junctions correctly.
- **Transform stack** stores `TrackedTransform` objects (forward + inverse pair). The frame is recomposed from the stack on each stage boundary, enabling inverse-pair cancellation (§8.1).
- **Source-based cancellation:** When a new transform is the inverse of the top of the stack AND originates from the same surface (same source), the pair is popped instead of pushed. This prevents infinite loops with self-inverse effects (reflection, circle inversion) when the ray re-hits the same surface.
- **No surface filtering:** All surfaces always participate in hit detection. The tracer never filters surfaces by position, proximity, or any other spatial criterion. This is a conscious architectural decision — simpler, more correct, and avoids edge cases from culling.
- The origin hitpoint (t=0) is always injected and sorted last by `projective_sort`. When reached, the ray escapes to bounds (forward and return segments).

### 12.2 Step record

See §14.7 for the full `Step` definition and §14.6 for the five step types. Each step carries its own Möbius frame `M`. In that frame, the step is a straight line. The visual representation (line or arc) is determined by applying `M`.

### 12.3 Converting to visual

```
func to_visual_path(traced: TracedPath) → VisualPath:
    for step in traced.steps:
        # Guard: escape steps have end=INF — render as line to viewport edge, not via normal conversion.
        if step.end == Vector2(INF, INF):
            visual_segments.append(VisualEscapeSegment(step.start, step.direction, step.frame))
            continue
        # In the normalized frame, all steps are lines, so via is
        # any point between start and end (e.g., the arithmetic mean).
        mid = (step.start + step.end) / 2
        math_segment = Segment(step.start, step.end, mid)
        visual_segments.append(to_visual(math_segment, step.frame))
    return VisualPath(visual_segments)
```

### 12.4 Frame updates

**Transformative:** `M' = M ∘ T`. Direction stays the same. Origin advances to `T⁻¹(hit_point)`.

**Projective:** `M' = I` (reset). New Direction from the projection. This is the only place Direction changes.

### 12.5 Beyond-infinity in the visual frame

- `M = I`: arrow travels to viewport edge, gap, returns from opposite edge.
- `M` involves inversion: ∞ maps to the inversion center. Arrow converges to that point, passes through, diverges.

### 12.6 Effect limit

Maximum **32 total hits** per arrow path (including pass-throughs). After 32, the path terminates. This prevents infinite loops from any source — including overlapping pass-through surfaces.

When the limit is reached, the preview shows a truncation marker (small stop icon) at the final hit point. During arrow flight, the arrow stops with a distinct visual cue (flash/fade).

---

## 13. Planning algorithm

### 13.1 Image chain method (from first principles)

The core planning trick: a path that bounces off a surface via a transformative effect can be "unfolded" by applying the **inverse transformation** to the target, producing an **image point**. In the normalized frame, the unfolded path to the image is a **straight line** — the bounce is eliminated. For several transformative surfaces in a fixed order, chain those inverse transformations so every leg of the ideal plan is a **straight segment** in the normalized frame — no step-by-step "what did the ray hit first?" search.

**Forward images:** transform the origin through planned surfaces' effects in order.
**Backward images:** inverse-transform the cursor through planned surfaces' effects in reverse order.

The planned path connects corresponding images with straight segments **in the normalized frame**. In the visual frame, these legs may be straight lines or circular arcs, depending on the accumulated Möbius frame transform. Each segment ends where the ray meets the relevant planned surface carrier (in the normalized frame, this is always a line-vs-generalized-circle intersection). The bounce order is **fixed by the plan**.

This is deterministic: the plan fixes the order, so you're not searching "which surface comes next?" — only "where does this known line cross this known surface?" The Möbius frame ensures this works uniformly for reflections, inversions, and rigid motions.

### 13.2 Transformative sub-chains

For a contiguous sub-sequence of transformative surfaces:

```
func plan_transformative_subchain(sub_origin, sub_target, entries, initial_frame):
    # entries is Array[{surface, side}] — side is known per entry.
    image = sub_target
    frame = initial_frame
    for i in reverse(entries):
        effect = entries[i].surface.active_side_config(entries[i].side, game_state).effect
        image = cache.apply(image, effect.get_inverse_mobius())
        frame = frame.compose(effect.get_inverse_mobius())

    # Compute bounce points: walk forward, intersecting the aim line
    # with each entry's surface carrier.
    aim_ray = make_ray(sub_origin, image)
    # make_ray creates a ray toward image regardless of direction.
    # image may be 'behind' the origin if the image chain reflects past the player.
    bounce_points = []
    current_image = image
    for i in forward(entries):
        carrier = derive_carrier(entries[i].surface.segment)
        hit = intersect_line_with_carrier(aim_ray, carrier)
        # If multiple candidates, select the one with smallest positive t (or most negative t if no positive). Same rule as §11.3.
        bounce_points.append(hit)
        effect = entries[i].surface.active_side_config(entries[i].side, game_state).effect
        current_image = cache.apply(current_image, effect.get_mobius())
        aim_ray = make_ray(hit, current_image)

    return PlannedSubpath(sub_origin, bounce_points, sub_target)
```

# The aim_ray and image are in the 'fully unfolded' frame (after all inverse transforms). Surface carriers are in visual coords. This works because each sub-chain starts with identity frame (§13.4 Pass 2), so unfolded = visual.

Each leg is a straight line in the normalized frame. In the visual frame, legs may be arcs.

### 13.3 Projective break points

> **Status: PLANNED** — Projective break points and mixed chain planning depend on projective effects (§10.4), which are not yet implemented. The planner currently handles only transformative sub-chains (§13.2).

Projective surfaces are **break points** that partition the plan into transformative sub-chains. At each break point:

1. Back-propagate: find the hit point from which the projected ray reaches the current target.
2. That hit point becomes the new target for the preceding sub-chain.
3. Frame resets to identity.

Back-propagation per effect:

| Effect | Given target T, find hit point H |
|--------|----------------------------------|
| Line normal | H = orthogonal projection of T onto the surface line. |
| Circle normal | H = intersection of line(center, T) with the arc. |
| Semi-circle directional | H = intersection of line(T, −normal) with the arc, where normal is perpendicular to the segment's diameter line. |

The hit point returned by `back_propagate()` must lie within the surface's segment bounds (checked using the same arc containment test as §11.1). If the carrier-level intersection exists but falls outside the segment, `back_propagate()` returns null.

Back-propagation targets are always in the **visual frame** (identity frame after a projective reset, or the current accumulated frame for terminal surfaces). Since projective effects reset the frame to identity, and back-propagation is called immediately after encountering a projective surface, the target `T` is in the visual frame.

### 13.4 Mixed chain algorithm

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
            terminal_ray = Ray(real_target, Direction(real_target, segment_center(entry.surface.segment)))
            terminal_hit = intersect_line_with_gcircle(terminal_ray, entry.surface.segment)
            terminal_point = terminal_hit.point if terminal_hit else entry.surface.segment.start
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
        subpath = plan_transformative_subchain(
            current_origin, sc.target, sc.entries, MobiusTransform.IDENTITY)
        all_steps.extend(subpath.steps)
        current_origin = sc.target
    
    return PlannedPath(all_steps)
```

### 13.5 Plan validation

A plan is valid for a given origin and cursor if all back-propagations succeed, all image-chain legs cross their surfaces, and the aim ray reaches the first planned surface.

When invalid, the preview shows the partial path up to the first failure.

An empty plan is valid — it means 'fire straight in the aim direction.' A plan consisting only of interactive terminal entries is valid — the trace ends at the first terminal.

### 13.6 Side in planning

Each plan entry is `{surface, side}` — the side is selected by the player at plan-construction time (§4.2) and is **fixed** for that entry. This eliminates a potential circular dependency: the effect (and its Möbius transform) depends on the side, and the image-chain geometry depends on the effect. Because the side is known before the planner runs, the effect is unambiguous at every step.

The planner never guesses or resolves the side — it reads `entry.side` and looks up the corresponding `SideConfig`. If the side's effect is null (pass-through), the entry is skipped (no frame change, but a step is still recorded).

### 13.7 State simulation during planning

Before computing the backward image chain, the planner walks the plan **forward** and applies state changes to a **temporary copy** of the game state. This ensures that later plan entries see the post-effect state of earlier entries. For example, if plan entry 1 breaks a surface (state change), and plan entry 2 relies on that surface being broken (conditional effect), the planner correctly sees the broken state when evaluating entry 2.

The temporary state is discarded after planning — it does not affect the actual game state until the shot is fired and the physical trace runs.

### 13.8 Planned trace as Steps

The planned trace uses the same `trace()` function as the physical trace (§12.1), with `mode = PLANNED`. It produces **Steps** with the same structure — same hitpoints, same `start`/`end` coordinates. The only difference is which effects are applied (plan-queue-based rather than `is_on_segment`-based), which affects the `frame` field.

Both traces are `Array[Step]` with 1:1 index correspondence by construction. The step tree merge (§14.5) compares frames at each index.

---

## 14. Step system

### 14.1 Two trace modes — unified hitpoints

Hit-point computation uses a **unified hitpoint model**: both modes intersect the shared ray with ALL surface carriers (ignoring `is_on_segment`), producing the **same hitpoints** (same step boundaries). The modes differ ONLY in which effects are applied at each hitpoint.

| Mode | Effect application rule | When it runs |
|------|------------------------|-------------|
| **PHYSICAL** | Effect applied ONLY if the hitpoint `is_on_segment`. Off-segment carrier intersections are pass-throughs (no effect, no frame change). | Always runs from origin to escape/block. |
| **PLANNED** | Effect applied ONLY if the surface is the NEXT entry in the plan queue, regardless of `is_on_segment`. | Always runs from origin to escape/block. |

Both modes run the full trace from origin to escape/block — neither stops at the cursor. The cursor is a **virtual hitpoint** (§12.1) that competes with carrier intersections via t-sorting. `cursor_index` is the step index where the cursor falls.

Because both modes produce the same hitpoints, their step arrays have **1:1 index correspondence by construction**. Step `i` in the planned trace and step `i` in the physical trace have the same `start` and `end` in visual coords — they differ only in `frame` when different effects were applied.

Both modes use the same intersection pipeline, the same carriers, and the same scene state. **Caching is shared** between modes — any intersection, transform, or carrier derivation computed by one mode is available to the other.

### 14.2 Divergence: exact definition

Divergence is defined by comparing the PLANNED and PHYSICAL traces step by step. Since both modes produce the **same hitpoints** (§14.1), steps at the same index always have the same `start` and `end` coordinates. The ONLY field that can differ is the **frame** (the accumulated Möbius transform).

**Aligned** — step `i` is aligned if the planned and physical traces have the same **frame ID** at that step. This means both modes applied the same effects up to and including step `i`.

**Diverged** — step `i` is diverged if the frame IDs differ. This means a surface effect was applied in one mode but not the other (e.g., physical hit was on-segment but planned didn't have it in the queue, or vice versa).

Once divergence occurs, the traces never re-converge — divergence is permanent for the remainder of the shot. *(Principle 12.)* Different frames cause different normalized surfaces, so subsequent carrier intersections will differ — but the hitpoints are still computed identically within each mode's frame.

### 14.3 Divergence examples (non-exhaustive)

The following are common scenarios that trigger divergence, but they are all instances of the two cases above:

- Physical ray hits a different surface before the planned one (case 1: different hit point).
- Physical ray hits the planned surface on a different side, triggering a different effect (case 2: different frame after the hit).
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

The merge walks both traces **by index**. Since both modes produce the same hitpoints (§14.1), steps at the same index have identical `start` and `end`. The merge compares only the **frame ID** at each step.

```
func merge(planned_steps, physical_steps, cursor_index) → Array[MergedStep]:
    merged = []
    diverged = false
    
    # Both arrays have the same length (same hitpoints).
    for idx in range(len(planned_steps)):
        p = planned_steps[idx]
        r = physical_steps[idx]
        past_cursor = (idx >= cursor_index)
        
        if not diverged:
            if p.frame.id == r.frame.id:
                # Aligned — same effect was applied (or both passed through).
                merged.append(MergedStep(p, ALIGNED if not past_cursor else ALIGNED_POST_PLANNED))
            else:
                # Diverged — different effects applied at this hitpoint.
                diverged = true
                merged.append(MergedStep(p, DIVERGED_PLANNED if not past_cursor else DIVERGED_POST_PLANNED))
                merged.append(MergedStep(r, DIVERGED_PHYSICAL))
        else:
            # Already diverged — all subsequent steps are diverged.
            merged.append(MergedStep(p, DIVERGED_PLANNED if not past_cursor else DIVERGED_POST_PLANNED))
            merged.append(MergedStep(r, DIVERGED_PHYSICAL))
    
    return merged
```

The merge is trivial because hitpoints are mode-independent. No partial alignment or sub-segmentation is needed — steps always have the same geometry. *(Principle 22, §14.8.)*

After divergence, the planned and physical traces are in different frames, so their subsequent hitpoints will differ (different normalized surfaces produce different carrier intersections). Both traces continue independently; the merge emits both as diverged steps.

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
Step:
    start:   Vector2           # in visual coords (frame.apply(normalized_start))
    end:     Vector2           # in visual coords (frame.apply(normalized_end))
    ray:     Ray               # the originating ray in the normalized frame (provenance)
    frame:   MobiusTransform   # the frame active during this step (maps normalized → visual)
    hit:     HitRecord?        # null if ray escaped
    type:    StepType          # ALIGNED, ALIGNED_POST_PLANNED, DIVERGED_PHYSICAL,
                               # DIVERGED_PLANNED, DIVERGED_POST_PLANNED
```

**Shared ray:** Every step stores the `shared_ray` — the original ray from the player in the aim direction. Both PHYSICAL and PLANNED modes use the same shared ray, guaranteeing all steps share the same Ray object by reference identity. This provides:
1. The original ray is always available for re-computation (numerical stability — avoids accumulating floating-point drift).
2. Any system that needs to re-intersect a step's ray with surfaces should use `step.ray`, not construct a new ray from endpoints.

```
TracedPath:
    steps:       Array[Step]
    targets_hit: Set[int]     # surface IDs of targets hit during this trace
    final_state: GameState    # the game state copy after all trace state changes
```

`PlannedPath = Array[Step]`

`VisualPath = Array[VisualLineSegment | VisualArcSegment]`

`VisualEscapeSegment = {start: Vector2, direction: Direction, frame: MobiusTransform}`

The step tree is the **single source of truth** for what happens on a shot. *(Principles 2, 15.)*

### 14.8 Mode interaction and caching

The PLANNED and PHYSICAL traces share the computation cache. When one trace computes an intersection or transform, the result is cached. The other trace retrieves cached results for identical computations.

Frame comparison for alignment is by **Möbius transform ID** (§17.3). Since both traces share the cache, identical compositions through the same effects produce the same cached transform IDs. Aligned steps (same effects applied) have identical frame IDs by construction.

**Precondition:** both traces must start with the global identity transform (ID 0) and use the same shared ray.

### 14.9 Empty-plan step tree

With an empty plan, the PLANNED trace has no plan queue entries, so no effects are ever applied — every hitpoint is a pass-through. The PHYSICAL trace applies effects normally. Since both produce the same hitpoints, the merge compares frames: in the empty-plan case, every frame matches (both are identity), so all steps are **ALIGNED** (before cursor) or **ALIGNED_POST_PLANNED** (after cursor).

### 14.10 Post-cursor computation

There is no separate post-cursor computation. Both traces run the full path from origin to escape/block in a single pass. The cursor is a virtual hitpoint (§12.1) that creates a step boundary. `cursor_index` is the step index at the cursor position. Steps before `cursor_index` are pre-cursor; steps at or after are post-cursor. The step type (ALIGNED vs ALIGNED_POST_PLANNED, or DIVERGED_PLANNED vs DIVERGED_POST_PLANNED) is determined by comparing against `cursor_index`.

---

## 15. Visibility

> **Status: PLANNED** — The visibility system is not yet implemented. The design below is the intended specification for future development. The infrastructure it depends on (same intersection pipeline, same effects) is in place.

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

**`see_through` parameter in `find_all_hits`:** *(Not yet implemented — specified for the visibility system §15.)* Surfaces in the `see_through` set will be treated as transparent — rays pass through them without recording a hit. This is distinct from `skip_segment` (which prevents re-hitting the departure surface, §11.4). Both can be active simultaneously.

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

Visibility uses the **same intersection pipeline**, **same effects**, and **same scene state** as trajectory. *(Principle 14.)* The same `intersect_line_with_gcircle`, the same Möbius transforms, the same game state. If a surface is broken in the game state, it is broken for visibility too.

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

**Frame transform after hit:** M = circle inversion Möbius matrix. In the normalized frame, the ray continues as a line from the new-frame bounce point toward the cursor. In the visual frame, this second leg renders as a **circular arc** (the image of a straight line under inversion).

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

**Result:** two transformative sub-chains ({M1} and {M2}) separated by projective break point P. The frame resets to identity at P.

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
4. **Planned trace computed** (image chains + back-propagation). Produces planned Steps.
5. **Step tree merged** from planned and physical traces (§14.5).
6. **Arrow flight animated** step-by-step. Surfaces visually update at state-change hit points.
7. **Animation completes** (or player skips). Arrow disappears.
8. **State promoted:** the trace copy's game state and targets_hit replace the real ones.
9. **Win check:** if all targets hit → level complete. Otherwise → play phase continues.
10. **Game time unfreezes.** Player physics resume with updated state.

---

# Part V: Infrastructure

## 17. Computation cache

### 17.1 Reversibility guarantee

**All reversible computations are stored on first calculation.** Reversal returns the cached value — never recomputed. *(Principle 5.)*

This guarantees `f(f⁻¹(x)) == x` exactly, eliminating floating-point round-trip bugs.

### 17.2 What gets cached

| Computation | Forward → Reverse (from cache) |
|-------------|-------------------------------|
| **Carrier derivation** | `derive_carrier(start, end, via) → GeneralizedCircle` / `derive_via(start, end, carrier) → via` (returns the exact original via, not a recomputed approximation) |
| Frame conversion | `to_visual(P, M) → P'` / `to_math(P', M) → P` |
| Point reflection | `reflect(P, line) → P'` / `reflect(P', line) → P` |
| Point inversion | `invert(P, circle) → P'` / `invert(P', circle) → P` |
| Rigid motion | `rigid(P, R, d) → P'` / `rigid_inverse(P') → P` |
| Carrier transform | `M(C) → C'` / `M⁻¹(C') → C` |

### 17.3 Cache architecture

Keyed by **provenance** (point identity), not coordinates. Avoids float-equality comparisons.

**Transform IDs:** each `MobiusTransform` receives a unique ID at construction time via a global counter. The identity transform has a fixed ID (0). Composed transforms receive new IDs. Mathematically identical transforms constructed separately get **different** IDs — identity is by construction, not by value comparison.

**Multi-key entries:** the `(point_id, transform_id) → point_id` pattern shown above is the common case. The cache also stores carrier derivations keyed by `(start_id, end_id, via_id) → carrier`. The cache is a general-purpose provenance-keyed store, not restricted to two-key entries.

```
TransformCache:
    entries: Dictionary[(point_id, transform_id), point_id]

    func apply(point, transform) → Point:
        key = (point.id, transform.id)
        if key in entries:
            return points[entries[key]]
        result = _compute(point, transform)
        entries[(point.id, transform.id)] = result.id
        entries[(result.id, transform.inverse_id)] = point.id
        return result
```

### 17.4 Manual overrides

The cache supports **manual override entries** from level data. These enforce exact algebraic identities that floating-point cannot guarantee.

Example: four mirrors in a `+` shape. Reflecting clockwise through all four should produce identity (`M = I`), but composing four Möbius matrices accumulates rounding error. The level declares the cycle identity as an override; the cache returns `I` exactly.

Override sources:
- **Level data**: declares cycle identities.
- **Validation**: at load time, computed composition is checked against the override. Large discrepancy flags a level design error.

### 17.5 Cache lifetime

Persists for the duration of a **shot**. Cleared between shots (scene state may have changed). Manual overrides persist across shots (level data).

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
    expected_result: MobiusTransform     # the declared composition result (typically identity)
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
| **Math** | Intersection, effects, planning, visibility, cache, Möbius transforms | **None.** Pure GDScript with `Vector2` and `float`. |
| **Visual** | Math→visual conversion, drawing, input | `Node2D`, `_draw()`, `draw_line()`, `draw_arc()`, `Color`. |
| **Game** | Scene management, state, shot lifecycle, player | `CharacterBody2D`, autoload singletons, scene tree. |

The math layer has **zero Godot engine dependencies** beyond `Vector2`. Testable in isolation.

**GDScript performance note:** GDScript is significantly slower than C++ for tight numerical loops. For the intersection kernel (32 steps × N surfaces per frame), avoid object allocation in hot loops — reuse arrays, use `PackedFloat64Array` for batch math where possible. If profiling shows the intersection kernel is the bottleneck, consider moving it to a GDExtension (C++) module. The math layer's zero-dependency design makes this migration straightforward.

**Resource mapping:** `LevelData`, `SurfaceData`, and effect configurations extend Godot `Resource`. Each has `@export` properties for serialization to `.tres`. Surfaces in the scene tree are `Node2D` instances that reference their `SurfaceData` resource.

### 20.2 Project structure

```
ricochet-game-v2/
├── project.godot
├── GAME_SPEC.md
├── scripts/
│   ├── math/
│   │   ├── generalized_circle.gd
│   │   ├── segment.gd
│   │   ├── mobius_transform.gd
│   │   ├── intersection.gd
│   │   ├── transform_cache.gd
│   │   ├── effects/
│   │   │   ├── transformative.gd
│   │   │   ├── projective.gd
│   │   │   └── terminal.gd
│   │   ├── tracer.gd
│   │   ├── planner.gd
│   │   └── visibility.gd
│   ├── visual/
│   │   ├── visual_converter.gd
│   │   ├── path_renderer.gd
│   │   └── visibility_renderer.gd
│   └── game/
│       ├── game_manager.gd
│       ├── player.gd
│       ├── surface_node.gd
│       ├── level_loader.gd
│       └── arrow_animator.gd
├── scenes/
│   ├── main.tscn
│   ├── levels/
│   ├── ui/
│   └── editor/
└── resources/
    └── levels/
```

### 20.3 Data flow per frame

```
player_position + cursor_position + plan
        │
        ├──→ planned_trace (image chains / back-prop, PLANNED mode)
        │       ▼
        │    planned steps
        │
        ├──→ physical_trace (ray casting, PHYSICAL mode)
        │         │
        │    shared cache ←──┘
        │
        ▼
  merge ──→ step_tree (5 step types, divergence index)
        │
        ▼
  visual_convert (per-step frame transform M → line or arc)
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

Surfaces are drawn in the visual frame using their defining geometry (start, via, end → derive carrier → draw_line or draw_arc) (for rendering). The math layer (§15) transforms surfaces into the normalized frame for computation — this is a separate context. No frame transform is needed for rendering — surfaces are always in visual coords.

Within the step tree preview, draw order (back to front): DIVERGED_PLANNED → DIVERGED_POST_PLANNED → ALIGNED → ALIGNED_POST_PLANNED → DIVERGED_PHYSICAL. The physical path (green/yellow) always draws on top of the planned path (red).

---

## 21. Arrow flight and animation

### 21.1 Time freeze

When the player fires, **game time freezes**. All physics, surface movement, and timers stop. The shot is logically instant — the full physical trace is computed before animation begins.

**Godot mechanism:** use `get_tree().paused = true`. The arrow animator node has `process_mode = PROCESS_MODE_ALWAYS` so it continues running during pause. All other game nodes use the default process mode and freeze automatically. During arrow flight, the preview is **hidden** — only the arrow animation is shown. After the shot completes, the tree is unpaused and the preview reappears for the retained plan.

**Animation skip:** the player can skip the flight animation by pressing any non-movement key during flight. Skip instantly completes the animation: all remaining state changes are applied, all visual state changes are applied, and the camera jumps back to the player. Movement keys (WASD) pressed during flight are ignored — they do not queue movement or trigger skip. Fire during animation only skips — it does not queue a new shot. A new shot requires a separate Fire press after the animation completes and game time unfreezes.

### 21.2 Animation

The arrow is animated step-by-step along the computed `TracedPath`:

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
| Max total hits per arrow path | **32** | All hits count (including pass-through). Prevents infinite loops. |
| Inversion circle = carrier | Always | No separate inversion-circle parameter. |
| Min segment length | > 0 | Zero-length segments never hit. Validated at load. |
| Frame determinant | ≠ 0 | Möbius matrix must be invertible. Validated on composition. |
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

A target is hit if the physical trace produces a `HitRecord` whose `surface.is_target == true`. The target hit is registered at the moment of contact (when the HitRecord is created), **regardless of the surface's effect**:

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

> **Implementation status:** Phases 0–5 are complete. Phase 6 onward is planned. The TDD documents map phases to numbered stages — see TDD_01 through TDD_08 for details. Stages 44–51 were repurposed during development; see TDD_05 for the reconciliation table.

### Phase 0: Core abstractions *(Done)*
- `Direction`, `Ray`, `Segment` data classes (§8).
- `GeneralizedCircle` class with fields `(a, b, c, d)` — method implementations (`is_line()`, `center()`, `radius()`, `contains_point()`) are added in Phase 1.
- `MobiusTransform` struct with `conjugating` flag, complex number helpers (§5.2, §5.3).
- `Surface`, `SideConfig` (§9). *(ConfigResolver deferred to Phase 8.)*
- `Effect` base with `Kind` enum, `TransformativeEffect`, `TerminalEffect` (§10.1). *(ProjectiveEffect deferred to Phase 6.)*
- `HitRecord`, `Step`, `StepTree` data structures (§11.2, §14).
- `GameState`, `CheckpointData` data structures (§18). *(LevelData deferred to Phase 8.)*
- `TransformCache` with provenance-keyed lookup (§17).

**Deliverable:** all core types compile, have correct field definitions, and can be instantiated. No algorithms — just the type system skeleton that subsequent phases build on.

### Phase 1: Math foundations *(Done)*
- `GeneralizedCircle` class with `is_line()`, `center()`, `radius()`, `contains_point()`.
- `MobiusTransform` class with apply, compose, invert, transform_carrier.
- `Segment` class with start, end, via (carrier derived via cache), side determination.
- `Direction` and `Ray` classes.
- `intersect_line_with_gcircle()` — the single intersection primitive.
- Unit tests for all math classes.

**Deliverable:** math layer passes unit tests for intersection, transform composition, and carrier transformation.

### Phase 2: Basic effects and ray tracing *(Done)*
- `TransformativeEffect` (reflection only), `TerminalEffect` (block).
- `Surface` and `SideConfig` classes.
- `trace()` function — the physical ray tracing loop.
- `Step` and `TracedPath` classes.
- Frame tracking (identity only — no inversions yet).
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
- `Planner` — image chain method for transformative sub-chains.
- Plan construction input: click to add, right-click to remove.
- Step tree: planned vs physical paths, divergence detection.
- Preview shows aligned/diverged colors.

**Deliverable:** player can build plans, see the ideal path, and observe divergence when the plan doesn't match the physical trace.

### Phase 5: Circle inversion and arc visualization *(Done)*
- `CircleInversionEffect` (transformative).
- Frame transform composition (non-identity frames).
- `VisualConverter` extended: math→visual now produces arc segments.
- `PathRenderer` extended: draws arcs via `draw_arc()`.
- Beyond-infinity hit handling.

**Deliverable:** circle inversion works. Arrow paths curve in the visual frame. Arcs render correctly.

### Phase 6: Projective effects and mixed planning *(Planned)*
- `LineNormalProjection`, `CircleNormalProjection`, `SemicircleDirectionalProjection`.
- Back-propagation for each projective effect.
- Mixed chain planning algorithm.
- Frame reset on projective effects.
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
- Computation cache implementation and validation.
- Manual override testing.
- Edge case fixes.

**Deliverable:** all invariant tests pass. Performance meets frame budget.

**Phase dependencies:** each phase depends on all prior phases. Phase 7 (visibility) requires Phase 6 (all effects). Phase 8 (game loop) requires Phase 7. No phases are independent enough to parallelize safely.

---

## 29. Testing strategy

**Testing framework:** use GUT (Godot Unit Test) as the test framework. Invariant sweeps run as GUT test cases that iterate over the position grid programmatically.

### 29.1 Math layer unit tests

- Intersection: all carrier types (line-line, line-circle), boundary cases, degenerate cases.
- Möbius transforms: composition, inversion, apply-to-point, transform-carrier.
- Cache: round-trip exactness, manual overrides, provenance-keyed lookup.

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

**S1. Cache: carrier ↔ via round-trip.** `derive_via(start, end, derive_carrier(start, end, via))` returns the exact original `via` (same Point ID). *(§17.2, Principle 22.)*

**S2. Cache: transform round-trip.** For every point P transformed by effect E: applying E's inverse returns a Point with the same ID as P. *(§17.)* **Tested as:** `BACK_TRANSFORM_ALIGNMENT`.

**S3. Determinism.** Same inputs → identical step trees (same count, types, Point IDs, frame IDs). *(Principle 17.)*

**S4. Divergence is monotonic.** Once divergence occurs at index `i`, all steps at `≥ i` are diverged types. No re-convergence. *(§14.2.)* **Tested as:** `SINGLE_DIVERGENCE`.

**S5. Aligned steps share provenance.** For ALIGNED/ALIGNED_POST_PLANNED steps, planned and physical have identical `start.id` and `frame_id`. *(§14.8.)*

**S6. Aligned steps match.** Before divergence: same hit surface ID, side, and frame ID. Checked by provenance, not coordinates. *(§14.8.)* **Tested as:** `PLAN_EFFECTS_APPLIED`.

**S7. Per-entry state matches.** *(PLANNED — depends on state changes §10.6.)* For aligned steps: planner's `state_at[i]` equals the physical trace's game state at step `i`.

**S8. Forward-first hit ordering.** Selected hit has smallest `t > 0`. If no `t > 0`: most negative `t < 0`. *(§11.3.)* **Tested as:** `PARAMETER_MONOTONICITY`, `PHYSICS_COMPLIANCE`.

**S9. Exclusion respected.** The `skip_segment` segment is excluded from the hit result. *(§11.4, §12.1.)* **Tested as:** `ORIGIN_NOT_REHIT`.

**S10. Projective resets frame.** *(PLANNED — depends on projective effects §10.4.)* After every projective hit, `frame_id == IDENTITY_ID`.

**S11. Three points on carrier.** Start/end/via evaluate to ≈0 under the carrier equation. *(Derivation validation.)* **Tested as:** `HITPOINT_ON_CARRIER`.

**S12. Side determination consistent.** LEFT/RIGHT at every hit matches the cross-product formula (§9.2). *(§9.2.)* **Tested as:** `ON_SEGMENT_CONSISTENCY`.

**S13. Visibility: no self-intersection.** *(PLANNED — depends on visibility system §15.)* Region boundaries do not self-intersect.

**S14. Visibility: edges on geometry.** *(PLANNED — depends on visibility system §15.)* Every polygon edge lies on a surface carrier or a ray from the origin.

**S15. Visibility: non-overlapping.** *(PLANNED — depends on visibility system §15.)* Distinct regions do not overlap.

**S16. No NaN/Inf in output.** No NaN/Inf in any coordinate field (except the `Vector2(INF, INF)` escape sentinel). *(§31.)* **Tested as:** `S16`.

**S17. Provenance IDs unique.** Every Point has a unique ID. *(§8.1.)*

**S18. Frame determinant non-zero.** Every MobiusTransform has `|αδ - βγ|² > 0`. *(§23.)* **Tested as:** `S18_FRAME_DETERMINANT`.

**S19. Trace preserves real state.** *(PLANNED — depends on state changes §10.6.)* After a preview trace, `GameState.flags` is unchanged.

#### Additional implemented invariants

The following invariants are tested by the sweep but not in the original specification:

| Check name | Description |
|------------|-------------|
| `PREVIEW_NOGAPS` | No gaps between consecutive step endpoints in the preview path |
| `PHYSICAL_CONTINUITY` | Step endpoints connect seamlessly (end of step N = start of step N+1) |
| `SHARED_RAY` | All steps within a stage reference the same Ray object |
| `RAY_ALIGNMENT` | Ray direction is preserved across steps within a transformative sub-chain |
| `ARC_MIDPOINT_ALIGNMENT` | Arc step midpoints lie on the arc's carrier circle |
| `POST_INVERSION_ARC` | Steps after circle inversion correctly produce arcs (not lines) |
| `VIA_ON_ARC` | Step via points lie on the step's arc carrier |
| `VISUAL_ON_PHYSICAL_CARRIER` | Visual segment points lie on the physical carrier |
| `DIRECTION_ONLY` | Only the direction (not position) changes after a plan bounce |

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
- **Spatial indexing**: broad-phase rejection of distant surfaces.
- **Frame transform caching**: avoid recomputing `M⁻¹` for unchanged frames.

---

## 31. Stability and numerical requirements

### 31.1 Direction representation

Rays use two-point `Direction` — no stored angles, no unit vectors. The `Direction` is immutable and shared across all steps of a transformative sub-chain. Generalized circle coefficients are derived from points only when needed.

### 31.2 Ordering

- Cross-product sign against a fixed reference direction. Never `atan2`.
- Earliest hit by smallest ray parameter `t`. Never by distance from an unrelated point.
- Collinear points ordered by precomputed provenance, not recomputed from coordinates.
- Ties broken by surface ID, then provenance rank. *(Deterministic, total order.)*

**Cross-products over angles:** all internal geometric decisions (arc containment, winding direction, radial ordering, sweep direction) use cross-product sign tests, never atan2. Angles appear only at the Godot API boundary (`draw_arc()` parameters) and are computed from points at the last possible moment during visual conversion (§7.3).

### 31.3 No epsilon decisions

Do not use `|x| < ε` for decisions that change topology (which hit wins, on-segment, vertex order). Epsilon logic varies with scale and platform. Prefer structural or provenance facts.

**Collinearity in particular:** whether a segment is a line or arc is determined by **construction provenance**, not by coordinate comparison. A segment is a line if its three defining points were placed collinearly by the level editor (stored as a construction fact in the serialized cache) or if the via is `Vector2(INF, INF)`. At runtime, no floating-point collinearity test is performed — the carrier type (line vs circle) is a stored decision, loaded from the serialized cache.

**Specific replacements (v0.7.0):** All six epsilon usages in the core math layer have been replaced:

| Location | Was | Now |
|----------|-----|-----|
| `projective_sort` | `absf(t) < 1e-12` | `t == 0.0` (origin is constructed with exactly 0.0) |
| `at_which_endpoint` | `distance_to < 0.01` | `point == segment.start.coords` (exact equality) |
| `endpoint_blocked_sides` | `absf(cross) < 1e-9` | `cross == 0.0` (exact sign test) |
| `maps_lines_to_arcs` | `cmod2(c) > 1e-20` | `c != Vector2.ZERO` (exact zero check) |
| Tracer zero-length step | `distance_to < 0.01` | `vis_start == vis_end` (exact equality) |
| `VisualConverter.is_arc` | `distance_squared_to < 1e-10` | `start == end_v` (exact equality) |

Testing-layer tolerances (e.g., in `invariant_checker.gd`) are unaffected — they measure rendered pixel positions, not topology-changing decisions.

#### 31.3.1 Three-tier endpoint detection

When intersecting a ray with a segment, the intersection system uses a three-tier provenance chain to detect whether a hit occurs at a segment endpoint. This replaces the former distance-based `at_which_endpoint` check.

**Tier 1 — Exact coordinate match (provenance):** Compare each segment endpoint coordinate against the ray's defining points (`ray.origin.coords`, `ray.direction.start.coords`, `ray.direction.end.coords`). If `endpoint_coords == ray_defining_point`, the endpoint is detected. This catches structural coincidences where the ray was constructed from the same defining points as the segment — the most common case (player at a vertex, reflection point at an adjacent segment's endpoint).

**Tier 2 — Structural collinearity:** Compute `cross(endpoint - ray.origin, ray.direction)`. If the cross product is exactly `0.0`, the endpoint is collinear with the ray and is detected. This catches cases where the endpoint is structurally on the ray line but was not a defining point of the ray. The `== 0.0` test is deterministic — not an epsilon band.

**Tier 3 — Quadratic solver:** The general ray-carrier intersection via the quadratic formula (§11.1). This produces approximate floating-point hit coordinates. Hits from this tier get `at_endpoint = 0` (interior hit).

**Provenance snapping:** When Tier 1 or 2 detects an endpoint, the nearest quadratic hit (if one was found) is snapped to the exact endpoint coordinates and the exact `t`-value from projecting the endpoint onto the ray. The `at_endpoint` field is set to 1 (start) or 2 (end). This eliminates floating-point drift at structurally significant points.

**Unmatched endpoint hits:** If a Tier 1/2 detected endpoint has no nearby quadratic hit (e.g., ray lies on the carrier — the quadratic has no isolated intersection), it is emitted as an additional HitRecord. This handles ray-on-carrier edge cases where the quadratic solver returns empty.

**Blockage at endpoints:** When a hit occurs at an endpoint (`at_endpoint > 0`), the tracer examines the tangent direction into the segment relative to the ray direction to determine `blocked_left` and `blocked_right`. At non-endpoint interior hits, both sides are blocked. At endpoints, only the side where the segment extends is blocked. Multiple segments sharing an endpoint accumulate their blocked sides independently (§12.1).

### 31.4 Provenance

When the system classifies a point (on-segment, which side, intersection index), that classification is stored and reused. Recomputation from coordinates can contradict the original decision.

### 31.5 Frame composition stability

Mitigations:
- 32-effect limit bounds chain length.
- Cache stores intermediate results (no recomputation through full chain).
- Projective effects reset the frame, breaking long chains.
- Manual overrides enforce exact cycle identities.

### 31.6 Degenerate cases

| Case | Resolution |
|------|-----------|
| Inversion of a point at the center | Maps to ∞. Step escapes. Beyond-infinity ordering applies. |
| Inversion of a line through the center | Result is a line. Carrier stays a line in normalized frame. |
| Zero rotation and displacement | Identity — valid no-op. |
| Frame determinant → 0 | Cannot occur with valid effects. Validated on composition. |
| Parallel ray and line surface | No intersection. |
| Origin on a surface | Exclude surface from current cast. |

### 31.7 Consistency

- Trajectory and visibility use the same intersection pipeline, same scene state.
- Planning and physical tracing use the same effect implementations.
- Projective back-propagation and forward application are consistent.
- Screen boundaries are surfaces, not special cases. *(Principle 7.)*

### 31.8 Error handling policy

In **debug/development** builds: crash with a descriptive message on violated invariants (invalid frame determinant, carrier-type mismatch, NaN in output). Early failure surfaces bugs before they compound.

In **release** builds: log the error and recover gracefully — skip the problematic step, continue the trace, and display a visual indicator that something went wrong. The game should never hard-crash during gameplay.

---

## 32. Terminology

| Term | Definition |
|------|-----------|
| **Point** | 2D position with provenance (reason for creation). |
| **Provenance** | The stored reason a point was created — reused, not recomputed. |
| **Direction** | Immutable two-point line orientation. Shared across transformative sub-chain steps. |
| **Ray** | Origin + Direction. Always a line in the normalized frame. |
| **Generalized circle** | A line or circle, unified by the equation `a(x²+y²) + bx + cy + d = 0`. A derived value — computed from a segment's three points and cached bidirectionally. |
| **Carrier** | The unbounded generalized circle a segment lives on. Derived from the segment's three points, not stored. |
| **Segment** | Three points — start, end, via — that fully define a bounded path and its carrier. |
| **Via** | Any point on the segment between start and end. The point the path passes through — disambiguates which arc/path is intended. `INF` for segments through infinity. Recoverable exactly from a cached carrier via reverse derivation. |
| **Collision body** | A `StaticBody2D` with `CollisionShape2D` used for player physics only. Invisible to arrow tracing, visibility, and planning. Not a Surface. Defined in `LevelData.collision_bodies`. |
| **Surface** | Segment + per-side effect configuration + stable ID. |
| **Side** | LEFT or RIGHT, determined by traversal direction (start → via/end). Uniform for all segments. |
| **Plan** | Ordered list of `{surface, side}` entries the player intends the arrow to hit. Each entry specifies which surface and which side. |
| **Hit** | Where a ray meets a surface. Stored as a HitRecord with parameter, point, surface, side, provenance. |
| **Step** | One leg of a path: start → end, with frame and hit info. |
| **Step chain** | Ordered list of steps forming a continuous path. |
| **Step tree** | All paths for one shot sharing a prefix, with explicit branch points at divergence. |
| **Divergence** | Where planned and physical paths split. A first-class event, not a rendering trick. |
| **Normalized frame** | Standard Euclidean plane where all math happens. Rays are always lines. |
| **Visual frame** | The coordinate system the player sees. Rays may be arcs. |
| **Frame transform** | Möbius transformation mapping normalized → visual. Per-step, updated on transformative effects, reset on projective. |
| **Transformative effect** | Fully invertible Möbius transformation. Supports image-chain planning. |
| **Projective effect** | One-way effect where outgoing ray depends only on hit point. Supports back-propagation planning. |
| **Terminal effect** | Block — stops the ray. |
| **Compound effect** | Multiple transformative effects composed into one Möbius matrix. |
| **Rigid motion** | Rotation + translation (Euclidean isometry excluding reflections). |
| **Transformative sub-chain** | A contiguous sequence of plan entries with transformative effects, bounded by projective break points (or the start/end of the plan). Solved as a unit using image chains. |
| **Break point** | A projective surface that resets the frame and partitions the plan into transformative sub-chains. |
| **Image** | Virtual point from reflecting/inverting a real point through surfaces — used to "unfold" bounces into straight lines. |
| **Back-propagation** | Finding the hit point on a projective surface given a target on the outgoing side. |
| **Beyond-infinity hit** | A hit on the back side of a ray's great circle, reached by passing through ∞. |
| **Pass-through** | A surface side with no effect. The ray continues as if the surface is absent on that side. |
| **Arc chain** | Open sequence of connected segments. Used for paths. |
| **Generalized polygon** | Closed chain of segments with line and/or arc edges. |
| **Visibility region set** | Zero or more polygons representing valid aim regions. May be disconnected. |
| **Scene state** | Scene geometry + current game state. What the simulation sees. |
| **Computation cache** | Provenance-keyed bidirectional store ensuring exact round-trip reversal. |
| **Manual override** | Cache entry set by the level designer to enforce exact algebraic identities. |

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

**Current version: 2.4.0.** See [CHANGELOG.md](CHANGELOG.md) for revision history.
