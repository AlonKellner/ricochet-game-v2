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
- `native/gd/` — The GDExtension bindings (`BeamTracer`, `BeamSolver`, `InvariantKernel`, `SweepKernel`, `VisibilityRegion`, ...).
- `scripts/visual/` — Rendering via `_draw()` and `queue_redraw()` (the visibility fill: `visibility_renderer.gd`).
- `scripts/game/` — Scene-dependent game logic (player, surfaces, game manager).
- `scripts/editor/` — Level editor scripts.

## Testing

The protocol: `docs/WORKFLOW.md` (the three gates, the three build trees, the data pipelines).

```bash
# The inner loop: the rc_tests binary alone (no godot-cpp), then ctest entries of an area
make check                      # every entry, ~18 s after the build
make check AREA=vis             # solver | vis | goldens | all
make check TC="visibility digest" SKIP=0   # one doctest filter (--no-skip with SKIP=0)

# Before every commit (core, oracle, ctest, the extension, GUT, goldens unchanged; timed summary)
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
```

- Test files: `tests/test_stage{N}_{topic}.gd`
- All tests extend `GutTest`
- Test methods: `func test_{description}():`
- `TestTrace` (`scripts/testing/test_trace.gd`) runs the beam solver behind the old `Tracer.trace` signature, for behavioural tests.
- **Regression policy**: After Stage N, ALL tests from Stages 1 through N must pass before proceeding.

## Stage Workflow

Each stage follows the feedback loop protocol:

1. Implement stage (code + unit tests)
2. Run all unit tests (new + all prior) — all must pass
3. User performs interactive tests
4. User provides pass/fail feedback
5. Convert failing interactive tests into automated tests
6. Fix and re-run — repeat until all pass
7. Stage complete

**No stage is complete without user sign-off on interactive tests.**

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
