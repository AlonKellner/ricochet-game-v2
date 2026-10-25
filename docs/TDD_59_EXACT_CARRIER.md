# TDD 59: Exact carriers (Stage 154)

The exact carrier deferred in TDD_53. Stage 149 made carriers an authoring construct: the lowering projects an
element's points onto its carrier and rounds them to float32. A float32 point lies exactly on a declared line or
circle only by luck, so two arcs on one declared circle are, to the solver's exact predicates (`same_carrier`:
`incircle_zero`, `orient_zero`), on two circles a rounding apart. Principle 23 (designer intent) wants the one
circle. The rule is in GAME_SPEC 19.1 ("Exact carriers").

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 154.0 | The scope: a solver-level carrier with symbolic points (TDD_53) against points exactly on the carrier | Done: the lattice, below |
| 154.1 | `rcg/lattice.hpp`: a line's lattice (a grid point, a primitive direction with components up to 1024), a circle's (a grid center, N = r^2 / h^2 with the most representations as X^2 + Y^2 within half a pixel, every (X, Y) by angle; cached per circle, about 10-20 ms) | Done |
| 154.2 | The lowering: `exact` on a carrier; projections, an arc's default via and an explicit via onto the lattice; an exact circle holds arcs only | Done |
| 154.3 | The editor's `exact` row; the demo level `objectives/ring`; tests; docs; `make gate` | Done |
| 154.4 | The user's interactive sign-off | Done (the user's sign-off 2026-10-25) |

## Decisions

- **Points exactly on the carrier instead of symbolic points.** The solver decides corners and shared points by
  identity, so a carrier is exact only if every point on it is exactly on it. TDD_53's way is symbolic points (a
  point as the carrier's parameter, exact at every tier): a change to the solver's inputs, the oracle, the pieces,
  the controller, the drawing and the dumps. Float32 points that lie exactly on a line or a circle exist, and with
  them nothing downstream changes. The elements on an exact carrier are ordinary surfaces whose points happen to be
  exactly collinear or concyclic, which the exact predicates see.
- **Lines.** A float32 point is exactly on the line through A in the direction h(q, p) when it is A + k h (q, p)
  with A on the grid h. The direction snaps to the nearest primitive (q, p) with components up to 1024 (an angle
  within about 1e-6 rad); its points are a fraction of a pixel apart.
- **Circles.** With the center C on the grid h, a point C + h (X, Y) is exactly on the circle of radius^2 = N h^2
  when X^2 + Y^2 = N. The number of such points is 4 times the product of (e + 1) over N's primes = 1 mod 4 (none
  when a prime = 3 mod 4 has an odd power). The radius moves by at most half a pixel to an N rich in such primes:
  N = P k, P a product of distinct primes = 1 mod 4, k in the window and itself of small such primes, 2s and
  squares. The points cluster (the solutions' symmetry), so the largest gap grows with the radius:

  | radius | points | largest gap |
  |---|---|---|
  | 40 | 1024 | 0.8 px |
  | 150 | 1536 | 2.7 px |
  | 400 | 2048 | 4.6 px |
  | 900 | 2304 | 11.5 px |

- **h is the finest grid float32 holds** at the level's magnitudes (2^-13 for coordinates under 2048), so every
  lattice point is a float32 value and N fits 64 bits.
- **An exact circle holds arcs only.** A whole circle is declared by a float64 center and radius; sqrt(N) h is not
  one in general, so a whole circle would be a rounding off its arcs. An element without from and to on an exact
  circle is an error.
- **Crossings of two exact carriers are not exact (v1).** A point on two lattices exists only by luck. The provenance
  form `{"cross": [c1, c2]}` stays a later option (symbolic points again).
- **Opt-in.** Exact carriers snap points by up to a few pixels, which a designer may not want; `exact` is off by
  default, and no level changes.

## Tests

`rcg_tests` "lattice: ..." (5): a circle's points are float32 values with X^2 + Y^2 = N, near the radius, any four
exactly concyclic (the largest gaps reported); a line's points are float32 values exactly collinear, near their
projections; elements on an exact circle and an exact line: every arc's points on one circle exactly, the ring
closing on shared points, the walls exactly collinear, and without `exact` not so; the errors (a whole exact circle,
an end landing on the other, `exact` not a bool); the demo level (the hatch on the ring opened, the button inside
pressed off the mirror, the goal reached). The session golden gains `objectives__ring__play`.
