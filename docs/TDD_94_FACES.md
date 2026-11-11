# TDD 94: Inversion, projection and portal as faces (Stage 216)

Your request (2026-11-11): plan and do the queue -- its last item, inversion, projection and portal as faces of the
surface element; your choice: included, last in the queue. The plan you approved (2026-11-11): Stages 213-220.

The rules are GAME_SPEC 9.13 (the face, the Stage 216 note).

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 216.1 | `face`/`back` take `invert`, `project` and `portal`, with `projection`, `reverse`, `normal`, `d`, `theta`; plannable by default | Done |
| 216.2 | `decompose` reads them back (one set of a face's parameters for every side) | Done |
| 216.3 | inversion, projection and portal presets; a projection's `type` now `projection` (11 elements rewritten) | Done |
| 216.4 | Migrate weighs what a kind does twice; hit_effects' portal spawner reads back as a spawner (no longer `raw`) | Done |
| 216.5 | The editor: six face chips, a face's parameters under the faces, `d` scaled for any portal face | Done |
| 216.6 | Goldens: none move (lowerings byte-identical, the session golden unchanged) | Done |
| 216.7 | The user's interactive sign-off | Done ("Works perfectly! Proceed", 2026-11-11) |

## Decisions

**A face's parameters are the element's.** A projecting face reads `projection` (the four effect types, as the
template's `type`: the composer does not see the geometry, the builder checks the carrier), `reverse` and, when
directional, `normal`; a portal `d` and `theta`. Both sides read the same ones, and `decompose` refuses sides whose
parameters differ (two projections either way round).

**Plannable by what the face does.** The templates made these sides interactive; the rule now: a face other than stop
and pass is plannable unless `plannable` says otherwise (a mirror already was).

**A portal is one portal.** The builder pairs a surface whose sides are the same portal, with no resolver: face and
back both `portal` (or back unset), and no `when` or `armed` -- errors name the element.

**Labels.** inversion: `FullInversion` while both sides invert, else `Inversion`; projection: by `projection` and
`reverse` as before; portal: `Portal` (the builder names the pair PortalSrc / PortalTgt).

**What migrate prefers.** With these faces a shield can be an inversion (face invert) and a shield can spawn: a run
with an overriding label (`Spec`) tied between a kind writing its face and one writing its body. The score now counts
what a kind does -- a face, a feature it is not about -- twice and its body or plannable once: an inversion with body
pass, a spawner with face stop. hit_effects' raw portal spawner (a surface no element said before) migrates to a
spawner with face portal; the level's file says so now (its lowering unchanged).

## Tests

- `rcg_tests`, `test_surface_element.cpp`: the equivalence grid now 28 kinds (inversion, projection, portal against
  their templates over every value); the read-back grid over the new faces (each projection, either way, a portal);
  "as faces": a blocking projection that is a button, a portal target, a portal without `d`, with a stop back, with
  `when` refused.
- `test_editor.cpp`: a face's parameters under the faces (a projection's, a directional one's normal, a portal's).
- `make lowered dumps`: no diff; the session golden unchanged.
