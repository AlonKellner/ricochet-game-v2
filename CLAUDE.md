# Ricochet Game v2

## Stack

- **Engine**: Godot 4.7+ (GDScript glue over a native C++ core, GDExtension)
- **Testing**: GUT 9.6.0 (Godot Unit Test); doctest/ctest for the native core; pytest for the mpmath oracle
- **IDE**: VSCode

## Architecture

Three-layer architecture with strict dependency direction: Math -> Visual -> Game.

- `scripts/math/` — Geometric primitives and algorithms. **Zero Godot dependencies beyond Vector2.** Classes must extend `RefCounted`, `Object`, or have no `extends`. Never `Node`, `Node2D`, `Control`, or any scene-tree type.
- `scripts/math/effects/` — Effect definitions (reflection, inversion, rigid motion, projective): kind, self-inverse, display. The beam maps are native (`rc/beam_effects.hpp`).
- `native/core/include/rc/` — The native core, header-only C++. The trajectory solver is the only solver: `solver.hpp` is the rule walker (spec: `docs/research/oracle/trace2.py`), `ladder.hpp` the dynamic precision, `tracer.hpp` turns events into `TracePath`/`TraceStep`, `exact.hpp` and `bigfloat.hpp` the exact predicates and arithmetic. Decisions use provenance and exact predicates, never epsilons. `invariants.hpp` and `sweep.hpp` are the invariant kernel; `visibility.hpp` and `visibility_checks.hpp` the visibility region and its invariants.
- `native/game/include/rcg/` — The engine-free game layer (Stage 115 on, the engine-agnostic port): the level data (`level.hpp`: `rc-level/1`, `docs/LEVEL_FORMAT.md`), its builder and scene defs (`build.hpp`), the surface model (`model.hpp`), crossings and pieces (`geom.hpp`), the landing (`landing.hpp`), the world (`world.hpp`), the player's movement (`kinematics.hpp`), the game session (`session.hpp`: `rcg::Session`, byte-identical to the GDScript game on the session golden), the session digest (`digest.hpp`), the sweep manifests (`sweeps.hpp`), the canonical JSON spelling (`canon.hpp`). Tests: `rcg_tests` (`make check AREA=game`, incl. `rcg_sessions_*`).
- `native/hosts/headless/` — `rc_tool`, the headless host: `fmt [--check]`, `dumps` (`make dumps`), `manifests` (`make manifests`, from `data/sweeps.json`), `replay` (the sessions natively).
- `native/gd/` — The GDExtension bindings (`BeamTracer`, `BeamSolver`, `InvariantKernel`, `SweepKernel`, `VisibilityRegion`, `RcgLevel`, ...).
- `data/levels/` — The levels (JSON, the source of truth; a scene points at its data with `level_path`). `data/sessions/` — the session scripts of the replay golden (`bench/sessions`, `docs/research/session_digest.md`).
- `scripts/visual/` — Rendering via `_draw()` and `queue_redraw()` (the visibility fill: `visibility_renderer.gd`).
- `scripts/game/` — Scene-dependent game logic (player, surfaces, game manager).
- `scripts/editor/` — Level editor scripts.

## Testing

The protocol: `docs/WORKFLOW.md` (the three gates, the three build trees, the data pipelines).

```bash
# The inner loop: the rc_tests binary alone (no godot-cpp), then ctest entries of an area
make check                      # every entry, ~18 s after the build
make check AREA=vis             # solver | vis | goldens | sweeps | game | all
make check TC="visibility digest" SKIP=0   # one doctest filter (--no-skip with SKIP=0)

# Before every commit (core, oracle, ctest, the extension, the dumps/manifests/sessions current, GUT,
# goldens unchanged; timed summary)
make gate

# Before a merge to main or a phase close: the same plus ASan+UBSan
make gates

# The pieces
make core && make ctest         # rc_tests (native/build-core) and its ctest
make native                     # the extension Godot loads (native/build -> native/bin), import if needed
make native-asan                # rc_tests under ASan+UBSan (native/build-asan)
make oracle-test                # the mpmath oracle's pytest
./run_tests_parallel.sh         # the GUT suite, sweeps sharded over parallel processes
make test-stage STAGE=3         # one stage's GUT files
make sessions-check             # the session replay golden (make sessions re-records: a behaviour change)
```

- Test files: `tests/test_stage{N}_{topic}.gd`
- All tests extend `GutTest`
- Test methods: `func test_{description}():`
- `TestTrace` (`scripts/testing/test_trace.gd`) runs the beam solver behind the old `Tracer.trace` signature, for behavioural tests.
- **Regression policy**: After Stage N, ALL tests from Stages 1 through N must pass before proceeding.

## Stage Workflow

Each stage follows the feedback loop protocol (the gates and targets: `docs/WORKFLOW.md`):

1. Spec first: `GAME_SPEC.md`, then the oracle (`docs/research/oracle`) and its tests, then the fixtures (`make regen`)
2. Native: the kernel and its tests green under `make check`, then `make gate` before each commit (small green commits on a branch)
3. Bindings, GUT, visuals
4. Goldens: `make judge`, the user's approval of the report, `make rebless APPROVED=1`
5. Docs: the stage's TDD doc (phase table), CHANGELOG, GAME_SPEC's version; `make gates` closes each phase
6. User performs interactive tests and gives pass/fail feedback
7. Convert failing interactive tests into automated tests; fix and re-run until all pass
8. Stage complete

**No stage is complete without user sign-off on interactive tests.** A changed golden, region digest or
fixture is a behaviour change: bring the evidence (the judge's report) before applying it.

## Code Style

- GDScript conventions, `snake_case` for variables/functions, `PascalCase` for classes
- Type hints where possible
- Immutable data classes extend `RefCounted`
- Error handling: debug builds assert+crash on invariant violations; release builds log+recover
- Complex numbers represented as `Vector2(real, imag)`
- Coordinate system: Y-down, X-right (Godot default), 1 unit = 1 pixel

## Key References

- `GAME_SPEC.md` — Single source of truth for game design (25 core principles in section 2)
- `docs/TDD_01_FOUNDATION.md` through `docs/TDD_08_EDITOR_AND_POLISH.md` — Stage-by-stage implementation guide (TDD_06 is history)
- `docs/TDD_09_PRECISION_INTEGRATION.md` through `docs/TDD_14_VISIBILITY.md` — The beam solver, the native core, hit effects, crossings, spec import, visibility
